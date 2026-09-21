#include "net.hpp"
#include "util.hpp"

#include <cerrno>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#define CLOSESOCK closesocket
#define LASTERR WSAGetLastError()
static void ensureWsa() {
    static bool done = false;
    if (!done) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        done = true;
    }
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK ::close
#define LASTERR errno
static void ensureWsa() {}
#endif

#ifdef TUNNEL_LITE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace lite {

// ================= Socket =================

Socket::Socket() { ensureWsa(); }

Socket::~Socket() { release(); }

void Socket::release() {
#ifdef TUNNEL_LITE_TLS
    if (ssl_) {
        SSL_shutdown((SSL *)ssl_);
        SSL_free((SSL *)ssl_);
        ssl_ = nullptr;
    }
    if (sslCtx_) {
        SSL_CTX_free((SSL_CTX *)sslCtx_);
        sslCtx_ = nullptr;
    }
#endif
    if (fd_ >= 0) {
        CLOSESOCK((int)fd_);
        fd_ = -1;
    }
}

bool Socket::connectTo(const std::string &host, int port, std::string *err) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "DNS 解析失败: " + host;
        return false;
    }
    bool ok = false;
    for (auto *p = res; p; p = p->ai_next) {
#ifdef _WIN32
        SOCKET s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
#else
        int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
        // 非阻塞 connect + 5s 超时，避免本地服务卡死时线程永久阻塞
        int rc = ::connect(s, p->ai_addr, (socklen_t)p->ai_addrlen);
        bool connected = false;
        if (rc == 0) {
            connected = true;
        } else {
            bool inProgress = false;
#ifdef _WIN32
            inProgress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
            inProgress = (errno == EINPROGRESS);
#endif
            if (inProgress) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(s, &wf);
                timeval tv;
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                int sel = ::select((int)s + 1, nullptr, &wf, nullptr, &tv);
                if (sel > 0) {
                    int soerr = 0;
                    socklen_t l = sizeof(soerr);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &l);
                    connected = (soerr == 0);
                }
            }
        }
        if (connected) {
#ifdef _WIN32
            u_long bl = 0;
            ioctlsocket(s, FIONBIO, &bl);
#else
            fcntl(s, F_SETFL, flags);
#endif
            fd_ = (long long)s;
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
            ok = true;
            break;
        }
        CLOSESOCK((int)s);
    }
    freeaddrinfo(res);
    if (!ok && err)
        *err = "连接失败: " + host + ":" + portStr;
    return ok;
}

bool Socket::startTls(const std::string &host, std::string *err) {
#ifdef TUNNEL_LITE_TLS
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        if (err) *err = "SSL_CTX 创建失败";
        return false;
    }
    SSL_CTX_set_default_verify_paths(ctx);
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, (int)fd_);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (SSL_connect(ssl) != 1) {
        if (err) *err = "TLS 握手失败";
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return false;
    }
    ssl_ = ssl;
    sslCtx_ = ctx;
    return true;
#else
    (void)host;
    if (err)
        *err = "本二进制未启用 TLS（wss）。请用 ws:// 或启用 -DTUNNEL_LITE_TLS=ON 重新编译";
    return false;
#endif
}

long Socket::readSome(void *buf, size_t n) {
#ifdef TUNNEL_LITE_TLS
    if (ssl_) {
        int r = SSL_read((SSL *)ssl_, buf, (int)n);
        if (r > 0) return r;
        int e = SSL_get_error((SSL *)ssl_, r);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return -2; // 超时
        return -1;
    }
#endif
#ifdef _WIN32
    int r = ::recv((SOCKET)fd_, (char *)buf, (int)n, 0);
    if (r == 0) return 0;
    if (r == SOCKET_ERROR) {
        const int e = WSAGetLastError();
        if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK) return -2;
        return -1;
    }
    return r;
#else
    ssize_t r = ::recv((int)fd_, buf, n, 0);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
    return (long)r;
#endif
}

void Socket::setReadTimeout(int ms) {
    if (fd_ < 0) return;
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    setsockopt((SOCKET)fd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt((int)fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

bool Socket::writeAll(const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w;
#ifdef TUNNEL_LITE_TLS
        if (ssl_) {
            int r = SSL_write((SSL *)ssl_, p, (int)left);
            if (r <= 0) return false;
            w = r;
        } else
#endif
        {
#ifdef _WIN32
            w = ::send((SOCKET)fd_, p, (int)left, 0);
            if (w == SOCKET_ERROR) return false;
#else
            w = ::send((int)fd_, p, left, MSG_NOSIGNAL);
            if (w < 0) return false;
#endif
        }
        p += w;
        left -= (size_t)w;
    }
    return true;
}

void Socket::shutdownAll() {
    if (fd_ >= 0)
        ::shutdown((int)fd_, 2);
}

// ================= WebSocket =================

WebSocket::~WebSocket() { close(); }

static bool parseWsUrl(const std::string &url, bool &tls, std::string &host, int &port,
                       std::string &path) {
    std::string rest;
    if (url.rfind("wss://", 0) == 0) {
        tls = true;
        port = 443;
        rest = url.substr(6);
    } else if (url.rfind("ws://", 0) == 0) {
        tls = false;
        port = 80;
        rest = url.substr(5);
    } else {
        return false;
    }
    size_t slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
    path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        port = std::atoi(hostPort.substr(colon + 1).c_str());
    } else {
        host = hostPort;
    }
    return !host.empty();
}

bool WebSocket::readExact(void *buf, size_t n) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < n) {
        long r = sock_.readSome(p + got, n - got);
        if (r == -2) {
            if (stop_ && *stop_)
                return false; // 收到退出信号，立即返回
            continue;         // 读超时，继续等
        }
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

bool WebSocket::connect(const std::string &url, const std::string &bearerToken, std::string *err) {
    if (!parseWsUrl(url, tls_, host_, port_, path_)) {
        if (err) *err = "URL 非法: " + url;
        return false;
    }
    if (!sock_.connectTo(host_, port_, err))
        return false;
    if (tls_ && !sock_.startTls(host_, err))
        return false;

    const std::string key = randomB64(16);
    std::ostringstream req;
    req << "GET " << path_ << " HTTP/1.1\r\n"
        << "Host: " << host_ << ":" << port_ << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n";
    if (!bearerToken.empty())
        req << "Authorization: Bearer " << bearerToken << "\r\n";
    req << "\r\n";

    if (!sock_.writeAll(req.str().data(), req.str().size())) {
        if (err) *err = "发送握手失败";
        return false;
    }

    // 逐字节读响应头
    std::string head;
    char c;
    while (head.size() < 16384) {
        if (!readExact(&c, 1))
            break;
        head += c;
        if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0)
            break;
    }
    if (head.find(" 101") == std::string::npos) {
        std::string first = head.substr(0, head.find("\r\n"));
        if (err) *err = "握手失败: " + first;
        return false;
    }
    open_ = true;
    return true;
}

bool WebSocket::sendFrame(uint8_t opcode, const std::string &payload) {
    std::string frame;
    frame += (char)(0x80 | opcode); // FIN + opcode
    const size_t len = payload.size();
    if (len < 126) {
        frame += (char)(0x80 | len); // MASK + len
    } else if (len <= 0xFFFF) {
        frame += (char)(0x80 | 126);
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    } else {
        frame += (char)(0x80 | 127);
        for (int i = 7; i >= 0; --i)
            frame += (char)(((uint64_t)len >> (i * 8)) & 0xFF);
    }
    // 掩码（客户端必须加）
    uint8_t mask[4];
    std::string mk = randomB64(4);
    std::memcpy(mask, mk.data(), 4);
    frame.append((char *)mask, 4);
    for (size_t i = 0; i < len; ++i)
        frame += (char)((uint8_t)payload[i] ^ mask[i % 4]);
    txBytes_ += frame.size();
    return sock_.writeAll(frame.data(), frame.size());
}

bool WebSocket::sendText(const std::string &text, std::string *err) {
    if (!sendFrame(0x1, text)) {
        if (err) *err = "WebSocket 发送失败";
        return false;
    }
    return true;
}

bool WebSocket::sendBinary(const std::string &data, std::string *err) {
    if (!sendFrame(0x2, data)) {
        if (err) *err = "WebSocket 发送失败";
        return false;
    }
    return true;
}

bool WebSocket::recvText(std::string &out, std::string *err) {
    bool bin = false;
    return recvMessage(out, bin, err);
}

bool WebSocket::recvMessage(std::string &out, bool &isBinary, std::string *err) {
    std::string message;
    bool isBin = false;
    bool haveOp = false;
    for (;;) {
        uint8_t hdr[2];
        if (!readExact(hdr, 2)) {
            if (err) *err = "连接已关闭";
            return false;
        }
        const bool fin = (hdr[0] & 0x80) != 0;
        const uint8_t opcode = hdr[0] & 0x0F;
        const bool masked = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7F;
        uint64_t extBytes = 0;
        if (len == 126) {
            uint8_t e[2];
            if (!readExact(e, 2)) return false;
            len = ((uint64_t)e[0] << 8) | e[1];
            extBytes = 2;
        } else if (len == 127) {
            uint8_t e[8];
            if (!readExact(e, 8)) return false;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | e[i];
            extBytes = 8;
        }
        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked && !readExact(mask, 4))
            return false;
        std::string payload;
        payload.resize((size_t)len);
        if (len && !readExact(&payload[0], (size_t)len))
            return false;
        rxBytes_ += 2 + extBytes + (masked ? 4 : 0) + len;
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = (char)((uint8_t)payload[i] ^ mask[i % 4]);
        }

        switch (opcode) {
        case 0x1: // text
        case 0x2: // binary
        case 0x0: // continuation
            if (!haveOp && opcode != 0x0) {
                isBin = (opcode == 0x2);
                haveOp = true;
            }
            message += payload;
            if (fin) {
                out = std::move(message);
                isBinary = isBin;
                return true;
            }
            break;
        case 0x8: // close
            if (err) *err = "服务端关闭连接";
            return false;
        case 0x9: // ping -> pong
            sendFrame(0xA, payload);
            break;
        case 0xA: // pong
            break;
        default:
            break;
        }
    }
}

void WebSocket::close() {
    if (open_) {
        sendFrame(0x8, "");
        open_ = false;
    }
    sock_.shutdownAll();
}

// ================= HTTP =================

std::string httpHeader(const HttpResponse &r, const std::string &name) {
    for (const auto &h : r.headers) {
        if (h.first.size() == name.size()) {
            bool eq = true;
            for (size_t i = 0; i < name.size(); ++i)
                if (tolower((unsigned char)h.first[i]) != tolower((unsigned char)name[i])) {
                    eq = false;
                    break;
                }
            if (eq)
                return h.second;
        }
    }
    return {};
}

bool httpRequest(const std::string &url, const std::string &method,
                 const std::vector<std::pair<std::string, std::string>> &headers,
                 const std::string &body, HttpResponse &out, std::string *err) {
    bool tls = false;
    std::string host, path;
    int port = 80;
    if (url.rfind("https://", 0) == 0) {
        tls = true;
        port = 443;
        std::string rest = url.substr(8);
        size_t slash = rest.find('/');
        std::string hp = slash == std::string::npos ? rest : rest.substr(0, slash);
        path = slash == std::string::npos ? "/" : rest.substr(slash);
        size_t colon = hp.rfind(':');
        if (colon != std::string::npos) {
            host = hp.substr(0, colon);
            port = std::atoi(hp.substr(colon + 1).c_str());
        } else {
            host = hp;
        }
    } else if (url.rfind("http://", 0) == 0) {
        std::string rest = url.substr(7);
        size_t slash = rest.find('/');
        std::string hp = slash == std::string::npos ? rest : rest.substr(0, slash);
        path = slash == std::string::npos ? "/" : rest.substr(slash);
        size_t colon = hp.rfind(':');
        if (colon != std::string::npos) {
            host = hp.substr(0, colon);
            port = std::atoi(hp.substr(colon + 1).c_str());
        } else {
            host = hp;
        }
    } else {
        if (err) *err = "URL 非法: " + url;
        return false;
    }

    Socket sock;
    if (!sock.connectTo(host, port, err))
        return false;
    if (tls && !sock.startTls(host, err))
        return false;

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n";
    for (const auto &h : headers)
        req << h.first << ": " << h.second << "\r\n";
    if (!body.empty())
        req << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n" << body;
    const std::string rs = req.str();
    if (!sock.writeAll(rs.data(), rs.size())) {
        if (err) *err = "发送请求失败";
        return false;
    }

    std::string head;
    char c;
    while (head.size() < 65536) {
        if (sock.readSome(&c, 1) <= 0)
            break;
        head += c;
        if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0)
            break;
    }
    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos) {
        if (err) *err = "响应非法";
        return false;
    }
    std::istringstream ls(head.substr(0, lineEnd));
    std::string http;
    ls >> http >> out.status;
    std::getline(ls, out.reason);
    if (!out.reason.empty() && out.reason[0] == ' ')
        out.reason.erase(0, 1);

    size_t pos = lineEnd + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos || e == pos)
            break;
        std::string line = head.substr(pos, e - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
                v.erase(0, 1);
            out.headers.emplace_back(k, v);
        }
        pos = e + 2;
    }

    const std::string cl = httpHeader(out, "Content-Length");
    if (!cl.empty()) {
        size_t need = (size_t)std::atoll(cl.c_str());
        out.body.resize(need);
        size_t got = 0;
        while (got < need) {
            long r = sock.readSome(&out.body[got], need - got);
            if (r <= 0)
                break;
            got += (size_t)r;
        }
        out.body.resize(got);
    } else {
        char buf[4096];
        long r;
        while ((r = sock.readSome(buf, sizeof(buf))) > 0)
            out.body.append(buf, (size_t)r);
    }
    return true;
}

} // namespace lite
