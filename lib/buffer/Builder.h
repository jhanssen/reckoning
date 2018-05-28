#ifndef BUFFERBUILDER_H
#define BUFFERBUILDER_H

#include <buffer/Buffer.h>
#include <string>

namespace reckoning {
namespace buffer {

class Builder
{
public:
    Builder(const std::shared_ptr<Buffer>& buffer);
    ~Builder();

    Builder& operator<<(const char* str);
    Builder& operator<<(const std::string& str);

    template<typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
    Builder& operator<<(T num);

    void flush();

    bool overflow() const;

private:
    const std::shared_ptr<Buffer>& mBuffer;
    char* mData;
    size_t mOffset;
    bool mOverflow;
};

inline Builder::Builder(const std::shared_ptr<Buffer>& buffer)
    : mBuffer(buffer), mData(reinterpret_cast<char*>(buffer->data())), mOffset(0), mOverflow(false)
{
}

inline Builder::~Builder()
{
    flush();
}

inline void Builder::flush()
{
    // zero terminate
    if (mData) {
        *(mData + mOffset) = '\0';
        mBuffer->setSize(mOffset);
        mData = nullptr;
    }
}

inline bool Builder::overflow() const
{
    return mOverflow;
}

inline Builder& Builder::operator<<(const char* str)
{
    const size_t sz = strlen(str);
    if (mOffset + sz >= mBuffer->max()) {
        mOverflow = true;
        return *this;
    }
    memcpy(mData + mOffset, str, sz);
    mOffset += sz;
    return *this;
}

inline Builder& Builder::operator<<(const std::string& str)
{
    const size_t sz = str.size();
    if (mOffset + sz >= mBuffer->max()) {
        mOverflow = true;
        return *this;
    }
    memcpy(mData + mOffset, &str[0], sz);
    mOffset += sz;
    return *this;
}

template<typename T, typename std::enable_if<std::is_integral<T>::value, T>::type*>
inline Builder& Builder::operator<<(T num)
{
    return operator<<(std::to_string(num));
}

}} // namespace reckoning::buffer

#endif
