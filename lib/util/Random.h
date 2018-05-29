#ifndef RANDOM_H
#define RANDOM_H

#include <random>
#include <buffer/Buffer.h>

namespace reckoning {
namespace util {

class Random
{
public:
    ~Random();

    void fill(uint8_t* buf, size_t size);
    void fill(std::shared_ptr<buffer::Buffer>& buffer);
    int generate();

    static Random& random();

private:
    Random();

    std::random_device mRd;
    std::mt19937 mGen;
    std::uniform_int_distribution<int> mDist;

    thread_local static Random tRandom;
};

inline Random::Random()
    : mGen(mRd()), mDist(0, 255)
{
}

inline Random::~Random()
{
}

inline void Random::fill(uint8_t* buf, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        *(buf + i) = mDist(mGen);
    }
}

inline void Random::fill(std::shared_ptr<buffer::Buffer>& buffer)
{
    if (!buffer)
        return;
    fill(buffer->data(), buffer->size());
}

inline int Random::generate()
{
    return mDist(mGen);
}

inline Random& Random::random()
{
    return tRandom;
}

}} // namespace reckoning::util

#endif // RANDOM_H
