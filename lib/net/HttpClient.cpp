#include <net/HttpClient.h>
#include <buffer/Pool.h>
#include <buffer/Builder.h>
#include <event/Loop.h>
#include <log/Log.h>
#include <net/TcpSocket.h>
#include <util/Socket.h>
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <regex>

using namespace reckoning;
using namespace reckoning::net;

thread_local std::shared_ptr<HttpCurlInfo> tCurlInfo;

class CurlTimer;
struct HttpSocketInfo;

namespace reckoning {
namespace net {
struct HttpCurlInfo
{
    CURLM* multi { nullptr };
    std::weak_ptr<event::Loop> loop;
    std::shared_ptr<CurlTimer> timer;
};

struct HttpConnectionInfo
{
    CURL* easy { nullptr };
    std::string url;
    std::weak_ptr<HttpClient> http;
    char error[CURL_ERROR_SIZE];
    curl_slist* outHeaders { nullptr };

    uint16_t status { 0 };
    SSL* ssl { nullptr };
    std::string reason;
    HttpClient::Headers headers;

    HttpSocketInfo* socketInfo { nullptr };
};

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
        int running;
        curl_multi_socket_action(tCurlInfo->multi, CURL_SOCKET_TIMEOUT, 0, &running);
        HttpClient::checkMultiInfo();
    }
};
}} // namespace reckoning::net

struct HttpSocketInfo
{
    CURL* easy { nullptr };
    int fd { -1 };
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

static int sslidx = SSL_CTX_get_ex_new_index(0, &sslidx, nullptr, nullptr, nullptr);

static int sslVerifyPeerCallback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
    SSL *ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(x509_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
    SSL_CTX *sslctx = SSL_get_SSL_CTX(ssl);
    HttpConnectionInfo* connInfo = static_cast<HttpConnectionInfo*>(SSL_CTX_get_ex_data(sslctx, sslidx));
    connInfo->ssl = ssl;
    return preverify_ok;
}

static CURLcode sslCtxCallback(CURL *curl, void *ssl_ctx, void *userptr)
{
    SSL_CTX* ctx = static_cast<SSL_CTX*>(ssl_ctx);
    SSL_CTX_set_ex_data(ctx, sslidx, userptr);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, sslVerifyPeerCallback);
    return CURLE_OK;
}

HttpClient::HttpClient(const std::string& url)
    : mUrl(url), mFlags(0)
{
}

HttpClient::HttpClient(const std::string& url, uint8_t flags)
    : mUrl(url), mFlags(flags)
{
}

HttpClient::HttpClient(const std::string& url, const Headers& headers)
    : mUrl(url), mHeaders(headers), mFlags(0)
{
}

HttpClient::HttpClient(const std::string& url, const Headers& headers, uint8_t flags)
    : mUrl(url), mHeaders(headers), mFlags(flags)
{
}

HttpClient::~HttpClient()
{
}

void HttpClient::init()
{
    if (mConnectionInfo)
        return;
    ensureCurlInfo();
    connect(mUrl, mHeaders, mFlags);
}

void HttpClient::connect(const std::string& url, const Headers& headers, uint8_t flags)
{
    HttpConnectionInfo* conn = new HttpConnectionInfo;
    conn->error[0] = '\0';
    conn->easy = curl_easy_init();
    conn->url = url;
    conn->http = shared_from_this();
    if (!headers.empty()) {
        for (const auto& h : headers) {
            const std::string header = h.first + ": " + h.second;
            conn->outHeaders = curl_slist_append(conn->outHeaders, header.c_str());
        }
        curl_easy_setopt(conn->easy, CURLOPT_HTTPHEADER, conn->outHeaders);
    }
    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url.c_str());
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, HttpClient::easyWriteCallback);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_HEADERFUNCTION, HttpClient::easyHeaderCallback);
    curl_easy_setopt(conn->easy, CURLOPT_HEADERDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1);
    if (flags & WebSocket) {
        curl_easy_setopt(conn->easy, CURLOPT_FORBID_REUSE, 1);
        curl_easy_setopt(conn->easy, CURLOPT_SSL_ENABLE_ALPN, 0);
        curl_easy_setopt(conn->easy, CURLOPT_SSL_ENABLE_NPN, 0);
        curl_easy_setopt(conn->easy, CURLOPT_SSL_CTX_DATA, conn);
        curl_easy_setopt(conn->easy, CURLOPT_SSL_CTX_FUNCTION, sslCtxCallback);
    }
    if (flags & Post) {
        curl_easy_setopt(conn->easy, CURLOPT_POST, 1);
        curl_easy_setopt(conn->easy, CURLOPT_READFUNCTION, HttpClient::easyReadCallback);
        curl_easy_setopt(conn->easy, CURLOPT_READDATA, conn);
    }
    auto rc = curl_multi_add_handle(tCurlInfo->multi, conn->easy);
    if (rc != CURLM_OK) {
        // bad
        curl_easy_cleanup(conn->easy);
        if (conn->outHeaders) {
            curl_slist_free_all(conn->outHeaders);
        }
        delete conn;
        return;
    }

    mConnectionInfo = conn;
}

void HttpClient::ensureCurlInfo()
{
    if (tCurlInfo)
        return;

    tCurlInfo = std::make_shared<HttpCurlInfo>();

    auto multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, HttpClient::multiSocketCallback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, tCurlInfo.get());
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, HttpClient::multiTimerCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, tCurlInfo.get());

    tCurlInfo->multi = multi;
    tCurlInfo->loop = event::Loop::loop();
}

void HttpClient::socketEventCallback(int fd, uint8_t flags)
{
    int action = 0;
    if (flags & event::Loop::FdRead)
        action |= CURL_CSELECT_IN;
    if (flags & event::Loop::FdWrite)
        action |= CURL_CSELECT_OUT;
    int running = 0;
    curl_multi_socket_action(tCurlInfo->multi, fd, action, &running);
    HttpClient::checkMultiInfo();
    if (running <= 0) {
        // we're done!
        if (tCurlInfo->timer) {
            tCurlInfo->timer->stop();
            tCurlInfo->timer.reset();
        }
    }
}

int HttpClient::multiSocketCallback(CURL* easy, curl_socket_t socket, int what, void* globalSocketData, void* perSocketData)
{
    HttpCurlInfo* curlInfo = static_cast<HttpCurlInfo*>(globalSocketData);
    auto loop = curlInfo->loop.lock();
    if (!loop) {
        // bad stuff
        return 0;
    }
    switch (what) {
    case CURL_POLL_REMOVE: {
        // remove this fd
        loop->removeFd(socket);
        HttpSocketInfo* socketInfo = static_cast<HttpSocketInfo*>(perSocketData);
        delete socketInfo;
        break; }
    default: {
        if (!perSocketData) {
            // brand new socket
            HttpConnectionInfo* connInfo;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &connInfo);

            HttpSocketInfo* socketInfo = new HttpSocketInfo;
            socketInfo->easy = easy;
            socketInfo->http = connInfo->http;
            socketInfo->fd = socket;
            connInfo->socketInfo = socketInfo;
            curl_multi_assign(curlInfo->multi, socket, socketInfo);
            loop->addFd(socket, curlToReckoning(what), std::bind(socketEventCallback, std::placeholders::_1, std::placeholders::_2));
        } else {
            HttpSocketInfo* socketInfo = static_cast<HttpSocketInfo*>(perSocketData);
            if (socket != socketInfo->fd) {
                // remove and readd
                loop->removeFd(socket);
                curl_multi_assign(curlInfo->multi, socket, socketInfo);
                loop->addFd(socket, curlToReckoning(what), std::bind(socketEventCallback, std::placeholders::_1, std::placeholders::_2));
                socketInfo->fd = socket;
            } else {
                loop->updateFd(socket, curlToReckoning(what));
            }
        }
        break; }
    }

    return 0;
}

int HttpClient::multiTimerCallback(CURLM* multi, long timeoutMs, void* timerData)
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

size_t HttpClient::easyWriteCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
    auto buffer = buffer::Pool<20, net::TcpSocket::BufferSize>::pool().get(size * nmemb);
    buffer->assign(static_cast<uint8_t*>(ptr), size * nmemb);

    HttpConnectionInfo* conn = static_cast<HttpConnectionInfo*>(data);
    auto http = conn->http.lock();
    if (!http) {
        // failure
        return 0;
    }

    http->mBodyData.emit(std::move(buffer));

    return size * nmemb;
}

size_t HttpClient::easyHeaderCallback(char *buffer, size_t size, size_t nmemb, void *userdata)
{
    const std::string header(buffer, size * nmemb);

    auto trim = [&header]() {
        size_t n = header.size();
        while (n > 0 && isspace(header[n - 1]))
            --n;
        return n;
    };

    const size_t nsize = trim();

    HttpConnectionInfo* conn = static_cast<HttpConnectionInfo*>(userdata);
    if (conn->headers.empty() && conn->status == 0) {
        // assume HTTP line?
        std::regex headerrx("^HTTP\\/\\d\\.\\d (\\d{3}) ([a-zA-Z0-9]+)");
        std::smatch match;
        if (regex_search(header, match, headerrx) == true && match.size() == 3) {
            conn->status = std::stoi(match.str(1));
            conn->reason = match.str(2);
        } else {
            return 0;
        }
    } else {
        // normal header, or alternatively the last header
        const size_t split = header.find(": ");
        if (!split || split == std::string::npos) {
            if (header.size() == 2 && header[0] == '\r' && header[1] == '\n') {
                // we're done

                auto http = conn->http.lock();
                if (!http) {
                    // failure
                    return 0;
                }

                // if this is a 300 response, don't emit
                if (conn->status >= 300 && conn->status < 400) {
                    conn->status = 0;
                    conn->headers.clear();
                } else {
                    http->mResponseReceived = true;
                    if (http->mFlags & WebSocket) {
                        http->setupWS();
                    }
                    http->mResponse.emit({ conn->status, conn->reason, std::move(conn->headers) });
                }

                return size * nmemb;
            } else {
                // bad
                return 0;
            }
        }
        assert(nsize >= split + 2);
        std::string headerKey = header.substr(0, split);
        std::transform(headerKey.begin(), headerKey.end(), headerKey.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        conn->headers.add(std::move(headerKey), header.substr(split + 2, nsize - (split + 2)));
    }

    return size * nmemb;
}

void HttpClient::setupWS()
{
    auto conn = mConnectionInfo;
    auto loop = tCurlInfo->loop.lock();
    if (loop && conn->socketInfo && conn->socketInfo->fd != -1) {
        loop->removeFd(conn->socketInfo->fd);
        std::weak_ptr<HttpClient> wclient = shared_from_this();
        loop->addFd(conn->socketInfo->fd, event::Loop::FdRead, [wclient](int fd, uint8_t flags) {
            auto client = wclient.lock();
            auto loop = tCurlInfo->loop.lock();
            if (!client || !loop)
                return;
            if (flags & event::Loop::FdWrite) {
                loop->updateFd(fd, event::Loop::FdRead);
            }
            client->handleWS(flags);
        });

        handleWS(0);
    }
}

void HttpClient::handleWS(uint8_t flags)
{
    if (!mResponseReceived)
        return;
    int e;
    uint8_t staticBuffer[net::TcpSocket::BufferSize];
    auto loop = tCurlInfo->loop.lock();
    auto ssl = mConnectionInfo->ssl;
    const auto fd = mConnectionInfo->socketInfo->fd;

    auto notifyWritten = [&](int written) {
        assert(mBufferPos < mBuffers.size());
        const auto& buffer = mBuffers[mBufferPos];
        assert(mBufferOffset + written <= buffer->size());
        mBufferOffset += written;
        if (mBufferOffset == buffer->size()) {
            mBufferOffset = 0;
            ++mBufferPos;
            if (mBufferPos == mBuffers.size()) {
                // all all done
                mBufferPos = 0;
                mBuffers.clear();
                return;
            }
        }
    };

    if (ssl) {
        auto sslRead = [&]() {
            e = SSL_read(ssl, staticBuffer, sizeof(staticBuffer));
            if (e <= 0) {
                // errory
                e = SSL_get_error(ssl, e);
                if (e == SSL_ERROR_WANT_READ) {
                    mSSLReadWaitState = SSLReadWaitingForRead;
                } else if (e == SSL_ERROR_WANT_WRITE) {
                    mSSLReadWaitState = SSLReadWaitingForWrite;
                    if (loop) {
                        loop->updateFd(fd, event::Loop::FdRead | event::Loop::FdWrite);
                    }
                } else {
                    // hooorrible
                }
            } else {
                auto newBuffer = buffer::Pool<20, net::TcpSocket::BufferSize>::pool().get(e);
                newBuffer->assign(staticBuffer, e);
                mBodyData.emit(std::move(newBuffer));
                mSSLReadWaitState = SSLNotWaiting;
            }
        };

        auto sslWrite = [&]() {
            assert(mBufferPos < mBuffers.size());
            const auto& buffer = mBuffers[mBufferPos];
            e = SSL_write(ssl, buffer->data() + mBufferOffset, buffer->size() - mBufferOffset);
            if (e <= 0) {
                // errory
                e = SSL_get_error(ssl, e);
                if (e == SSL_ERROR_WANT_READ) {
                    mSSLWriteWaitState = SSLWriteWaitingForRead;
                } else if (e == SSL_ERROR_WANT_WRITE) {
                    mSSLWriteWaitState = SSLWriteWaitingForWrite;
                    if (loop) {
                        loop->updateFd(fd, event::Loop::FdRead | event::Loop::FdWrite);
                    }
                } else {
                    // hooorrible
                }
            } else {
                // yay, we wrote something
                notifyWritten(e);
                mSSLWriteWaitState = SSLNotWaiting;
            }
        };

        switch (mSSLReadWaitState) {
        case SSLReadWaitingForRead:
            if (flags & event::Loop::FdRead) {
                // repeat call
                sslRead();
            }
            break;
        case SSLReadWaitingForWrite:
            if (flags & event::Loop::FdWrite) {
                // repeat call
                sslRead();
            }
            break;
        default:
            break;
        }
        switch (mSSLWriteWaitState) {
        case SSLWriteWaitingForRead:
            if (flags & event::Loop::FdRead) {
                // repeat call
                sslWrite();
            }
            break;
        case SSLWriteWaitingForWrite:
            if (flags & event::Loop::FdWrite) {
                // repeat call
                sslWrite();
            }
            break;
        default:
            break;
        }
        // write any pending data we might have
        while (mSSLWriteWaitState == SSLNotWaiting && !mBuffers.empty()) {
            sslWrite();
        }
        // read all the data we have
        while (mSSLReadWaitState == SSLNotWaiting) {
            sslRead();
        }
    } else {
        // write any pending data we might have
        if ((!mWaitingForWrite || (flags & event::Loop::FdWrite)) && !mBuffers.empty()) {
            mWaitingForWrite = false;
            while (!mBuffers.empty()) {
                assert(mBufferPos < mBuffers.size());
                const auto& buffer = mBuffers[mBufferPos];
                eintrwrap(e, ::write(fd, buffer->data() + mBufferOffset, buffer->size() - mBufferOffset));
                if (e > 0) {
                    notifyWritten(e);
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    mWaitingForWrite = true;
                    if (loop) {
                        loop->updateFd(fd, event::Loop::FdRead | event::Loop::FdWrite);
                    }
                    break;
                } else {
                    // horrible stuff
                    break;
                }
            }
        }
        if (!mWaitingForRead || (flags & event::Loop::FdRead)) {
            mWaitingForRead = false;
            // try to read
            for (;;) {
                eintrwrap(e, ::read(fd, staticBuffer, sizeof(staticBuffer)));
                if (e > 0) {
                    auto newBuffer = buffer::Pool<20, net::TcpSocket::BufferSize>::pool().get(e);
                    newBuffer->assign(staticBuffer, e);
                    mBodyData.emit(std::move(newBuffer));
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    mWaitingForRead = true;
                    break;
                } else {
                    // badness
                    break;
                }
            }
        }
    }
}

size_t HttpClient::easyReadCallback(void *dest, size_t size, size_t nmemb, void *userp)
{
    HttpConnectionInfo* conn = static_cast<HttpConnectionInfo*>(userp);
    auto http = conn->http.lock();
    if (!http) {
        return 0;
    }

    if (http->mBuffers.empty()) {
        if (http->mWriteEnd) {
            return 0;
        }
        http->mPaused = true;
        return CURL_READFUNC_PAUSE;
    }

    assert(http->mBufferPos < http->mBuffers.size());
    const auto& buffer = http->mBuffers[http->mBufferPos];
    assert(http->mBufferOffset < buffer->size());
    const size_t toread = std::min(size * nmemb, buffer->size() - http->mBufferOffset);
    assert(http->mBufferOffset + toread <= buffer->size());
    memcpy(dest, buffer->data() + http->mBufferOffset, toread);
    if (http->mBufferOffset + toread == buffer->size()) {
        http->mBufferOffset = 0;
        ++http->mBufferPos;
        if (http->mBufferPos == http->mBuffers.size()) {
            http->mBuffers.clear();
            http->mBufferPos = 0;
        }
    } else {
        http->mBufferOffset += toread;
    }
    return toread;
}

void HttpClient::checkMultiInfo()
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

            auto http = conn->http.lock();
            if (http) {
                http->mConnectionInfo = nullptr;
                if (res != CURLE_OK) {
                    http->mError.emit(std::string(conn->error));
                }
                http->mComplete.emit();
            }

            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
            if (conn->outHeaders) {
                curl_slist_free_all(conn->outHeaders);
            }

            delete conn;
        }
    }
}

void HttpClient::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
    if (!mConnectionInfo)
        return;
    mBuffers.push_back(buffer);
    if (mFlags & WebSocket) {
        // we've taken over the read and write
        handleWS(0);
        return;
    }
    if (mPaused) {
        curl_easy_pause(mConnectionInfo->easy, CURLPAUSE_CONT);
        mPaused = false;
    }
}

void HttpClient::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    if (!mConnectionInfo)
        return;
    mBuffers.push_back(buffer);
    if (mFlags & WebSocket) {
        // we've taken over the read and write
        handleWS(0);
        return;
    }
    if (mPaused) {
        curl_easy_pause(mConnectionInfo->easy, CURLPAUSE_CONT);
        mPaused = false;
    }
}

void HttpClient::write(const uint8_t* data, size_t bytes)
{
    if (!mConnectionInfo)
        return;
    auto buffer = buffer::Pool<20, net::TcpSocket::BufferSize>::pool().get(bytes);
    buffer->assign(data, bytes);
    mBuffers.push_back(buffer);
    if (mFlags & WebSocket) {
        // we've taken over the read and write
        handleWS(0);
        return;
    }
    if (mPaused) {
        curl_easy_pause(mConnectionInfo->easy, CURLPAUSE_CONT);
        mPaused = false;
    }
}

void HttpClient::write(const char* data, size_t bytes)
{
    if (!mConnectionInfo)
        return;
    auto buffer = buffer::Pool<20, net::TcpSocket::BufferSize>::pool().get(bytes);
    buffer->assign(reinterpret_cast<const uint8_t*>(data), bytes);
    mBuffers.push_back(buffer);
    if (mFlags & WebSocket) {
        // we've taken over the read and write
        handleWS(0);
        return;
    }
    if (mPaused) {
        curl_easy_pause(mConnectionInfo->easy, CURLPAUSE_CONT);
        mPaused = false;
    }
}

void HttpClient::endWrite()
{
    if (!mConnectionInfo)
        return;
    mWriteEnd = true;
    if (mPaused) {
        curl_easy_pause(mConnectionInfo->easy, CURLPAUSE_CONT);
        mPaused = false;
    }
}
