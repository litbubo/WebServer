#include <sqlconnpool.h>

#include <cassert>

SqlConnPool::~SqlConnPool()
{
    this->closePool();
}

SqlConnPool *SqlConnPool::instance()
{
    static SqlConnPool connPool;
    return &connPool;
}

void SqlConnPool::init(const char *host,
                       int port,
                       const char *user,
                       const char *pwd,
                       const char *db,
                       size_t connSize)
{
    assert(connSize > 0);
    for (size_t i = 0; i < connSize; i++)
    {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (sql == nullptr)
        {
            assert(static_cast<bool>(sql));
        }
        sql = mysql_real_connect(sql, host, user, pwd, db, port, nullptr, 0);
        if (sql == nullptr)
        {
            assert(static_cast<bool>(sql));
        }
        connQue_.push(sql);
    }
    MAX_CONN_ = connSize;
    sem_init(&sem_, 0, MAX_CONN_);
}

void SqlConnPool::closePool()
{
    std::lock_guard<std::mutex> locker(mtx_);
    while (connQue_.empty() != true)
    {
        MYSQL *sql = connQue_.front();
        connQue_.pop();
        mysql_close(sql);
    }
    mysql_library_end();
}

MYSQL *SqlConnPool::getConn()
{
    MYSQL *sql = nullptr;
    sem_wait(&sem_);

    {
        std::lock_guard<std::mutex> locker(mtx_);
        if (connQue_.empty())
        {
            return nullptr;
        }
        sql = connQue_.front();
        connQue_.pop();
    }
    assert(static_cast<bool>(sql));
    return sql;
}

void SqlConnPool::freeConn(MYSQL *sql)
{
    assert(static_cast<bool>(sql));

    {
        std::lock_guard<std::mutex> locker(mtx_);
        connQue_.push(sql);
    }
    sem_post(&sem_);
}

int SqlConnPool::getFreeConnCount()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}
