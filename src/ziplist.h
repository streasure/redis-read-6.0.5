/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#ifndef _ZIPLIST_H
#define _ZIPLIST_H

#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

//创建一个新的ziplist压缩表
unsigned char *ziplistNew(void);
//ziplist合并
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second);
//像列表中推入数据
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
//索引定位到列表的某个位置
unsigned char *ziplistIndex(unsigned char *zl, int index);
//获取当前列表位置的下一个值
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
//获取当期列表位置的前一个值
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
//获取p为首地址的entry中存放的数据，**sstr代表的是字符串的值，*sval代表的是数字
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
//向列表中插入数据，与ziplistPush一个道理
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
//列表中删除p所在的结点
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
//删除第index个位置entry之后的num个entry
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num);
//比较p所在的节点的数据和s的数据是不是一样，分string和数字
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
//在列表中寻找p开始的值和*vstr一样的结点
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
//返回ziplist中entry个数
unsigned int ziplistLen(unsigned char *zl);
//返回列表的占用的位数大小
size_t ziplistBlobLen(unsigned char *zl);
//打印ziplist
void ziplistRepr(unsigned char *zl);

#ifdef REDIS_TEST
int ziplistTest(int argc, char *argv[]);
#endif

#endif /* _ZIPLIST_H */
