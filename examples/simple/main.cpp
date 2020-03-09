#include <event/Loop.h>
#include <event/Signal.h>
#include <log/Log.h>
#include <net/Fetch.h>
#include <net/TcpSocket.h>
#include <net/TcpServer.h>
#include <net/HttpClient.h>
#include <net/WebSocketClient.h>
#include <image/Decoder.h>
#include <serializer/Serializer.h>
#include <args/Args.h>
#include <args/Parser.h>
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

    {
        serializer::Serializer s1(fs::Path("/tmp/reckoning_serialize"), serializer::Serializer::Write | serializer::Serializer::Truncate);
        assert(s1.isValid());
        s1 << "ting" << 123;
        s1.close();

        serializer::Serializer s2(fs::Path("/tmp/reckoning_serialize"));
        assert(s2.valid() == serializer::Serializer::DataReady);
        std::string str;
        int i;
        s2 >> str >> i;
        printf("file deserialized %s %d\n", str.c_str(), i);
    }
    {
        serializer::Serializer s3;
        assert(s3.isValid());
        s3 << "ting" << 123;
        s3.close();

        serializer::Serializer s4(s3.buffer());
        assert(s4.valid() == serializer::Serializer::DataReady);
        std::string str;
        int i;
        s4 >> str >> i;
        printf("buffer deserialized %s %d\n", str.c_str(), i);
    }

    auto args = reckoning::args::Parser::parse(argc, argv);
    if (args.has<bool>("f"))
        log::Log(log::Log::Error) << "f" << args.value<bool>("f");
    if (args.has<float>("i"))
        log::Log(log::Log::Error) << "i" << args.value<float>("i");
    if (args.freeformSize() > 0) {
        log::Log(log::Log::Error) << "freeforms" << args.freeformSize() << args.freeformValue(0);
    }


    std::shared_ptr<event::Loop> loop = event::Loop::create();

    /*
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

    std::shared_ptr<event::Loop::Timer> timer;
    timer = loop->addTimer(1000ms, event::Loop::Interval, [&timer]() {
            static int cnt = 0;
            Log(Log::Info) << "yes" << ++cnt;
            if (cnt == 5)
                timer->stop();
        });
    loop->addTimer(1500ms, [](Test&& t) {
            Log(Log::Info) << "t" << t.str();
        }, Test("timer"));
    loop->send([](Test&& t) {
            Log(Log::Info) << "s" << t.str();
            }, Test("send"));
    */

    /*
    FILE* f = fopen("/tmp/data.html", "w");
    auto http = net::HttpClient::create("http://www.vg.no/");
    http->onResponse().connect([](net::HttpClient::Response&& response) {
        printf("responsey %d\n", response.status);
    });
    http->onComplete().connect([f]() {
        fclose(f);
        printf("complete\n");
    });
    http->onError().connect([](std::string&& err) {
        printf("error %s\n", err.c_str());
    });
    http->onBodyData().connect([f](std::shared_ptr<buffer::Buffer>&& buffer) {
        printf("got body %zu\n", buffer->size());
        fwrite(buffer->data(), buffer->size(), 1, f);
    });
    */
    auto fetch = net::Fetch::create();
    auto decoder = image::Decoder::create();
    fetch->fetch("https://www.google.com/images/branding/googlelogo/2x/googlelogo_color_272x92dp.png").then([&decoder](std::shared_ptr<buffer::Buffer>&& buffer) -> auto& {
        printf("fetched (goog) %zu\n", buffer->size());
        if (!buffer) {
            return reckoning::then::rejected<image::Decoder::Image>("no buffer from fetch");
        }
        return decoder->decode(std::move(buffer));
    }).then([](image::Decoder::Image&& image) -> reckoning::then::MaybeFail<size_t> {
        if (false || !image.data) {
            return reckoning::then::Fail("ball");
        }
        return image.data->size();
    }).then([](size_t ting) -> reckoning::then::MaybeFail<void> {
        printf("decoded to %zu\n", ting);
        return {};
    }).fail([](std::string&& err) {
        printf("failed... '%s'\n", err.c_str());
    });
;
    fetch->fetch("/usr/share/doc/bash/bash.html").then([](std::shared_ptr<buffer::Buffer>&& buffer) {
        printf("fetched (file) %zu\n", buffer->size());
    });

    /*
    std::shared_ptr<then::Then<float> > then3 = std::make_shared<then::Then<float> >();
    auto ball = [then3](int floff) -> then::Then<float>& {
        return *then3.get();
    };
    printf("uuuh %p\n", then3.get());

    std::shared_ptr<then::Then<std::string> > then4 = std::make_shared<then::Then<std::string> >();
    auto ball2 = [then4](float fliff) -> then::Then<std::string>& {
        return *then4.get();
    };

    then3->resolve(99.9);

    then::Then<std::string> then2;
    then2.then([](std::string&& arg) {
        printf("hello %s\n", arg.c_str());
        return 20;
    }).then([&ball](int arg) -> then::Then<float>& {
        printf("got %d\n", arg);
        printf("wippo %p\n", &ball(arg));
        return ball(arg);
    }).then([&ball2](float trall) -> then::Then<std::string>& {
        printf("got floaty %f\n", trall);
        return ball2(trall);
    }).then([](std::string&& fliff) {
        printf("and finally fliff '%s'\n", fliff.c_str());
    });
    std::string bkll("abc");
    then2.resolve(std::move(bkll));

    loop->post([then3, then4]() {
        printf("oky %p\n", then3.get());
        then4->resolve("bokko bokk");
    });
    */

    /*
    auto ws = net::WebSocketClient::create("ws://demos.kaazing.com/echo");
    ws->onMessage().connect([](std::shared_ptr<buffer::Buffer>&& msg) {
        Log(Log::Info) << "ws msg" << msg->size();
    });
    ws->onError().connect([](std::string&& err) {
        printf("error %s\n", err.c_str());
    });
    ws->onComplete().connect([]() {
        Log(Log::Info) << "ws complete";
    });
    ws->write("foo bar", 7);
    */

    /*
    std::shared_ptr<net::TcpSocket> socket = net::TcpSocket::create();
    socket->onStateChanged().connect([](net::TcpSocket::State state) {
            Log(Log::Info) << "socket state change" << static_cast<int>(state);
        });
    socket->onData().connect([](std::shared_ptr<buffer::Buffer>&& buf) {
            Log(Log::Info) << "ready to read";
            if (buf) {
                printf("read %zu bytes\n", buf->size());
                printf("%s\n", std::string(reinterpret_cast<const char*>(buf->data()), buf->size()).c_str());
            }
        });
    socket->connect("www.google.com", 80);
    //socket->connect("www.vg.no", 443, net::TcpSocket::TLS);
    socket->write("GET / HTTP/1.0\r\nHost: www.google.no\r\n\r\n", 39);
    */
    /*
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
    http->get(net::HttpClient::v11, "/");
    */
    /*
    auto ws = net::WebSocketClient::create();
    ws->onStateChanged().connect([](net::WebSocketClient::State state) {
            Log(Log::Info) << "ws state" << state;
        });
    ws->onMessage().connect([](std::shared_ptr<buffer::Buffer>&& msg) {
            Log(Log::Info) << "msg" << msg->size();
        });
    ws->write("{\"foo\": 123}", 12);
    ws->connect("localhost", 8999, "/");
    */
    /*
    auto server = net::TcpServer::create();
    server->onConnection().connect([](std::shared_ptr<net::TcpSocket>&& socket) {
            Log(Log::Error) << "got connection";
            auto s = std::move(socket);
            s->onData().connect([](std::shared_ptr<buffer::Buffer>&& buffer) {
                    Log(Log::Error) << "got socket data" << buffer->size();
                });
            s->onStateChanged().connect([s](net::TcpSocket::State state) mutable {
                    if (state == net::TcpSocket::Closed || state == net::TcpSocket::Error)
                        s.reset();
                });
        });
    server->onError().connect([]() {
            Log(Log::Error) << "tcp server error";
        });
        server->listen(8998);
    */
    /*
    auto server = net::HttpServer::create();
    server->onRequest().connect([](std::shared_ptr<net::HttpServer::Request>&& req) {
            Log(Log::Error) << "http server request" << req->query;
            req->onBody().connect([](std::shared_ptr<buffer::Buffer>&& buffer) {
                    Log(Log::Info) << "got data from request" << buffer->size();
                });
            req->onEnd().connect([req]() mutable {
                    Log(Log::Info) << "request end";
                    net::HttpServer::Response response;
                    response.status = 200;
                    response.reason = "Ok";
                    response.headers.add("Content-Length", "13");
                    req->write(std::move(response));
                    req->write("Hello world\r\n", 13);
                    req.reset();
                });
        });
    server->onError().connect([]() {
            Log(Log::Error) << "http server error";
            });
    server->listen(8998);
    auto wsserver = net::WebSocketServer::create(std::move(server));
    wsserver->onConnection().connect([](std::shared_ptr<net::WebSocketServer::Connection>&& conn) {
            Log(Log::Error) << "got ws conn";
            auto c = std::move(conn);
            c->onMessage().connect([](std::shared_ptr<buffer::Buffer>&& msg) {
                    Log(Log::Info) << "ws msg" << msg->size();
                });
            c->onStateChanged().connect([c](net::WebSocketServer::Connection::State state) mutable {
                    Log(Log::Info) << "ws state" << state;

                    c.reset();
                });
        });
    wsserver->onError().connect([]() {
            Log(Log::Error) << "ws server error";
        });
    */

    // std::shared_ptr<buffer::Buffer> buf1, buf2;
    // auto buf3 = buffer::Buffer::concat(buf1, buf2);

    // hey.join();

    return loop->execute(60000ms);
}
