#pragma once
#include "Materials.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// ══════════════════════════════════════════════════════════
// Acoustics — what the voxels do to a sound on its way to you
// ══════════════════════════════════════════════════════════
// First pass: the straight line. A ray is stepped voxel by voxel
// from the listener to the sound (the same grid walk the light bake
// and the aim use), and every voxel it crosses that isn't air adds
// its material's muffle (audio/Materials.h): stone stops a lot, wood
// less, leaves and grass almost nothing, water a lot. Muffling takes
// the highs first, so a voice through a wall comes out low and dull,
// and through a hill not at all.
//
// Next passes (ROADMAP: Talk and audio): around corners (a flood of
// the open air from the sound, so a voice in the next room comes from
// the doorway), and the room's echo from what it's made of.
namespace sound {

struct Through {
    float muffle = 0.0f;       // summed over the voxels crossed
    float gain = 1.0f;         // what's left of the loudness
    float cutoffHz = 20000.0f; // what's left of the highs
    int   solid = 0;           // voxels crossed that weren't air
};

// The muffle sum → loudness and tone. One stone voxel (4 inches)
// takes the edge off; a wall of three is clearly "next door"; a
// hillside is silence.
inline Through shape(float muffle, int solid) {
    Through t;
    t.muffle = muffle;
    t.solid = solid;
    t.gain = std::exp(-0.25f * muffle);
    t.cutoffHz = std::max(250.0f, 16000.0f * std::exp(-0.6f * muffle));
    return t;
}

// blockAt(x, y, z) → the block id there. 'skip' voxels at each end
// don't count (an ear in the camera's voxel, a mouth in a head).
template <class BlockAt>
Through throughWalls(glm::vec3 from, glm::vec3 to, BlockAt&& blockAt, const MaterialTable& table = gMaterials,
                     int maxSteps = 600, float skip = 1.2f) {
    glm::vec3 d = to - from;
    float len = glm::length(d);
    if (len < 1e-3f) return shape(0.0f, 0);
    glm::vec3 dir = d / len;
    glm::ivec3 cell = glm::ivec3(glm::floor(from));
    const glm::ivec3 end = glm::ivec3(glm::floor(to));
    glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
    glm::vec3 tMax, tDelta;
    for (int a = 0; a < 3; a++) {
        if (std::fabs(dir[a]) < 1e-6f) { tMax[a] = 1e30f; tDelta[a] = 1e30f; continue; }
        float next = step[a] > 0 ? std::floor(from[a]) + 1.0f - from[a] : from[a] - std::floor(from[a]);
        tMax[a] = next / std::fabs(dir[a]);
        tDelta[a] = 1.0f / std::fabs(dir[a]);
    }
    float muffle = 0.0f, t = 0.0f;
    int solid = 0;
    for (int i = 0; i < maxSteps && cell != end; i++) {
        int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        t = tMax[a];
        if (t > len) break;
        cell[a] += step[a];
        tMax[a] += tDelta[a];
        if (t < skip || len - t < skip) continue;
        uint16_t id = blockAt(cell.x, cell.y, cell.z);
        if (id == 0) continue;
        float m = table.forBlock(id).muffle;
        if (m > 0.0f) { muffle += m; solid++; }
    }
    return shape(muffle, solid);
}

} // namespace sound
