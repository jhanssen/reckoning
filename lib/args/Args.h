#ifndef ARGS_H
#define ARGS_H

#include <string>
#include <unordered_map>
#include <util/Any.h>

namespace reckoning {
namespace args {

class Parser;

class Args
{
public:
    ~Args();

    bool has(const std::string& key) const;
    template<typename T>
    bool has(const std::string& key) const;

    template<typename T, typename std::enable_if<!std::is_pod<T>::value>::type* = nullptr>
    T value(const std::string& key, const T& defaultValue = T()) const;

    template<typename T, typename std::enable_if<std::is_pod<T>::value>::type* = nullptr>
    T value(const std::string& key, T defaultValue = T()) const;

    size_t freeformSize() const;
    std::string freeformValue(size_t idx) const;

private:
    Args();

    friend class Parser;

private:
    std::unordered_map<std::string, std::any> mValues;
    std::vector<std::string> mFreeform;
};

inline Args::Args()
{
}

inline Args::~Args()
{
}

inline bool Args::has(const std::string& key) const
{
    return mValues.count(key) > 0;
}

template<typename T>
inline bool Args::has(const std::string& key) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return false;
    if (v->second.type() == typeid(T))
        return true;
    return false;
}

template<typename T, typename std::enable_if<!std::is_pod<T>::value>::type*>
inline T Args::value(const std::string& key, const T& defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(T))
        return std::any_cast<T>(v->second);
    return defaultValue;
}

template<typename T, typename std::enable_if<std::is_pod<T>::value>::type*>
inline T Args::value(const std::string& key, T defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(T))
        return std::any_cast<T>(v->second);
    return defaultValue;
}

}} // namespace reckoning::args

#endif // ARGS_H
