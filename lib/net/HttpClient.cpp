#include "HttpClient.h"
#include <buffer/Pool.h>
#include <buffer/Builder.h>
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::net;

inline bool HttpClient::parseBody(uint8_t* body, size_t size)
{
    if (!mChunked)
        return false;
    // handle chunked here
    return false;
}

void HttpClient::connect(const std::string& host, uint16_t port)
{
    if (mSocket)
        return;

    mSocket = TcpSocket::create();
    mSocket->onStateChanged().connect([this](TcpSocket::State state) {
            switch (state) {
            case TcpSocket::Connected:
                mState = Connected;
                mStateChanged.emit(Connected);
                break;
            case TcpSocket::Closed:
                if (mPendingBodyEnd) {
                    mBodyEnd.emit();
                    mPendingBodyEnd = false;
                }
                mSocket.reset();
                mState = Closed;
                mStateChanged.emit(Closed);
                break;
            case TcpSocket::Error:
                mSocket.reset();
                mState = Closed;
                mStateChanged.emit(Error);
                break;
            default:
                break;
            }
        });
    mSocket->onData().connect([this](std::shared_ptr<buffer::Buffer>&& buf) {
            auto maybeEmitEnd = [this](size_t sz) {
                if (mContentLength == -1)
                    return;
                mReceived += sz;
                assert(mReceived <= mContentLength);
                if (mReceived == mContentLength) {
                    mBodyEnd.emit();
                    mPendingBodyEnd = false;
                } else if (mReceived > mContentLength) {
                    // badly behaved server?
                    log::Log(log::Log::Error) << "got too much data from http server" << mReceived << mContentLength;
                    close();
                }
            };
            auto skipSpace = [](char* cur) -> char* {
                while (*cur == ' ' || *cur == '\t')
                    ++cur;
                if (!*cur || *cur == '\r')
                    return nullptr;
                return cur;
            };

            if (!mHeadersReceived) {
                // work on headers
                if (!buf)
                    return;
                // is our end sequence in this chunk?
                if (mReadBuffer->size() > 0 || buf->size() < 4) {
                    mReadBuffer = buffer::Buffer::concat(mReadBuffer, buf);
                    if (mReadBuffer->size() < 4)
                        return;
                    buf = mReadBuffer;
                }
                char* end = strnstr(reinterpret_cast<char*>(buf->data()), "\r\n\r\n", buf->size());
                if (!end)
                    return;
                end += 4;
                mHeadersReceived = true;
                Response response;
                // parse buffers
                char* cur = reinterpret_cast<char*>(buf->data());
                if (cur < end - 4) {
                    // parse status line
                    char* eol = strnstr(cur, "\r\n", end - cur);
                    // find two spaces
                    char* sp1 = reinterpret_cast<char*>(memchr(cur, ' ', end - cur));
                    if (!sp1) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        close();
                    }
                    char* sp2 = reinterpret_cast<char*>(memchr(sp1 + 1, ' ', end - (sp1 + 1)));
                    if (!sp2) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        close();
                    }
                    char* endptr;
                    const long status = strtol(sp1 + 1, &endptr, 10);
                    if (endptr > sp2 || status < 100 || status >= 600) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        close();
                    }
                    response.status = static_cast<uint8_t>(status);
                    response.reason = std::string(sp2 + 1, eol - (sp2 + 1));
                    cur = eol + 2;
                }
                while (cur < end - 4) {
                    // find eol and eq
                    char* eol = strnstr(cur, "\r\n", end - cur);
                    char* sep = strnstr(cur, ":", end - cur);
                    char* vs = (!sep || sep > eol) ? nullptr : skipSpace(sep + 1);
                    if (!vs) {
                        // take the whole line as key
                        std::string key = std::string(cur, eol - cur);
                        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                        // printf("adding single header '%s'\n", key.c_str());
                        response.headers.push_back(std::make_pair(std::move(key), std::string()));
                    } else {
                        std::string key = std::string(cur, sep - cur);
                        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                        if (key == "content-length") {
                            // make sure our data is null-terminated
                            *eol = '\0';
                            char* endptr;
                            mContentLength = strtoll(vs, &endptr, 10);
                            if (*endptr != '\0') {
                                // bad content length
                                mContentLength = -1;
                                log::Log(log::Log::Error) << "bad content length from http client";
                                close();
                                return;
                            }
                        } else if (key == "transfer-encoding") {
                            // yeah, take an extra copy
                            mTransferEncoding = std::string(vs, eol - vs);
                            mChunked = (mTransferEncoding == "chunked");
                        }
                        // printf("adding header '%s' = '%s'\n", key.c_str(), std::string(vs, eol - vs).c_str());
                        response.headers.push_back(std::make_pair(std::move(key), std::string(vs, eol - vs)));
                    }
                    cur = eol + 2;
                }
                mResponse.emit(std::move(response));
                uint8_t* bodyStart = reinterpret_cast<uint8_t*>(end);
                if (bodyStart >= buf->data() + buf->size()) {
                    mReadBuffer.reset();
                    return;
                }
                const size_t bodyLength = (buf->data() + buf->size()) - bodyStart;
                if (!parseBody(bodyStart, bodyLength)) {
                    if (bodyLength > 0) {
                        auto body = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(bodyLength);
                        body->assign(bodyStart, bodyLength);
                        mBodyData.emit(std::move(body));
                    }
                    maybeEmitEnd(bodyLength);
                }
                mReadBuffer.reset();
                return;
            }
            // work on body
            const size_t sz = buf->size();
            if (!parseBody(buf->data(), sz)) {
                mBodyData.emit(std::move(buf));
                maybeEmitEnd(sz);
            }
        });
    mSocket->connect(host, port);
}

void HttpClient::close()
{
    if (!mSocket)
        return;
    mSocket.reset();
}

inline void HttpClient::prepare(const char* method, HttpVersion version, const std::string& query, const Headers& headers)
{
    mHeadersReceived = false;
    mReadBuffer = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    mReadBuffer->setSize(0);
    mContentLength = -1;
    mPendingBodyEnd = true;
    mReceived = 0;
    mTransferEncoding.clear();
    mChunked = false;
    assert(mReadBuffer);

    auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    // build our request
    buffer::Builder builder(buf);
    builder << method << query << ((version == v10) ? " HTTP/1.0\r\n" : " HTTP/1.1\r\n");
    for (const auto& header : headers) {
        builder << header.first << ": " << header.second << "\r\n";
    }
    builder << "\r\n";
    if (builder.overflow()) {
        log::Log(log::Log::Error) << "Http request too large, max" << buf->max();
        return;
    }
    builder.flush();
    mSocket->write(std::move(buf));
}

void HttpClient::get(HttpVersion version, const std::string& query, const Headers& headers)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        return;
    }
    prepare("GET ", version, query, headers);
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, const std::shared_ptr<buffer::Buffer>& body)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        return;
    }
    prepare("POST ", version, query, headers);
    mSocket->write(body);
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, std::shared_ptr<buffer::Buffer>&& body)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        return;
    }
    prepare("POST ", version, query, headers);
    mSocket->write(std::move(body));
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, const std::string& body)
{
    prepare("POST ", version, query, headers);
    mSocket->write(&body[0], body.size());
}
