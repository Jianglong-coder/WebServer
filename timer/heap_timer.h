#ifndef LST_TIMER
#define LST_TIMER

// #include <unistd.h>
// #include <signal.h>
// #include <sys/types.h>
// #include <sys/epoll.h>
// #include <fcntl.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <assert.h>
// #include <sys/stat.h>
// #include <string.h>
// #include <pthread.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/mman.h>
// #include <stdarg.h>
// #include <errno.h>
// #include <sys/wait.h>
// #include <sys/uio.h>

// #include <time.h>
// #include "../log/log.h"

#include <iostream>
#include <netinet/in.h>
#include <time.h>
using std::exception;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    heap_timer *timer;
};
class heap_timer
{
public:
    heap_timer(int delay = 0)
    {
        expire = time(nullptr) + delay;
    }

public:
    time_t expire;                  // 定时器绝对生效时间
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;         // 用户数据
};

class time_heap
{
public:
    time_heap(int cap = 0) : capacity(cap), cur_size(0)
    {
        array = new heap_timer *[capacity]; // 创建堆数组
        if (!array)
        {
            throw ::std::exception();
        }
        for (int i = 0; i < capacity; ++i)
        {
            array[i] = nullptr;
        }
    }
    ~time_heap()
    {
        for (int i = 0; i < cur_size; ++i)
        {
            delete array[i];
        }
        delete[] array;
    }

public:
    void resize();
    void add_timer(heap_timer *timer);
    void del_timer(heap_timer *timer);

    heap_timer *top() const; // 获得堆顶部定时器

    void pop_timer(); // 删除堆顶部定时器
    void tick();
    bool empty() const { return cur_size == 0; }

private:
    void percolate_down(int hole); // 最小堆的下率操作

private:
    heap_timer **array; // 堆数组
    int capacity;       // 堆数组容量
    int cur_size;       // 堆数组当前包含元素的个数
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;   // 由server程序定义的匿名管道
    time_heap m_timer_heap; // 定时器链表
    static int u_epollfd;   // 由server程序定义的epollfd
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);
#endif