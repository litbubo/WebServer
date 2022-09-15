#pragma once

#include <sqlconnpool.h>
#include <mysql/mysql.h>
#include <cassert>

/*
 * RAII应用，构造时取走连接，离开作用域自动放回连接
 */
class SqlConnRAII
{

public:
    SqlConnRAII(MYSQL **sql, SqlConnPool *connPool)
    {
        assert(static_cast<bool>(connPool));
        connPool_ = connPool;
        sql_ = connPool_->getConn();
        *sql = sql_;
    }
    ~SqlConnRAII()
    {
        if (sql_ != nullptr)
        {
            connPool_->freeConn(sql_);
        }
    }

private:
    MYSQL *sql_;
    SqlConnPool *connPool_;
};
