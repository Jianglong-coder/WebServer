#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;        // 日志行数记录
    m_is_async = false; // 是否同步标志位 true为异步 false为同步 默认同步
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
// 异步需要设置阻塞队列的长度max_queue_size，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;                                     // 异步
        m_log_queue = new block_queue<string>(max_queue_size); // 创建阻塞队列
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL); // 创建了一个异步线程来写日志 该线程的工作函数为flush_log_thread
    }

    m_close_log = close_log;             // 是否关闭日志 0:打开  1:关闭   默认打开
    m_log_buf_size = log_buf_size;       // 日志缓冲区大小
    m_buf = new char[m_log_buf_size];    // 创建日志缓冲区
    memset(m_buf, '\0', m_log_buf_size); // 清空日志缓冲区
    m_split_lines = split_lines;         // 日志最大行数

    time_t t = time(NULL);             // 获取当前的时间，系统格式的时间
    struct tm *sys_tm = localtime(&t); // 转换成人可以看懂的时间
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/'); // 搜索最后一次出现'/'的位置
    char log_full_name[256] = {0};           // 保存要打开的日志文件的最终路径

    if (p == NULL)
    {
        // 生成例如 2023_09_27_ServerLog
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);                         // log_name = ServerLog
        strncpy(dir_name, file_name, p - file_name + 1); // 将/前面的路径名(包括/)复制到dir_name中
        //  生成例如 ./2023_09_27_ServerLog
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name); // 将路径名时间文件名拼接起来
    }

    m_today = my_tm.tm_mday; // 记录当前的日期

    m_fp = fopen(log_full_name, "a"); // 打开日志文件
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0}; // now的秒数和微秒数初始化为0
    gettimeofday(&now, NULL);    // 将当前系统时间的秒数和微秒数存在now中
    time_t t = now.tv_sec;       // 将now的秒数赋给t
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm; // 当前系统时间
    char s[16] = {0};          // 用于拼接最终的日志信息
    switch (level)             // 根据level 在日志信息最前面加上该条日志信息的类型
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++; // 日志行数+1

    // 当前日期和日志的日期不相同了 或者日志达到最大行了  生成一个新的日期文件
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) // everyday log
    {

        char new_log[256] = {0};
        fflush(m_fp); // 强制刷新写入流缓冲区
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday) // 日期不同 需要新开日志文件
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name); // 将路径名 系统年月日 文件名 拼接写道newlog中
            m_today = my_tm.tm_mday;                                    // 更新日志实例中的日期
            m_count = 0;                                                // 如果是开新日期的日志文件 则m_count需要归0  但是如果是日志满了新开的文件还是这个日期的文件行数就不归0 而是继续记录
        }
        else // 行数满了 新开文件
        {
            // 生成例如 2023_09_27_ServerLog1
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a"); // 打开新的文件
    }

    m_mutex.unlock();

    va_list valst;           // 声明一个va_list类型变量用于存储可变参数列表
    va_start(valst, format); // 使用 va_start 宏将可变参数列表的起始位置初始化为 format

    string log_str; // 用于存储最终的日志内容
    m_mutex.lock();

    // 写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", // 这里应该是m_log_buf_size 但是为什么给了48固定值？
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s); // 将当前系统时间 写入m_buf 然后把s加在系统时间后面

    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst); // 将日志内容实际内容写入m_buf
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf; // 将m_buf中的内容取出到log_str中 因为把锁解开后 m_buf中内容可能就被后来的内容覆盖了   但是这个项目不管同步还是异步都只有一个线程操作日志 所以没事

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full()) // 如果是异步日志 并且阻塞队列没满
    {
        m_log_queue->push(log_str); // 日志内容加入阻塞队列  让子线程写
    }
    else // 同步日志 让主线程写
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    /*
        fputs(log_str.c_str(), m_fp) 和 fflush(m_fp) 这两个操作是不同的，
        前者是将日志内容写入到缓冲区，后者是将缓冲区的内容立即写入到文件中
    */
    m_mutex.lock();
    // 强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
