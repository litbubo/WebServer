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
    if (thread_ != nullptr && thread_->joinable())
    {
        while (queue_->empty() != true)
        {
            queue_->flush();
        }
        queue_->close();
        thread_->join();
    }
    if (fp_)
    {
        this->flush();
        fclose(fp_);
    }
}

void Log::init(LOG_LEVEL level,
               const char *path,
               const char *suffix,
               int maxQueueSize)
{
    isOpen_ = true;
    level_ = level;
    if (maxQueueSize > 0)
    {
        isAsync_ = true;
        if (queue_ == nullptr && thread_ == nullptr)
        {
            std::unique_ptr<BlockQueue<std::string>> que_ptr(new BlockQueue<std::string>(maxQueueSize));
            queue_ = std::move(que_ptr);
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
    time_t timer = time(nullptr);
    auto systime = localtime(&timer);
    path_ = path;
    suffix_ = suffix;

    char fileName[LOG_NAME_LEN];
    memset(fileName, 0, sizeof(fileName));
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
             path_, systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, suffix_);

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
    struct timeval now = {0};
    gettimeofday(&now, nullptr);
    time_t tsec = now.tv_sec;
    auto systime = localtime(&tsec);

    if (today_ != systime->tm_mday || (lineCount_ && (lineCount_ % LOG_MAX_LEN == 0)))
    {
        std::lock_guard<std::mutex> locker(mtx_);

        char newFile[LOG_NAME_LEN] = {0};
        if (today_ != systime->tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
                     path_, systime->tm_year, systime->tm_mon, systime->tm_mday, suffix_);
            lineCount_ = 0;
            today_ = systime->tm_mday;
        }
        else
        {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d_%d%s",
                     path_, systime->tm_year, systime->tm_mon, systime->tm_mday, lineCount_ / LOG_MAX_LEN, suffix_);
        }

        this->flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        std::lock_guard<std::mutex> locker(mtx_);
        lineCount_++;

        char temp[1024] = {0};

        snprintf(temp, sizeof(temp), "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                 systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday,
                 systime->tm_hour, systime->tm_min, systime->tm_sec, now.tv_usec);
        buffer_.append(temp, strlen(temp));
        memset(temp, 0, sizeof(temp));

        appendLogLevelTitle(level);
        
        va_list vaList;
        va_start(vaList, format);
        vsnprintf(temp, sizeof(temp), format, vaList);
        va_end(vaList);

        buffer_.append(temp, strlen(temp));
        buffer_.append("\n\0", 2);

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
        buffer_.append("[DEBUG]: ", 10);
        break;
    case Log::INFO:
        buffer_.append("[INFO]:  ", 10);
        break;
    case Log::WARN:
        buffer_.append("[WARN]:  ", 10);
        break;
    case Log::ERROR:
        buffer_.append("[ERROR]: ", 10);
        break;
    default:
        buffer_.append("[INFO]:  ", 10);
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