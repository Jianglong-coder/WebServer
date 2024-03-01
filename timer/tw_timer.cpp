#include "lst_timer.h"
#include "../http/http_conn.h"


time_wheel::time_wheel() : cur_slot(0)
{
    for (int i = 0; i < N; ++i)
    {
        slots[i] = nullptr;
    }
}
time_wheel::~time_wheel()
{
    // 遍历每个槽 销毁其中的定时器
    for (int i = 0; i < N; ++i)
    {
        tw_timer *tmp = slots[i];
        while (tmp)
        {
            slots[i] = tmp->next;
            delete tmp;
            tmp = slots[i];
        }
    }
}
void time_wheel::add_timer(time_t timeout, tw_timer *timer) // 根据定时器timeout创建一个定时器 并把它插入合适的槽中
{
    if (timeout < 0)
    {
        return;
    }
    int ticks = 0;
    /*
        下面根据待插入定时器的超时值计算它将在时间轮转动多少个滴答后被触发
        并将该滴答数存储与变量ticks中 如果待插入定时器的超时值小于时间轮的槽间隔SI
        则将ticks向上折合为1 厚泽就将ticks向下折合为timeout/SI
    */
    if (timeout < SI)
    {
        ticks = 1;
    }
    else
    {
        ticks = timeout / SI;
    }

    int rotation = ticks / N;              // 计算待插入的定时器在时间轮转动多少圈后被触发
    int ts = ((cur_slot + ticks % N)) % N; // 计算待插入的定时器应该被插入哪个槽中
    // tw_timer *timer = new tw_timer(rotation, ts);
    timer->rotation = rotation; //
    timer->time_slot = ts;

    // 如果第ts个槽中尚无任何定时器 把新创建的定时器插入其中 并将该定时器设置为该槽的头节点
    if (!slots[ts])
    {
        // printf("add timer, rotation is %d, ts is %d, cur_slot is %d\n", rotation, ts, cur_slot);
        slots[ts] = timer;
    }
    // 否则 将定时器插入第ts个槽中
    else
    {
        timer->next = slots[ts];
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }
}

void time_wheel::del_timer(tw_timer *timer) // 删除目标定时器timer
{
    if (!timer)
    {
        return;
    }
    int ts = timer->time_slot;
    // slots[ts]是目标定时器所在槽的头节点 如果目标定时器就是该头节点 则需要重置第ts个槽的头节点
    if (timer == slots[ts])
    {
        slots[ts] = slots[ts]->next;
        if (slots[ts])
        {
            slots[ts]->prev = nullptr;
        }
        delete timer;
    }
    else
    {
        timer->prev->next = timer->next;
        if (timer->next)
            timer->next->prev = timer->prev;
        delete timer;
    }
}
void time_wheel::adjust_timer(time_t timeout, tw_timer *timer) // 删除目标定时器timer
{
    if (!timer)
    {
        return;
    }
    int ts = timer->time_slot;
    // slots[ts]是目标定时器所在槽的头节点 如果目标定时器就是该头节点 则需要重置第ts个槽的头节点
    if (timer == slots[ts])
    {
        slots[ts] = slots[ts]->next;
        if (slots[ts])
        {
            slots[ts]->prev = nullptr;
        }
        add_timer(timeout, timer);
    }
    else
    {
        timer->prev->next = timer->next;
        if (timer->next)
            timer->next->prev = timer->prev;
        add_timer(timeout, timer);
    }
}
void time_wheel::tick() // SI时间到后 调用该函数 时间轮向前滚动一个槽的间隔
{
    tw_timer *tmp = slots[cur_slot]; // 取得时间轮上当前槽的头节点
    // printf("current slot is %d\n", cur_slot);
    while (tmp)
    {
        // printf("tick the timer once\n");
        if (tmp->rotation > 0) // rotation > 0 说明 这个定时器的轮数还没到
        {
            --tmp->rotation;
            tmp = tmp->next;
        }
        else // 定时器已经到期 执行定时任务 然后删除该定时器
        {
            // printf("execute timer cb_func\n");
            tmp->cb_func(tmp->user_data);
            if (tmp == slots[cur_slot])
            {
                // printf("delete head in cur_slot\n");
                slots[cur_slot] = tmp->next;
                delete tmp;
                if (slots[cur_slot])
                    slots[cur_slot]->prev = nullptr;
                tmp = slots[cur_slot];
            }
            else
            {
                tmp->prev->next = tmp->next;
                if (tmp->next)
                {
                    tmp->next->prev = tmp->prev;
                }
                tw_timer *tmp2 = tmp->next;
                delete tmp;
                tmp = tmp2;
            }
        }
    }
    cur_slot = (cur_slot + 1) % N; // 更新时间轮的当前槽 以反映时间轮的转动
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot; // 每次alarm设置定时器的触发时间间隔
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // EPOLLRDHUP表示关注对端关闭连接事件
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0); // 向管道中写入信号
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_wheel.tick(); // 调用定时器链表的tick检查链表上的超时定时器
    alarm(m_TIMESLOT);  // 一次alarm只会引起一次SIGALRM信号 所以要重新定时 以不断触发SIGALRM
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0); // 发送给客户端服务器繁忙
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

// 函数指针 作用是将监听文件描述符从epollfd删除 然后关闭连接
void cb_func(client_data *user_data)
{
    if (!user_data)
    {
        printf("cb_func() user_data is nullptr");
        return;
    }
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0); // 将客户端连接文件描述符从epollfd实例中删除
    assert(user_data);
    close(user_data->sockfd);  // 关闭客户端连接
    http_conn::m_user_count--; // 连接总数-1
}
