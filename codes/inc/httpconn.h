#pragma once

#include <errno.h>
#include <cstdlib>
#include <cassert>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include <log.h>
#include <buffer.h>
#include <sqlconnRAII.hpp>
#include <httpresponse.h>
#include <httprequest.h>

class HttpConn
{
public:
    HttpConn();
    ~HttpConn();

    void init(int sockfd, const sockaddr_in &addr);
    ssize_t read(int *retErrno);
    ssize_t write(int *retErrno);
    void close();
    int getFd() const;
    int getPort() const;
    const char *getIP() const;
    sockaddr_in getAddr() const;
    bool process();
    size_t toWriteBytes();
    bool isKeepAlive() const;

    static bool isET_;
    static const char *srcDir_;
    static std::atomic<int> userCount_;

private:
    int fd_;
    bool isClose_;
    int iovCount_;
    struct sockaddr_in addr_;
    struct iovec iov[2];

    Buffer readBuff_;
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;
};