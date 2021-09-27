#include <kurisu/kurisu.h>

int main()
{
    kurisu::EventLoop loop;
    kurisu::TcpServer server(&loop, kurisu::SockAddr(5005), "echo");  //listen port 5005

    server.SetMessageCallback([](const std::shared_ptr<kurisu::TcpConnection>& conn, kurisu::Buffer* buf, kurisu::Timestamp) {
        conn->Send(buf);
    });

    server.SetThreadNum(4);
    server.Start();
    loop.Loop();
}