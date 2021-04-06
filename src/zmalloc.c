/* zmalloc - total amount of allocated memory aware version of malloc()
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* This function provide us access to the original libc free(). This is useful
 * for instance to free results obtained by backtrace_symbols(). We need
 * to define this function before including zmalloc.h that may shadow the
 * free implementation if we use jemalloc or another non standard allocator. */
void zlibc_free(void *ptr) {
    free(ptr);
}

#include <string.h>
#include <pthread.h>
#include "config.h"
#include "zmalloc.h"
#include "atomicvar.h"

#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)
#else
#if defined(__sun) || defined(__sparc) || defined(__sparc__)
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t))
#endif
#endif

/* Explicitly override malloc/free etc when using tcmalloc. */
#if defined(USE_TCMALLOC)
#define malloc(size) tc_malloc(size)
#define calloc(count,size) tc_calloc(count,size)
#define realloc(ptr,size) tc_realloc(ptr,size)
#define free(ptr) tc_free(ptr)
#elif defined(USE_JEMALLOC)
#define malloc(size) je_malloc(size)
#define calloc(count,size) je_calloc(count,size)
#define realloc(ptr,size) je_realloc(ptr,size)
#define free(ptr) je_free(ptr)
#define mallocx(size,flags) je_mallocx(size,flags)
#define dallocx(ptr,flags) je_dallocx(ptr,flags)
#endif

/*
malloc()本身能够保证所分配的内存是8字节对齐的：如果你要分配的内存不是8的倍数，
那么malloc就会多分配一点，来凑成8的倍数。所以update_zmalloc_stat_alloc函数
（或者说zmalloc()相对malloc()而言）真正要实现的功能并不是进行8字节对齐
（malloc已经保证了），它的真正目的是使变量used_memory精确的维护实际已分配内存的大小。
*/
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    //这段代码就是判断分配的内存空间的大小是不是sizeof(long)的倍数。
    //如果内存大小不是sizeof(long)的倍数，就加上相应的偏移量使之变成sizeof(long)的倍数。
    //_n&(sizeof(long)-1) 在功能上等价于 _n%sizeof(long)，不过位操作的效率显然更高。
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    atomicIncr(used_memory,__n); \
} while(0)

#define update_zmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    atomicDecr(used_memory,__n); \
} while(0)
//定义全局静态变量used_memory
static size_t used_memory = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

static void zmalloc_default_oom(size_t size) {
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    /*
    fflush是一个在C语言标准输入输出库中的函数，功能是冲洗流中的信息，
    该函数通常用于处理磁盘文件。fflush()会强迫将缓冲区内的数据写回参数stream 指定的文件中。
    */
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;
//参数size是我们需要分配的内存大小
//实际上我们调用malloc实际分配的大小是size+PREFIX_SIZE。PREFIX_SIZE是一个条件编译的宏，
//不同的平台有不同的结果，在Linux中其值是sizeof(size_t)，
//所以我们多分配了一个字长(8个字节)的空间
//（后面代码可以看到多分配8个字节的目的是用于储存size的值）
void *zmalloc(size_t size) {
    //从内存中获取这么一段的空间加上一个预留的默认空间
    void *ptr = malloc(size+PREFIX_SIZE);
    //如果ptr指针为NULL（内存分配失败），调用zmalloc_oom_handler（size）。
    //该函数实际上是一个函数指针指向函数zmalloc_default_oom，
    //其主要功能就是打印错误信息并终止程序。
    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
/*
第一行就是在已分配空间的第一个字长（前8个字节）处存储需要分配的字节大小（size）。
第二行调用了update_zmalloc_stat_alloc()【宏函数】，
它的功能是更新全局变量used_memory（已分配内存的大小）的值（源码解读见下一节）。
第三行返回的（char *）ptr+PREFIX_SIZE。
就是将已分配内存的起始地址向右偏移PREFIX_SIZE * sizeof(char)的长度（即8个字节），
此时得到的新指针指向的内存空间的大小就等于size了。
*/
#else
    *((size_t*)ptr) = size;
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //指针偏移，所以获取sds[-1]就是这个字段的长度了
    return (char*)ptr+PREFIX_SIZE;
#endif
}

/* Allocation and free functions that bypass the thread cache
 * and go straight to the allocator arena bins.
 * Currently implemented only for jemalloc. Used for online defragmentation. */
#ifdef HAVE_DEFRAG
void *zmalloc_no_tcache(size_t size) {
    void *ptr = mallocx(size+PREFIX_SIZE, MALLOCX_TCACHE_NONE);
    if (!ptr) zmalloc_oom_handler(size);
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
}

void zfree_no_tcache(void *ptr) {
    if (ptr == NULL) return;
    update_zmalloc_stat_free(zmalloc_size(ptr));
    dallocx(ptr, MALLOCX_TCACHE_NONE);
}
#endif
/*
calloc()的功能是也是分配内存空间，与malloc()的不同之处有两点：
它分配的空间大小是 size * nmemb。比如calloc(10,sizoef(char)); // 分配10个字节
calloc()会对分配的空间做初始化工作（初始化为0），而malloc()不会
*/
void *zcalloc(size_t size) {
    /*
    zcalloc()中没有calloc()的第一个函数nmemb。因为它每次调用calloc(),其第一个参数都是1。
    也就是说zcalloc()功能是每次分配 size+PREFIX_SIZE 的空间，并初始化。
    其余代码的分析和zmalloc()相同，也就是说：
    zcalloc()和zmalloc()具有相同的编程接口，实现功能基本相同，
    唯一不同之处是zcalloc()会做初始化工作，而zmalloc()不会。
    */
    void *ptr = calloc(1, size+PREFIX_SIZE);

    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;
#endif
}
/*
要完成的功能是给首地址ptr的内存空间，重新分配大小。如果失败了，
则在其它位置新建一块大小为size字节的空间，将原先的数据复制到新的内存空间，
并返回这段内存首地址【原内存会被系统自然释放】。
*/
void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;

    if (size == 0 && ptr != NULL) {
        zfree(ptr);
        return NULL;
    }
    if (ptr == NULL) return zmalloc(size);
#ifdef HAVE_MALLOC_SIZE
    oldsize = zmalloc_size(ptr);
    newptr = realloc(ptr,size);
    if (!newptr) zmalloc_oom_handler(size);

    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(zmalloc_size(newptr));
    return newptr;
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);

    *((size_t*)newptr) = size;
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)newptr+PREFIX_SIZE;
#endif
}

/* Provide zmalloc_size() for systems where this function is not provided by
 * malloc itself, given that in that case we store a header with this
 * information as the first bytes of every allocation. */
/*
 这个函数是一个条件编译的函数，通过阅读zmalloc.h文件，
 我们可以得知zmalloc_size()依据不同的平台，具有不同的宏定义，
 因为在某些平台上提供查询已分配内存实际大小的函数，可以直接#define zmalloc_size(p)：
tc_malloc_size(p)               【tcmalloc】
je_malloc_usable_size(p)【jemalloc】 
malloc_size(p)                 【Mac系统】
*/
#ifndef HAVE_MALLOC_SIZE
size_t zmalloc_size(void *ptr) {
    void *realptr = (char*)ptr-PREFIX_SIZE;
    size_t size = *((size_t*)realptr);
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */
    //继续字节对齐，就像malloc分配空间是以字节为最小单位的
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1));
    return size+PREFIX_SIZE;
}
size_t zmalloc_usable(void *ptr) {
    return zmalloc_size(ptr)-PREFIX_SIZE;
}
#endif

void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_free(zmalloc_size(ptr));
    free(ptr);
#else
    //获取最初的地址，因为前置了一段字节用来存放数据空间大小
    realptr = (char*)ptr-PREFIX_SIZE;
    //获取到这个数据块占用空间的长度
    oldsize = *((size_t*)realptr);
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
#endif
}
//字符串复制
char *zstrdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = zmalloc(l);
    /*
    这是标准C【ANSI C】中用于内存复制的函数，在头文件<string.h>中（gcc）。声明如下：
    void *memcpy(void *dest, const void *src, size_t n);
    dest即目的地址，src是源地址。n是要复制的字节数。
    */
    memcpy(p,s,l);
    return p;
}

size_t zmalloc_used_memory(void) {
    size_t um;
    atomicGet(used_memory,um);
    return um;
}

void zmalloc_set_oom_handler(void (*oom_handler)(size_t)) {
    zmalloc_oom_handler = oom_handler;
}

/* Get the RSS information in an OS-specific way.
 *
 * WARNING: the function zmalloc_get_rss() is not designed to be fast
 * and may not be called in the busy loops where Redis tries to release
 * memory expiring or swapping out objects.
 *
 * For this kind of "fast RSS reporting" usages use instead the
 * function RedisEstimateRSS() that is a much faster (and less precise)
 * version of the function. */

#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/*
获取当前进程实际所驻留在内存中的空间大小，即不包括被交换（swap）出去的空间。
我们所申请的内存空间不会全部常驻内存，
系统会把其中一部分暂时不用的部分从内存中置换到swap区
当前进程的 /proc/<pid>/stat 【<pid>表示当前进程id】文件中进行检索。
该文件的第24个字段是RSS的信息，它的单位是pages（内存页的数目）
*/
size_t zmalloc_get_rss(void) {
    //通过调用库函数sysconf()【大家可以man sysconf查看详细内容】来查询内存页的大小。
    int page = sysconf(_SC_PAGESIZE);
    size_t rss;
    char buf[4096];
    char filename[256];
    int fd, count;
    char *p, *x;
    /*
    getpid()就是获得当前进程的id，
    所以这个snprintf()的功能就是将当前进程所对应的stat文件的
    绝对路径名保存到字符数组filename中。
    */
    snprintf(filename,256,"/proc/%d/stat",getpid());
    /*
    以只读模式打开 /proc/<pid>/stat 文件。然后从中读入4096个字符到字符数组buf中。
    如果失败就关闭文件描述符fd，并退出
    */
    if ((fd = open(filename,O_RDONLY)) == -1) return 0;
    if (read(fd,buf,4096) <= 0) {
        close(fd);
        return 0;
    }
    close(fd);
    /*
    RSS在stat文件中的第24个字段位置，所以就是在第23个空格的后面。
    观察while循环，循环体中用到了字符串函数strchr()，这个函数在字符串p中查询空格字符，
    如果找到就把空格所在位置的字符指针返回并赋值给p，找不到会返回NULL指针。
    p++原因是因为，p当前指向的是空格，在执行自增操作之后就指向下一个字段的首地址了。
    如此循环23次，最终p就指向第24个字段的首地址了。
    */
    p = buf;
    count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
    while(p && count--) {
        p = strchr(p,' ');
        if (p) p++;
    }
    //判断是否存在第23个空格
    if (!p) return 0;
    //指向下一个空格，p，x之间的就是所需的数据
    x = strchr(p,' ');
    if (!x) return 0;
    //将第24个字段之后的空格设置为'\0'，这样p就指向一个一般的C风格字符串了
    *x = '\0';
    /*
    strtoll()：顾名思义就是string to long long的意思啦。
    它有三个参数，前面两个参数表示要转换的字符串的起始和终止位置（字符指针类型），
    NULL和'\0'是等价的。最后一个参数表示的是“进制”，这里就是10进制了。
    */
    rss = strtoll(p,NULL,10);
    /*
    rss和page相乘并返回，因为rss获得的实际上是内存页的页数，
    page保存的是每个内存页的大小（单位字节），相乘之后就表示RSS实际的内存大小了。
    */
    rss *= page;
    return rss;
}
#elif defined(HAVE_TASKINFO)
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/task.h>
#include <mach/mach_init.h>

size_t zmalloc_get_rss(void) {
    task_t task = MACH_PORT_NULL;
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

    if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
        return 0;
    task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

    return t_info.resident_size;
}
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>

size_t zmalloc_get_rss(void) {
    struct kinfo_proc info;
    size_t infolen = sizeof(info);
    int mib[4];
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    if (sysctl(mib, 4, &info, &infolen, NULL, 0) == 0)
        return (size_t)info.ki_rssize;

    return 0L;
}
#else
size_t zmalloc_get_rss(void) {
    /* If we can't get the RSS in an OS-specific way for this system just
     * return the memory usage we estimated in zmalloc()..
     *
     * Fragmentation will appear to be always 1 (no fragmentation)
     * of course... */
    return zmalloc_used_memory();
}
#endif

#if defined(USE_JEMALLOC)

int zmalloc_get_allocator_info(size_t *allocated,
                               size_t *active,
                               size_t *resident) {
    uint64_t epoch = 1;
    size_t sz;
    *allocated = *resident = *active = 0;
    /* Update the statistics cached by mallctl. */
    sz = sizeof(epoch);
    je_mallctl("epoch", &epoch, &sz, &epoch, sz);
    sz = sizeof(size_t);
    /* Unlike RSS, this does not include RSS from shared libraries and other non
     * heap mappings. */
    je_mallctl("stats.resident", resident, &sz, NULL, 0);
    /* Unlike resident, this doesn't not include the pages jemalloc reserves
     * for re-use (purge will clean that). */
    je_mallctl("stats.active", active, &sz, NULL, 0);
    /* Unlike zmalloc_used_memory, this matches the stats.resident by taking
     * into account all allocations done by this process (not only zmalloc). */
    je_mallctl("stats.allocated", allocated, &sz, NULL, 0);
    return 1;
}

void set_jemalloc_bg_thread(int enable) {
    /* let jemalloc do purging asynchronously, required when there's no traffic 
     * after flushdb */
    char val = !!enable;
    je_mallctl("background_thread", NULL, 0, &val, 1);
}

int jemalloc_purge() {
    /* return all unused (reserved) pages to the OS */
    char tmp[32];
    unsigned narenas = 0;
    size_t sz = sizeof(unsigned);
    if (!je_mallctl("arenas.narenas", &narenas, &sz, NULL, 0)) {
        sprintf(tmp, "arena.%d.purge", narenas);
        if (!je_mallctl(tmp, NULL, 0, NULL, 0))
            return 0;
    }
    return -1;
}

#else

int zmalloc_get_allocator_info(size_t *allocated,
                               size_t *active,
                               size_t *resident) {
    *allocated = *resident = *active = 0;
    return 1;
}

void set_jemalloc_bg_thread(int enable) {
    ((void)(enable));
}

int jemalloc_purge() {
    return 0;
}

#endif

#if defined(__APPLE__)
/* For proc_pidinfo() used later in zmalloc_get_smap_bytes_by_field().
 * Note that this file cannot be included in zmalloc.h because it includes
 * a Darwin queue.h file where there is a "LIST_HEAD" macro (!) defined
 * conficting with Redis user code. */
#include <libproc.h>
#endif

/* Get the sum of the specified field (converted form kb to bytes) in
 * /proc/self/smaps. The field must be specified with trailing ":" as it
 * apperas in the smaps output.
 *
 * If a pid is specified, the information is extracted for such a pid,
 * otherwise if pid is -1 the information is reported is about the
 * current process.
 *
 * Example: zmalloc_get_smap_bytes_by_field("Rss:",-1);
 */
#if defined(HAVE_PROC_SMAPS)
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid) {
    char line[1024];
    size_t bytes = 0;
    int flen = strlen(field);
    FILE *fp;
    /*
    用标准C的fopen()以只读方式打开/proc/self/smaps文件。简单介绍一下该文件，
    前面我们已经说过/proc目录下有许多以进程id命名的目录，里面保存着每个进程的状态信息，
    而/proc/self目录的内容和它们是一样的，self/ 表示的是当前进程的状态目录。
    而smaps文件中记录着该进程的详细映像信息，该文件内部由多个结构相同的块组成，
    看一下其中某一块的内容：
    00400000-004ef000 r-xp 00000000 08:08 1305603   /bin/bash
    Size:                956 kB
    Rss:                 728 kB 
    Pss:                 364 kB 
    Shared_Clean:        728 kB
    Shared_Dirty:          0 kB
    Private_Clean:         0 kB
    Private_Dirty:         0 kB
    Referenced:          728 kB
    Anonymous:             0 kB
    AnonHugePages:         0 kB
    Swap:                  0 kB
    KernelPageSize:        4 kB
    MMUPageSize:           4 kB
    Locked:                0 kB
    VmFlags: rd ex mr mw me dw sd 

    00400000-004ef000:地址空间的开始地址 - 结束地址 
    r-xp属性:前三个是rwx（读、写、可执行）,如果没有相应的权限则为“-”。最后一个可以是p或者s(p表示私有，s表示共享) 。
    00000000：偏移量，如果这段内存是从文件里映射过来的，则偏移量为这段内容在文件中的偏移量。如果不是从文件里面映射过来的则为0. 
    03:02：文件所在设备的主设备号和子设备号
    1305603：文件号，即/bin/bash的文件号
    /bin/bash：文件名
    Rss：Resident Set Size 实际使用物理内存（包含共享库占用的内存） 
        Rss的大小=Shared_Clean+Shared_Dirty+Private_Clean+Private_Dirty 
        Shared_Clean:多进程共享的内存，且其内容未被任意进程修改 
        Shared_Dirty:多进程共享的内存，但其内容被某个进程修改 
        Private_Clean:某个进程独享的内存，且其内容没有修改 
        Private_Dirty:某个进程独享的内存，但其内容被该进程修改
    Pss：实际使用的物理内存（按比例包含共享库占用的内存）。比如四个进程共享同一个占内存1000MB的共享库，每个进程算进250MB在Pss。 
    Shared_Clean 、 Shared_Dirty 、 Private_Clean、 Private_Dirty：
        （shared/private）共享和私有 
    一个页的clean字段表示没有更改此页，当发生换页时不用写回。dirty表示更改了此页，
    当发生换页时要写回磁盘。此处这四个值是遍历页表中各个页后得到的。 
    */
    if (pid == -1) {
        fp = fopen("/proc/self/smaps","r");
    } else {
        //打开特定进程的smaps
        char filename[128];
        snprintf(filename,sizeof(filename),"/proc/%ld/smaps",pid);
        fp = fopen(filename,"r");
    }

    if (!fp) return 0;
    //利用fgets()逐行读取/proc/self/smaps文件内容
    while(fgets(line,sizeof(line),fp) != NULL) {
        //遍历每一行查找到特定字段的内容
        if (strncmp(line,field,flen) == 0) {
            //然后strchr()将p指针定义到字符k的位置
            char *p = strchr(line,'k');
            if (p) {
                //将k设置为\0
                *p = '\0';
                /*
                line指向的该行的首字符，line+flen（要查询的字段的长度）所指向的位置就是字段名后面的空格处了，
                不必清除空格，strtol()无视空格可以将字符串转换成int类型
                strol()转换的结果再乘以1024，这是因为smaps里面的大小是kB表示的，
                我们要返回的是B（字节byte）表示
                */
                bytes += strtol(line+flen,NULL,10) * 1024;
            }
        }
    }
    fclose(fp);
    return bytes;
}
#else
/* Get sum of the specified field from libproc api call.
 * As there are per page value basis we need to convert
 * them accordingly.
 *
 * Note that AnonHugePages is a no-op as THP feature
 * is not supported in this platform
 */
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid) {
#if defined(__APPLE__)
    struct proc_regioninfo pri;
    if (proc_pidinfo(pid, PROC_PIDREGIONINFO, 0, &pri, PROC_PIDREGIONINFO_SIZE) ==
	PROC_PIDREGIONINFO_SIZE) {
	if (!strcmp(field, "Private_Dirty:")) {
            return (size_t)pri.pri_pages_dirtied * 4096;
	} else if (!strcmp(field, "Rss:")) {
            return (size_t)pri.pri_pages_resident * 4096;
	} else if (!strcmp(field, "AnonHugePages:")) {
            return 0;
	}
    }
    return 0;
#endif
    ((void) field);
    ((void) pid);
    return 0;
}
#endif

size_t zmalloc_get_private_dirty(long pid) {
    return zmalloc_get_smap_bytes_by_field("Private_Dirty:",pid);
}

/* Returns the size of physical memory (RAM) in bytes.
 * It looks ugly, but this is the cleanest way to achieve cross platform results.
 * Cleaned up from:
 *
 * http://nadeausoftware.com/articles/2012/09/c_c_tip_how_get_physical_memory_size_system
 *
 * Note that this function:
 * 1) Was released under the following CC attribution license:
 *    http://creativecommons.org/licenses/by/3.0/deed.en_US.
 * 2) Was originally implemented by David Robert Nadeau.
 * 3) Was modified for Redis by Matt Stancliff.
 * 4) This note exists in order to comply with the original license.
 */
size_t zmalloc_get_memory_size(void) {
#if defined(__unix__) || defined(__unix) || defined(unix) || \
    (defined(__APPLE__) && defined(__MACH__))
#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
    mib[1] = HW_MEMSIZE;            /* OSX. --------------------- */
#elif defined(HW_PHYSMEM64)
    mib[1] = HW_PHYSMEM64;          /* NetBSD, OpenBSD. --------- */
#endif
    int64_t size = 0;               /* 64-bit */
    size_t len = sizeof(size);
    if (sysctl( mib, 2, &size, &len, NULL, 0) == 0)
        return (size_t)size;
    return 0L;          /* Failed? */

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    /* FreeBSD, Linux, OpenBSD, and Solaris. -------------------- */
    return (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGESIZE);

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
    /* DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. -------- */
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_REALMEM)
    mib[1] = HW_REALMEM;        /* FreeBSD. ----------------- */
#elif defined(HW_PHYSMEM)
    mib[1] = HW_PHYSMEM;        /* Others. ------------------ */
#endif
    unsigned int size = 0;      /* 32-bit */
    size_t len = sizeof(size);
    if (sysctl(mib, 2, &size, &len, NULL, 0) == 0)
        return (size_t)size;
    return 0L;          /* Failed? */
#else
    return 0L;          /* Unknown method to get the data. */
#endif
#else
    return 0L;          /* Unknown OS. */
#endif
}

#ifdef REDIS_TEST
#define UNUSED(x) ((void)(x))
int zmalloc_test(int argc, char **argv) {
    void *ptr;

    UNUSED(argc);
    UNUSED(argv);
    printf("Initial used memory: %zu\n", zmalloc_used_memory());
    ptr = zmalloc(123);
    printf("Allocated 123 bytes; used: %zu\n", zmalloc_used_memory());
    ptr = zrealloc(ptr, 456);
    printf("Reallocated to 456 bytes; used: %zu\n", zmalloc_used_memory());
    zfree(ptr);
    printf("Freed pointer; used: %zu\n", zmalloc_used_memory());
    return 0;
}
#endif
