#pragma once
#include <glm/glm.hpp>
#include <algorithm>

// ══════════════════════════════════════════════════════════
// Cube targeting — pickaxes and the dev Cube Tool
// ══════════════════════════════════════════════════════════
// Voxels are only 4 inches, so digging one at a time is
// slow and hard to aim. Cube tools work on an N × N × N
// cube instead:
//
//   on the grid   the world is split into cells N voxels
//                 wide; the cube is the cell holding the
//                 voxel you aim at (or, when placing, the one
//                 in front of it). Neat tunnels and walls.
//   Shift         off the grid: the cube is centred on the
//                 aimed voxel across the face, and reaches
//                 N voxels into it (breaking) or out of it
//                 (placing).
struct CubeBox {
    glm::ivec3 lo{0}, hi{0};      // inclusive corners
    int size() const { return hi.x - lo.x + 1; }
};

inline int cubeFloorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

//   voxel    the voxel under the crosshair
//   normal   the face it was hit on (one axis ±1)
//   n        cube width in voxels
//   snap     true: on the grid   false: off the grid (Shift)
//   placing  true: the cube goes on the face, not into it
inline CubeBox cubeFor(glm::ivec3 voxel, glm::ivec3 normal, int n, bool snap, bool placing) {
    n = std::max(1, n);
    glm::ivec3 p = placing ? voxel + normal : voxel;
    CubeBox c;
    for (int a = 0; a < 3; a++) {
        if (snap) {
            c.lo[a] = cubeFloorDiv(p[a], n) * n;
        } else if (normal[a] == 0) {
            c.lo[a] = p[a] - n / 2;                        // centred across the face
        } else {
            // breaking goes into the surface (away from the
            // face), placing grows out of it (along the face)
            bool grows = placing ? normal[a] > 0 : normal[a] < 0;
            c.lo[a] = grows ? p[a] : p[a] - (n - 1);
        }
        c.hi[a] = c.lo[a] + n - 1;
    }
    return c;
}
