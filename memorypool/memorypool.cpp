#include "memorypool.h"

MemoryPool::MemoryPool() {}
MemoryPool::~MemoryPool() {}

void MemoryPool::init(int size)
{
    assert(size > 0);
    slotSize_ = size;
    currentBlock_ = nullptr;
    currentSlot_ = nullptr;
    lastSlot_ = nullptr;
    freeSlot_ = nullptr;
}
// 计算对齐所需补的空间
inline size_t MemoryPool::padPointer(char *p, size_t align)
{
    size_t result = reinterpret_cast<size_t>(p);
    return ((align - result) % align);
}
// 分配一个新的block
Slot *MemoryPool::allocateBlock()
{
    /*
        operator new: 是一个函数，用于分配内存  返回一个指向分配内存块的指针 (void*)
        由 operator new 分配的内存，需要使用 operator delete 来释放
    */
    char *newBlock = reinterpret_cast<char *>(operator new(BLOCKSIZE));
    char *body = newBlock + sizeof(Slot *);                                // 留出一个指针位置
    size_t bodyPadding = padPointer(body, static_cast<size_t>(slotSize_)); // 计算内存对齐需要空出多少位置

    Slot *useSlot;
    {
        std::lock_guard<std::mutex> lock(mutex_other_);
        reinterpret_cast<Slot *>(newBlock)->next = currentBlock_; // newBlock接到Block链表的头部
        currentBlock_ = reinterpret_cast<Slot *>(newBlock);

        /*为该Block开始的地方加上bodyPadding个char*空间*/
        currentSlot_ = reinterpret_cast<Slot *>(body + bodyPadding);
        lastSlot_ = reinterpret_cast<Slot *>(newBlock + BLOCKSIZE - slotSize_ + 1);
        useSlot = currentSlot_;
        currentSlot_ += (slotSize_ >> 3); // slot指针一次移动8个字节
    }
    return useSlot;
}
// 如果free链表上空间不够了 从currentSlot_分配一个slot 如果也不够了 申请一个新的block
Slot *MemoryPool::nofree_solve()
{
    if (currentSlot_ >= lastSlot_)
        return allocateBlock();
    Slot *useSlot;
    {
        std::lock_guard<std::mutex> lock(mutex_other_);
        useSlot = currentSlot_;
        currentSlot_ += (slotSize_ >> 3);
    }
    return useSlot;
}

Slot *MemoryPool::allocate()
{
    if (freeSlot_) // free链表内存在元素
    {
        std::lock_guard<std::mutex> lock(mutex_freeSlot_);
        if (freeSlot_)
        {
            Slot *result = freeSlot_;
            freeSlot_ = freeSlot_->next;
            return result;
        }
    }
    return nofree_solve();
}

inline void MemoryPool::deAllocate(Slot *p)
{
    if (p)
    {
        std::lock_guard<std::mutex> lock(mutex_freeSlot_);
        p->next = freeSlot_;
        freeSlot_ = p;
    }
}



MemoryPool &getMemoryPool(int id)
{
    static MemoryPool memorypool_[64];
    return memorypool_[id];
}

// 给内存池数组初始化 slotsize大小分别为8 16 ...512
void init_MemoryPool()
{
    for (int i = 0; i < 64; ++i)
    {
        getMemoryPool(i).init((i + 1) << 3);
    }
}

// 超过512字节就直接new
void *use_Memory(size_t size)
{
    if (!size)
        return nullptr;
    if (size > 512)
    {
        return operator new(size);
    }
    return reinterpret_cast<void *>(getMemoryPool(((size + 7) >> 3) - 1).allocate());
}

void free_Memory(size_t size, void *p)
{
    if (!p)
        return;
    if (size > 512)
    {
        operator delete(p);
        return;
    }
    getMemoryPool(((size + 7) >> 3) - 1).deAllocate(reinterpret_cast<Slot *>(p));
}


