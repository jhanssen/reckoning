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

struct HttpCurlInfo;

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
        uint16_t status;
        std::string reason;
        Headers headers;
    };

    enum Method { Get, Post };

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

    void init();

protected:
    HttpClient(const std::string& url, Method method = Get);
    HttpClient(const std::string& url, const Headers& headers = Headers(), Method method = Get);

private:
    static void ensureCurlInfo();

    void connect(const std::string& url, const Headers& headers, Method method);

private:
    std::string mUrl;
    Headers mHeaders;
    Method mMethod;

    event::Signal<Response&&> mResponse;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mBodyData;
    event::Signal<> mBodyEnd;
    event::Signal<State> mStateChanged;
    State mState;
};

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
