# epoll_create介绍url
https://blog.csdn.net/JMW1407/article/details/107963618
https://www.cnblogs.com/xuewangkai/p/11158576.html

epoll的接口非常简单，一共就三个函数：
# epoll_create
#include <sys/epoll.h> 
int epoll_create(int size);
创建一个epoll的句柄，size用来告诉内核这个监听的数目一共有多大。这个参数不同于select()中的第一个参数，给出最大监听的fd+1的值。需要注意的是，当创建好epoll句柄后，它就是会占用一个fd值，在linux下如果查看/proc/进程id/fd/，是能够看到这个fd的，所以在使用完epoll后，必须调用close()关闭，否则可能导致fd被耗尽。
epoll把用户关心的文件描述符上的事件放在内核里的一个事件表中，从而无须像select和poll那样每次调用都要重复传入文件描述符集或事件集。
调用epoll create时，内核除了帮我们在epoll文件系统里建了个fi1e结点(epo11_create创建的文件描述符)，在内核cache里建了个红黑树用于存储以后epoll_ctl传来的socket外，还会再建立一个list链表，用于存储准备就绪的事件,(概括就是:调用epoll_create方法时，内核会跟着创建一个eventpoll对象)
eventpoll结构如下：
    struct eventpoll{
        spin_lock_t lock;               //对本数据结构的访问
        struct mutex mtx;               //防止使用时被删除
        wait_queue_head_t wg;           //sys_epoll_wait()使用的等待队列
        wait_queue_head_t poll_wait;    //file->poll()使用的等待队列
        struct list_head rdllist;       //事件满足条件的链表
        struct rb_root rbr:             /用于管理所有fd的红黑树
        struct epitem *ovflist;         //将事件到达的fd进行链接起来发送至用户空间
    }
注意：size参数只是告诉内核这个epoll对象会处理的事件大致数目，而不是能够处理的事件的最大个数。在Linux最新的一些内核版本的实现中，这个size参数没有任何意义。

# epoll_ctl
#include <sys/epoll.h> 
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
epoll的事件注册函数，epoll_ctl向 epoll对象中添加、修改或者删除感兴趣的事件，返回0表示成功，否则返回–1，此时需要根据errno错误码判断错误类型。
它不同与select()是在监听事件时告诉内核要监听什么类型的事件，而是在这里先注册要监听的事件类型。
epoll_wait方法返回的事件必然是通过 epoll_ctl添加到epoll中的。
参数说明：
epfd:epoll_create()的返回值
op:表示操作类型，用三个宏来表示：
    EPOLL_CTL_ADD：注册新的fd到epfd中；
    EPOLL_CTL_MOD：修改已经注册的fd的监听事件；
    EPOLL_CTL_DEL：从epfd中删除一个fd；
fd:需要监听的fd
event:告诉内核需要监听什么事，
struct epoll_event结构如下：
    typedef union epoll_data {
        void *ptr;          //指定与fd相关的用户数据
        int fd;             //指定事件所从属的目标文件描述符
        __uint32_t u32;
        __uint64_t u64;
    } epoll_data_t;
    struct epoll_event {
        __uint32_t events; /* Epoll events */
        epoll_data_t data; /* User data variable */
    };
events可以是以下几个宏的集合：
    EPOLLIN ：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；
    EPOLLOUT：表示对应的文件描述符可以写；
    EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
    EPOLLERR：表示对应的文件描述符发生错误；
    EPOLLHUP：表示对应的文件描述符被挂断；
    EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
    EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里

# epoll_wait
#include <sys/epoll.h> 
int epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout);
等待事件的产生，类似于select()调用。参数events用来从内核得到事件的集合，maxevents告之内核这个events有多大，这个maxevents的值不能大于创建epoll_create()时的size，参数timeout是超时时间（毫秒，0会立即返回，-1将不确定，也有说法说是永久阻塞）。该函数返回需要处理的事件数目，如返回0表示已超时。如果返回–1，则表示出现错误，需要检查errno错误码判断错误类型。
参数：
epfd:epoll_create()的返回值
events:分配好的epoll_event结构体数组，epoll将会把发生的事件复制到events数组中（events不可以是空指针，内核只负责把数据复制到这个events数组中，不会去帮助我们在用户态中分配内存。内核这种做法效率很高）。
maxevents:表示本次可以返回的最大事件数目，通常maxevents参数与预分配的events数组的大小是相等的。
timeout:表示在没有检测到事件发生时最多等待的时间（单位为毫秒），
    timeout=-1:表示调用将一直阻塞，直到有文件描述符进入ready状态或者捕获到信号才返回;
    timeout=0:用于非阻塞检测是否有描述符处于ready状态，不管结果怎么样，调用都立即返回;。
    timeout>0:表示调用将最多持续tmeout时间，如果期间有检测对象变为ready状态或者捕获到信号则返回，否则直到超时,

    timeout 参数设计技巧问题?
    。1、设置为-1，程序阻塞在此，后续任务没法执行。
    。2、设置为0，程序能继续跑，但即使没事件时，程序也在空转，十分占用cpu时间片，我测试时每个进程都是60+%的cpu占用时间
    综上，我们给出比较好的设置方法: 将其设置为1，但还没完，因为即使这样设置，处理其它任务时，在每次循环都会在这浪费1ms的阻塞时间，多次循环
    后性能损失就比较明显了
        为了避免该现象，我们通常向epo11再添加一个fd，我们有其它任务要执行时直接向该fd随便写入一个字节 ，将epo11唤醒从而跳过阳0塞时间。没
    任务时epoll超过阻塞时间1ms也会自动挂起，不会占用cpu，两全其美。
        int eventfd(unsigned int initval, int flags)，linux中是一个较新的进程通信方式，可以通过它写入字节,

# fcntl
功能描述：根据文件描述词来操作文件的特性。
文件控制函数          fcntl -- file control
头文件：
#include <unistd.h>
#include <fcntl.h>
函数原型：          
int fcntl(int fd, int cmd);
int fcntl(int fd, int cmd, long arg);         
int fcntl(int fd, int cmd, struct flock *lock);
描述：
fcntl()针对(文件)描述符提供控制.参数fd是被参数cmd操作(如下面的描述)的描述符.            
针对cmd的值,fcntl能够接受第三个参数（arg）
fcntl函数有5种功能：
    1.复制一个现有的描述符（cmd=F_DUPFD）.
    2.获得／设置文件描述符标记(cmd=F_GETFD或F_SETFD).
    3.获得／设置文件状态标记(cmd=F_GETFL或F_SETFL).
    4.获得／设置异步I/O所有权(cmd=F_GETOWN或F_SETOWN).
    5.获得／设置记录锁(cmd=F_GETLK,F_SETLK或F_SETLKW).

# bzero
#include<string.h>
void bzero(void *s, int n);
bzero() 能够将内存块（字符串）的前n个字节清零
s为内存（字符串）指针，n 为需要清零的字节数。

# socket
创建套接字的函数是socket()，函数原型为：
#include <sys/types.h>
#include <sys/socket.h>
int socket(int domain, int type, int protocol);
其中 “int domain”参数表示套接字要使用的协议簇，协议簇的在“linux/socket.h”里有详细定义，常用的协议簇：
    AF_UNIX（本机通信）
    AF_INET（TCP/IP – IPv4）
    AF_INET6（TCP/IP – IPv6）
其中 “type”参数指的是套接字类型，常用的类型有：
    SOCK_STREAM（TCP流）
    SOCK_DGRAM（UDP数据报）
    SOCK_RAW（原始套接字）
最后一个 “protocol”一般设置为“0”，也就是当确定套接字使用的协议簇和类型时，这个参数的值就为0，但是有时候创建原始套接字时，并不知道要使用的协议簇和类型，也就是domain参数未知情况下，这时protocol这个参数就起作用了，它可以确定协议的种类。
socket是一个函数，那么它也有返回值，当套接字创建成功时，返回套接字，失败返回“-1”，错误代码则写入“errno”中。

# inet_ntoa,inet_aton
inet_ntoa函数所在的头文件：<arpa/inet.h>
函数原型：char *inet_ntoa(struct in_addr in);
函数
函数功能
    将一个网络字节序的IP地址（也就是结构体in_addr类型变量）转化为点分十进制的IP地址（字符串）。
函数形参in_addr
    struct in_addr { 
        in_addr_t s_addr; //按网络顺序存储的IP地址
    }; 
函数返回值
    该函数的返回值是一个字符串，这个字符串是点分十进制的IP地址。
网络字节序
    网络字节序就是将二进制IP地址按“大端存储”所得到的一个无符号数。接下来我们用一个简单例子解释什么按网络字节顺序的存
    储。

inet_aton函数所在的头文件：<arpa/inet.h>
函数原型：int inet_aton(const char *IP, struct in_addr *addr);
函数
函数功能
    将一个字符串表示的点分十进制IP地址IP转换为网络字节序存储在addr中，并且返回该网络字节序表示的无符号整数。
函数形参
    const char *IP:我们输入的点分十进制的IP地址；
    struct in_addr* addr: 将IP转换为网络字节序（大端存储）后并保存在addr中；
函数返回值
    失败：返回0；
    成功：返回点分十进制的IP地址对应的网络字节序表示的无符号整数。

现在有一个点分十进制的IP地址“     159 .    12    .  8       .  109  ” 
相应的二进制表示形式为      “10011111 . 00001100 . 00001000 . 01101101”，这表示该IP的主机字节序。
将上面的二进制变为大端存储： “01101101 . 00001000 . 00001100 . 10011111”,这就是该IP的网络字节序。
inet_aton(const char * IP)就是返回IP对应的网络字节序表示的无符号整数。
inet_ntoa(in_addr n)就是网络字节序n转化为点分十进制的IP。

# inet_pton,inet_ntop
// windows下头文件
#include <ws2tcpip.h>
// linux下头文件
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

##inet_pton()
功能：
    将标准文本表示形式的IPv4或IPv6 Internet网络地址转换为数字二进制形式。
参数：
    INT WSAAPI inet_pton(
        INT   Family,  //地址家族  IPV4使用AF_INET  IPV6使用AF_INET6 
        PCSTR pszAddrString, //指向以NULL为结尾的字符串指针，该字符串包含要转换为数字的二进制形式的IP地址文本形式。
        PVOID pAddrBuf//指向存储二进制表达式的缓冲区
    );
返回值：
    1.若无错误发生，则inet_pton()返回1，pAddrBuf参数执行的缓冲区包含按网络字节顺序的二进制数字IP地址。
    2.若pAddrBuf指向的字符串不是一个有效的IPv4点分十进制字符串或者不是一个有效的IPv6点分十进制字符串，则返回0。否则返回-1。
    3.可以通过WSAGetLastError()获得错误错误代码。
案例：（socket编程中的使用方法）
    sockaddr_in addrServer;
    inet_pton(AF_INET, "127.0.0.1", &addrServer.sin_addr);
    addrServer.sin_port = htons(6000);  // 该函数需要添加头文件： #include <winsock.h>
    addrServer.sin_family = AF_INET;

##inet_ntop()
功能：将IPv4或IPv6 Internet网络地址转换为 Internet标准格式的字符串。
参数：
    PCWSTR WSAAPI InetNtopW(
        INT        Family,  //地址家族  IPV4使用AF_INET  IPV6使用AF_INET6 
        const VOID *pAddr,  //指向网络字节中要转换为字符串的IP地址的指针
        PWSTR      pStringBuf,//指向缓冲区的指针，该缓冲区用于存储IP地址的以NULL终止的字符串表示形式。
        size_t     StringBufSize//输入时，由pStringBuf参数指向的缓冲区的长度（以字符为单位）
    );
举例：（socket中显示连接的IP地址时）
    char buf[20] = { 0 };
    inet_ntop(AF_INET, &recvAddr.sin_addr, buf, sizeof(buf));//其中recvAddr为SOCKADDR_IN类型
    cout << "客户端地址：" << buf << endl;
返回值：
    1.若无错误发生，Inet_ntop()函数返回一个指向缓冲区的指针，该缓冲区包含标准格式的IP地址的字符串表示形式。
    2.否则返回NULL
    3.获取错误码：WSAGetLastError()

# htonl,htons,ntohl,ntohs
头文件：
    #include <arpa/inet.h> 
htonl()函数
    函数原型是：uint32_t htonl(uint32_t hostlong)
    其中,hostlong是主机字节顺序表达的32位数，htonl中的h–host主机地址，to–to,n–net网络，l–unsigned long无符号的长整型(32位的系统是4字节)；
    函数返回值是一个32位的网络字节顺序；
    函数的作用是将一个32位数从主机字节顺序转换成网络字节顺序。
htons()函数
    函数原型是：uint16_t htons(uint16_t hostlong)
    其中,hostlong是主机字节顺序表达的16位数，htons中的h–host主机地址，to–to,n–net网络，s–signed long无符号的短整型(32位的系统是2字节)；
    函数返回值是一个16位的网络字节顺序；
    函数的作用是将一个16位数从主机字节顺序转换成网络字节顺序，简单的说就是把一个16位数高低位呼唤。
ntohs()函数
    函数原型是：uint16_t ntohs(uint16_t hostlong)
    其中,hostlong是网络字节顺序表达的16位数，ntohs中的,n–net网络，to–toh–host主机地址，s–signed long有符号的短整型(32位的系统是2字节)；
    函数返回值是一个16位的主机字节顺序；
    函数的作用是将一个16位数由网络字节顺序转换为主机字节顺序，简单的说就是把一个16位数高低位互换。
ntohl()函数
    函数原型是：uint32_t ntohs(uint32_t hostlong)
    其中,hostlong是网络字节顺序表达的32位数，ntohs中的,n–net网络，to–toh–host主机地址，s–unsigned long无符号的短整型(32位的系统是4字节)；
    函数返回值是一个32位的主机字节顺序；
    函数的作用是将一个32位数由网络字节顺序转换为主机字节顺序。
这些函数存在的意义
    说到这部分需要引入字节存放的两个概念一个是“大端顺序”，一个是“小端顺序”。俗称“小尾顺序”、“大尾顺序”。
    简单的说就是对应数据的高字节存放在低地址，低字节存放在高地址上就是大端顺序，对应数据的高字节存放在高地址，低字节存放在低地址上就是小端顺序。

# bind
int bind(int sockfd, const struct sockaddr *addr,socklen_t addrlen);
    参数 sockfd ，需要绑定的socket。
    参数 addr ，存放了服务端用于通信的地址和端口。
    参数 addrlen ，表示 addr 结构体的大小
    返回值：成功则返回0，失败返回-1，错误原因存于errno中。如果绑定的地址错误，或者端口已被占用，bind函数一定会报错，否
则一般不会返回错误。

# listen
#include <sys/types.h>
#include <sys/socket.h>
int listen(int sockfd, int backlog);
第一个参数sockfd为创建socket返回的文件描述符。
第二个参数backlog为建立好连接处于ESTABLISHED状态的队列的长度。
backlog的最大值128（linux原文描述如下）

# accept
int accept(int sockfd, struct sockaddr* addr, socklen_t* len)  
accept函数主要用于服务器端，一般位于listen函数之后，默认会阻塞进程，直到有一个客户请求连接，建立好连接后，它返回的一个
新的套接字socketfd_new ，此后，服务器端即可使用这个新的套接字socketfd_new与该客户端进行通信，而sockfd则继续用于监听
其他客户端的连接请求。

# read
#include <unistd.h>
ssize_t read(int fd, void * buf, size_t count);
read()会把参数fd所指的文件传送count个字节到buf指针所指的内存中. 若参数count为0, 则read()不会有作用并返回0.
返回值为实际读取到的字节数, 如果返回0, 表示已到达文件尾或是无可读取的数据,此外文件读写位置会随读取到的字节移动.
错误代码：
EINTR 此调用被信号所中断.
EAGAIN 当使用不可阻断I/O 时(O_NONBLOCK), 若无数据可读取则返回此值.
EBADF 参数fd 非有效的文件描述词, 或该文件已关闭.

# write
#include<unistd.h>
ssize_t write(int fd,const void*buf,size_t count);
参数说明：
  fd:是文件描述符（write所对应的是写，即就是1）
  buf:通常是一个字符串，需要写入的字符串
  count：是每次写入的字节数
返回值：
成功：返回写入的字节数
失败：返回-1并设置errno
ps： 写常规文件时，write的返回值通常等于请求写的字节

# 关于ET、LT两种工作模式：
epoll有两种工作模式：
LT（水平触发）模式和ET（边缘触发）模式。
默认情况下，epoll采用LT模式工作，这时可以处理阻塞和非阻塞套接字，而上表中的EPOLLET表示可以将一个事件改为ET模式。ET模式的效率要比LT模式高，它只支持非阻塞套接字。
ET模式与LT模式的区别在于：
当一个新的事件到来时，ET模式下当然可以从epoll_wait调用中获取到这个事件，可是如果这次没有把这个事件对应的套接字缓冲区处理完，在这个套接字没有新的事件再次到来时，在ET模式下是无法再次从epoll_wait调用中获取这个事件的；而LT模式则相反，只要一个事件对应的套接字缓冲区还有数据，就总能从epoll_wait中获取这个事件。因此，在LT模式下开发基于epoll的应用要简单一些，不太容易出错，而在ET模式下事件发生时，如果没有彻底地将缓冲区数据处理完，则会导致缓冲区中的用户请求得不到响应。默认情况下，Nginx是通过ET模式使用epoll的。

LT水平触发(Level Triggered)
    socket接收缓冲区 不为空说明有数据可读，读事件一直触发
    socket 发送缓冲区不满 ，说明可以继续写入数据，写事件一直触发
    符合思维习惯，epoll_wait返回的事件就是socket的状态
LT的处理过程:
    1.accept一个连接，添加到epoll中监听EPOLLIN事件(注意这里没有关注EPOLLOUT事件)
    2.当 EPOLLIN事件 到达时，read fd 中的数据并处理
    3.当需要写出数据时，把数据write到fd中;如果数据较大，无法一次性写出，那么在epoll中监听EPOLLOUT事件
    4.当EPOLLOUT事件到达时，继续把数据write到fd中:如果数据写出完毕，那么在epoll中关闭EPOLLOUT事件。

ET边沿触发(Edge Triggered)
    socket的接收缓冲区状态变化时触发读事件，即空的接收缓冲区刚接收到数据时触发读事件(从无到有)。
    socket的发送缓冲区状态变化时触发写事件，即满的缓冲区刚空出空间时触发读事件(从有到无)。
    仅在状态变化时触发事件
ET的处理过程:
    1.accept一个连接，添加到epoll中监听EPOLLIN|EPOLLOUT事件 ()
    2.当EPOLLIN事件到达时，read fd中的数据并处理，read需要一直读，直到返回EAGAIN为止。
    3.当需要写出数据时，把数据write到fd中，直到数据全部写完，或者write返回EAGAIN
    4.当EPOLLOUT事件到达时，继续把数据write到fd中，直到数据全部写完，或者write返回EAGAIN
从ET的处理过程中可以看到，ET的要求是需要一直读写，直到返回EAGAIN，否则就会遗漏事件。而LT的处理过程中，直到返回EAGAIN不是硬性要求，但通常的处理过程都会读写直到返回EAGAIN，但LT比ET多了一个开关EPOLLOUT事件的步骤
当我们使用ET模式的epoll时，我们应该按照以下规则设计:
    1.在接收到一个!/0事件通知后，立即处理该事件。程序在某个时刻应该在相应的文件描述符上尽可能多地执行I/O
    2.在ET模式下，在使用epoll_ctl注册文件描述符的事件时，应该把描述符设置为非阻塞的(非常重要)
        因为程序采用 循环(ET里面采用whi1e循环，看清楚呦，LT是if判断) 来对文件描述符执行尽可能多的I/O，而文件描述符又被设置为可阻塞的，那
        么最终当没有更多的I/O可执行时，I/O系统调用就会阻塞。基于这个原因，每个被检查的文件描述符通常应该置为非阻塞模式，在得到I/O事件通知
        后重复执行I/O操作 ，直到相应的系统调用(比如read(),wrte()以错误码EAGAIN或EWOULDBLOCK的形式失败。

结论:
ET模式仅当状态发生变化的时候才获得通知,这里所谓的状态的变化并不包括缓冲区中还有未处理的数据,也就是说,如果要采用ET模式,需要一直read/write直到出错为止,很多人反映为什么采用ET模式只接收了一部分数据就再也得不到通知了,大多因为这样;而LT模式是只要有数据没有处理就会一直通知下去的.

常见用法：
#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

using namespace std;

#define MAXLINE 5
#define OPEN_MAX 100
#define LISTENQ 20
#define SERV_PORT 5000
#define INFTIM 1000

//文件描述符增加非阻塞标记
void setnonblocking(int sock)
{
    int opts;
    //获取文件描述符的opts
    opts=fcntl(sock,F_GETFL);
    if(opts<0)
    {
        perror("fcntl(sock,GETFL)");
        exit(1);
    }
    //opts增加O_NONBLOCK标识
    opts = opts|O_NONBLOCK;
    //将增加了新标识的opts赋值给sock
    if(fcntl(sock,F_SETFL,opts)<0)
    {
        perror("fcntl(sock,SETFL,opts)");
        exit(1);
    }
}

int main(int argc, char* argv[])
{
    int i, listenfd, connfd, sockfd,epfd,nfds, portnumber;
    ssize_t n;
    char line[MAXLINE];
    socklen_t clilen;

    if ( 2 == argc )
    {
        if( (portnumber = atoi(argv[1])) < 0 )
        {
            fprintf(stderr,"Usage:%s portnumber/a/n",argv[0]);
            return 1;
        }
    }
    else
    {
        fprintf(stderr,"Usage:%s portnumber/a/n",argv[0]);
        return 1;
    }
    //声明epoll_event结构体的变量,ev用于注册事件,数组用于回传要处理的事件
    struct epoll_event ev,events[20];
    //生成用于处理accept的epoll专用的文件描述符
    epfd=epoll_create(256);
    /*
    #include <netinet/in.h>
    struct sockaddr_in{
        unsigned short         sin_family;    //地址类型，对于基于TCP/IP传输协议的通信，该值只能是AF_INET；
        unsigned short int     sin_port;      //端口号，例如：21 或者 80 或者 27015，总之在0 ~ 65535之间；
        struct in_addr         sin_addr;      //32位的IP地址，例如：192.168.1.5 或 202.96.134.133；
        unsigned char          sin_zero[8];   //填充字节，一般情况下该值为0；
    };
    */
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    //把socket设置为非阻塞方式
    //setnonblocking(listenfd);
    //设置与要处理的事件相关的文件描述符
    ev.data.fd=listenfd;
    //设置要处理的事件类型，设置LT还是ET默认LT
    ev.events=EPOLLIN|EPOLLET;
    //ev.events=EPOLLIN;
    //注册epoll事件
    epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
    //将serveraddr所有字节都清0，感觉可以当struct的初始化零值
    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    char *local_addr="127.0.0.1";
    //将127.0.0.1转化为网络字节序的无符号整数并保存在serveraddr.sin_addr中
    inet_aton(local_addr,&(serveraddr.sin_addr));//htons(portnumber);
    //字面意思的确是端口但是htons方式好像是转换数字为网络字节
    serveraddr.sin_port=htons(portnumber);
    //将通信的地址和端口绑定在listenfd上
    bind(listenfd,(sockaddr *)&serveraddr, sizeof(serveraddr));
    /*
        if (bind(listenfd,(sockaddr *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
            wprintf(L"bind function failed with error %d\n", WSAGetLastError());
            bind_result = closesocket(listenfd);
            if (bind_result == SOCKET_ERROR)
                wprintf(L"closesocket function failed with error %d\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }
    */
    listen(listenfd, LISTENQ);//LISTENQ=20
    /*
        if (listen(listenfd, LISTENQ); == SOCKET_ERROR)
            wprintf(L"listen function failed with error: %d\n", WSAGetLastError());
    */
    /*
        个人理解
        通过epoll_wait获取在timeout时间内最多maxevents数量的event，再通过event中fd的判断区分是新连接还是老的连接区分处理，新连接则生成新的listenfd_new,老的连接则判断IN还是OUT状态执行对event中fd读或者写操作，所有的操作都通过生成一个新的event，并将fd和opt标识（可以设置ET还是LT）写入event然后通过epoll_ctl的方式让epoll核心统一去处理
    */
    for ( ; ; ) {
        //获取可以处理的event数据（最多20个超时时间为500ms，数据会写入events中并将实际数量通过返回值赋值给nfds）
        nfds=epoll_wait(epfd,events,LISTENQ,500);
        //处理所发生的所有事件
        for(i=0;i<nfds;++i)
        {
            //如果待处理的event中的fd为监听的listenfd代表为新的连接请求，已经建立连接的请求会生成一个新的listenfd_new
            if(events[i].data.fd==listenfd)//如果新监测到一个SOCKET用户连接到了绑定的SOCKET端口，建立新的连接。
            {
                //会针对这个这个连接生成listenfd_new,服务端会通过这个listenfd_new与客户端进行通信
                listenfd_new = accept(listenfd,(sockaddr *)&clientaddr, &clilen);
                if(listenfd_new<0){
                    perror("listenfd_new<0");
                    exit(1);
                }
                //setnonblocking(listenfd_new);
                char *str = inet_ntoa(clientaddr.sin_addr);
                cout << "accapt a connection from " << str << endl;
                //设置用于读操作的文件描述符，这边会设置新的listenfd_new
                ev.data.fd=listenfd_new;
                //设置用于注测的读操作事件
                ev.events=EPOLLIN|EPOLLET;
                //ev.events=EPOLLIN;
                //注册ev，将这个fd添加到epoll内核中
                epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd_new,&ev);
            }
            else if(events[i].events&EPOLLIN)//如果是已经连接的用户，并且收到数据，那么进行读入。
            {
                cout << "EPOLLIN" << endl;
                if ( (sockfd = events[i].data.fd) < 0)//fd不合法
                    continue;
                if ( (n = read(sockfd, line, MAXLINE)) < 0) {//从socket中读取maxline字节的内容到line中，n为实际读到的字节数
                    if (errno == ECONNRESET) {
                        close(sockfd);
                        events[i].data.fd = -1;
                    } else
                        std::cout<<"readline error"<<std::endl;
                } else if (n == 0) {//没有数据可读
                    close(sockfd);
                    events[i].data.fd = -1;
                }
                line[n] = '/0';//设置结束标记
                cout << "read " << line << endl;
                //设置用于写操作的文件描述符
                ev.data.fd=sockfd;
                //设置用于注测的写操作事件
                ev.events=EPOLLOUT|EPOLLET;
                //修改sockfd上要处理的事件为EPOLLOUT
                epoll_ctl(epfd,EPOLL_CTL_MOD,sockfd,&ev);
            }
            else if(events[i].events&EPOLLOUT) // 如果有数据发送
            {
                sockfd = events[i].data.fd;
                //将已读到的内容写入sockfd中
                write(sockfd, line, n);
                //设置用于读操作的文件描述符
                ev.data.fd=sockfd;
                //已写则将状态设置为IN设置用于注测的读操作事件
                ev.events=EPOLLIN|EPOLLET;
                //修改sockfd上要处理的事件为EPOLIN
                epoll_ctl(epfd,EPOLL_CTL_MOD,sockfd,&ev);
            }
        }
    }
    return 0;
}