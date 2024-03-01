#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    // C++11以后,使用局部变量懒汉不用加锁
    // 单例模式 参考 https://blog.csdn.net/u011718663/article/details/115922357?ops_request_misc=%257B%2522request%255Fid%2522%253A%2522169676203816800213096227%2522%252C%2522scm%2522%253A%252220140713.130102334..%2522%257D&request_id=169676203816800213096227&biz_id=0&utm_medium=distribute.pc_search_result.none-task-blog-2~all~baidu_landing_v2~default-1-115922357-null-null.142^v95^insert_down28v1&utm_term=c%2B%2B%E5%8D%95%E4%BE%8B%E6%A8%A1%E5%BC%8F%E4%BB%A3%E7%A0%81&spm=1018.2226.3001.4187
    static Log *get_instance() // 单例模式创建日志
    {
        /*
            如果当变量在初始化的时候，并发同时进入声明语句，并发线程将会阻塞等待初始化结束
            这样保证了并发线程在获取静态局部变量的时候一定是初始化过的，
            所以具有线程安全性 C++静态变量的生存期 是从声明到程序结束，这也是一种懒汉式
        */
        static Log instance; // 函数内部的静态变量  它在函数首次调用时进行初始化，并在后续的函数调用中保持其值
        return &instance;    // 返回一个静态日志实例
    }

    static void *flush_log_thread(void *args) // 异步日志的子线程工作函数
    {
        /*
            C++类中的线程处理函数必须使用全局函数或类静态成员函数
            C++成员函数都隐含了一个传递函数作为参数，亦即“this”指针
            而线程创建的时候需要给出工作函数 以及工作函数参数
            由于this指针，使CALLBACK型的成员函数作为回调函数时就会因为隐含的this指针使得函数参数个数不匹配，
            从而导致回调函数安装失败
        */
        Log::get_instance()->async_write_log();
    }
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void); // 强制刷新写入流缓冲区

private:
    Log(); // 单例模式下 构造函数为private
    virtual ~Log();
    void *async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];               // 路径名
    char log_name[128];               // log文件名
    int m_split_lines;                // 日志最大行数
    int m_log_buf_size;               // 日志缓冲区大小
    long long m_count;                // 日志行数记录
    int m_today;                      // 因为按天分类,记录当前时间是那一天
    FILE *m_fp;                       // 打开log的文件指针
    char *m_buf;                      // 日志缓冲区
    block_queue<string> *m_log_queue; // 阻塞队列   block_queue为自定义类型
    bool m_is_async;                  // 是否同步标志位
    locker m_mutex;                   // 写日志时的锁
    int m_close_log;                  // 关闭日志
};

#define LOG_DEBUG(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_INFO(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_WARN(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_ERROR(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }

#endif
