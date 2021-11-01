realloc 
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

memcmp函数原型
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

memcpy函数原型
void *memcpy(void *str1, const void *str2, size_t n)从存储区str2复制n个字节到存储区str1。
返回值
该函数返回一个指向目标存储区 str1 的指针。

memmove函数原型
void *memmove(void *str1, const void *str2, size_t n) 
从str2复制n个字符到str1，但是在重叠内存块这方面，memmove()是比memcpy()更安全的方法。如果目标区域和源区域有重叠的话，memmove()能够保证源串在被覆盖之前将重叠区域的字节拷贝到目标区域中，复制后源区域的内容会被更改。如果目标区域与源区域没有重叠，则和memcpy()函数功能相同。