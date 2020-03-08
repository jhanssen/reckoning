#ifndef FETCH_H
#define FETCH_H

#include <buffer/Buffer.h>
#include <buffer/Pool.h>
#include <event/Then.h>
#include <pool/Pool.h>
#include <util/Creatable.h>
#include <net/HttpClient.h>
#include <net/TcpSocket.h>
#include <memory>
#include <string>

namespace reckoning {
namespace net {

class Fetch : public util::Creatable<Fetch>
{
public:
    event::Then<std::shared_ptr<buffer::Buffer> >& fetch(const std::string& uri);

protected:
    Fetch() { };

private:
    struct Job : public util::Creatable<Job>
    {
        std::shared_ptr<HttpClient> http;
        std::shared_ptr<buffer::Buffer> buffer;
        event::Then<std::shared_ptr<buffer::Buffer> > then;

        void clear()
        {
            http = {};
            buffer = {};
        }
    };

private:
    Fetch(const Fetch&) = delete;
};

inline event::Then<std::shared_ptr<buffer::Buffer> >& Fetch::fetch(const std::string& uri)
{
    auto job = pool::Pool<Job, 10>::pool().get();
    {
        // does this look like a file path?
        if (uri.find("://") == std::string::npos) {
            // yes, load from file
            auto buf = buffer::Buffer::fromFile(uri);
            job->then.resolve(std::move(buf));
            job->clear();
        } else {
            // no, http?
            job->http = HttpClient::create(uri);
            job->http->onBodyData().connect([job](std::shared_ptr<buffer::Buffer>&& buf) {
                job->buffer = buffer::Pool<20, TcpSocket::BufferSize>::pool().concat(std::move(job->buffer), std::move(buf));
            });
            job->http->onComplete().connect([job]() {
                job->then.resolve(std::move(job->buffer));
                job->clear();
            });
        }
    }
    return job->then;
}

}} // namespace reckoning::net

#endif
