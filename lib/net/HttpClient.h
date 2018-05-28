#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <memory>
#include <net/TcpSocket.h>
#include <buffer/Buffer.h>
#include <util/Creatable.h>
#include <vector>
#include <string>

namespace reckoning {
namespace net {

class HttpClient : public std::enable_shared_from_this<HttpClient>, public util::Creatable<HttpClient>
{
public:
    ~HttpClient();

    typedef std::vector<std::pair<std::string, std::string> > Headers;

    struct Response
    {
        uint8_t status;
        std::string reason;
        Headers headers;
    };

    enum HttpVersion { v10, v11 };

    void connect(const std::string& host, uint16_t port);
    void close();
    void get(HttpVersion version, const std::string& query, const Headers& headers = Headers());
    void post(HttpVersion version, const std::string& query, const Headers& headers, std::shared_ptr<buffer::Buffer>&& body);
    void post(HttpVersion version, const std::string& query, const Headers& headers, const std::shared_ptr<buffer::Buffer>& body);
    void post(HttpVersion version, const std::string& query, const Headers& headers, const std::string& body);

    event::Signal<Response&&>& onResponse();
    event::Signal<std::shared_ptr<buffer::Buffer>&&>& onBodyData();
    event::Signal<>& onBodyEnd();

    enum State {
        Idle,
        Connected,
        Closed,
        Error
    };
    event::Signal<State>& onStateChanged();
    State state() const;

    const std::shared_ptr<TcpSocket>& socket() const;

protected:
    HttpClient();

private:
    void prepare(const char* method, HttpVersion version, const std::string& query, const Headers& headers);
    bool parseBody(std::shared_ptr<buffer::Buffer>&& buffer, size_t offset);

private:
    event::Signal<Response&&> mResponse;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mBodyData;
    event::Signal<> mBodyEnd;
    event::Signal<State> mStateChanged;
    State mState;
    std::shared_ptr<TcpSocket> mSocket;
    std::shared_ptr<buffer::Buffer> mHeaderBuffer, mBodyBuffer;
    std::string mTransferEncoding;
    int64_t mContentLength, mReceived, mChunkSize;
    size_t mChunkPrefix;
    bool mHeadersReceived, mChunked, mPendingBodyEnd;
};

inline HttpClient::HttpClient()
    : mState(Idle), mContentLength(-1), mReceived(0), mChunkSize(0), mChunkPrefix(0),
      mHeadersReceived(false), mChunked(false), mPendingBodyEnd(false)
{
}

inline HttpClient::~HttpClient()
{
}

inline event::Signal<HttpClient::Response&&>& HttpClient::onResponse()
{
    return mResponse;
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& HttpClient::onBodyData()
{
    return mBodyData;
}

inline event::Signal<>& HttpClient::onBodyEnd()
{
    return mBodyEnd;
}

inline event::Signal<HttpClient::State>& HttpClient::onStateChanged()
{
    return mStateChanged;
}

HttpClient::State HttpClient::state() const
{
    return mState;
}

const std::shared_ptr<TcpSocket>& HttpClient::socket() const
{
    return mSocket;
}

}} // namespace reckoning::net

#endif // HTTPCLIENT_H
