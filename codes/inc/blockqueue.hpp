#pragma once

#include <chrono>
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
    bool empty();
    bool full();
    size_t size();
    size_t capacity();
    T front();
    T back();
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

template <typename T>
BlockQueue<T>::BlockQueue(size_t maxCapacity) : capacity_(maxCapacity)
{
    assert(maxCapacity > 0);
}

template <typename T>
BlockQueue<T>::~BlockQueue()
{
    this->close();
}

/*
 * 加锁清空队列，关闭队列，唤醒所有等待事件
 */
template <typename T>
void BlockQueue<T>::close()
{
    {
        std::lock_guard<std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    condProducer_.notify_all();
    condConsumer_.notify_all();
}

/*
 * 唤醒一个消费者执行任务
 */
template <typename T>
void BlockQueue<T>::flush()
{
    condConsumer_.notify_one();
}

/*
 * 清空消息队列
 */
template <typename T>
void BlockQueue<T>::clear()
{
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

/*
 * 获取队首元素
 */
template <typename T>
T BlockQueue<T>::front()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

/*
 * 获取队尾元素
 */
template <typename T>
T BlockQueue<T>::back()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

/*
 * 获取队列容量
 */
template <typename T>
size_t BlockQueue<T>::capacity()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

/*
 * 获取队列大小
 */
template <typename T>
size_t BlockQueue<T>::size()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

/*
 * 获取队列是否为空
 */
template <typename T>
bool BlockQueue<T>::empty()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

/*
 * 获取队列是否为满
 */
template <typename T>
bool BlockQueue<T>::full()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

/*
 * 向队尾加入一个元素
 */
template <typename T>
void BlockQueue<T>::push_back(const T &item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    /* 没有元素消生产者应该阻塞 */
    while (deq_.size() >= capacity_)
    {
        condProducer_.wait(locker);
    }
    deq_.push_back(item);
    /* 唤醒一个消费者 */
    condConsumer_.notify_one();
}

/*
 * 向队头加入一个元素
 */
template <typename T>
void BlockQueue<T>::push_front(const T &item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    /* 没有元素消生产者应该阻塞 */
    while (deq_.size() >= capacity_)
    {
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    /* 唤醒一个消费者 */
    condConsumer_.notify_one();
}

/*
 * 队头弹出一个元素
 */
template <typename T>
bool BlockQueue<T>::pop(T &item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    /* 消费者阻塞 */
    while (deq_.empty())
    {
        condConsumer_.wait(locker);
        if (isClose_ == true)
            return false;
    }
    item = deq_.front();
    deq_.pop_front();
    /* 唤醒一个生产者 */
    condProducer_.notify_one();
    return true;
}
/*
 * 队头弹出一个元素，带超时返回
 */
template <typename T>
bool BlockQueue<T>::pop(T &item, int timeout)
{
    std::unique_lock<std::mutex> locker(mtx_);
    /* 消费者阻塞 */
    while (deq_.empty())
    {
        if (condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) == std::cv_status::timeout)
            return false;
        if (isClose_ == true)
            return false;
    }
    item = deq_.front();
    deq_.pop_front();
    /* 唤醒一个生产者 */
    condProducer_.notify_one();
    return true;
}