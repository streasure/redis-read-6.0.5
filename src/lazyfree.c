#include "server.h"
#include "bio.h"
#include "atomicvar.h"
#include "cluster.h"

static size_t lazyfree_objects = 0;
pthread_mutex_t lazyfree_objects_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Return the number of currently pending objects to free. */
size_t lazyfreeGetPendingObjectsCount(void) {
    size_t aux;
    atomicGet(lazyfree_objects,aux);
    return aux;
}

/* Return the amount of work needed in order to free an object.
 * The return value is not always the actual number of allocations the
 * object is compoesd of, but a number proportional to it.
 *
 * For strings the function always returns 1.
 *
 * For aggregated objects represented by hash tables or other data structures
 * the function just returns the number of elements the object is composed of.
 *
 * Objects composed of single allocations are always reported as having a
 * single item even if they are actually logical composed of multiple
 * elements.
 *
 * For lists the function returns the number of elements in the quicklist
 * representing the list. */
//根据obj的类型获取这个数据实际占用的数据大小
size_t lazyfreeGetFreeEffort(robj *obj) {
    if (obj->type == OBJ_LIST) {
        quicklist *ql = obj->ptr;
        return ql->len;
    } else if (obj->type == OBJ_SET && obj->encoding == OBJ_ENCODING_HT) {
        dict *ht = obj->ptr;
        return dictSize(ht);
    } else if (obj->type == OBJ_ZSET && obj->encoding == OBJ_ENCODING_SKIPLIST){
        zset *zs = obj->ptr;
        return zs->zsl->length;
    } else if (obj->type == OBJ_HASH && obj->encoding == OBJ_ENCODING_HT) {
        dict *ht = obj->ptr;
        return dictSize(ht);
    } else {
        return 1; /* Everything else is a single allocation. *///空数据为指针，占用一字节
    }
}

/* Delete a key, value, and associated expiration entry if any, from the DB.
 * If there are enough allocations to free the value object may be put into
 * a lazy free list instead of being freed synchronously. The lazy free list
 * will be reclaimed in a different bio.c thread. */
#define LAZYFREE_THRESHOLD 64
//异步删除这个key
int dbAsyncDelete(redisDb *db, robj *key) {
    /* Deleting an entry from the expires dict will not free the sds of
     * the key, because it is shared with the main dictionary. */
    //如果db中的过期key没有直接返回
    if (dictSize(db->expires) > 0) dictDelete(db->expires,key->ptr);

    /* If the value is composed of a few allocations, to free in a lazy way
     * is actually just slower... So under a certain limit we just free
     * the object synchronously. */
    //将这个db中的key提取出来
    dictEntry *de = dictUnlink(db->dict,key->ptr);
    if (de) {//如果找到这个key
        robj *val = dictGetVal(de);//获取这个key的value
        //获取这个value占用的空间大小
        size_t free_effort = lazyfreeGetFreeEffort(val);

        /* If releasing the object is too much work, do it in the background
         * by adding the object to the lazy free list.
         * Note that if the object is shared, to reclaim it now it is not
         * possible. This rarely happens, however sometimes the implementation
         * of parts of the Redis core may call incrRefCount() to protect
         * objects, and then call dbDelete(). In this case we'll fall
         * through and reach the dictFreeUnlinkedEntry() call, that will be
         * equivalent to just calling decrRefCount(). */
        /*
        如果释放对象的工作量太大，可以在后台通过将对象添加到空闲列表来完成。
        请注意，如果对象是共享的，则现在无法回收它。这种情况很少发生，
        但是有时候Redis核心的部分实现可能会调用incrRefCount（）来保护对象，然后调用dbDelete（）。
        在本例中，我们将进行dictFreeUnlinkEntry（）调用，这相当于只调用decrefCount（）。
        */
        //如果这个值的大小超过64，并且只有自身一个引用
        if (free_effort > LAZYFREE_THRESHOLD && val->refcount == 1) {
            atomicIncr(lazyfree_objects,1);//删除个数++
            //后台创建删除任务
            bioCreateBackgroundJob(BIO_LAZY_FREE,val,NULL,NULL);
            dictSetVal(db->dict,de,NULL);//设置de位置value为空
        }
    }

    /* Release the key-val pair, or just the key if we set the val
     * field to NULL in order to lazy free it later. */
    if (de) {//找到元素
        //在调用dictUnlink（）后，需要调用此函数才能真正释放条目。使用'he'=NULL调用此函数是安全的
        //逻辑有问题，6.2.6版本已修改
        dictFreeUnlinkedEntry(db->dict,de);
        //集群中需要删除这个key
        if (server.cluster_enabled) slotToKeyDel(key->ptr);
        return 1;
    } else {
        return 0;
    }
}

/* Free an object, if the object is huge enough, free it in async way. */
void freeObjAsync(robj *o) {
    size_t free_effort = lazyfreeGetFreeEffort(o);
    if (free_effort > LAZYFREE_THRESHOLD && o->refcount == 1) {
        atomicIncr(lazyfree_objects,1);
        bioCreateBackgroundJob(BIO_LAZY_FREE,o,NULL,NULL);
    } else {
        decrRefCount(o);
    }
}

/* Empty a Redis DB asynchronously. What the function does actually is to
 * create a new empty set of hash tables and scheduling the old ones for
 * lazy freeing. */
void emptyDbAsync(redisDb *db) {
    dict *oldht1 = db->dict, *oldht2 = db->expires;
    db->dict = dictCreate(&dbDictType,NULL);
    db->expires = dictCreate(&keyptrDictType,NULL);
    atomicIncr(lazyfree_objects,dictSize(oldht1));
    bioCreateBackgroundJob(BIO_LAZY_FREE,NULL,oldht1,oldht2);
}

/* Empty the slots-keys map of Redis CLuster by creating a new empty one
 * and scheduiling the old for lazy freeing. */
void slotToKeyFlushAsync(void) {
    rax *old = server.cluster->slots_to_keys;

    server.cluster->slots_to_keys = raxNew();
    memset(server.cluster->slots_keys_count,0,
           sizeof(server.cluster->slots_keys_count));
    atomicIncr(lazyfree_objects,old->numele);
    bioCreateBackgroundJob(BIO_LAZY_FREE,NULL,NULL,old);
}

/* Release objects from the lazyfree thread. It's just decrRefCount()
 * updating the count of objects to release. */
void lazyfreeFreeObjectFromBioThread(robj *o) {
    decrRefCount(o);
    atomicDecr(lazyfree_objects,1);
}

/* Release a database from the lazyfree thread. The 'db' pointer is the
 * database which was substitutied with a fresh one in the main thread
 * when the database was logically deleted. 'sl' is a skiplist used by
 * Redis Cluster in order to take the hash slots -> keys mapping. This
 * may be NULL if Redis Cluster is disabled. */
void lazyfreeFreeDatabaseFromBioThread(dict *ht1, dict *ht2) {
    size_t numkeys = dictSize(ht1);
    dictRelease(ht1);
    dictRelease(ht2);
    atomicDecr(lazyfree_objects,numkeys);
}

/* Release the skiplist mapping Redis Cluster keys to slots in the
 * lazyfree thread. */
void lazyfreeFreeSlotsMapFromBioThread(rax *rt) {
    size_t len = rt->numele;
    raxFree(rt);
    atomicDecr(lazyfree_objects,len);
}
