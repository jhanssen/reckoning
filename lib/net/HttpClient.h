#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <net/TcpSocket.h>
#include <buffer/Buffer.h>
#include <util/Creatable.h>
#include <memory>
#include <vector>
#include <string>

namespace reckoning {
namespace net {

class HttpClient : public std::enable_shared_from_this<HttpClient>, public util::Creatable<HttpClient>
{
public:
    ~HttpClient();

    struct Headers : public std::vector<std::pair<std::string, std::string> >
    {
        template<typename T, typename U>
        void add(T key, U value);

        template<typename T>
        std::string find(T key) const;
    };

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

    void write(std::shared_ptr<buffer::Buffer>&& buffer);
    void write(const std::shared_ptr<buffer::Buffer>& buffer);
    void write(const uint8_t* data, size_t bytes);
    void write(const char* data, size_t bytes);

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

inline HttpClient::State HttpClient::state() const
{
    return mState;
}

inline const std::shared_ptr<TcpSocket>& HttpClient::socket() const
{
    return mSocket;
}

inline void HttpClient::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    if (mState != Connected || !mSocket)
        return;
    mSocket->write(buffer);
}

inline void HttpClient::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
    if (mState != Connected || !mSocket)
        return;
    mSocket->write(std::move(buffer));
}

inline void HttpClient::write(const uint8_t* data, size_t bytes)
{
    if (mState != Connected || !mSocket)
        return;
    mSocket->write(data, bytes);
}

inline void HttpClient::write(const char* data, size_t bytes)
{
    if (mState != Connected || !mSocket)
        return;
    mSocket->write(data, bytes);
}

template<typename T, typename U>
inline void HttpClient::Headers::add(T key, U value)
{
    push_back(std::make_pair(std::forward<typename std::decay<T>::type>(key), std::forward<typename std::decay<U>::type>(value)));
}

template<typename T>
std::string HttpClient::Headers::find(T key) const
{
    const std::string keystr = std::forward<typename std::decay<T>::type>(key);
    for (auto header : *this) {
        if (header.first == keystr)
            return header.second;
    }
    return std::string();
}

}} // namespace reckoning::net

#endif // HTTPCLIENT_H
