/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "server.h"
#include <math.h>

/*-----------------------------------------------------------------------------
 * Hash type API
 *----------------------------------------------------------------------------*/

/* Check the length of a number of objects to see if we need to convert a
 * ziplist to a real hash. Note that we only check string encoded objects
 * as their string length can be queried in constant time. */
/*
检查多个对象的长度，看看是否需要将ziplist转换为真正的hash表。注意，我们只检查被encode完的对象，因为它们的字符串长度可以在常量时间内查询到。
*/

// typedef struct redisObject {
//     unsigned type:4;
//     unsigned encoding:4;
//     unsigned lru:LRU_BITS=24; /* LRU time (relative to global lru_clock) or
//                             * LFU data (least significant 8 bits frequency
//                             * and most significant 16 bits access time). */
//     int refcount;
//     void *ptr;
// } robj;
//看看返回的o中的encoding就知道argv数组中的start到end下标中可被encoding的数据可不可以被转换为ziplist，不能则会将o转化为dict对象
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {
    int i;
    //压缩类型不是ziplist
    if (o->encoding != OBJ_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {
        //如果数据类型不是原始类型或者sds，并且数据的长度超过了ziplist的最大长度
        if (sdsEncodedObject(argv[i]) &&
            sdslen(argv[i]->ptr) > server.hash_max_ziplist_value)
        {
            //将o转化为hashtable
            hashTypeConvert(o, OBJ_ENCODING_HT);
            break;
        }
    }
}

/* Get the value from a ziplist encoded hash, identified by field.
 * Returns -1 when the field cannot be found. */
//在ziplst中获取值为field的后一个元素，无法被用数字表的存在**vstr，可以用数字表示的用*vll
//找到返回0，否则为-1
int hashTypeGetFromZiplist(robj *o, sds field,
                           unsigned char **vstr,
                           unsigned int *vlen,
                           long long *vll)
{
    unsigned char *zl, *fptr = NULL, *vptr = NULL;
    int ret;

    serverAssert(o->encoding == OBJ_ENCODING_ZIPLIST);

    zl = o->ptr;
    //获取ziplist的第一个元素地址
    fptr = ziplistIndex(zl, ZIPLIST_HEAD);
    if (fptr != NULL) {
        //间隔为1的跳跃查找
        fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
        if (fptr != NULL) {
            /* Grab pointer to the value (fptr points to the field) */
            //fptr的下一个元素就是value的值
            vptr = ziplistNext(zl, fptr);
            serverAssert(vptr != NULL);
        }
    }
    //第一个都找不到直接不处理
    if (vptr != NULL) {
        //获取vptr存的值
        ret = ziplistGet(vptr, vstr, vlen, vll);
        serverAssert(ret);
        return 0;
    }

    return -1;
}

/* Get the value from a hash table encoded hash, identified by field.
 * Returns NULL when the field cannot be found, otherwise the SDS value
 * is returned. */
//在dict中查找key为field的值
sds hashTypeGetFromHashTable(robj *o, sds field) {
    dictEntry *de;

    serverAssert(o->encoding == OBJ_ENCODING_HT);

    de = dictFind(o->ptr, field);
    if (de == NULL) return NULL;
    return dictGetVal(de);
}

/* Higher level function of hashTypeGet*() that returns the hash value
 * associated with the specified field. If the field is found C_OK
 * is returned, otherwise C_ERR. The returned object is returned by
 * reference in either *vstr and *vlen if it's returned in string form,
 * or stored in *vll if it's returned as a number.
 *
 * If *vll is populated *vstr is set to NULL, so the caller
 * can always check the function return by checking the return value
 * for C_OK and checking if vll (or vstr) is NULL. */
//在o中查询key为field的值，数据存在**vstr或者*vll
int hashTypeGetValue(robj *o, sds field, unsigned char **vstr, unsigned int *vlen, long long *vll) {
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {//如果是ziplist
        *vstr = NULL;
        if (hashTypeGetFromZiplist(o, field, vstr, vlen, vll) == 0)//如果查到数据在vstr或vll
            return C_OK;
    } else if (o->encoding == OBJ_ENCODING_HT) {//如果为dict
        sds value;
        //获取的是entry中的v.val
        if ((value = hashTypeGetFromHashTable(o, field)) != NULL) {
            //需要将value转为sds类型
            *vstr = (unsigned char*) value;
            //获取字符串长度
            *vlen = sdslen(value);
            return C_OK;
        }
    } else {
        serverPanic("Unknown hash encoding");
    }
    return C_ERR;
}

/* Like hashTypeGetValue() but returns a Redis object, which is useful for
 * interaction with the hash type outside t_hash.c.
 * The function returns NULL if the field is not found in the hash. Otherwise
 * a newly allocated string object with the value is returned. */
robj *hashTypeGetValueObject(robj *o, sds field) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vll;
    //是否可在o中找打key为field的值
    if (hashTypeGetValue(o,field,&vstr,&vlen,&vll) == C_ERR) return NULL;
    //返回的值存在*vstr或者vll
    if (vstr) return createStringObject((char*)vstr,vlen);
    else return createStringObjectFromLongLong(vll);
}

/* Higher level function using hashTypeGet*() to return the length of the
 * object associated with the requested field, or 0 if the field does not
 * exist. */
//获取field为key的value，只支持ziplist和dict
size_t hashTypeGetValueLength(robj *o, sds field) {
    size_t len = 0;
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0)
            len = vstr ? vlen : sdigits10(vll);
    } else if (o->encoding == OBJ_ENCODING_HT) {
        sds aux;

        if ((aux = hashTypeGetFromHashTable(o, field)) != NULL)
            len = sdslen(aux);
    } else {
        serverPanic("Unknown hash encoding");
    }
    return len;
}

/* Test if the specified field exists in the given hash. Returns 1 if the field
 * exists, and 0 when it doesn't. */
//判断robj元素中的key为field的元素是否存在，只适用ziplist和dict
int hashTypeExists(robj *o, sds field) {
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0) return 1;
    } else if (o->encoding == OBJ_ENCODING_HT) {
        if (hashTypeGetFromHashTable(o, field) != NULL) return 1;
    } else {
        serverPanic("Unknown hash encoding");
    }
    return 0;
}

/* Add a new field, overwrite the old with the new value if it already exists.
 * Return 0 on insert and 1 on update.
 *
 * By default, the key and value SDS strings are copied if needed, so the
 * caller retains ownership of the strings passed. However this behavior
 * can be effected by passing appropriate flags (possibly bitwise OR-ed):
 *
 * HASH_SET_TAKE_FIELD -- The SDS field ownership passes to the function.
 * HASH_SET_TAKE_VALUE -- The SDS value ownership passes to the function.
 *
 * When the flags are used the caller does not need to release the passed
 * SDS string(s). It's up to the function to use the string to create a new
 * entry or to free the SDS string before returning to the caller.
 *
 * HASH_SET_COPY corresponds to no flags passed, and means the default
 * semantics of copying the values if needed.
 *
 */
#define HASH_SET_TAKE_FIELD (1<<0)//1
#define HASH_SET_TAKE_VALUE (1<<1)//2
#define HASH_SET_COPY 0
//在o的数据中插入键值对field-value,field有就替换没有就插入，根据flag看是不是需要另外申请空间去替换元素
int hashTypeSet(robj *o, sds field, sds value, int flags) {
    int update = 0;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr, *vptr;

        zl = o->ptr;//获取数据
        //定位到ziplist的首数据首地址
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) {//没有元素
            //查找field在ziplist中是否存在
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) {//找到
                /* Grab pointer to the value (fptr points to the field) */
                //获取这个元素的下一个元素，k-v就是v
                vptr = ziplistNext(zl, fptr);
                serverAssert(vptr != NULL);
                update = 1;

                /* Delete value */
                //删除这个元素
                zl = ziplistDelete(zl, &vptr);

                /* Insert new value */
                //在这个元素的位置将新元素插入
                zl = ziplistInsert(zl, vptr, (unsigned char*)value,
                        sdslen(value));
            }
        }
        //如果有field代表数据被更新
        if (!update) {//没有找到这个field，没有这个key
            //在ziplist的尾部插入新元素k-v
            /* Push new field/value pair onto the tail of the ziplist */
            zl = ziplistPush(zl, (unsigned char*)field, sdslen(field),
                    ZIPLIST_TAIL);
            zl = ziplistPush(zl, (unsigned char*)value, sdslen(value),
                    ZIPLIST_TAIL);
        }
        //重新申请的数据空间赋值过去
        o->ptr = zl;

        /* Check if the ziplist needs to be converted to a hash table */
        //如果这个ziplist的元素超过了设置的上限需要将他转化为dict
        if (hashTypeLength(o) > server.hash_max_ziplist_entries)
            hashTypeConvert(o, OBJ_ENCODING_HT);
    } else if (o->encoding == OBJ_ENCODING_HT) {//类型为dict
        dictEntry *de = dictFind(o->ptr,field);//不用dictscan是因为field确定hash就确定
        if (de) {//如果找到了
            //删除这个sds的内存空间
            sdsfree(dictGetVal(de));
            //如果是替换的value，根据flag处理value
            if (flags & HASH_SET_TAKE_VALUE) {//设置value，需要将value的值丢失
                dictGetVal(de) = value;
                value = NULL;
            } else {//设置value但是不影响原先的value
                dictGetVal(de) = sdsdup(value);
            }
            update = 1;
        } else {//如果没有找到
            sds f,v;
            //判断是不是需要将原先的指针给释放
            if (flags & HASH_SET_TAKE_FIELD) {
                f = field;
                field = NULL;
            } else {
                f = sdsdup(field);
            }
            if (flags & HASH_SET_TAKE_VALUE) {
                v = value;
                value = NULL;
            } else {
                v = sdsdup(value);
            }
            //增加新的键值对
            dictAdd(o->ptr,f,v);
        }
    } else {
        serverPanic("Unknown hash encoding");
    }

    /* Free SDS strings we did not referenced elsewhere if the flags
     * want this function to be responsible. */
    //如果设置field或者key的时候需要释放但是没有释放传过来的k-v就释放
    if (flags & HASH_SET_TAKE_FIELD && field) sdsfree(field);
    if (flags & HASH_SET_TAKE_VALUE && value) sdsfree(value);
    //返回更新标记
    return update;
}

/* Delete an element from a hash.
 * Return 1 on deleted and 0 on not found. */
int hashTypeDelete(robj *o, sds field) {
    //设置删除标志
    int deleted = 0;
    //ziplist的数据删除
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr;

        zl = o->ptr;
        //定位到ziplist第一个元素
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) {//存在元素
        //查找ziplist是否有这个元素
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) {//如果没有这个元素
                //删除key代表的元素
                zl = ziplistDelete(zl,&fptr); /* Delete the key. */
                //删除value代表的元素
                zl = ziplistDelete(zl,&fptr); /* Delete the value. */
                //数据重新赋值
                o->ptr = zl;
                //成功删除标志
                deleted = 1;
            }
        }
    } else if (o->encoding == OBJ_ENCODING_HT) {
        //删除dict两张hash表中key为field的元素
        if (dictDelete((dict*)o->ptr, field) == C_OK) {
            deleted = 1;

            /* Always check if the dictionary needs a resize after a delete. */
            //判断是否需要resize
            if (htNeedsResize(o->ptr)) dictResize(o->ptr);
        }

    } else {
        serverPanic("Unknown hash encoding");
    }
    return deleted;
}

/* Return the number of elements in a hash. */
//获取o元素的k-v数量，只适用ziplist和dict
unsigned long hashTypeLength(const robj *o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        //获取的是k+v的数量所以/2
        length = ziplistLen(o->ptr) / 2;
    } else if (o->encoding == OBJ_ENCODING_HT) {
        //计算dict两个hash表中被使用的个数和
        length = dictSize((const dict*)o->ptr);
    } else {
        serverPanic("Unknown hash encoding");
    }
    return length;
}

//初始化hash表的迭代器，用于数据检索
hashTypeIterator *hashTypeInitIterator(robj *subject) {
    //申请空间存放对象
    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));
    //原始数据记录
    hi->subject = subject;
    //记录转化到hashtable之前的encoding方式
    hi->encoding = subject->encoding;
    //如果为ziplist
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {//filed/value对于ziplist不存在
        hi->fptr = NULL;
        hi->vptr = NULL;
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        //初始化dict的iter
        hi->di = dictGetIterator(subject->ptr);
    } else {
        serverPanic("Unknown hash encoding");
    }
    return hi;
}

//释放迭代器
void hashTypeReleaseIterator(hashTypeIterator *hi) {
    if (hi->encoding == OBJ_ENCODING_HT)
        dictReleaseIterator(hi->di);
    zfree(hi);
}

/* Move to the next entry in the hash. Return C_OK when the next entry
 * could be found and C_ERR when the iterator reaches the end. */
//获取hi指向元素的下一个元素，使用hi内部存储的变量记录当前的迭代元素位置，可用于ziplist和dict，返回是否被检索到
int hashTypeNext(hashTypeIterator *hi) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {//查看encoding为ziplist
        unsigned char *zl;
        unsigned char *fptr, *vptr;
        //ziplist的数据
        zl = hi->subject->ptr;
        //都为空
        fptr = hi->fptr;
        vptr = hi->vptr;

        if (fptr == NULL) {
            /* Initialize cursor */
            serverAssert(vptr == NULL);
            //fptr定位到第一个元素位置
            fptr = ziplistIndex(zl, 0);
        } else {
            /* Advance cursor */
            serverAssert(vptr != NULL);
            //获取下一个元素的首地址
            fptr = ziplistNext(zl, vptr);
        }
        //没有元素了
        if (fptr == NULL) return C_ERR;

        /* Grab pointer to the value (fptr points to the field) */
        vptr = ziplistNext(zl, fptr);
        serverAssert(vptr != NULL);

        /* fptr, vptr now point to the first or next pair */
        //fptr为next第一个元素，vptr为next第二个元素
        hi->fptr = fptr;
        hi->vptr = vptr;
    } else if (hi->encoding == OBJ_ENCODING_HT) {//dict的检索
        if ((hi->de = dictNext(hi->di)) == NULL) return C_ERR;
    } else {
        serverPanic("Unknown hash encoding");
    }
    return C_OK;
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a ziplist. Prototype is similar to `hashTypeGetFromZiplist`. */
//ziplist中存的数据是以k-v的形式存在的，需要这个去读取
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    serverAssert(hi->encoding == OBJ_ENCODING_ZIPLIST);
    //ziplist中的key
    if (what & OBJ_HASH_KEY) {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        serverAssert(ret);
    } else {//ziplist中的value
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        serverAssert(ret);
    }
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a hash table. Prototype is similar to
 * `hashTypeGetFromHashTable`. */
//获取dict当前这个iter指向的entry的key或者value
sds hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what) {
    serverAssert(hi->encoding == OBJ_ENCODING_HT);

    if (what & OBJ_HASH_KEY) {
        return dictGetKey(hi->de);
    } else {
        return dictGetVal(hi->de);
    }
}

/* Higher level function of hashTypeCurrent*() that returns the hash value
 * at current iterator position.
 *
 * The returned element is returned by reference in either *vstr and *vlen if
 * it's returned in string form, or stored in *vll if it's returned as
 * a number.
 *
 * If *vll is populated *vstr is set to NULL, so the caller
 * can always check the function return by checking the return value
 * type checking if vstr == NULL. */
//获取当前迭代器的指向元素的key或者value的值，只适用于ziplist和dict
void hashTypeCurrentObject(hashTypeIterator *hi, int what, unsigned char **vstr, unsigned int *vlen, long long *vll) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {//ziplist的压缩方式
        *vstr = NULL;
        //这边只能获取到vll
        hashTypeCurrentFromZiplist(hi, what, vstr, vlen, vll);
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        sds ele = hashTypeCurrentFromHashTable(hi, what);
        *vstr = (unsigned char*) ele;
        *vlen = sdslen(ele);
    } else {
        serverPanic("Unknown hash encoding");
    }
}

/* Return the key or value at the current iterator position as a new
 * SDS string. */
//返回当前的key或者value，值为一个新的sds对象
sds hashTypeCurrentObjectNewSds(hashTypeIterator *hi, int what) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vll;

    hashTypeCurrentObject(hi,what,&vstr,&vlen,&vll);
    if (vstr) return sdsnewlen(vstr,vlen);
    //将long long转化为sds
    return sdsfromlonglong(vll);
}

//查找redis这个db中是否有这个key，没有就创建一个hash类型的key，如果有并且不是hash类型的返回类型失败
robj *hashTypeLookupWriteOrCreate(client *c, robj *key) {
    //返回redis这个db中是否有这个key
    robj *o = lookupKeyWrite(c->db,key);
    if (o == NULL) {
        //如果不存在则创建，这边创建的是ziplist的压缩方式
        o = createHashObject();
        dbAdd(c->db,key,o);
    } else {
        //如果这个key的类型不是hash
        if (o->type != OBJ_HASH) {//返回类型失败
            addReply(c,shared.wrongtypeerr);
            return NULL;
        }
    }
    return o;
}

//将ziplist转化为dict
void hashTypeConvertZiplist(robj *o, int enc) {
    //校验压缩类型
    serverAssert(o->encoding == OBJ_ENCODING_ZIPLIST);
    //如果压缩类型为ziplst
    if (enc == OBJ_ENCODING_ZIPLIST) {
        /* Nothing to do... */

    } else if (enc == OBJ_ENCODING_HT) {//如果需要转化为hashtable
        /*
        typedef struct {
            robj *subject;
            int encoding;
            unsigned char *fptr, *vptr;
            dictIterator *di;
            dictEntry *de;
        } hashTypeIterator;
        */
        hashTypeIterator *hi;
        /*
        typedef struct dict {
            dictType *type;
            void *privdata;
            dictht ht[2];
            long rehashidx; //rehashing not in progress if rehashidx == -1 
            unsigned long iterators; // number of iterators currently running 
        } dict;
        */
        dict *dict;
        int ret;
        //如果encoding为ziplist，则只是记录原始数据和原始的encoding方式
        hi = hashTypeInitIterator(o);
        //初始一个dict
        dict = dictCreate(&hashDictType, NULL);
        //如果能找到下一个元素
        while (hashTypeNext(hi) != C_ERR) {
            sds key, value;

            key = hashTypeCurrentObjectNewSds(hi,OBJ_HASH_KEY);
            value = hashTypeCurrentObjectNewSds(hi,OBJ_HASH_VALUE);
            //ziplist的数据为k-v结构
            ret = dictAdd(dict, key, value);
            if (ret != DICT_OK) {//失败打印
                serverLogHexDump(LL_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                serverPanic("Ziplist corruption detected");
            }
        }
        //释放hi
        hashTypeReleaseIterator(hi);
        //释放ziplist，已经将数据都放入dict中
        zfree(o->ptr);
        //修改encoding方式
        o->encoding = OBJ_ENCODING_HT;
        //数据改为新的dict
        o->ptr = dict;
    } else {
        serverPanic("Unknown hash encoding");
    }
}

//将各种类型转化为dict
void hashTypeConvert(robj *o, int enc) {
    //如果需要转化的压缩类型为ziplist
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o, enc);
    } else if (o->encoding == OBJ_ENCODING_HT) {
        serverPanic("Not implemented");
    } else {
        serverPanic("Unknown hash encoding");
    }
}

/*-----------------------------------------------------------------------------
 * Hash type commands
 *----------------------------------------------------------------------------*/
//设置field当且仅当这个field不存在
void hsetnxCommand(client *c) {
    robj *o;
    //如果找到的这个对象类型不是hash
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    //将参数的2-3转化为ziplist或者dict
    hashTypeTryConversion(o,c->argv,2,3);
    //如果存在这个key
    if (hashTypeExists(o, c->argv[2]->ptr)) {
        //回包修改个数为0
        /*类似这种返回
        127.0.0.1:6379[1]> hset bb 1 1 2 2 3 3
        (integer) 0
        */
        addReply(c, shared.czero);
    } else {
        //不存在则插入
        hashTypeSet(o,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_COPY);
        //回包修改一个
        /*
        127.0.0.1:6379[1]> hset bb 1 1 2 2 3 3
        (integer) 1
        */
        addReply(c, shared.cone);
        signalModifiedKey(c,c->db,c->argv[1]);
        //记录在redis指定db下的操作
        notifyKeyspaceEvent(NOTIFY_HASH,"hset",c->argv[1],c->db->id);
        server.dirty++;
    }
}

//hash set
/*
hset hmset走的是同一条路子
如果用hset返回的是插入的新的k-v个数
用hmset则是ok
*/
void hsetCommand(client *c) {
    //创建的个数
    int i, created = 0;
    robj *o;
    //查看如果使用hmset参数个数对不对
    if ((c->argc % 2) == 1) {
        addReplyError(c,"wrong number of arguments for HMSET");
        return;
    }
    //如果找到的这个对象类型不是hash
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    //转化args
    hashTypeTryConversion(o,c->argv,2,c->argc-1);
    //一个个插入到对象中
    for (i = 2; i < c->argc; i += 2)
        //计算新key的个数
        created += !hashTypeSet(o,c->argv[i]->ptr,c->argv[i+1]->ptr,HASH_SET_COPY);

    /* HMSET (deprecated) and HSET return value is different. */
    char *cmdname = c->argv[0]->ptr;
    //判断指令是hset还是hmset
    if (cmdname[1] == 's' || cmdname[1] == 'S') {
        /* HSET */
        addReplyLongLong(c, created);
    } else {
        /* HMSET */
        addReply(c, shared.ok);
    }
    signalModifiedKey(c,c->db,c->argv[1]);
    //统一的都是hset
    notifyKeyspaceEvent(NOTIFY_HASH,"hset",c->argv[1],c->db->id);
    server.dirty++;
}

//HINCRBY key field increment
/*
为redis哈希表key中的域field的值加上增量increment。
field中的值如果不能被转化为longlong或者增加以后的值超出longlong表示范围都会失败
增量也可以为负数，相当于对给定域进行减法操作。
返回给客户端新的值。
*/
void hincrbyCommand(client *c) {
    long long value, incr, oldvalue;
    robj *o;
    sds new;
    unsigned char *vstr;
    unsigned int vlen;
    //如果值increment无法被longlong表示
    if (getLongLongFromObjectOrReply(c,c->argv[3],&incr,NULL) != C_OK) return;
    //判断redis的key类型是不是hash
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    //查看hash表中是否存在这个field
    if (hashTypeGetValue(o,c->argv[2]->ptr,&vstr,&vlen,&value) == C_OK) {
        if (vstr) {//如果有元素
            if (string2ll((char*)vstr,vlen,&value) == 0) {//判断是否值为longlong
                addReplyError(c,"hash value is not an integer");
                return;
            }
        } /* Else hashTypeGetValue() already stored it into &value */
    } else {//没有设置默认值0
        value = 0;
    }
    //记录老的值
    oldvalue = value;
    //判断是否在加完或者减完之后的新值会越界longlong能代表的最大最小值
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) {
        addReplyError(c,"increment or decrement would overflow");
        return;
    }
    //修改value
    value += incr;
    //生成新的sds
    new = sdsfromlonglong(value);
    //设置新的值
    hashTypeSet(o,c->argv[2]->ptr,new,HASH_SET_TAKE_VALUE);
    //返回值为新的值
    addReplyLongLong(c,value);
    signalModifiedKey(c,c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"hincrby",c->argv[1],c->db->id);
    server.dirty++;
}

//HINCRBYFLOAT key field increment
/*
为哈希表 key 中的域 field 加上浮点数增量 increment 。
如果哈希表中没有域 field ，那么 HINCRBYFLOAT 会先将域 field 的值设为 0 ，然后再执行加法操作。
如果键 key 不存在，那么 HINCRBYFLOAT 会先创建一个哈希表，再创建域 field ，最后再执行加法操作。
当以下任意一个条件发生时，返回一个错误：
域 field 的值不是字符串类型(因为 redis 中的数字和浮点数都以字符串的形式保存，所以它们都属于字符串类型）
域 field 当前的值或给定的增量 increment 不能解释(parse)为双精度浮点数(double precision floating point number)
*/
void hincrbyfloatCommand(client *c) {
    long double value, incr;
    long long ll;
    robj *o;
    sds new;
    unsigned char *vstr;
    unsigned int vlen;
    //校验参数是否正确
    if (getLongDoubleFromObjectOrReply(c,c->argv[3],&incr,NULL) != C_OK) return;
    //查看操作的redis的key的type是否正确
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    //和HINCRBY一个流程
    if (hashTypeGetValue(o,c->argv[2]->ptr,&vstr,&vlen,&ll) == C_OK) {
        if (vstr) {
            if (string2ld((char*)vstr,vlen,&value) == 0) {
                addReplyError(c,"hash value is not a float");
                return;
            }
        } else {
            value = (long double)ll;
        }
    } else {
        value = 0;
    }

    value += incr;
    if (isnan(value) || isinf(value)) {
        addReplyError(c,"increment would produce NaN or Infinity");
        return;
    }

    char buf[MAX_LONG_DOUBLE_CHARS];
    int len = ld2string(buf,sizeof(buf),value,LD_STR_HUMAN);
    new = sdsnewlen(buf,len);
    hashTypeSet(o,c->argv[2]->ptr,new,HASH_SET_TAKE_VALUE);
    addReplyBulkCBuffer(c,buf,len);
    //信号：键值已经改变了。调用touchWatchedKey(db,key)
    signalModifiedKey(c,c->db,c->argv[1]);
    //发送键空间通知和键事件通知
    notifyKeyspaceEvent(NOTIFY_HASH,"hincrbyfloat",c->argv[1],c->db->id);
    server.dirty++;

    /* Always replicate HINCRBYFLOAT as an HSET command with the final value
     * in order to make sure that differences in float pricision or formatting
     * will not create differences in replicas or after an AOF restart. */
    /*
    始终将HINCRBYFLOAT复制为带有最终值的HSET命令，以确保浮点精度或格式的差异不会在副本或AOF重新启动后产生差异。
    */
    robj *aux, *newobj;
    aux = createStringObject("HSET",4);
    newobj = createRawStringObject(buf,len);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,3,newobj);
    decrRefCount(newobj);
}

//在o中获取key为field的value，放入client的reply
static void addHashFieldToReply(client *c, robj *o, sds field) {
    int ret;
    //value为空
    if (o == NULL) {
        addReplyNull(c);
        return;
    }
    //根据o的encoding方式解析数据
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;
        //获取field后一个元素value
        ret = hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll);
        if (ret < 0) {//未找到
            addReplyNull(c);
        } else {//返回实际值
            if (vstr) {
                addReplyBulkCBuffer(c, vstr, vlen);
            } else {
                addReplyBulkLongLong(c, vll);
            }
        }

    } else if (o->encoding == OBJ_ENCODING_HT) {
        sds value = hashTypeGetFromHashTable(o, field);
        if (value == NULL)
            addReplyNull(c);
        else
            addReplyBulkCBuffer(c, value, sdslen(value));
    } else {
        serverPanic("Unknown hash encoding");
    }
}

//hget key
void hgetCommand(client *c) {
    robj *o;
    //判断这个key是否存在，或者类型不对
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.null[c->resp])) == NULL ||
        checkType(c,o,OBJ_HASH)) return;
    //o存的就是value的数据
    addHashFieldToReply(c, o, c->argv[2]->ptr);
}

//hmget
void hmgetCommand(client *c) {
    robj *o;
    int i;

    /* Don't abort when the key cannot be found. Non-existing keys are empty
     * hashes, where HMGET should respond with a series of null bulks. */
    //类型检测
    o = lookupKeyRead(c->db, c->argv[1]);
    if (o != NULL && o->type != OBJ_HASH) {
        addReply(c, shared.wrongtypeerr);
        return;
    }
    //记录key数量
    addReplyArrayLen(c, c->argc-2);
    //将数据写入回包
    for (i = 2; i < c->argc; i++) {
        addHashFieldToReply(c, o, c->argv[i]->ptr);
    }
}

//hdel key field
void hdelCommand(client *c) {
    robj *o;
    int j, deleted = 0, keyremoved = 0;
    //校验类型
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_HASH)) return;
    //可以一次性删除多个
    for (j = 2; j < c->argc; j++) {
        if (hashTypeDelete(o,c->argv[j]->ptr)) {
            deleted++;
            //如果全部删除了
            if (hashTypeLength(o) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }
    //如果删除了key
    if (deleted) {
        signalModifiedKey(c,c->db,c->argv[1]);
        //操作记录
        notifyKeyspaceEvent(NOTIFY_HASH,"hdel",c->argv[1],c->db->id);
        if (keyremoved)
            notifyKeyspaceEvent(NOTIFY_GENERIC,"del",c->argv[1],
                                c->db->id);
        server.dirty += deleted;
    }
    //返回的是删除的key数量
    addReplyLongLong(c,deleted);
}

//hlen key
//返回的是key中field的数量
void hlenCommand(client *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_HASH)) return;

    addReplyLongLong(c,hashTypeLength(o));
}

//hstrlen key field
//获取value的长度
void hstrlenCommand(client *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_HASH)) return;
    addReplyLongLong(c,hashTypeGetValueLength(o,c->argv[2]->ptr));
}

static void addHashIteratorCursorToReply(client *c, hashTypeIterator *hi, int what) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);
        if (vstr)
            addReplyBulkCBuffer(c, vstr, vlen);
        else
            addReplyBulkLongLong(c, vll);
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        sds value = hashTypeCurrentFromHashTable(hi, what);
        addReplyBulkCBuffer(c, value, sdslen(value));
    } else {
        serverPanic("Unknown hash encoding");
    }
}

//hgetall/hkeys/hvalue
void genericHgetallCommand(client *c, int flags) {
    robj *o;
    hashTypeIterator *hi;
    int length, count = 0;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymap[c->resp]))
        == NULL || checkType(c,o,OBJ_HASH)) return;

    /* We return a map if the user requested keys and values, like in the
     * HGETALL case. Otherwise to use a flat array makes more sense. */
    //获取k-v长度
    length = hashTypeLength(o);
    //根据标志位获取key或则value字段
    if (flags & OBJ_HASH_KEY && flags & OBJ_HASH_VALUE) {
        addReplyMapLen(c, length);
    } else {
        addReplyArrayLen(c, length);
    }
    //初始化迭代器
    hi = hashTypeInitIterator(o);
    //一直迭代知道最后一个元素
    while (hashTypeNext(hi) != C_ERR) {
        //根据标志位在reply构建回包
        if (flags & OBJ_HASH_KEY) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_KEY);
            count++;
        }
        if (flags & OBJ_HASH_VALUE) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_VALUE);
            count++;
        }
    }
    //释放迭代器
    hashTypeReleaseIterator(hi);

    /* Make sure we returned the right number of elements. */
    //确保count正确，在hgetall的时候count会计算两遍
    if (flags & OBJ_HASH_KEY && flags & OBJ_HASH_VALUE) count /= 2;
    serverAssert(count == length);
}

//hkeys key
void hkeysCommand(client *c) {
    genericHgetallCommand(c,OBJ_HASH_KEY);
}

//hvals key
void hvalsCommand(client *c) {
    genericHgetallCommand(c,OBJ_HASH_VALUE);
}

//hgetall key
void hgetallCommand(client *c) {
    genericHgetallCommand(c,OBJ_HASH_KEY|OBJ_HASH_VALUE);
}

//hexists field key
void hexistsCommand(client *c) {
    robj *o;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_HASH)) return;

    addReply(c, hashTypeExists(o,c->argv[2]->ptr) ? shared.cone : shared.czero);
}

//hscan key 0
//遍历hash表，每次返回一个值，返回0代表遍历结束
void hscanCommand(client *c) {
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == C_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,OBJ_HASH)) return;
    scanGenericCommand(c,o,cursor);
}
