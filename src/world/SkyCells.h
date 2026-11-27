#pragma once
#include "../chunk/Chunk.h"
#include "../chunk/TerrainGenerator.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// The sky world's coarse voxel copy, without GL (render/SkyFarView.h
// draws it; tests read it): one cell per 8 × 8 × 8 voxels, kept per
// chunk as runs of cells up each column, made from the generator or
// shrunk from a chunk's real voxels. (Claude, 2026-11-25)
namespace skycells {

constexpr int CELL = 8;                          // voxels a cell
constexpr int CPC  = VOXEL_DIM / CELL;           // cells a chunk side (16)
constexpr int CY   = WORLD_HEIGHT / CELL;        // cells up (128)
static_assert(CY < 256, "a run keeps its cells in bytes");

// What the copy needs of each block: its colour from afar, whether
// it fills a cell, whether it glows
struct BlockLook { glm::u8vec3 color{128}; uint8_t kind = 0; bool glow = false; };   // kind: 0 skip, 1 solid, 2 fluid
using LookTable = std::vector<BlockLook>;

// One column's cells: runs from y0 up to (not counting) y1
struct Run { uint8_t y0 = 0, y1 = 0; uint8_t r = 0, g = 0, b = 0, glow = 0; };
struct ChunkCells {
    std::vector<Run> runs;
    std::vector<uint32_t> start;          // CPC * CPC + 1: where each column's runs begin (column = z * CPC + x)
    bool fromVoxels = false;              // from the real chunk (saved), not the generator
};


// The cells of a chunk from the generator (headless; tests)
inline std::shared_ptr<ChunkCells> fromGenerator(const TerrainGenerator& g, const LookTable& L, int cx, int cz) {
    auto c = std::make_shared<ChunkCells>();
    c->start.assign(CPC * CPC + 1, 0);
    for (int z = 0; z < CPC; z++)
    for (int x = 0; x < CPC; x++) {
        c->start[(size_t)(z * CPC + x)] = (uint32_t)c->runs.size();
        int wx = cx * VOXEL_DIM + x * CELL + CELL / 2, wz = cz * VOXEL_DIM + z * CELL + CELL / 2;
        TerrainColumn col = g.column(wx, wz);
        int floor = std::max(0, col.floor), ground = std::min(col.ground, WORLD_HEIGHT);
        if (ground - floor >= 2) {
            int c0 = floor / CELL, c1 = (ground - 1) / CELL + 1;
            auto rgb = [&](uint16_t id) { return id < L.size() ? L[id].color : glm::u8vec3(128); };
            auto push = [&](int y0, int y1, glm::u8vec3 k, bool glow) {
                if (y1 <= y0) return;
                Run r; r.y0 = (uint8_t)y0; r.y1 = (uint8_t)std::min(y1, CY); r.r = k.r; r.g = k.g; r.b = k.b; r.glow = glow;
                c->runs.push_back(r);
            };
            uint16_t underId = col.under ? col.under : col.deep;
            if (c1 - c0 == 1) push(c0, c1, rgb(col.top), false);
            else {
                push(c0, c0 + 1, rgb(underId), false);
                push(c0 + 1, c1 - 1, rgb(col.deep), false);
                push(c1 - 1, c1, rgb(col.top), false);
            }
        }
        if (col.waterLevel > col.ground && col.waterLevel - 1 >= 0) {
            int wy = (col.waterLevel - 1) / CELL;
            glm::u8vec3 w = col.fluid < L.size() ? L[col.fluid].color : glm::u8vec3(60, 90, 160);
            Run r; r.y0 = (uint8_t)wy; r.y1 = (uint8_t)(wy + 1); r.r = w.r; r.g = w.g; r.b = w.b;
            if (c->runs.empty() || c->runs.back().y1 <= r.y0) c->runs.push_back(r);
        }
    }
    c->start[CPC * CPC] = (uint32_t)c->runs.size();
    return c;
}
// The cells of a chunk from its real voxels (headless; tests)
inline std::shared_ptr<ChunkCells> fromVoxels(const std::array<std::shared_ptr<const RleData>, SECTIONS>& secs, const LookTable& L) {
    auto c = std::make_shared<ChunkCells>();
    c->fromVoxels = true;
    // per cell: how many voxels are solid, the colour of the highest, glow
    struct Acc { uint16_t n = 0; int16_t topY = -1; uint16_t id = 0; uint8_t glow = 0; };
    std::vector<Acc> acc((size_t)CPC * CPC * CY);
    const int D = VOXEL_DIM;
    for (int s = 0; s < SECTIONS; s++) {
        const RleData* r = secs[(size_t)s].get();
        if (!r || r->types.empty()) continue;
        if (r->types.size() == 1 && r->types[0] == BLOCK_AIR) continue;
        uint32_t from = 0;
        for (size_t k = 0; k < r->types.size(); k++) {
            uint32_t to = r->ends[k];
            uint16_t id = r->types[k];
            const BlockLook* bl = id < L.size() ? &L[id] : nullptr;
            if (id != BLOCK_AIR && bl && bl->kind != 0) {
                for (uint32_t i = from; i < to; i++) {
                    int y = (int)(i / (D * D)), rem = (int)(i % (D * D)), x = rem / D, z = rem % D;
                    int wy = s * D + y;
                    Acc& a = acc[((size_t)(wy / CELL) * CPC + (z / CELL)) * CPC + (x / CELL)];
                    a.n++;
                    if (wy >= a.topY) { a.topY = (int16_t)wy; a.id = id; }
                    if (bl->glow) a.glow = 1;
                }
            }
            from = to;
        }
    }
    c->start.assign(CPC * CPC + 1, 0);
    for (int z = 0; z < CPC; z++)
    for (int x = 0; x < CPC; x++) {
        c->start[(size_t)(z * CPC + x)] = (uint32_t)c->runs.size();
        for (int y = 0; y < CY; y++) {
            const Acc& a = acc[((size_t)y * CPC + z) * CPC + x];
            // solid enough to show: a quarter of the cell (thin rims stay)
            if (a.n < CELL * CELL * CELL / 8 && !a.glow) continue;
            glm::u8vec3 k = L[a.id].color;
            if (c->runs.size() > c->start[(size_t)(z * CPC + x)]) {
                Run& last = c->runs.back();
                if (last.y1 == y && last.r == k.r && last.g == k.g && last.b == k.b && last.glow == a.glow) { last.y1++; continue; }
            }
            Run r; r.y0 = (uint8_t)y; r.y1 = (uint8_t)(y + 1); r.r = k.r; r.g = k.g; r.b = k.b; r.glow = a.glow;
            c->runs.push_back(r);
        }
    }
    c->start[CPC * CPC] = (uint32_t)c->runs.size();
    return c;
}


} // namespace skycells
