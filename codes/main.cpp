#include <webserver.h>
#include <signal.h>

typedef void (*sighandler_t)(int);

void myexit(int s)
{
    exit(0);
}

int main()
{
    signal(SIGINT, myexit);
    Webserver server(
        1316, 60000,                                          /* 端口 timeoutMs  */
        3306, "debian-sys-maint", "Xs2MbM94SgMsraFP", "mydb", /* Mysql配置 */
        12, 12, true, Log::DEBUG, 0);                      /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.start();
}
