#include <webserver.h>

Webserver::Webserver(int port, int trigMode, int timeoutMs, bool optLinger,
                     int sqlPort, const char *sqlUser, const char *sqlPwd, const char *dbName,
                     int connPoolNum, int threadNum, bool openLog, Log::LOG_LEVEL logLevel, int logQueSize)
    : port_(port), timeoutMs_(timeoutMs), openLinger_(optLinger), isClose_(false),
      timer_(new HeapTimer()), threadPool_(new ThreadPool(threadNum)), epoller_(new Epoller)
{
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);

    HttpConn::userCount_ = 0;
    HttpConn::srcDir_ = srcDir_;
    SqlConnPool::instance()->init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    this->initEventMode(trigMode);

    if (this->initSocket() == false)
    {
        isClose_ = true;
    }

    if (openLog)
    {
        Log::instance()->init(logLevel, logQueSize);
        if (isClose_)
        {
            LOG_ERROR("=================Server init error!===================");
        }
        else
        {
            LOG_INFO("=================Server init success=================");
            LOG_INFO("Port: %d,OpenLinger: %s", port, optLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, Conn Mode: %s", (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("Log level: %d", logLevel);
            LOG_INFO("srcDir: %s", srcDir_);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

Webserver::~Webserver()
{
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::instance()->closePool();
}

void Webserver::start()
{
    int timeMs = -1;
    if (isClose_ == false)
    {
        LOG_INFO("=================Server start!===================");
    }

    while (isClose_ == false)
    {
        if (timeoutMs_ > 0)
        {
            timeMs = timer_->getNextTick();
        }
        int count = epoller_->wait(timeMs);
        for (int i = 0; i < count; i++)
        {
            int fd = epoller_->getEventFd(i);
            uint32_t events = epoller_->getEvents(i);

            if (fd == listenFd_)
            {
                this->dealListen();
            }
            else if (events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                assert(users_.count(fd) > 0);
                this->closeConn(&users_[fd]);
            }
            else if (events & EPOLLIN)
            {
                this->dealRead(&users_[fd]);
            }
            else if (events & EPOLLOUT)
            {
                this->dealWrite(&users_[fd]);
            }
            else
            {
                LOG_ERROR("Unexpect epoll events");
            }
        }
    }
}

int Webserver::setFdNonBlock(int fd)
{
    assert(fd >= 0);
    int oldFl = fcntl(fd, F_GETFL);
    int newFl = oldFl | O_NONBLOCK;
    fcntl(fd, F_SETFL, newFl);

    return oldFl;
}

bool Webserver::initSocket()
{
    int ret = 0;
    struct sockaddr_in addr;

    if (port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("port %d is not access!!", port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    struct linger optLinger = {0};
    if (openLinger_)
    {
        optLinger.l_linger = 1;
        optLinger.l_onoff = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        LOG_ERROR("listenfd socket init failed !!");
        return false;
    }
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("setsockopt SO_LINGER failed");
        return false;
    }
    int optVal = 1;
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (void *)&optVal, sizeof(optVal));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("setsockopt SO_REUSEADDR failed");
        return false;
    }

    ret = bind(listenFd_, (sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("bind failed");
        return false;
    }

    ret = listen(listenFd_, 10);
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("listen failed");
        return false;
    }

    bool res = epoller_->addFd(listenFd_, listenEvent_ | EPOLLIN);
    if (res == false)
    {
        close(listenFd_);
        LOG_ERROR("add epoll fd failed");
        return false;
    }

    this->setFdNonBlock(listenFd_);
    LOG_INFO("lisnten fd init success !! port : %d", port_);
    return true;
}

void Webserver::initEventMode(int trigMode)
{
    listenEvent_ = EPOLLHUP;
    /*EPOLLONESHOT，为了保证当前连接在同一时刻只被一个线程处理*/
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    /*根据触发模式设置对应选项*/
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        /*连接模式为ET，边沿触发*/
        connEvent_ |= EPOLLET;
        break;
    case 2:
        /*监听模式为ET，边沿触发*/
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        /*处理模式皆为边沿触发*/
        connEvent_ |= EPOLLET;
        listenEvent_ |= EPOLLET;
        break;
    default:
        connEvent_ |= EPOLLET;
        listenEvent_ |= EPOLLET;
        break;
    }
    /*若连接事件为ET模式，那么设置Http类中的标记isET为true*/
    HttpConn::isET_ = (connEvent_ & EPOLLET);
}

void Webserver::addClient(int fd, sockaddr_in addr)
{
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if (timeoutMs_ > 0)
    {
        timer_->add(fd, timeoutMs_, std::bind(&Webserver::closeConn, this, &users_[fd]));
    }

    epoller_->addFd(fd, EPOLLIN | connEvent_);
    this->setFdNonBlock(fd);

    LOG_INFO("add client fd : %d, ip : %s, port : %d", users_[fd].getFd(), users_[fd].getIP(), users_[fd].getPort());
}

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

void Webserver::dealWrite(HttpConn *client)
{
    assert(client);
    this->extentTime(client);
    threadPool_->addTask(std::bind(&Webserver::onWrite, this, client));
}

void Webserver::dealRead(HttpConn *client)
{
    assert(client);
    this->extentTime(client);
    threadPool_->addTask(std::bind(&Webserver::onRead, this, client));
}

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

void Webserver::extentTime(HttpConn *client)
{
    assert(client);
    if (timeoutMs_ > 0)
    {
        timer_->adjust(client->getFd(), timeoutMs_);
    }
}

void Webserver::closeConn(HttpConn *client)
{
    assert(client);
    LOG_INFO("client[%d] quit", client->getFd());
    epoller_->delFd(client->getFd());
    client->close();
}

void Webserver::onRead(HttpConn *client)
{
    assert(client);
    int readErrno = 0;

    ssize_t ret = client->read(&readErrno);
    if (ret < 0 && readErrno != EAGAIN)
    {
        this->closeConn(client);
        return;
    }
    this->onProcess(client);
}

void Webserver::onWrite(HttpConn *client)
{
    assert(client);
    int writeErrno = 0;

    ssize_t ret = client->write(&writeErrno);
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
            epoller_->addFd(client->getFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    this->closeConn(client);
}

void Webserver::onProcess(HttpConn *client)
{
    if (client->process())
    {
        epoller_->modFd(client->getFd(), connEvent_ | EPOLLOUT);
    }
    else
    {
        epoller_->modFd(client->getFd(), connEvent_ | EPOLLIN);
    }
}
