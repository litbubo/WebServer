#include <epoller.h>

/*
 * 初始化 epollfd 和 events
 */
Epoller::Epoller(int maxEvent) : epollFd_(epoll_create(512)), events_(maxEvent)
{
    assert(epollFd_ >= 0 && events_.size() > 0);
}

/*
 * 析构时关闭文件描述符
 */
Epoller::~Epoller()
{
    close(epollFd_);
}

/*
 * 向 epoll 树上添加一个 fd 和事件
 */
bool Epoller::addFd(int fd, uint32_t events)
{
    assert(fd >= 0);
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == 0)
        return true;
    else
        return false;
}

/*
 * 向 epoll 树上修改一个 fd 和事件
 */
bool Epoller::modFd(int fd, uint32_t events)
{
    assert(fd >= 0);
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0)
        return true;
    else
        return false;
}

/*
 * 删除 epoll 树上的 fd 和事件
 */
bool Epoller::delFd(int fd)
{
    assert(fd >= 0);
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev) == 0)
        return true;
    else
        return false;
}

/*
 * 封装wait
 */
int Epoller::wait(int timeoutMs)
{
    return epoll_wait(epollFd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
}

int Epoller::getEventFd(size_t i) const
{
    assert(i < events_.size() && i >= 0);
    return events_.at(i).data.fd;
}

uint32_t Epoller::getEvents(size_t i) const
{
    assert(i < events_.size() && i >= 0);
    return events_.at(i).events;
}
