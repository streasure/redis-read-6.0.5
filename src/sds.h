/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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

/*
整个sds的数据结构是这样的
|内存长度|struct sdshdr##T(这里的最后存放的是type-SDS_TYPE_5)|string|\0|
*/

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
//redis的动态字符串
typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
//len来表示当前的sds中字符串的大小，alloc代表当前申请的总大小。char数组为存储内容的存储空间，flag只用低三位表示类型，高五位并没有使用。
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used */
    uint8_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
//SDS_HDR_VAR和SDS_HDR这两个宏用于获取SDS头的位置
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)
/*获取当前sds的已使用长度*/
static inline size_t sdslen(const sds s) {
    /*
    在sds.c的sdsnewlen函数中有
    fp = ((unsigned char*)s)-1;
    ...
    *fp = type;
    所以这个-1存放的就是sds的结构类型，获取长度就是赋值时的反向过程
    */
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}
/*获取sds字符串空余空间*/
static inline size_t sdsavail(const sds s) {
    //获取type
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}
/*设置sds字符串长度*/
static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}
/*增加sds字符串长度*/
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}
/*获取sds字符串容量*/
/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}
/*设置sds字符串容量*/
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);//生成一个sds字符串
sds sdsnew(const char *init);//初始化sds，实际就是调用sdsnewlen
sds sdsempty(void);//将sds的字符串置为一个空的只含\0的
sds sdsdup(const sds s);//做一个复制
void sdsfree(sds s);//删除整个sds，不只是string，还有三段附加信息空间
sds sdsgrowzero(sds s, size_t len);//增加新的空间
/*
字符串拼接
s：拼接目标字符串
t：拼接内容（任意）
len：拼接内容需要的长度，可以选择接t中len长度的内容到s上
*/
sds sdscatlen(sds s, const void *t, size_t len);
//字符串拼接
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
//拷贝len长度的t到s中，返回s
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);
/*
va_list 不确定个数的参数，一般情况下定义变量为字符指针
格式化输出的字符串连接到源字符串
*/
sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
/* 格式化输入，类似C语言中的sprintf函数 */
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif
/* 字符串格式化输出 */
sds sdscatfmt(sds s, char const *fmt, ...);
/* 字符串的trim操作，高级语言普遍提供 */
sds sdstrim(sds s, const char *cset);
/* 字符串截取 */
void sdsrange(sds s, ssize_t start, ssize_t end);
/* 更新字符串的长度，考虑下面这种情况
    s = sdsnew("foobar");
    s[2] = '\0';
    这是就需要调用sdsupdatelen(s)更新字符串长度，底层是使用strlen计算字符串长度
*/
void sdsupdatelen(sds s);
/* 清空字符串 */
void sdsclear(sds s);
/* 字符串比较操作 */
int sdscmp(const sds s1, const sds s2);
/* 字符串分割操作 */
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);
/* 释放sdssplitlen函数返回的sds数组 */
void sdsfreesplitres(sds *tokens, int count);
/* 统一转换为小写字符 */
void sdstolower(sds s);
/* 统一转换为大写字符 */
void sdstoupper(sds s);
/* 将一个long long类型的数字转换为字符串 */
sds sdsfromlonglong(long long value);
/* 添加引用字符串 */
sds sdscatrepr(sds s, const char *p, size_t len);
/* 参数解析 */
sds *sdssplitargs(const char *line, int *argc);
/* 字符替换操作 */
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
/* 将一个C风格的字符串数组用指定分隔符连接成一个字符串*/
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
/* 确保sds中的可用空间大于或等于addlen，如果当前字符串可用空间不满足则重新配置空间 */
sds sdsMakeRoomFor(sds s, size_t addlen);
/* 根据给定参数incr调整当前长度和可用空间大小 */
void sdsIncrLen(sds s, ssize_t incr);
/* 释放字符数组buf中的多余空间，使其刚好能存放当前字符数 */
sds sdsRemoveFreeSpace(sds s);
/* 获取sds实际分配的空间大小（包括最后的'\0'结束符） */
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
