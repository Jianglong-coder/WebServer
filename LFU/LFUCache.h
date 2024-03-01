#ifndef __LFUCACHE_H__
#define __LFUCACHE_H__

// #include <string>
#include <mutex>
#include <unordered_map>
#include <sys/stat.h>
#include <cassert>
#include <sys/mman.h>
#include "../memorypool/memorypool.h"
#define LFU_CAPACITY 10
// #define FILENAME_LEN 200
// 链表的节点
template <typename T>
class Node
{
public:
    void setPre(Node *p) { pre_ = p; }
    void setNext(Node *p) { next_ = p; };
    Node *getPre() { return pre_; }
    Node *getNext() { return next_; }
    T &getValue() { return value_; }

private:
    T value_;
    Node *pre_;
    Node *next_;
};

// 文件名->文件内容的映射
struct Key
{
    char *key_;              // 文件名
    char *m_file_address;    // 文件地址
    struct stat m_file_stat; // 文件状态
};
using key_node = Node<Key> *;
// 小链表 有多个Node<Key>*组成
class KeyList
{
public:
    void init(int freq);
    void destory();
    int getFreq() { return freq_; } // 返回当前链表的频度
    void add(key_node &node);       // 项链表中添加节点
    void del(key_node &node);       // 项链表中删除节点
    bool isEmpty() { return dummyHead_ == tail_; };
    key_node getLast() { return tail_; };

private:
    int freq_;           // 当前链表频度
    key_node dummyHead_; // 链表虚拟头节点的头指针
    key_node tail_;      // 链表尾指针
};

using freq_node = Node<KeyList> *;

class LFUCache
{
public:
    LFUCache(int capcity);
    ~LFUCache();
    bool get(char *key, char *&m_file_address, struct stat &m_file_stat); // 通过key返回value并进行LFU操作
    void set(char *key, char *m_file_address, struct stat m_file_stat);   // 更新LFU缓存
    static LFUCache &getCache()
    {
        static LFUCache cache(LFU_CAPACITY);
        return cache;
    }

private:
    freq_node dummyHead_; // 大链表的虚拟头节点 大链表里面的每个节点都是小链表的头节点
    size_t capacity_;     // 小链表中的所有节点总个数
    std::mutex mutex_;

    std::unordered_map<std::string, key_node> kmap_;  // key到keynode的映射
    std::unordered_map<std::string, freq_node> fmap_; // key到freqnode的映射

    void addFreq(key_node &nowk, freq_node &nowf); // 把节点升一个频次 如果没有对应频次链表 就添加一个新链表
    void del(freq_node &node);                     // 删除小链表
    void init();                                   // 初始化
};
#endif