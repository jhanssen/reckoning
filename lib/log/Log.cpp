#include <log/Log.h>
#include <fcntl.h>
#include <cstdlib>

using namespace reckoning;
using namespace reckoning::log;

int Log::sFd = -1;
Log::Level Log::sLevel = Log::Error;
Log::Output Log::sOutput = Log::Default;
std::mutex Log::sMutex;

void Log::initialize(Level level, Output output, const std::string& filename)
{
    sLevel = level;
    sOutput = output;
    if (!filename.empty()) {
        sFd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CREAT);
        if (sFd != -1) {
            std::atexit([]() {
                    ::close(sFd);
                });
        }
    }
}
