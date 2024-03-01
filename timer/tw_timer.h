#ifndef TW_TIMER
#define TW_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "../log/log.h"

class tw_timer;
struct client_data
{
    sockaddr_in address;
    int sockfd;
    tw_timer *timer;
};
// 定时器类
class tw_timer
{
public:
    tw_timer(int rot = 0, int ts = 0) : next(nullptr), prev(nullptr), rotation(rot), time_slot(ts) {}

public:
    int rotation;                   // 记录定时器在时间轮转多少圈后生效
    int time_slot;                  // 记录定时器属于时间轮上哪个槽(对应的链表)
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;         // 客户数据
    tw_timer *next;
    tw_timer *prev;
};
class time_wheel
{
public:
    time_wheel();
    ~time_wheel();
    void add_timer(time_t timeout, tw_timer *timer);    // 根据定时器timeout创建一个定时器 并把它插入合适的槽中
    void adjust_timer(time_t timeout, tw_timer *timer); // 调整定时器的位置
    void del_timer(tw_timer *timer);                 // 删除目标定时器timer
    void tick();                                     // SI时间到后 调用该函数 时间轮向前滚动一个槽的间隔
private:
    static const int N = 60; // 时间轮上槽的数目
    static const int SI = 1; // 每1s时间轮转动一次
    tw_timer *slots[N];      // 时间轮的槽 其中每个元素指向一个定时器链表 链表无序
    int cur_slot;            // 时间轮的当前槽
};

/*工具类 一些处理文件描述符和设置信号处理函数等公用的工具函数放到这个类中了 同时也把定时器链表放到util中了不知道为啥*/
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
    static int *u_pipefd;     // 由server程序定义的匿名管道
    time_wheel m_timer_wheel; // 定时器链表
    static int u_epollfd;     // 由server程序定义的epollfd
    int m_TIMESLOT;           // alarm信号时间
};

void cb_func(client_data *user_data);

#endif
