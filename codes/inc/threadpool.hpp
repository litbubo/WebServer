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
    explicit ThreadPool(size_t threadCount = 12);
    ~ThreadPool();
    ThreadPool(ThreadPool &&) = default;
    template <typename F>
    void addTask(F &&task);

private:
    struct pool
    {
        std::mutex mtx;
        std::condition_variable cond;
        std::queue<std::function<void()>> task;
        bool isClose;
    };

    std::shared_ptr<pool> pool_;
};

ThreadPool::ThreadPool(size_t threadCount) : pool_(std::make_shared<pool>())
{
    assert(threadCount > 0);
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
                                task();
                                locker.lock();
                            }
                            else if (pool_->isClose == true)
                            {
                                break;
                            }
                            else
                            {
                                pool_->cond.wait(locker);
                            }
                        } })
            .detach();
    }
}

ThreadPool::~ThreadPool()
{
    if (static_cast<bool>(pool_))
    {
        std::lock_guard<std::mutex> locker(pool_->mtx);
        pool_->isClose = true;
    }
    pool_->cond.notify_all();
}

template <typename F>
void ThreadPool::addTask(F &&task)
{
    {
        std::lock_guard<std::mutex> locker(pool_->mtx);
        pool_->task.emplace(task);
    }
    pool_->cond.notify_one();
}