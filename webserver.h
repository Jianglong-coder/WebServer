#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./memorypool/memorypool.h"
const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model); // 服务器参数初始化

    void init_memorypool_array();                              // 初始化内存池
    void thread_pool();                                        // 初始化线程池资源
    void sql_pool();                                           // 初始化数据库连接池
    void log_write();                                          // 日志初始化
    void trig_mode();                                          // 触发模式设置
    void eventListen();                                        // 创建监听套接字和epollfd以及其他变量初始化
    void eventLoop();                                          // 服务器主程序 不断循环epoll_wait()
    void timer(int connfd, struct sockaddr_in client_address); // 创建定时器
    void adjust_timer(util_timer *timer);                      // 调整定时器在链表中的位置
    void deal_timer(util_timer *timer, int sockfd);            // 删除定时器
    bool dealclientdata();                                     // 与客户端建立连接
    bool dealwithsignal(bool &timeout, bool &stop_server);     // 处理信号
    void dealwithread(int sockfd);                             // 处理读事件
    void dealwithwrite(int sockfd);                            // 处理写事件

public:
    // 基础
    int m_port;       // 端口号  默认9006
    char *m_root;     // 网站(资源)根目录  由当前工作目录和root目录拼接成
    int m_log_write;  // 日志写入的方式  0:同步  1:异步  默认为0
    int m_close_log;  // 是否关闭日志功能   0:打开  1:关闭   默认打开
    int m_actormodel; // 并发模型选择  0 ：proactor  1 ： reactor  默认为0

    int m_pipefd[2];  // 用于创建全双工管道    作用是出现信号时 通过信号回调函数给服务器主程序发送信号数据 以便主程序根据对应信号去处理
    int m_epollfd;    // epoll文件描述符
    http_conn *users; // 客户端连接数组

    // 数据库相关
    connection_pool *m_connPool; // 数据库连接池
    string m_user;               // 用户名
    string m_passWord;           // 密码
    string m_databaseName;       // 数据库名
    int m_sql_num;               // 数据库连接池中连接的数量 默认8

    // 线程池相关
    ThreadPool *m_pool; // 线程池
    int m_thread_num;   // 线程池中线程数量  默认8

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER]; // 事件就绪数组

    int m_listenfd;       // 监听套接字
    int m_OPT_LINGER;     // 是否优雅关闭  0:不使用  1:使用   默认0  作用？
    int m_TRIGMode;       // 触发组合模式,默认listenfd LT + connfd LT
    int m_LISTENTrigmode; // listenfd触发模式，默认LT
    int m_CONNTrigmode;   // connfd触发模式，默认LT

    // 定时器相关
    client_data *users_timer; // 定时器数组 里面保存每个客户端连接的信息和对应的定时器
    Utils utils;              // 专门负责 epoll事件注册修改的。
};
#endif
