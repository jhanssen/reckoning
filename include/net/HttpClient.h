#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <buffer/Buffer.h>
#include <event/Signal.h>
#include <util/Creatable.h>
#include <memory>
#include <vector>
#include <string>

typedef void CURL;
typedef void CURLM;
typedef int curl_socket_t;

namespace reckoning {
namespace net {

struct HttpCurlInfo;
struct HttpConnectionInfo;
class CurlTimer;

class HttpClient : public std::enable_shared_from_this<HttpClient>, public util::Creatable<HttpClient>
{
public:
    ~HttpClient();

    struct Headers : public std::vector<std::pair<std::string, std::string> >
    {
        template<typename T, typename U>
        void add(T&& key, U&& value);

        template<typename T>
        std::string find(T&& key) const;

        std::string toString() const;
    };

    struct Response
    {
        uint16_t status;
        std::string reason;
        Headers headers;
    };

    enum Flags { Post = 0x1 };

    void write(std::shared_ptr<buffer::Buffer>&& buffer);
    void write(const std::shared_ptr<buffer::Buffer>& buffer);
    void write(const uint8_t* data, size_t bytes);
    void write(const char* data, size_t bytes);
    void endWrite();

    event::Signal<Response&&>& onResponse();
    event::Signal<std::shared_ptr<buffer::Buffer>&&>& onBodyData();
    event::Signal<>& onComplete();
    event::Signal<std::string&&>& onError();

    void init();

protected:
    HttpClient(const std::string& url);
    HttpClient(const std::string& url, uint8_t flags);
    HttpClient(const std::string& url, const Headers& headers);
    HttpClient(const std::string& url, const Headers& headers, uint8_t flags);

private:
    static void ensureCurlInfo();

    void connect(const std::string& url, const Headers& headers, uint8_t flags);
    static size_t easyReadCallback(void *dest, size_t size, size_t nmemb, void *userp);
    static size_t easyHeaderCallback(char *buffer, size_t size, size_t nmemb, void *userdata);
    static size_t easyWriteCallback(void *ptr, size_t size, size_t nmemb, void *data);
    static int multiSocketCallback(CURL* easy, curl_socket_t socket, int what, void* globalSocketData, void* perSocketData);
    static int multiTimerCallback(CURLM* multi, long timeoutMs, void* timerData);
    static void socketEventCallback(int fd, uint8_t flags);
    static void checkMultiInfo();

private:
    std::string mUrl;
    Headers mHeaders;
    uint8_t mFlags;

    event::Signal<Response&&> mResponse;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mBodyData;
    event::Signal<> mComplete;
    event::Signal<std::string&&> mError;

    bool mResponseReceived { false };
    bool mPaused { false };
    bool mWriteEnd { false };
    size_t mBufferPos { 0 };
    size_t mBufferOffset { 0 };
    std::vector<std::shared_ptr<buffer::Buffer> > mBuffers;
    bool mWaitingForRead { false }, mWaitingForWrite { false };

    HttpConnectionInfo* mConnectionInfo { nullptr };

    friend class CurlTimer;
};

inline event::Signal<HttpClient::Response&&>& HttpClient::onResponse()
{
    return mResponse;
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& HttpClient::onBodyData()
{
    return mBodyData;
}

inline event::Signal<>& HttpClient::onComplete()
{
    return mComplete;
}

inline event::Signal<std::string&&>& HttpClient::onError()
{
    return mError;
}

template<typename T, typename U>
inline void HttpClient::Headers::add(T&& key, U&& value)
{
    push_back(std::make_pair(std::forward<typename std::decay<T>::type>(key), std::forward<typename std::decay<U>::type>(value)));
}

template<typename T>
std::string HttpClient::Headers::find(T&& key) const
{
    const std::string keystr = std::forward<typename std::decay<T>::type>(key);
    for (auto header : *this) {
        if (header.first == keystr)
            return header.second;
    }
    return std::string();
}

inline std::string HttpClient::Headers::toString() const
{
    std::string str;
    for (const auto& h : *this) {
        str += h.first + ": " + h.second + "\r\n";
    }
    return str;
}

}} // namespace reckoning::net

#endif // HTTPCLIENT_H
