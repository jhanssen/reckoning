#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <cstdint>
#include <string>
#include <vector>

#include <fs/Path.h>
#include <buffer/Buffer.h>
#include <event/Signal.h>

namespace reckoning {
namespace serializer {

class Serializer
{
public:
    enum {
        Read = 0x1,
        Write = 0x2
    };

    Serializer(const fs::Path& path, uint8_t mode = Serializer::Read);
    ~Serializer();

    enum ValidType {
        Invalid,
        NoData,
        DataNotReady,
        DataReady,
    };
    ValidType isValid() const;
    event::Signal<ValidType>& onValid();
    uint8_t mode() const;

    // serialize in
    Serializer& operator<<(const std::string& str);

    template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type* = nullptr>
    Serializer& operator<<(T num);

    template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type* = nullptr>
    Serializer& operator<<(T num);

    // serialize out
    Serializer& operator>>(std::string& str);

    template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type* = nullptr>
    Serializer& operator>>(T& num);

    template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type* = nullptr>
    Serializer& operator>>(T& num);

private:
    void read(void* data, size_t size);
    void write(const void* data, size_t size);
    void realloc(size_t size);

private:
    fs::Path mPath;
    ValidType mValid;
    size_t mReadPos;
    event::Signal<ValidType> mOnValid;
    std::shared_ptr<buffer::Buffer> mBuffer;
    uint8_t mMode;
};

inline Serializer::Serializer(const fs::Path& path, uint8_t mode)
    : mPath(path), mReadPos(0), mMode(mode)
{
    if (mMode & Read)
        mBuffer = mPath.read();
    if (!mBuffer) {
        if (mPath.type() == fs::Path::Nonexistant) {
            realloc(16384);
            mValid = NoData;
        } else {
            mPath.clear();
            mValid = Invalid;
        }
    } else {
        mValid = DataReady;
    }
}

inline Serializer::~Serializer()
{
    if (mMode & Write && !mPath.isEmpty()) {
        mPath.write(mBuffer);
    }
}

inline uint8_t Serializer::mode() const
{
    return mMode;
}

inline event::Signal<Serializer::ValidType>& Serializer::onValid()
{
    return mOnValid;
}

inline void Serializer::read(void* data, size_t size)
{
    assert(mValid == DataReady || mValid == NoData);
    assert(mReadPos + size <= mBuffer->size());
    assert(mMode & Read);
    memcpy(data, mBuffer->data() + mReadPos, size);
    mReadPos += size;
}

inline void Serializer::write(const void* data, size_t size)
{
    assert(mValid == DataReady || mValid == NoData);
    assert(mMode & Write);
    if (!mBuffer || mBuffer->size() + size >= mBuffer->max())
        realloc(size);
    mBuffer->append(reinterpret_cast<const uint8_t*>(data), size);
}

template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type*>
inline Serializer& Serializer::operator<<(T num)
{
    // this is not endian safe, but eh
    write(&num, sizeof(T));
    return *this;
}

template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type*>
inline Serializer& Serializer::operator<<(T num)
{
    write(&num, sizeof(T));
    return *this;
}

inline Serializer& Serializer::operator<<(const std::string& str)
{
    this->operator<<(str.size());
    write(&str[0], str.size());
    return *this;
}

template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, T>::type*>
inline Serializer& Serializer::operator>>(T& num)
{
    read(&num, sizeof(T));
    return *this;
}

template<typename T, typename std::enable_if<std::is_enum<T>::value, T>::type*>
inline Serializer& Serializer::operator>>(T& num)
{
    read(&num, sizeof(T));
    return *this;
}

inline Serializer& Serializer::operator>>(std::string& str)
{
    size_t sz;
    read(&sz, sizeof(size_t));
    str.resize(sz);
    read(&str[0], sz);
    return *this;
}

}} // namespace reckoning::serializer

#endif // SERIALIZER_H
