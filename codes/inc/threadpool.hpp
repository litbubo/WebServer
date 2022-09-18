#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <cassert>

class ThreadPool
{

public:
    explicit ThreadPool(size_t threadCount = 12) : pool_(std::make_shared<pool>())
    {
        assert(threadCount > 0);
        /* 创建threadCount个线程，并且线程分离 */
        for (size_t i = 0; i < threadCount; i++)
        {
            std::thread([=]()
                        {
                        std::unique_lock<std::mutex> locker(pool_->mtx);
                        while (true)
                        {
                            if (pool_->task.empty() != true)
                            {
                                auto task = std::move(pool_->task.front());
                                pool_->task.pop();
                                locker.unlock();
                                /* 执行任务*/
                                task();
                                locker.lock();
                            }
                            else if (pool_->isClose == true)
                            {
                                break;
                            }
                            else
                            {
                                /* 无任务则等待生产者唤醒 */
                                pool_->cond.wait(locker);
                            }
                        } })
                .detach();
        }
    }
    ~ThreadPool()
    {
        if (static_cast<bool>(pool_))
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);
            /* 设置线程池关闭标志，让线程自杀 */
            pool_->isClose = true;
        }
        pool_->cond.notify_all();
    }
    ThreadPool(ThreadPool &&) = default;
    template <typename F>
    void addTask(F &&task);

private:
    struct pool
    {
        std::mutex mtx;
        std::condition_variable cond;
        std::queue<std::function<void()>> task; // 线程池任务队列
        bool isClose;
    };

    std::shared_ptr<pool> pool_;
};

/* 添加任务 */
template <typename F>
void ThreadPool::addTask(F &&task)
{
    {
        std::lock_guard<std::mutex> locker(pool_->mtx);
        pool_->task.emplace(task);
    }
    pool_->cond.notify_one();
}