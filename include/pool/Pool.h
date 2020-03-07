#ifndef POOLPOOL_H
#define POOLPOOL_H

#include <memory>
#include <vector>
#include <log/Log.h>

namespace reckoning {
namespace pool {

template<typename Type, size_t Size>
class Pool
{
public:
    Pool();
    ~Pool();

    std::shared_ptr<Type> get();

    static Pool<Type, Size>& pool();

private:
    std::vector<std::shared_ptr<Type> > mItems;
    thread_local static Pool<Type, Size> tPool;
};

template<typename Type, size_t Size>
thread_local Pool<Type, Size> Pool<Type, Size>::tPool;

template<typename Type, size_t Size>
inline Pool<Type, Size>::Pool()
{
    mItems.reserve(Size);
    for (size_t i = 0; i < Size; ++i) {
        mItems.push_back(Type::create());
    }
}

template<typename Type, size_t Size>
inline Pool<Type, Size>::~Pool()
{
    for (const auto& item : mItems) {
        if (item.use_count() != 1) {
            // bad news
            log::Log(log::Log::Fatal) << "Pool d'tor called when item still alive";
        }
    }
}

template<typename Type, size_t Size>
std::shared_ptr<Type> Pool<Type, Size>::get()
{
    for (const auto& item : mItems) {
        if (item.use_count() == 1)
            return item;
    }
    return Type::create();
}

template<typename Type, size_t Size>
Pool<Type, Size>& Pool<Type, Size>::pool()
{
    return tPool;
}

}} // namespace reckoning::pool

#endif
