#pragma once

#include <queue>
#include <mutex>
#include <thread>
#include <mysql/mysql.h>
#include <semaphore.h>

class SqlConnPool
{
public:
    static SqlConnPool *instance();
    MYSQL *getConn();
    void freeConn(MYSQL *sql);
    int getFreeConnCount();
    void init(const char *host,
              int port,
              const char *user,
              const char *pwd,
              const char *db,
              size_t connSize);
    void closePool();

private:
    SqlConnPool() = default;
    ~SqlConnPool();

    size_t MAX_CONN_;

    std::mutex mtx_;
    sem_t sem_;
    std::queue<MYSQL *> connQue_;
};
