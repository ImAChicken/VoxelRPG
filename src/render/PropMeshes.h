#pragma once
#include "../character/CharacterModel.h"
#include "../physics/RigidVoxelBody.h"
#include "../voxel/VoxelRegistry.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// PropMeshes — meshes for loose objects (no GL here)
// ══════════════════════════════════════════════════════════
// Built as character-style part vertices, drawn with the
// character shader (CharacterRenderer::drawProps).
//   voxelPieces  a felled tree, from its voxels (in their
//                original world positions; RigidVoxelBody::
//                transform() moves them)
//   cube         a unit cube centred on 0 in one colour (wood
//                on the ground, leaves, chips)
namespace propmesh {

inline void quad(std::vector<PartVertex>& out, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                 glm::ivec3 n, glm::u8vec3 col) {
    auto v = [&](glm::vec3 p) {
        out.push_back({ p.x, p.y, p.z, (int8_t)(n.x * 127), (int8_t)(n.y * 127), (int8_t)(n.z * 127), 0,
                        col.r, col.g, col.b, 0 });
    };
    v(a); v(b); v(c); v(a); v(c); v(d);
}

// The six faces of a box [lo, hi]; skip(f) leaves face f out
template <class Skip>
inline void box(std::vector<PartVertex>& out, glm::vec3 lo, glm::vec3 hi, glm::u8vec3 col, Skip skip) {
    const glm::vec3 L = lo, H = hi;
    if (!skip(0)) quad(out, {H.x, L.y, L.z}, {H.x, H.y, L.z}, {H.x, H.y, H.z}, {H.x, L.y, H.z}, { 1, 0, 0}, col);
    if (!skip(1)) quad(out, {L.x, L.y, H.z}, {L.x, H.y, H.z}, {L.x, H.y, L.z}, {L.x, L.y, L.z}, {-1, 0, 0}, col);
    if (!skip(2)) quad(out, {L.x, H.y, L.z}, {L.x, H.y, H.z}, {H.x, H.y, H.z}, {H.x, H.y, L.z}, { 0, 1, 0}, col);
    if (!skip(3)) quad(out, {L.x, L.y, L.z}, {H.x, L.y, L.z}, {H.x, L.y, H.z}, {L.x, L.y, H.z}, { 0,-1, 0}, col);
    if (!skip(4)) quad(out, {H.x, L.y, H.z}, {H.x, H.y, H.z}, {L.x, H.y, H.z}, {L.x, L.y, H.z}, { 0, 0, 1}, col);
    if (!skip(5)) quad(out, {L.x, L.y, L.z}, {L.x, H.y, L.z}, {H.x, H.y, L.z}, {H.x, L.y, L.z}, { 0, 0,-1}, col);
}

// A felled tree: every voxel face with no voxel beside it
inline std::vector<PartVertex> voxelPieces(const std::vector<VoxelPiece>& pieces) {
    auto key = [](glm::ivec3 p) {
        return ((int64_t)(p.x & 0x1FFFFF) << 42) | ((int64_t)(p.y & 0x1FFFFF) << 21) | (int64_t)(p.z & 0x1FFFFF);
    };
    std::unordered_set<int64_t> has;
    for (const VoxelPiece& v : pieces) has.insert(key(v.p));
    static const glm::ivec3 N[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    std::vector<PartVertex> out;
    for (const VoxelPiece& v : pieces) {
        const VoxelTypeDef& b = gRegistry.get(v.id);
        // a little variation per voxel, like bark
        uint32_t h = (uint32_t)(v.p.x * 73856093) ^ (uint32_t)(v.p.y * 19349663) ^ (uint32_t)(v.p.z * 83492791);
        float k = 0.88f + 0.12f * (float)(h % 100) / 100.0f;
        glm::u8vec3 col(glm::clamp(glm::vec3(b.color[0], b.color[1], b.color[2]) * k * 255.0f, glm::vec3(0), glm::vec3(255)));
        glm::vec3 lo(v.p);
        box(out, lo, lo + 1.0f, col, [&](int f) { return has.count(key(v.p + N[f])) > 0; });
    }
    return out;
}

inline std::vector<PartVertex> cube(glm::vec3 colour01) {
    std::vector<PartVertex> out;
    glm::u8vec3 col(glm::clamp(colour01 * 255.0f, glm::vec3(0), glm::vec3(255)));
    box(out, glm::vec3(-0.5f), glm::vec3(0.5f), col, [](int) { return false; });
    return out;
}

// One key per colour, so cubes of a colour share a mesh
inline uint32_t colourKey(glm::vec3 c) {
    glm::u8vec3 q(glm::clamp(c * 255.0f, glm::vec3(0), glm::vec3(255)));
    return (uint32_t)q.r << 16 | (uint32_t)q.g << 8 | q.b;
}

} // namespace propmesh
