#ifndef BUFFER_H
#define BUFFER_H

#include <memory>
#include <cassert>

namespace reckoning {
namespace buffer {

class Buffer : public std::enable_shared_from_this<Buffer>
{
public:
    Buffer(uint8_t* data, size_t max);
    ~Buffer();

    uint8_t* data();
    const uint8_t* data() const;
    bool isInUse() const;

    void setSize(size_t sz);
    size_t size() const;

private:
    uint8_t* mData;
    size_t mSize, mMax;
    bool mOwned;
};

inline Buffer::Buffer(uint8_t* data, size_t max)
    : mData(data), mSize(max), mMax(max), mOwned(!data)
{
    if (mOwned) {
        assert(!mData);
        mData = reinterpret_cast<uint8_t*>(malloc(mMax));
    }
}

inline Buffer::~Buffer()
{
    if (mOwned) {
        free(mData);
    }
}

inline uint8_t* Buffer::data()
{
    return mData;
}

inline const uint8_t* Buffer::data() const
{
    return mData;
}

inline bool Buffer::isInUse() const
{
    auto buf = shared_from_this();
    assert(buf.use_count() >= 2);
    return buf.use_count() == 2;
}

inline void Buffer::setSize(size_t sz)
{
    assert(sz <= mMax);
    mSize = sz;
}

inline size_t Buffer::size() const
{
    return mSize;
}

}} // namespace reckoning::buffer

#endif
