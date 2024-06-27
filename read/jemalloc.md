启动流程
Jemalloc作为一个高效的内存分配库，其启动流程涉及到初始化、配置加载、线程安全初始化、内存区域（arenas）的创建等多个步骤。由于Jemalloc的源代码较为复杂，下面提供一个简化的概念性流程，并结合少量示例代码来说明关键步骤。请注意，实际源代码会更加详细且包含很多内部逻辑和平台相关的实现。1. 初始化入口Jemalloc的启动通常从库被链接的应用程序启动时开始。对于直接使用jemalloc的程序，启动流程可能从malloc_init()这样的函数开始，尽管实际入口点可能因编译选项和平台而异。2. 配置加载Jemalloc支持通过环境变量或配置文件来调整内存管理策略。在启动初期，它会读取这些配置，比如MALLOC_CONF环境变量，来决定是否开启某些特性或调整内存分配参数。3. 线程安全初始化为了确保在多线程环境中的正确运行，Jemalloc需要初始化线程相关的数据结构和锁。这通常包括初始化全局锁（如示例代码中的init_lock），以及为每个线程准备线程局部存储（Thread-Local Storage, TLS）。// 假设的初始化锁函数
static void
_init_init_lock() {
    malloc_mutex_init(&init_lock, "jemalloc init lock");
}

// 加锁操作
malloc_mutex_lock(TSDN_NULL, &init_lock);
c4. 内存区域（Arenas）的初始化与分配Jemalloc使用内存区域（arena）来管理内存。在启动时，它会根据配置和系统环境创建一定数量的arenas。每个arena负责管理一部分内存，包括小块内存的分配、大块内存的分配等。5. 分配器初始化接下来，会初始化各个内存分配器，包括小对象分配器、大对象分配器等。这些分配器负责实际的内存分配和释放操作。6. 启动后回调（可选）Jemalloc提供了一些机制，允许用户在初始化完成后执行自定义的回调函数，用于进一步的定制化设置。示例代码概念性解释以下代码是概念性的，展示了启动时初始化锁和进行一些基础配置的伪代码示例：#include "jemalloc_internal.h"

// 假设的全局锁
static malloc_mutex_t init_lock;

// 应用程序启动时调用的初始化函数
void jemalloc_startup() {
    // 初始化全局锁，确保线程安全
    _init_init_lock();

    // 加锁，防止多线程初始化冲突
    malloc_mutex_lock(TSDN_NULL, &init_lock);

    // 这里会调用内部函数进行更详细的初始化
    // 包括arenas的初始化、配置加载等
    internal_init();

    // 初始化完毕后解锁
    malloc_mutex_unlock(TSDN_NULL, &init_lock);
}

int main() {
    jemalloc_startup();
    // 应用程序主体逻辑...
    return 0;
}


申请内存流程
Jemalloc内存分配库在申请内存时，会经过一系列复杂的内部处理流程，旨在高效地管理内存并减少碎片。以下是一个简化的流程说明，结合伪代码来阐述jemalloc如何处理一个典型的内存分配请求，比如申请长度为len的内存。1. 分配策略选择首先，jemalloc会根据len的大小决定使用哪种分配策略。Jemalloc将内存分配划分为Small、Large和Huge三个不同的区间，每个区间有专门的分配器处理。2. 选择ArenaJemalloc使用arena模型来管理内存。每个arena是一个大的内存区域，可以独立地进行内存分配。jemalloc会根据某种策略（如arena的负载均衡）选择一个arena来执行分配。3. Chunk分配每个arena由多个chunk组成，chunk是jemalloc从系统获得的大块内存。如果arena没有足够的空间来满足请求，jemalloc可能需要从系统调用如mmap或brk来获取新的chunk。4. Bin分配对于Small对象，jemalloc使用bin结构来管理。每个bin对应一个特定大小范围的对象。当请求分配内存时，jemalloc会找到适合该大小的bin，并尝试从中分配空间。如果bin为空，则可能需要从所在chunk中分割出新的内存块，或者从其他bin中迁移内存块。5. Large对象直接分配对于大于Small对象阈值的请求，jemalloc会直接在arena的large或huge区域中分配，可能通过维护的空闲列表或直接分配新的chunk。6. 返回分配的内存一旦找到合适的空间，jemalloc会返回指向这块内存的指针给调用者。简化的代码示例（非真实代码，仅示意流程）：void* je_malloc(size_t len) {
    // 1. 根据len选择对应的分配策略
    allocation_strategy strategy = choose_allocation_strategy(len);

    // 2. 选择或创建arena
    arena_t *arena = select_arena(strategy);

    // 3. 尝试在选定的arena中分配
    void *ptr = NULL;
    switch (strategy) {
        case SMALL:
            ptr = small_bin_allocate(arena, len);
            break;
        case LARGE:
            ptr = large_object_allocate(arena, len);
            break;
        case HUGE:
            ptr = huge_object_allocate(arena, len);
            break;
    }

    // 4. 如果分配失败，尝试从系统获取更多内存
    if (ptr == NULL) {
        if (!acquire_more_memory_from_system(arena)) {
            return NULL; // 分配失败
        }
        // 重新尝试分配
        ptr = retry_allocation_after_acquire_memory(arena, len, strategy);
    }

    return ptr; // 成功分配，返回内存指针
}