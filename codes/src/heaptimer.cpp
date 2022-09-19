
#include <heaptimer.h>

/*
 * 构造函数中预申请一些空间
 */
HeapTimer::HeapTimer()
{
    heap_.reserve(64);
}

/*
 * 析构函数中清空空间
 */
HeapTimer::~HeapTimer()
{
    this->clear();
}

/*
 * 将数组下标为i的节点与父节点进行比较
 * 如果子节点小于父节点则进行交换
 * 交换后，再次和新的父节点进行交换，直至下标为0，也就是小根堆的根节点
 * 这样保证了根节点永远是最小的
 */
void HeapTimer::siftParent(size_t i)
{
    assert(i >= 0 && i < heap_.size());
    /* j为i节点的父节点 */
    size_t j = (i - 1) / 2;
    LOG_DEBUG("i == %d, j == %d", i, j);
    while (j >= 0)
    {
        if (heap_[j] < heap_[i])
        {
            break;
        }
        this->swapNode(i, j);
        /* 更新当前节点的下标 */
        i = j;
        /* 选择新的父节点进行比较 */
        j = (i - 1) / 2;
    }
}

/*
 * 交换堆中两个节点
 */
void HeapTimer::swapNode(size_t i, size_t j)
{
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    /* std */
    std::swap(heap_[i], heap_[j]);
    /* 更新两个两个节点的位置下标记录，fd : pos */
    ref_[heap_[i].fd] = i;
    ref_[heap_[j].fd] = j;
}

/*
 * 将数组下标为index的元素与最大的子节点进行比较，大则交换
 * 保证父节点一定比子节点小
 * 一直和孩子节点比较下去，保证大的节点永远在叶子上
 * 该节点确实向后移动了，返回真
 * 该节点比孩子节点小没有移动，返回假
 */
bool HeapTimer::siftChild(size_t index, size_t n)
{
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    /* j为i节点的左子树 */
    size_t j = i * 2 + 1;
    while (j < n)
    {
        /* 判断有没有右子树参与比较 */
        /* 如果右子树小于左子树，则将右子树拿出来进行比较，否则还是用左子树进行比较 */
        if (j + 1 < n && heap_[j + 1] < heap_[j])
            j++;
        /* 父节点是孩子中最小的无需改变 */
        if (heap_[i] < heap_[j])
            break;
        swapNode(i, j);
        /* 更新当前节点下标 */
        i = j;
        /* 选择下一个左子树准备用来比较 */
        j = i * 2 + 1;
    }
    return i > index;
}

/*
 * 为指定文件描述符添加一个定时器到堆中
 */
void HeapTimer::add(int fd, int timeout, const std::function<void()> &cb)
{
    assert(fd >= 0);
    size_t i;
    /* 如果数组中没有该文件描述符的定时器返回0 */
    if (ref_.count(fd) == 0)
    {
        /* 新节点：堆尾插入，调整堆 */
        i = heap_.size();
        ref_[fd] = i;
        heap_.push_back({fd,
                         std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(timeout),
                         cb});
        /* 因为在堆尾插入，所以和父节点比较即可 */
        siftParent(i);
    }
    else
    {
        /* 已有结点：调整堆 */
        i = ref_[fd];
        heap_[i].expires = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(timeout);
        heap_[i].timeoutCb = cb;
        /* 先和子节点比较，若该节点比子节点都小，则反过来和父节点进行比较 */
        if (!siftChild(i, heap_.size()))
        {
            siftParent(i);
        }
    }
}

/*
 * 删除指定节点
 */
void HeapTimer::del(size_t index)
{
    /* 删除指定位置的结点 */
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if (i < n)
    {
        /* 将要删除的结点换到队尾，然后调整堆 */
        swapNode(i, n);
        if (!siftChild(i, n))
        {
            siftParent(i);
        }
    }
    /* 队尾元素删除 */
    ref_.erase(heap_.back().fd);
    heap_.pop_back();
}

/*
 * 调整当前节点的过期时间为 当前时间+timeout
 */
void HeapTimer::adjust(int fd, int timeout)
{
    /* 调整指定id的结点 */
    assert(!heap_.empty() && ref_.count(fd) > 0);
    heap_[ref_[fd]].expires = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(timeout);
    /* 因为调整节点时间后，肯定是时间加长为最长了，向后调整即可 */
    siftChild(ref_[fd], heap_.size());
}

/*
 * 删除队中所有的超时节点
 */
void HeapTimer::tick()
{
    /* 清除超时结点 */
    if (heap_.empty())
    {
        return;
    }
    while (!heap_.empty())
    {
        TimerNode node = heap_.front();
        /* 如果节点的时间戳毫秒数大于系统当前时间的数，则未超时 */
        /* 根节点未超时则子节点必定未超时 */
        if (std::chrono::duration_cast<std::chrono::milliseconds>(node.expires - std::chrono::high_resolution_clock::now()).count() > 0)
        {
            break;
        }
        /* 超时调用回调函数 */
        node.timeoutCb();
        this->pop();
    }
}

/*
 * 删除第一个节点，小根堆根节点
 */
void HeapTimer::pop()
{
    assert(!heap_.empty());
    del(0);
}

/*
 * 清空堆中元素
 */
void HeapTimer::clear()
{
    ref_.clear();
    heap_.clear();
}

/*
 * 删除所有超时节点，返回最近一个超时节点的超时时间
 */
int HeapTimer::getNextTick()
{
    /* 删除超时节点 */
    this->tick();
    ssize_t res = -1;
    if (!heap_.empty())
    {
        /* 返回时间剩余最小的节点的剩余时间(毫秒)，设置为epoll_wait等待事件 */
        res = std::chrono::duration_cast<std::chrono::milliseconds>(heap_.front().expires - std::chrono::high_resolution_clock::now()).count();
        if (res < 0)
        {
            res = 0;
        }
    }
    return res;
}
