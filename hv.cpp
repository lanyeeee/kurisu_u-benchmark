#include <hv/TcpServer.h>

using namespace hv;

int main()
{
    TcpServer server;
    server.createsocket(5005);

    server.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        channel->write(buf);
    };

    server.setThreadNum(4);
    server.start();

    while (1)
        sleep(1);
    return 0;
}
