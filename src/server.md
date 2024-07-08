# setlocale
#include <locale.h>
char *setlocale(int category, const char *locale);
参数
category：指定要设置或查询的本地化类别。可以是以下宏之一：
    LC_ALL：设置或查询所有本地化类别。
    LC_COLLATE：设置或查询字符串比较的本地化信息。
    LC_CTYPE：设置或查询字符处理的本地化信息。
    LC_MONETARY：设置或查询货币格式的本地化信息。
    LC_NUMERIC：设置或查询数字格式的本地化信息。
    LC_TIME：设置或查询时间格式的本地化信息。
locale：指定要设置的本地化信息。可以是以下之一：
    ""：设置为用户环境变量中的默认设置。
    NULL：查询当前的本地化信息。
    具体的区域设置名称：如 "en_US.UTF-8"，用于设置特定的区域设置。
返回值
    如果 locale 为 NULL，返回一个指向当前区域设置信息字符串的指针。
    如果 locale 非 NULL 并且成功设置，返回一个指向该区域设置信息字符串的指针。
    如果 locale 非 NULL 并且设置失败，返回 NULL。

设置和查询本地化信息
    在程序开始时，可以使用 setlocale(LC_ALL, ""); 设置程序的本地化信息为用户环境变量中
    的默认设置。用户环境变量通常由操作系统或用户设置，指定了程序在运行时应该使用的默认区
    域设置。
    可以使用 setlocale(LC_ALL, "en_US.UTF-8"); 或其他区域设置字符串设置特定的区域设置。
    使用 setlocale(LC_ALL, NULL); 查询当前的本地化信息，并返回一个描述当前区域设置的字
    符串。
本地化类别
    每个类别控制程序的特定方面：
        LC_COLLATE：影响字符串比较函数（如 strcoll 和 strxfrm）。
        LC_CTYPE：影响字符分类和转换函数（如 isalpha 和 toupper）。
        LC_MONETARY：影响与货币格式相关的函数（如 localeconv）。
        LC_NUMERIC：影响数字格式（如小数点和千位分隔符）。
        LC_TIME：影响日期和时间格式（如 strftime）。
总结
    setlocale 函数是用于设置和查询程序本地化信息的重要工具。它允许程序适应不同的语言和文化习惯，从而实现国际化和本地化。通过使用 setlocale，程序可以正确处理和显示字符串、数字、货币、日期和时间等本地化信息。

# tzset
#incude <time.h >
void tzset(void);
设置时间环境变量。
说明
tzset()函数使用环境变量TZ的当前设置把值赋给三个全局变量:daylight,timezone和tzname。
这些变量由ftime和localtime函数使用校正格林威治(UTC)时间为本地时间,通过time函数从系统时间计算UTC,使用如下语法设置TZ环境变量:

# gettimeofday
获取时间戳

# setsid
实现与其父进程进程组和会话组脱离

# getaddrinfo
#include<netdb.h>
int getaddrinfo( const char *hostname, const char *service, const struct addrinfo *hints, struct addrinfo **result );
getaddrinfo函数能够处理名字到地址以及服务到端口这两 种转换，返回的是一个sockaddr结构的链表而不是一个地址清单。这些sockaddr结构随后可由套接口函数直接使用。
返回0： 成功
返回非0： 出错
struct addrinfo{
    int ai_flags; 
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
};