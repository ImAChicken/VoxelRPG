#pragma once
#include "CharacterModel.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// HeldItemModel — things drawn in the right hand, from files
// ══════════════════════════════════════════════════════════
// Each model is a voxel file in data/models/held/<id>.vxm; an
// item shows one with held_model = "<id>" (items.toml). A new
// weapon or tool needs a file, not code.
//
// A model lives in its own frame, in inches:
//   u (along)   out of the fist toward the business end
//   v (edge)    the way the cutting edge faces
//   w (across)  the flat side
// 'along', 'edge' and 'across' say how that frame sits in the
// right wrist's frame; the fist holds the model at u = v = w = 0
// (heldGrip). It is meshed like the hair (CharacterModel::
// meshVoxelShape: voxels or shards per the hair mesh setting)
// and drawn with the wrist bone's matrix.
//
// Named points ('point <name> u v w') mark places on it. Points
// named edge* are the cutting edge, which a swing traces through
// the world (game/AxeSwing.h).
//
// What it hits bodies with is the voxels themselves: when the
// file loads they are merged into a few boxes (hitBoxes), and a
// hit is one of those boxes, grown by hit_margin inches on every
// side, touching a limb (game/HitShapes.h, CombatHits.h).
//
// File format (one statement a line, '#' comments):
//   vxm 1
//   voxel <size>                 inches a voxel
//   origin <u v w>               corner of voxel (0, 0, 0)
//   along / edge / across <x y z>   the frame, in the wrist's
//   color <index> <r g b>        palette, 1..255 (0 = empty)
//   point <name> <u v w>
//   hit_margin <inches>          how much bigger than its voxels it hits (default 0.75)
//   v <i j k> <color>            a voxel at origin + (i+.5, j+.5, k+.5) · size

// Where the fist holds things, in the wrist frame
inline glm::vec3 heldGrip(const CharacterModel& m) {
    float s = m.heightIn / 67.4f;
    return glm::vec3(0.0f, -2.6f * s, 0.6f * s);
}

struct HeldModelDef {
    std::string id;
    float     voxel = 0.5f;
    glm::vec3 origin{0.0f};
    glm::vec3 along{0, 0, 1}, edge{0, -1, 0}, across{1, 0, 0};
    std::vector<glm::vec3> palette = std::vector<glm::vec3>(1, glm::vec3(0));
    struct Vox { glm::ivec3 p; uint8_t c; };
    std::vector<Vox> voxels;
    std::vector<std::pair<std::string, glm::vec3>> points;
    // The voxels as boxes (model frame, inches: lo, hi), and how
    // much they are grown by for hits
    std::vector<std::pair<glm::vec3, glm::vec3>> hitBoxes;
    float hitMargin = 0.75f;
    // 'thrust <x y z>': where it points (wrist frame) at the height
    // of a thrust (movesets.toml grip = "thrust"), e.g. along the
    // forearm for a spear; carried, it points 'along'. 0: no turn
    glm::vec3 thrust{0.0f};

    // The item's turn about the fist, w = 0 (as carried) .. 1
    // (pointing 'thrust'). Drawing and hits both apply it after
    // the wrist: wrist × turn.
    glm::mat4 turn(const CharacterModel& m, float w) const {
        if (w <= 1e-3f || glm::length(thrust) < 1e-4f) return glm::mat4(1.0f);
        glm::vec3 a = glm::normalize(along), b = glm::normalize(thrust);
        glm::vec3 ax = glm::cross(a, b);
        float s = glm::length(ax), c = glm::dot(a, b);
        if (s < 1e-5f) return glm::mat4(1.0f);
        float ang = std::atan2(s, c) * std::min(w, 1.0f);
        glm::vec3 g = heldGrip(m);
        return glm::translate(glm::mat4(1.0f), g) * glm::rotate(glm::mat4(1.0f), ang, ax / s) * glm::translate(glm::mat4(1.0f), -g);
    }

    // Merge the voxels into boxes: from each voxel not yet used,
    // grow a box along u as far as the voxels go, then along v
    // while every row is full, then along w while every layer is.
    void buildHitBoxes() {
        hitBoxes.clear();
        if (voxels.empty()) return;
        glm::ivec3 lo(INT32_MAX), hi(INT32_MIN);
        for (auto& v : voxels) { lo = glm::min(lo, v.p); hi = glm::max(hi, v.p); }
        glm::ivec3 n = hi - lo + 1;
        std::vector<uint8_t> grid((size_t)n.x * n.y * n.z, 0);   // 1 filled, 2 used
        auto at = [&](int i, int j, int k) -> uint8_t& { return grid[((size_t)k * n.y + j) * n.x + i]; };
        for (auto& v : voxels) { glm::ivec3 q = v.p - lo; at(q.x, q.y, q.z) = 1; }
        for (int k = 0; k < n.z; k++)
            for (int j = 0; j < n.y; j++)
                for (int i = 0; i < n.x; i++) {
                    if (at(i, j, k) != 1) continue;
                    int i1 = i;
                    while (i1 + 1 < n.x && at(i1 + 1, j, k) == 1) i1++;
                    auto rowFull = [&](int jj, int kk) {
                        for (int ii = i; ii <= i1; ii++) if (at(ii, jj, kk) != 1) return false;
                        return true;
                    };
                    int j1 = j;
                    while (j1 + 1 < n.y && rowFull(j1 + 1, k)) j1++;
                    int k1 = k;
                    auto layerFull = [&](int kk) {
                        for (int jj = j; jj <= j1; jj++) if (!rowFull(jj, kk)) return false;
                        return true;
                    };
                    while (k1 + 1 < n.z && layerFull(k1 + 1)) k1++;
                    for (int kk = k; kk <= k1; kk++)
                        for (int jj = j; jj <= j1; jj++)
                            for (int ii = i; ii <= i1; ii++) at(ii, jj, kk) = 2;
                    glm::vec3 a = origin + glm::vec3(lo + glm::ivec3(i, j, k)) * voxel;
                    glm::vec3 b = origin + glm::vec3(lo + glm::ivec3(i1 + 1, j1 + 1, k1 + 1)) * voxel;
                    hitBoxes.push_back({ a, b });
                }
    }

    // Model frame (u, v, w) → wrist frame
    glm::vec3 toWrist(const CharacterModel& m, glm::vec3 uvw) const {
        return heldGrip(m) + along * uvw.x + edge * uvw.y + across * uvw.z;
    }
    bool hasPoint(const std::string& n) const {
        for (auto& p : points) if (p.first == n) return true;
        return false;
    }
    glm::vec3 point(const CharacterModel& m, const std::string& n) const {
        for (auto& p : points) if (p.first == n) return toWrist(m, p.second);
        return heldGrip(m);
    }
    // The cutting edge: every point named edge*, in name order
    std::vector<glm::vec3> edgePoints(const CharacterModel& m) const {
        std::vector<std::pair<std::string, glm::vec3>> e;
        for (auto& p : points) if (p.first.rfind("edge", 0) == 0) e.push_back(p);
        std::sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<glm::vec3> out;
        for (auto& p : e) out.push_back(toWrist(m, p.second));
        return out;
    }
    float reach() const {   // farthest voxel from the fist along u (inches)
        float r = 0.0f;
        for (auto& v : voxels) r = std::max(r, origin.x + (v.p.x + 1) * voxel);
        return r;
    }

    // Meshed in its own frame (u along, v edge, w across, inches):
    // for item icons (render/IconRaster.h)
    std::vector<PartVertex> meshLocal(HairMesh mode) const {
        std::vector<PartVertex> out;
        if (voxels.empty()) return out;
        glm::ivec3 hi(0);
        std::unordered_map<int64_t, uint8_t> at;
        auto key = [](glm::ivec3 p) { return ((int64_t)p.x * 4096 + p.y) * 4096 + p.z; };
        for (auto& v : voxels) { at[key(v.p)] = v.c; hi = glm::max(hi, v.p); }
        auto role = [&](glm::vec3 p) -> int {
            glm::ivec3 i = glm::ivec3(glm::floor((p - origin) / voxel));
            auto it = at.find(key(i));
            return it == at.end() ? 0 : it->second;
        };
        CharacterModel::meshVoxelShape(origin, origin + glm::vec3(hi + 1) * voxel, voxel, role, palette.data(), mode, out);
        return out;
    }

    std::vector<PartVertex> mesh(const CharacterModel& m, HairMesh mode) const {
        std::vector<PartVertex> out;
        if (voxels.empty()) return out;
        glm::ivec3 hi(0);
        std::unordered_map<int64_t, uint8_t> at;
        auto key = [](glm::ivec3 p) { return ((int64_t)p.x * 4096 + p.y) * 4096 + p.z; };
        for (auto& v : voxels) { at[key(v.p)] = v.c; hi = glm::max(hi, v.p); }
        auto role = [&](glm::vec3 p) -> int {
            glm::ivec3 i = glm::ivec3(glm::floor((p - origin) / voxel));
            auto it = at.find(key(i));
            return it == at.end() ? 0 : it->second;
        };
        CharacterModel::meshVoxelShape(origin, origin + glm::vec3(hi + 1) * voxel, voxel, role, palette.data(), mode, out);
        const glm::mat3 B(along, edge, across);
        for (PartVertex& pv : out) {
            glm::vec3 p = toWrist(m, { pv.x, pv.y, pv.z });
            glm::vec3 n = B * glm::vec3(pv.nx, pv.ny, pv.nz);
            pv.x = p.x; pv.y = p.y; pv.z = p.z;
            pv.nx = (int8_t)std::lround(std::clamp(n.x, -127.0f, 127.0f));
            pv.ny = (int8_t)std::lround(std::clamp(n.y, -127.0f, 127.0f));
            pv.nz = (int8_t)std::lround(std::clamp(n.z, -127.0f, 127.0f));
            pv.a = 3;                     // its own part, so edge lines outline it
        }
        return out;
    }

    // ── Reading a file ────────────────────────────────────
    static std::unique_ptr<HeldModelDef> load(const std::string& path, std::string id) {
        std::ifstream f(path);
        if (!f) return nullptr;
        auto d = std::make_unique<HeldModelDef>();
        d->id = std::move(id);
        std::string line;
        int n = 0;
        while (std::getline(f, line)) {
            n++;
            if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
            std::istringstream s(line);
            std::string k;
            if (!(s >> k)) continue;
            auto v3 = [&](glm::vec3& o) { s >> o.x >> o.y >> o.z; };
            if (k == "vxm") { int ver; s >> ver; }
            else if (k == "voxel") s >> d->voxel;
            else if (k == "hit_margin") s >> d->hitMargin;
            else if (k == "thrust") v3(d->thrust);
            else if (k == "origin") v3(d->origin);
            else if (k == "along") v3(d->along);
            else if (k == "edge") v3(d->edge);
            else if (k == "across") v3(d->across);
            else if (k == "color") {
                int i; glm::vec3 c; s >> i; v3(c);
                if (i >= 1 && i < 256) { if ((int)d->palette.size() <= i) d->palette.resize(i + 1, glm::vec3(200)); d->palette[i] = c; }
            } else if (k == "point") {
                std::string name; glm::vec3 p; s >> name; v3(p);
                d->points.push_back({ name, p });
            } else if (k == "v") {
                glm::ivec3 p; int c; s >> p.x >> p.y >> p.z >> c;
                if (c > 0 && c < 256) d->voxels.push_back({ p, (uint8_t)c });
            } else std::cerr << path << ":" << n << ": unknown '" << k << "'\n";
        }
        d->along = glm::normalize(d->along);
        d->edge = glm::normalize(d->edge);
        d->across = glm::normalize(d->across);
        for (auto& v : d->voxels) if (v.c >= d->palette.size()) d->palette.resize(v.c + 1, glm::vec3(200));
        d->buildHitBoxes();
        return d;
    }
};

// Every held model, by id (the file name)
class HeldModelRegistry {
public:
    void loadDir(const std::string& dir) {
        std::unordered_map<std::string, std::unique_ptr<HeldModelDef>> fresh;
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".vxm") continue;
            std::string id = e.path().stem().string();
            if (auto d = HeldModelDef::load(e.path().string(), id)) fresh[id] = std::move(d);
        }
        for (auto& [k, v] : models) retired.push_back(std::move(v));
        models = std::move(fresh);
        std::cout << "Held models: " << models.size() << " from " << dir << "\n";
    }
    const HeldModelDef* find(const std::string& id) const {
        auto it = models.find(id);
        return it == models.end() ? nullptr : it->second.get();
    }
    uint32_t generation() const { return gen; }
    void bump() { gen++; }
private:
    std::unordered_map<std::string, std::unique_ptr<HeldModelDef>> models;
    std::vector<std::unique_ptr<HeldModelDef>> retired;
    uint32_t gen = 0;
};
inline HeldModelRegistry gHeldModels;
