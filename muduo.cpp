#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoopThreadPool.h>
using namespace muduo;
using namespace muduo::net;

int main()
{
    EventLoop loop;
    TcpServer serv(&loop, InetAddress(5005), "echo");
    serv.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
        conn->send(buf);
    });

    serv.setThreadNum(4);
    serv.start();
    loop.loop();
}
