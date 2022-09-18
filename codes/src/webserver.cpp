#include <webserver.h>

/*
 * 构造函数，初始化服务器各种配置
 */
Webserver::Webserver(int port, int timeoutMs,
                     int sqlPort, const char *sqlUser, const char *sqlPwd, const char *dbName,
                     int connPoolNum, int threadNum, bool openLog, Log::LOG_LEVEL logLevel, int logQueSize)
    : port_(port), timeoutMs_(timeoutMs), isClose_(false),
      timer_(new HeapTimer()), threadPool_(new ThreadPool(threadNum)), epoller_(new Epoller)
{
    /* 获取程序根目录 */
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    /* 追加资源目录 */
    strncat(srcDir_, "/resources", 16);

    HttpConn::userCount_ = 0;
    HttpConn::srcDir_ = srcDir_;
    /*初始化数据库连接池*/
    SqlConnPool::instance()->init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    /* 初始化事件模式 ET */
    this->initEventMode();

    /* 初始化listenfd */
    if (this->initSocket() == false)
    {
        isClose_ = true;
    }

    /* 初始化日志 */
    if (openLog)
    {
        /* 单例初始化 */
        Log::instance()->init(logLevel, logQueSize);
        if (isClose_)
        {
            LOG_ERROR("=================Server init error!===================");
        }
        else
        {
            LOG_INFO("=================Server init success=================");
            LOG_INFO("Port: %d", port);
            LOG_INFO("Listen Mode: EPOLLET, Conn Mode: EPOLLET");
            LOG_INFO("Log level: %d", logLevel);
            LOG_INFO("srcDir: %s", srcDir_);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

/*
 * 析构函数 关闭文件描述符 释放堆内存
 */
Webserver::~Webserver()
{
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::instance()->closePool();
}

/*
 * 服务器开始运行
 */
void Webserver::start()
{
    int timeMs = -1;
    if (isClose_ == false)
    {
        LOG_INFO("=================Server start!===================");
    }

    /* 循环等待epoll时间 */
    while (isClose_ == false)
    {
        /* 从定时器取出最进要过期的文件描述符时间 */
        if (timeoutMs_ > 0)
        {
            /* 获取最近超时时间，同时删除已经超时的连接 */
            timeMs = timer_->getNextTick();
        }
        /* 等待产生事件返回 */
        int count = epoller_->wait(timeMs);
        for (int i = 0; i < count; i++)
        {
            /* 获取对应的文件描述符和时间 */
            int fd = epoller_->getEventFd(i);
            uint32_t events = epoller_->getEvents(i);

            if (fd == listenFd_)
            {
                /* 如果是监听描述符，处理新连接 */
                this->dealListen();
            }
            else if (events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                /* 如果是EPOLLHUP | EPOLLRDHUP | EPOLLERR其中之一，直接关闭连接 */
                LOG_DEBUG("EPOLLHUP | EPOLLRDHUP | EPOLLERR %d", fd);
                assert(users_.count(fd) > 0);
                this->closeConn(&users_[fd]);
            }
            else if (events & EPOLLIN)
            {
                /* 如果读事件到达，处理读 */
                LOG_DEBUG("EPOLLIN %d", fd);
                this->dealRead(&users_[fd]);
            }
            else if (events & EPOLLOUT)
            {
                /* 如果写事件到达，处理读 */
                LOG_DEBUG("EPOLLOUT %d", fd);
                this->dealWrite(&users_[fd]);
            }
            else
            {
                /* 其他情况为意外情况 打印log */
                LOG_ERROR("Unexpect epoll events");
            }
        }
    }
}

/*
 * 设置文件描述符非阻塞，成功返回旧的fcntl属性
 */
int Webserver::setFdNonBlock(int fd)
{
    assert(fd >= 0);
    int oldFl = fcntl(fd, F_GETFL);
    int newFl = oldFl | O_NONBLOCK;
    fcntl(fd, F_SETFL, newFl);
    LOG_DEBUG("fd : %d  setFdNonBlock", fd);
    return oldFl;
}

/*
 * 初始化套接字，成功返回true
 */
bool Webserver::initSocket()
{
    int ret = 0;
    struct sockaddr_in addr;

    if (port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("port %d is not access!!", port_);
        return false;
    }
    /* 绑定IP和Port */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    struct linger optLinger = {0};
    optLinger.l_linger = 1;
    optLinger.l_onoff = 1;
    /* 创建流式套接字 */
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        LOG_ERROR("listenfd socket init failed !!");
        return false;
    }
    /* 直到所有数据发送完成或超时再关闭 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("setsockopt SO_LINGER failed");
        return false;
    }
    /* 设置端口复用，无需等待2MSL，但是只有最后一个绑定该端口的才可以接收数据 */
    int optVal = 1;
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (void *)&optVal, sizeof(optVal));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("setsockopt SO_REUSEADDR failed");
        return false;
    }
    /* 绑定IP和端口 */
    ret = bind(listenFd_, (sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("bind failed");
        return false;
    }
    /* 监听 */
    ret = listen(listenFd_, 6);
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("listen failed");
        return false;
    }
    /* 将监听描述符加入epoll等待事件 */
    bool res = epoller_->addFd(listenFd_, listenEvent_ | EPOLLIN);
    if (res == false)
    {
        close(listenFd_);
        LOG_ERROR("add epoll fd failed");
        return false;
    }
    /* 设置文件描述符非阻塞 */
    this->setFdNonBlock(listenFd_);
    LOG_INFO("lisnten fd init success !! port : %d", port_);
    return true;
}

/*
 * 设置epoll ET边沿触发模式
 */
void Webserver::initEventMode()
{
    listenEvent_ = EPOLLHUP;
    /*EPOLLONESHOT，为了保证当前连接在同一时刻只被一个线程处理*/
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;

    connEvent_ |= EPOLLET;
    listenEvent_ |= EPOLLET;
}

/*
 * 向std::unordered_map<int, HttpConn> users_添加一个新连接成员
 */
void Webserver::addClient(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if (timeoutMs_ > 0)
    {
        timer_->add(fd, timeoutMs_, std::bind(&Webserver::closeConn, this, &users_[fd]));
    }
    /* 将新文件描述符添加到epoll树上 */
    epoller_->addFd(fd, EPOLLIN | connEvent_);
    this->setFdNonBlock(fd);

    LOG_DEBUG("add client fd : %d, ip : %s, port : %d", users_[fd].getFd(), users_[fd].getIP(), users_[fd].getPort());
}

/*
 * 处理新连接，加入std::unordered_map<int, HttpConn> users_;
 */
void Webserver::dealListen()
{
    struct sockaddr_in addr;
    socklen_t sockLen = sizeof(addr);
    do
    {
        int fd = accept(listenFd_, (sockaddr *)&addr, &sockLen);
        if (fd < 0)
        {
            break;
        }
        else if (HttpConn::userCount_ >= static_cast<int>(MAX_FD_CNT_))
        {
            this->sendError(fd, "Server busy");
            LOG_WARN("Clients is full!");
            return;
        }
        this->addClient(fd, addr);
    } while (listenEvent_ & EPOLLET);
}

/*
 * 处理写事件
 */
void Webserver::dealWrite(HttpConn *client)
{
    assert(client);
    this->extentTime(client);
    threadPool_->addTask(std::bind(&Webserver::onWrite, this, client));
}

/*
 * 处理读事件
 */
void Webserver::dealRead(HttpConn *client)
{
    assert(client);
    this->extentTime(client);
    threadPool_->addTask(std::bind(&Webserver::onRead, this, client));
}

/*
 * 发送错误信息
 */
void Webserver::sendError(int fd, const char *info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0)
    {
        LOG_WARN("send error to client[%d]", fd);
    }
    close(fd);
}

/*
 * 对应连接有新动作，更新连接超时时间
 */
void Webserver::extentTime(HttpConn *client)
{
    assert(client);
    if (timeoutMs_ > 0)
    {
        timer_->adjust(client->getFd(), timeoutMs_);
    }
}

/*
 * 关闭一个http连接
 */
void Webserver::closeConn(HttpConn *client)
{
    assert(client);
    LOG_INFO("client[%d] quit", client->getFd());
    /* 关闭前先从epoll树上将文件描述符摘掉 */
    epoller_->delFd(client->getFd());
    client->close();
}

/*
 * 读事件线程池回调，从http连接中取出数据
 */
void Webserver::onRead(HttpConn *client)
{
    assert(client);
    int readErrno = 0;

    ssize_t ret = client->read(&readErrno);
    /* 非阻塞模式下会返回EAGAIN，如果不是EAGAIN说明有问题 */
    if (ret <= 0 && readErrno != EAGAIN)
    {
        this->closeConn(client);
        return;
    }
    this->onProcess(client);
}

/*
 * 写事件线程池回调函数，向http发送数据
 */
void Webserver::onWrite(HttpConn *client)
{
    assert(client);
    int writeErrno = 0;

    ssize_t ret = client->write(&writeErrno);
    /* 如果数据已经写完了，但链接是长连接，则重新调用onProcess，会把文件描述符重新设置为EPOLLIN */
    if (client->toWriteBytes() == 0)
    {
        if (client->isKeepAlive())
        {
            this->onProcess(client);
            return;
        }
    }
    else if (ret < 0)
    {
        if (writeErrno == EAGAIN)
        {
            /* 说明写缓冲区满了，需要重新设置读事件，放回epoll树 */
            epoller_->modFd(client->getFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    LOG_DEBUG("ret == %d, error == %d", ret, writeErrno);
    /* 其他意外情况，关闭连接 */
    this->closeConn(client);
}

/* 解析http报文 */
void Webserver::onProcess(HttpConn *client)
{
    if (client->process())
    {
        /* 成功处理http请求，则设置文件描述符写事件，准备写响应 */
        epoller_->modFd(client->getFd(), connEvent_ | EPOLLOUT);
    }
    else
    {
        /* 解析http请求失败，重新注册文件描述符为读，继续读取socket */
        epoller_->modFd(client->getFd(), connEvent_ | EPOLLIN);
    }
}
