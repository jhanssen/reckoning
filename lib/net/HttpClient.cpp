#include <net/HttpClient.h>
#include <buffer/Pool.h>
#include <buffer/Builder.h>
#include <event/Loop.h>
#include <log/Log.h>
#include <curl/curl.h>

using namespace reckoning;
using namespace reckoning::net;

thread_local std::shared_ptr<HttpCurlInfo> tCurlInfo;

class CurlTimer;

static void checkMultiInfo();

namespace reckoning {
namespace net {
struct HttpCurlInfo
{
    CURLM* multi;
    std::weak_ptr<event::Loop> loop;
    std::shared_ptr<CurlTimer> timer;
};
}} // namespace reckoning::net

class CurlTimer : public event::Loop::Timer
{
public:
    CurlTimer(std::chrono::milliseconds timeout)
        : event::Loop::Timer(timeout)
    {
    }

protected:
    void execute()
    {
        curl_multi_socket_action(tCurlInfo->multi, CURL_SOCKET_TIMEOUT, 0, nullptr);
        checkMultiInfo();
    }
};

struct HttpConnectionInfo
{
    CURL* easy;
    std::string url;
    std::weak_ptr<HttpClient> http;
    char error[CURL_ERROR_SIZE];
};

struct HttpSocketInfo
{
    CURL* easy;
    std::weak_ptr<HttpClient> http;
};

static uint8_t curlToReckoning(int what)
{
    switch (what) {
    case CURL_POLL_IN:
        return event::Loop::FdRead;
    case CURL_POLL_OUT:
        return event::Loop::FdWrite;
    case CURL_POLL_INOUT:
        return event::Loop::FdRead | event::Loop::FdWrite;
    }
    return 0;
}

static void socketEventCallback(int fd, uint8_t flags)
{
    int action = 0;
    if (flags & event::Loop::FdRead)
        action |= CURL_CSELECT_IN;
    if (flags & event::Loop::FdWrite)
        action |= CURL_CSELECT_OUT;
    curl_multi_socket_action(tCurlInfo->multi, fd, action, nullptr);
    checkMultiInfo();
}

static int multiSocketCallback(CURL* easy, curl_socket_t socket, int what, void* globalSocketData, void* perSocketData)
{
    HttpCurlInfo* curlInfo = static_cast<HttpCurlInfo*>(globalSocketData);
    auto loop = curlInfo->loop.lock();
    if (!loop) {
        // bad stuff
        return 0;
    }
    switch (what) {
    case CURL_POLL_REMOVE:
        // remove this fd
        loop->removeFd(socket);
        break;
    default:
        if (!perSocketData) {
            // brand new socket
            HttpConnectionInfo* connInfo;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &connInfo);

            HttpSocketInfo* socketInfo = new HttpSocketInfo;
            socketInfo->easy = easy;
            socketInfo->http = connInfo->http;
            curl_multi_assign(curlInfo->multi, socket, socketInfo);
            loop->addFd(socket, curlToReckoning(what), std::bind(socketEventCallback, std::placeholders::_1, std::placeholders::_2));
        } else {
            loop->updateFd(socket, curlToReckoning(what));
        }
        break;
    }

    return 0;
}

static int multiTimerCallback(CURLM* multi, long timeoutMs, void* timerData)
{
    HttpCurlInfo* curlInfo = static_cast<HttpCurlInfo*>(timerData);
    auto loop = curlInfo->loop.lock();
    if (!loop) {
        // bad stuff
        return 0;
    }
    if (!curlInfo->timer) {
        curlInfo->timer = std::make_shared<CurlTimer>(std::chrono::milliseconds(timeoutMs));
    } else {
        curlInfo->timer->stop();
        curlInfo->timer->updateTimeout(std::chrono::milliseconds(timeoutMs));
    }
    loop->addTimer(curlInfo->timer);

    return 0;
}

static size_t easyWrite(void *ptr, size_t size, size_t nmemb, void *data)
{
    return 0;
}

static size_t easyRead(void *dest, size_t size, size_t nmemb, void *userp)
{
    return 0;
}

HttpClient::HttpClient(const std::string& url, Method method)
    : mUrl(url), mMethod(method)
{
}

HttpClient::HttpClient(const std::string& url, const Headers& headers, Method method)
    : mUrl(url), mHeaders(headers), mMethod(method)
{
}

void HttpClient::init()
{
    ensureCurlInfo();
    connect(mUrl, mHeaders, mMethod);
}

void HttpClient::connect(const std::string& url, const Headers& headers, Method method)
{
    HttpConnectionInfo* conn = new HttpConnectionInfo;
    conn->easy = curl_easy_init();
    conn->url = url;
    conn->http = shared_from_this();
    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url.c_str());
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, easyWrite);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1);
    if (method == Post) {
        curl_easy_setopt(conn->easy, CURLOPT_POST, 1);
        curl_easy_setopt(conn->easy, CURLOPT_READFUNCTION, easyRead);
        curl_easy_setopt(conn->easy, CURLOPT_READDATA, conn);
    }
    auto rc = curl_multi_add_handle(tCurlInfo->multi, conn->easy);
    if (rc != CURLM_OK) {
        // bad
        curl_easy_cleanup(conn->easy);
        delete conn;
    }

    //curl_easy_setopt(conn->easy,
}

void HttpClient::ensureCurlInfo()
{
    if (tCurlInfo)
        return;

    tCurlInfo = std::make_shared<HttpCurlInfo>();

    auto multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, multiSocketCallback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, tCurlInfo.get());
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, multiTimerCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, tCurlInfo.get());

    tCurlInfo->multi = multi;
    tCurlInfo->loop = event::Loop::loop();
}

static void checkMultiInfo()
{
    int msgs_left;
    CURLMsg *msg;
    CURL *easy;
    CURLcode res;
    HttpConnectionInfo* conn;

    auto multi = tCurlInfo->multi;

    while ((msg = curl_multi_info_read(multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            easy = msg->easy_handle;
            res = msg->data.result;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
            delete conn;
        }
    }
}

void HttpClient::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
}

void HttpClient::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
}

void HttpClient::write(const uint8_t* data, size_t bytes)
{
}

void HttpClient::write(const char* data, size_t bytes)
{
}
