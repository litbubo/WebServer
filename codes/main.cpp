#include <webserver.h>

int main()
{
    Webserver server(
        1316, 60000,                                          /* 端口 timeoutMs  */
        3306, "debian-sys-maint", "Xs2MbM94SgMsraFP", "mydb", /* Mysql配置 */
        12, 12, true, Log::ERROR, 1024);                      /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.start();
}
