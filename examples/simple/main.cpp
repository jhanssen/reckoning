#include <event/EventLoop.h>

using namespace reckoning;

int main(int argc, char** argv)
{
    event::EventLoop loop;
    return loop.execute();
}
