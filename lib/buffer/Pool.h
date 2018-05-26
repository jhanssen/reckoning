#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include <memory>
#include "Buffer.h"

namespace reckoning {
namespace buffer {

template<size_t NumberOfBuffers, size_t Size>
class Pool
{
public:
    Pool();

    std::shared_ptr<Buffer<Size> > get();

    static Pool<NumberOfBuffers, Size>& pool();

private:
    std::vector<std::shared_ptr<Buffer<Size> > > mBuffers;
    thread_local static Pool<NumberOfBuffers, Size> tPool;
};

template<size_t NumberOfBuffers, size_t Size>
thread_local Pool<NumberOfBuffers, Size> Pool<NumberOfBuffers, Size>::tPool;

template<size_t NumberOfBuffers, size_t Size>
inline Pool<NumberOfBuffers, Size>::Pool()
{
    mBuffers.reserve(NumberOfBuffers);
    for (size_t i = 0; i < NumberOfBuffers; ++i) {
        mBuffers.push_back(std::make_shared<Buffer<Size> >());
    }
}

template<size_t NumberOfBuffers, size_t Size>
std::shared_ptr<Buffer<Size> > Pool<NumberOfBuffers, Size>::get()
{
    for (const auto& buffer : mBuffers) {
        if (!buffer->isInUse())
            return buffer;
    }
    return std::make_shared<Buffer<Size> >();
}

template<size_t NumberOfBuffers, size_t Size>
Pool<NumberOfBuffers, Size>& Pool<NumberOfBuffers, Size>::pool()
{
    return tPool;
}

}} // namespace reckoning::buffer

#endif
