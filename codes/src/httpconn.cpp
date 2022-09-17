#include <httpconn.h>

bool HttpConn::isET_;
const char *HttpConn::srcDir_;
std::atomic<int> HttpConn::userCount_;

HttpConn::HttpConn() : fd_(-1), isClose_(false), addr_{0}
{
}
HttpConn::~HttpConn()
{
    this->close();
}

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

ssize_t HttpConn::read(int *retErrno)
{
    ssize_t totalLen = 0;
    do
    {
        const ssize_t len = readBuff_.readFd(fd_, retErrno);
        if (len <= 0)
        {
            *retErrno = errno;
            break;
        }
        totalLen += len;
    } while (isET_);
    return totalLen;
}

ssize_t HttpConn::write(int *retErrno)
{
    ssize_t totalLen = 0;
    do
    {
        const ssize_t len = writev(fd_, iov, iovCount_);
        if (len <= 0)
        {
            *retErrno = errno;
            break;
        }
        totalLen += len;

        if (iov[0].iov_len + iov[1].iov_len == 0)
        {
            break;
        }
        else if (iov[0].iov_len < static_cast<size_t>(len))
        {
            iov[1].iov_base = (uint8_t *)iov[1].iov_base + (len - iov[0].iov_len);
            iov[1].iov_len = iov[1].iov_len - (len - iov[0].iov_len);

            if (iov[0].iov_len)
            {
                writeBuff_.retrieveAll();
                iov[0].iov_len = 0;
            }
        }
        else
        {
            iov[0].iov_base = (uint8_t *)iov[0].iov_base + len;
            iov[0].iov_len = iov[0].iov_len - len;
            writeBuff_.retrieve(len);
        }

    } while (isET_);
    return totalLen;
}

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

int HttpConn::getFd() const
{
    return fd_;
}

int HttpConn::getPort() const
{
    return addr_.sin_port;
}

const char *HttpConn::getIP() const
{
    return inet_ntoa(addr_.sin_addr);
}

sockaddr_in HttpConn::getAddr() const
{
    return addr_;
}

bool HttpConn::process()
{
    return true;
}

size_t HttpConn::toWriteBytes()
{
    return iov[0].iov_len + iov[1].iov_len;
}

bool HttpConn::isKeepAlive() const
{
    return request_.iskeepAlive();
}