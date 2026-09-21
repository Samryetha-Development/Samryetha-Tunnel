#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace lite {

inline std::string b64encode(const uint8_t *data, size_t len) {
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t n = data[i] << 16;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == len) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

inline std::string b64encode(const std::string &s) {
    return b64encode(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

inline std::vector<uint8_t> b64decode(const std::string &in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0)
            continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

inline std::string b64decodeStr(const std::string &in) {
    auto v = b64decode(in);
    return std::string(v.begin(), v.end());
}

inline std::string randomB64(size_t nbytes) {
    std::vector<uint8_t> b(nbytes);
    std::random_device rd;
    for (size_t i = 0; i < nbytes; ++i)
        b[i] = (uint8_t)(rd() & 0xFF);
    return b64encode(b.data(), b.size());
}

} // namespace lite
