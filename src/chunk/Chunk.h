#pragma once
#include "voxel/Voxel.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// ── RLE voxel data ────────────────────────────────────────
// A whole chunk stored as runs of identical block IDs.
//
// Voxels are laid out Y-major:  index = (y * DIM + x) * DIM + z
// so each horizontal slice is contiguous. That is what makes
// RLE work well on terrain: the solid stone below the surface
// and the open air above it each collapse into a single run,
// and only the slices near the surface cost anything.
//
// An RleData is immutable once built. Worker threads hold it
// through a shared_ptr, so the main thread can keep editing
// the chunk without any locking.
struct RleData {
    std::vector<uint32_t> ends;   // exclusive end index of each run
    std::vector<uint16_t> types;  // block ID of each run

    // Random access — binary search over the run ends
    uint16_t at(uint32_t index) const {
        auto it = std::upper_bound(ends.begin(), ends.end(), index);
        if (it == ends.end()) return BLOCK_AIR;
        return types[(size_t)(it - ends.begin())];
    }

    // Expand into a flat VOXEL_COUNT array
    void expand(uint16_t* out) const {
        uint32_t pos = 0;
        for (size_t i = 0; i < ends.size(); i++) {
            std::fill(out + pos, out + ends[i], types[i]);
            pos = ends[i];
        }
        if (pos < (uint32_t)VOXEL_COUNT)
            std::fill(out + pos, out + VOXEL_COUNT, BLOCK_AIR);
    }

    size_t bytes() const {
        return ends.size()  * sizeof(uint32_t)
             + types.size() * sizeof(uint16_t);
    }

    static std::shared_ptr<const RleData> build(const uint16_t* raw) {
        auto rle = std::make_shared<RleData>();
        uint16_t cur = raw[0];
        for (uint32_t i = 1; i < (uint32_t)VOXEL_COUNT; i++) {
            if (raw[i] != cur) {
                rle->ends.push_back(i);
                rle->types.push_back(cur);
                cur = raw[i];
            }
        }
        rle->ends.push_back((uint32_t)VOXEL_COUNT);
        rle->types.push_back(cur);
        rle->ends.shrink_to_fit();
        rle->types.shrink_to_fit();
        return rle;
    }

    static std::shared_ptr<const RleData> allAir() {
        auto rle = std::make_shared<RleData>();
        rle->ends.push_back((uint32_t)VOXEL_COUNT);
        rle->types.push_back(BLOCK_AIR);
        return rle;
    }
};

// ── Chunk ─────────────────────────────────────────────────
// 128³ voxels. Two storage states:
//
//   raw        flat 4 MB array — used while generating or editing
//   compressed RLE runs        — the resting state (tens of KB)
//
// typeAt() works in either state. setBlock() switches the chunk
// to raw automatically; call compress() when a batch of edits
// is done (snapshot() does it for you).
class Chunk {
public:
    static int index(int x, int y, int z) {
        return (y * VOXEL_DIM + x) * VOXEL_DIM + z;
    }

    static bool inBounds(int x, int y, int z) {
        return x >= 0 && x < VOXEL_DIM &&
               y >= 0 && y < VOXEL_DIM &&
               z >= 0 && z < VOXEL_DIM;
    }

    // Safe accessor — returns AIR for out of bounds
    uint16_t typeAt(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return BLOCK_AIR;
        if (raw) return raw[index(x, y, z)];
        if (rle) return rle->at((uint32_t)index(x, y, z));
        return BLOCK_AIR;
    }

    void setBlock(int x, int y, int z, uint16_t id) {
        if (!inBounds(x, y, z)) return;
        ensureRaw();
        raw[index(x, y, z)] = id;
    }

    // Direct access to the flat array for bulk writes
    // (terrain generation, file loading). Y-major layout.
    uint16_t* rawData() {
        ensureRaw();
        return raw.get();
    }

    // Copy every voxel into a flat VOXEL_COUNT array (Y-major)
    void copyTo(uint16_t* out) const {
        if (raw) std::memcpy(out, raw.get(),
                             VOXEL_COUNT * sizeof(uint16_t));
        else if (rle) rle->expand(out);
        else std::memset(out, 0,
                         VOXEL_COUNT * sizeof(uint16_t));
    }

    // Raw → RLE. Frees the 4 MB array.
    void compress() {
        if (raw) {
            rle = RleData::build(raw.get());
            raw.reset();
        } else if (!rle) {
            rle = RleData::allAir();
        }
    }

    // Immutable copy of the current contents, safe to hand
    // to another thread. Compresses the chunk if needed.
    std::shared_ptr<const RleData> snapshot() {
        compress();
        return rle;
    }

    bool   isCompressed()    const { return !raw; }
    size_t compressedBytes() const { return rle ? rle->bytes() : 0; }

private:
    std::unique_ptr<uint16_t[]>    raw;
    std::shared_ptr<const RleData> rle;

    void ensureRaw() {
        if (raw) return;
        raw.reset(new uint16_t[VOXEL_COUNT]);
        if (rle) rle->expand(raw.get());
        else std::memset(raw.get(), 0,
                         VOXEL_COUNT * sizeof(uint16_t));
        rle.reset();   // stale once the raw copy is edited
    }
};

// Chunks always live on the heap
using ChunkPtr = std::unique_ptr<Chunk>;

inline ChunkPtr makeChunk() {
    return std::make_unique<Chunk>();
}
