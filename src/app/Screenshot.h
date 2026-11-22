#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Screenshot — F2 saves the frame as a PNG
// ══════════════════════════════════════════════════════════
// main.cpp reads the finished frame back (glReadPixels, after the HUD)
// and hands the pixels here. Saved to screenshots/VoxelRPG-<date>-
// <time>.png in the folder the game runs from. A plain PNG writer with
// no library: deflate "stored" blocks (no compression), so a 1080p shot
// is about 6 MB. Good enough for bug reports; swap in real compression
// if they pile up.
namespace screenshot {

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        made = true;
    }
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

// rgb: w * h * 3 bytes, top row first
inline bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    if (w <= 0 || h <= 0 || rgb.size() < (size_t)w * h * 3) return false;
    // the image data: a filter byte (0) before each row
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    // zlib: header, stored blocks of up to 65535 bytes, adler32
    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t at = 0; at < raw.size() || raw.empty();) {
        size_t n = std::min<size_t>(65535, raw.size() - at);
        bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 0xFF)); z.push_back((uint8_t)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
        if (last) break;
    }
    uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((uint8_t)(adler >> s));

    std::error_code ec;
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { uint8_t q[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; f.write((char*)q, 4); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        u32((uint32_t)data.size());
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        f.write((const char*)td.data(), (std::streamsize)td.size());
        u32(crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    f.write((const char*)sig, 8);
    std::vector<uint8_t> ihdr = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                                  (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                                  8, 2, 0, 0, 0 };     // 8 bits, RGB, deflate, no filter method, no interlace
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return (bool)f;
}

// A new file name in screenshots/ (made if missing)
inline std::string nextPath() {
    std::error_code ec;
    std::filesystem::create_directories("screenshots", ec);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[48];
    std::strftime(b, sizeof(b), "screenshots/VoxelRPG-%Y%m%d-%H%M%S", &tm);
    std::string base = b, p = base + ".png";
    for (int i = 2; std::filesystem::exists(p, ec); i++) p = base + "-" + std::to_string(i) + ".png";
    return p;
}

} // namespace screenshot
