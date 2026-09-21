#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lite {

/// 跨平台 TCP socket（Windows 用 Winsock），可选 OpenSSL TLS
class Socket {
public:
    Socket();
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    bool connectTo(const std::string &host, int port, std::string *err = nullptr);
    bool startTls(const std::string &host, std::string *err = nullptr);
    /// 设置读超时（毫秒），用于让 Ctrl+C 能及时退出
    void setReadTimeout(int ms);

    /// >0 读到字节，0 = 对端关闭，-1 = 错误，-2 = 超时
    long readSome(void *buf, size_t n);
    bool writeAll(const void *buf, size_t n);
    void shutdownAll();

    bool valid() const { return fd_ >= 0; }

private:
    long long fd_ = -1;
    void *ssl_ = nullptr;    // SSL*
    void *sslCtx_ = nullptr; // SSL_CTX*
    void release();
};

/// 极简 WebSocket 客户端（Text 帧，支持 ws/wss）
class WebSocket {
public:
    ~WebSocket();
    bool connect(const std::string &url, const std::string &bearerToken, std::string *err);
    /// 设置退出标志：读超时时检查，Ctrl+C 可立即返回
    void setStopFlag(const std::atomic<bool> *f) { stop_ = f; }
    /// 设置读超时（毫秒）
    void setReadTimeout(int ms) { sock_.setReadTimeout(ms); }
    bool sendText(const std::string &text, std::string *err = nullptr);
    /// 阻塞读一条完整文本消息；false = 连接结束或错误
    bool recvText(std::string &out, std::string *err = nullptr);
    void close();
    bool isOpen() const { return open_; }

private:
    Socket sock_;
    std::string host_, path_;
    int port_ = 80;
    bool tls_ = false;
    bool open_ = false;
    const std::atomic<bool> *stop_ = nullptr;

    bool readExact(void *buf, size_t n);
    bool sendFrame(uint8_t opcode, const std::string &payload);
};

struct HttpResponse {
    int status = 0;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

/// 一次性 HTTP 请求（用于 dev-token 等小请求）
bool httpRequest(const std::string &url, const std::string &method,
                 const std::vector<std::pair<std::string, std::string>> &headers,
                 const std::string &body, HttpResponse &out, std::string *err = nullptr);

std::string httpHeader(const HttpResponse &r, const std::string &name);

} // namespace lite
