// vp3d - file paths are std::wstring in the engine (Windows). nativePath() gives what std::fstream accepts on the
// current platform: the wide path itself with MSVC, its UTF-8 encoding elsewhere (iOS, Linux test builds).
#pragma once
#include <string>

namespace vp3d {

#ifdef _WIN32
inline const std::wstring& nativePath(const std::wstring& p) { return p; }
#else
inline std::string nativePath(const std::wstring& p) {
    std::string s;
    for (wchar_t wc : p) {
        uint32_t c = (uint32_t)wc;
        if (c < 0x80) {
            s.push_back((char)c);
        } else if (c < 0x800) {
            s.push_back((char)(0xC0 | (c >> 6)));
            s.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            s.push_back((char)(0xE0 | (c >> 12)));
            s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            s.push_back((char)(0xF0 | (c >> 18)));
            s.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return s;
}
#endif

} // namespace vp3d
