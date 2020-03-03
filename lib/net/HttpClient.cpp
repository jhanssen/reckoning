#include <net/HttpClient.h>
#include <buffer/Pool.h>
#include <buffer/Builder.h>
#include <log/Log.h>
#include <curl/curl.h>

using namespace reckoning;
using namespace reckoning::net;

struct HttpConnectionInfo
{
    CURL* easy;
};

namespace reckoning {
namespace net {
struct HttpCurlInfo
{
    CURLM* multi;
};
}} // namespace reckoning::net

static int multiSocketCallback(CURL* easy, curl_socket_t socket, int what, void* globalSocketData, void* perSocketData)
{
}

static int multiTimerCallback(CURLM* multi, long timeoutMs, void* timerData)
{
}

thread_local std::shared_ptr<HttpCurlInfo> HttpClient::sCurlInfo;

HttpClient::HttpClient(const std::string& url, Method method)
{
    ensureCurlInfo();
    connect(url, Headers(), method);
}

HttpClient::HttpClient(const std::string& url, const Headers& headers, Method method)
{
    ensureCurlInfo();
    connect(url, headers, method);
}

void HttpClient::ensureCurlInfo()
{
    if (sCurlInfo)
        return;

    auto multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, multiSocketCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, multiTimerCallback);

    sCurlInfo = std::make_shared<HttpCurlInfo>();
    sCurlInfo->multi = multi;
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
