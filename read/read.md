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
