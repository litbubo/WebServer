#include <blockqueue.h>

#include <chrono>

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
T BlockQueue<T>::front() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

/*
 * 获取队尾元素
 */
template <typename T>
T BlockQueue<T>::back() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

/*
 * 获取队列容量
 */
template <typename T>
size_t BlockQueue<T>::capacity() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

/*
 * 获取队列大小
 */
template <typename T>
size_t BlockQueue<T>::size() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return size;
}

/*
 * 获取队列是否为空
 */
template <typename T>
bool BlockQueue<T>::empty() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

/*
 * 获取队列是否为满
 */
template <typename T>
bool BlockQueue<T>::full() const
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size >= capacity_;
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
        condProducer_.wait(locker);
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
        if (condProducer_.wait_for(locker, std::chrono::seconds(timeout)) == std::cv_status::timeout)
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