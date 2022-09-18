#pragma once

#include <unordered_map>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <log.h>
#include <epoller.h>
#include <httpconn.h>
#include <heaptimer.h>
#include <threadpool.hpp>
#include <sqlconnRAII.hpp>
#include <sqlconnpool.h>

class Webserver
{
public:
    Webserver(int port, int timeoutMs,
              int sqlPort, const char *sqlUser, const char *sqlPwd, const char *dbName,
              int connPoolNum, int threadNum, bool openLog, Log::LOG_LEVEL logLevel, int logQueSize);
    ~Webserver();

    void start();

private:
    static int setFdNonBlock(int fd);

    bool initSocket();
    void initEventMode();
    void addClient(int fd, sockaddr_in addr);
    void dealListen();
    void dealWrite(HttpConn *client);
    void dealRead(HttpConn *client);
    void sendError(int fd, const char *info);
    void extentTime(HttpConn *client);
    void closeConn(HttpConn *client);
    void onRead(HttpConn *client);
    void onWrite(HttpConn *client);
    void onProcess(HttpConn *client);

    int port_;
    int timeoutMs_;
    int listenFd_;
    volatile bool isClose_;
    char *srcDir_;
    uint32_t listenEvent_;
    uint32_t connEvent_;

    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadPool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;

    static const size_t MAX_FD_CNT_ = 65536;
};