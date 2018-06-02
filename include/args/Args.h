#ifndef ARGS_H
#define ARGS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <typeinfo>
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

    template<typename T, typename std::enable_if<std::is_pod<T>::value && !std::is_floating_point<T>::value>::type* = nullptr>
    T value(const std::string& key, T defaultValue = T()) const;

    template<typename T, typename std::enable_if<std::is_pod<T>::value && std::is_floating_point<T>::value>::type* = nullptr>
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
    if (v->second.type() == typeid(int64_t)) {
        if (typeid(T) == typeid(int32_t) ||
            typeid(T) == typeid(float) ||
            typeid(T) == typeid(double))
            return true;
        return false;
    }
    if (typeid(T) == typeid(float) && v->second.type() == typeid(double))
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

template<typename T, typename std::enable_if<std::is_pod<T>::value && !std::is_floating_point<T>::value>::type*>
inline T Args::value(const std::string& key, T defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(T))
        return std::any_cast<T>(v->second);
    if (typeid(T) == typeid(int32_t) && v->second.type() == typeid(int64_t))
        return static_cast<int32_t>(std::any_cast<int64_t>(v->second));
    return defaultValue;
}

template<typename T, typename std::enable_if<std::is_pod<T>::value && std::is_floating_point<T>::value>::type*>
inline T Args::value(const std::string& key, T defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(double))
        return static_cast<T>(std::any_cast<double>(v->second));
    if (v->second.type() == typeid(int64_t))
        return static_cast<T>(std::any_cast<int64_t>(v->second));
    return defaultValue;
}

inline size_t Args::freeformSize() const
{
    return mFreeform.size();
}

inline std::string Args::freeformValue(size_t idx) const
{
    if (idx >= mFreeform.size())
        return std::string();
    return mFreeform[idx];
}

}} // namespace reckoning::args

#endif // ARGS_H
