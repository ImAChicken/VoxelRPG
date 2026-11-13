#pragma once
#include "Chunk.h"
#include "../voxel/Voxel.h"
#include <cmath>

// ── Tree structures at 4-inch voxel scale ────────────────
// Every tree is built in its own structure chunk with the
// trunk centred at x = 64, z = 64 and the base at y = 0.
// That base point is the paste origin.
//
// At 4 inches per voxel:
//   player           17 voxels  =  5'8"
//   oak trunk        50 voxels  = 16'8"
//   pine trunk       70 voxels  = 23'4"
//   ancient oak      65 voxels  = 21'8" to the fork

constexpr int STRUCTURE_ORIGIN_X = VOXEL_DIM / 2;
constexpr int STRUCTURE_ORIGIN_Z = VOXEL_DIM / 2;

// Ball of leaves — never overwrites existing blocks
inline void fillLeafSphere(Chunk& c,
                           int cx, int cy, int cz,
                           int radius) {
    float r = (float)radius + 0.5f;
    for (int dx = -radius; dx <= radius; dx++)
    for (int dy = -radius; dy <= radius; dy++)
    for (int dz = -radius; dz <= radius; dz++) {
        float dist = std::sqrt(
            (float)(dx*dx + dy*dy + dz*dz));
        if (dist > r) continue;
        int x = cx + dx, y = cy + dy, z = cz + dz;
        if (c.typeAt(x, y, z) == BLOCK_AIR)
            c.setBlock(x, y, z, BLOCK_LEAVES);
    }
}

// Flat disc of leaves — one voxel thick
inline void fillLeafDisc(Chunk& c,
                         int cx, int y, int cz,
                         int radius) {
    float r = (float)radius + 0.5f;
    for (int dx = -radius; dx <= radius; dx++)
    for (int dz = -radius; dz <= radius; dz++) {
        float dist = std::sqrt((float)(dx*dx + dz*dz));
        if (dist > r) continue;
        if (c.typeAt(cx + dx, y, cz + dz) == BLOCK_AIR)
            c.setBlock(cx + dx, y, cz + dz, BLOCK_LEAVES);
    }
}

// Vertical log cylinder from yStart up to (not including) yEnd
inline void fillLogCylinder(Chunk& c,
                            int cx, int cz,
                            int yStart, int yEnd,
                            int radius) {
    float r = (float)radius + 0.4f;
    for (int y = yStart; y < yEnd; y++)
    for (int dx = -radius; dx <= radius; dx++)
    for (int dz = -radius; dz <= radius; dz++) {
        float dist = std::sqrt((float)(dx*dx + dz*dz));
        if (dist <= r)
            c.setBlock(cx + dx, y, cz + dz, BLOCK_LOG_Y);
    }
}

// ── Tree 1 — Oak ──────────────────────────────────────────
// Trunk 50 voxels (16'8"), 5 voxels (20") across at the base.
// Wide spreading crown made of a big central ball and four
// side clusters. About 66 voxels tall, 49 across.
inline ChunkPtr generateOakTree() {
    ChunkPtr chunk = makeChunk();
    const int cx = STRUCTURE_ORIGIN_X, cz = STRUCTURE_ORIGIN_Z;

    fillLogCylinder(*chunk, cx, cz,  0, 38, 2);
    fillLogCylinder(*chunk, cx, cz, 38, 50, 1);

    fillLeafSphere(*chunk, cx,      50, cz,      18);
    fillLeafSphere(*chunk, cx - 12, 42, cz,      12);
    fillLeafSphere(*chunk, cx + 12, 42, cz,      12);
    fillLeafSphere(*chunk, cx,      42, cz - 12, 12);
    fillLeafSphere(*chunk, cx,      42, cz + 12, 12);
    fillLeafSphere(*chunk, cx,      58, cz,       8);

    return chunk;
}

// ── Tree 2 — Pine ─────────────────────────────────────────
// Trunk 70 voxels (23'4"). Stacked tiers of branches, each a
// short cone, getting narrower toward the tip.
// About 76 voxels tall, 33 across.
inline ChunkPtr generatePineTree() {
    ChunkPtr chunk = makeChunk();
    const int cx = STRUCTURE_ORIGIN_X, cz = STRUCTURE_ORIGIN_Z;

    fillLogCylinder(*chunk, cx, cz,  0, 30, 2);
    fillLogCylinder(*chunk, cx, cz, 30, 70, 1);

    // Each tier: widest disc at the bottom, tapering over
    // 'height' voxels to a narrow top
    struct Tier { int y; int radius; int height; };
    const Tier tiers[] = {
        { 18, 16, 6 },
        { 27, 14, 6 },
        { 36, 12, 6 },
        { 44, 10, 5 },
        { 51,  8, 5 },
        { 57,  6, 4 },
        { 62,  5, 4 },
        { 66,  4, 3 },
        { 69,  3, 3 },
    };
    for (const Tier& t : tiers) {
        for (int i = 0; i < t.height; i++) {
            int r = t.radius -
                    (t.radius - 2) * i / t.height;
            fillLeafDisc(*chunk, cx, t.y + i, cz, r);
        }
    }
    // Pointed tip
    fillLeafDisc(*chunk, cx, 72, cz, 2);
    fillLeafDisc(*chunk, cx, 73, cz, 1);
    fillLeafDisc(*chunk, cx, 74, cz, 1);
    fillLeafDisc(*chunk, cx, 75, cz, 0);

    return chunk;
}

// ── Tree 3 — Dead tree ────────────────────────────────────
// Leaning trunk 40 voxels (13'4") with bare branches.
// For swamp, volcanic and ruined areas.
inline ChunkPtr generateDeadTree() {
    ChunkPtr chunk = makeChunk();
    const int cx = STRUCTURE_ORIGIN_X, cz = STRUCTURE_ORIGIN_Z;

    // Trunk drifts sideways as it climbs
    for (int y = 0; y < 40; y++) {
        int ox = (y > 20) ? 1 : 0;
        int oz = (y > 30) ? 1 : 0;
        fillLogCylinder(*chunk, cx + ox, cz + oz,
                        y, y + 1, (y < 15) ? 2 : 1);
    }

    // Bare branches reaching out and up
    struct Branch { int y; int dx; int dz; int len; };
    const Branch branches[] = {
        { 22,  10,   0, 12 },
        { 25, -10,   2, 12 },
        { 29,   2,  10, 11 },
        { 33,   7,   7, 10 },
        { 34,  -7,  -7, 10 },
        { 37,   5,  -6,  8 },
    };
    for (const Branch& b : branches) {
        for (int i = 0; i <= b.len; i++) {
            int x = cx + b.dx * i / b.len;
            int z = cz + b.dz * i / b.len;
            int y = b.y + i / 2;
            chunk->setBlock(x, y,     z, BLOCK_LOG_Y);
            chunk->setBlock(x, y + 1, z, BLOCK_LOG_Y);
        }
    }

    return chunk;
}

// ── Tree 4 — Ancient oak ──────────────────────────────────
// Massive trunk, 9 voxels (36") across at the base, forking
// into two limbs at y = 40. Twin crowns merge into one huge
// canopy. About 83 voxels tall, 57 across.
inline ChunkPtr generateAncientOak() {
    ChunkPtr chunk = makeChunk();
    const int cx = STRUCTURE_ORIGIN_X, cz = STRUCTURE_ORIGIN_Z;

    fillLogCylinder(*chunk, cx, cz,  0, 25, 4);
    fillLogCylinder(*chunk, cx, cz, 25, 40, 3);

    // The fork
    fillLogCylinder(*chunk, cx - 4, cz, 40, 65, 2);
    fillLogCylinder(*chunk, cx + 4, cz, 40, 65, 2);

    // Left crown
    fillLeafSphere(*chunk, cx - 4,  65, cz,      20);
    fillLeafSphere(*chunk, cx - 14, 58, cz,      14);
    fillLeafSphere(*chunk, cx - 4,  58, cz - 14, 12);
    fillLeafSphere(*chunk, cx - 4,  58, cz + 14, 12);
    fillLeafSphere(*chunk, cx - 4,  72, cz,      10);

    // Right crown
    fillLeafSphere(*chunk, cx + 4,  65, cz,      20);
    fillLeafSphere(*chunk, cx + 14, 58, cz,      14);
    fillLeafSphere(*chunk, cx + 4,  58, cz - 14, 12);
    fillLeafSphere(*chunk, cx + 4,  58, cz + 14, 12);
    fillLeafSphere(*chunk, cx + 4,  72, cz,      10);

    // Fill the gap between the crowns
    fillLeafSphere(*chunk, cx, 60, cz, 14);

    return chunk;
}
