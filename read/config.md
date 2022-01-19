# cronloops 属性
cronloops属性是一个计数器，用于记录服务器的 serverCron 函数被执行的次数，是一个 int 类型的整数。

# rdb_child_pid 与 aof_child_pid 属性
rdb_child_pid 和 aof_child_pid 属性用于检查 Redis 服务器持久化操作的运行状态，它们记录执行 BGSAVE 和 BGREWRITEAOF 命令的子进程的 ID。也常常使用这两个属性来判断 BGSAVE 和 BGREWRITEAOF 命令是否正在被执行。
当执行 serverCron 函数时，会检查 rdb_child_pid 和 aof_child_pid 属性的值，只要其中一个属性的值不等于-1，程序就会调用一次 wait3 函数来判断子进程是否发送信号到服务器中。
如果没有信号到达，则表示服务器持久化操作没有完成，程序不做任何处理。而如果有信号到达，那么，针对 BGSAVE 命令，表示新的 RDB 文件已经成功生成；针对 BGREWRITEAOF 命令，表示新的 AOF 文件生成完毕，然后服务器继续执行相应的后续操作。比如，将旧的 RDB 文件或 AOF 文件替换为新的 RDB 文件或 AOF 文件。
另外，当 rdb_child_pid 和 aof_child_pid 属性的值都为-1 时，表示此时的服务器没有执行持久化操作，这时程序会做出如下判断。
（1）判断 BGREWRITEAOF 命令的执行是否被延迟了(通过下文所讲aof_rewrite_scheduled 属性)。如果被延迟了，则重新执行一次 BGREWRITEAOF 命令。
（2）判断是否满足服务器的自动保存条件。如果满足服务器的自动保存条件，并且服务器没有执行其他持久化操作，那么服务器将开始执行 BGSAVE 命令。
（3）判断是否满足服务器设置的 AOF 重写条件。如果条件满足，同时服务器没有执行其他持久化操作，那么服务器将重新执行 BGREWRITEAOF 命令。

# stat_peak_memory 属性
stat_peak_memory 属性用于记录 Redis 服务器的内存峰值大小。在每次执行 serverCron 函数时，程序都会检查服务器当前内存的使用情况，并与 stat_peak_memory 属性保存的上一次内存峰值大小进行比较。如果当前的内存峰值大小大于 stat_peak_memory 属性保存的值，就将当前最新的内存峰值大小赋给 stat_peak_memory 属性。
在执行 INFO memory 命令后，返回的 used_memory_peak 和 used_memory_peak_human 属性分别以两种格式记录了服务器的内存峰值大小。

# lruclock 属性
lruclock 属性是一种服务器时间缓存，它记录了服务器的 LRU 时钟。在默认情况下，serverCron 函数会以每 10 秒一次的频率更新 lruclock 属性的值。LRU 时钟不是实时的，它只是一个模糊的估计值。
Redis 的每个对象都有一个 lru 属性，该属性记录了这个对象最后一次被命令访问的时间。使用 lruclock 属性的值减去 lru 属性的值，就能计算出这个对象的空转时间。
可以使用 INFO server 命令的 lru_clock 属性来查看当前 LRU 时钟的时间

# aof_rewrite_scheduled 属性
aof_rewrite_scheduled 属性用于记录服务器中 BGREWRITEAOF 命令执行是否被延迟。当 aof_rewrite_scheduled 属性的值为 1 时，表示执行 BGREWRITEAOF 命令超时了。在服务器执行 BGSAVE 命令时，如果客户端发送了 BGREWRITEAOF 命令请求，那么服务器在接收到命令请求之后，会将 BGREWRITEAOF 命令延迟到 BGSAVE 命令执行成功后再执行。

# 懒惰删除
一直以来我们认为 Redis是单线程的，单线程为Redis带来了代码的简洁性和丰富多样的数据结构。不过 Redis内部实际上并不是只有一个主线程，它还有几个异步线程专门用来处理一些耗时的操作。
1 unlink指令
删除指令 del 会直接释放对象的内存，大部分情况下，这个指令非常快，没有明显延迟。不过如果删除的 key 是一个非常大的对象，比如一个包含了千万元素的 hash，那么删除操作就会导致单线程卡顿。
Redis 为了解决这个卡顿问题，在4.0版本引入了unlink指令，它能对删除操作进行懒处理，丢给后台线程来异步回收内存。
2 flush async指令
Redis 提供了 flushdb和flushall指令，用来清空数据库，这也是极其缓慢的操作。
Redis 4.0 同样给这两个指令也带来了异步化，在指令后面增加async参数就可以将整棵大树连根拔起，扔给后台线程慢慢焚烧

# master_replid
是master启动时生成的40位16进制的随机字符串，用来标识master节点

# master_repl_offset
是复制流中的一个偏移量，master处理完写入命令后，会把命令的字节长度做累加记录，统计在该字段。该字段也是实现部分复制的关键字段。

# slave_repl_offset
同样也是一个偏移量，从节点收到主节点发送的命令后，累加自身的偏移量，通过比较主从节点的复制偏移量可以判断主从节点数据是否一致。
当从实例连接到主实例时，从实例会发送master_replid和master_repl_offset（标识与主实例同步的最后一个快照）请求部分复制。如果主实例接收部分复制的话则从最后一个偏移量开始增量进行部分复制，否则将进行全量复制

# lazyfree-lazy-eviction
针对redis内存使用达到maxmeory，并设置有淘汰策略时；在被动淘汰键时，是否采用lazy free机制；
因为此场景开启lazy free, 可能使用淘汰键的内存释放不及时，导致redis内存超用，超过maxmemory的限制。此场景使用时，请结合业务测试。

# lazyfree-lazy-expire
针对设置有TTL的键，达到过期后，被redis清理删除时是否采用lazy free机制；
此场景建议开启，因TTL本身是自适应调整的速度。

# lazyfree-lazy-server-del
针对有些指令在处理已存在的键时，会带有一个隐式的DEL键的操作。如rename命令，当目标键已存在,redis会先删除目标键，如果这些目标键是一个big key,那就会引入阻塞删除的性能问题。 此参数设置就是解决这类问题，建议可开启。

# slave-lazy-flush
针对slave进行全量数据同步，slave在加载master的RDB文件前，会运行flushall来清理自己的数据场景，
参数设置决定是否采用异常flush机制。如果内存变动不大，建议可开启。可减少全量同步耗时，从而减少主库因输出缓冲区爆涨引起的内存使用增长。