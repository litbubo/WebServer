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
