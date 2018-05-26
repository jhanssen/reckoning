#include <event/EventLoop.h>
#include <log/Log.h>

using namespace reckoning;
using namespace std::chrono_literals;

int main(int argc, char** argv)
{
    log::Log::initialize(log::Log::Debug);

    std::shared_ptr<event::EventLoop> loop = std::make_shared<event::EventLoop>();
    std::shared_ptr<event::EventLoop::Timer> timer;
    timer = loop->timer(1000ms, event::EventLoop::Interval, [&timer]() {
            static int cnt = 0;
            log::Log(log::Log::Info) << "yes" << ++cnt;
            if (cnt == 5)
                timer->stop();
        });
    return loop->execute(10000ms);
}
