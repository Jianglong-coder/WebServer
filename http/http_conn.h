#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
#include "../LFU/LFUCache.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;       // 文件名最大长度
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    /*
        CHAECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在分析请求体
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST:         请求不完整，需要继续读取客户数据
        GET_REQUEST:        表示获得了一个完整的客户请求
        BAD_REQUEST:        表示客户请求语法错误
        NO_RESOURCE:        表示服务器没有资源
        FORBIDDEN_REQUEST:  表示客户对资源没有足够的访问权限
        FILE_REQUEST:       文件请求，获取文件成功
        INTERNAL_ERROR:     表示服务器内部错误
        CLOSED_CONNECTION:  表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    /*
        从状态机的三种可能状态，即行的读取状态，分别表示：
        1.读取到一个完整行
        2.行出错
        3.行数据尚且不完整
    */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname); // 初始化新接收到的连接
    void close_conn(bool real_close = true);                                                                      // 关闭连接
    void process();                                                                                               // 处理客户端的请求
    bool read_once();                                                                                             // 读
    bool write();                                                                                                 // 写
    sockaddr_in *get_address() { return &m_address; }                                                             // 返回通信的socket地址
    void initmysql_result(connection_pool *connPool);                                                             // 将数据库中所有的用户名和密码查询回来并保存在一个map中
    int timer_flag;                                                                                               // reactor模式下 通知主线程 请求的内容全部读完失败了 然后关闭连接
    int improv;                                                                                                   // reactor模式下 通知主线程 内容全部读完了

private:
    void init();                                            // 初始化连接其余的数据
    HTTP_CODE process_read();                               // 解析HTTP请求
    bool process_write(HTTP_CODE ret);                      // 填充http应答
    HTTP_CODE parse_request_line(char *text);               // 解析请求首行
    HTTP_CODE parse_headers(char *text);                    // 解析请求头
    HTTP_CODE parse_content(char *text);                    // 解析请求内容
    HTTP_CODE do_request();                                 //
    char *get_line() { return m_read_buf + m_start_line; }; // 获取一行数据
    LINE_STATUS parse_line();                               // 解析一行
    void unmap();                                           // 释放内存映射
    bool add_response(const char *format, ...);             // 往写缓冲中写入待发送的数据 format是添加格式 ...是可变参数
    bool add_content(const char *content);                  // 添加响应体
    bool add_status_line(int status, const char *title);    // 添加响应行
    bool add_headers(int content_length);                   // 添加响应头
    bool add_content_type();                                // 添加响应体类型的响应头
    bool add_content_length(int content_length);            // 添加响应体长度的响应头
    bool add_linger();                                      // 添加是否保持连接的响应头
    bool add_blank_line();                                  // 添加空行

public:
    static int m_epollfd;    // epollfd示例 用于监听事件
    static int m_user_count; // 连接总数
    MYSQL *mysql;            // 数据库连接
    int m_state;             // 读为0, 写为1  在reactor模式下 用于区分在工作队列上时 是读还是写

private:
    int m_sockfd;                        // 该HTTP连接的socket
    sockaddr_in m_address;               // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE];   // 读缓冲区
    long m_read_idx;                     // 标识读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置
    long m_checked_idx;                  // 当前正在分析的字符再读缓冲区的位置
    int m_start_line;                    // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx;                     // 当前写缓冲区中写到的位置
    CHECK_STATE m_check_state;           // 主状态机当前所处的状态
    METHOD m_method;                     // 请求方法
    char m_real_file[FILENAME_LEN];      // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_rul, doc_root是网站的根目录
    char *m_url;                         // 请求目标文件的文件名
    char *m_version;                     // 协议版本
    char *m_host;                        // 请求的主机名
    long m_content_length;               // 请求体长度
    bool m_linger;                       // HTTP请求是否要保持连接
    char *m_file_address;                // 客户请求的目标文件mmap到内存中的起始位置
    struct stat m_file_stat;             // 目标文件的状态，通过它我们可以判断文件是否存在，是否为目录，是否可读，并获取文件大小信息
    /*
        struct iovec m_iv[2];
        int m_iv_count;
        如果要返回文件 则要将写缓冲区和文件都返回 但是它们不在地址连续的一段空间 而是在两块内存块
        所以采用writev来执行写操作 所以定义下面两个成员 其中m_iv_count表示被写内存块的数量
    */
    struct iovec m_iv[2]; // 内存块数组
    int m_iv_count;       // 表示被写内存块的数量
    int cgi;              // 是否启用的POST
    char *m_string;       // 存储请求内容数据(POST提交的内容(一般是用户名和密码)) 在解析请求头的时候会保存到这个变量中
    int bytes_to_send;    // 将要发送的字节数
    int bytes_have_send;  // 已经发送的字节数
    char *doc_root;       // 网站根目录

    map<string, string> m_users; // 这个变量没用到  我觉得是存储数据库中所有的用户名和密码的  但是放在http_conn.cpp的一个map中了  没有用到这个变量
    int m_TRIGMode;              // 触发模式
    int m_close_log;             // 是否启用日志

    char sql_user[100];   // 用户名
    char sql_passwd[100]; // 密码
    char sql_name[100];   // 数据库名
};

#endif
