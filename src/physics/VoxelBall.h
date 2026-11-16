#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <functional>

// ══════════════════════════════════════════════════════════
// VoxelBall — a ball pushed out of solid voxels
// ══════════════════════════════════════════════════════════
// Shared by everything simulated as points against the voxel
// world: ragdoll points (physics/Ragdoll.h), falling trees
// (physics/RigidVoxelBody.h) and items lying on the ground
// (world/Woodcutting.h). Headless.

// Is the voxel at (x, y, z) solid?
using SolidFn = std::function<bool(int, int, int)>;

// Push a ball of radius r at p out of every solid voxel it
// overlaps. A centre inside a voxel goes out through the
// nearest open face (straight up if buried). Returns true on
// contact, with the push direction in nOut.
inline bool collideBall(glm::vec3& p, float r, const SolidFn& solid, glm::vec3& nOut) {
    if (!solid) return false;
    glm::vec3 nsum(0.0f);
    bool any = false;
    const int x0 = (int)std::floor(p.x - r), x1 = (int)std::floor(p.x + r);
    const int y0 = (int)std::floor(p.y - r), y1 = (int)std::floor(p.y + r);
    const int z0 = (int)std::floor(p.z - r), z1 = (int)std::floor(p.z + r);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++) {
                if (!solid(x, y, z)) continue;
                glm::vec3 lo((float)x, (float)y, (float)z), hi = lo + 1.0f;
                glm::vec3 q = glm::clamp(p, lo, hi);
                glm::vec3 d = p - q;
                float L = glm::length(d);
                if (L >= r) continue;
                glm::vec3 n;
                if (L > 1e-5f) {
                    n = d / L;
                    p += n * (r - L);
                } else {
                    // centre inside: shortest way out through a free face
                    const glm::ivec3 dirs[6] = { {0,1,0}, {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,-1,0} };
                    float best = 1e9f, push = 0.0f;
                    n = glm::vec3(0, 1, 0);
                    for (const glm::ivec3& D : dirs) {
                        if (solid(x + D.x, y + D.y, z + D.z)) continue;
                        float dist = D.x > 0 ? hi.x - p.x : D.x < 0 ? p.x - lo.x
                                   : D.y > 0 ? hi.y - p.y : D.y < 0 ? p.y - lo.y
                                   : D.z > 0 ? hi.z - p.z : p.z - lo.z;
                        if (dist < best) { best = dist; n = glm::vec3(D); push = dist; }
                    }
                    if (best == 1e9f) push = hi.y - p.y;      // buried: up
                    p += n * (push + r);
                }
                nsum += n;
                any = true;
            }
    if (!any) return false;
    float l = glm::length(nsum);
    nOut = l > 1e-6f ? nsum / l : glm::vec3(0, 1, 0);
    return true;
}
