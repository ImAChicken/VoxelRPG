#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "chunk/Chunk.h"
#include "voxel/VoxelRegistry.h"
#include "render/ResourcePack.h"

// ══════════════════════════════════════════════════════════
// Greedy chunk mesher — CPU only
// ══════════════════════════════════════════════════════════
// Turns voxel data into a triangle list. Nothing in this file
// touches OpenGL, so it runs on worker threads (and would run
// on a headless server). The GPU side lives in ChunkMesh.h.
//
// Greedy meshing merges neighbouring faces of the same block
// into one big rectangle. A flat 128 × 128 grass surface is
// 16,384 separate faces drawn naively — here it is 1 quad.

// ── Face directions ───────────────────────────────────────
// Stored in every vertex. The shader uses it to pick the
// face shade and how the texture is laid across the face.
enum Face : uint8_t {
    FACE_TOP    = 0,   // +Y
    FACE_BOTTOM = 1,   // -Y
    FACE_NORTH  = 2,   // +Z
    FACE_SOUTH  = 3,   // -Z
    FACE_WEST   = 4,   // -X
    FACE_EAST   = 5,   // +X
};

// ── Vertex — 8 bytes ──────────────────────────────────────
// Position is in voxels relative to the chunk corner (0..128),
// so it fits in a byte. No UVs are stored: the shader works
// them out from the position, which is what lets one large
// quad repeat its texture every few voxels.
struct ChunkVertex {
    uint8_t x, y, z;
    uint8_t face;          // Face enum
    uint8_t tileX, tileY;  // which tile in the atlas grid
    uint8_t pad0, pad1;
};

// ── Mesh data produced by the mesher ──────────────────────
// One vertex array holding two ranges:
//   [0, opaqueCount)             solid blocks — drawn with
//                                back-face culling
//   [opaqueCount, size())        cut-out blocks (leaves) —
//                                drawn two-sided so you see
//                                the far side through the gaps
struct ChunkMeshData {
    std::vector<ChunkVertex> vertices;
    uint32_t opaqueCount = 0;

    uint32_t quadCount() const {
        return (uint32_t)vertices.size() / 6;
    }
};

// ── Per-block render info ─────────────────────────────────
// A snapshot of what the mesher needs from the block registry
// and the active resource pack, indexed by block ID. Built on
// the main thread, then shared read-only with the workers.
struct BlockRenderInfo {
    uint8_t tileX[6] = {0,0,0,0,0,0};   // atlas tile per Face
    uint8_t tileY[6] = {0,0,0,0,0,0};
    bool    opaque   = false;   // completely hides what is behind it
    bool    cutout   = false;   // has see-through pixels (leaves)
    bool    glow     = false;   // gives off light: drawn at full brightness
    bool    sprite   = false;   // crossed flat pictures (flowers, reeds), not a cube
    uint8_t spriteW  = 1;       // voxels wide
    uint8_t spanW    = 1, spanH = 1;   // atlas tiles the picture covers
};

struct BlockRenderTable {
    std::vector<BlockRenderInfo> blocks;

    const BlockRenderInfo& get(uint16_t id) const {
        return blocks[id < blocks.size() ? id : 0];
    }

    static std::shared_ptr<const BlockRenderTable> build(
            const VoxelRegistry&       registry,
            const ResourcePackManager& packs) {
        auto table = std::make_shared<BlockRenderTable>();
        table->blocks.resize(std::max<size_t>(registry.size(), 1));

        for (size_t id = 1; id < registry.size(); id++) {
            const VoxelTypeDef& def = registry.get((uint16_t)id);
            FaceUV uv = packs.getFaceUV(def.name);

            BlockRenderInfo& info = table->blocks[id];
            info.opaque = (def.opacity == VoxelOpacity::Full);
            info.cutout = (def.opacity == VoxelOpacity::Partial);
            info.glow   = def.glows();
            info.sprite  = def.sprite;
            info.spriteW = def.spriteWidth;
            info.spanW   = (uint8_t)uv.spanW;
            info.spanH   = (uint8_t)uv.spanH;
            if (info.sprite) info.opaque = info.cutout = false;

            info.tileX[FACE_TOP]    = (uint8_t)uv.topX;
            info.tileY[FACE_TOP]    = (uint8_t)uv.topY;
            info.tileX[FACE_BOTTOM] = (uint8_t)uv.botX;
            info.tileY[FACE_BOTTOM] = (uint8_t)uv.botY;
            for (int f = FACE_NORTH; f <= FACE_EAST; f++) {
                info.tileX[f] = (uint8_t)uv.sideX;
                info.tileY[f] = (uint8_t)uv.sideY;
            }
        }
        return table;
    }
};

// ── Everything one mesh job needs ─────────────────────────
// One 128³ section plus its six neighbours, so faces on the
// border are hidden when the neighbour covers them. A null
// side neighbour (edge of the world) or a null section above
// (top of the world) reads as air; below the bottom section
// is treated as solid, since nobody can look up from there.
struct MeshInput {
    std::shared_ptr<const RleData> center;
    std::shared_ptr<const RleData> west;    // column at cx - 1
    std::shared_ptr<const RleData> east;    // column at cx + 1
    std::shared_ptr<const RleData> south;   // column at cz - 1
    std::shared_ptr<const RleData> north;   // column at cz + 1
    std::shared_ptr<const RleData> above;   // section sy + 1
    std::shared_ptr<const RleData> below;   // section sy - 1
    bool bottomOfWorld = false;
    std::shared_ptr<const BlockRenderTable> table;
};

class ChunkMesher {
public:
    // Build the mesh for one chunk. Thread-safe: uses only
    // its inputs and per-thread scratch memory.
    static void build(const MeshInput& in, ChunkMeshData& out) {
        out.vertices.clear();
        out.opaqueCount = 0;
        if (!in.center || !in.table) return;

        // A chunk that is one single run is all one block.
        // All air (very common high up) has nothing to draw.
        if (in.center->types.size() == 1 &&
            in.center->types[0] == BLOCK_AIR)
            return;

        // Solid rock with solid rock on every side shows no
        // face at all — very common deep under a mountain
        auto solid = [&](const std::shared_ptr<const RleData>& r) {
            return r && r->types.size() == 1 &&
                   in.table->get(r->types[0]).opaque;
        };
        if (solid(in.center) && solid(in.west) && solid(in.east) &&
            solid(in.south) && solid(in.north) && solid(in.above) &&
            (in.bottomOfWorld || solid(in.below)))
            return;

        Scratch& s = scratch();
        fillVolume(in, s);

        const BlockRenderTable& table = *in.table;
        s.cutout.clear();

        //            normal  u  v   sign  face
        sweep(s, table, 1,    2, 0,  +1,   FACE_TOP,    out.vertices);
        sweep(s, table, 1,    2, 0,  -1,   FACE_BOTTOM, out.vertices);
        sweep(s, table, 2,    0, 1,  +1,   FACE_NORTH,  out.vertices);
        sweep(s, table, 2,    0, 1,  -1,   FACE_SOUTH,  out.vertices);
        sweep(s, table, 0,    1, 2,  -1,   FACE_WEST,   out.vertices);
        sweep(s, table, 0,    1, 2,  +1,   FACE_EAST,   out.vertices);

        sprites(in, s, table);

        // Solid quads first, cut-out quads after
        out.opaqueCount = (uint32_t)out.vertices.size();
        out.vertices.insert(out.vertices.end(),
                            s.cutout.begin(), s.cutout.end());
    }

private:
    // The chunk is copied into a volume padded by one voxel on
    // every side. The padding holds the neighbours' border
    // voxels, so the mesher never needs a bounds check.
    static constexpr int DIM = VOXEL_DIM;      // chunk size
    static constexpr int PAD = VOXEL_DIM + 2;  // padded size

    // Distance between neighbouring voxels along X, Y, Z in
    // the padded array (Y-major, same as Chunk)
    static constexpr int STRIDE[3] = { PAD, PAD * PAD, 1 };

    static int pidx(int x, int y, int z) {
        return ((y + 1) * PAD + (x + 1)) * PAD + (z + 1);
    }

    struct Scratch {
        std::vector<uint16_t>    flat;     // chunk expanded from RLE
        std::vector<uint16_t>    vol;      // padded volume
        std::vector<uint16_t>    mask;     // one slice of faces
        std::vector<ChunkVertex> cutout;   // two-sided quads
    };

    // One set of buffers per thread, reused for every chunk
    static Scratch& scratch() {
        static thread_local Scratch s;
        if (s.vol.empty()) {
            s.flat.resize((size_t)VOXEL_COUNT);
            s.vol.resize((size_t)PAD * PAD * PAD);
            s.mask.resize((size_t)DIM * DIM);
        }
        return s;
    }

    static void fillVolume(const MeshInput& in, Scratch& s) {
        std::memset(s.vol.data(), 0,
                    s.vol.size() * sizeof(uint16_t));

        // Centre chunk — copy row by row into the padded volume
        in.center->expand(s.flat.data());
        for (int y = 0; y < DIM; y++)
        for (int x = 0; x < DIM; x++) {
            std::memcpy(&s.vol[(size_t)pidx(x, y, 0)],
                        &s.flat[(size_t)Chunk::index(x, y, 0)],
                        DIM * sizeof(uint16_t));
        }

        // Neighbour borders — just the one layer that touches us
        for (int y = 0; y < DIM; y++)
        for (int i = 0; i < DIM; i++) {
            if (in.west)
                s.vol[(size_t)pidx(-1, y, i)] = in.west->at(
                    (uint32_t)Chunk::index(DIM - 1, y, i));
            if (in.east)
                s.vol[(size_t)pidx(DIM, y, i)] = in.east->at(
                    (uint32_t)Chunk::index(0, y, i));
            if (in.south)
                s.vol[(size_t)pidx(i, y, -1)] = in.south->at(
                    (uint32_t)Chunk::index(i, y, DIM - 1));
            if (in.north)
                s.vol[(size_t)pidx(i, y, DIM)] = in.north->at(
                    (uint32_t)Chunk::index(i, y, 0));
        }
        // The layers above and below
        for (int x = 0; x < DIM; x++)
        for (int z = 0; z < DIM; z++) {
            if (in.above)
                s.vol[(size_t)pidx(x, DIM, z)] = in.above->at(
                    (uint32_t)Chunk::index(x, 0, z));
            if (in.below)
                s.vol[(size_t)pidx(x, -1, z)] = in.below->at(
                    (uint32_t)Chunk::index(x, DIM - 1, z));
            else if (in.bottomOfWorld)
                s.vol[(size_t)pidx(x, -1, z)] = BLOCK_STONE;
        }
    }

    // Mesh every face pointing one way.
    //   n      axis the faces point along (0 = X, 1 = Y, 2 = Z)
    //   u, v   the two axes spanning the face — always chosen
    //          so that u × v = +n (a right-handed set)
    //   sign   +1 or -1: which way along n the faces point
    static void sweep(Scratch& s,
                      const BlockRenderTable& table,
                      int n, int u, int v,
                      int sign, uint8_t face,
                      std::vector<ChunkVertex>& opaqueOut) {
        const uint16_t* vol  = s.vol.data();
        uint16_t*       mask = s.mask.data();
        const int sn = STRIDE[n], su = STRIDE[u], sv = STRIDE[v];
        const int origin   = pidx(0, 0, 0);
        const int toNeighbour = sign * sn;

        for (int slice = 0; slice < DIM; slice++) {
            // ── Which voxels in this slice show a face? ───
            bool any = false;
            for (int j = 0; j < DIM; j++) {
                int idx = origin + slice * sn + j * sv;
                uint16_t* row = mask + j * DIM;
                for (int i = 0; i < DIM; i++, idx += su) {
                    uint16_t id    = vol[idx];
                    uint16_t other = vol[idx + toNeighbour];
                    bool show = false;
                    if (id != BLOCK_AIR && id != other) {
                        // Visible if the neighbour is air, or
                        // is something you can see through.
                        // Sprites draw no cube faces, and water
                        // doesn't wall off a reed standing in it.
                        const BlockRenderInfo& oi = table.get(other);
                        show = ((other == BLOCK_AIR) || !oi.opaque) &&
                               !table.get(id).sprite &&
                               !(id == BLOCK_WATER && oi.sprite);
                    }
                    row[i] = show ? id : (uint16_t)0;
                    any |= show;
                }
            }
            if (!any) continue;

            // ── Merge faces into the largest rectangles ───
            for (int j = 0; j < DIM; j++)
            for (int i = 0; i < DIM; ) {
                uint16_t id = mask[j * DIM + i];
                if (id == 0) { i++; continue; }

                // Grow along u
                int w = 1;
                while (i + w < DIM &&
                       mask[j * DIM + i + w] == id) w++;

                // Grow along v while the whole row matches
                int h = 1;
                while (j + h < DIM) {
                    const uint16_t* row = mask + (j + h) * DIM + i;
                    int k = 0;
                    while (k < w && row[k] == id) k++;
                    if (k < w) break;
                    h++;
                }

                const BlockRenderInfo& info = table.get(id);
                emitQuad(info.cutout ? s.cutout : opaqueOut,
                         n, u, v, sign, face,
                         slice, i, j, w, h,
                         info.tileX[face], info.tileY[face], info.glow ? 1 : 0);

                // Mark the merged area as done
                for (int jj = j; jj < j + h; jj++)
                    std::memset(mask + jj * DIM + i, 0,
                                (size_t)w * sizeof(uint16_t));
                i += w;
            }
        }
    }

    // ── Sprites ───────────────────────────────────────────
    // A stack of one sprite block is one picture: two quads
    // crossing on the diagonals of its footprint, as tall as the
    // stack, drawn two-sided with the cut-outs. Vertex layout:
    //   x, z   position + 8 (a 3-wide footprint pokes out of the
    //          chunk by one voxel)
    //   face   6 or 7 (which diagonal)
    //   pad0   bit 0 glow, bit 1 mirror the picture
    //   pad1   bit 0 corner u, bit 1 corner v (top),
    //          bits 2-3 tiles wide - 1, bits 4-6 tiles tall - 1
    static void sprites(const MeshInput& in, Scratch& s, const BlockRenderTable& table) {
        const RleData& r = *in.center;
        bool any = false;
        for (uint16_t t : r.types) if (table.get(t).sprite) { any = true; break; }
        if (!any) return;
        uint32_t start = 0;
        for (size_t k = 0; k < r.ends.size(); k++) {
            uint32_t end = r.ends[k];
            uint16_t id = r.types[k];
            const BlockRenderInfo& info = table.get(id);
            if (info.sprite)
                for (uint32_t i = start; i < end; i++) {
                    int z = (int)(i % DIM), x = (int)((i / DIM) % DIM), y = (int)(i / (DIM * DIM));
                    if (s.vol[(size_t)pidx(x, y - 1, z)] == id) continue;      // not the bottom of a stack
                    int h = 1;
                    while (y + h <= DIM && s.vol[(size_t)pidx(x, y + h, z)] == id) h++;
                    int k2 = info.spriteW / 2;
                    int x0 = x - k2 + 8, x1 = x + 1 + k2 + 8, z0 = z - k2 + 8, z1 = z + 1 + k2 + 8;
                    uint32_t hsh = (uint32_t)(x * 73856093) ^ (uint32_t)(z * 19349663) ^ (uint32_t)(y * 83492791);
                    uint8_t p0 = (uint8_t)((info.glow ? 1 : 0) | ((hsh >> 7) & 1 ? 2 : 0));
                    uint8_t span = (uint8_t)(((info.spanW - 1) & 3) << 2 | ((info.spanH - 1) & 7) << 4);
                    auto quad = [&](uint8_t face, int ax, int az, int bx, int bz) {
                        auto v = [&](int cu, int cv) {
                            return ChunkVertex{ (uint8_t)(cu ? bx : ax), (uint8_t)(y + (cv ? h : 0)), (uint8_t)(cu ? bz : az),
                                                face, info.tileX[FACE_NORTH], info.tileY[FACE_NORTH], p0,
                                                (uint8_t)(span | (cu ? 1 : 0) | (cv ? 2 : 0)) };
                        };
                        ChunkVertex a = v(0, 0), b = v(1, 0), c = v(1, 1), d = v(0, 1);
                        s.cutout.insert(s.cutout.end(), { a, b, c, a, c, d });
                    };
                    quad(6, x0, z0, x1, z1);
                    quad(7, x0, z1, x1, z0);
                }
            start = end;
        }
    }

    // Two triangles for one merged rectangle.
    //
    // Winding: OpenGL treats counter-clockwise triangles as
    // front faces. Because u × v = +n, walking the corners
    //   (i, j) → (i+w, j) → (i+w, j+h) → (i, j+h)
    // is counter-clockwise when seen from the +n side. Faces
    // pointing toward -n walk the corners the other way round.
    static void emitQuad(std::vector<ChunkVertex>& out,
                         int n, int u, int v,
                         int sign, uint8_t face,
                         int slice, int i, int j,
                         int w, int h,
                         uint8_t tileX, uint8_t tileY, uint8_t glow = 0) {
        // The face sits on the far side of the voxel for +n
        int plane = slice + (sign > 0 ? 1 : 0);

        auto corner = [&](int cu, int cv) {
            uint8_t p[3];
            p[n] = (uint8_t)plane;
            p[u] = (uint8_t)cu;
            p[v] = (uint8_t)cv;
            return ChunkVertex{ p[0], p[1], p[2], face,
                                tileX, tileY, glow, 0 };
        };

        ChunkVertex c0 = corner(i,     j);
        ChunkVertex c1 = corner(i + w, j);
        ChunkVertex c2 = corner(i + w, j + h);
        ChunkVertex c3 = corner(i,     j + h);

        if (sign > 0) {
            out.push_back(c0); out.push_back(c1); out.push_back(c2);
            out.push_back(c0); out.push_back(c2); out.push_back(c3);
        } else {
            out.push_back(c0); out.push_back(c3); out.push_back(c2);
            out.push_back(c0); out.push_back(c2); out.push_back(c1);
        }
    }
};
