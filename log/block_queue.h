/*************************************************************
 *循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
 *线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 **************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000) // 队列长度默认值为1000
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;     // 队列最大长度
        m_array = new T[max_size]; // 创建数组
        m_size = 0;                // 队列当前元素个数
        m_front = -1;              // 队首索引初始化
        m_back = -1;               // 队尾索引初始化
    }

    void clear() // 将队列清零
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue() // 析构
    {
        m_mutex.lock();
        if (m_array != NULL)
            delete[] m_array;

        m_mutex.unlock();
    }
    // 判断队列是否满了
    bool full()
    {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 判断队列是否为空
    bool empty()
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 返回队首元素
    bool front(T &value)
    {
        m_mutex.lock();
        if (0 == m_size) // 队列为空
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front]; // 取队首元素
        m_mutex.unlock();
        return true;
    }
    // 返回队尾元素
    bool back(T &value)
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    // 返回队列当前元素个数
    int size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }
    // 返回队列最大元素个数
    int max_size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }
    // 往队列添加元素，需要将所有使用队列的线程先唤醒
    // 当有元素push进队列,相当于生产者生产了一个元素
    // 若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {

        m_mutex.lock();
        if (m_size >= m_max_size) // 队列满 push失败
        {

            m_cond.broadcast(); // 唤醒所有等待条件变量的线程
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size; // 移动索引
        m_array[m_back] = item;             // 将元素放入队列中

        m_size++; // 元素个数+1

        m_cond.broadcast(); // 唤醒所有等待条件变量的线程
        m_mutex.unlock();
        return true;
    }
    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {

        m_mutex.lock();
        while (m_size <= 0) // 队列为空
        {
            // 使用条件变量m_cond等待队列中有元素可供弹出 队列为空 线程将被阻塞 直到有元素可弹出
            // m_cond.wait(m_mutex.get())会等待条件变量m_cond，
            // 并且会在等待期间释放互斥锁m_mutex  这样主线程写日志时 是可以获得m_mutex然后push到队列里的
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        // 添加元素时 是先移动索引 后添加元素 所以m_front位置是没有元素的  所以这里先移动所以 后取元素
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--; // 元素个数-1
        m_mutex.unlock();
        return true;
    }

    // 增加了超时处理
    bool pop(T &item, int ms_timeout)
    {
        /*
            timespec和timeval是两种不同的时间结构体，用于表示时间的不同精度和格式。

            timespec结构体用于表示时间的高精度，包含以下成员：
            - tv_sec：表示秒数，以自UTC时间1970年1月1日以来的秒数为单位。
            - tv_nsec：表示纳秒数，以纳秒为单位，取值范围为0到999999999。

            timeval结构体用于表示时间的低精度，包含以下成员：
            - tv_sec：表示秒数，以自UTC时间1970年1月1日以来的秒数为单位。
            - tv_usec：表示微秒数，以微秒为单位，取值范围为0到999999。
        */
        struct timespec t = {0, 0};//timewait 传入timespec格式
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL); // 取当前系统时间的秒和微秒 存在now中
        m_mutex.lock();
        if (m_size <= 0) // 队列空
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))//timewait阻塞加了时间限制
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }
        /*  
            第一次判空队列是在设置超时时间之前。如果队列为空，那么线程将等待一段时间，直到有元素可供弹出或超时发生。
            这是通过调用m_cond.timewait(m_mutex.get(), t)实现的，其中t是一个timespec结构，表示等待的超时时间。

            第二次判空队列是在等待超时后。如果等待超时后队列仍然为空，那么函数将返回false，表示弹出操作失败。

            这两次判空队列的目的是确保在等待超时的情况下，队列仍然为空，以避免在没有元素可供弹出时返回错误的结果
        */
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex; // 锁
    cond m_cond;    // 条件变量

    T *m_array;     // 队列数组  当循环数组用
    int m_size;     // 队列当前元素个数
    int m_max_size; // 队列最大长度
    int m_front;    // 队首元素索引
    int m_back;     // 队尾元素索引
};

#endif
