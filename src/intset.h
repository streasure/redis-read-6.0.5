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

#ifndef __INTSET_H
#define __INTSET_H
#include <stdint.h>

typedef struct intset {
    uint32_t encoding;//代表存放的最大数的数据类型 short/int/longlong
    uint32_t length;//存放了多少个数据
    //存储的数据是从大到小有序的按照encoding获取后反转就是原数据
    int8_t contents[];//实际数据,看_intsetGetEncoded这个函数就知道里面的数据如何获取和存储
} intset;

intset *intsetNew(void);//创建一个空的intset
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);//插入一个元素，会将最新的对象返回回来
intset *intsetRemove(intset *is, int64_t value, int *success);//在intset中删除value，返回最新的intset
uint8_t intsetFind(intset *is, int64_t value);//查看这个value是否在这个intset中
int64_t intsetRandom(intset *is);//随机获取一个intset中的一个值
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);//获取intset在pos位置的数据将数据赋值给value
uint32_t intsetLen(const intset *is);//返回intset的长度
size_t intsetBlobLen(intset *is);//返回intset占用的字节总长度

#ifdef REDIS_TEST
int intsetTest(int argc, char *argv[]);
#endif

#endif // __INTSET_H
