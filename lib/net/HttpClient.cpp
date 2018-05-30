#include "HttpClient.h"
#include <buffer/Pool.h>
#include <buffer/Builder.h>
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::net;

inline bool HttpClient::parseBody(std::shared_ptr<buffer::Buffer>&& body, size_t offset)
{
    enum { MaxChunkSize = 1024 * 1000, ChunkBufferNo = 4, ChunkBufferSize = 1024 * 256 };

    if (!mChunked)
        return false;
    if (mBodyBuffer) {
        if (offset) {
            // manual concat
            auto newBody = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(mBodyBuffer->size() + body->size() - offset);
            newBody->assign(mBodyBuffer->data(), mBodyBuffer->size());
            newBody->append(body->data() + offset, body->size() - offset);
            mBodyBuffer = std::move(newBody);
        } else {
            mBodyBuffer = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().concat(mBodyBuffer, body);
        }
    } else {
        mChunkSize = 0;
        mChunkPrefix = 0;
        if (offset) {
            mBodyBuffer = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(body->size() - offset);
            mBodyBuffer->assign(body->data() + offset, body->size() - offset);
        } else {
            mBodyBuffer = std::move(body);
        }
    }
    if (mChunkSize) {
        // see if this completes our chunk
        if (mBodyBuffer->size() - mChunkPrefix + 2 >= static_cast<size_t>(mChunkSize)) {
            // yes it does
            auto newBody = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(mChunkSize);
            newBody->assign(mBodyBuffer->data() + mChunkPrefix, mChunkSize);
            mBodyData.emit(std::move(newBody));

            // did we use up our buffer?
            if (mChunkPrefix + mChunkSize + 2 == mBodyBuffer->size()) {
                // yes
                mBodyBuffer.reset();
                return true;
            } else {
                // no, let's make a new buffer and continue
                assert(mChunkPrefix + mChunkSize + 2 < mBodyBuffer->size());
                const size_t sz = mBodyBuffer->size() - (mChunkPrefix + mChunkSize + 2);
                newBody = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(sz);
                newBody->assign(mBodyBuffer->data() + mChunkPrefix + mChunkSize + 2, sz);
                mBodyBuffer = std::move(newBody);
                mChunkSize = 0;
                mChunkPrefix = 0;
            }
        } else {
            // nope, bail out
            return true;
        }
    }
    char* start = reinterpret_cast<char*>(mBodyBuffer->data());
    char* end = reinterpret_cast<char*>(mBodyBuffer->data() + (mBodyBuffer->size()));
    size_t rem = mBodyBuffer->size();
    for (;;) {
        // find our newline
        char* eol = static_cast<char*>(memmem(start, rem, "\r\n", 2));
        if (!eol) {
            // we done
            return true;
        }
        // parse chunked size
        char* endptr;
        const long size = strtol(start, &endptr, 16);
        if (endptr != eol || size < 0) {
            // bad
            log::Log(log::Log::Error) << "failed to parse chunk size" << size << *endptr << std::string(start, eol - start);
            mState = Error;
            mStateChanged.emit(Error);
            mBodyBuffer.reset();
            close();
            return true;
        }
        // verify that our chunk size isn't too big
        if (size > MaxChunkSize) {
            log::Log(log::Log::Error) << "chunk size too big" << size << MaxChunkSize;
            mState = Error;
            mStateChanged.emit(Error);
            mBodyBuffer.reset();
            close();
            return true;
        }
        if (!size) {
            // this was our last chunk
            mBodyEnd.emit();
            mBodyBuffer.reset();
            return true;
        }
        // do we have this kind of data in our chunk?
        if (end - (eol + 2) >= size + 2) {
            assert(eol + 2 + size + 2 <= end);
            // indeed we do
            auto newBody = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(size);
            newBody->assign(reinterpret_cast<uint8_t*>(eol + 2), size);
            mBodyData.emit(std::move(newBody));
            // mayhaps we have more data to process?
            if (eol + 2 + size + 2 < end) {
                // why yes we do
                assert(eol + 2 + size + 2 > start);
                rem -= (eol + 2 + size + 2) - start;
                start = eol + 2 + size + 2;
                assert(*eol == '\r' && *(eol + 1) == '\n');
                assert(*(eol + 2 + size) == '\r');
                assert(*(eol + 2 + size + 1) == '\n');
                continue;
            }
            // no, we're done
            mBodyBuffer.reset();
            return true;
        } else {
            // no, we'll have to wait
            const uint8_t* startu8 = reinterpret_cast<uint8_t*>(start);
            if (startu8 == mBodyBuffer->data()) {
                mChunkPrefix = (eol + 2) - start;
            } else {
                assert(rem > 0);
                auto newBody = buffer::Pool<ChunkBufferNo, ChunkBufferSize>::pool().get(rem);
                newBody->assign(startu8, rem);
                mBodyBuffer = std::move(newBody);
                mChunkPrefix = (eol - start) + 2;
            }
            mChunkSize = size;
            return true;
        }
    }

    return true;
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
            enum { MaxHeaderSize = 32768 };

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
                    mState = Error;
                    mStateChanged.emit(Error);
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
                if (mHeaderBuffer->size() > 0 || buf->size() < 4) {
                    if (mHeaderBuffer->size() + buf->size() > MaxHeaderSize) {
                        // consider this request malformed
                        log::Log(log::Log::Error) << "maximum header size exceeded" << mHeaderBuffer->size() + buf->size();
                        mState = Error;
                        mStateChanged.emit(Error);
                        close();
                        return;
                    }
                    mHeaderBuffer = buffer::Pool<20, TcpSocket::BufferSize>::pool().concat(mHeaderBuffer, buf);
                    if (mHeaderBuffer->size() < 4)
                        return;

                    buf = mHeaderBuffer;
                }
                char* end = static_cast<char*>(memmem(buf->data(), buf->size(), "\r\n\r\n", 4));
                if (!end)
                    return;
                end += 4;
                mHeadersReceived = true;
                Response response;
                // parse buffers
                char* cur = reinterpret_cast<char*>(buf->data());
                if (cur < end - 4) {
                    // parse status line
                    char* eol = static_cast<char*>(memmem(cur, end - cur, "\r\n", 2));
                    // find two spaces
                    char* sp1 = reinterpret_cast<char*>(memchr(cur, ' ', end - cur));
                    if (!sp1) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        mState = Error;
                        mStateChanged.emit(Error);
                        close();
                    }
                    char* sp2 = reinterpret_cast<char*>(memchr(sp1 + 1, ' ', end - (sp1 + 1)));
                    if (!sp2) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        mState = Error;
                        mStateChanged.emit(Error);
                        close();
                    }
                    char* endptr;
                    const long status = strtol(sp1 + 1, &endptr, 10);
                    if (endptr > sp2 || status < 100 || status >= 600) {
                        // malformed response
                        log::Log(log::Log::Error) << "malformed http status line" << std::string(cur, eol - cur);
                        mState = Error;
                        mStateChanged.emit(Error);
                        close();
                    }
                    response.status = static_cast<uint8_t>(status);
                    response.reason = std::string(sp2 + 1, eol - (sp2 + 1));
                    cur = eol + 2;
                }
                while (cur < end - 4) {
                    // find eol and eq
                    char* eol = static_cast<char*>(memmem(cur, end - cur, "\r\n", 2));
                    char* sep = static_cast<char*>(memchr(cur, ':', end - cur));
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
                                mState = Error;
                                mStateChanged.emit(Error);
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
                    mHeaderBuffer.reset();
                    return;
                }
                const size_t bodyLength = (buf->data() + buf->size()) - bodyStart;
                if (!parseBody(std::move(buf), bodyStart - buf->data())) {
                    if (bodyLength > 0) {
                        auto body = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(bodyLength);
                        body->assign(bodyStart, bodyLength);
                        mBodyData.emit(std::move(body));
                    }
                    maybeEmitEnd(bodyLength);
                }
                mHeaderBuffer.reset();
                return;
            }
            // work on body
            const size_t sz = buf->size();
            if (!parseBody(std::move(buf), 0)) {
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
    mHeaderBuffer = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    mHeaderBuffer->setSize(0);
    mContentLength = -1;
    mPendingBodyEnd = true;
    mReceived = 0;
    mChunkSize = 0;
    mChunkPrefix = 0;
    mTransferEncoding.clear();
    mChunked = false;
    assert(mHeaderBuffer);

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
        mState = Error;
        mStateChanged.emit(Error);
        return;
    }
    builder.flush();
    mSocket->write(std::move(buf));
}

void HttpClient::get(HttpVersion version, const std::string& query, const Headers& headers)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        mState = Error;
        mStateChanged.emit(Error);
        return;
    }
    prepare("GET ", version, query, headers);
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, const std::shared_ptr<buffer::Buffer>& body)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        mState = Error;
        mStateChanged.emit(Error);
        return;
    }
    prepare("POST ", version, query, headers);
    mSocket->write(body);
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, std::shared_ptr<buffer::Buffer>&& body)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        mState = Error;
        mStateChanged.emit(Error);
        return;
    }
    prepare("POST ", version, query, headers);
    mSocket->write(std::move(body));
}

void HttpClient::post(HttpVersion version, const std::string& query, const Headers& headers, const std::string& body)
{
    if (!mSocket) {
        log::Log(log::Log::Error) << "No connected TCP socket for HTTP client";
        mState = Error;
        mStateChanged.emit(Error);
        return;
    }
    prepare("POST ", version, query, headers);
    mSocket->write(&body[0], body.size());
}
