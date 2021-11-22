/* quicklist.h - A generic doubly linked quicklist implementation
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this quicklist of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this quicklist of conditions and the following disclaimer in the
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

#include <stdint.h> // for UINTPTR_MAX

#ifndef __QUICKLIST_H__
#define __QUICKLIST_H__

/* Node, quicklist, and Iterator are the only data structures used currently. */

/* quicklistNode is a 32 byte struct describing a ziplist for a quicklist.
 * We use bit fields keep the quicklistNode at 32 bytes.
 * count: 16 bits, max 65536 (max zl bytes is 65k, so max count actually < 32k).
 * encoding: 2 bits, RAW=1, LZF=2.
 * container: 2 bits, NONE=1, ZIPLIST=2.
 * recompress: 1 bit, bool, true if node is temporarry decompressed for usage.
 * attempted_compress: 1 bit, boolean, used for verifying during testing.
 * extra: 10 bits, free for future use; pads out the remainder of 32 bits */
//   sizeof(quicklistNode)=32
typedef struct quicklistNode {
    struct quicklistNode *prev;//8 bytes 结构体指针 
    struct quicklistNode *next;//8 bytes
    //数据指针。如果当前节点的数据没有压缩，那么它指向一个ziplist结构；否则，它指向一个quicklistLZF结构。
    unsigned char *zl;//1 bytes //字节对齐 8 bytes
    //表示zl指向的ziplist的总大小（包括zlbytes, zltail, zllen, zlend和各个数据项）。需要注意的是：如果ziplist被压缩了，那么这个sz的值仍然是压缩前的ziplist大小。
    unsigned int sz; //4 bytes            /* ziplist size in bytes */
    /*************************下面的所有字节总共占用32位4字节，与上一个sz int型内存对齐************************************/
    //ziplist中的item数量
    unsigned int count : 16; //16 bits    /* count of items in ziplist */
    //表示ziplist是否压缩了（以及用了哪个压缩算法）。目前只有两种取值：2表示被压缩了（而且用的是LZF压缩算法），1表示没有压缩。
    unsigned int encoding : 2; // 2 bits  /* RAW==1 or LZF==2 */
    /*
    是一个预留字段。本来设计是用来表明一个quicklist节点下面是直接存数据，还是使用ziplist存数据，
    或者用其它的结构来存数据（用作一个数据容器，所以叫container）。
    但是，在目前的实现中，这个值是一个固定的值2，表示使用ziplist作为数据容器。
    */
    unsigned int container : 2; //2 bits /* NONE==1 or ZIPLIST==2 */
    //当我们使用类似lindex这样的命令查看了某一项本来压缩的数据时，需要把数据暂时解压，这时就设置recompress=1做一个标记，等有机会再把数据重新压缩。
    unsigned int recompress : 1; // 1 bits/* was this node previous compressed? */
    //这个值只对Redis的自动化测试程序有用。我们不用管它。
    unsigned int attempted_compress : 1; //1 bits/* node can't compress; too small */
    //预留位
    unsigned int extra : 10; //10 bits/* more bits to steal for future usage */
} quicklistNode;

/* quicklistLZF is a 4+N byte struct holding 'sz' followed by 'compressed'.
 * 'sz' is byte length of 'compressed' field.
 * 'compressed' is LZF data with total (compressed) length 'sz'
 * NOTE: uncompressed length is stored in quicklistNode->sz.
 * When quicklistNode->zl is compressed, node->zl points to a quicklistLZF */
/*
quicklistLZF结构表示一个被压缩过的ziplist。其中：
sz: 表示压缩后的ziplist大小。
compressed: 是个柔性数组（flexible array member），存放压缩后的ziplist字节数组。
*/
typedef struct quicklistLZF {
    unsigned int sz; /* LZF size in bytes*/
    char compressed[];
} quicklistLZF;

/* Bookmarks are padded with realloc at the end of of the quicklist struct.
 * They should only be used for very big lists if thousands of nodes were the
 * excess memory usage is negligible, and there's a real need to iterate on them
 * in portions.
 * When not used, they don't add any memory overhead, but when used and then
 * deleted, some overhead remains (to avoid resonance).
 * The number of bookmarks used should be kept to minimum since it also adds
 * overhead on node deletion (searching for a bookmark to update). */
typedef struct quicklistBookmark {
    quicklistNode *node;
    char *name;
} quicklistBookmark;

#if UINTPTR_MAX == 0xffffffff
/* 32-bit */
#   define QL_FILL_BITS 14
#   define QL_COMP_BITS 14
#   define QL_BM_BITS 4
#elif UINTPTR_MAX == 0xffffffffffffffff
/* 64-bit */
#   define QL_FILL_BITS 16
#   define QL_COMP_BITS 16
#   define QL_BM_BITS 4 /* we can encode more, but we rather limit the user
                           since they cause performance degradation. */
#else
#   error unknown arch bits count
#endif

/* quicklist is a 40 byte struct (on 64-bit systems) describing a quicklist.
 * 'count' is the number of total entries.
 * 'len' is the number of quicklist nodes.
 * 'compress' is: -1 if compression disabled, otherwise it's the number
 *                of quicklistNodes to leave uncompressed at ends of quicklist.
 * 'fill' is the user-requested (or default) fill factor.
 * 'bookmakrs are an optional feature that is used by realloc this struct,
 *      so that they don't consume memory when not used. */
typedef struct quicklist {
    quicklistNode *head;
    quicklistNode *tail;
    //所有ziplist数据项的个数总和。
    unsigned long count;        /* total count of all entries in all ziplists */
    //quicklist节点的个数。
    unsigned long len;          /* number of quicklistNodes */
    //16bit，ziplist大小设置，存放list-max-ziplist-size参数的值。
    /*
    这个fill值如果>0,则这个quicklist在插入元素时，ziplist可被插入的判断条件位ziplist的长度有没有查过系统安全上限和节点的count有没有超过fill
    如果小于0，由于之前设置的时候，最小就是-5，在使用的时候会进行-fill+1，则会在optimization_level这个表中看size是否满足ziplist的判定标准
    static const size_t optimization_level[] = {4096, 8192, 16384, 32768, 65536};
    */
    int fill : QL_FILL_BITS;  //取值范围：[-5,2^15-1]            /* fill factor for individual nodes */
    unsigned int compress : QL_COMP_BITS;//取值范围：[0,2^15-1] /* depth of end nodes not to compress;0=off */
    unsigned int bookmark_count: QL_BM_BITS;//记录bookmarks中元素的数量[0-15]
    quicklistBookmark bookmarks[];
} quicklist;

//quicklist迭代器结构
typedef struct quicklistIter {
    const quicklist *quicklist;
    quicklistNode *current;
    unsigned char *zi;//初始化的时候为NULL
    long offset; /* offset in current ziplist */
    int direction;//开始迭代的位置，头或者尾，如果current被删除则需要将current指向next或者prev
} quicklistIter;

typedef struct quicklistEntry {
    const quicklist *quicklist;
    quicklistNode *node;//操作的node
    unsigned char *zi;//node中zl需要操作的entry位置
    unsigned char *value;
    long long longval;
    unsigned int sz;
    int offset;//向前或者向后的偏移量
} quicklistEntry;

#define QUICKLIST_HEAD 0
#define QUICKLIST_TAIL -1

/* quicklist node encodings */
#define QUICKLIST_NODE_ENCODING_RAW 1//未压缩的zl
#define QUICKLIST_NODE_ENCODING_LZF 2//lzf压缩过的zl

/* quicklist compression disable */
#define QUICKLIST_NOCOMPRESS 0

/* quicklist container formats */
#define QUICKLIST_NODE_CONTAINER_NONE 1
#define QUICKLIST_NODE_CONTAINER_ZIPLIST 2

#define quicklistNodeIsCompressed(node)                                        \
    ((node)->encoding == QUICKLIST_NODE_ENCODING_LZF)

/* Prototypes */
//创建一个新的quicklist并初始化
quicklist *quicklistCreate(void);
//使用默认参数生成一个新的quicklist,设置了fill和compress
quicklist *quicklistNew(int fill, int compress);
//设置quicklist的compress
void quicklistSetCompressDepth(quicklist *quicklist, int depth);
//设置quicklist的fill
void quicklistSetFill(quicklist *quicklist, int fill);
//设置quicklist的fill和compress
void quicklistSetOptions(quicklist *quicklist, int fill, int depth);
//释放quicklist
void quicklistRelease(quicklist *quicklist);
/*
向头结点插入一个元素
返回0代表使用的是原来的node
返回1代表新创建了一个node
新的node会成为新的head
*/
int quicklistPushHead(quicklist *quicklist, void *value, const size_t sz);
/*
向尾部节点插入一个元素
返回0代表使用的是原来的node
返回1代表新创建了一个node
新的node会成为新的tail
*/
int quicklistPushTail(quicklist *quicklist, void *value, const size_t sz);
//quicklist数据插入，根据where决定调用quicklistPushHead还是quicklistPushTail
void quicklistPush(quicklist *quicklist, void *value, const size_t sz,int where);
//在quicklist的tail之后插入一个新的node，node中元素为zl
void quicklistAppendZiplist(quicklist *quicklist, unsigned char *zl);
// 将zl代表的ziplist中的entry全部插入到quicklist的尾节点
quicklist *quicklistAppendValuesFromZiplist(quicklist *quicklist,unsigned char *zl);
//用zl代表的ziplist创建一个初始化了fill和compress的quicklist
quicklist *quicklistCreateFromZiplist(int fill, int compress, unsigned char *zl);
//向entry指向的node中zl末尾插入value
void quicklistInsertAfter(quicklist *quicklist, quicklistEntry *node,void *value, const size_t sz);
//向entry指向的node中zl头部插入value
void quicklistInsertBefore(quicklist *quicklist, quicklistEntry *node, void *value, const size_t sz);
//删除entry这个node的ziplist中zi所在位置的元素，并更新iter的node和offset
void quicklistDelEntry(quicklistIter *iter, quicklistEntry *entry);
//替换quicklist位于index位置的元素，index正负代表头部还是尾部第几位
int quicklistReplaceAtIndex(quicklist *quicklist, long index, void *data, int sz);
//删除quicklist在index位之后的stop个元素
int quicklistDelRange(quicklist *quicklist, const long start, const long stop);
//根据direction获取这个quicklist的iter
quicklistIter *quicklistGetIterator(const quicklist *quicklist, int direction);
//获取quicklist位于idx位置元素的iter
quicklistIter *quicklistGetIteratorAtIdx(const quicklist *quicklist, int direction, const long long idx);
//获取iter这个迭代器所在quicklist的node根据direction的zi的下一个元素
int quicklistNext(quicklistIter *iter, quicklistEntry *node);
//释放iter，需要对iter所在的node做压缩
void quicklistReleaseIterator(quicklistIter *iter);
//拷贝quicklist
quicklist *quicklistDup(quicklist *orig);
//查找quicklist在index位置的元素，数据放入entry中
int quicklistIndex(const quicklist *quicklist, const long long index,quicklistEntry *entry);
//下面两个找不到函数
void quicklistRewind(quicklist *quicklist, quicklistIter *li);
void quicklistRewindTail(quicklist *quicklist, quicklistIter *li);
//将尾节点的尾元素放到头节点的最开始位置
void quicklistRotate(quicklist *quicklist);
//从quicklist中的头或者尾pop一个元素出来，存在data或者sval
int quicklistPopCustom(quicklist *quicklist, int where, unsigned char **data,
                       unsigned int *sz, long long *sval,
                       void *(*saver)(unsigned char *data, unsigned int sz));
//调用quicklistPopCustom获取quicklist中的头部元素或者尾部元素，返回quicklistPopCustom的返回值
int quicklistPop(quicklist *quicklist, int where, unsigned char **data,
                 unsigned int *sz, long long *slong);
//获取quicklist的元素个数
unsigned long quicklistCount(const quicklist *ql);
//比较ziplist中p1指向的元素和p2指向的元素是否相同
int quicklistCompare(unsigned char *p1, unsigned char *p2, int p2_len);
//获取quicklist这个node压缩过的quicklistLZF数据
size_t quicklistGetLzf(const quicklistNode *node, void **data);

/* bookmarks */
//在bookmark中插入name和node代表的bookmark node
int quicklistBookmarkCreate(quicklist **ql_ref, const char *name, quicklistNode *node);
//删除bookmark中值为name的元素，返回0代表没找到，返回1代表删除成功
int quicklistBookmarkDelete(quicklist *ql, const char *name);
//找到bookmark中值为name的元素中所指向的node
quicklistNode *quicklistBookmarkFind(quicklist *ql, const char *name);
//删除这个bookmark但是不释放bookmark的指针，只释放元素
void quicklistBookmarksClear(quicklist *ql);

#ifdef REDIS_TEST
int quicklistTest(int argc, char *argv[]);
#endif

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __QUICKLIST_H__ */
