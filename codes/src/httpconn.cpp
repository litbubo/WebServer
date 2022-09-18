#include <httpconn.h>

const char *HttpConn::srcDir_;
std::atomic<int> HttpConn::userCount_;

/*
 * 构造函数。
 */
HttpConn::HttpConn() : fd_(-1), isClose_(false), addr_{0}
{
}

/*
 * 析构时关闭连接
 */
HttpConn::~HttpConn()
{
    this->close();
}

/*
 * 连接初始化
 */
void HttpConn::init(int sockfd, const sockaddr_in &addr)
{
    assert(sockfd > 0);
    userCount_++;
    addr_ = addr;
    fd_ = sockfd;
    writeBuff_.retrieveAll();
    readBuff_.retrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount: %d", sockfd, getIP(), getPort(), (int)userCount_);
}

/*
 * http连接数据读取到readbuff，返回len和errno
 */
ssize_t HttpConn::read(int *retErrno)
{
    ssize_t len;
    while (1)
    {
        /* 因为是ET模式，当读取不到数据时会返回-1，errno = EAGAIN */
        len = readBuff_.readFd(fd_, retErrno);
        if (len <= 0)
        {
            *retErrno = errno;
            break;
        }
    }
    return len;
}

/*
 * http连接从writebuff发送数据，返回len和errno
 */
ssize_t HttpConn::write(int *retErrno)
{
    ssize_t len;
    while (1)
    {
        /* ET模式，当发送缓冲区满无法发送时，会返回-1，errno = EAGAIN */
        len = writev(fd_, iov, iovCount_);
        if (len <= 0)
        {
            *retErrno = errno;
            break;
        }
        /* 长度均为0，发送完成 */
        if (iov[0].iov_len + iov[1].iov_len == 0)
        {
            break;
        }
        /* 如果发送的长度大于iov[0]的长度，则iov[0]发送完毕，iov[1]只发送完部分数据 */
        else if (iov[0].iov_len < static_cast<size_t>(len))
        {
            /* 根据发送数据的量，进行指针偏移 */
            iov[1].iov_base = (uint8_t *)iov[1].iov_base + (len - iov[0].iov_len);
            iov[1].iov_len = iov[1].iov_len - (len - iov[0].iov_len);
            /* 回收writebuff空间 */
            if (iov[0].iov_len)
            {
                writeBuff_.retrieveAll();
                iov[0].iov_len = 0;
            }
        }
        else
        {
            /* 发送数据的量小于iov[0]的长度，只将iov[0]进行偏移即可 */
            iov[0].iov_base = (uint8_t *)iov[0].iov_base + len;
            iov[0].iov_len = iov[0].iov_len - len;
            writeBuff_.retrieve(len);
        }
    }
    return len;
}

/*
 * 关闭http连接
 */
void HttpConn::close()
{
    response_.unmapFile();
    if (isClose_ == false)
    {
        isClose_ = true;
        userCount_--;
        ::close(fd_);
    }
}

/*
 * 获取http连接中的fd
 */
int HttpConn::getFd() const
{
    return fd_;
}

/*
 * 获取http中的端口
 */
int HttpConn::getPort() const
{
    return addr_.sin_port;
}

/*
 * 获取http连接中的IP
 */
const char *HttpConn::getIP() const
{
    return inet_ntoa(addr_.sin_addr);
}

/*
 * 获取http连接中的sockaddr_in
 */
sockaddr_in HttpConn::getAddr() const
{
    return addr_;
}

/*
 * http请求数据解析及响应报文生成，成功返回true，
 * 失败返回false，上层会重新注册连接为EPOLLIN，等待请求报文读取
 */
bool HttpConn::process()
{
    /* 如果上一次请求解析已经完成，则重新初始化请求解析类，清空之前数据 */
    if (request_.state() == HttpRequest::FINISH)
    {
        request_.init();
    }
    /* 如果读buff数据为空，返回false，上层程序会重新将连接注册为EPOLLIN */
    if (readBuff_.readableBytes() <= 0)
    {
        return false;
    }

    /* 请求解析返回GET_REQUEST表示解析完成，可以正常生成响应报文 */
    /* 请求解析返回NO_REQUEST表示解析未完成，可能是报文没有完全收到，返回false */
    HttpRequest::HTTP_CODE processStatus = request_.parse(readBuff_);
    if (processStatus == HttpRequest::GET_REQUEST)
    {
        LOG_DEBUG("request path %s", request_.path().data());
        /* 传递资源目录，请求路径，长连接及状态码200 */
        response_.init(srcDir_, request_.path(), request_.iskeepAlive(), 200);
    }
    else if (processStatus == HttpRequest::NO_REQUEST)
    {
        return false;
    }
    else
    {
        response_.init(srcDir_, request_.path(), false, 400);
    }
    /* 向writebuff中写入http响应报文，等待发送 */
    response_.makeResponse(writeBuff_);

    /* iov[0]指向响应头 */
    iov[0].iov_base = const_cast<char *>(writeBuff_.peek());
    iov[0].iov_len = writeBuff_.readableBytes();
    iovCount_ = 1;
    /* iov[1]指向相应报文的payload */
    if (response_.fileLen() > 0 && response_.file())
    {
        iov[1].iov_base = response_.file();
        iov[1].iov_len = response_.fileLen();
        iovCount_ = 2;
    }
    LOG_DEBUG("filesize == %d, iovcnt == %d, total == %d", response_.fileLen(), iovCount_, this->toWriteBytes());

    return true;
}

/*
 * http连接将要发送的字节数
 */
size_t HttpConn::toWriteBytes()
{
    return iov[0].iov_len + iov[1].iov_len;
}

/*
 * 获取http是否为长连接
 */
bool HttpConn::isKeepAlive() const
{
    return request_.iskeepAlive();
}