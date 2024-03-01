#include "heap_timer.h"
#include "../http/http_conn.h"

void time_heap::resize()
{
    heap_timer **temp = new heap_timer *[2 * capacity];
    for (int i = 0; i < 2 * capacity; ++i)
    {
        temp[i] = nullptr;
    }
    if (!temp)
    {
        throw std::exception();
    }
    capacity = 2 * capacity;
    for (int i = 0; i < cur_size; ++i)
    {
        delete[] array;
        array = temp;
    }
}
void time_heap::add_timer(heap_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (cur_size >= capacity)
    {
        resize();
    }
    int hole = cur_size++; // 新插入另一个元素 当前堆大小+1 hole是新建空穴的位置
    int parent = 0;
    for (; hole > 0; hole = parent)
    {
        parent = (hole - 1) / 2;
        if (array[parent]->expire <= timer->expire)
        {
            break;
        }
        array[hole] = array[parent];
    }
    array[hole] = timer;
}
void time_heap::del_timer(heap_timer *timer)
{
    if (!timer)
    {
        return;
    }
    /*
        仅仅将目标定时器的回调函数设置为空 即所谓的延迟销毁 这样将节省真正删除该定时器造成的开销
        但这样做容易使堆数组膨胀
    */
    timer->cb_func = nullptr;
}
heap_timer *time_heap::top() const // 获得堆顶部定时器
{
    if (empty())
    {
        return nullptr;
    }
    return array[0];
}

void time_heap::pop_timer()
{
    if (empty())
    {
        return;
    }
    if (array[0])
    {
        delete array[0];
        /* 将原来的堆顶元素替换成对数组中最后一个元素*/
        array[0] = array[--cur_size];
        percolate_down(0); // 对新的堆顶元素执行下虑操作
    }
}
void time_heap::tick()
{
    heap_timer *tmp = array[0];
    time_t cur = time(nullptr);
    // 循环处理堆中到时的定时器
    while (!empty())
    {
        if (!tmp)
        {
            break;
        }
        // 如果堆顶定时器没到期 则退出循环
        if (tmp->expire > cur)
        {
            break;
        }
        if (array[0]->cb_func)
        {
            array[0]->cb_func(array[0]->user_data);
        }
        // 删除堆顶元素
        pop_timer();
        tmp = array[0];
    }
}
// 最小堆的下率操作
void time_heap::percolate_down(int hole)
{
    heap_timer *temp = array[hole];
    int child = 0;
    for (; (hole * 2 + 1) <= (cur_size - 1); hole = child)
    {
        child = hole * 2 + 1;
        if ((child < (cur_size - 1)) && (array[child + 1]->expire < array[child]->expire))
        {
            ++child;
        }
        if (array[child]->expire < temp->expire)
        {
            array[hole] = array[child];
        }
        else
            break;
    }
    array[hole] = temp;
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
    m_timer_heap.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0); // 发送给客户端服务器繁忙
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0); // 将客户端连接文件描述符从epollfd实例中删除
    assert(user_data);
    close(user_data->sockfd);  // 关闭客户端连接
    http_conn::m_user_count--; // 连接总数-1
}