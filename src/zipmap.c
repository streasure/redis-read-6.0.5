/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to a hash table once a given number of
 * elements is reached.
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 *
 * <zmlen> is 1 byte length that holds the current size of the zipmap.
 * When the zipmap length is greater than or equal to 254, this value
 * is not used and the zipmap needs to be traversed to find out the length.
 *
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 253, it's a single-byte length. If it is 254 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value of 255 is used to
 * signal the end of the hash.
 *
 * <free> is the number of free unused bytes after the string, resulting
 * from modification of values associated to a key. For instance if "foo"
 * is set to "bar", and later "foo" will be set to "hi", it will have a
 * free byte to use if the value will enlarge again later, or even in
 * order to add a key/value pair if it fits.
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, the zipmap will be
 * reallocated to make sure it is as small as possible.
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * Note that because keys and values are prefixed length "objects",
 * the lookup will take O(N) where N is the number of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

#include <stdio.h>
#include <string.h>
#include "zmalloc.h"
#include "endianconv.h"

#define ZIPMAP_BIGLEN 254
#define ZIPMAP_END 255

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
#define ZIPMAP_VALUE_MAX_FREE 4

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)

/* Create a new empty zipmap. */
unsigned char *zipmapNew(void) {
    unsigned char *zm = zmalloc(2);

    zm[0] = 0; /* Length */
    zm[1] = ZIPMAP_END;
    return zm;
}

/* Decode the encoded length pointed by 'p' */
//获取这个元素所占用的空间的大小
static unsigned int zipmapDecodeLength(unsigned char *p) {
    unsigned int len = *p;
    //如果长度是正常的数字<254，直接返回存储的数据
    if (len < ZIPMAP_BIGLEN) return len;
    //将后4个字节的内容赋值给len
    memcpy(&len,p+1,sizeof(unsigned int));
    //获取实际的长度
    memrev32ifbe(&len);
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
//返回这个len长度的数据所需要占用的字节数并将len写入p之后指定的位置
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {
    if (p == NULL) {//如果没有传指针，返回存这个长度需要的字节长度
        return ZIPMAP_LEN_BYTES(len);
    } else {
        if (len < ZIPMAP_BIGLEN) {//如果长度小于254，代表这个值可以被一个字节存下
            p[0] = len;//将这个值存入p[0]
            return 1;
        } else {
            p[0] = ZIPMAP_BIGLEN;//p[0]设置长度超出上限的标志
            memcpy(p+1,&len,sizeof(len));//将len的数据复制到p之后的字节中
            memrev32ifbe(p+1);//数据反转
            return 1+sizeof(len);//返回实际的字节长度
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries. */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen) {
    //p为头节点偏移一位
    unsigned char *p = zm+1, *k = NULL;
    unsigned int l,llen;
    //存的值不为尾节点标识，遍历到结束标记位置
    while(*p != ZIPMAP_END) {
        unsigned char free;

        /* Match or skip the key */
        //获取p指向元素存储key长度所需要占用的长度
        l = zipmapDecodeLength(p);
        //获取存这个key本身的长度
        llen = zipmapEncodeLength(NULL,l);
        //如果key有值并且k没被赋值，当前节点k长度相同，并且数据相同
        if (key != NULL && k == NULL && l == klen && !memcmp(p+llen,key,l)) {
            /* Only return when the user doesn't care
             * for the total length of the zipmap. */
            if (totlen != NULL) {//如果总长度需要记录
                k = p;//将p记到临时变量
            } else {//不然直接返回key所在的数据首地址
                return p;
            }
        }
        //p偏移到这个元素所在的value首地址
        p += llen+l;
        /* Skip the value as well */
        l = zipmapDecodeLength(p);//获取存这个val长度所用的字节数
        //偏移到实际数据首地址
        p += zipmapEncodeLength(NULL,l);
        //获取vempty0
        free = p[0];
        //需要查看实际数据存储方式才能看懂
        p += l+1+free; /* +1 to skip the free byte */
    }
    //获取总长度
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;
    return k;
}

//获取k+v需要的长度，以254为flag，类似于ziplist len为1或者5
/******************************************************/
/*
返回的是存放klen+key+vlen+val+3的长度
*/
/******************************************************/
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;
    //会多申请三个字节用于使用，分别为存储key长度，存储value长度，free
    l = klen+vlen+3;
    if (klen >= ZIPMAP_BIGLEN) l += 4;
    if (vlen >= ZIPMAP_BIGLEN) l += 4;
    return l;
}

/* Return the total amount used by a key (encoded length + payload) */
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    //获取这个key的长度
    unsigned int l = zipmapDecodeLength(p);
    //返回存放key数据长度所用的字节数和存放key数据所用的字节数就是这个key总占用的字节数
    return zipmapEncodeLength(NULL,l) + l;
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) */
static unsigned int zipmapRawValueLength(unsigned char *p) {
    //获取value的长度
    unsigned int l = zipmapDecodeLength(p);
    unsigned int used;

    used = zipmapEncodeLength(NULL,l);
    //p[used=1/5]为free中存的数据
    //p[used](空闲空间) + 1（free数据存储占用空间） + l（value占用空间）
    used += p[used] + 1 + l;
    return used;
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). */
//获取zipmap中p所在这个key-value两个元素占用的总空间大小
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    //获取key占用的长度，一个是存放key所用的字节数还有存放key长度所用的字节
    unsigned int l = zipmapRawKeyLength(p);
    //再加上元素所占用的长度
    return l + zipmapRawValueLength(p+l);
}

//zipmap重新规划空间，长度为len，原数据保持不变
static inline unsigned char *zipmapResize(unsigned char *zm, unsigned int len) {
    zm = zrealloc(zm, len);
    //设置尾数据
    zm[len-1] = ZIPMAP_END;
    return zm;
}

/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. */
/*
zm      ：zipmap节点
key     ：插入的key值
klen    ：插入key的长度
val     ：插入的value
vlen    ：插入value的长度
update  ：更新标识，如果不为空在key存在的情况下为1，不然为0
*/
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {
    unsigned int zmlen, offset;
    //获取需要新用到的空间大小
    unsigned int freelen, reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;
    //防止找不到key做的处理
    freelen = reqlen;
    //update不为null先赋值为0，0：插入 1：更新原key
    if (update) *update = 0;
    //返回key所在的位置，并且将总长度zmlen计算出来
    p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p == NULL) {//没有找到
        /* Key not found: enlarge */
        //没有找到就需要扩大
        zm = zipmapResize(zm, zmlen+reqlen);
        //将p偏移到应该插入元素所在的位置
        p = zm+zmlen-1;
        //总长度为之前的加上新扩展的
        zmlen = zmlen+reqlen;

        /* Increase zipmap length (this is an insert) */
        //第一位为元素个数，元素个数+1（如果元素数量还在254以内）
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]++;
    } else {//找到了对应的key
        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        if (update) *update = 1;//代表原先的value被更新了
        //获取需要释放的字节长度key+value+free
        freelen = zipmapRawEntryLength(p);
        //如果释放的比需要的短需要重新申请大小
        if (freelen < reqlen) {//代表没有多余的空间 free为0
            /* Store the offset of this key within the current zipmap, so
             * it can be resized. Then, move the tail backwards so this
             * pair fits at the current position. */
            //元素的尾地址偏移量
            offset = p-zm;
            //重新申请空间，数据不变
            zm = zipmapResize(zm, zmlen-freelen+reqlen);
            //重定位到原始key所在的地点
            p = zm+offset;

            /* The +1 in the number of bytes to be moved is caused by the
             * end-of-zipmap byte. Note: the *original* zmlen is used. */
            //p+freelen代表的是原来的被释放的可以之后的数据最开始的位置
            //p+reqlen代表的是最新的key-value数据写入之后下一个数据的起始位置
            //这个函数的作用就是将原先要删除的key-value之后的所有元素(开始于p+freelen)移动到新的key-value下一个数据起始位置（p+reqlen）
            //offset+freelen+1获取的值就是纯粹的key-value数据所占用的空间大小
            //最后的结果就是在原key-value的地方空了一块reqlen长度的数据空间
            memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
            //新的总长度为插入新元素之后的长度
            zmlen = zmlen-freelen+reqlen;
            //释放的空间为新数据实际占用的空间
            freelen = reqlen;
        }
    }

    /* We now have a suitable block where the key/value entry can
     * be written. If there is too much free space, move the tail
     * of the zipmap a few bytes to the front and shrink the zipmap,
     * as we want zipmaps to be very space efficient. */
    //这玩意只会在老的value长度大于新的value长度时才会结果不为0
    empty = freelen-reqlen;//所释放的减去需要的空间大小
    //释放的空间比需要使用的大出很多，只适用于老的value长度大于新的value长度
    if (empty >= ZIPMAP_VALUE_MAX_FREE) {//如果大于一个int型，需要重排数据
        /* First, move the tail <empty> bytes to the front, then resize
         * the zipmap to be <empty> bytes smaller. */
        //定位到map的数据修改的初始位置
        offset = p-zm;
        //直接将后面的元素往前移动空闲长度的距离，直接赋值因为空间够用，这边并未对zipmap[end]做结束标记的赋值，所以最后的数据是不正确的
        memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
        //总长度减去多余的
        zmlen -= empty;
        //重新申请空间，并将结束标记赋值上
        zm = zipmapResize(zm, zmlen);
        //定位到需要修改的数据这边
        p = zm+offset;
        vempty = 0;
    } else {//如果找不到key则vempty为0
        vempty = empty;
    }

    /* Just write the key + value and we are done. */
    /* Key: */
    //p在可以写入数据的地方并且地址后reqlen的空间就是为新的key-value准备的
    p += zipmapEncodeLength(p,klen);//这一步将klen写入到p之后的字节中并将p偏移这段字节
    memcpy(p,key,klen);//将key的数据拷贝到p之后的数据中
    p += klen;//继续偏移
    /* Value: */
    p += zipmapEncodeLength(p,vlen);//与key相同的操作
    /*
    运行顺序
    *p=vempty
    p++
    */
   //这个不为0代表key存在的时候替换value多出来的字节
    *p++ = vempty;//p[0]存的是多出来的字节数，并向后偏移一位
    memcpy(p,val,vlen);//将val记录到后续的字节中
    return zm;
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
//删除key，delete不为空的情况下，0为没找到key，1为找到并且删除
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {
    unsigned int zmlen, freelen;
    //查找key并且计算总长度
    unsigned char *p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p) {//找到了key
        freelen = zipmapRawEntryLength(p);
        //数据迁移
        memmove(p, p+freelen, zmlen-((p-zm)+freelen+1));
        //空间重新规划并将end标记置上
        zm = zipmapResize(zm, zmlen-freelen);

        /* Decrease zipmap length */
        //更新k-v数量
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]--;

        if (deleted) *deleted = 1;
    } else {//找不到key
        if (deleted) *deleted = 0;
    }
    return zm;
}

/* Call before iterating through elements via zipmapNext() */
//将zipmap的首地址偏移一个字节，可以直接使用zipmapnext进行迭代
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}

/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 * //这边展现的就是遍历zipmap的用法
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 */
//提取最近的key和value
//这里zm是经过zipmaprewind之后的地址可以去除了第一位存储元素数量的地址
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {
    //如果这个zipmap没有值
    if (zm[0] == ZIPMAP_END) return NULL;
    if (key) {
        *key = zm;
        *klen = zipmapDecodeLength(zm);
        //返回的是key的长度和地址指针
        *key += ZIPMAP_LEN_BYTES(*klen);
    }
    //偏移到value所在的首地址
    zm += zipmapRawKeyLength(zm);
    if (value) {
        //提前偏移free
        *value = zm+1;
        //获取value长度
        *vlen = zipmapDecodeLength(zm);
        //*value += ZIPMAP_LEN_BYTES(*vlen) + 1;这样更好理解
        *value += ZIPMAP_LEN_BYTES(*vlen);
    }
    //偏移到下一个key首地址
    zm += zipmapRawValueLength(zm);
    return zm;
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;
    //查找key
    if ((p = zipmapLookupRaw(zm,key,klen,NULL)) == NULL) return 0;
    //找到的话需要偏移到value首地址
    p += zipmapRawKeyLength(p);
    //长度
    *vlen = zipmapDecodeLength(p);
    //返回value首地址
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL) != NULL;
}

/* Return the number of entries inside a zipmap */
//获取zipmap中key-value的个数
unsigned int zipmapLen(unsigned char *zm) {
    unsigned int len = 0;
    if (zm[0] < ZIPMAP_BIGLEN) {//如果长度为可被第一个字节存储下来
        len = zm[0];
    } else {//遍历长度，如果长度可被存储下来则直接存下来，可以为下一次获取方便
        unsigned char *p = zipmapRewind(zm);
        while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;

        /* Re-store length if small enough */
        if (len < ZIPMAP_BIGLEN) zm[0] = len;
    }
    return len;
}

/* Return the raw size in bytes of a zipmap, so that we can serialize
 * the zipmap on disk (or everywhere is needed) just writing the returned
 * amount of bytes of the C array starting at the zipmap pointer. */
size_t zipmapBlobLen(unsigned char *zm) {
    unsigned int totlen;
    zipmapLookupRaw(zm,NULL,0,&totlen);
    return totlen;
}

#ifdef REDIS_TEST
static void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#define UNUSED(x) (void)(x)
int zipmapTest(int argc, char *argv[]) {
    unsigned char *zm;

    UNUSED(argc);
    UNUSED(argv);

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);

    printf("\nLook up large key:\n");
    {
        unsigned char buf[512];
        unsigned char *value;
        unsigned int vlen, i;
        for (i = 0; i < 512; i++) buf[i] = 'a';

        zm = zipmapSet(zm,buf,512,(unsigned char*) "long",4,NULL);
        if (zipmapGet(zm,buf,512,&value,&vlen)) {
            printf("  <long key> is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }

    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate through elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
