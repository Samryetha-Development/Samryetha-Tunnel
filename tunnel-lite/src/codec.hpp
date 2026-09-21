// 协议编解码：JSON（兼容）与私有二进制（v3）
#pragma once

#include "json.hpp"
#include "util.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace lite {

enum class Mode { Json, Binary };

struct TunnelCfg {
    std::string id, proto, sub, path, local;
    /// 连接分组名：同一 conn 的隧道共用一条 WebSocket（留空表示默认组）
    std::string conn;
};

struct EffectiveT {
    std::string id, proto, url;
    int port = 0;
};

struct OpenStream {
    uint64_t streamId = 0;
    std::string tunnelId, proto, method, path;
    std::map<std::string, std::string> headers;
    std::string body;
};

/// 解析后的服务端消息
struct InMsg {
    std::string type; // register_ack | ping | open_stream | close_stream | chunk | mgmt_resp
    bool ackOk = false;
    std::string ackError;
    std::vector<EffectiveT> tunnels;
    OpenStream os;
    uint64_t streamId = 0;
    std::string data;
    // mgmt_resp
    uint64_t reqId = 0;
    int status = 0;
    std::string respBody;
};

// ==================== 二进制 写/读 ====================

struct W {
    std::string b;
    void u8(uint8_t v) { b.push_back((char)v); }
    void u16(uint16_t v) {
        b.push_back((char)(v & 0xFF));
        b.push_back((char)((v >> 8) & 0xFF));
    }
    void varint(uint64_t v) {
        while (v >= 0x80) {
            b.push_back((char)((v & 0x7F) | 0x80));
            v >>= 7;
        }
        b.push_back((char)v);
    }
    void str(const std::string &s) {
        varint(s.size());
        b += s;
    }
    void bytes(const std::string &s) {
        varint(s.size());
        b += s;
    }
    void headers(const std::map<std::string, std::string> &h) {
        u16((uint16_t)h.size());
        for (const auto &kv : h) {
            str(kv.first);
            str(kv.second);
        }
    }
};

struct R {
    const std::string &d;
    size_t i = 0;
    explicit R(const std::string &s) : d(s) {}
    void need(size_t n) const {
        if (i + n > d.size())
            throw std::runtime_error("帧被截断");
    }
    uint8_t u8() {
        need(1);
        return (uint8_t)d[i++];
    }
    uint16_t u16() {
        need(2);
        uint16_t v = (uint8_t)d[i] | ((uint16_t)(uint8_t)d[i + 1] << 8);
        i += 2;
        return v;
    }
    uint64_t varint() {
        uint64_t v = 0;
        int shift = 0;
        for (;;) {
            need(1);
            uint8_t b = (uint8_t)d[i++];
            v |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80))
                return v;
            shift += 7;
            if (shift > 63)
                throw std::runtime_error("varint 溢出");
        }
    }
    std::string str() {
        uint64_t n = varint();
        need((size_t)n);
        std::string s = d.substr(i, (size_t)n);
        i += (size_t)n;
        return s;
    }
    std::string bytes() { return str(); }
    std::map<std::string, std::string> headers() {
        uint16_t n = u16();
        std::map<std::string, std::string> h;
        for (uint16_t k = 0; k < n; ++k) {
            std::string key = str();
            std::string val = str();
            h[key] = val;
        }
        return h;
    }
};

// 帧类型
enum : uint8_t {
    T_REGISTER = 0x01,
    T_REGISTER_ACK = 0x02,
    T_PING = 0x03,
    T_PONG = 0x04,
    T_OPEN_STREAM = 0x05,
    T_RESPONSE_HEAD = 0x06,
    T_CHUNK = 0x07,
    T_END = 0x08,
    T_ABORT = 0x09,
    T_CLOSE_STREAM = 0x0A,
    T_MGMT = 0x0B,
    T_MGMT_RESP = 0x0C,
};

inline uint8_t protoU8(const std::string &p) { return p == "tcp" ? 1 : 0; }
inline std::string u8Proto(uint8_t b) { return b == 1 ? "tcp" : "http"; }

// ==================== 编码（客户端 -> 服务端）====================

inline std::string encRegister(Mode mode, const std::string &clientId,
                               const std::vector<TunnelCfg> &ts) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_REGISTER);
        w.str(clientId);
        w.u16((uint16_t)ts.size());
        for (const auto &t : ts) {
            w.str(t.id);
            w.u8(protoU8(t.proto.empty() ? "http" : t.proto));
            w.str(t.sub);
            w.str(t.path);
            w.str(t.local);
        }
        return w.b;
    }
    J root = J::O();
    root.set("type", J::S("register"));
    root.set("client_id", J::S(clientId));
    J arr = J::A();
    for (const auto &t : ts) {
        J o = J::O();
        o.set("tunnel_id", J::S(t.id));
        o.set("proto", J::S(t.proto.empty() ? "http" : t.proto));
        if (!t.sub.empty())
            o.set("subdomain", J::S(t.sub));
        if (!t.path.empty())
            o.set("path_prefix", J::S(t.path));
        if (!t.local.empty())
            o.set("local_addr", J::S(t.local));
        arr.push(std::move(o));
    }
    root.set("tunnels", std::move(arr));
    return root.dump();
}

inline std::string encPong(Mode mode) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_PONG);
        return w.b;
    }
    J o = J::O();
    o.set("type", J::S("pong"));
    return o.dump();
}

inline std::string encHead(Mode mode, uint64_t sid, int status,
                           const std::map<std::string, std::string> &headers) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_RESPONSE_HEAD);
        w.varint(sid);
        w.u16((uint16_t)status);
        w.headers(headers);
        return w.b;
    }
    J h = J::O();
    for (const auto &kv : headers)
        h.set(kv.first, J::S(kv.second));
    J o = J::O();
    o.set("type", J::S("response_head"));
    o.set("stream_id", J::N((double)sid));
    o.set("status", J::N(status));
    o.set("headers", std::move(h));
    return o.dump();
}

inline std::string encChunk(Mode mode, uint64_t sid, const std::string &data) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_CHUNK);
        w.varint(sid);
        w.bytes(data);
        return w.b;
    }
    J o = J::O();
    o.set("type", J::S("chunk"));
    o.set("stream_id", J::N((double)sid));
    o.set("data_b64", J::S(b64encode(data)));
    return o.dump();
}

inline std::string encEnd(Mode mode, uint64_t sid) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_END);
        w.varint(sid);
        return w.b;
    }
    J o = J::O();
    o.set("type", J::S("end"));
    o.set("stream_id", J::N((double)sid));
    return o.dump();
}

inline std::string encAbort(Mode mode, uint64_t sid, const std::string &reason) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_ABORT);
        w.varint(sid);
        w.str(reason);
        return w.b;
    }
    J o = J::O();
    o.set("type", J::S("abort"));
    o.set("stream_id", J::N((double)sid));
    o.set("reason", J::S(reason));
    return o.dump();
}

inline std::string encMgmt(Mode mode, uint64_t reqId, const std::string &method,
                           const std::string &path, const std::string &body) {
    if (mode == Mode::Binary) {
        W w;
        w.u8(T_MGMT);
        w.varint(reqId);
        w.str(method);
        w.str(path);
        w.bytes(body);
        return w.b;
    }
    J o = J::O();
    o.set("type", J::S("mgmt"));
    o.set("req_id", J::N((double)reqId));
    o.set("method", J::S(method));
    o.set("path", J::S(path));
    o.set("body_b64", J::S(b64encode(body)));
    return o.dump();
}

// ==================== 解码（服务端 -> 客户端）====================

inline InMsg decServer(Mode mode, const std::string &raw) {
    InMsg m;
    if (mode == Mode::Binary) {
        R r(raw);
        uint8_t t = r.u8();
        switch (t) {
        case T_REGISTER_ACK: {
            m.type = "register_ack";
            m.ackOk = r.u8() != 0;
            m.ackError = r.str();
            uint16_t n = r.u16();
            for (uint16_t k = 0; k < n; ++k) {
                EffectiveT e;
                e.id = r.str();
                e.proto = u8Proto(r.u8());
                e.url = r.str();
                e.port = r.u16();
                m.tunnels.push_back(e);
            }
            break;
        }
        case T_PING:
            m.type = "ping";
            break;
        case T_OPEN_STREAM: {
            m.type = "open_stream";
            m.os.streamId = r.varint();
            m.os.tunnelId = r.str();
            m.os.proto = u8Proto(r.u8());
            m.os.method = r.str();
            m.os.path = r.str();
            m.os.headers = r.headers();
            m.os.body = r.bytes();
            m.streamId = m.os.streamId;
            break;
        }
        case T_CLOSE_STREAM:
            m.type = "close_stream";
            m.streamId = r.varint();
            break;
        case T_CHUNK:
            m.type = "chunk";
            m.streamId = r.varint();
            m.data = r.bytes();
            break;
        case T_MGMT_RESP:
            m.type = "mgmt_resp";
            m.reqId = r.varint();
            m.status = (int)r.u16();
            m.respBody = r.bytes();
            break;
        default:
            m.type = "unknown";
        }
        return m;
    }

    std::string perr;
    J j = J::parse(raw, &perr);
    m.type = j.str("type");
    if (m.type == "register_ack") {
        m.ackOk = j.boolean("ok");
        m.ackError = j.str("error");
        for (const auto &t : j.at("tunnels").a) {
            EffectiveT e;
            e.id = t.str("tunnel_id");
            e.proto = t.str("proto");
            e.url = t.str("public_url");
            e.port = (int)t.numv("public_port");
            m.tunnels.push_back(e);
        }
    } else if (m.type == "open_stream") {
        m.os.streamId = (uint64_t)j.numv("stream_id");
        m.os.tunnelId = j.str("tunnel_id");
        m.os.proto = j.str("proto", "http");
        m.os.method = j.str("method", "GET");
        m.os.path = j.str("path", "/");
        if (j.has("headers")) {
            for (const auto &kv : j.at("headers").o)
                m.os.headers[kv.first] = kv.second.s;
        }
        m.os.body = b64decodeStr(j.str("body_b64"));
        m.streamId = m.os.streamId;
    } else if (m.type == "close_stream") {
        m.streamId = (uint64_t)j.numv("stream_id");
    } else if (m.type == "chunk") {
        m.streamId = (uint64_t)j.numv("stream_id");
        m.data = b64decodeStr(j.str("data_b64"));
    } else if (m.type == "mgmt_resp") {
        m.reqId = (uint64_t)j.numv("req_id");
        m.status = (int)j.numv("status");
        m.respBody = b64decodeStr(j.str("body_b64"));
    }
    return m;
}

} // namespace lite
