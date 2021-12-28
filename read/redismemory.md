 作为内存数据库，为了防止redis占用过多的内存对其他的应用程序造成影响，redis的使用者可以在redis.conf文件中通过设置maxmemory选项对redis所能够使用的最大内存做限制，并通过maxmemory_policy对redis占用内存超过maxmemory之后的行为做定制。这篇文章，我们从redis源码的角度剖析一下redis的最大内存管理策略。
        redis源码中的内存管理策略都存在于evict.c文件中，其中最重要的一个函数就是freeMemoryIfNeeded。该函数用于在redis占用的内存超过maxmemory之后真正释放掉redis中的某些键值对，将redis占用的内存控制在合理的范围之内。
        redis在占用的内存超过指定的maxmemory之后，通过maxmemory_policy确定redis是否释放内存以及如何释放内存。redis提供了8种内存超过限制之后的响应措施，分别如下：
        1.volatile-lru(least recently used):最近最少使用算法，从设置了过期时间的键中选择空转时间最长的键值对清除掉；
        2.volatile-lfu(least frequently used):最近最不经常使用算法，从设置了过期时间的键中选择某段时间之内使用频次最小的键值对清除掉；
        3.volatile-ttl:从设置了过期时间的键中选择过期时间最早的键值对清除；
        4.volatile-random:从设置了过期时间的键中，随机选择键进行清除；
        5.allkeys-lru:最近最少使用算法，从所有的键中选择空转时间最长的键值对清除；
        6.allkeys-lfu:最近最不经常使用算法，从所有的键中选择某段时间之内使用频次最少的键值对清除；
        7.allkeys-random:所有的键中，随机选择键进行删除；
        8.noeviction:不做任何的清理工作，在redis的内存超过限制之后，所有的写入操作都会返回错误；但是读操作都能正常的进行;
        前缀为volatile-和allkeys-的区别在于二者选择要清除的键时的字典不同，volatile-前缀的策略代表从redisDb中的expire字典中选择键进行清除；allkeys-开头的策略代表从dict字典中选择键进行清除。这里简单介绍一下redis中struct redisDb的定义(更加详尽的解释会专门写一篇关于redisDb的博客）。在redis中每个struct redisDb包含5个指向dict的指针(dict*)，这里我们重点关注两个dict*成员，分别是dict* dict和dict* expire。dict用于存放所有的键值对，无论是否设置了过期时间；expire只用于存放设置了过期时间的键值对的值对象。如果我们使用expire、expireat、pexpire、pexpireat、setex、psetex命令设置一个键的过期时间，那么将在dict *dict中创建一个sds字符串用于存放该键，dict和expire共享该键，以减少占用的内存；但是需要创建两个字符串对象分别存放该键关联的值(存放在dict中）和该键的过期时间（存放在expire中)。
————————————————
版权声明：本文为CSDN博主「孤独剑0001」的原创文章，遵循CC 4.0 BY-SA版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/GDJ0001/article/details/80117797/