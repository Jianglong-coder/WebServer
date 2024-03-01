#ifndef __MEMORYPOOL9_H__
#define __MEMORYPOOL9_H__

#include <mutex>
#include <cassert>
#define BLOCKSIZE 4096

struct Slot
{
    Slot *next;
};

class MemoryPool
{
public:
    MemoryPool();
    ~MemoryPool();

    void init(int size);

    Slot *allocate();         // 分配一个元素空间
    void deAllocate(Slot *p); // 收回一个元素空间
private:
    int slotSize_; // 每个槽所占的字节数

    Slot *currentBlock_;        // 内存块链表的头指针
    Slot *currentSlot_;         // 元素链表的头指针
    Slot *lastSlot_;            // 可存放元素的最后指针
    Slot *freeSlot_;            // 元素构造后释放掉的内存链表头指针
    std::mutex mutex_freeSlot_; // free后的内存的链表的锁
    std::mutex mutex_other_;    // 其他锁

    inline size_t padPointer(char *p, size_t align); // 计算内存对齐所需空间
    Slot *allocateBlock();                           // 申请内存块放进内存池
    Slot *nofree_solve();                            // 在从free分配时 如果空间不够就调用这个函数 函数里会分配未使用的空间 如果也不够就申请新的block
};

void init_MemoryPool();
void *use_Memory(size_t size);
void free_Memory(size_t size, void *p);
MemoryPool &getMemoryPool(int id);
template <typename T, typename... Args>
T *newElement(Args &&...args);
template <typename T>
void deleteElement(T *p);


template <typename T, typename... Args>
T *newElement(Args &&...args)
{
    T *p = nullptr;
    if ((p = reinterpret_cast<T *>(use_Memory(sizeof(T)))) != nullptr)
    {
        new (p) T(std::forward<Args>(args)...); // 完美转发
    }
    return p;
}

template <typename T>
void deleteElement(T *p)
{
    if (p)
        p->~T();
    free_Memory(sizeof(T), reinterpret_cast<void *>(p));
}
#endif