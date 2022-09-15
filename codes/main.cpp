#include <buffer.h>
#include <blockqueue.hpp>
#include <epoller.h>
#include <sqlconnpool.h>
#include <sqlconnRAII.hpp>
#include <threadpool.h>
#include <iostream>
#include <log.h>

int main()
{
    Buffer buffer;
    buffer.ensurewritable(10);
    BlockQueue<int> q(1024);
    q.flush();
    q.empty();
    Epoller ep(5);
    ep.addFd(5, 4);

    exit(0);
}