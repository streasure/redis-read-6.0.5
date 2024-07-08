/* Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * Copyright (c) 2020, Amazon Web Services
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
 * POSSIBILITY OF SUCH DAMAGE. */

#include "crc64.h"
#include "crcspeed.h"
static uint64_t crc64_table[8][256] = {{0}};

#define POLY UINT64_C(0xad93d23594c935a9)
/******************** BEGIN GENERATED PYCRC FUNCTIONS ********************/
/**
 * Generated on Sun Dec 21 14:14:07 2014,
 * by pycrc v0.8.2, https://www.tty1.net/pycrc/
 *
 * LICENSE ON GENERATED CODE:
 * ==========================
 * As of version 0.6, pycrc is released under the terms of the MIT licence.
 * The code generated by pycrc is not considered a substantial portion of the
 * software, therefore the author of pycrc will not claim any copyright on
 * the generated code.
 * ==========================
 *
 * CRC configuration:
 *    Width        = 64
 *    Poly         = 0xad93d23594c935a9
 *    XorIn        = 0xffffffffffffffff
 *    ReflectIn    = True
 *    XorOut       = 0x0000000000000000
 *    ReflectOut   = True
 *    Algorithm    = bit-by-bit-fast
 *
 * Modifications after generation (by matt):
 *   - included finalize step in-line with update for single-call generation
 *   - re-worked some inner variable architectures
 *   - adjusted function parameters to match expected prototypes.
 *****************************************************************************/

/**
 * Reflect all bits of a \a data word of \a data_len bytes.
 *
 * \param data         The data word to be reflected.
 * \param data_len     The width of \a data expressed in number of bits.
 * \return             The reflected data.
 *****************************************************************************/
static inline uint_fast64_t crc_reflect(uint_fast64_t data, size_t data_len) {
    uint_fast64_t ret = data & 0x01;

    for (size_t i = 1; i < data_len; i++) {
        data >>= 1;
        ret = (ret << 1) | (data & 0x01);
    }

    return ret;
}

/**
 *  Update the crc value with new data.
 *
 * \param crc      The current crc value.
 * \param data     Pointer to a buffer of \a data_len bytes.
 * \param data_len Number of bytes in the \a data buffer.
 * \return         The updated crc value.
 ******************************************************************************/
//使用新的in_data计算新的crc数值
uint64_t _crc64(uint_fast64_t crc, const void *in_data, const uint64_t len) {
    const uint8_t *data = in_data;
    unsigned long long bit;

    for (uint64_t offset = 0; offset < len; offset++) {
        uint8_t c = data[offset];
        for (uint_fast8_t i = 0x01; i & 0xff; i <<= 1) {
            bit = crc & 0x8000000000000000;
            if (c & i) {
                bit = !bit;
            }

            crc <<= 1;
            if (bit) {
                crc ^= POLY;
            }
        }

        crc &= 0xffffffffffffffff;
    }

    crc = crc & 0xffffffffffffffff;
    return crc_reflect(crc, 64) ^ 0x0000000000000000;
}

/******************** END GENERATED PYCRC FUNCTIONS ********************/

/* Initializes the 16KB lookup tables. */
/*
可以看这个文章看一下来龙去脉，这个文件上面也有贡献者matt的字段
https://xie.infoq.cn/article/dc032d3adcc5892fdaa9322b5
用途
在跨实例迁移键时添加校验和，（并验证上述校验和）数据校验
为RDB输出添加一个校验和，用于复制和持久化（是可选项，可通过配置禁用，因为性能低）
用于内存测试
*/
void crc64_init(void) {
    crcspeed64native_init(_crc64, crc64_table);
}

/* Compute crc64 */
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l) {
    return crcspeed64native(crc64_table, crc, (void *) s, l);
}

/* Test main */
#ifdef REDIS_TEST
#include <stdio.h>

#define UNUSED(x) (void)(x)
int crc64Test(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    crc64_init();
    printf("[calcula]: e9c6d914c4b8d9ca == %016" PRIx64 "\n",
           (uint64_t)_crc64(0, "123456789", 9));
    printf("[64speed]: e9c6d914c4b8d9ca == %016" PRIx64 "\n",
           (uint64_t)crc64(0, "123456789", 9));
    char li[] = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed "
                "do eiusmod tempor incididunt ut labore et dolore magna "
                "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
                "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis "
                "aute irure dolor in reprehenderit in voluptate velit esse "
                "cillum dolore eu fugiat nulla pariatur. Excepteur sint "
                "occaecat cupidatat non proident, sunt in culpa qui officia "
                "deserunt mollit anim id est laborum.";
    printf("[calcula]: c7794709e69683b3 == %016" PRIx64 "\n",
           (uint64_t)_crc64(0, li, sizeof(li)));
    printf("[64speed]: c7794709e69683b3 == %016" PRIx64 "\n",
           (uint64_t)crc64(0, li, sizeof(li)));
    return 0;
}

#endif

#ifdef REDIS_TEST_MAIN
int main(int argc, char *argv[]) {
    return crc64Test(argc, argv);
}

#endif
