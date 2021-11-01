/* String -> String Map data structure optimized for size.
 *
 * See zipmap.c for more info.
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef _ZIPMAP_H
#define _ZIPMAP_H

//初始化只申请两个字节的空间，[]char{0,255}
//每个char以254为辨识标准
unsigned char *zipmapNew(void);
//设置新的key
/*
unsigned char *zm   //zipmap
unsigned char *key  //key值
unsigned int klen   //key的长度
unsigned char *val  //value值
unsigned int vlen   //value长度
int *update         //key更新标志
*/
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update);
//删除zipmap中key
/*
unsigned char *zm   //zipmap
unsigned char *key  //key值
unsigned int klen   //key长度
int *deleted        //删除成功标识
*/
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted);
//可能只是为了定位到key-value数据的首地址
unsigned char *zipmapRewind(unsigned char *zm);
//获取下一个k-v
/*
unsigned char *zm       //zipmap key首地址
unsigned char **key     //key地址       
unsigned int *klen      //key长度
unsigned char **value   //value地址
unsigned int *vlen      //value长度
*/
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen);
//查询key
/*
unsigned char *zm       //zipmap
unsigned char *key      //key值
unsigned int klen       //key length
unsigned char **value   //value首地址
unsigned int *vlen      //value长度
*/
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen);
//验证key是否存在
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen);
//获取k-v的个数
unsigned int zipmapLen(unsigned char *zm);
//获取zipmap占用的总长度
size_t zipmapBlobLen(unsigned char *zm);
void zipmapRepr(unsigned char *p);

#ifdef REDIS_TEST
int zipmapTest(int argc, char *argv[]);
#endif

#endif
