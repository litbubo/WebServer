#include <buffer.h>
#include <blockqueue.hpp>
#include <epoller.h>
#include <sqlconnpool.h>
#include <sqlconnRAII.hpp>
#include <threadpool.hpp>
#include <iostream>
#include <log.h>

int main()
{
    Log *log = Log::instance();
    log->init(Log::DEBUG);
    int i = 0;
    while (i < 100000)
    {
        if (i % 2 == 0)
        {
            LOG_ERROR("test---%d", i);
        }
            
        else
        {
            LOG_INFO("test---%d", i);
        }
            

        i++;
    }

    exit(0);
}