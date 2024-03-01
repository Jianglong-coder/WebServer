#ifndef __THRAEDPOOL_H__
#define __THRAEDPOOL_H__

#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <functional>

class ThreadPool
{
public:
    using Task = std::packaged_task<void()>;

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    static ThreadPool &get_instance()
    {
        static ThreadPool ins;
        return ins;
    }
    ~ThreadPool()
    {
        stop();
    }
    int threadCount() // 返回当前线程池中的线程数量
    {
        return thread_num_;
    }
    template <typename F, typename... Args>
    auto commit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        using ReturnType = decltype(f(args...));
        if (stop_.load())
            return std::future<ReturnType>{};
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<ReturnType> ret = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace([task]
                           { (*task)(); });
        }
        cv_.notify_one();
        return ret;
    }

private:
    ThreadPool(unsigned int num = 8) : stop_(false)
    {
        if (num < 1)
            thread_num_ = 1;
        else
            thread_num_ = num;
        start();
    }
    void start()
    {
        for (int i = 0; i < thread_num_; ++i)
        {
            pool_.emplace_back([this]
                               {
                while (!this->stop_.load())//如果线程池 运行没停止
                {
                    Task task; //用于存储一会从队列取出的任务(封装好的可调用对象)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);//要访问任务队列前  先上锁
                        this->cv_.wait(lock, [this] {
                            return this->stop_.load() || !this->tasks_.empty();
                        });//队列为空 或者 线程池停止运行 阻塞在条件变量
                        if (this->tasks_.empty())
                            return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    --this->thread_num_;//线程数量减1
                    task();//执行从任务队列中拿出的任务
                    ++this->thread_num_;//线程数量+1
                } });
        }
    }
    void stop()
    {
        stop_.store(true); // 线程池运行停止
        cv_.notify_all();  // 唤醒所有线程
        for (auto &sub_thread : pool_)
        {
            if (sub_thread.joinable())
            {
                std::cout << "join thread " << sub_thread.get_id() << std::endl;
                sub_thread.join(); // 等待子线程结束
            }
        }
    }

private:
    std::mutex mutex_;              // 互斥量
    std::condition_variable cv_;    // 条件变量
    std::atomic_bool stop_;         // 线程池是否停止
    std::atomic_int thread_num_;    // 线程数量
    std::queue<Task> tasks_;        // 任务队列
    std::vector<std::thread> pool_; // 存放线程的容器
};

#endif // !__THREADPOOL_H__