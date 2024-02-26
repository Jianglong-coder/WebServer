​	线程安全的查找结构，实现一个类似线程安全的map结构，但是map基于红黑树实现，假设我们要增加或者删除节点，设计思路是依次要删除或增加节点的父节点，然后修改子节点数据 。尽管这种思路可行，但是难度较大，红黑树节点的插入要修改多个节点的关系。另外加锁的流程也是锁父节点，再锁子节点，尽管在处理子节点时我们已经处理完父节点，可以对父节点解锁，继续对子节点加锁，这种情况锁的粒度也不是很精细，考虑用散列表实现。对于链表的增删改查需要加锁,所以考虑将链表封装为一个类`bucket_type`,支持数据的增删改查。将整体的查找表封装为`threadsafe_lookup_table`类，实现散列规则和调度`bucket_type`类。

​	在`threadsafe_lookup_table`类中定义`bucket_type`类，并在`bucket_type`中将`threadsafe_lookup_table`设置为友元类。在`bucket_type`类中定义一个链表，链表中存放的是pair的键值对。为保证读并发定义了一个读写锁`shared_mutex`。

```c++
using bucket_value = std::pair<Key, Value>;             // 存储元素的类型为pair 由key和value构成
using bucket_data = std::list<bucket_value>;            // 用链表存储元素
using bucket_iterator = typename bucket_data::iterator; // 链表的迭代器
bucket_data data;                                       // 链表数据
mutable std::shared_mutex mutex;                        // 共享锁 单个桶内用共享锁 可以并发读 互斥写
//在bucket_type内部定义以下函数
bucket_iterator find_entry_for(const Key & key)//内部私有查找函数  返回迭代器
Value value_for(Key const& key, Value const& default_value)//查找函数
void add_or_update_mapping(Key const& key, Value const& value)//添加key和value，找到则更新，没找到则添加
void remove_mapping(Key const& key)//删除对应的key
```

`threadsafe_lookup_table`类里用`vector`存储`bucket_type`的指针 然后利用`std::hash`作为默认hash算法

```c++
std::vector<std::unique_ptr<bucket_type>> buckets; //用vector存储桶类型
Hash hasher;    //hash<Key> 哈希表 用来根据key生成哈希值
bucket_type& get_bucket(Key const& key) const////根据key生成数字，并对桶的大小取余得到下标，根据下标返回对应的桶
Value value_for(Key const& key, Value const& default_value = Value()) //先根据key找到对应桶 然后调用底层桶的查找
void add_or_update_mapping(Key const& key, Value const& value)//先根据key找到对应桶 然后调用底层桶的加入函数
void remove_mapping(Key const& key)//先根据key找到对应桶 然后调用底层桶的删除函数
std::map<Key, Value> get_map()//把所有的pair放入map中返回
```

