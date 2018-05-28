#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include <memory>
#include <vector>
#include <log/Log.h>
#include "Buffer.h"

namespace reckoning {
namespace buffer {

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
class Pool
{
public:
    Pool();
    ~Pool();

    std::shared_ptr<Buffer> get();

    static Pool<NumberOfBuffers, SizeOfBuffer>& pool();

private:
    std::vector<std::shared_ptr<Buffer> > mBuffers;
    thread_local static Pool<NumberOfBuffers, SizeOfBuffer> tPool;
    uint8_t mBufferData[NumberOfBuffers * SizeOfBuffer];
};

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
thread_local Pool<NumberOfBuffers, SizeOfBuffer> Pool<NumberOfBuffers, SizeOfBuffer>::tPool;

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
inline Pool<NumberOfBuffers, SizeOfBuffer>::Pool()
{
    mBuffers.reserve(NumberOfBuffers);
    uint8_t* mem = mBufferData;
    for (size_t i = 0; i < NumberOfBuffers; ++i) {
        mBuffers.push_back(Buffer::create(mem, SizeOfBuffer));
        mem += SizeOfBuffer;
    }
}

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
inline Pool<NumberOfBuffers, SizeOfBuffer>::~Pool()
{
    for (const auto& buf : mBuffers) {
        if (buf.use_count() != 1) {
            // bad news
            log::Log(log::Log::Fatal) << "Pool d'tor called when buffers still alive";
        }
    }
}

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
std::shared_ptr<Buffer> Pool<NumberOfBuffers, SizeOfBuffer>::get()
{
    for (const auto& buffer : mBuffers) {
        if (!buffer->isInUse())
            return buffer;
    }
    return Buffer::create(nullptr, SizeOfBuffer);
}

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
Pool<NumberOfBuffers, SizeOfBuffer>& Pool<NumberOfBuffers, SizeOfBuffer>::pool()
{
    return tPool;
}

}} // namespace reckoning::buffer

#endif
