// tunnel-lite：不依赖 Qt 的极简跨平台客户端（macOS/Linux/Windows）
// 目标：单文件小体积、低内存占用，适合服务器/长期常驻。
// 用法：
//   tunnel-lite --server ws://127.0.0.1:18090/tunnel --dev-token you@example.com \
//               --tunnel id=web,path=/web,local=127.0.0.1:8080
//   tunnel-lite --server wss://frp.example.com/tunnel --token tun_xxx --tunnel ...

#include "json.hpp"
#include "net.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace lite;

static std::atomic<bool> g_stop{false};

// ---------------- 工具 ----------------

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
    std::cout << "[" << nowStr() << "][" << level << "][" << src << "] " << msg << std::endl;
}

struct TunnelDef {
    std::string id;
    std::string proto = "http";
    std::string subdomain;
    std::string pathPrefix;
    std::string localAddr;
};

static bool parseTunnelSpec(const std::string &spec, TunnelDef &d) {
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
        else if (k == "sub" || k == "subdomain") d.subdomain = v;
        else if (k == "path" || k == "prefix" || k == "path_prefix") d.pathPrefix = v;
        else if (k == "local" || k == "local_addr") d.localAddr = v;
    }
    return !d.id.empty() && !d.localAddr.empty();
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
        return ws->sendText(s);
    }
};

struct Stream {
    uint64_t id = 0;
    std::string proto;
    std::string tunnelId;
    std::string localAddr;
    Socket sock;
    std::mutex mtx;              // 保护 sock 写与 pending
    std::atomic<bool> connected{false};
    std::atomic<bool> closed{false};
    std::string pending;         // TCP：本地连接建立前的早到数据
};

static std::mutex g_streamsMtx;
static std::map<uint64_t, std::shared_ptr<Stream>> g_streams;

static std::string buildRegister(const std::string &clientId, const std::vector<TunnelDef> &ts) {
    J root = J::O();
    root.set("type", J::S("register"));
    root.set("client_id", J::S(clientId));
    J arr = J::A();
    for (const auto &t : ts) {
        J o = J::O();
        o.set("tunnel_id", J::S(t.id));
        o.set("proto", J::S(t.proto.empty() ? "http" : t.proto));
        if (!t.subdomain.empty())
            o.set("subdomain", J::S(t.subdomain));
        if (!t.pathPrefix.empty())
            o.set("path_prefix", J::S(t.pathPrefix));
        if (!t.localAddr.empty())
            o.set("local_addr", J::S(t.localAddr));
        arr.push(J(std::move(o)));
    }
    root.set("tunnels", arr);
    return root.dump();
}

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

static void handleHttpStream(const std::shared_ptr<Session> &sess, const std::shared_ptr<Stream> &st,
                             const J &open) {
    const std::string method = open.str("method", "GET");
    const std::string path = open.str("path", "/");
    const std::string body = b64decodeStr(open.str("body_b64"));

    std::string host;
    int port;
    splitAddr(st->localAddr, host, port);
    std::string err;
    if (!st->sock.connectTo(host, port, &err)) {
        log("err", st->tunnelId, "本地连接失败: " + err);
        J ab = J::O();
        ab.set("type", J::S("abort"));
        ab.set("stream_id", J::N((double)st->id));
        ab.set("reason", J::S(err));
        sess->send(ab.dump());
        return;
    }

    // 组装请求
    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + st->localAddr + "\r\n";
    req += "Connection: close\r\n";
    if (open.has("headers")) {
        const J &hs = open.at("headers");
        for (const auto &kv : hs.o) {
            std::string k = kv.first;
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk == "host" || lk == "content-length" || lk == "connection" ||
                lk == "transfer-encoding" || lk == "accept-encoding")
                continue;
            req += k + ": " + kv.second.s + "\r\n";
        }
    }
    if (!body.empty())
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n";
    req += body;
    if (!st->sock.writeAll(req.data(), req.size())) {
        log("err", st->tunnelId, "写本地请求失败");
        return;
    }

    std::string head;
    if (!readHttpHead(st->sock, head)) {
        J ab = J::O();
        ab.set("type", J::S("abort"));
        ab.set("stream_id", J::N((double)st->id));
        ab.set("reason", J::S("本地无响应"));
        sess->send(ab.dump());
        return;
    }

    // 解析状态行与头
    auto lineEnd = head.find("\r\n");
    std::string statusLine = head.substr(0, lineEnd);
    std::istringstream ls(statusLine);
    std::string http, reason;
    int status = 0;
    ls >> http >> status;
    std::getline(ls, reason);

    J headers = J::O();
    bool chunked = false;
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
            if (lk != "content-length" && lk != "connection" && lk != "transfer-encoding")
                headers.set(k, J::S(v));
        }
        pos = e + 2;
    }

    J headMsg = J::O();
    headMsg.set("type", J::S("response_head"));
    headMsg.set("stream_id", J::N((double)st->id));
    headMsg.set("status", J::N(status));
    headMsg.set("headers", headers);
    sess->send(headMsg.dump());

    auto sendChunk = [&](const std::string &data) {
        if (data.empty())
            return;
        J c = J::O();
        c.set("type", J::S("chunk"));
        c.set("stream_id", J::N((double)st->id));
        c.set("data_b64", J::S(b64encode(data)));
        sess->send(c.dump());
    };

    char buf[16384];
    long total = 0;
    if (chunked) {
        // 解析 chunked
        for (;;) {
            std::string sizeLine;
            char c;
            while (true) {
                long r = st->sock.readSome(&c, 1);
                if (r <= 0)
                    break;
                if (c == '\n')
                    break;
                if (c != '\r')
                    sizeLine += c;
            }
            if (sizeLine.empty())
                break;
            long sz = std::strtol(sizeLine.c_str(), nullptr, 16);
            if (sz <= 0)
                break;
            long got = 0;
            while (got < sz) {
                long r = st->sock.readSome(buf, std::min<long>(sizeof(buf), sz - got));
                if (r <= 0)
                    break;
                sendChunk(std::string(buf, (size_t)r));
                got += r;
            }
            char crlf[2];
            st->sock.readSome(crlf, 2);
        }
    } else {
        while (true) {
            if (contentLength >= 0 && total >= contentLength)
                break;
            size_t want = sizeof(buf);
            if (contentLength >= 0)
                want = std::min<size_t>(sizeof(buf), (size_t)(contentLength - total));
            long r = st->sock.readSome(buf, want);
            if (r <= 0)
                break;
            sendChunk(std::string(buf, (size_t)r));
            total += r;
        }
    }

    J end = J::O();
    end.set("type", J::S("end"));
    end.set("stream_id", J::N((double)st->id));
    sess->send(end.dump());
}

static void handleTcpStream(const std::shared_ptr<Session> &sess, const std::shared_ptr<Stream> &st) {
    std::string host;
    int port;
    splitAddr(st->localAddr, host, port);
    std::string err;
    if (!st->sock.connectTo(host, port, &err)) {
        J ab = J::O();
        ab.set("type", J::S("abort"));
        ab.set("stream_id", J::N((double)st->id));
        ab.set("reason", J::S(err));
        sess->send(ab.dump());
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
    // 本地 -> 服务端
    char buf[16384];
    long r;
    while (!st->closed && (r = st->sock.readSome(buf, sizeof(buf))) > 0) {
        J c = J::O();
        c.set("type", J::S("chunk"));
        c.set("stream_id", J::N((double)st->id));
        c.set("data_b64", J::S(b64encode(std::string(buf, (size_t)r))));
        if (!sess->send(c.dump()))
            break;
    }
    if (!st->closed) {
        J e = J::O();
        e.set("type", J::S("end"));
        e.set("stream_id", J::N((double)st->id));
        sess->send(e.dump());
    }
    std::lock_guard<std::mutex> g(g_streamsMtx);
    g_streams.erase(st->id);
}

static void eraseStream(uint64_t id) {
    std::lock_guard<std::mutex> g(g_streamsMtx);
    g_streams.erase(id);
}

// ---------------- 控制循环 ----------------

static bool runOnce(const std::string &server, const std::string &token, const std::string &clientId,
                    const std::vector<TunnelDef> &tunnels) {
    auto sess = std::make_shared<Session>();
    sess->ws = std::make_shared<WebSocket>();
    std::string err;
    if (!sess->ws->connect(server, token, &err)) {
        log("warn", "net", "连接失败: " + err);
        return false;
    }
    log("ok", "net", "控制通道已建立，注册 " + std::to_string(tunnels.size()) + " 条隧道");
    sess->send(buildRegister(clientId, tunnels));

    while (!g_stop) {
        std::string text;
        if (!sess->ws->recvText(text, &err)) {
            log("info", "net", "连接结束: " + err);
            break;
        }
        std::string perr;
        J m = J::parse(text, &perr);
        const std::string type = m.str("type");

        if (type == "ping") {
            J p = J::O();
            p.set("type", J::S("pong"));
            sess->send(p.dump());
        } else if (type == "register_ack") {
            if (m.boolean("ok")) {
                for (const auto &t : m.at("tunnels").a)
                    log("ok", t.str("tunnel_id"), "公网地址 " + t.str("public_url"));
            } else {
                log("err", "net", "注册失败: " + m.str("error"));
            }
        } else if (type == "open_stream") {
            auto st = std::make_shared<Stream>();
            st->id = (uint64_t)m.numv("stream_id");
            st->proto = m.str("proto", "http");
            st->tunnelId = m.str("tunnel_id");
            st->localAddr = m.str("local_addr");
            // local_addr 由客户端配置决定
            for (const auto &t : tunnels)
                if (t.id == st->tunnelId)
                    st->localAddr = t.localAddr;
            if (st->localAddr.empty()) {
                J ab = J::O();
                ab.set("type", J::S("abort"));
                ab.set("stream_id", J::N((double)st->id));
                ab.set("reason", J::S("unknown tunnel"));
                sess->send(ab.dump());
                continue;
            }
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                g_streams[st->id] = st;
            }
            if (st->proto == "tcp") {
                std::thread([sess, st]() { handleTcpStream(sess, st); }).detach();
            } else {
                std::thread([sess, st, m]() {
                    handleHttpStream(sess, st, m);
                    eraseStream(st->id);
                }).detach();
            }
        } else if (type == "chunk") {
            const uint64_t id = (uint64_t)m.numv("stream_id");
            std::shared_ptr<Stream> st;
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                auto it = g_streams.find(id);
                if (it != g_streams.end())
                    st = it->second;
            }
            if (st) {
                std::string data = b64decodeStr(m.str("data_b64"));
                std::lock_guard<std::mutex> l(st->mtx);
                if (st->connected)
                    st->sock.writeAll(data.data(), data.size());
                else
                    st->pending += data;
            }
        } else if (type == "close_stream") {
            const uint64_t id = (uint64_t)m.numv("stream_id");
            std::shared_ptr<Stream> st;
            {
                std::lock_guard<std::mutex> g(g_streamsMtx);
                auto it = g_streams.find(id);
                if (it != g_streams.end())
                    st = it->second;
                g_streams.erase(id);
            }
            if (st) {
                st->closed = true;
                st->sock.shutdownAll();
            }
        }
    }
    sess->alive = false;
    sess->ws->close();
    // 清理未完成流
    std::lock_guard<std::mutex> g(g_streamsMtx);
    for (auto &kv : g_streams) {
        kv.second->closed = true;
        kv.second->sock.shutdownAll();
    }
    g_streams.clear();
    return true;
}

// ---------------- main ----------------

static void usage() {
    std::cout <<
        "tunnel-lite - 轻量跨平台客户端（无 Qt）\n\n"
        "用法:\n"
        "  tunnel-lite --server <ws://|wss://.../tunnel> [--token T | --dev-token EMAIL]\n"
        "              [--client-id ID] [--config file.json]\n"
        "              --tunnel id=ID,proto=http|tcp,sub=..,path=..,local=host:port（可重复）\n\n"
        "示例:\n"
        "  tunnel-lite --server ws://127.0.0.1:18090/tunnel --dev-token a@b.com \\\n"
        "              --tunnel id=web,path=/web,local=127.0.0.1:8080\n";
}

int main(int argc, char **argv) {
    std::string server, token, devToken, clientId = "lite", configPath;
    std::vector<TunnelDef> tunnels;
    bool noReconnect = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string &out) {
            if (i + 1 < argc)
                out = argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "-s" || a == "--server") next(server);
        else if (a == "-t" || a == "--token") next(token);
        else if (a == "--dev-token") next(devToken);
        else if (a == "--client-id") next(clientId);
        else if (a == "-c" || a == "--config") next(configPath);
        else if (a == "--no-reconnect") noReconnect = true;
        else if (a == "--tunnel") {
            std::string spec;
            next(spec);
            TunnelDef d;
            if (parseTunnelSpec(spec, d))
                tunnels.push_back(d);
            else {
                std::cerr << "隧道参数非法: " << spec << "\n";
                return 2;
            }
        }
    }

    // 配置文件（可选）
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
                    TunnelDef d;
                    d.id = t.str("tunnel_id");
                    d.proto = t.str("proto", "http");
                    d.subdomain = t.str("subdomain");
                    d.pathPrefix = t.str("path_prefix");
                    d.localAddr = t.str("local_addr");
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

    // dev-token 换取
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

    int backoff = 1;
    while (!g_stop) {
        if (runOnce(server, token, clientId, tunnels))
            backoff = 1;
        if (g_stop || noReconnect)
            break;
        log("warn", "net", std::to_string(backoff) + " 秒后重连…");
        for (int i = 0; i < backoff * 10 && !g_stop; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoff = std::min(backoff * 2, 30);
    }
    log("info", "cli", "已退出");
    return 0;
}
