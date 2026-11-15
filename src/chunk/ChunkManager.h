#pragma once
#include "Chunk.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <glm/glm.hpp>

// ── World size ────────────────────────────────────────────
// 32 × 32 chunks of 128 voxels = 4096 × 4096 voxels
// = 1365 ft per side at 4 inches per voxel
// World size in chunks. Set when a world is opened (its
// world type decides: Classic 32, Island 72 / 96 / 128) and
// before the ChunkManager for it is created. One world is
// open at a time.
inline int WORLD_SIZE_X = 32;
inline int WORLD_SIZE_Z = 32;

// ── View distance (in chunks, measured from the player) ───
// Chunks inside MESH_DISTANCE are drawn.
// Chunk data is kept one ring further out, because a chunk
// can only be meshed once all four neighbours exist.
// Beyond this the distant terrain (render/FarTerrain.h) takes over.
// Set from Settings (Esc -> Settings -> Chunk distance); only
// changed on the main thread, between frames.
constexpr int MESH_DISTANCE_MIN = 2, MESH_DISTANCE_MAX = 16, MESH_DISTANCE_DEFAULT = 6;
inline int MESH_DISTANCE = MESH_DISTANCE_DEFAULT;
inline int loadDistance() { return MESH_DISTANCE + 1; }

// ── ChunkManager ──────────────────────────────────────────
// Owns the voxel data for the whole world. Pure data — no
// generation, no rendering. Chunks start out empty (nullptr)
// and are filled in by WorldStreamer as the player moves.
class ChunkManager {
public:
    ChunkManager() {
        int total = WORLD_SIZE_X * WORLD_SIZE_Z;
        chunks.resize(total);
        dirty.assign(total, 0);
        meshDirty.assign(total, 0);
    }

    static bool inWorld(int cx, int cz) {
        return cx >= 0 && cx < WORLD_SIZE_X &&
               cz >= 0 && cz < WORLD_SIZE_Z;
    }

    int index(int cx, int cz) const {
        return cx * WORLD_SIZE_Z + cz;
    }

    Chunk* getChunk(int cx, int cz) {
        if (!inWorld(cx, cz)) return nullptr;
        return chunks[index(cx, cz)].get();
    }

    const Chunk* getChunk(int cx, int cz) const {
        if (!inWorld(cx, cz)) return nullptr;
        return chunks[index(cx, cz)].get();
    }

    bool isLoaded(int cx, int cz) const {
        return getChunk(cx, cz) != nullptr;
    }

    void setChunk(int cx, int cz, ChunkPtr chunk) {
        if (!inWorld(cx, cz)) return;
        chunks[index(cx, cz)] = std::move(chunk);
    }

    // ── Dirty tracking ────────────────────────────────────
    // dirty     = changed since last save  → written on Save and Quit
    // meshDirty = sections changed since last mesh (one bit per
    //             section) → remeshed by WorldStreamer
    // 'sections' says which sections were edited. The sections
    // just above and below are remeshed too, and so are the
    // neighbouring columns, because faces on a border may have
    // changed.
    void markDirty(int cx, int cz, uint8_t sections = 0xFF) {
        if (!inWorld(cx, cz)) return;
        dirty[index(cx, cz)] = 1;
        uint8_t m = (uint8_t)(sections | (sections << 1) | (sections >> 1));
        markMeshDirty(cx,     cz, m);
        markMeshDirty(cx - 1, cz, m);
        markMeshDirty(cx + 1, cz, m);
        markMeshDirty(cx, cz - 1, m);
        markMeshDirty(cx, cz + 1, m);
    }

    // Which section a world height is in, as a bit
    static uint8_t sectionBit(int y) {
        return (uint8_t)(1u << std::clamp(y / VOXEL_DIM, 0, SECTIONS - 1));
    }

    void markMeshDirty(int cx, int cz, uint8_t sections = 0xFF) {
        if (inWorld(cx, cz)) meshDirty[index(cx, cz)] |= sections;
    }

    bool isDirty(int cx, int cz) const {
        return inWorld(cx, cz) && dirty[index(cx, cz)];
    }

    uint8_t meshDirtySections(int cx, int cz) const {
        return inWorld(cx, cz) ? meshDirty[index(cx, cz)] : 0;
    }

    void clearDirty(int cx, int cz) {
        if (inWorld(cx, cz)) dirty[index(cx, cz)] = 0;
    }

    void clearMeshDirty(int cx, int cz) {
        if (inWorld(cx, cz)) meshDirty[index(cx, cz)] = 0;
    }

    // Voxel lookup relative to chunk (cx, cz). x and z may run
    // past the chunk edge — the lookup follows them into the
    // neighbouring chunk. Unloaded chunks read as air.
    uint16_t typeAtWorld(int cx, int cz,
                         int x, int y, int z) const {
        if (y < 0 || y >= WORLD_HEIGHT) return BLOCK_AIR;

        while (x < 0)          { cx--; x += VOXEL_DIM; }
        while (x >= VOXEL_DIM) { cx++; x -= VOXEL_DIM; }
        while (z < 0)          { cz--; z += VOXEL_DIM; }
        while (z >= VOXEL_DIM) { cz++; z -= VOXEL_DIM; }

        const Chunk* c = getChunk(cx, cz);
        if (!c) return BLOCK_AIR;
        return c->typeAt(x, y, z);
    }

    // ── Lookups by world voxel coordinate ─────────────────
    // (0, 0, 0) is the corner of chunk (0, 0). Used by physics,
    // which thinks in world positions rather than chunks.
    static int chunkCoord(int w) {
        return (w >= 0) ? w / VOXEL_DIM
                        : -((-w + VOXEL_DIM - 1) / VOXEL_DIM);
    }

    uint16_t typeAtVoxel(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= WORLD_HEIGHT) return BLOCK_AIR;
        int cx = chunkCoord(wx), cz = chunkCoord(wz);
        const Chunk* c = getChunk(cx, cz);
        if (!c) return BLOCK_AIR;
        return c->typeAt(wx - cx * VOXEL_DIM, wy,
                         wz - cz * VOXEL_DIM);
    }

    // True if the chunk holding this voxel column has its data.
    // Columns outside the world count as loaded — there is
    // nothing there to wait for, just open air.
    bool isColumnLoaded(int wx, int wz) const {
        int cx = chunkCoord(wx), cz = chunkCoord(wz);
        if (!inWorld(cx, cz)) return true;
        return isLoaded(cx, cz);
    }

    glm::vec3 chunkOrigin(int cx, int cz) const {
        return glm::vec3((float)(cx * VOXEL_DIM),
                         0.0f,
                         (float)(cz * VOXEL_DIM));
    }

    // Which chunk a world position is in (may be outside the world)
    glm::ivec2 worldToChunk(glm::vec3 pos) const {
        return glm::ivec2(
            (int)std::floor(pos.x / (float)VOXEL_DIM),
            (int)std::floor(pos.z / (float)VOXEL_DIM));
    }

    int loadedCount() const {
        int n = 0;
        for (auto& c : chunks) if (c) n++;
        return n;
    }

    size_t compressedBytes() const {
        size_t total = 0;
        for (auto& c : chunks)
            if (c) total += c->compressedBytes();
        return total;
    }

private:
    std::vector<ChunkPtr> chunks;
    std::vector<uint8_t>  dirty;
    std::vector<uint8_t>  meshDirty;
};
