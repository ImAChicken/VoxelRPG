#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Wire — bytes for messages between server and client
// ══════════════════════════════════════════════════════════
// Little-endian, no padding. Unsigned numbers can go as varints
// (7 bits a byte), most are small. A Reader that runs off the end
// stops reading and reports !ok(); every message is read through
// one, so a short or garbled packet is dropped, never trusted.
namespace wire {

class Writer {
public:
    std::vector<uint8_t> buf;

    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) u8((uint8_t)(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) u8((uint8_t)(v >> (8 * i))); }
    void var(uint64_t v) { while (v >= 0x80) { u8((uint8_t)(v | 0x80)); v >>= 7; } u8((uint8_t)v); }
    void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void boolean(bool b) { u8(b ? 1 : 0); }
    void vec2(glm::vec2 v) { f32(v.x); f32(v.y); }
    void vec3(glm::vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
    void ivec3(glm::ivec3 v) { u32((uint32_t)v.x); u32((uint32_t)v.y); u32((uint32_t)v.z); }
    void str(const std::string& s) { var(s.size()); buf.insert(buf.end(), s.begin(), s.end()); }
    void bytes(const std::vector<uint8_t>& b) { var(b.size()); buf.insert(buf.end(), b.begin(), b.end()); }
};

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : p(d), end(d + n) {}
    explicit Reader(const std::vector<uint8_t>& v) : p(v.data()), end(v.data() + v.size()) {}

    bool ok() const { return good; }
    bool done() const { return p >= end; }
    size_t left() const { return (size_t)(end - p); }

    uint8_t u8() { if (p >= end) { good = false; return 0; } return *p++; }
    uint16_t u16() { uint16_t a = u8(); return (uint16_t)(a | (u8() << 8)); }
    uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i); return v; }
    uint64_t u64() { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)u8() << (8 * i); return v; }
    uint64_t var() {
        uint64_t v = 0;
        for (int s = 0; s < 64; s += 7) { uint8_t b = u8(); v |= (uint64_t)(b & 0x7f) << s; if (!(b & 0x80)) return v; }
        good = false; return 0;
    }
    float f32() { uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    bool boolean() { return u8() != 0; }
    glm::vec2 vec2() { float x = f32(); return { x, f32() }; }
    glm::vec3 vec3() { float x = f32(), y = f32(); return { x, y, f32() }; }
    glm::ivec3 ivec3() { int x = (int)u32(), y = (int)u32(); return { x, y, (int)u32() }; }
    std::string str(size_t max = 1 << 20) {
        uint64_t n = var();
        if (!good || n > max || n > left()) { good = false; return {}; }
        std::string s((const char*)p, (size_t)n); p += n; return s;
    }
    std::vector<uint8_t> bytes(size_t max = 64 << 20) {
        uint64_t n = var();
        if (!good || n > max || n > left()) { good = false; return {}; }
        std::vector<uint8_t> b(p, p + n); p += n; return b;
    }

private:
    const uint8_t* p;
    const uint8_t* end;
    bool good = true;
};

} // namespace wire
