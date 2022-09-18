#include <log.h>

#include <cstring>
#include <cassert>
#include <cstdarg>
#include <sys/time.h>
#include <sys/stat.h>

/*
 * 私有化构造函数，单例模式
 */
Log::Log() : lineCount_(0),
             today_(0),
             isOpen_(0),
             level_(DEBUG),
             isAsync_(false),
             fp_(nullptr),
             queue_(nullptr),
             thread_(nullptr)
{
}

/*
 * 析构时将队列中所有的日志取出写入日志文件
 */
Log::~Log()
{
    /* 写线程不为空且写线程可回收 */
    if (thread_ != nullptr && thread_->joinable())
    {
        /* 队列不为空则一直唤醒消费者取出日志 */
        while (queue_->empty() != true)
        {
            queue_->flush();
        }
        /* 关闭阻塞队列和线程 */
        queue_->close();
        thread_->join();
    }
    if (fp_)
    {
        /* 刷新文件并且关闭文件 */
        this->flush();
        fclose(fp_);
    }
}

/* 
 * 初始化Log配置
 */
void Log::init(LOG_LEVEL level,
               int maxQueueSize,
               const char *path,
               const char *suffix)
{
    isOpen_ = true;
    level_ = level;
    /* 如果消息对列容量不为 0，则为异步日志 */
    if (maxQueueSize > 0)
    {
        isAsync_ = true;
        if (queue_ == nullptr && thread_ == nullptr)
        {
            /* 初始化消息队列和线程智能指针 */
            std::unique_ptr<BlockQueue<std::string>> que_ptr(new BlockQueue<std::string>(maxQueueSize));
            queue_ = std::move(que_ptr);
            /* 初始化同时设置线程回调函数为异步写日志 */
            std::unique_ptr<std::thread> thread_ptr(new std::thread([]()
                                                                    { Log::instance()->asyncWrite(); }));
            thread_ = std::move(thread_ptr);
        }
        else
        {
            isAsync_ = false;
        }
    }
    lineCount_ = 0;
    /* 获取系统本地时间 */
    time_t timer = time(nullptr);
    auto systime = localtime(&timer);
    path_ = path;
    suffix_ = suffix;

    /* 填充文件名，eg：2022_09_17.log */
    char fileName[LOG_NAME_LEN];
    memset(fileName, 0, sizeof(fileName));
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
             path_, systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, suffix_);
    /* 设置今天日期 */
    today_ = systime->tm_mday;

    {
        std::lock_guard<std::mutex> locker(mtx_);
        if (fp_)
        {
            this->flush();
            fclose(fp_);
            assert(fp_ == nullptr);
        }
        fp_ = fopen(fileName, "a");
        if (fp_ == nullptr)
        {
            /* 如果文件打开失败，可能是没有log目录。 */
            mkdir(path, 0777);
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }
}

/*
 * 返回日志单例
 */
Log *Log::instance()
{
    /* C++11后局部静态变量无需枷锁 */
    static Log log;
    return &log;
}

/*
 * 获取日志等级
 */
Log::LOG_LEVEL Log::getLevel()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

/*
 * 设置日志等级
 */
void Log::setLevel(Log::LOG_LEVEL level)
{
    std::lock_guard<std::mutex> locker(mtx_);
    level_ = level;
}

/*
 * 获取日志启动状态
 */
bool Log::isOpen()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return isOpen_;
}

/*
 * 刷新文件流
 */
void Log::flush()
{
    fflush(fp_);
}

/*
 *
 */
void Log::write(LOG_LEVEL level, const char *format, ...)
{
    /* 获取系统当前本地时间 */
    struct timeval now = {0};
    gettimeofday(&now, nullptr);
    time_t tsec = now.tv_sec;
    auto systime = localtime(&tsec);

    /* 如果时间不是今天或者日志行数超过5w行，就该换一个文件写了 */
    if (today_ != systime->tm_mday || (lineCount_ && (lineCount_ % LOG_MAX_LEN == 0)))
    {
        std::lock_guard<std::mutex> locker(mtx_);
        /* 生成新的文件名eg：2022_09_17_1.log */
        char newFile[LOG_NAME_LEN] = {0};
        /* 如果是日期变了 */
        if (today_ != systime->tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
                     path_, systime->tm_year, systime->tm_mon, systime->tm_mday, suffix_);
            lineCount_ = 0;
            today_ = systime->tm_mday;
        }
        /* 如果是行数超过5w行 */
        else
        {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d_%d%s",
                     path_, systime->tm_year, systime->tm_mon, systime->tm_mday, lineCount_ / LOG_MAX_LEN, suffix_);
        }

        /* 刷新原文件并创建新文件 */
        this->flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    /* 正常向文件中写日志 */
    {
        std::lock_guard<std::mutex> locker(mtx_);
        lineCount_++;

        char temp[1024] = {0};

        snprintf(temp, sizeof(temp), "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                 systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday,
                 systime->tm_hour, systime->tm_min, systime->tm_sec, now.tv_usec);
        buffer_.append(temp, strlen(temp));
        memset(temp, 0, sizeof(temp));

        /* 添加日志头 */
        appendLogLevelTitle(level);

        va_list vaList;
        va_start(vaList, format);
        vsnprintf(temp, sizeof(temp), format, vaList);
        va_end(vaList);

        buffer_.append(temp, strlen(temp));
        buffer_.append("\n\0", 2);

        /* 异步写，放入消息队列中 */
        if (isAsync_ == true && queue_ != nullptr && queue_->full() != true)
        {
            queue_->push_back(buffer_.retrieveAlltoString());
        }
        else
        {
            fputs(buffer_.peek(), fp_);
        }
        buffer_.retrieveAll();
    }
}

/*
 * 添加日志头
 */
void Log::appendLogLevelTitle(LOG_LEVEL level)
{
    switch (level)
    {
    case Log::DEBUG:
        buffer_.append("[DEBUG]: ", 9);
        break;
    case Log::INFO:
        buffer_.append("[INFO]:  ", 9);
        break;
    case Log::WARN:
        buffer_.append("[WARN]:  ", 9);
        break;
    case Log::ERROR:
        buffer_.append("[ERROR]: ", 9);
        break;
    default:
        buffer_.append("[INFO]:  ", 9);
        break;
    }
}

/*
 * 异步写，从阻塞队列取出日志内容写入文件
 */
void Log::asyncWrite()
{
    std::string str = std::string("");
    while (queue_->pop(str))
    {
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(str.data(), fp_);
    }
}