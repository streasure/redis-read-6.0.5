# realloc 
       原型：extern void *realloc(void *mem_address, unsigned int newsize); 
       用法：#include <stdlib.h> 有些编译器需要#include <alloc.h> 
       功能：改变mem_address所指内存区域的大小为newsize长度。 
       说明：如果重新分配成功则返回指向被分配内存的指针，否则返回空指针NULL。 
                 当内存不再使用时，应使用free()函数将内存块释放。 
       注意：这里原始内存中的数据还是保持不变的。 
       详细说明及注意要点： 
            1、如果有足够空间用于扩大mem_address指向的内存块，则分配额外内存，并返回mem_address 
            这里说的是“扩大”，我们知道，realloc是从堆上分配内存的，当扩大一块内存空间时， realloc()试图直接从堆上现存的数据后面的那些字节中获得附加的字节，如果能够满足，自然天下太平。也就是说，如果原先的内存大小后面还有足够的空闲空间用来分配，加上原来的空间大小＝ newsize。那么就ok。得到的是一块连续的内存。 
            2、如果原先的内存大小后面没有足够的空闲空间用来分配，那么从堆中另外找一块newsize大小的内存。 
            并把原来大小内存空间中的内容复制到newsize中。返回新的mem_address指针。（数据被移动了）。 
            老块被放回堆上。新分配的内存中的数据不可知 
            3、也可用来缩小长度

# memcmp函数原型
     int memcmp(const void *str1, const void *str2, size_t n));
参数
     str1-- 指向内存块的指针。
     str2-- 指向内存块的指针。
     n-- 要被比较的字节数。
功能
     比较内存区域str1和str2的前n个字节。
返回值
     如果返回值 < 0，则表示 str1 小于 str2。
     如果返回值 > 0，则表示 str2 小于 str1。
     如果返回值 = 0，则表示 str1 等于 str2。

# memcpy函数原型
void *memcpy(void *str1, const void *str2, size_t n)从存储区str2复制n个字节到存储区str1。
返回值
该函数返回一个指向目标存储区 str1 的指针。

# memmove函数原型
void *memmove(void *str1, const void *str2, size_t n) 
从str2复制n个字符到str1，但是在重叠内存块这方面，memmove()是比memcpy()更安全的方法。如果目标区域和源区域有重叠的话，memmove()能够保证源串在被覆盖之前将重叠区域的字节拷贝到目标区域中，复制后源区域的内容会被更改。如果目标区域与源区域没有重叠，则和memcpy()函数功能相同。

# memset
void *memset(void *str, int c, size_t n) 用于将一段内存区域设置为指定的值。
memset() 函数将指定的值 c 复制到 str 所指向的内存区域的前 n 个字节中，这可以用于将
内存块清零或设置为特定值。
在一些情况下，需要快速初始化大块内存为零或者特定值，memset() 可以提供高效的实现。
在清空内存区域或者为内存区域赋值时，memset() 是一个常用的工具函数。
参数
     str -- 指向要填充的内存区域的指针。
     c -- 要设置的值，通常是一个无符号字符。
     n -- 要被设置为该值的字节数。
返回值
     该值返回一个指向存储区 str 的指针。

注意事项
     memset() 并不对指针 ptr 指向的内存区域做边界检查，因此使用时需要确保 ptr 指向
的内存区域足够大，避免发生越界访问。
     memset() 的第二个参数 value 通常是一个 int 类型的值，但实际上只使用了该值的低
8位。这意味着在范围 0 到 255 之外的其他值可能会产生未定义的行为。
     num 参数表示要设置的字节数，通常是通过 sizeof() 或其他手段计算得到的。

# isinf(arg)
确定给定的浮点数arg是正的还是负的无穷大。该宏返回一个整数值。
isnan()函数是cmath标头的库函数，用于检查给定的值是否为NaN(非数字)。 它接受一个值( float ， double或long double )，如果给定值为NaN，则返回1；否则，返回1。 0，否则。

# likely和unlikely
对于条件选择语句，gcc内建了一条指令用于优化，在一个条件经常出现，或者该条件很少出现的时候，编译器可以根据这条指令对条件分支选择进行优化。内核把这条指令封装成了宏，比如likely()和unlikely()，这样使用起来比较方便。
例如，下面是一个条件选择语句：
if (foo) {
    /* .. */
}
如果想要把这个选择标记成绝少发生的分支：
/* 我们认为foo绝大多数时间都会为0.. */
if (unlikely(foo)) {
    /* .. */
}
相反，如果我们想把一个分支标记为通常为真的选择：
/* 我们认为foo通常都不会为0 */
if  (likely(foo)) {
      /* .. */
}

# gettimeofday()
gettimeofday是计算机函数，使用C语言编写程序需要获得当前精确时间（1970年1月1日到现在的时间），或者为执行计时，可以使用gettimeofday()函数。

# snprintf()
函数原型为int snprintf(char *str, size_t size, const char *format, ...)。
将可变个参数(...)按照format格式化成字符串，然后将其复制到str中。
(1) 如果格式化后的字符串长度 < size，则将此字符串全部复制到str中，并给其后添加一个字符串结束符('\0')；
(2) 如果格式化后的字符串长度 >= size，则只将其中的(size-1)个字符复制到str中，并给其后添加一个字符串结束符('\0')，返回值为欲写入的字符串长度。
#include <stdio.h>
int main () {
  char a[16];
  size_t i;
  i = snprintf(a, 13, "%012d", 12345);  // 第 1 种情况
  printf("i = %lu, a = %s\n", i, a);    // 输出：i = 12, a = 000000012345
  i = snprintf(a, 9, "%012d", 12345);   // 第 2 种情况
  printf("i = %lu, a = %s\n", i, a);    // 输出：i = 12, a = 00000001
  return 0;
}

# qsort
函数声明
     void qsort(void *base, size_t nitems, size_t size, int (*compar)(const void *, const void*))
参数
     base-- 指向要排序的数组的第一个元素的指针。
     nitems-- 由 base 指向的数组中元素的个数。
     size-- 数组中每个元素的大小，以字节为单位。
     compar-- 用来比较两个元素的函数，即函数指针（回调函数）
回调函数：
     回调函数就是一个通过函数指针调用的函数。如果把函数的指针（地址）作为参数传递给另一个函数，当这个指针被用来调用其所指向的函数时，就说这是回调函数。 [2] 
compar参数
     compar参数指向一个比较两个元素的函数。比较函数的原型应该像下面这样。注意两个形参必须是const void *型，同时在调用compar 函数（compar实质为函数指针，这里称它所指向的函数也为compar）时，传入的实参也必须转换成const void *型。在compar函数内部会将const void *型转换成实际类型。
     int compar(const void *p1, const void *p2);
     如果compar返回值小于0（< 0），那么p1所指向元素会被排在p2所指向元素的左面；
     如果compar返回值等于0（= 0），那么p1所指向元素与p2所指向元素的顺序不确定；
     如果compar返回值大于0（> 0），那么p1所指向元素会被排在p2所指向元素的右面。 [2] 
功 能
     使用排序例程进行排序。
说明
     该函数不返回任何值。

# strncmp
int strncmp(const char *str1, const char *str2, size_t n) 把 str1 和 str2 进行比较，最多比较前 n 个字符
该函数返回值如下：
如果返回值 < 0，则表示 str1 小于 str2。
如果返回值 > 0，则表示 str1 大于 str2。
如果返回值 = 0，则表示 str1 等于 str2。

# strcmp
int strcmp(const char *str1, const char *str2)
参数
str1 -- 要进行比较的第一个字符串。
str2 -- 要进行比较的第二个字符串。
返回值
如果返回值小于 0，则表示 str1 小于 str2。
如果返回值大于 0，则表示 str1 大于 str2。
如果返回值等于 0，则表示 str1 等于 str2。

# strcasecmp
定义函数：
int strcasecmp (const char *s1, const char *s2);
函数说明：
strcasecmp()用来比较参数s1 和s2 字符串，比较时会自动忽略大小写的差异。
返回值：
若参数s1 和s2 字符串相同则返回0。
s1 长度大于s2 长度则返回大于0 的值，
s1 长度若小于s2 长度则返回小于0 的值。

# fgets
char *fgets(char *str, int n, FILE *stream) 从指定的流 stream 读取一行，并把它存储在 str 所指向的字符串内。当读取 (n-1) 个字符时，或者读取到换行符时，或者到达文件末尾时，它会停止，具体视情况而定

# strtol
C 库函数 long int strtol(const char *str, char **endptr, int base) 把参数 str 所指向的字符串根据给定的 base 转换为一个长整数（类型为 long int 型），base 必须介于 2 和 36（包含）之间，或者是特殊值 0。

str -- 要转换为长整数的字符串。
endptr -- 对类型为 char* 的对象的引用，其值由函数设置为 str 中数值后的下一个字符。
base -- 基数，必须介于 2 和 36（包含）之间，或者是特殊值 0。如果 base 为 0，则会根据字符串的前缀来判断进制：如果字符串以 '0x' 或 '0X' 开头，则将其视为十六进制；如果字符串以 '0' 开头，则将其视为八进制；否则将其视为十进制。

# ftell/ftello(2G以上大文件)
C 库函数 long int ftell(FILE *stream) 返回给定流 stream 的当前文件位置。 好像只是用于获取文件大小，看代码用法感觉是获取文件已偏移的位置大小
参数
stream -- 这是指向 FILE 对象的指针，该 FILE 对象标识了流。
返回值
该函数返回位置标识符的当前值。如果发生错误，则返回 -1L，全局变量 errno 被设置为一个正值。

# fread
用完指针会偏移
从给定流 stream 读取size*nmemb字节的数据到 ptr 所指向的数组中
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
参数
ptr -- 这是指向带有最小尺寸 size*nmemb 字节的内存块的指针。
size -- 这是要读取的每个元素的大小，以字节为单位。
nmemb -- 这是元素的个数，每个元素的大小为 size 字节。
stream -- 这是指向 FILE 对象的指针，该 FILE 对象指定了一个输入流。
返回值
成功读取的元素总数会以 size_t 对象返回，size_t 对象是一个整型数据类型。如果总数与 nmemb 参数不同，则可能发生了一个错误或者到达了文件末尾。

# feof
声明
int feof(FILE *stream)
参数
stream -- 这是指向 FILE 对象的指针，该 FILE 对象标识了流。
返回值
当设置了与流关联的文件结束标识符时，该函数返回一个非零值，否则返回零。

# fileno
int fileno(FILE *stream);
将文件流指针转换为文件描述符

# rewind
设置文件位置为给定流 stream 的文件的开头。
声明
void rewind(FILE *stream)
参数
stream -- 这是指向 FILE 对象的指针，该 FILE 对象标识了流。
返回值
该函数不返回任何值。

# fturncate
定义函数 int ftruncate(int fd,off_t length);
ftruncate()会将参数fd指定的文件大小改为参数length指定的大小。
参数
fd为已打开的文件描述词，而且必须是以写入模式打开的文件。
如果原来的文件大小比参数length大，则超过的部分会被删去。
返回值
执行成功则返回0，失败返回-1，错误原因存于errno。
错误代码
EBADF 参数fd文件描述词为无效的或该文件已关闭。
EINVAL 参数fd 为一socket 并非文件，或是该文件并非以写入模式打开。