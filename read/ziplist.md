ziplist的插入流程
    请结合ziplistPush的函数源码理解
插入的位置分为entry的头位置和尾位置
插入的是sds形式的数据
获取到原先这个ziplist的总长度curlen
获取插入位置之前的元素存放的上一个entry的长度prevlen
判断这个要插入的entry可不可以被encode，获取存放这个entry所需的reqlen
    encode标准：
        如果长度超过32位或者长度为0则无法压缩
        如果这个字符串无法转换为longlong类型的整型也是无法被encode的
    如果无法压缩则直接reqlen+slen
    如果可以压缩，根据转化成的数字判断需要多少位来存储这个数字，并返回对应的encoding
reqlen需要加上存放prevlen数据的内存
reqlen在加上存储entry的encode方式的encoding数据的内存
最终获得reqlen总需求量，也得到了entry存储数据的结构
    <prelen>------以255为边界，分为1字节和5字节
                  tips:如果为1；其中存的值就是上一个数据的长度
                       如果为5；第一个字节存放的是固定值254，则后四个字节存放的是前一个数据的长度（一个int可以表示的值范围）
    <prevlen>|<encoding>|<data>
插入准备：
    重新申请新的ziplist的所需的空间，并对新申请的数据的头部节点的总长度数据和尾节点的固定数据做修改
如果不是在尾部插入就将数据偏移到应该到的地方
最后就是依次对entry的部分按照上面的结构依次进行赋值
最后对存储entry数量的数据+1

ZIPLIST_TAIL_OFFSET(zl)//重点在ziplistpush的时候
如果插入的位置为尾节点则会将尾节点到头结点的距离赋值给zltail
如果头节点位置开始插入会将新申请的reqlen+原来的zltail赋值给最新的zltail
所以这个函数获取到的就是最后一个entry所在的头部数据指针


redis.conf配置
如果保持ziplist的合理长度，取决于具体的应用场景。redis提供了默认配置
list-max-ziplist-size -2
参数的含义解释，取正值时表示quicklist节点ziplist包含的数据项。取负值表示按照占用字节来限定quicklist节点ziplist的长度。
-5: 每个quicklist节点上的ziplist大小不能超过64 Kb。
-4: 每个quicklist节点上的ziplist大小不能超过32 Kb。
-3: 每个quicklist节点上的ziplist大小不能超过16 Kb。
-2: 每个quicklist节点上的ziplist大小不能超过8 Kb。（默认值）
-1: 每个quicklist节点上的ziplist大小不能超过4 Kb。

list设计最容易被访问的是列表两端的数据，中间的访问频率很低，如果符合这个场景，list还有一个配置，可以对中间节点进行压缩（采用的LZF——一种无损压缩算法），进一步节省内存。配置如下
list-compress-depth 0 
含义：
0: 是个特殊值，表示都不压缩。这是Redis的默认值。
1: 表示quicklist两端各有1个节点不压缩，中间的节点压缩。
2: 表示quicklist两端各有2个节点不压缩，中间的节点压缩。