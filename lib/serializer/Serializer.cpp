#include <serializer/Serializer.h>
#include <buffer/Pool.h>

using namespace reckoning;
using namespace serializer;

void Serializer::realloc(size_t size)
{
    if (!mBuffer) {
        mBuffer = buffer::Pool<4, 16384>::pool().get(size);
        mBuffer->setSize(0);
        return;
    }

    size_t newSize;
    const auto step = mBuffer->max() * 2;
    if (step < 2048 * 1024 && mBuffer->size() + size < step) {
        newSize = step;
    } else {
        newSize = mBuffer->size() + (size * 2);
    }

    auto buffer = buffer::Pool<4, 16384>::pool().get(newSize);
    buffer->setSize(0);
    buffer->append(mBuffer);
    mBuffer = buffer;
}
