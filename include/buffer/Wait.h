#ifndef BUFFERWAIT_H
#define BUFFERWAIT_H

#include <buffer/Buffer.h>
#include <buffer/Pool.h>
#include <event/Signal.h>
#include <string>
#include <string.h>

namespace reckoning {
namespace buffer {

template<size_t NumberOfBuffers = 5, size_t MaxBufferSize = 65536>
class Wait
{
public:
    Wait(const char* needle);
    Wait(std::string&& needle);
    Wait(const std::string& needle);
    Wait(Wait&& other);
    ~Wait();

    Wait& operator=(Wait&& other);

    void feed(std::shared_ptr<Buffer>&& buffer);

    event::Signal<std::shared_ptr<Buffer>&&, size_t>& onData();

private:
    Wait(const Wait&) = delete;
    Wait& operator=(const Wait&) = delete;

    std::shared_ptr<Buffer> mBuffer;
    event::Signal<std::shared_ptr<Buffer>&&, size_t> mData;
    std::string mNeedle;
};

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>::Wait(const char* needle)
    : mNeedle(needle)
{
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>::Wait(std::string&& needle)
    : mNeedle(std::move(needle))
{
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>::Wait(const std::string& needle)
    : mNeedle(needle)
{
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>::Wait(Wait&& other)
    : mBuffer(std::move(other.mBuffer)), mData(std::move(other.mData)), mNeedle(std::move(other.mNeedle))
{
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>& Wait<NumberOfBuffers, MaxBufferSize>::Wait::operator=(Wait&& other)
{
    mBuffer = std::move(other.mBuffer);
    mData = std::move(other.mData);
    mNeedle = std::move(other.mNeedle);
    return *this;
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline Wait<NumberOfBuffers, MaxBufferSize>::~Wait()
{
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
inline void Wait<NumberOfBuffers, MaxBufferSize>::feed(std::shared_ptr<Buffer>&& buffer)
{
    if (!mBuffer) {
        // do we have what we're looking for right here?
        if (buffer->size() >= mNeedle.size()) {
            char* found = static_cast<char*>(memmem(buffer->data(), buffer->size(), &mNeedle[0], mNeedle.size()));
            if (found) {
                // yes
                mData.emit(std::move(buffer), found - reinterpret_cast<char*>(buffer->data()));
                return;
            }
        }
        // no, need to buffer up
        mBuffer = std::move(buffer);
    } else {
        // if this will exceed our max size, bail out
        if (mBuffer->size() + buffer->size() > MaxBufferSize) {
            mBuffer.reset();
            mData.emit(std::shared_ptr<Buffer>(), 0);
            return;
        }

        // append and check
        if (mBuffer->size() < MaxBufferSize) {
            // make a temporary buffer and copy our data
            auto buf = Pool<NumberOfBuffers, MaxBufferSize>::pool().get(MaxBufferSize);
            assert(buf);
            buf->assign(mBuffer->data(), mBuffer->size());
            mBuffer = std::move(buf);
        }
        // append our buffer
        mBuffer->append(buffer->data(), buffer->size());
        buffer.reset();

        // check if we have what we're looking for
        if (mBuffer->size() >= mNeedle.size()) {
            char* found = static_cast<char*>(memmem(mBuffer->data(), mBuffer->size(), &mNeedle[0], mNeedle.size()));
            if (found) {
                // yes
                mData.emit(std::move(mBuffer), found - reinterpret_cast<char*>(mBuffer->data()));
            }
        }
    }
}

template<size_t NumberOfBuffers, size_t MaxBufferSize>
event::Signal<std::shared_ptr<Buffer>&&, size_t>& Wait<NumberOfBuffers, MaxBufferSize>::onData()
{
    return mData;
}

}} // namespace reckoning::buffer

#endif // BUFFERWAIT_H
