#ifndef BUFFER_H
#define BUFFER_H

#include <memory>
#include <cassert>

namespace reckoning {
namespace buffer {

template<size_t Size>
class Buffer : public std::enable_shared_from_this<Buffer<Size> >
{
public:
    Buffer(size_t size = 0);

    uint8_t* data();
    const uint8_t* data() const;
    bool isInUse() const;

    void setSize(size_t sz);
    size_t size() const;

private:
    uint8_t mData[Size];
    size_t mSize;
};

template<size_t Size>
inline Buffer<Size>::Buffer(size_t sz)
    : mSize(sz)
{
}

template<size_t Size>
inline uint8_t* Buffer<Size>::data()
{
    return mData;
}

template<size_t Size>
inline const uint8_t* Buffer<Size>::data() const
{
    return mData;
}

template<size_t Size>
inline bool Buffer<Size>::isInUse() const
{
    auto buf = Buffer<Size>::shared_from_this();
    assert(buf.use_count() >= 2);
    return buf.use_count() == 2;
}

template<size_t Size>
inline void Buffer<Size>::setSize(size_t sz)
{
    mSize = sz;
}

template<size_t Size>
inline size_t Buffer<Size>::size() const
{
    return mSize;
}

}} // namespace reckoning::buffer

#endif
