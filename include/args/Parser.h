#ifndef ARGSPARSER_H
#define ARGSPARSER_H

#include <args/Args.h>
#include <log/Log.h>
#include <cassert>

namespace reckoning {
namespace args {

class Parser
{
public:
    static Args parse(int argc, char** argv);

private:
    Parser() = delete;
};

inline Args Parser::parse(int argc, char** argv)
{
    Args args;

    enum State { Normal, Dash, DashDash, Value, Freeform, FreeformOnly };
    State state = Normal;

    std::string key;

    auto guessValue = [](char* start, char* end) -> std::any {
        if (!(end - start))
            return std::any(true);
        if (end - start == 4 && !strcmp("true", start))
            return std::any(true);
        if (end - start == 5 && !strcmp("false", start))
            return std::any(false);
        char* endptr;
        long long l = strtoll(start, &endptr, 0);
        if (endptr == end) {
            return std::any(static_cast<int64_t>(l));
        }
        double d = strtod(start, &endptr);
        if (endptr == end) {
            return std::any(d);
        }
        return std::any(std::string(start, end - start));
    };

    auto add = [&key, &args, &guessValue](State state, char* start, char* end) {
        //log::Log(log::Log::Error) << "want to add" << state << std::string(start, end - 1 - start);
        assert(state != Normal);
        switch (state) {
        case Dash:
            while (start < end - 1) {
                args.mValues[std::string(1, *(start++))] = std::any(true);
            }
            break;
        case DashDash:
            key = std::string(start, end - 1 - start);
            break;
        case Value: {
            const auto v = guessValue(start, end - 1);
            if (v.type() == typeid(bool)) {
                if (key.size() > 3 && !strncmp(key.c_str(), "no-", 3)) {
                    args.mValues[key.substr(3)] = std::any(!std::any_cast<bool>(std::move(v)));
                    break;
                }
                if (key.size() > 8 && !strncmp(key.c_str(), "disable-", 8)) {
                    args.mValues[key.substr(8)] = std::any(!std::any_cast<bool>(std::move(v)));
                    break;
                }
            }
            args.mValues[key] = std::move(v);
            break; }
        case Freeform:
            args.mFreeform.push_back(std::string(start, end - 1 - start));
            break;
        default:
            break;
        }
    };

    auto error = [](const char* msg, int offset, char* word) {
        log::Log(log::Log::Error) << msg << "at offset" << offset - 1 << "word" << word;
    };

    size_t off = 0;
    for (int i = 1; i < argc; ++i) {
        char* arg = argv[i];
        char* argStart = arg;
        char* prev = arg;
        bool done = false;
        while (!done) {
            switch (*(arg++)) {
            case '-':
                switch (state) {
                case Normal:
                    prev = arg;
                    state = Dash;
                    continue;
                case Dash:
                    if (prev == arg - 1) {
                        ++prev;
                        state = DashDash;
                    } else {
                        error("unexpected dash", off + arg - argStart, argStart);
                        return Args();
                    }
                    continue;
                case Freeform:
                case FreeformOnly:
                case DashDash:
                case Value:
                    continue;
                default:
                    error("unexpected dash", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            case '\0':
                done = true;
                switch (state) {
                case Normal:
                    add(Freeform, prev, arg);
                    prev = arg;
                    continue;
                case Dash:
                    add(Dash, prev, arg);
                    prev = arg;
                    state = Normal;
                    continue;
                case DashDash:
                    if (arg - argStart == 3) { // 3 = --\0
                        prev = arg;
                        state = FreeformOnly;
                    } else {
                        add(DashDash, prev, arg);
                        add(Value, arg, arg + 1);
                        prev = arg;
                        state = Normal;
                    }
                    continue;
                case Freeform:
                    state = Normal;
                    // fall through
                case FreeformOnly:
                    add(Freeform, prev, arg);
                    prev = arg;
                    continue;
                case Value:
                    add(Value, prev, arg);
                    prev = arg;
                    state = Normal;
                    continue;
                default:
                    error("unexpected state", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            case '=':
                switch (state) {
                case Freeform:
                case FreeformOnly:
                case Value:
                    continue;
                case DashDash:
                    add(DashDash, prev, arg);
                    prev = arg;
                    state = Value;
                    continue;
                default:
                    error("unexpected equals", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            default:
                if (state == Normal && argStart == arg - 1) {
                    prev = arg - 1;
                    state = Freeform;
                }
                break;
            }
        }
        off += arg - argStart;
    }

    return args;
}

}} // namespace reckoning::args

#endif // ARGSPARSER_H