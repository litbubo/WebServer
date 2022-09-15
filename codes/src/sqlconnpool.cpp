#include <sqlconnpool.h>
#include <log.h>
#include <cassert>

/*
 * 单例模式，私有化构造函数和析构函数，析构时关闭连接池
 */
SqlConnPool::~SqlConnPool()
{
    this->closePool();
}

/*
 * 单例模式，返回连接池实例
 */
SqlConnPool *SqlConnPool::instance()
{
    static SqlConnPool connPool;
    return &connPool;
}

/*
 * 连接池初始化函数
 */
void SqlConnPool::init(const char *host,
                       int port,
                       const char *user,
                       const char *pwd,
                       const char *db,
                       size_t connSize)
{
    assert(connSize > 0);
    /* 生成 connSize个连接 */
    for (size_t i = 0; i < connSize; i++)
    {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (sql == nullptr)
        {
            LOG_ERROR("MySQL init error!");
            assert(static_cast<bool>(sql));
        }
        sql = mysql_real_connect(sql, host, user, pwd, db, port, nullptr, 0);
        if (sql == nullptr)
        {
            LOG_ERROR("MySQL connect error!");
            assert(static_cast<bool>(sql));
        }
        connQue_.push(sql);
    }
    MAX_CONN_ = connSize;
    /* 初始化信号量 */
    sem_init(&sem_, 0, MAX_CONN_);
}

void SqlConnPool::closePool()
{
    /* 枷锁，将池中所有的链接取出后关闭 */
    std::lock_guard<std::mutex> locker(mtx_);
    while (connQue_.empty() != true)
    {
        MYSQL *sql = connQue_.front();
        connQue_.pop();
        mysql_close(sql);
    }
    mysql_library_end();
}

/*
 * 取出一个连接
 */
MYSQL *SqlConnPool::getConn()
{
    MYSQL *sql = nullptr;
    sem_wait(&sem_);

    {
        std::lock_guard<std::mutex> locker(mtx_);
        /* 双检，保证取出来的连接是真实存在的 */
        if (connQue_.empty())
        {
            return nullptr;
        }
        sql = connQue_.front();
        connQue_.pop();
    }
    /* 判断是否真正取出来连接了 */
    assert(static_cast<bool>(sql));
    return sql;
}

/*
 * 将连接放回连接池
 */
void SqlConnPool::freeConn(MYSQL *sql)
{
    assert(static_cast<bool>(sql));

    {
        std::lock_guard<std::mutex> locker(mtx_);
        connQue_.push(sql);
    }
    sem_post(&sem_);
}

/*
 * 获取连接池中的连接数
 */
int SqlConnPool::getFreeConnCount()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}
