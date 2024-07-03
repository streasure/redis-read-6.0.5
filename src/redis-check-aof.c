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

#include "server.h"
#include <sys/stat.h>

#define ERROR(...) { \
    char __buf[1024]; \
    snprintf(__buf, sizeof(__buf), __VA_ARGS__); \
    snprintf(error, sizeof(error), "0x%16llx: %s", (long long)epos, __buf); \
}

static char error[1044];
//个人感觉代表的是fp已偏移的文件大小
static off_t epos;

//判断buf首两个字符是否为换行符
int consumeNewline(char *buf) {
    if (strncmp(buf,"\r\n",2) != 0) {
        ERROR("Expected \\r\\n, got: %02x%02x",buf[0],buf[1]);
        return 0;
    }
    return 1;
}

//从文件中读取prefix字符开头结尾为\r\n的long类型的数据
int readLong(FILE *fp, char prefix, long *target) {
    char buf[128], *eptr;
    //获取文件大小
    epos = ftello(fp);
    //从fp流中读取128字节的数据
    if (fgets(buf,sizeof(buf),fp) == NULL) {
        return 0;
    }
    //首字符固定为prefix
    if (buf[0] != prefix) {
        ERROR("Expected prefix '%c', got: '%c'",prefix,buf[0]);
        return 0;
    }
    *target = strtol(buf+1,&eptr,10);
    //判断是否正确遇到结束标识符
    return consumeNewline(eptr);
}

//从文件fp中读取length字节的数据到target中
int readBytes(FILE *fp, char *target, long length) {
    long real;
    epos = ftello(fp);
    //从fp中读取length*1个字节的数据到target中
    real = fread(target,1,length,fp);
    if (real != length) {
        ERROR("Expected to read %ld bytes, got %ld bytes",length,real);
        return 0;
    }
    return 1;
}

//从文件中读取字符串并赋值到target中
int readString(FILE *fp, char** target) {
    long len;
    *target = NULL;
    //从p中读取$开头后面的数字并赋值到len中
    if (!readLong(fp,'$',&len)) {
        return 0;
    }

    /* Increase length to also consume \r\n */
    len += 2;
    //申请len长度的空间
    *target = (char*)zmalloc(len);
    //从fp中取出len长度的数据
    if (!readBytes(fp,*target,len)) {
        return 0;
    }
    //判断是否正确结束
    if (!consumeNewline(*target+len-2)) {
        return 0;
    }
    //将\r替换为\0
    (*target)[len-2] = '\0';
    return 1;
}

//读取参数个数
int readArgc(FILE *fp, long *target) {
    return readLong(fp,'*',target);
}

//检测文件中aof存储格式是否正确并返回最新的偏移pos
off_t process(FILE *fp) {
    long argc;
    off_t pos = 0;
    //multi标识事务数据的读取是否正常
    int i, multi = 0;
    char *str;

    while(1) {
        //修改文件偏移大小
        if (!multi) pos = ftello(fp);
        //读取参数失败则break，证明会有成功获取参数的情况，个数存储在argc中
        if (!readArgc(fp, &argc)) break;
        //参数获取成功以后fp的指针已经偏移到正确数据位置处
        for (i = 0; i < argc; i++) {
            //数据读取入str中
            if (!readString(fp,&str)) break;
            if (i == 0) {//第一位只能是multi或者exec
                //无视大小写判断str是不是等于multi
                if (strcasecmp(str, "multi") == 0) {
                    if (multi++) {//首次multi才会不触发报错
                        ERROR("Unexpected MULTI");
                        break;
                    }
                } else if (strcasecmp(str, "exec") == 0) {
                    if (--multi) {
                        ERROR("Unexpected EXEC");
                        break;
                    }
                }
            }
            //释放空间
            zfree(str);
        }

        /* Stop if the loop did not finish */
        //如果循环没有正常结束
        if (i < argc) {
            if (str) zfree(str);
            break;
        }
    }
    //文件结束或者multi数值不为0（代表multi之后缺少exec的正常读取）
    if (feof(fp) && multi && strlen(error) == 0) {
        ERROR("Reached EOF before reading EXEC for MULTI");
    }
    if (strlen(error) > 0) {
        printf("%s\n", error);
    }
    //返回偏移位
    return pos;
}

//aof检测main函数
int redis_check_aof_main(int argc, char **argv) {
    char *filename;
    int fix = 0;//从传进来的参数获取是否需要对aof文件进行修复
    //从参数表中获取filename
    if (argc < 2) {
        printf("Usage: %s [--fix] <file.aof>\n", argv[0]);
        exit(1);
    } else if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3) {
        //判断第一个参数是不是--fix
        if (strcmp(argv[1],"--fix") != 0) {
            printf("Invalid argument: %s\n", argv[1]);
            exit(1);
        }
        filename = argv[2];
        fix = 1;
    } else {
        printf("Invalid arguments\n");
        exit(1);
    }
    //创建文件流
    FILE *fp = fopen(filename,"r+");
    if (fp == NULL) {
        printf("Cannot open file: %s\n", filename);
        exit(1);
    }

    struct redis_stat sb;
    //判断文件状态
    if (redis_fstat(fileno(fp),&sb) == -1) {
        printf("Cannot stat file: %s\n", filename);
        exit(1);
    }
    //文件大小为0
    off_t size = sb.st_size;
    if (size == 0) {
        printf("Empty file: %s\n", filename);
        exit(1);
    }

    /* This AOF file may have an RDB preamble. Check this to start, and if this
     * is the case, start processing the RDB part. */
    //如果有rdb的部分
    if (size >= 8) {    /* There must be at least room for the RDB header. */
        char sig[5];
        int has_preamble = fread(sig,sizeof(sig),1,fp) == 1 &&
                            memcmp(sig,"REDIS",sizeof(sig)) == 0;
        //重新将偏移指针定位到文件开始位置
        rewind(fp);
        if (has_preamble) {//换rdb的检测方式
            printf("The AOF appears to start with an RDB preamble.\n"
                   "Checking the RDB preamble to start:\n");
            if (redis_check_rdb_main(argc,argv,fp) == C_ERR) {
                printf("RDB preamble of AOF file is not sane, aborting.\n");
                exit(1);
            } else {
                printf("RDB preamble is OK, proceeding with AOF tail...\n");
            }
        }
    }
    //返回最新的偏移位置
    off_t pos = process(fp);
    off_t diff = size-pos;//如果diff不为0代表process校验异常终止
    printf("AOF analyzed: size=%lld, ok_up_to=%lld, diff=%lld\n",
        (long long) size, (long long) pos, (long long) diff);
    if (diff > 0) {
        if (fix) {//如果需要修复aof文件
            char buf[2];
            printf("This will shrink the AOF from %lld bytes, with %lld bytes, to %lld bytes\n",(long long)size,(long long)diff,(long long)pos);
            printf("Continue? [y/N]: ");
            //从终端获取玩家选择
            if (fgets(buf,sizeof(buf),stdin) == NULL ||
                strncasecmp(buf,"y",1) != 0) {
                    printf("Aborting...\n");
                    exit(1);
            }
            //将fp打开的文件修改为pos长度大小的文件，删除diff之后非正确aof格式的数据
            if (ftruncate(fileno(fp), pos) == -1) {
                printf("Failed to truncate AOF\n");
                exit(1);
            } else {
                printf("Successfully truncated AOF\n");
            }
        } else {
            printf("AOF is not valid. "
                   "Use the --fix option to try fixing it.\n");
            exit(1);
        }
    } else {
        printf("AOF is valid\n");
    }
    //关闭文件
    fclose(fp);
    exit(0);
}
