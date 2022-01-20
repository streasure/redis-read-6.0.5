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

/*-----------------------------------------------------------------------------
 * List API
 *----------------------------------------------------------------------------*/

/* The function pushes an element to the specified list object 'subject',
 * at head or tail position as specified by 'where'.
 *
 * There is no need for the caller to increment the refcount of 'value' as
 * the function takes care of it if needed. */
//在subject的quicklist中按照where插入value
void listTypePush(robj *subject, robj *value, int where) {
    //只适用与quicklist
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        //判断位置是头还是尾
        int pos = (where == LIST_HEAD) ? QUICKLIST_HEAD : QUICKLIST_TAIL;
        //增加引用计数，将int转化为string等操作
        value = getDecodedObject(value);
        //获取value的len
        size_t len = sdslen(value->ptr);
        //根据pos插入到quicklist的头或者尾
        quicklistPush(subject->ptr, value->ptr, len, pos);
        //减少引用计数
        decrRefCount(value);
    } else {
        serverPanic("Unknown list encoding");
    }
}

//获取一个值为data的robj
void *listPopSaver(unsigned char *data, unsigned int sz) {
    return createStringObject((char*)data,sz);
}

//将subject指向的quicklist中的一个元素按照where从头或者尾部去除，并创建robj存储这个值
robj *listTypePop(robj *subject, int where) {
    long long vlong;
    robj *value = NULL;
    //判断实在quicklist的head还是tail
    int ql_where = where == LIST_HEAD ? QUICKLIST_HEAD : QUICKLIST_TAIL;
    //对象必须是quicklist
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        //从quicklist中pop出一个元素
        if (quicklistPopCustom(subject->ptr, ql_where, (unsigned char **)&value,
                               NULL, &vlong, listPopSaver)) {
            if (!value)//如果数据不在value中就是存在vlong，创建新的robj
                value = createStringObjectFromLongLong(vlong);
        }
    } else {
        serverPanic("Unknown list encoding");
    }
    return value;
}

//获取这个subject中quicklist的元素个数 quicklist->count
unsigned long listTypeLength(const robj *subject) {
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        return quicklistCount(subject->ptr);
    } else {
        serverPanic("Unknown list encoding");
    }
}

/* Initialize an iterator at the specified index. */
//获取subject指向的quicklist中index位置direction方向的的迭代器
listTypeIterator *listTypeInitIterator(robj *subject, long index,
                                       unsigned char direction) {
    //初始化返回值
    listTypeIterator *li = zmalloc(sizeof(listTypeIterator));
    //初始化迭代器的变量
    li->subject = subject;
    li->encoding = subject->encoding;
    li->direction = direction;
    li->iter = NULL;
    /* LIST_HEAD means start at TAIL and move *towards* head.
     * LIST_TAIL means start at HEAD and move *towards tail. */
    //获取迭代方向为头开始还是尾开始
    int iter_direction =
        direction == LIST_HEAD ? AL_START_TAIL : AL_START_HEAD;
    //只支持quicklist
    if (li->encoding == OBJ_ENCODING_QUICKLIST) {
        //初始化这个quicklist在这个index下的iter
        li->iter = quicklistGetIteratorAtIdx(li->subject->ptr,
                                             iter_direction, index);
    } else {
        serverPanic("Unknown list encoding");
    }
    return li;
}

/* Clean up the iterator. */
//释放robj指向quicklist的迭代器
void listTypeReleaseIterator(listTypeIterator *li) {
    zfree(li->iter);
    zfree(li);
}

/* Stores pointer to current the entry in the provided entry structure
 * and advances the position of the iterator. Returns 1 when the current
 * entry is in fact an entry, 0 otherwise. */
//根据li迭代器中的信息，获取li指向的quicklist的下一个元素，并将这个元素的数据存在entry中，返回0表示没有，返回1便是数据存在
int listTypeNext(listTypeIterator *li, listTypeEntry *entry) {
    /* Protect from converting when iterating */
    //如果两个encoding不一样，后期看一下哪里做修改
    //TODO 增加修改位置的解释
    serverAssert(li->subject->encoding == li->encoding);
    //设置entry迭代器
    entry->li = li;
    if (li->encoding == OBJ_ENCODING_QUICKLIST) {
        //迭代下一个元素，数据放在entry中
        return quicklistNext(li->iter, &entry->entry);
    } else {
        serverPanic("Unknown list encoding");
    }
    return 0;
}

/* Return entry or NULL at the current position of the iterator. */
//将entry中的元素转化为robj
robj *listTypeGet(listTypeEntry *entry) {
    //构建新的robj
    robj *value = NULL;
    //只对quicklist做处理
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        //根据entry存的值做处理
        if (entry->entry.value) {//字符串放在value中
            value = createStringObject((char *)entry->entry.value,
                                       entry->entry.sz);
        } else {//数字放在longval
            value = createStringObjectFromLongLong(entry->entry.longval);
        }
    } else {
        serverPanic("Unknown list encoding");
    }
    return value;
}

//将value插入到entry指向的quicklist的头或者尾
void listTypeInsert(listTypeEntry *entry, robj *value, int where) {
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        //获取纯string的robj
        value = getDecodedObject(value);
        sds str = value->ptr;
        size_t len = sdslen(str);
        if (where == LIST_TAIL) {//判断时头还是尾
            quicklistInsertAfter((quicklist *)entry->entry.quicklist,
                                 &entry->entry, str, len);
        } else if (where == LIST_HEAD) {
            quicklistInsertBefore((quicklist *)entry->entry.quicklist,
                                  &entry->entry, str, len);
        }
        //减少引用
        decrRefCount(value);
    } else {
        serverPanic("Unknown list encoding");
    }
}

/* Compare the given object with the entry at the current position. */
//判断entry指向的quicklist中的元素是不是和robj中的元素相同
int listTypeEqual(listTypeEntry *entry, robj *o) {
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        serverAssertWithInfo(NULL,o,sdsEncodedObject(o));
        return quicklistCompare(entry->entry.zi,o->ptr,sdslen(o->ptr));
    } else {
        serverPanic("Unknown list encoding");
    }
}

/* Delete the element pointed to. */
//删除entry指向的quicklist存在entry->zi位置的元素，并更新iter的zi和node等信息，防止野指针和node不同步 
void listTypeDelete(listTypeIterator *iter, listTypeEntry *entry) {
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        //删除entry指向的元素并更新iter中有关这个quicklist的信息
        quicklistDelEntry(iter->iter, &entry->entry);
    } else {
        serverPanic("Unknown list encoding");
    }
}

/* Create a quicklist from a single ziplist */
//将subject中的ziplist转化为quicklist，enc为压缩类型，只对quicklist有作用
void listTypeConvert(robj *subject, int enc) {
    //校验subject的数据类型是不是符合转换条件
    serverAssertWithInfo(NULL,subject,subject->type==OBJ_LIST);
    serverAssertWithInfo(NULL,subject,subject->encoding==OBJ_ENCODING_ZIPLIST);
    //值转换为quicklist
    if (enc == OBJ_ENCODING_QUICKLIST) {
        //获取配置的相关信息，查看ziplist的最大长度
        size_t zlen = server.list_max_ziplist_size;
        //看压缩的深度
        int depth = server.list_compress_depth;
        //创建quicklist
        subject->ptr = quicklistCreateFromZiplist(zlen, depth, subject->ptr);
        //设置压缩方式
        subject->encoding = OBJ_ENCODING_QUICKLIST;
    } else {
        serverPanic("Unsupported list conversion");
    }
}

/*-----------------------------------------------------------------------------
 * List Commands  操作代码
 *----------------------------------------------------------------------------*/
//这个操作如果key不存在会被创建 lpush/rpush
//LPUSH languages python
void pushGenericCommand(client *c, int where) {
    int j, pushed = 0;
    //在db中查找key，没有或者过期会返回null
    robj *lobj = lookupKeyWrite(c->db,c->argv[1]);
    //判断数据类型是不是正确的list
    if (lobj && lobj->type != OBJ_LIST) {
        addReply(c,shared.wrongtypeerr);
        return;
    }
    //LPUSH languages python
    for (j = 2; j < c->argc; j++) {
        if (!lobj) {//如果没有这个key
            lobj = createQuicklistObject();
            quicklistSetOptions(lobj->ptr, server.list_max_ziplist_size,
                                server.list_compress_depth);
            //添加key
            dbAdd(c->db,c->argv[1],lobj);
        }
        //插入元素，在这边减少lobj的引用计数
        listTypePush(lobj,c->argv[j],where);
        pushed++;
    }
    //返回新的quicklist的数据长度
    addReplyLongLong(c, (lobj ? listTypeLength(lobj) : 0));
    if (pushed) {//如果正确插入了
        char *event = (where == LIST_HEAD) ? "lpush" : "rpush";
        //通知clients这个key有变动
        signalModifiedKey(c,c->db,c->argv[1]);
        //推送key的操作
        notifyKeyspaceEvent(NOTIFY_LIST,event,c->argv[1],c->db->id);
    }
    server.dirty += pushed;
}

//lpush 头部插入quicklist元素 lpush key value [value...]
void lpushCommand(client *c) {
    pushGenericCommand(c,LIST_HEAD);
}

//rpush 尾部插入quicklist元素 rpush key value [value...]
void rpushCommand(client *c) {
    pushGenericCommand(c,LIST_TAIL);
}
//这个操作只在已存在的list的做处理，key不存在直接返回
void pushxGenericCommand(client *c, int where) {
    int j, pushed = 0;
    robj *subject;
    //判断是否有这个key或者key的类型不为quicklist
    if ((subject = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,subject,OBJ_LIST)) return;
    //插入元素
    for (j = 2; j < c->argc; j++) {
        listTypePush(subject,c->argv[j],where);
        pushed++;
    }

    addReplyLongLong(c,listTypeLength(subject));

    if (pushed) {
        char *event = (where == LIST_HEAD) ? "lpush" : "rpush";
        signalModifiedKey(c,c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_LIST,event,c->argv[1],c->db->id);
    }
    server.dirty += pushed;
}

//尝试插入，key不存在不做操作 LPUSHX greet "hello"
void lpushxCommand(client *c) {
    pushxGenericCommand(c,LIST_HEAD);
}
//尝试插入，key不存在不做操作 RPUSHX greet "hello"
void rpushxCommand(client *c) {
    pushxGenericCommand(c,LIST_TAIL);
}

//LINSERT key BEFORE|AFTER pivot value
/*
将值 value 插入到列表 key 当中，位于值 pivot 之前或之后。
当 pivot 不存在于列表 key 时，不执行任何操作。
当 key 不存在时， key 被视为空列表，不执行任何操作。
如果 key 不是列表类型，返回一个错误。
*/
void linsertCommand(client *c) {
    int where;
    robj *subject;
    listTypeIterator *iter;
    listTypeEntry entry;
    int inserted = 0;
    //判断是在前插还是后插
    if (strcasecmp(c->argv[2]->ptr,"after") == 0) {
        where = LIST_TAIL;
    } else if (strcasecmp(c->argv[2]->ptr,"before") == 0) {
        where = LIST_HEAD;
    } else {//返回错误
        addReply(c,shared.syntaxerr);
        return;
    }
    //检测key的合法性
    if ((subject = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,subject,OBJ_LIST)) return;

    /* Seek pivot from head to tail */
    //获取这个quicklist方向向后的头节点的iter
    iter = listTypeInitIterator(subject,0,LIST_TAIL);
    while (listTypeNext(iter,&entry)) {//迭代数据
        if (listTypeEqual(&entry,c->argv[3])) {//是否元素相同
            listTypeInsert(&entry,c->argv[4],where);//插入指定位置
            inserted = 1;
            break;
        }
    }
    //释放迭代器
    listTypeReleaseIterator(iter);
    //插入成功
    if (inserted) {
        //通知监听这个key的client
        signalModifiedKey(c,c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_LIST,"linsert",
                            c->argv[1],c->db->id);
        server.dirty++;
    } else {
        /* Notify client of a failed insert */
        addReplyLongLong(c,-1);
        return;
    }
    //返回总长度
    addReplyLongLong(c,listTypeLength(subject));
}

//LLEN key 获取这个quicklist的数据个数
void llenCommand(client *c) {
    robj *o = lookupKeyReadOrReply(c,c->argv[1],shared.czero);
    if (o == NULL || checkType(c,o,OBJ_LIST)) return;
    addReplyLongLong(c,listTypeLength(o));
}

//返回列表 key 中，下标为 index 的元素。
//LINDEX key index
void lindexCommand(client *c) {
    //判断是否有这个key
    robj *o = lookupKeyReadOrReply(c,c->argv[1],shared.null[c->resp]);
    //如果找不到reply会被赋值null
    if (o == NULL || checkType(c,o,OBJ_LIST)) return;
    long index;
    robj *value = NULL;
    //判断index是否可被转化为longlong
    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != C_OK))
        return;
    //数据为quicklist
    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklistEntry entry;
        //将index下的元素取出放到entry中
        if (quicklistIndex(o->ptr, index, &entry)) {
            if (entry.value) {//根据返回entry的值将value转化为string还是数字
                value = createStringObject((char*)entry.value,entry.sz);
            } else {
                value = createStringObjectFromLongLong(entry.longval);
            }
            addReplyBulk(c,value);
            //取出引用计数
            decrRefCount(value);
        } else {
            addReplyNull(c);
        }
    } else {
        serverPanic("Unknown list encoding");
    }
}

//将列表 key 下标为 index 的元素的值设置为 value 。
//当 index 参数超出范围，或对一个空列表( key 不存在)进行 LSET 时，返回一个错误。
//LSET key index value
void lsetCommand(client *c) {
    robj *o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr);
    if (o == NULL || checkType(c,o,OBJ_LIST)) return;
    long index;
    robj *value = c->argv[3];

    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != C_OK))
        return;

    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklist *ql = o->ptr;//获取这个key所在的quicklist
        //数据替换
        int replaced = quicklistReplaceAtIndex(ql, index,
                                               value->ptr, sdslen(value->ptr));
        if (!replaced) {//替换失败
            addReply(c,shared.outofrangeerr);
        } else {
            addReply(c,shared.ok);
            //通知监听这个key的client数据有变动
            signalModifiedKey(c,c->db,c->argv[1]);
            notifyKeyspaceEvent(NOTIFY_LIST,"lset",c->argv[1],c->db->id);
            server.dirty++;//服务修改次数++
        }
    } else {
        serverPanic("Unknown list encoding");
    }
}

// 移除并返回列表 key 的头/尾元素。
void popGenericCommand(client *c, int where) {
    //判断key存在与否
    robj *o = lookupKeyWriteOrReply(c,c->argv[1],shared.null[c->resp]);
    if (o == NULL || checkType(c,o,OBJ_LIST)) return;
    //获取头部或者尾部的一个值
    robj *value = listTypePop(o,where);
    if (value == NULL) {
        addReplyNull(c);
    } else {
        char *event = (where == LIST_HEAD) ? "lpop" : "rpop";
        //构建回包
        addReplyBulk(c,value);
        //减少引用
        decrRefCount(value);
        notifyKeyspaceEvent(NOTIFY_LIST,event,c->argv[1],c->db->id);
        if (listTypeLength(o) == 0) {//如果没有数据，会删除这个key
            notifyKeyspaceEvent(NOTIFY_GENERIC,"del",
                                c->argv[1],c->db->id);
            dbDelete(c->db,c->argv[1]);
        }
        signalModifiedKey(c,c->db,c->argv[1]);
        server.dirty++;
    }
}

//LPOP key 下标是从0开始的
void lpopCommand(client *c) {
    popGenericCommand(c,LIST_HEAD);
}

//RPOP key 下标是从0开始的
void rpopCommand(client *c) {
    popGenericCommand(c,LIST_TAIL);
}

//返回列表 key 中指定区间内的元素，区间以偏移量 start 和 stop 指定。lrange key 0 -1
//LRANGE key start stop
void lrangeCommand(client *c) {
    robj *o;
    long start, end, llen, rangelen;
    //换算开始和结束index
    if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != C_OK) ||
        (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != C_OK)) return;
    //判断key是否空
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyarray)) == NULL
         || checkType(c,o,OBJ_LIST)) return;
    //获取实际长度
    llen = listTypeLength(o);

    /* convert negative indexes */
    if (start < 0) start = llen+start;
    if (end < 0) end = llen+end;
    if (start < 0) start = 0;

    /* Invariant: start >= 0, so this test will be true when end < 0.
     * The range is empty when start > end or start >= length. */
    if (start > end || start >= llen) {//区间不对
        addReply(c,shared.emptyarray);
        return;
    }
    if (end >= llen) end = llen-1;//end会自动校准
    rangelen = (end-start)+1;

    /* Return the result in form of a multi-bulk reply */
    //构建回包数组
    addReplyArrayLen(c,rangelen);
    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        listTypeIterator *iter = listTypeInitIterator(o, start, LIST_TAIL);

        while(rangelen--) {
            listTypeEntry entry;
            listTypeNext(iter, &entry);
            quicklistEntry *qe = &entry.entry;
            if (qe->value) {//将数据插入数组
                addReplyBulkCBuffer(c,qe->value,qe->sz);
            } else {
                addReplyBulkLongLong(c,qe->longval);
            }
        }
        //释放迭代器
        listTypeReleaseIterator(iter);
    } else {
        serverPanic("List encoding is not QUICKLIST!");
    }
}

/*
对一个列表进行修剪(trim)，就是说，让列表只保留指定区间内的元素，不在指定区间之内的元素都将被删除。
举个例子，执行命令 LTRIM list 0 2 ，表示只保留列表 list 的前三个元素，其余元素全部删除。
下标(index)参数 start 和 stop 都以 0 为底，也就是说，以 0 表示列表的第一个元素，以 1 表示列表的第二个元素，以此类推。
你也可以使用负数下标，以 -1 表示列表的最后一个元素， -2 表示列表的倒数第二个元素，以此类推。
当 key 不是列表类型时，返回一个错误。
*/
//LTRIM key start stop
void ltrimCommand(client *c) {
    robj *o;
    long start, end, llen, ltrim, rtrim;
    //数据转化
    if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != C_OK) ||
        (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != C_OK)) return;
    //找不到也算ok
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.ok)) == NULL ||
        checkType(c,o,OBJ_LIST)) return;
    llen = listTypeLength(o);

    /* convert negative indexes */
    if (start < 0) start = llen+start;
    if (end < 0) end = llen+end;
    if (start < 0) start = 0;

    /* Invariant: start >= 0, so this test will be true when end < 0.
     * The range is empty when start > end or start >= length. */
    if (start > end || start >= llen) {
        /* Out of range start or start > end result in empty list */
        ltrim = llen;
        rtrim = 0;
    } else {
        if (end >= llen) end = llen-1;
        ltrim = start;
        rtrim = llen-end-1;
    }

    /* Remove list elements to perform the trim */
    //删除不在这个区间的元素
    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklistDelRange(o->ptr,0,ltrim);
        quicklistDelRange(o->ptr,-rtrim,rtrim);
    } else {
        serverPanic("Unknown list encoding");
    }

    notifyKeyspaceEvent(NOTIFY_LIST,"ltrim",c->argv[1],c->db->id);
    if (listTypeLength(o) == 0) {//删光了
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }
    signalModifiedKey(c,c->db,c->argv[1]);
    server.dirty++;
    addReply(c,shared.ok);
}

/*
根据参数 count 的值，移除列表中与参数 value 相等的元素。
count 的值可以是以下几种：
count > 0 : 从表头开始向表尾搜索，移除与 value 相等的元素，数量为 count 。
count < 0 : 从表尾开始向表头搜索，移除与 value 相等的元素，数量为 count 的绝对值。
count = 0 : 移除表中所有与 value 相等的值。
*/
//LREM key count value
void lremCommand(client *c) {
    robj *subject, *obj;
    obj = c->argv[3];//value
    long toremove;
    long removed = 0;
    //将count转成toremove
    if ((getLongFromObjectOrReply(c, c->argv[2], &toremove, NULL) != C_OK))
        return;

    subject = lookupKeyWriteOrReply(c,c->argv[1],shared.czero);
    if (subject == NULL || checkType(c,subject,OBJ_LIST)) return;

    listTypeIterator *li;
    if (toremove < 0) {//<0创建从尾部开始向头部运行的迭代器
        toremove = -toremove;
        li = listTypeInitIterator(subject,-1,LIST_HEAD);
    } else {//创建从头部向尾部的迭代器
        li = listTypeInitIterator(subject,0,LIST_TAIL);
    }

    listTypeEntry entry;
    while (listTypeNext(li,&entry)) {//一直迭代
        if (listTypeEqual(&entry,obj)) {//如果值相同
            listTypeDelete(li, &entry);
            server.dirty++;//修改次数为实际删除个数
            removed++;//实际删除个数++
            //个数够直接退出循环
            if (toremove && removed == toremove) break;
        }
    }
    //释放iter
    listTypeReleaseIterator(li);
    //如果成功删除部分元素
    if (removed) {
        //通知监听的客户端数据变脏
        signalModifiedKey(c,c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_LIST,"lrem",c->argv[1],c->db->id);
    }
    //如果没有了元素直接删除key
    if (listTypeLength(subject) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    addReplyLongLong(c,removed);
}

/* This is the semantic of this command:
 *  RPOPLPUSH srclist dstlist:
 *    IF LLEN(srclist) > 0
 *      element = RPOP srclist
 *      LPUSH dstlist element
 *      RETURN element
 *    ELSE
 *      RETURN nil
 *    END
 *  END
 *
 * The idea is to be able to get an element from a list in a reliable way
 * since the element is not just returned but pushed against another list
 * as well. This command was originally proposed by Ezra Zygmuntowicz.
 */
//处理对dstobj的元素插入
void rpoplpushHandlePush(client *c, robj *dstkey, robj *dstobj, robj *value) {
    /* Create the list if the key does not exist */
    if (!dstobj) {//操作的表可为空
        dstobj = createQuicklistObject();
        quicklistSetOptions(dstobj->ptr, server.list_max_ziplist_size,
                            server.list_compress_depth);
        dbAdd(c->db,dstkey,dstobj);
    }
    //添加元素
    signalModifiedKey(c,c->db,dstkey);
    listTypePush(dstobj,value,LIST_HEAD);
    notifyKeyspaceEvent(NOTIFY_LIST,"lpush",dstkey,c->db->id);
    /* Always send the pushed value to the client. */
    addReplyBulk(c,value);
}

/*
命令 RPOPLPUSH 在一个原子时间内，执行以下两个动作：
将列表 source 中的最后一个元素(尾元素)弹出，并返回给客户端。
将 source 弹出的元素插入到列表 destination ，作为 destination 列表的的头元素。
举个例子，你有两个列表 source 和 destination ， source 列表有元素 a, b, c ，
destination 列表有元素 x, y, z ，执行 RPOPLPUSH source destination 之后， 
source 列表包含元素 a, b ， destination 列表包含元素 c, x, y, z ，并且元素 c 会被返回给客户端。

如果 source 不存在，值 nil 被返回，并且不执行其他动作。
如果 source 和 destination 相同，则列表中的表尾元素被移动到表头，并返回该元素，可以把这种特殊情况视作列表的旋转(rotation)操作。
*/
//向source的尾元素放在destination的头部
//RPOPLPUSH source destination
void rpoplpushCommand(client *c) {
    robj *sobj, *value;
    if ((sobj = lookupKeyWriteOrReply(c,c->argv[1],shared.null[c->resp]))
        == NULL || checkType(c,sobj,OBJ_LIST)) return;

    if (listTypeLength(sobj) == 0) {//第一张表没有元素
        /* This may only happen after loading very old RDB files. Recent
         * versions of Redis delete keys of empty lists. */
        addReplyNull(c);
    } else {
        //判断第二张表
        robj *dobj = lookupKeyWrite(c->db,c->argv[2]);
        robj *touchedkey = c->argv[1];

        if (dobj && checkType(c,dobj,OBJ_LIST)) return;
        //拿出source尾元素
        value = listTypePop(sobj,LIST_TAIL);
        /* We saved touched key, and protect it, since rpoplpushHandlePush
         * may change the client command argument vector (it does not
         * currently). */
        //加上引用计数
        incrRefCount(touchedkey);
        rpoplpushHandlePush(c,c->argv[2],dobj,value);

        /* listTypePop returns an object with its refcount incremented */
        //value计数-1
        decrRefCount(value);

        /* Delete the source list when it is empty */
        //实际就是出发了rpop c->argv[1]
        notifyKeyspaceEvent(NOTIFY_LIST,"rpop",touchedkey,c->db->id);
        if (listTypeLength(sobj) == 0) {//key元素为0需要删除
            dbDelete(c->db,touchedkey);
            notifyKeyspaceEvent(NOTIFY_GENERIC,"del",
                                touchedkey,c->db->id);
        }
        signalModifiedKey(c,c->db,touchedkey);
        //key引用-1，代表已操作完
        decrRefCount(touchedkey);
        server.dirty++;
        //如果操作指令为BRPOPLPUSH，代表没有被阻塞
        if (c->cmd->proc == brpoplpushCommand) {
            rewriteClientCommandVector(c,3,shared.rpoplpush,c->argv[1],c->argv[2]);
        }
    }
}

/*-----------------------------------------------------------------------------
 * Blocking POP operations
 *----------------------------------------------------------------------------*/

/* This is a helper function for handleClientsBlockedOnKeys(). It's work
 * is to serve a specific client (receiver) that is blocked on 'key'
 * in the context of the specified 'db', doing the following:
 *
 * 1) Provide the client with the 'value' element.
 * 2) If the dstkey is not NULL (we are serving a BRPOPLPUSH) also push the
 *    'value' element on the destination list (the LPUSH side of the command).
 * 3) Propagate the resulting BRPOP, BLPOP and additional LPUSH if any into
 *    the AOF and replication channel.
 *
 * The argument 'where' is LIST_TAIL or LIST_HEAD, and indicates if the
 * 'value' element was popped from the head (BLPOP) or tail (BRPOP) so that
 * we can propagate the command properly.
 *
 * The function returns C_OK if we are able to serve the client, otherwise
 * C_ERR is returned to signal the caller that the list POP operation
 * should be undone as the client was not served: This only happens for
 * BRPOPLPUSH that fails to push the value to the destination key as it is
 * of the wrong type. */
int serveClientBlockedOnList(client *receiver, robj *key, robj *dstkey, redisDb *db, robj *value, int where)
{
    robj *argv[3];

    if (dstkey == NULL) {
        /* Propagate the [LR]POP operation. */
        argv[0] = (where == LIST_HEAD) ? shared.lpop :
                                          shared.rpop;
        argv[1] = key;
        propagate((where == LIST_HEAD) ?
            server.lpopCommand : server.rpopCommand,
            db->id,argv,2,PROPAGATE_AOF|PROPAGATE_REPL);

        /* BRPOP/BLPOP */
        addReplyArrayLen(receiver,2);
        addReplyBulk(receiver,key);
        addReplyBulk(receiver,value);

        /* Notify event. */
        char *event = (where == LIST_HEAD) ? "lpop" : "rpop";
        notifyKeyspaceEvent(NOTIFY_LIST,event,key,receiver->db->id);
    } else {
        /* BRPOPLPUSH */
        robj *dstobj =
            lookupKeyWrite(receiver->db,dstkey);
        if (!(dstobj &&
             checkType(receiver,dstobj,OBJ_LIST)))
        {
            rpoplpushHandlePush(receiver,dstkey,dstobj,
                value);
            /* Propagate the RPOPLPUSH operation. */
            argv[0] = shared.rpoplpush;
            argv[1] = key;
            argv[2] = dstkey;
            propagate(server.rpoplpushCommand,
                db->id,argv,3,
                PROPAGATE_AOF|
                PROPAGATE_REPL);

            /* Notify event ("lpush" was notified by rpoplpushHandlePush). */
            notifyKeyspaceEvent(NOTIFY_LIST,"rpop",key,receiver->db->id);
        } else {
            /* BRPOPLPUSH failed because of wrong
             * destination type. */
            return C_ERR;
        }
    }
    return C_OK;
}

/* Blocking RPOP/LPOP */
void blockingPopGenericCommand(client *c, int where) {
    robj *o;
    mstime_t timeout;
    int j;

    if (getTimeoutFromObjectOrReply(c,c->argv[c->argc-1],&timeout,UNIT_SECONDS)
        != C_OK) return;

    for (j = 1; j < c->argc-1; j++) {
        o = lookupKeyWrite(c->db,c->argv[j]);
        if (o != NULL) {
            if (o->type != OBJ_LIST) {
                addReply(c,shared.wrongtypeerr);
                return;
            } else {
                if (listTypeLength(o) != 0) {
                    /* Non empty list, this is like a non normal [LR]POP. */
                    char *event = (where == LIST_HEAD) ? "lpop" : "rpop";
                    robj *value = listTypePop(o,where);
                    serverAssert(value != NULL);

                    addReplyArrayLen(c,2);
                    addReplyBulk(c,c->argv[j]);
                    addReplyBulk(c,value);
                    decrRefCount(value);
                    notifyKeyspaceEvent(NOTIFY_LIST,event,
                                        c->argv[j],c->db->id);
                    if (listTypeLength(o) == 0) {
                        dbDelete(c->db,c->argv[j]);
                        notifyKeyspaceEvent(NOTIFY_GENERIC,"del",
                                            c->argv[j],c->db->id);
                    }
                    signalModifiedKey(c,c->db,c->argv[j]);
                    server.dirty++;

                    /* Replicate it as an [LR]POP instead of B[LR]POP. */
                    rewriteClientCommandVector(c,2,
                        (where == LIST_HEAD) ? shared.lpop : shared.rpop,
                        c->argv[j]);
                    return;
                }
            }
        }
    }

    /* If we are inside a MULTI/EXEC and the list is empty the only thing
     * we can do is treating it as a timeout (even with timeout 0). */
    if (c->flags & CLIENT_MULTI) {
        addReplyNullArray(c);
        return;
    }

    /* If the list is empty or the key does not exists we must block */
    blockForKeys(c,BLOCKED_LIST,c->argv + 1,c->argc - 2,timeout,NULL,NULL);
}

/*
它是 LPOP key 命令的阻塞版本，当给定列表内没有任何元素可供弹出的时候，连接将被BLPOP命令阻塞，直到等待超时或发现可弹出元素为止。
当给定多个key参数时，按参数key的先后顺序依次检查各个列表，弹出第一个非空列表的头元素。
返回值：
redis> DEL job command request           # 确保key都被删除
(integer) 0
redis> LPUSH command "update system..."  # 为command列表增加一个值
(integer) 1
redis> LPUSH request "visit page"        # 为request列表增加一个值
(integer) 1
redis> BLPOP job command request 0       # job 列表为空，被跳过，紧接着 command 列表的第一个元素被弹出。
1) "command"                             # 弹出元素所属的列表
2) "update system..."                    # 弹出元素所属的值
*/
//BLPOP key [key …] timeout
void blpopCommand(client *c) {
    blockingPopGenericCommand(c,LIST_HEAD);
}

/*
BRPOP 是列表的阻塞式(blocking)弹出原语。
它是 RPOP key 命令的阻塞版本，当给定列表内没有任何元素可供弹出的时候，连接将被 BRPOP 命令阻塞，直到等待超时或发现可弹出元素为止。
当给定多个 key 参数时，按参数 key 的先后顺序依次检查各个列表，弹出第一个非空列表的尾部元素。
*/
// BRPOP key [key …] timeout
void brpopCommand(client *c) {
    blockingPopGenericCommand(c,LIST_TAIL);
}

/*
BRPOPLPUSH 是 RPOPLPUSH source destination 的阻塞版本，当给定列表 source 不为空时， 
BRPOPLPUSH 的表现和 RPOPLPUSH source destination 一样。

当列表 source 为空时， BRPOPLPUSH 命令将阻塞连接，直到等待超时，
或有另一个客户端对 source 执行 LPUSH key value [value …] 或 RPUSH key value [value …] 命令为止。

超时参数 timeout 接受一个以秒为单位的数字作为值。超时参数设为 0 表示阻塞时间可以无限期延长(block indefinitely) 。
*/
//BRPOPLPUSH source destination timeout
void brpoplpushCommand(client *c) {
    mstime_t timeout;
    //获取过期的毫秒时间戳
    if (getTimeoutFromObjectOrReply(c,c->argv[3],&timeout,UNIT_SECONDS)
        != C_OK) return;

    robj *key = lookupKeyWrite(c->db, c->argv[1]);

    if (key == NULL) {
        if (c->flags & CLIENT_MULTI) {//如果多个客户端同时访问直接返回
            /* Blocking against an empty list in a multi state
             * returns immediately. */
            addReplyNull(c);
        } else {
            /* The list is empty and the client blocks. *///只有一个则阻塞客户端
            blockForKeys(c,BLOCKED_LIST,c->argv + 1,1,timeout,c->argv[2],NULL);
        }
    } else {
        if (key->type != OBJ_LIST) {//类型不对
            addReply(c, shared.wrongtypeerr);
        } else {
            /* The list exists and has elements, so
             * the regular rpoplpushCommand is executed. */
            serverAssertWithInfo(c,key,listTypeLength(key) > 0);
            rpoplpushCommand(c);
        }
    }
}
