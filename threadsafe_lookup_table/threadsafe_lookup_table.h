#ifndef __THREADSAFE_LOOKUP_TABLE_H__
#define __THREADSAFE_LOOKUP_TABLE_H__
#include <vector>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <map>
#include <algorithm>
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class threadsafe_lookup_table
{
private:
    class bucket_type // 桶类型
    {
    public:
        friend class threadsafe_lookup_table; // 为了查找表可以访问链表 将查找表设置为友元类
    private:
        using bucket_value = std::pair<Key, Value>;             // 存储元素的类型为pair 由key和value构成
        using bucket_data = std::list<bucket_value>;            // 用链表存储元素
        using bucket_iterator = typename bucket_data::iterator; // 链表的迭代器
        bucket_data data;                                       // 链表数据
        mutable std::shared_mutex mutex;                        // 共享锁 单个桶内用共享锁 可以并发读 互斥写
        bucket_iterator find_entry_for(const Key &key)          // 查找操作 再list中找到匹配的Key值 返回其迭代器 私有的查找接口 内部使用
        {
            return std::find_if(data.begin(), data.end(), [&](bucket_value const &item)
                                { return item.first == key; });
        }

    public:
        // 查找key 找到返回其value 未找到返回默认值
        Value value_for(const Key &key, Value const &default_value)
        {
            std::shared_lock<std::shared_mutex> lock(mutex); // shared_lock?
            bucket_iterator const found_entry = find_entry_for(key);
            return found_entry == data.end() ? default_value : found_entry->second;
        }
        // 添加key和value 找到则更新 没找到则添加
        void add_or_update_mapping(Key const &key, Value const &value)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator found_entry = find_entry_for(key);
            if (found_entry != data.end())
            {
                found_entry->second = value;
            }
            else
            {
                data.push_back(bucket_value(key, value));
            }
        }
        // 删除对应key的pair
        void remove_mapping(Key const &key)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator found_entry = find_entry_for(key);
            if (found_entry != data.end())
            {
                data.erase(found_entry);
            }
        }
    };

    std::vector<std::unique_ptr<bucket_type>> buckets; // 用vector存储桶类型
    Hash hasher;                                       // 因为我们要计算hash值，key可能是多种类型string, int等，所以我们采用std的hash算法作为散列函数即可

    // 根据key生成数字 并对桶的大小取余得到下标 根据下标返回对应的桶
    bucket_type &get_bucket(Key const &key) const
    {
        std::size_t const bucket_index = hasher(key) % buckets.size();
        return *buckets[bucket_index];
    }

public:
    threadsafe_lookup_table(unsigned num_buckets = 19, Hash const &hasher_ = Hash()) : buckets(num_buckets), hasher(hasher_)
    {
        // 在堆区 生成每个对应的桶
        for (unsigned i = 0; i < num_buckets; ++i)
        {
            buckets[i].reset(new bucket_type);
        }
    }
    threadsafe_lookup_table(threadsafe_lookup_table const &) = delete;
    threadsafe_lookup_table &operator=(threadsafe_lookup_table const &) = delete;
    // 查询函数  先得到key所在的桶 然后调用桶的内部查询
    Value value_for(Key const &key, Value const &default_value = Value())
    {
        return get_bucket(key).value_for(key, default_value);
    }
    // 添加或更新函数 先得到key所在的桶 然后调用桶的内部添加或更新函数
    void add_or_update_mapping(Key const &key, Value const &value)
    {
        return get_bucket(key).add_or_update_mapping(key, value);
    }
    // 删除函数 先得到key所在的桶 然后调用桶的内部删除函数
    void remove_mapping(Key const &key)
    {
        return get_bucket(key).remove_mapping(key);
    }
    // 返回当前查找表中所有的键值对
    std::map<Key, Value> get_map()
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (unsigned i = 0; i < buckets.size(); ++i)
        {
            locks.push_back(std::unique_lock<std::shared_mutex>(buckets[i]->mutex));
        }
        std::map<Key, Value> res;
        for (unsigned i = 0; i < buckets.size(); ++i)
        {
            for (auto it = buckets[i]->data.begin(); it != buckets[i]->data.end(); ++it)
            {
                res.insert(*it);
            }
            // for (auto const &item : buckets[i]->data)
            // {
            //     res.insert(item);
            // }
        }
        return res;
    }
};
#endif