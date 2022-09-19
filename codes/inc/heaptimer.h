#pragma once

#include <queue>
#include <chrono>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <ctime>
#include <cassert>
#include <arpa/inet.h>

#include <log.h>

/*
 * 定时器时间节点
 * 包含事件戳，节点对应的文件描述符，超时触发的回调等
 */
struct TimerNode
{
    int fd;                                                 /* 节点对应的文件描述符 */
    std::chrono::high_resolution_clock::time_point expires; /* 超时时间，高精度时间戳 */
    std::function<void()> timeoutCb;                        /* 超时回调函数，超时后删除节点，关闭连接，调用WebServer::CloseConn */
    /* 重载的比较运算符，使得节点之间可以互相比较 */
    bool operator<(const TimerNode &t)
    {
        return expires < t.expires;
    }
};

/* 小根堆 */
class HeapTimer
{
public:
    HeapTimer();

    ~HeapTimer();

    void adjust(int fd, int newExpires);

    void add(int fd, int timeout, const std::function<void()> &cb);

    void clear();

    void tick();

    void pop();

    int getNextTick();

private:
    void del(size_t i);

    void siftParent(size_t i);

    bool siftChild(size_t index, size_t n);

    void swapNode(size_t i, size_t j);

    std::vector<TimerNode> heap_;         /* 使用vector对小根堆进行存储，因为小根堆最适合用一维数组 */
    std::unordered_map<int, size_t> ref_; /* 使用hash，O(1)，查找文件描述符对应的节点在数组中的下标位置 */
};
