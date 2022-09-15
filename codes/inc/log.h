#pragma once

#include <thread>
#include <string>
#include <mutex>

#include <buffer.h>
#include <blockqueue.hpp>

class Log
{

public:
    enum LOG_LEVEL
    {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    void init(LOG_LEVEL level = INFO,
              const char *path = "./log",
              const char *suffix = ".log",
              int maxQueueSize = 1024);
    static Log *instance();

    LOG_LEVEL getLevel();

    void setLevel(LOG_LEVEL level);

    void flush();

    void write(LOG_LEVEL level, const char *format, ...);

    bool isOpen();

private:
    Log();

    ~Log();

    void appendLogLevelTitle(LOG_LEVEL level);

    void asyncWrite();

    static const size_t LOG_NAME_LEN = 256;
    static const int LOG_MAX_LEN = 50000;

    const char *path_;
    const char *suffix_;

    int lineCount_;
    int today_;
    bool isOpen_;
    LOG_LEVEL level_;
    bool isAsync_;

    FILE *fp_;
    Buffer buffer_;
    
    std::unique_ptr<BlockQueue<std::string>> queue_;
    std::unique_ptr<std::thread> thread_;
    std::mutex mtx_;
};
