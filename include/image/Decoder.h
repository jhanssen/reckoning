#ifndef DECODER_H
#define DECODER_H

#include <buffer/Buffer.h>
#include <then/Then.h>
#include <pool/Pool.h>
#include <util/Creatable.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace reckoning {
namespace image {

class Decoder : public util::Creatable<Decoder>
{
public:
    ~Decoder();

    struct Image
    {
        uint32_t width { 0 };
        uint32_t height { 0 };
        uint32_t bpl { 0 };
        uint8_t depth { 0 };
        bool alpha { false };
        std::shared_ptr<buffer::Buffer> data;
    };

    then::Then<Image>& decode(std::shared_ptr<buffer::Buffer>&& buffer);

protected:
    Decoder();

private:
    struct Job : public util::Creatable<Job>
    {
        std::shared_ptr<buffer::Buffer> data;
        then::Then<Image> then;
    };

    std::mutex mMutex;
    std::condition_variable mCond;
    std::vector<std::shared_ptr<Job > > mJobs;
    std::thread mThread;
    bool mStopped { false };

private:
    Decoder(const Decoder&) = delete;
};

inline then::Then<Decoder::Image>& Decoder::decode(std::shared_ptr<buffer::Buffer>&& buffer)
{
    auto job = pool::Pool<Job, 10>::pool().get();
    job->data = std::move(buffer);
    {
        std::unique_lock<std::mutex> locker(mMutex);
        mJobs.push_back(job);
        mCond.notify_one();
    }
    return job->then;
}

}} // namespace reckoning::image

#endif
