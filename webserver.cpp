#include "webserver.h"

void handle_request(http_conn *request, int m_actor_model, connection_pool *m_connPool)
{
    if (!request)
        return;
    if (1 == m_actor_model)
    {
        if (0 == request->m_state)
        {
            if (request->read_once())
            {
                request->improv = 1;
                connectionRAII mysqlcon(&request->mysql, m_connPool);
                request->process();
            }
            else
            {
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
        else
        {
            if (request->write())
            {
                request->improv = 1;
            }
            else
            {
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
    }
    else
    {
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        request->process();
    }
}

WebServer::WebServer()
{
    // 在堆区创建http_conn类对象数组(客户端连接数组)
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    /*
        getcwd是一个C库函数，用于获取当前工作目录的路径。它的原型如下：
        char *getcwd(char *buf, size_t size);
    */
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path); // 将server_path的内容复制到m_root中
    strcat(m_root, root);        // 将root的内容追加到m_root的末尾

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    // delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

// 触发模式设置  监听套接字和连接套接字的触发模式设置
void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

// 日志初始化
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write) // 日志写入方式为异步
            // get_instance为Log类的静态函数获取日志唯一实例 init()对日志对象初始化
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else // 日志写入方式为同步
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 初始化数据库连接池
void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();                                                     // connection_pool对象的静态函数 创建一个了静态数据库连接池对象(只有第一次调用这个函数才会创建对象)并返回
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log); // 连接池初始化

    // 初始化数据库读取表
    // 利用数组中的第一个http_conn对象初始化数据库相关的设置 存入所有的用户名和密码
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    // 线程池
    //  m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
    m_pool = &(ThreadPool::get_instance());
}

void WebServer::init_memorypool_array()
{
    init_MemoryPool();
}
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        /*
            struct linger {
                int l_onoff;    // 是否启用SO_LINGER选项 0表示关闭  1表示打开
                int l_linger;   // 延迟关闭的时间，单位为秒
            };
        */
        struct linger tmp = {0, 1}; // 默认的关闭模式:不启用SO_LINGER选项
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        // close关闭套接字时 如果还有未发送的数据 套接字将等待1秒钟尝试发送这些数据
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    /*
        设置端口复用:
        SO_REUSEADDR选项允许在套接字关闭后立即重新使用相同的地址和端口。
        这对于服务器程序在重启后快速恢复服务很有用。
        flag参数是一个整数，用于指定选项的值。
    */
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT); // 初始化时钟信号的触发时间间隔

    // epoll创建内核事件表
    // epoll_event events[MAX_EVENT_NUMBER];//就绪事件表  类内有一个相同的数组  这里重复定义了
    m_epollfd = epoll_create(5); // 创建epollfd
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); // utils的u_epollfd再本函数最后赋值了 这里可以先赋值就不用传入m_epollfd了
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd); // 创建了全双工管道(匿名套接字) 作用是传递信号
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    /*
        将SIGPIPE信号的处理方式设置为忽略。
        SIGPIPE信号在写入已关闭的套接字时会触发，
        默认情况下会导致进程终止。
        通过将其处理方式设置为忽略，可以避免进程终止。
    */
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false); // 注册定时器信号的信号处理函数
    utils.addsig(SIGTERM, utils.sig_handler, false); // 注册服务器停止信号的信号处理函数

    /*
        设置一个定时器，当定时器到达指定的时间后，会触发一个SIGALRM信号。
        alarm(TIMESLOT)的作用是设置一个定时器，时间间隔为TIMESLOT(5)秒
        alarm(TIMESLOT)函数只会触发一次定时器。它会在指定的时间间隔（TIMESLOT）之后发送一个SIGALRM信号，
        SIGALRM的信号捕捉函数就会调用该信号的回调函数 向管道中写入该信号 而管道的读端已经被epollfd监听了
        所以服务器的主程序会从管道读取信号 发现是SIGALRM信号后就会对客户端的定时器链表做检查 如果有超时的客户端连接就断开连接
        也就是说服务器每过TIEMSLOT时间就检查哪个用户连接超时了
        然后定时器就会被重置为0。如果需要定期触发定时器，
        可以在信号处理函数中再次调用alarm(TIMESLOT)函数来设置下一次定时器。
    */
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName); // 初始化client_data数据

    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);

    // tw_timer *timer = new tw_timer();
    // timer->user_data = &users_timer[connfd];
    // timer->cb_func = cb_func;
    // time_t timeout = 10 * TIMESLOT;
    // users_timer[connfd].timer = timer;
    // utils.m_timer_wheel.add_timer(timeout, timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
// void WebServer::adjust_timer(tw_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust timer once");

    // time_t timeout = 10 * TIMESLOT;
    // utils.m_timer_wheel.adjust_timer(timeout, timer);
    // LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
// void WebServer::deal_timer(tw_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);

    // if (!timer)
    // {
    //     printf("deal_timer timer is nullptr");
    //     return;
    // }
    // timer->cb_func(&users_timer[sockfd]);
    // if (timer)
    // {
    //     utils.m_timer_wheel.del_timer(timer);
    // }
    // LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() // 处理客户端新连接
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode) // 水平触发模式
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) // 超过最大连接数
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 客户端连接初始化也放在timer里了  我觉得放着这行比较好  名字都是timer了 最好只做定时器的工作
        timer(connfd, client_address); // 给新建立的连接添加定时器
    }

    else // 边缘触发模式 需要循环处理
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy"); // 给客户端发送服务器繁忙的数据
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address); // 给新建立的连接添加定时器
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0); // 将管道中的信号都读出  读到signals中
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM: // SIGALRM信号 每5秒发一次 将timeout设置为true 后续会根据timeout的值处理
            {
                timeout = true;
                break;
            }
            case SIGTERM: // SIGTERM信号 服务器停止循环
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 读数据
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // tw_timer *timer = users_timer[sockfd].timer;
    // reactor  该模式下 主线程只监听事件 任务交给子线程处理
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer); // 先调整定时器 将定时器上的时间重置
        }

        // 若监测到读事件，将该事件放入请求队列
        // m_pool->append(users + sockfd, 0); // 将读工作放入线程池的请求队列中  读为0, 写为1  拿到读事件的子线程在读完之后 直接就调用process()处理请求了  并不是读完之后在放到请求队列上 也就是说拿到这个任务的子线程做了两件事读和解析请求(调用process())
        m_pool->commit(handle_request, users + sockfd, m_actormodel, m_connPool);
        while (true) // 主线程在这阻塞  直到子线程读完  这里不是很理解 这样用子线程读就没有意义了 主线程还是得等子线程读完
        {
            if (1 == users[sockfd].improv) // improv和timer_flag在子线程的工作函数run里面读完了会改变他俩的值  improv初始化是0 子线程读完变成1(读了就为1，不管读失败还是读成功)
            {
                if (1 == users[sockfd].timer_flag) // timer_flag为1 说明读函数http_conn::read_once()失败
                {
                    deal_timer(timer, sockfd);    // 读失败了 删除套接字
                    users[sockfd].timer_flag = 0; // 这里已经关闭连接了 等新连接(如果被分配这个sockfd)覆盖这个sockfd时 会重新初始化这些值  所以这里timer_flag置零有什么意义？
                }
                users[sockfd].improv = 0; // 连接还没有关闭  下次读数据还需要用到improv 所以置零
                break;
            }
        }
    }
    else
    {
        // proactor  在proactor模式下 主线程读完数据还要将解析请求的任务放到请求队列上
        if (users[sockfd].read_once()) // 主线程读数据
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            // m_pool->append_p(users + sockfd); // 在读完数据之后 还需要处理请求 所以再次加入请求队列 让子线程处理请求
            m_pool->commit(handle_request, users + sockfd, m_actormodel, m_connPool);
            if (timer)
            {
                adjust_timer(timer); // 调整定时器
            }
        }
        else // 没读到数据 删除定时器和关闭文件描述符 关闭连接
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // tw_timer *timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer); // 重新设置和调整定时器
        }

        // m_pool->append(users + sockfd, 1); // 加入线程池的请求队列
        (users + sockfd)->m_state = 1;
        m_pool->commit(handle_request, (users + sockfd), m_actormodel, m_connPool);

        while (true) // 只有当所有数据写完了 这个循环才会停止
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].write()) // 主线程写
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer); // 调整定时器
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 服务器运行主程序
void WebServer::eventLoop()
{
    bool timeout = false;     // 当出现SIGALRM时 会置为true
    bool stop_server = false; // 当出现SIGTERM时 会置为true

    while (!stop_server) // 一直循环等待就绪事件  当出现SIGTERM时 跳出循环
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1); //-1表示无限等待直到有事件发生 所以epoll_wait会阻塞
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure"); // 写入日志
            break;
        }

        for (int i = 0; i < number; i++) // 遍历就绪事件数组
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata(); // 与客户端建立连接
                if (false == flag)
                    continue;
            }
            /*
                - EPOLLRDHUP表示对端关闭连接或者关闭了写操作。当这个事件发生时，表示与客户端的连接已经断开或者对端已经关闭了写操作。
                - EPOLLHUP表示发生了挂起事件。当这个事件发生时，表示与客户端的连接已经断开或者发生了异常情况。
                - EPOLLERR表示发生了错误事件。当这个事件发生时，表示与客户端的连接发生了错误。
            */
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
                // tw_timer *timer = users_timer[sockfd].timer;
                // deal_timer(timer, sockfd);
            }
            // 处理信号
            /*
                时钟信号是在eventListen()中创建的 5秒发一次  并且注册了时钟信号的捕捉  同时也注册了停止信号的捕捉
                捕捉信号后就会把信号写入管道中   管道已经在epollfd中注册了(即监听管道文件描述符)
                所以当前这种情况是 时钟信号(或者停止信号)的回调函数向管道中写的数据(数据内容就是信号)被epollfd监听到了
            */
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server); // 去处理信号
                if (false == flag)
                    LOG_ERROR("%s", "dealwithsignal failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout) // 如果有时钟信号
        {
            // 定期检查定时器队列 有过期的连接就删除 没有就什么都不做
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}