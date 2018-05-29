#include "HttpServer.h"
#include <log/Log.h>
#include <buffer/Builder.h>
#include <string.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

static inline const char* makeReason(uint16_t status)
{
    switch (status) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 103: return "Early Hints";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 306: return "Switch Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Non Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    default: return "Unknown";
    }
}

inline void HttpServer::setupServer()
{
    assert(mServer);

#warning have to store the connection handle and disconnect

    mServer->onConnection().connect([this](std::shared_ptr<TcpSocket>&& socket) {
            auto conn = mConnections.insert(std::make_pair(std::move(socket), Connection()));
            if (!conn.second) {
                // bad
                Log(Log::Error) << "Connection already exists?";
                return;
            }

            auto& s = conn.first->first;
            auto& c = conn.first->second;
            c.waitFor.onData().connect([this, conn](std::shared_ptr<buffer::Buffer>&& buffer, size_t where) {
                    if (!buffer) {
                        Log(Log::Error) << "http request too large";
                        return;
                    }

                    char* cur = reinterpret_cast<char*>(buffer->data());
                    char* end = cur + where + 4;

                    auto req = Request::create();
                    req->mSocket = conn.first->first;

                    auto skipSpace = [](char* cur) -> char* {
                        while (*cur == ' ' || *cur == '\t')
                            ++cur;
                        if (!*cur || *cur == '\r')
                            return nullptr;
                        return cur;
                    };

                    if (cur < end - 4) {
                        // parse request line
                        // METHOD SP QUERY SP VERSION
                        char* eol = static_cast<char*>(memmem(cur, end - cur, "\r\n", 2));

                        auto malformed = [&]() {
                            log::Log(log::Log::Error) << "malformed http request line" << std::string(cur, eol - cur);
                            mError.emit();
                            mConnections.erase(conn.first);
                        };

                        // find two spaces
                        char* sp1 = reinterpret_cast<char*>(memchr(cur, ' ', end - cur));
                        if (!sp1) {
                            // malformed response
                            malformed();
                            return;
                        }
                        char* sp2 = reinterpret_cast<char*>(memchr(sp1 + 1, ' ', end - (sp1 + 1)));
                        if (!sp2) {
                            // malformed response
                            malformed();
                            return;
                        }
                        if (eol - (sp2 + 1) != 8) { // 8 == strlen("HTTP/1.x")
                            // malformed response
                            malformed();
                            return;
                        }
                        *sp1 = '\0';
                        if (!strcmp(cur, "GET")) {
                            req->method = HttpServer::Get;
                        } else if (!strcmp(cur, "POST")) {
                            req->method = HttpServer::Post;
                        } else {
                            // malformed response
                            malformed();
                            return;
                        }
                        if (!strncmp(sp2 + 1, "HTTP/1.", 7)) {
                            switch (*(sp2 + 8)) {
                            case '1':
                                req->version = HttpServer::HttpVersion::v11;
                                break;
                            case '0':
                                req->version = HttpServer::HttpVersion::v10;
                                break;
                            default:
                                malformed();
                                return;
                            }
                        } else {
                            // malformed response
                            malformed();
                            return;
                        }

                        req->query = std::string(sp1 + 1, sp2 - (sp1 + 1));
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
                            req->headers.push_back(std::make_pair(std::move(key), std::string()));
                        } else {
                            std::string key = std::string(cur, sep - cur);
                            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                            if (key == "content-length") {
                                char* endptr;
                                req->mContentLength = strtoll(vs, &endptr, 10);
                                if (*endptr != '\r') {
                                    log::Log(log::Log::Error) << "invalid content length" << std::string(vs, eol - vs);
                                    mError.emit();
                                    mConnections.erase(conn.first);
                                    return;
                                }
                            }
                            req->headers.push_back(std::make_pair(std::move(key), std::string(vs, eol - vs)));
                        }
                        cur = eol + 2;
                    }
                    // if we have more data, we'll have to emit the data after we emit the request
                    std::shared_ptr<Request> reqcopy;
                    uint8_t* bodyStart = reinterpret_cast<uint8_t*>(end);
                    const size_t bodyLength = (buffer->data() + buffer->size()) - bodyStart;
                    if (bodyLength > 0 || !req->mContentLength) {
                        reqcopy = req;
                    }

                    // we're done with the connection from the HttpServer point of view
                    // body data will be emitted by the request (see Request::finalize())
                    auto& c = conn.first->second;
                    c.waitFor.onData().disconnect();
                    c.onSocketData.disconnect();
                    c.onSocketState.disconnect();

                    // emit our request
                    req->finalize();
                    mRequest.emit(std::move(req));

                    if (bodyLength > 0) {
                        // make a new buffer
                        auto body = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(bodyLength);
                        body->assign(bodyStart, bodyLength);
                        reqcopy->mBody.emit(std::move(body));
                        reqcopy->mContentReceived = bodyLength;
                        if (reqcopy->mContentLength == bodyLength) {
                            // done with the thing
                            reqcopy->mEnd.emit();
                        }
                    } else if (reqcopy && !reqcopy->mContentLength) {
                        reqcopy->mEnd.emit();
                    }
                });
            c.onSocketData = s->onData().connect([conn](std::shared_ptr<buffer::Buffer>&& buffer) {
                    auto& c = conn.first->second;
                    if (!c.onSocketData.connected())
                        return;
                    c.waitFor.feed(std::move(buffer));
                });
            c.onSocketState = s->onStateChanged().connect([this, conn](net::TcpSocket::State state) {
                    auto& c = conn.first->second;
                    if (!c.onSocketState.connected())
                        return;
                    if (state == net::TcpSocket::Closed || state == net::TcpSocket::Error) {
                        mConnections.erase(conn.first);
                    }
                });
        });
    mServer->onError().connect([this]() {
            mError.emit();
        });
}

bool HttpServer::listen(uint16_t port)
{
    if (mServer)
        return true;
    mServer = TcpServer::create();
    setupServer();
    return mServer->listen(port);
}

bool HttpServer::listen(const IPv4& ip, uint16_t port)
{
    if (mServer)
        return true;
    mServer = TcpServer::create();
    setupServer();
    return mServer->listen(ip, port);
}

bool HttpServer::listen(const IPv6& ip, uint16_t port)
{
    if (mServer)
        return true;
    mServer = TcpServer::create();
    setupServer();
    return mServer->listen(ip, port);
}

void HttpServer::close()
{
    if (!mServer)
        return;
    mServer->close();
    mServer.reset();
}

void HttpServer::Request::write(Response&& response)
{
    if (!mSocket)
        return;
    auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    // build our request
    buffer::Builder builder(buf);
    if (response.reason.empty()) {
        response.reason = makeReason(response.status);
    }
    builder << ((version == HttpVersion::v11) ? "HTTP/1.1 " : "HTTP/1.0 ") << response.status << " " << response.reason << "\r\n";
    for (const auto& header : response.headers) {
        builder << header.first << ": " << header.second << "\r\n";
    }
    builder << "\r\n";
    if (builder.overflow()) {
        log::Log(log::Log::Error) << "Http request too large, max" << buf->max();
        mError.emit();
        return;
    }
    builder.flush();
    mSocket->write(std::move(buf));
}

void HttpServer::Request::finalize()
{
    mOnSocketData = mSocket->onData().connect([this](std::shared_ptr<buffer::Buffer>&& buffer) {
            if (!mOnSocketData.connected())
                return;
            mContentReceived += buffer->size();
            if (mContentReceived > mContentLength) {
                // bad
                mError.emit();
                return;
            }
            mBody.emit(std::move(buffer));
            if (mContentReceived == mContentLength) {
                mEnd.emit();
            }
        });
    mOnSocketState = mSocket->onStateChanged().connect([this](TcpSocket::State state) {
            if (!mOnSocketState.connected())
                return;
            if (state == TcpSocket::Closed || state == TcpSocket::Error) {
                mSocket.reset();
            }
        });
}

void HttpServer::Request::neuter()
{
    mOnSocketData.disconnect();
    mOnSocketState.disconnect();
}
