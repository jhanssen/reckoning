#ifndef LOG_H
#define LOG_H

#include <string>
#include <mutex>
#include <cstdio>
#include <unistd.h>

namespace reckoning {
namespace log {

class Log
{
public:
    enum Level {
        Debug,
        Info,
        Warn,
        Error,
        Fatal
    };
    enum Output {
        Default,
        Stdout,
        Stderr,
        File
    };

    Log(Level level = Log::Info, Output output = Default);
    ~Log();

    Log& operator<<(const char* str);
    Log& operator<<(const std::string& str);

    template<typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
    Log& operator<<(T num);

    template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type* = nullptr>
    Log& operator<<(T num);

    struct EndLine {};
    static const EndLine& endl();

    Log& operator<<(const EndLine&);

    static void initialize(Level level, Output output = Default, const std::string& filename = std::string());

private:
    Level mLevel;
    Output mOutput;
    int mNum;

    static int sFd;
    static Level sLevel;
    static Output sOutput;
    static std::mutex sMutex;
};

inline Log::Log(Level level, Output output)
    : mLevel(level), mOutput(output), mNum(0)
{
    sMutex.lock();
    if (mOutput == Default)
        mOutput = sOutput;
}

inline Log::~Log()
{
    operator<<("\n");
    if (mLevel == Fatal) {
        abort();
    }
    sMutex.unlock();
}

inline Log& Log::operator<<(const char* str)
{
    if (mLevel < sLevel)
        return *this;

    auto write = [this](const char* str) {
        auto maybeWriteToFile = [str](int fd) {
            if (fd == -1)
                return;
            dprintf(fd, "%s", str);
        };

        switch (mOutput) {
        case Default:
            maybeWriteToFile(sFd);
            // fallthrough
        case Stdout:
            dprintf(STDOUT_FILENO, "%s", str);
            break;
        case Stderr:
            dprintf(STDERR_FILENO, "%s", str);
            break;
        case File:
            maybeWriteToFile(sFd);
            break;
        }
    };

    if (mNum++ > 0)
        write(" ");
    write(str);
    return *this;
}

inline Log& Log::operator<<(const std::string& str)
{
    return operator<<(str.c_str());
}

template<typename T, typename std::enable_if<std::is_integral<T>::value, T>::type*>
inline Log& Log::operator<<(T num)
{
    return operator<<(std::to_string(num).c_str());
}

template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type*>
inline Log& Log::operator<<(T num)
{
    return operator<<(std::to_string(static_cast<uint64_t>(num)).c_str());
}

inline const Log::EndLine& Log::endl()
{
    static EndLine inst;
    return inst;
}

inline Log& Log::operator<<(const EndLine&)
{
    return operator<<("\n");
}

}} // namespace reckoning::log

#endif
