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

// ── ChunkSection ──────────────────────────────────────────
// One 128³ block of voxels. Two storage states:
//
//   raw        flat 4 MB array — used while generating or editing
//   compressed RLE runs        — the resting state (tens of KB;
//              a section of pure air or pure stone is one run)
//
// typeAt() works in either state. setBlock() switches the
// section to raw automatically; compress() goes back.
class ChunkSection {
public:
    uint16_t typeAt(uint32_t localIndex) const {
        if (raw) return raw[localIndex];
        if (rle) return rle->at(localIndex);
        return BLOCK_AIR;
    }

    void set(uint32_t localIndex, uint16_t id) {
        ensureRaw();
        raw[localIndex] = id;
    }

    uint16_t* rawData() { ensureRaw(); return raw.get(); }

    void copyTo(uint16_t* out) const {
        if (raw) std::memcpy(out, raw.get(), VOXEL_COUNT * sizeof(uint16_t));
        else if (rle) rle->expand(out);
        else std::memset(out, 0, VOXEL_COUNT * sizeof(uint16_t));
    }

    void compress() {
        if (raw) { rle = RleData::build(raw.get()); raw.reset(); }
        else if (!rle) rle = allAirShared();
    }

    std::shared_ptr<const RleData> snapshot() { compress(); return rle; }

    // Replace the contents with ready-made runs (generation)
    void setRle(std::shared_ptr<const RleData> r) { raw.reset(); rle = std::move(r); }

    // True when the section is nothing but air
    bool isAir() const {
        if (raw) return false;   // unknown without a scan; treat as content
        return !rle || (rle->types.size() == 1 && rle->types[0] == BLOCK_AIR);
    }

    bool   isCompressed()    const { return !raw; }
    size_t compressedBytes() const { return rle ? rle->bytes() : 0; }

    // One shared all-air run list for every empty section
    static std::shared_ptr<const RleData> allAirShared() {
        static const std::shared_ptr<const RleData> air = RleData::allAir();
        return air;
    }

private:
    std::unique_ptr<uint16_t[]>    raw;
    std::shared_ptr<const RleData> rle;

    void ensureRaw() {
        if (raw) return;
        raw.reset(new uint16_t[VOXEL_COUNT]);
        if (rle) rle->expand(raw.get());
        else std::memset(raw.get(), 0, VOXEL_COUNT * sizeof(uint16_t));
        rle.reset();
    }
};

// ── Chunk ─────────────────────────────────────────────────
// One column of the world: 128 × 1024 × 128 voxels, stored as
// SECTIONS stacked sections. Loaded, saved and generated as a
// unit; meshed per section.
//
// index() is Y-major over the whole column:
//   index = (y * DIM + x) * DIM + z,   y in 0 .. WORLD_HEIGHT-1
// Section s holds indices [s * VOXEL_COUNT, (s+1) * VOXEL_COUNT),
// so for y < 128 it is also the index inside one section.
class Chunk {
public:
    static int index(int x, int y, int z) {
        return (y * VOXEL_DIM + x) * VOXEL_DIM + z;
    }

    static bool inBounds(int x, int y, int z) {
        return x >= 0 && x < VOXEL_DIM &&
               y >= 0 && y < WORLD_HEIGHT &&
               z >= 0 && z < VOXEL_DIM;
    }

    // Safe accessor — returns AIR for out of bounds
    uint16_t typeAt(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return BLOCK_AIR;
        return getIndex((uint32_t)index(x, y, z));
    }

    void setBlock(int x, int y, int z, uint16_t id) {
        if (!inBounds(x, y, z)) return;
        setIndex((uint32_t)index(x, y, z), id);
    }

    // By column index (see index())
    uint16_t getIndex(uint32_t idx) const {
        return sections[idx / VOXEL_COUNT].typeAt(idx % VOXEL_COUNT);
    }
    void setIndex(uint32_t idx, uint16_t id) {
        sections[idx / VOXEL_COUNT].set(idx % VOXEL_COUNT, id);
    }

    ChunkSection&       section(int s)       { return sections[s]; }
    const ChunkSection& section(int s) const { return sections[s]; }

    // Every section back to RLE
    void compress() { for (auto& s : sections) s.compress(); }

    // Immutable copy of one section, safe to hand to another
    // thread. Compresses that section if needed.
    std::shared_ptr<const RleData> snapshot(int s) { return sections[s].snapshot(); }

    bool isCompressed() const {
        for (auto& s : sections) if (!s.isCompressed()) return false;
        return true;
    }
    size_t compressedBytes() const {
        size_t n = 0;
        for (auto& s : sections) n += s.compressedBytes();
        return n;
    }

private:
    ChunkSection sections[SECTIONS];
};

// Chunks always live on the heap
using ChunkPtr = std::unique_ptr<Chunk>;

inline ChunkPtr makeChunk() {
    return std::make_unique<Chunk>();
}
