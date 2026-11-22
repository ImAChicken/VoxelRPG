#pragma once
// ══════════════════════════════════════════════════════════
// MonsterModel — a rigged, animated creature from a file
// ══════════════════════════════════════════════════════════
// The bodies that aren't people: a mesh skinned to its own skeleton
// and its own animations, made by tools/import_monster.py from a glTF
// (data/models/monsters/<name>.vmon; the Quaternius monster kit, CC0).
// Drawn by render/CharacterRenderer.h with the characters' toon light
// bands and edge lines, so it sits with them. Headless: the game's
// renderer, a test and the server can all read one.
// (Claude, 2026-11-22: the mushroom monsters)
//
//   const MonsterModel* m = gMonsters.find("mushnub");
//   MonsterPose p;  m->sample(m->clip("Walk"), t, true, p);
//   m->skin(p, mats);          // per bone: rest → posed (model space)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct MonsterVertex {              // (the GPU's layout too)
    float   x, y, z;
    int8_t  nx, ny, nz, pad0;
    uint8_t r, g, b, pad1;
    uint8_t bone[4];
    uint8_t weight[4];               // / 255, summing to 255
};
static_assert(sizeof(MonsterVertex) == 28, "MonsterVertex is packed as the file has it");

struct MonsterBoneTRS { glm::vec3 t{0.0f}; glm::quat r{1, 0, 0, 0}; glm::vec3 s{1.0f}; };
using MonsterPose = std::vector<MonsterBoneTRS>;

struct MonsterModel {
    struct Bone { std::string name; int parent = -1; glm::mat4 base{1.0f}; MonsterBoneTRS rest; glm::mat4 invBind{1.0f}; };
    struct Clip { std::string name; float seconds = 0.0f, fps = 30.0f; int frames = 0; std::vector<float> data; };
    std::string name;
    std::vector<Bone> bones;
    std::vector<MonsterVertex> verts;        // triangles, 3 a face
    glm::vec3 lo{0.0f}, hi{0.0f};            // the mesh at rest
    std::vector<Clip> clips;

    float height() const { return hi.y - std::min(lo.y, 0.0f); }
    int clip(const std::string& n) const {
        for (size_t i = 0; i < clips.size(); i++) if (clips[i].name == n) return (int)i;
        return -1;
    }
    float seconds(int c) const { return c >= 0 && c < (int)clips.size() ? clips[c].seconds : 0.0f; }

    void restPose(MonsterPose& p) const {
        p.resize(bones.size());
        for (size_t b = 0; b < bones.size(); b++) p[b] = bones[b].rest;
    }
    // The clip's pose at t (s): wrapped round if it loops, else held at
    // its end. An unknown clip gives the rest pose.
    void sample(int c, float t, bool loop, MonsterPose& p) const {
        if (c < 0 || c >= (int)clips.size() || clips[c].frames < 1) { restPose(p); return; }
        const Clip& C = clips[c];
        p.resize(bones.size());
        float d = std::max(C.seconds, 1e-4f);
        if (loop) { t = std::fmod(t, d); if (t < 0) t += d; }
        else t = std::clamp(t, 0.0f, d);
        float f = t * C.fps;
        int a = std::clamp((int)f, 0, C.frames - 1), b = std::min(a + 1, C.frames - 1);
        float k = std::clamp(f - (float)a, 0.0f, 1.0f);
        const size_t per = bones.size() * 10;
        const float* A = C.data.data() + (size_t)a * per;
        const float* B = C.data.data() + (size_t)b * per;
        for (size_t j = 0; j < bones.size(); j++, A += 10, B += 10) {
            glm::quat qa(A[6], A[3], A[4], A[5]), qb(B[6], B[3], B[4], B[5]);
            p[j].t = glm::mix(glm::vec3(A[0], A[1], A[2]), glm::vec3(B[0], B[1], B[2]), k);
            p[j].r = glm::slerp(qa, qb, k);
            p[j].s = glm::mix(glm::vec3(A[7], A[8], A[9]), glm::vec3(B[7], B[8], B[9]), k);
        }
    }
    // Blend two poses: w of 'b' over 'a', into a
    static void blend(MonsterPose& a, const MonsterPose& b, float w) {
        if (w <= 0.0f || b.size() != a.size()) return;
        for (size_t j = 0; j < a.size(); j++) {
            a[j].t = glm::mix(a[j].t, b[j].t, w);
            a[j].r = glm::slerp(a[j].r, b[j].r, w);
            a[j].s = glm::mix(a[j].s, b[j].s, w);
        }
    }
    // Each bone in model space, posed
    void globals(const MonsterPose& p, std::vector<glm::mat4>& g) const {
        g.resize(bones.size());
        for (size_t j = 0; j < bones.size(); j++) {
            const MonsterBoneTRS& x = j < p.size() ? p[j] : bones[j].rest;
            glm::mat4 L = glm::translate(glm::mat4(1.0f), x.t) * glm::mat4_cast(x.r) * glm::scale(glm::mat4(1.0f), x.s);
            g[j] = (bones[j].parent >= 0 ? g[bones[j].parent] : bones[j].base) * L;
        }
    }
    // The skinning matrices: each bone's rest → posed (model space)
    void skin(const MonsterPose& p, std::vector<glm::mat4>& m) const {
        globals(p, m);
        for (size_t j = 0; j < bones.size(); j++) m[j] = m[j] * bones[j].invBind;
    }

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        auto rd = [&](void* p, size_t n) { f.read((char*)p, (std::streamsize)n); return (bool)f; };
        auto u32 = [&]() { uint32_t v = 0; rd(&v, 4); return v; };
        auto str = [&]() { uint8_t n = 0; rd(&n, 1); std::string s(n, '\0'); if (n) rd(&s[0], n); return s; };
        char magic[4];
        if (!rd(magic, 4) || std::memcmp(magic, "VMON", 4) != 0 || u32() != 1) return false;
        uint32_t nb = u32();
        if (nb == 0 || nb > 255) return false;
        bones.resize(nb);
        for (Bone& b : bones) {
            b.name = str();
            int32_t par = -1; rd(&par, 4); b.parent = par;
            float m[16], r[10];
            rd(m, sizeof m); b.base = glm::make_mat4(m);
            rd(r, sizeof r);
            b.rest.t = glm::vec3(r[0], r[1], r[2]); b.rest.r = glm::quat(r[6], r[3], r[4], r[5]); b.rest.s = glm::vec3(r[7], r[8], r[9]);
            rd(m, sizeof m); b.invBind = glm::make_mat4(m);
        }
        for (size_t j = 0; j < bones.size(); j++)
            if (bones[j].parent >= (int)j) return false;              // parents first
        uint32_t nv = u32();
        if (nv > 4000000) return false;
        verts.resize(nv);
        if (nv && !rd(verts.data(), nv * sizeof(MonsterVertex))) return false;
        for (MonsterVertex& v : verts) for (uint8_t& b : v.bone) if (b >= nb) b = 0;
        float bb[6]; rd(bb, sizeof bb);
        lo = glm::vec3(bb[0], bb[1], bb[2]); hi = glm::vec3(bb[3], bb[4], bb[5]);
        uint32_t nc = u32();
        if (nc > 1000) return false;
        clips.resize(nc);
        for (Clip& c : clips) {
            c.name = str();
            rd(&c.seconds, 4); rd(&c.fps, 4);
            c.frames = (int)u32();
            if (c.frames < 0 || c.frames > 100000) return false;
            c.data.resize((size_t)c.frames * nb * 10);
            if (!c.data.empty() && !rd(c.data.data(), c.data.size() * sizeof(float))) return false;
        }
        return (bool)f;
    }
};

// Every monster in a folder, by file name (lower case, no extension)
class MonsterLibrary {
public:
    int load(const std::string& dir) {
        models.clear();
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return 0;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".vmon") continue;
            auto m = std::make_unique<MonsterModel>();
            m->name = e.path().stem().string();
            if (m->load(e.path().string())) models[m->name] = std::move(m);
            else std::cerr << "Monster model: can't read " << e.path().string() << "\n";
        }
        return (int)models.size();
    }
    const MonsterModel* find(const std::string& n) const {
        auto it = models.find(n);
        return it == models.end() ? nullptr : it->second.get();
    }
private:
    std::map<std::string, std::unique_ptr<MonsterModel>> models;
};
inline MonsterLibrary gMonsters;
