#ifndef LOG_H
#define LOG_H

#include <string>
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

    struct EndLine {};
    static const EndLine& endl();

    Log& operator<<(const EndLine&);

    static void initialize(Level level, Output output, const std::string& filename = std::string());

private:
    Level mLevel;
    Output mOutput;

    static int sFd;
    static Level sLevel;
    static Output sOutput;
};

inline Log::Log(Level level, Output output)
    : mLevel(level), mOutput(output)
{
    if (mOutput == Default)
        mOutput = sOutput;
}

inline Log::~Log()
{
    operator<<("\n");
}

inline Log& Log::operator<<(const char* str)
{
    if (mLevel < sLevel)
        return *this;

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
