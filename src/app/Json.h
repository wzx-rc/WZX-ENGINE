// Minimal JSON value + parser/writer helpers for the shared scene format.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json& operator[](const std::string& k) const {
        static Json none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
    const Json& operator[](size_t i) const {
        static Json none;
        return i < a.size() ? a[i] : none;
    }
    double num(double def = 0) const { return type == Number ? n : def; }
    std::string str(const std::string& def = "") const { return type == String ? s : def; }

    static bool parse(const std::string& text, Json& out) {
        size_t i = 0;
        return parseValue(text, i, out);
    }

private:
    static void ws(const std::string& t, size_t& i) {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i;
    }
    static bool parseString(const std::string& t, size_t& i, std::string& out) {
        if (t[i] != '"') return false;
        ++i;
        while (i < t.size() && t[i] != '"') {
            if (t[i] == '\\' && i + 1 < t.size()) {
                char e = t[i + 1];
                if (e == 'n') out.push_back('\n');
                else if (e == 't') out.push_back('\t');
                else if (e == 'r') out.push_back('\r');
                else if (e == 'b') out.push_back('\b');
                else if (e == 'f') out.push_back('\f');
                else if (e == 'u' && i + 5 < t.size()) {
                    unsigned cp = (unsigned)strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16);
                    if (cp < 0x80) out.push_back((char)cp);
                    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 63))); }
                    else { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 63))); out.push_back((char)(0x80 | (cp & 63))); }
                    i += 4;
                } else out.push_back(e);
                i += 2;
            } else {
                out.push_back(t[i++]);
            }
        }
        ++i;
        return true;
    }
    static bool parseValue(const std::string& t, size_t& i, Json& v) {
        ws(t, i);
        if (i >= t.size()) return false;
        char c = t[i];
        if (c == '{') {
            v.type = Object;
            ++i;
            ws(t, i);
            if (t[i] == '}') { ++i; return true; }
            while (i < t.size()) {
                ws(t, i);
                std::string k;
                if (!parseString(t, i, k)) return false;
                ws(t, i);
                if (t[i] != ':') return false;
                ++i;
                if (!parseValue(t, i, v.o[k])) return false;
                ws(t, i);
                if (t[i] == ',') { ++i; continue; }
                if (t[i] == '}') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            v.type = Array;
            ++i;
            ws(t, i);
            if (t[i] == ']') { ++i; return true; }
            while (i < t.size()) {
                v.a.emplace_back();
                if (!parseValue(t, i, v.a.back())) return false;
                ws(t, i);
                if (t[i] == ',') { ++i; continue; }
                if (t[i] == ']') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (c == '"') { v.type = String; return parseString(t, i, v.s); }
        if (t.compare(i, 4, "true") == 0) { v.type = Bool; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.type = Bool; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { i += 4; return true; }
        char* end = nullptr;
        v.n = strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return false;
        v.type = Number;
        i = end - t.c_str();
        return true;
    }
};

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", (unsigned)c); o += b; }   // (valid JSON for the MCP clients)
        else o.push_back(c);
    }
    return o;
}

} // namespace app
