#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <cassert>

/*
 * 实现线程安全的双端阻塞队列
 */
template <typename T>
class BlockQueue
{
public:
    explicit BlockQueue(size_t maxCapacity = 1000);
    ~BlockQueue();

    void clear();
    void close();
    void flush();
    bool empty() const;
    bool full() const;
    size_t size() const;
    size_t capacity() const;
    T front() const;
    T back() const;
    void push_back(const T &item);
    void push_front(const T &item);
    bool pop(T &item);
    bool pop(T &item, int timeout);

private:
    std::deque<T> deq_;
    std::mutex mtx_;
    std::condition_variable condConsumer_;
    std::condition_variable condProducer_;
    bool isClose_;
    size_t capacity_;
};
