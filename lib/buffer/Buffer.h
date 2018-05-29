#ifndef BUFFER_H
#define BUFFER_H

#include <memory>
#include <cassert>
#include <string.h>
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
    size_t max() const;

    void assign(const uint8_t* data, size_t size);
    void append(const uint8_t* data, size_t size);

    template<typename... Args>
    static std::shared_ptr<Buffer> concat(Args&&... args);

    template<size_t MaxSize, typename... Args>
    static size_t concat(uint8_t* blob, Args&&... args);

    using util::Creatable<Buffer>::create;

protected:
    enum NotOwnedTag { NotOwned };

    Buffer(size_t max);
    Buffer(uint8_t* data, size_t max);
    Buffer(NotOwnedTag, uint8_t* data, size_t max);

    static std::shared_ptr<Buffer> create(NotOwnedTag, uint8_t* data, size_t max);
    bool isInUse() const;

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
    : mData(data), mSize(max), mMax(max), mOwned(true)
{
    if (!mData) {
        mData = reinterpret_cast<uint8_t*>(malloc(mMax));
    }
}

inline Buffer::Buffer(NotOwnedTag, uint8_t* data, size_t max)
    : mData(data), mSize(max), mMax(max), mOwned(false)
{
}

inline Buffer::~Buffer()
{
    if (mOwned) {
        free(mData);
    }
}

inline std::shared_ptr<Buffer> Buffer::create(NotOwnedTag, uint8_t* data, size_t max)
{
    return util::Creatable<Buffer>::create(NotOwned, data, max);
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

inline size_t Buffer::max() const
{
    return mMax;
}

inline size_t Buffer::size() const
{
    return mSize;
}

inline void Buffer::assign(const uint8_t* data, size_t size)
{
    assert(size <= mMax);
    memcpy(mData, data, size);
    mSize = size;
}

inline void Buffer::append(const uint8_t* data, size_t size)
{
    assert(mSize + size <= mMax);
    memcpy(mData + mSize, data, size);
    mSize += size;
}

namespace detail {
template<size_t MaxSize, typename... Args>
struct Concat;

template<size_t MaxSize, typename First, typename... Args>
struct Concat<MaxSize, First, Args...>
{
    static void concat(uint8_t*& blob, size_t& used, size_t& size, First&& first, Args&&... args)
    {
        const size_t fsz = first->size();
        if (fsz > 0) {
            if (used + fsz > size) {
                // realloc
                if constexpr (!MaxSize) {
                    blob = reinterpret_cast<uint8_t*>(realloc(blob, size + (fsz * 2)));
                    size = size + (fsz * 2);
                } else {
                    blob = nullptr;
                    return;
                }
            }
            memcpy(blob + used, first->data(), fsz);
            used += fsz;
        }
        Concat<MaxSize, Args...>::concat(blob, used, size, std::forward<Args>(args)...);
    }
};

template<size_t MaxSize>
struct Concat<MaxSize>
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
    detail::Concat<0, Args...>::concat(blob, used, size, std::forward<Args>(args)...);
    assert(blob != nullptr);
    auto buf = Buffer::create(blob, size);
    buf->setSize(used);
    return buf;
}

template<size_t MaxSize, typename... Args>
inline size_t Buffer::concat(uint8_t* b, Args&&... args)
{
    uint8_t* blob = b;
    size_t used = 0, size = MaxSize;
    detail::Concat<MaxSize, Args...>::concat(blob, used, size, std::forward<Args>(args)...);
    assert(size == MaxSize);
    if (blob == nullptr) {
        return 0;
    }
    return used;
}

}} // namespace reckoning::buffer

#endif
