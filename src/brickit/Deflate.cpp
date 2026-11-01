#include "Deflate.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace brickit {

namespace {

struct BitWriter {
    std::string out;
    uint32_t acc = 0;
    int n = 0;
    void put(uint32_t bits, int count) {   // LSB first
        acc |= bits << n;
        n += count;
        while (n >= 8) { out.push_back((char)(acc & 255)); acc >>= 8; n -= 8; }
    }
    void putRev(uint32_t code, int len) {   // Huffman codes go MSB first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
        put(r, len);
    }
    void flush() { if (n > 0) { out.push_back((char)(acc & 255)); acc = 0; n = 0; } }
};

void putLiteral(BitWriter& w, int sym) {   // fixed Huffman table
    if (sym < 144) w.putRev(0x30 + sym, 8);
    else if (sym < 256) w.putRev(0x190 + sym - 144, 9);
    else if (sym < 280) w.putRev(sym - 256, 7);
    else w.putRev(0xC0 + sym - 280, 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putMatch(BitWriter& w, int len, int dist) {
    int li = 28;
    while (kLenBase[li] > len) --li;
    putLiteral(w, 257 + li);
    if (kLenExtra[li]) w.put(len - kLenBase[li], kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > dist) --di;
    w.putRev(di, 5);
    if (kDistExtra[di]) w.put(dist - kDistBase[di], kDistExtra[di]);
}

} // namespace

std::string zlibCompress(const std::string& data) {
    BitWriter w;
    w.out.push_back((char)0x78);
    w.out.push_back((char)0x01);
    w.put(1, 1);   // BFINAL
    w.put(1, 2);   // BTYPE = fixed Huffman
    const int kWindow = 32768, kHash = 1 << 15, kMaxChain = 48;
    const size_t n = data.size();
    const unsigned char* d = (const unsigned char*)data.data();
    std::vector<int> head(kHash, -1), prev(n > 0 ? n : 1, -1);
    auto hashAt = [&](size_t i) { return (int)(((d[i] << 10) ^ (d[i + 1] << 5) ^ d[i + 2]) & (kHash - 1)); };
    auto insert = [&](size_t i) { if (i + 2 < n) { int h = hashAt(i); prev[i] = head[h]; head[h] = (int)i; } };
    size_t i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hashAt(i)], chain = 0;
            const int maxLen = (int)std::min<size_t>(258, n - i);
            while (cand >= 0 && (int)i - cand <= kWindow && chain++ < kMaxChain) {
                if (d[cand + bestLen] == d[i + bestLen]) {
                    int l = 0;
                    while (l < maxLen && d[cand + l] == d[i + l]) ++l;
                    if (l > bestLen) { bestLen = l; bestDist = (int)i - cand; if (l == maxLen) break; }
                }
                cand = prev[cand];
            }
        }
        if (bestLen >= 3) {
            putMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + k);
            i += bestLen;
        } else {
            putLiteral(w, d[i]);
            insert(i);
            ++i;
        }
    }
    putLiteral(w, 256);
    w.flush();
    uint32_t a = 1, b = 0;   // Adler-32, big endian
    for (size_t k = 0; k < n; ++k) { a = (a + d[k]) % 65521; b = (b + a) % 65521; }
    const uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) w.out.push_back((char)((ad >> s) & 255));
    return w.out;
}

std::string base64(const std::string& data) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((data.size() + 2) / 3 * 4);
    const unsigned char* d = (const unsigned char*)data.data();
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == data.size()) { uint32_t v = d[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == data.size()) { uint32_t v = (d[i] << 16) | (d[i + 1] << 8); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}

} // namespace brickit
