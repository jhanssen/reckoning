#include <event/EventLoop.h>
#include <event/Signal.h>
#include <log/Log.h>
#include <net/TcpSocket.h>
#include <net/HttpClient.h>
#include <string>

using namespace reckoning;
using namespace reckoning::log;
using namespace std::chrono_literals;

#define HAVE_CREF

class Test
{
public:
    Test(const char* str)
        : s(str)
    {
        Log(Log::Info) << "  Test(const char*)" << str;
    }
    Test(Test&& t)
        : s(std::move(t.s))
    {
        Log(Log::Info) << "  Test(Test&&)" << s;
    }
#ifdef HAVE_CREF
    Test(const Test& t)
        : s(t.s)
    {
        Log(Log::Info) << "  Test(const Test&)" << s;
    }
#endif

    std::string str() const { return s; }

private:
#ifndef HAVE_CREF
    Test(const Test&) = delete;
#endif
    Test& operator=(const Test&) = delete;
    Test& operator=(Test&&) = delete;

    std::string s;
};

event::Signal<int, Test&&> sig;

int main(int argc, char** argv)
{
    Log::initialize(Log::Debug);

    std::shared_ptr<event::EventLoop> loop = event::EventLoop::create();
    loop->init();

    auto conn = sig.connect([](int i, Test&& t) {
            Log(Log::Info) << "sig" << i << t.str();
        });
    std::thread hey([&conn]() {
            Test slot1("slot1");
            sig.emit(10, std::move(slot1));

            conn.disconnect();

            Test slot2("slot2");
            sig.emit(20, std::move(slot2));
        });

    std::shared_ptr<event::EventLoop::Timer> timer;
    timer = loop->timer(1000ms, event::EventLoop::Interval, [&timer]() {
            static int cnt = 0;
            Log(Log::Info) << "yes" << ++cnt;
            if (cnt == 5)
                timer->stop();
        });
    loop->timer(1500ms, [](Test&& t) {
            Log(Log::Info) << "t" << t.str();
        }, Test("timer"));
    loop->send([](Test&& t) {
            Log(Log::Info) << "s" << t.str();
        }, Test("send"));

    /*
    std::shared_ptr<net::TcpSocket> socket = net::TcpSocket::create();
    socket->onStateChanged().connect([](std::shared_ptr<net::TcpSocket>&& socket, net::TcpSocket::State state) {
            Log(Log::Info) << "socket state change" << static_cast<int>(state);
        });
    socket->onReadyRead().connect([](std::shared_ptr<net::TcpSocket>&& socket) {
            Log(Log::Info) << "ready to read";
            auto buf = socket->read();
            if (buf)
                printf("read %zu bytes\n", buf->size());
        });
    socket->connect("www.google.com", 80);
    socket->write("GET / HTTP/1.0\r\n\r\n", 18);
    */
    auto http = net::HttpClient::create();
    http->onResponse().connect([](net::HttpClient::Response&& response) {
            Log(Log::Info) << response.status << response.reason;
            for (const auto& header : response.headers) {
                Log(Log::Info) << header.first << header.second;
            }
        });
    http->onBodyData().connect([](std::shared_ptr<buffer::Buffer>&& data) {
            Log(Log::Info) << "body size" << data->size();
        });
    http->onBodyEnd().connect([]() {
            Log(Log::Info) << "body end";
        });
    http->onStateChanged().connect([](net::HttpClient::State state) {
            Log(Log::Info) << "http state" << state;
        });
    http->connect("www.google.com", 80);
    http->get(net::HttpClient::v10, "/");

    // std::shared_ptr<buffer::Buffer> buf1, buf2;
    // auto buf3 = buffer::Buffer::concat(buf1, buf2);

    hey.join();

    return loop->execute(10000ms);
}
