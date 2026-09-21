// 极简 JSON（解析 + 序列化），仅为 tunnel-lite 协议服务，无第三方依赖
#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lite {

struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<J> a;
    std::map<std::string, J> o;

    // ---- 构造 ----
    static J S(const std::string &v) { J j; j.t = STR; j.s = v; return j; }
    static J N(double v) { J j; j.t = NUM; j.num = v; return j; }
    static J B(bool v) { J j; j.t = BOOL; j.b = v; return j; }
    static J A() { J j; j.t = ARR; return j; }
    static J O() { J j; j.t = OBJ; return j; }

    // ---- 访问 ----
    bool isNull() const { return t == NUL; }
    const J &at(const std::string &k) const {
        static const J nul;
        auto it = o.find(k);
        return it == o.end() ? nul : it->second;
    }
    bool has(const std::string &k) const { return t == OBJ && o.count(k) > 0; }
    std::string str(const std::string &k, const std::string &d = "") const {
        const J &v = at(k);
        return v.t == STR ? v.s : d;
    }
    double numv(const std::string &k, double d = 0) const {
        const J &v = at(k);
        return v.t == NUM ? v.num : d;
    }
    bool boolean(const std::string &k, bool d = false) const {
        const J &v = at(k);
        return v.t == BOOL ? v.b : d;
    }
    void set(const std::string &k, J v) {
        t = OBJ;
        o[k] = std::move(v);
    }
    void push(J v) {
        t = ARR;
        a.push_back(std::move(v));
    }

    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }

    void write(std::string &out) const {
        switch (t) {
        case NUL:
            out += "null";
            break;
        case BOOL:
            out += b ? "true" : "false";
            break;
        case NUM: {
            std::ostringstream os;
            if (num == (long long)num)
                os << (long long)num;
            else
                os << num;
            out += os.str();
            break;
        }
        case STR:
            writeStr(out, s);
            break;
        case ARR:
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) {
                if (i)
                    out += ',';
                a[i].write(out);
            }
            out += ']';
            break;
        case OBJ:
            out += '{';
            {
                bool first = true;
                for (auto &kv : o) {
                    if (!first)
                        out += ',';
                    first = false;
                    writeStr(out, kv.first);
                    out += ':';
                    kv.second.write(out);
                }
            }
            out += '}';
            break;
        }
    }

    static void writeStr(std::string &out, const std::string &v) {
        out += '"';
        for (char c : v) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
            }
        }
        out += '"';
    }

    static J parse(const std::string &text, std::string *err = nullptr) {
        size_t i = 0;
        J v = parseValue(text, i, err);
        return v;
    }

private:
    static void skipWs(const std::string &s, size_t &i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    static J parseValue(const std::string &s, size_t &i, std::string *err) {
        skipWs(s, i);
        if (i >= s.size()) {
            if (err) *err = "unexpected end";
            return J();
        }
        char c = s[i];
        if (c == '{') return parseObj(s, i, err);
        if (c == '[') return parseArr(s, i, err);
        if (c == '"') {
            J j;
            j.t = STR;
            j.s = parseStr(s, i);
            return j;
        }
        if (s.compare(i, 4, "true") == 0) { i += 4; J j; j.t = BOOL; j.b = true; return j; }
        if (s.compare(i, 5, "false") == 0) { i += 5; J j; j.t = BOOL; j.b = false; return j; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return J(); }
        // number
        size_t start = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' ||
                                s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            ++i;
        J j;
        j.t = NUM;
        try {
            j.num = std::stod(s.substr(start, i - start));
        } catch (...) {
            j.num = 0;
        }
        return j;
    }
    static std::string parseStr(const std::string &s, size_t &i) {
        std::string out;
        ++i; // skip "
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'u': {
                    if (i + 4 < s.size()) {
                        unsigned code = 0;
                        for (int k = 1; k <= 4; ++k) {
                            char h = s[i + k];
                            code = code * 16 + (isdigit((unsigned char)h) ? h - '0'
                                                                          : (tolower(h) - 'a' + 10));
                        }
                        i += 4;
                        // 仅处理 BMP
                        if (code < 0x80) {
                            out += (char)code;
                        } else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                    }
                    break;
                }
                default: out += s[i];
                }
            } else {
                out += s[i];
            }
            ++i;
        }
        ++i; // skip closing "
        return out;
    }
    static J parseObj(const std::string &s, size_t &i, std::string *err) {
        J j;
        j.t = OBJ;
        ++i; // {
        skipWs(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return j; }
        while (i < s.size()) {
            skipWs(s, i);
            if (s[i] != '"') {
                if (err) *err = "expected key";
                break;
            }
            std::string key = parseStr(s, i);
            skipWs(s, i);
            if (i < s.size() && s[i] == ':') ++i;
            J val = parseValue(s, i, err);
            j.o[key] = std::move(val);
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            break;
        }
        return j;
    }
    static J parseArr(const std::string &s, size_t &i, std::string *err) {
        J j;
        j.t = ARR;
        ++i; // [
        skipWs(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return j; }
        while (i < s.size()) {
            J val = parseValue(s, i, err);
            j.a.push_back(std::move(val));
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            break;
        }
        return j;
    }
};

} // namespace lite
