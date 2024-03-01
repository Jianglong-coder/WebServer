#include "LFUCache.h"
void KeyList::init(int freq)
{
    freq_ = freq;
    dummyHead_ = newElement<Node<Key>>();
    tail_ = dummyHead_;
    dummyHead_->setNext(nullptr);
}

void KeyList::destory()
{
    while (dummyHead_ != nullptr)
    {
        key_node pre = dummyHead_;
        munmap(pre->getValue().m_file_address, pre->getValue().m_file_stat.st_size);
        dummyHead_ = dummyHead_->getNext();
        deleteElement(pre);
    }
}

// 将节点添加到链表头部
void KeyList::add(key_node &node)
{
    if (dummyHead_->getNext() == nullptr)
    {
        tail_ = node;
    }
    else
    {
        dummyHead_->getNext()->setPre(node);
    }
    node->setNext(dummyHead_->getNext());
    dummyHead_->setNext(node);
    node->setPre(dummyHead_);
    assert(!isEmpty());
}

void KeyList::del(key_node &node)
{
    node->getPre()->setNext(node->getNext());

    if (node->getNext() == nullptr)
    {
        tail_ = node->getPre();
    }
    else
    {
        node->getNext()->setPre(node->getPre());
    }
}

LFUCache::LFUCache(int capacity = 10) : capacity_(capacity) { init(); };

void LFUCache::init()
{
    dummyHead_ = newElement<Node<KeyList>>(); // 给虚拟头节点在堆区申请空间
    dummyHead_->getValue().init(0);           // 给虚拟头节点的编号初始化为0
    dummyHead_->setNext(nullptr);
}
LFUCache::~LFUCache()
{
    while (dummyHead_)
    {
        freq_node pre = dummyHead_;
        dummyHead_ = dummyHead_->getNext();
        pre->getValue().destory();
        deleteElement(pre);
    }
}

void LFUCache::del(freq_node &node)
{
    node->getPre()->setNext(node->getNext());
    if (node->getNext() != nullptr)
    {
        node->getNext()->setPre(node->getPre());
    }

    node->getValue().destory();
    deleteElement(node);
}

/*
    更新节点频度：
    如果不存在下⼀个频度的链表，则增加⼀个
    然后将当前节点放到下⼀个频度的链表的头位置
*/
void LFUCache::addFreq(key_node &nowk, freq_node &nowf)
{
    freq_node next = nullptr;

    /*当前小链表为大链表的最后一个节点或者下一个链表的频度不等于当前链表的频度+1*/
    if (nowf->getNext() == nullptr || nowf->getNext()->getValue().getFreq() != nowf->getValue().getFreq() + 1)
    {
        next = newElement<Node<KeyList>>();                    // 新建一个小链表
        next->getValue().init(nowf->getValue().getFreq() + 1); // 新建小链表的频度为当前频度+1
        // 将新建的freq_node插入到当前freq_node的后面
        if (nowf->getNext() != nullptr) // 如果当前链表后面是有链表的
        {
            nowf->getNext()->setPre(next);
        }
        next->setNext(nowf->getNext());
        nowf->setNext(next);
        next->setPre(nowf);
    }
    else // 下一个链表频度 刚好等于当前链表的频度+1
    {
        next = nowf->getNext();
    }

    fmap_[nowk->getValue().key_] = next; // 添加映射  将当前value的key映射到大链表的map上

    /*
        将其从上⼀频度的⼩链表删除
        然后加到下⼀频度的⼩链表中
    */
    if (nowf != dummyHead_)
    {
        nowf->getValue().del(nowk);
    }
    next->getValue().add(nowk);

    assert(!next->getValue().isEmpty());

    if (nowf != dummyHead_ && nowf->getValue().isEmpty())
        del(nowf);
}

void LFUCache::set(char *key, char *m_file_address, struct stat m_file_stat)
{
    if (!capacity_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);

    // 缓存已满 删除频度最小链表里的最后一个节点
    if (kmap_.size() == capacity_)
    {
        freq_node head = dummyHead_->getNext();
        key_node last = head->getValue().getLast();
        head->getValue().del(last);
        kmap_.erase(last->getValue().key_);
        fmap_.erase(last->getValue().key_);
        munmap(last->getValue().m_file_address, last->getValue().m_file_stat.st_size);
        deleteElement(last);
        if (head->getValue().isEmpty())
        {
            del(head);
        }
    }
    key_node nowk = newElement<Node<Key>>(); // 新建节点

    nowk->getValue().key_ = key;                          // 设置节点key
    nowk->getValue().m_file_address = m_file_address;     // 设置节点value
    nowk->getValue().m_file_stat = m_file_stat;           // 文件状态
    addFreq(nowk, dummyHead_);                            // 将节点加入到链表中
    kmap_[nowk->getValue().key_] = nowk;                  // 将新建节点的key映射到链表的节点上
    fmap_[nowk->getValue().key_] = dummyHead_->getNext(); // 将新建节点的key映射到所属链表的节点上
}
bool LFUCache::get(char *key, char *&m_file_address, struct stat &m_file_stat)
{
    if (!capacity_)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);

    if (fmap_.find(key) != fmap_.end())
    {
        // 缓存命中
        key_node nowk = kmap_[key];
        freq_node nowf = fmap_[key];
        // val += nowk->getValue().value_;
        m_file_address = nowk->getValue().m_file_address;
        m_file_stat = nowk->getValue().m_file_stat;
        addFreq(nowk, nowf);
        return true;
    }
    return false;
}