#ifndef BUFFER_H
#define BUFFER_H

#include <memory>
#include <cassert>
#include <util/Creatable.h>

namespace reckoning {
namespace buffer {

template<size_t NumberOfBuffers, size_t SizeOfBuffer>
class Pool;

class Buffer : public std::enable_shared_from_this<Buffer>, public util::Creatable<Buffer>
{
public:
    ~Buffer();

    uint8_t* data();
    const uint8_t* data() const;

    void setSize(size_t sz);
    size_t size() const;

    template<typename... Args>
    static std::shared_ptr<Buffer> concat(Args&&... args);

    using util::Creatable<Buffer>::create;

protected:
    Buffer(size_t max);
    Buffer(uint8_t* data, size_t max);

    static std::shared_ptr<Buffer> create(uint8_t* data, size_t max);
    bool isInUse() const;
    void setOwned(bool owned);

private:
    template<size_t NumberOfBuffers, size_t SizeOfBuffer>
    friend class Pool;

    uint8_t* mData;
    size_t mSize, mMax;
    bool mOwned;
};

inline Buffer::Buffer(size_t max)
    : mSize(max), mMax(max), mOwned(true)
{
    mData = reinterpret_cast<uint8_t*>(malloc(mMax));
}

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

std::shared_ptr<Buffer> Buffer::create(uint8_t* data, size_t max)
{
    return util::Creatable<Buffer>::create(nullptr, max);
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
    return buf.use_count() > 2;
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

inline void Buffer::setOwned(bool owned)
{
    mOwned = owned;
}

namespace detail {
template<typename... Args>
struct Concat;

template<typename First, typename... Args>
struct Concat<First, Args...>
{
    static void concat(uint8_t*& blob, size_t& used, size_t& size, First&& first, Args&&... args)
    {
        if (used + first->size() > size) {
            // realloc
            blob = reinterpret_cast<uint8_t*>(realloc(blob, size + (first->size() * 2)));
            size = size + (first->size() * 2);
        }
        memcpy(blob + used, first->data(), first->size());
        used += first->size();
        Concat<Args...>::concat(blob, used, size, std::forward<Args>(args)...);
    }
};

template<>
struct Concat<>
{
    static void concat(uint8_t*& blob, size_t& used, size_t& size)
    {
    }
};
} // namespace detail

template<typename... Args>
inline std::shared_ptr<Buffer> Buffer::concat(Args&&... args)
{
    uint8_t* blob = nullptr;
    size_t used = 0, size = 0;
    detail::Concat<Args...>::concat(blob, used, size, std::forward<Args>(args)...);
    auto buf = Buffer::create(blob, size);
    buf->setSize(used);
    buf->setOwned(true);
    return buf;
}

}} // namespace reckoning::buffer

#endif
