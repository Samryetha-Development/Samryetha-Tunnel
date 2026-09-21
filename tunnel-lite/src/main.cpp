// tunnel-lite：不依赖 Qt 的极简跨平台客户端（macOS/Linux/Windows）
// 支持两种应用层协议：
//   json    —— 兼容模式（WebSocket 文本 + JSON/base64）
//   binary  —— 私有二进制帧 v3（更少字节、更低 CPU/延迟）
// 用法：
//   tunnel-lite --server ws://127.0.0.1:18090/tunnel --dev-token you@example.com \
//               --tunnel id=web,path=/web,local=127.0.0.1:8080 [--proto binary]

#include "codec.hpp"
#include "json.hpp"
#include "net.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <unistd.h>
#define ISATTY isatty
#define FILENO fileno
#endif

using namespace lite;

static std::atomic<bool> g_stop{false};
static Mode g_mode = Mode::Json;
static const char *kVersion = "0.4.0";

// ---------------- 日志 ----------------

static bool colorEnabled() {
    static int c = -1;
    if (c < 0)
        c = (ISATTY(FILENO(stdout)) && !std::getenv("NO_COLOR")) ? 1 : 0;
    return c == 1;
}

static const char *levelColor(const std::string &l) {
    if (l == "ok") return "\033[32m";
    if (l == "warn") return "\033[33m";
    if (l == "err") return "\033[31m";
    return "\033[90m";
}

static std::string nowStr() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

static void log(const std::string &level, const std::string &src, const std::string &msg) {
    static std::mutex m;
    std::lock_guard<std::mutex> l(m);
    if (colorEnabled())
        std::cout << "\033[90m[" << nowStr() << "]\033[0m " << levelColor(level) << level
                  << "\033[0m \033[36m" << src << "\033[0m " << msg << "\n";
    else
        std::cout << "[" << nowStr() << "][" << level << "][" << src << "] " << msg << "\n";
    std::cout.flush();
}

static std::string fmtBytes(long long b) {
    char buf[64];
    if (b < 1024)
        snprintf(buf, sizeof(buf), "%lldB", b);
    else if (b < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1fKB", b / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%.2fMB", b / 1024.0 / 1024.0);
    return buf;
}

// ---------------- 参数 ----------------

static bool parseTunnelSpec(const std::string &spec, TunnelCfg &d) {
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ',')) {
        auto eq = part.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = part.substr(0, eq);
        std::string v = part.substr(eq + 1);
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (k == "id" || k == "tunnel_id") d.id = v;
        else if (k == "proto") d.proto = v;
        else if (k == "sub" || k == "subdomain") d.sub = v;
        else if (k == "path" || k == "prefix" || k == "path_prefix") d.path = v;
        else if (k == "local" || k == "local_addr") d.local = v;
        else if (k == "conn") d.conn = v;
    }
    return !d.id.empty() && !d.local.empty();
}

static std::string httpBaseFromWs(const std::string &ws) {
    std::string s = ws;
    if (s.rfind("wss://", 0) == 0)
        s = "https://" + s.substr(6);
    else if (s.rfind("ws://", 0) == 0)
        s = "http://" + s.substr(5);
    auto p = s.find("/tunnel");
    if (p != std::string::npos)
        s = s.substr(0, p);
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    return s;
}

static void splitAddr(const std::string &addr, std::string &host, int &port) {
    auto c = addr.rfind(':');
    if (c == std::string::npos) {
        host = addr;
        port = 80;
        return;
    }
    host = addr.substr(0, c);
    port = std::atoi(addr.substr(c + 1).c_str());
}

// ---------------- 状态 ----------------

struct Session {
    std::shared_ptr<WebSocket> ws;
    std::mutex sendMtx;
    std::atomic<bool> alive{true};

    bool send(const std::string &s) {
        std::lock_guard<std::mutex> l(sendMtx);
        if (!alive || !ws)
            return false;
        return g_mode == Mode::Binary ? ws->sendBinary(s) : ws->sendText(s);
    }
};

struct Stream {
    uint64_t id = 0;
    std::string proto;
    std::string tunnelId;
    std::string localAddr;
    Socket sock;
    std::mutex mtx;
    std::atomic<bool> connected{false};
    std::atomic<bool> closed{false};
    std::string pending;
};

static std::mutex g_streamsMtx;
static std::map<uint64_t, std::shared_ptr<Stream>> g_streams;

// ---------------- 本地服务连接池（keep-alive 复用，省掉每请求握手）----------------

struct IdleConn {
    std::shared_ptr<Socket> sock;
    std::chrono::steady_clock::time_point at;
};

class LocalPool {
public:
    std::shared_ptr<Socket> acquire(const std::string &addr, bool &reused) {
        reused = false;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> l(m_);
        auto it = idle_.find(addr);
        if (it != idle_.end()) {
            auto &v = it->second;
            while (!v.empty()) {
                IdleConn c = v.back();
                v.pop_back();
                if (now - c.at > std::chrono::seconds(kIdleSecs))
                    continue; // 空闲过久，丢弃
                reused = true;
                return c.sock;
            }
        }
        return nullptr;
    }
    void release(const std::string &addr, const std::shared_ptr<Socket> &s) {
        std::lock_guard<std::mutex> l(m_);
        auto &v = idle_[addr];
        if ((int)v.size() >= kMaxIdle)
            return; // 超上限，直接析构关闭
        v.push_back({s, std::chrono::steady_clock::now()});
    }
    void purge() {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> l(m_);
        for (auto &kv : idle_) {
            auto &v = kv.second;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [&](const IdleConn &c) {
                                       return now - c.at > std::chrono::seconds(kIdleSecs);
                                   }),
                    v.end());
        }
    }

private:
    static constexpr int kIdleSecs = 30;
    static constexpr int kMaxIdle = 8;
    std::mutex m_;
    std::map<std::string, std::vector<IdleConn>> idle_;
};

static LocalPool g_localPool;

// ---------------- 工作线程池（避免每个请求创建线程）----------------

class Pool {
public:
    void start(int n) {
        for (int i = 0; i < n; ++i)
            workers_.emplace_back([this]() {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> l(m_);
                        cv_.wait(l, [this]() { return stop_ || !q_.empty(); });
                        if (stop_ && q_.empty())
                            return;
                        job = std::move(q_.front());
                        q_.pop_front();
                    }
                    job();
                }
            });
    }
    void submit(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> l(m_);
            q_.push_back(std::move(fn));
        }
        cv_.notify_one();
    }
    void stop() {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_)
            if (t.joinable())
                t.join();
        workers_.clear();
    }

private:
    std::deque<std::function<void()>> q_;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};

static Pool g_pool;

// ---------------- 本地转发 ----------------

static bool readHttpHead(Socket &s, std::string &head) {
    char c;
    head.clear();
    while (head.size() < 65536) {
        long r = s.readSome(&c, 1);
        if (r <= 0)
            return false;
        head += c;
        if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0)
            return true;
    }
    return false;
}

enum class HttpOutcome { Done, Retry };

static HttpOutcome runHttpOnce(const std::shared_ptr<Session> &sess, const std::shared_ptr<Stream> &st,
                               const OpenStream &req, const std::shared_ptr<Socket> &conn,
                               long long &sent) {
    const auto t0 = std::chrono::steady_clock::now();

    std::string r = req.method + " " + req.path + " HTTP/1.1\r\n";
    r += "Host: " + st->localAddr + "\r\n";
    r += "Connection: keep-alive\r\n";
    for (const auto &kv : req.headers) {
        std::string lk = kv.first;
        std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
        if (lk == "host" || lk == "content-length" || lk == "connection" ||
            lk == "transfer-encoding" || lk == "accept-encoding")
            continue;
        r += kv.first + ": " + kv.second + "\r\n";
    }
    if (!req.body.empty())
        r += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
    r += "\r\n";
    r += req.body;
    if (!conn->writeAll(r.data(), r.size()))
        return HttpOutcome::Retry;

    std::string head;
    if (!readHttpHead(*conn, head))
        return HttpOutcome::Retry;

    auto lineEnd = head.find("\r\n");
    std::istringstream ls(head.substr(0, lineEnd));
    std::string http, reason;
    int status = 0;
    ls >> http >> status;
    std::getline(ls, reason);

    std::map<std::string, std::string> headers;
    bool chunked = false;
    bool connClose = false;
    bool explicitKeepAlive = false;
    long long contentLength = -1;
    size_t pos = lineEnd + 2;
    while (pos < head.size()) {
        auto e = head.find("\r\n", pos);
        if (e == std::string::npos || e == pos)
            break;
        std::string line = head.substr(pos, e - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
                v.erase(0, 1);
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk == "transfer-encoding" && v.find("chunked") != std::string::npos)
                chunked = true;
            if (lk == "content-length")
                contentLength = std::atoll(v.c_str());
            if (lk == "connection") {
                if (v.find("close") != std::string::npos)
                    connClose = true;
                if (v.find("keep-alive") != std::string::npos)
                    explicitKeepAlive = true;
            }
            if (lk != "content-length" && lk != "connection" && lk != "transfer-encoding")
                headers[k] = v;
        }
        pos = e + 2;
    }

    // 只有能精确判定报文结尾、且对端愿意保持连接时，连接才可复用（否则会串包/复用到已关闭连接）
    const bool http11 = http.rfind("HTTP/1.1", 0) == 0;
    bool keepAlive =
        !connClose && (chunked || contentLength >= 0) && (http11 || explicitKeepAlive);

    sess->send(encHead(g_mode, st->id, status, headers));

    auto sendChunk = [&](const std::string &data) {
        if (data.empty())
            return;
        sent += (long long)data.size();
        sess->send(encChunk(g_mode, st->id, data));
    };

    char buf[16384];
    long long total = 0;
    if (chunked) {
        for (;;) {
            std::string sizeLine;
            char c;
            while (true) {
                long rr = conn->readSome(&c, 1);
                if (rr <= 0)
                    break;
                if (c == '\n')
                    break;
                if (c != '\r')
                    sizeLine += c;
            }
            if (sizeLine.empty())
                break;
            long sz = std::strtol(sizeLine.c_str(), nullptr, 16);
            if (sz <= 0) {
                // 读完 trailer（若有）才算完整
                std::string tail;
                char cc;
                while (true) {
                    long rr = conn->readSome(&cc, 1);
                    if (rr <= 0)
                        break;
                    tail += cc;
                    if (tail.size() >= 4 && tail.compare(tail.size() - 4, 4, "\r\n\r\n") == 0)
                        break;
                    if (tail.size() >= 2 && tail == "\r\n")
                        break;
                }
                break;
            }
            long got = 0;
            while (got < sz) {
                long rr = conn->readSome(buf, std::min<long>(sizeof(buf), sz - got));
                if (rr <= 0)
                    break;
                sendChunk(std::string(buf, (size_t)rr));
                got += rr;
            }
            char crlf[2];
            conn->readSome(crlf, 2);
        }
    } else if (contentLength >= 0) {
        while (total < contentLength) {
            size_t want = std::min<size_t>(sizeof(buf), (size_t)(contentLength - total));
            long rr = conn->readSome(buf, want);
            if (rr <= 0) {
                keepAlive = false; // 没读满，连接状态不可信
                break;
            }
            sendChunk(std::string(buf, (size_t)rr));
            total += rr;
        }
    } else {
        long rr;
        while ((rr = conn->readSome(buf, sizeof(buf))) > 0)
            sendChunk(std::string(buf, (size_t)rr));
        keepAlive = false; // close-delimited，不可复用
    }

    sess->send(encEnd(g_mode, st->id));

    if (keepAlive)
        g_localPool.release(st->localAddr, conn);

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    log("info", st->tunnelId,
        std::to_string(status) + " · " + fmtBytes(sent) + " · " + std::to_string(ms) + "ms" +
            (keepAlive ? " · reuse" : ""));
    return HttpOutcome::Done;
}

static void handleHttpStream(const std::shared_ptr<Session> &sess, const std::shared_ptr<Stream> &st,
                             const OpenStream &req) {
    long long sent = 0;
    std::string host;
    int port;
    splitAddr(st->localAddr, host, port);

    for (int attempt = 0; attempt < 2; ++attempt) {
        bool reused = false;
        std::shared_ptr<Socket> conn = g_localPool.acquire(st->localAddr, reused);
        if (!conn) {
            conn = std::make_shared<Socket>();
            std::string err;
            if (!conn->connectTo(host, port, &err)) {
                log("err", st->tunnelId, "本地连接失败: " + err);
                sess->send(encAbort(g_mode, st->id, err));
                return;
            }
        }
        const HttpOutcome out = runHttpOnce(sess, st, req, conn, sent);
        if (out == HttpOutcome::Done)
            return;
        if (reused && attempt == 0) {
            log("warn", st->tunnelId, "复用的本地连接已失效，改用新连接重试");
            continue;
        }
        sess->send(encAbort(g_mode, st->id, "本地无响应"));
        return;
    }
}

static void handleTcpStream(const std::shared_ptr<Session> &sess, const std::shared_ptr<Stream> &st) {
    const auto t0 = std::chrono::steady_clock::now();
    long long total = 0;
    std::string host;
    int port;
    splitAddr(st->localAddr, host, port);
    std::string err;
    if (!st->sock.connectTo(host, port, &err)) {
        sess->send(encAbort(g_mode, st->id, err));
        return;
    }
    {
        std::lock_guard<std::mutex> l(st->mtx);
        st->connected = true;
        if (!st->pending.empty()) {
            st->sock.writeAll(st->pending.data(), st->pending.size());
            st->pending.clear();
        }
    }
    char buf[16384];
    long r;
    while (!st->closed && (r = st->sock.readSome(buf, sizeof(buf))) > 0) {
        total += r;
        if (!sess->send(encChunk(g_mode, st->id, std::string(buf, (size_t)r))))
            break;
    }
    if (!st->closed)
        sess->send(encEnd(g_mode, st->id));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    log("info", st->tunnelId, "closed · " + fmtBytes(total) + " · " + std::to_string(ms) + "ms");
    std::lock_guard<std::mutex> g(g_streamsMtx);
    g_streams.erase(st->id);
}

static void eraseStream(uint64_t id) {
    std::lock_guard<std::mutex> g(g_streamsMtx);
    g_streams.erase(id);
}

// ---------------- 控制循环 ----------------

static bool runOnce(const std::string &serverIn, const std::string &token, const std::string &clientId,
                    const std::vector<TunnelCfg> &tunnels, uint64_t &txOut, uint64_t &rxOut) {
    std::string server = serverIn;
    if (g_mode == Mode::Binary)
        server += (server.find('?') == std::string::npos ? "?v=3" : "&v=3");

    auto sess = std::make_shared<Session>();
    sess->ws = std::make_shared<WebSocket>();
    std::string err;
    if (!sess->ws->connect(server, token, &err)) {
        log("warn", "net", "连接失败: " + err);
        return false;
    }
    const char *pname = g_mode == Mode::Binary ? "binary-v3" : "json";
    log("ok", "net", std::string("控制通道已建立（") + pname + "），注册 " +
                        std::to_string(tunnels.size()) + " 条隧道");
    sess->ws->setReadTimeout(1000);
    sess->ws->setStopFlag(&g_stop);
    sess->send(encRegister(g_mode, clientId, tunnels));

    while (!g_stop) {
        std::string raw;
        bool isBin = false;
        if (!sess->ws->recvMessage(raw, isBin, &err)) {
            log("info", "net", "连接结束: " + err);
            break;
        }
        InMsg m;
        try {
            m = decServer(g_mode, raw);
        } catch (const std::exception &e) {
            log("warn", "net", std::string("非法帧: ") + e.what());
            continue;
        }

        if (m.type == "ping") {
            sess->send(encPong(g_mode));
        } else if (m.type == "register_ack") {
            if (!m.ackError.empty())
                log("warn", "net", "服务端提示: " + m.ackError);
            if (m.ackOk) {
                for (const auto &t : m.tunnels)
                    log("ok", t.id, "公网地址 " + t.url);
            } else {
                log("err", "net", "注册失败");
            }
        } else if (m.type == "open_stream") {
            auto st = std::make_shared<Stream>();
            st->id = m.os.streamId;
            st->proto = m.os.proto;
            st->tunnelId = m.os.tunnelId;
            for (const auto &t : tunnels)
                if (t.id == st->tunnelId)
                    st->localAddr = t.local;
            if (st->localAddr.empty()) {
                sess->send(encAbort(g_mode, st->id, "unknown tunnel"));
                continue;
            }
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                g_streams[st->id] = st;
            }
            if (st->proto == "tcp")
                std::thread([sess, st]() { handleTcpStream(sess, st); }).detach();
            else
                g_pool.submit([sess, st, os = m.os]() {
                    handleHttpStream(sess, st, os);
                    eraseStream(st->id);
                });
        } else if (m.type == "chunk") {
            std::shared_ptr<Stream> st;
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                auto it = g_streams.find(m.streamId);
                if (it != g_streams.end())
                    st = it->second;
            }
            if (st) {
                std::lock_guard<std::mutex> l(st->mtx);
                if (st->connected)
                    st->sock.writeAll(m.data.data(), m.data.size());
                else
                    st->pending += m.data;
            }
        } else if (m.type == "close_stream") {
            std::shared_ptr<Stream> st;
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                auto it = g_streams.find(m.streamId);
                if (it != g_streams.end())
                    st = it->second;
                g_streams.erase(m.streamId);
            }
            if (st) {
                st->closed = true;
                st->sock.shutdownAll();
            }
        }
    }
    txOut = sess->ws->txBytes();
    rxOut = sess->ws->rxBytes();
    sess->alive = false;
    sess->ws->close();
    {
        std::lock_guard<std::mutex> g(g_streamsMtx);
        for (auto &kv : g_streams) {
            kv.second->closed = true;
            kv.second->sock.shutdownAll();
        }
        g_streams.clear();
    }
    return true;
}

// ---------------- 管理 API（不再依赖网页控制台）----------------

static bool httpJson(const std::string &base, const std::string &token, const std::string &method,
                     const std::string &path, const std::string &body, HttpResponse &out,
                     std::string *err) {
    std::vector<std::pair<std::string, std::string>> hs = {{"Content-Type", "application/json"}};
    if (!token.empty())
        hs.push_back({"Authorization", "Bearer " + token});
    return httpRequest(base + path, method, hs, body, out, err);
}

static void printJson(const std::string &body) {
    J j = J::parse(body);
    std::cout << j.dump() << "\n";
}

static int apiFail(const HttpResponse &r) {
    std::cerr << "HTTP " << r.status << ": " << r.body << "\n";
    return 1;
}

static void apiUsage() {
    std::cout <<
        "tunnel-lite api - 管理命令（全部操作无需网页控制台）\n\n"
        "通用: --base <http(s)://host[:port]>  --token <API Token>\n\n"
        "  me                                  当前用户\n"
        "  token-list                          列出 API Token\n"
        "  token-create <名称>                 新建（明文只显示一次）\n"
        "  token-revoke <id>                   吊销\n"
        "  tunnel-list                         我的隧道\n"
        "  tunnel-disable <隧道ID> / tunnel-enable <隧道ID>\n"
        "  tunnel-rm <隧道ID>\n"
        "  visitor-auth <隧道ID> [--basic 用户:密码] [--ips 1.2.3.4,10.0.0.0/8]\n"
        "  visitor-auth-clear <隧道ID>\n"
        "  usage                               今日用量与配额\n"
        "  admin-users                         用户列表（需管理员）\n"
        "  admin-user-patch <id> [--role user|admin] [--disable|--enable]\n"
        "                        [--max-tunnels N] [--daily-bytes N]\n"
        "  admin-tunnels / admin-tunnel-disable <id> / admin-tunnel-enable <id>\n"
        "  admin-audit [--limit N]\n\n"
        "登录（获取 Token）:\n"
        "  tunnel-lite login --base URL --dev <邮箱>       # dev 模式\n"
        "  tunnel-lite login --base URL --device           # SSO 设备码\n";
}

static int loginMain(int argc, char **argv) {
    std::string base, dev, token;
    bool device = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string &out) { if (i + 1 < argc) out = argv[++i]; };
        if (a == "--base") next(base);
        else if (a == "--dev") next(dev);
        else if (a == "--device") device = true;
        else if (a == "-h" || a == "--help") { apiUsage(); return 0; }
    }
    if (base.empty()) { std::cerr << "缺少 --base\n"; return 2; }

    if (!dev.empty()) {
        HttpResponse r; std::string err;
        if (!httpJson(base, "", "POST", "/auth/dev-token", "{\"email\":\"" + dev + "\"}", r, &err)) {
            std::cerr << "请求失败: " << err << "\n"; return 1;
        }
        if (r.status != 200) return apiFail(r);
        J j = J::parse(r.body);
        token = j.str("token");
        std::cout << token << "\n";
        std::cerr << "已登录: " << j.at("user").str("email") << "（把上面这行保存为 API Token）\n";
        return 0;
    }
    if (device) {
        HttpResponse r; std::string err;
        if (!httpJson(base, "", "POST", "/auth/device/start", "{}", r, &err) || r.status != 200) {
            std::cerr << "设备码请求失败: " << (err.empty() ? r.body : err) << "\n"; return 1;
        }
        J s = J::parse(r.body);
        const std::string code = s.str("device_code");
        const int interval = (int)s.numv("interval", 5);
        std::cerr << "请打开 " << s.str("verification_uri") << " 输入代码 " << s.str("user_code") << "\n";
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(interval > 0 ? interval : 5));
            HttpResponse p;
            if (!httpJson(base, "", "POST", "/auth/device/poll", "{\"device_code\":\"" + code + "\"}", p, &err)) {
                std::cerr << "轮询失败: " << err << "\n"; return 1;
            }
            J j = J::parse(p.body);
            const std::string st = j.str("status");
            if (st == "ok") {
                std::cout << j.str("token") << "\n";
                std::cerr << "已登录: " << j.at("user").str("email") << "\n";
                return 0;
            }
            if (st == "error") { std::cerr << "登录失败: " << j.str("error") << "\n"; return 1; }
            std::cerr << "等待授权…\n";
        }
    }
    std::cerr << "请指定 --dev <邮箱> 或 --device\n";
    return 2;
}

static int apiMain(int argc, char **argv) {
    std::string base, token, cmd, arg, basic, ips;
    long long limit = 100, maxTunnels = -1, dailyBytes = -1;
    std::string role; int disableState = -1;
    std::vector<std::string> pos;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string &out) { if (i + 1 < argc) out = argv[++i]; };
        if (a == "--base") next(base);
        else if (a == "--token") next(token);
        else if (a == "--limit") { std::string v; next(v); limit = std::atoll(v.c_str()); }
        else if (a == "--basic") next(basic);
        else if (a == "--ips") next(ips);
        else if (a == "--role") next(role);
        else if (a == "--max-tunnels") { std::string v; next(v); maxTunnels = std::atoll(v.c_str()); }
        else if (a == "--daily-bytes") { std::string v; next(v); dailyBytes = std::atoll(v.c_str()); }
        else if (a == "--disable") disableState = 1;
        else if (a == "--enable") disableState = 0;
        else if (a == "-h" || a == "--help") { apiUsage(); return 0; }
        else pos.push_back(a);
    }
    if (pos.empty()) { apiUsage(); return 2; }
    cmd = pos[0];
    if (pos.size() > 1) arg = pos[1];
    if (base.empty()) { std::cerr << "缺少 --base（如 http://127.0.0.1:18080 或 https://frp.samryetha.com/tunnel-admin）\n"; return 2; }

    auto call = [&](const std::string &method, const std::string &path, const std::string &body) -> int {
        HttpResponse r; std::string err;
        if (!httpJson(base, token, method, path, body, r, &err)) { std::cerr << "请求失败: " << err << "\n"; return 1; }
        if (r.status >= 400) return apiFail(r);
        if (!r.body.empty()) printJson(r.body);
        else std::cout << "ok\n";
        return 0;
    };

    if (cmd == "me") return call("GET", "/auth/me", "");
    if (cmd == "token-list") return call("GET", "/api/tokens", "");
    if (cmd == "token-create") {
        if (arg.empty()) { std::cerr << "用法: token-create <名称>\n"; return 2; }
        return call("POST", "/api/tokens", "{\"name\":\"" + arg + "\"}");
    }
    if (cmd == "token-revoke") return call("DELETE", "/api/tokens/" + arg, "");
    if (cmd == "tunnel-list") return call("GET", "/api/tunnels", "");
    if (cmd == "tunnel-disable") return call("PATCH", "/api/tunnels/" + arg, "{\"disabled\":true}");
    if (cmd == "tunnel-enable") return call("PATCH", "/api/tunnels/" + arg, "{\"disabled\":false}");
    if (cmd == "tunnel-rm") return call("DELETE", "/api/tunnels/" + arg, "");
    if (cmd == "visitor-auth") {
        if (arg.empty()) { std::cerr << "用法: visitor-auth <隧道ID> [--basic u:p] [--ips ...]\n"; return 2; }
        std::string va = "{";
        bool first = true;
        if (!basic.empty()) {
            auto c = basic.find(':');
            if (c == std::string::npos) { std::cerr << "--basic 需要 用户:密码\n"; return 2; }
            va += "\"basic\":{\"user\":\"" + basic.substr(0, c) + "\",\"pass\":\"" + basic.substr(c + 1) + "\"}";
            first = false;
        }
        std::string body = "{\"visitor_auth\":" + va;  // 结尾在下面补齐
        if (!ips.empty()) {
            if (!first) body += ",";
            std::string arr = "[";
            std::stringstream ss(ips);
            std::string it; bool f2 = true;
            while (std::getline(ss, it, ',')) {
                if (it.empty()) continue;
                if (!f2) arr += ",";
                arr += "\"" + it + "\""; f2 = false;
            }
            arr += "]";
            body += "\"ips\":" + arr;
        }
        body += "}}";
        return call("PATCH", "/api/tunnels/" + arg, body);
    }
    if (cmd == "visitor-auth-clear") return call("PATCH", "/api/tunnels/" + arg, "{\"visitor_auth\":{}}");
    if (cmd == "usage") return call("GET", "/api/usage", "");
    if (cmd == "admin-users") return call("GET", "/api/admin/users", "");
    if (cmd == "admin-user-patch") {
        if (arg.empty()) { std::cerr << "用法: admin-user-patch <id> [--role ..] [--disable] [--max-tunnels N] [--daily-bytes N]\n"; return 2; }
        std::string body = "{";
        bool first = true;
        auto add = [&](const std::string &kv) { if (!first) body += ","; body += kv; first = false; };
        if (!role.empty()) add("\"role\":\"" + role + "\"");
        if (disableState >= 0) add(std::string("\"disabled\":") + (disableState ? "true" : "false"));
        if (maxTunnels >= 0) add("\"max_tunnels\":" + std::to_string(maxTunnels));
        if (dailyBytes >= 0) add("\"daily_bytes\":" + std::to_string(dailyBytes));
        body += "}";
        return call("PATCH", "/api/admin/users/" + arg, body);
    }
    if (cmd == "admin-tunnels") return call("GET", "/api/admin/tunnels", "");
    if (cmd == "admin-tunnel-disable") return call("PATCH", "/api/admin/tunnels/" + arg, "{\"disabled\":true}");
    if (cmd == "admin-tunnel-enable") return call("PATCH", "/api/admin/tunnels/" + arg, "{\"disabled\":false}");
    if (cmd == "admin-audit") return call("GET", "/api/admin/audit?limit=" + std::to_string(limit), "");
    if (cmd == "admin-overview") return call("GET", "/api/admin/overview", "");

    std::cerr << "未知命令: " << cmd << "\n";
    apiUsage();
    return 2;
}

// ---------------- main ----------------

static void usage() {
    std::cout << "tunnel-lite " << kVersion << " - 轻量跨平台客户端（无 Qt）\n\n"
        "子命令:\n"
        "  (默认)      建立隧道（见下）\n"
        "  login       获取 API Token（--dev 邮箱 / --device 设备码）\n"
        "  api         管理命令：Token/隧道/访客鉴权/用量/管理员（无网页控制台）\n"
        "  api --help  查看全部管理命令\n\n"
        "建立隧道:\n"
        "  tunnel-lite --server <ws://|wss://.../tunnel> [--token T | --dev-token EMAIL]\n"
        "              [--client-id ID] [--config file.json] [--no-reconnect]\n"
        "              [--proto json|binary] [--isolate]\n"
        "              --tunnel id=ID,proto=http|tcp,sub=..,path=..,local=host:port[,conn=名]（可重复）\n\n"
        "协议:\n"
        "  json    JSON + base64（兼容，默认）\n"
        "  binary  私有二进制帧 v3（更少字节、更低延迟）\n\n"
        "连接:\n"
        "  默认所有隧道共用一条连接；--isolate 每条隧道各一条；\n"
        "  或用 conn=名 把若干隧道分到同一条连接（互不影响，避免队头阻塞）\n\n"
        "示例:\n"
        "  tunnel-lite --server ws://127.0.0.1:18090/tunnel --dev-token a@b.com \\\n"
        "              --tunnel id=web,path=/web,local=127.0.0.1:8080 --proto binary\n";
}

int main(int argc, char **argv) {
    if (argc > 1) {
        const std::string first = argv[1];
        if (first == "api")
            return apiMain(argc, argv);
        if (first == "login")
            return loginMain(argc, argv);
    }
    std::string server, token, devToken, clientId = "lite", configPath;
    std::vector<TunnelCfg> tunnels;
    bool noReconnect = false;
    bool isolate = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string &out) {
            if (i + 1 < argc)
                out = argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "-v" || a == "--version") {
            std::cout << "tunnel-lite " << kVersion << "\n";
            return 0;
        } else if (a == "-s" || a == "--server") next(server);
        else if (a == "-t" || a == "--token") next(token);
        else if (a == "--dev-token") next(devToken);
        else if (a == "--client-id") next(clientId);
        else if (a == "-c" || a == "--config") next(configPath);
        else if (a == "--no-reconnect") noReconnect = true;
        else if (a == "--isolate") isolate = true;
        else if (a == "--proto") {
            std::string p;
            next(p);
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            if (p == "binary" || p == "bin" || p == "v3") g_mode = Mode::Binary;
            else if (p == "json") g_mode = Mode::Json;
            else {
                std::cerr << "未知协议: " << p << "（json|binary）\n";
                return 2;
            }
        } else if (a == "--tunnel") {
            std::string spec;
            next(spec);
            TunnelCfg d;
            if (parseTunnelSpec(spec, d))
                tunnels.push_back(d);
            else {
                std::cerr << "隧道参数非法: " << spec << "\n";
                return 2;
            }
        }
    }

    if (!configPath.empty()) {
        std::ifstream f(configPath);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            J cfg = J::parse(ss.str());
            if (server.empty()) server = cfg.str("server_url");
            if (token.empty()) token = cfg.str("api_token");
            if (clientId == "lite") clientId = cfg.str("client_id", clientId);
            if (tunnels.empty()) {
                for (const auto &t : cfg.at("tunnels").a) {
                    TunnelCfg d;
                    d.id = t.str("tunnel_id");
                    d.proto = t.str("proto", "http");
                    d.sub = t.str("subdomain");
                    d.path = t.str("path_prefix");
                    d.local = t.str("local_addr");
                    if (!d.id.empty())
                        tunnels.push_back(d);
                }
            }
        }
    }

    if (server.empty()) {
        std::cerr << "缺少 --server\n";
        usage();
        return 2;
    }
    if (tunnels.empty())
        std::cerr << "警告：没有配置任何隧道\n";

    if (token.empty() && !devToken.empty()) {
        const std::string base = httpBaseFromWs(server);
        std::vector<std::pair<std::string, std::string>> hs = {{"Content-Type", "application/json"}};
        HttpResponse resp;
        std::string err;
        if (!httpRequest(base + "/auth/dev-token", "POST", hs, "{\"email\":\"" + devToken + "\"}",
                         resp, &err)) {
            std::cerr << "dev 登录失败: " << err << "\n";
            return 3;
        }
        J j = J::parse(resp.body);
        token = j.str("token");
        if (token.empty()) {
            std::cerr << "dev 登录失败: " << resp.body << "\n";
            return 3;
        }
        log("ok", "auth", "登录成功: " + j.at("user").str("email", devToken));
    }

    if (token.empty()) {
        std::cerr << "缺少 --token（或用 --dev-token EMAIL）\n";
        return 2;
    }

    std::signal(SIGINT, [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });

    int poolSize = 16;
    if (const char *p = std::getenv("TUNNEL_POOL"))
        poolSize = std::max(1, std::atoi(p));
    g_pool.start(poolSize);

    // 隧道分组：conn= 显式指定；--isolate 则每条隧道各一条独立连接
    std::vector<std::pair<std::string, std::vector<TunnelCfg>>> groups;
    {
        std::map<std::string, size_t> idx;
        for (const auto &t : tunnels) {
            std::string key = !t.conn.empty() ? ("c:" + t.conn)
                                              : (isolate ? ("t:" + t.id) : std::string("default"));
            auto it = idx.find(key);
            if (it == idx.end()) {
                idx[key] = groups.size();
                groups.push_back({key, {}});
            }
            groups[idx[key]].second.push_back(t);
        }
    }
    if (groups.size() > 1)
        log("info", "cli",
            "使用 " + std::to_string(groups.size()) + " 条独立连接（避免跨隧道队头阻塞）");

    std::atomic<uint64_t> totalTx{0}, totalRx{0};
    std::vector<std::thread> workers;
    for (auto &g : groups) {
        workers.emplace_back([&, grp = g]() {
            const std::string cid =
                groups.size() > 1 ? (clientId + "@" + grp.first) : clientId;
            uint64_t tx = 0, rx = 0;
            int backoff = 1;
            while (!g_stop) {
                if (runOnce(server, token, cid, grp.second, tx, rx))
                    backoff = 1;
                if (g_stop || noReconnect)
                    break;
                log("warn", "net", std::to_string(backoff) + " 秒后重连…");
                for (int i = 0; i < backoff * 10 && !g_stop; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                backoff = std::min(backoff * 2, 30);
            }
            totalTx += tx;
            totalRx += rx;
        });
    }
    for (auto &w : workers)
        w.join();

    log("info", "cli",
        std::string("已退出（协议 ") + (g_mode == Mode::Binary ? "binary-v3" : "json") + "，" +
            std::to_string(groups.size()) + " 条连接，累计发送 " +
            fmtBytes((long long)totalTx.load()) + " / 接收 " + fmtBytes((long long)totalRx.load()) +
            "）");
    g_pool.stop();
    return 0;
}
