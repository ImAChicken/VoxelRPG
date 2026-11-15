#pragma once
#include "WorldEditor.h"
#include "../chunk/TerrainGenerator.h"
#include "../game/StructureRegistry.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// StructureManager — multi-block structures as single objects
// ══════════════════════════════════════════════════════════
// A structure instance is one placed copy of a structure type
// (game/StructureRegistry.h): a foundation, a wall, a door, a
// tree. Its voxels are ordinary blocks in the chunks; the
// instance records type, position, rotation and state, so it
// can tell which voxels are its own.
//
// What instances make possible:
//   - a door opens and closes as one piece (E)
//   - a tree is chopped as one thing: chops take health away
//     (health comes from the registry) and at 0 the whole
//     tree comes down
//   - building pieces stand on foundations: each piece knows
//     what holds it up, so removing a foundation brings down
//     everything resting on it
//
// Natural trees are not stored: the generator can find them
// from the seed. The first chop turns one into a real
// instance so its health is remembered.
//
// Every voxel change goes through WorldEditor, tagged with
// its source like any other edit.
//
// Saved per world in saves/<world>/structures.dat.
//
// Not handled yet: edits made with dev tools (fill, brush,
// explosions) do not update instances they cut through.
struct StructureInstance {
    uint32_t   id      = 0;
    uint16_t   type    = 0;
    glm::ivec3 origin  = {0, 0, 0};   // world position of the shape's anchor
    int        rot     = 0;           // quarter turns
    int        state   = 0;           // doors: 0 closed, 1 open
    float      health  = 0.0f;        // trees
    uint32_t   support = 0;           // instance holding this one up (0 = ground)
    bool       natural = false;       // a generated tree
    int        cellX = 0, cellZ = 0;  // natural trees: grid cell
};

class StructureManager {
public:
    StructureManager(ChunkManager& w, WorldEditor& ed, const TerrainGenerator& g)
        : world(w), editor(ed), gen(g) {}

    std::function<void(const std::string&)> log = [](const std::string&) {};

    // ── Looking things up ─────────────────────────────────
    const StructureType* typeOf(const StructureInstance& s) const {
        return gStructures.get(s.type);
    }

    // The voxel box an instance covers, [lo, hi]
    void bounds(const StructureInstance& s, glm::ivec3& lo, glm::ivec3& hi) const {
        const StructureType* t = typeOf(s);
        if (!t) { lo = hi = s.origin; return; }
        // Union of every state's box, so a door is found
        // whether open or closed
        lo = glm::ivec3(1 << 30); hi = glm::ivec3(-(1 << 30));
        for (int st = 0; st < t->states; st++) {
            const VoxelVolume& v = t->shape(st, s.rot);
            glm::ivec3 o = s.origin - v.anchor;
            lo = glm::min(lo, o);
            hi = glm::max(hi, o + v.size - glm::ivec3(1));
        }
    }

    // Does this instance own world voxel p in its current state?
    bool owns(const StructureInstance& s, glm::ivec3 p) const {
        const StructureType* t = typeOf(s);
        if (!t) return false;
        const VoxelVolume& v = t->shape(s.state, s.rot);
        glm::ivec3 q = p - s.origin + v.anchor;
        return v.inside(q.x, q.y, q.z) && v.at(q.x, q.y, q.z) != BLOCK_AIR;
    }

    // The instance that world voxel p belongs to, or nullptr.
    // Also finds natural trees (without creating them).
    StructureInstance* instanceAt(glm::ivec3 p) {
        return const_cast<StructureInstance*>(
            static_cast<const StructureManager*>(this)->instanceAt(p));
    }
    const StructureInstance* instanceAt(glm::ivec3 p) const {
        auto it = byChunk.find(key(ChunkManager::chunkCoord(p.x),
                                   ChunkManager::chunkCoord(p.z)));
        if (it != byChunk.end())
            for (uint32_t id : it->second) {
                auto f = instances.find(id);
                if (f != instances.end() && owns(f->second, p)) return &f->second;
            }
        return nullptr;
    }

    // Can a foundation sink into this voxel? Ground (earth,
    // stone, sand, snow, water, leaves) yes; wood and other
    // structures no.
    bool foundationCanReplace(glm::ivec3 p, uint16_t here) const {
        const std::string& m = gRegistry.get(here).material;
        if (m == "wood") return false;
        if (instanceAt(p)) return false;
        return m == "dirt" || m == "stone" || m == "organic" ||
               m == "none" || m == "fluid";
    }

    // Same, but a generated tree that has never been touched
    // is turned into an instance (so its health can drop)
    StructureInstance* instanceOrNaturalTree(glm::ivec3 p) {
        if (StructureInstance* s = instanceAt(p)) return s;
        TerrainGenerator::NaturalTree nt;
        if (!gen.naturalTreeAt(p, nt)) return nullptr;
        if (feltNatural.count(cellKey(nt.cellX, nt.cellZ))) return nullptr;
        uint16_t type = gStructures.find(nt.typeName);
        const StructureType* t = gStructures.get(type);
        if (!t) return nullptr;
        StructureInstance s;
        s.type    = type;
        s.origin  = nt.base;
        s.health  = t->health;
        s.natural = true;
        s.cellX = nt.cellX; s.cellZ = nt.cellZ;
        return &add(s);
    }

    // The type of an untouched natural tree at p (no instance
    // is created), or nullptr
    const StructureType* naturalTreeTypeAt(glm::ivec3 p) const {
        TerrainGenerator::NaturalTree nt;
        if (!gen.naturalTreeAt(p, nt)) return nullptr;
        if (feltNatural.count(cellKey(nt.cellX, nt.cellZ))) return nullptr;
        return gStructures.get(gStructures.find(nt.typeName));
    }

    size_t count() const { return instances.size(); }

    // Follow supports down to the foundation (0 if none)
    uint32_t rootOf(const StructureInstance& s) const {
        const StructureInstance* cur = &s;
        for (int guard = 0; guard < 256; guard++) {
            if (!cur->support) {
                const StructureType* t = typeOf(*cur);
                return (t && t->foundation) ? cur->id : 0;
            }
            auto it = instances.find(cur->support);
            if (it == instances.end()) return 0;
            cur = &it->second;
        }
        return 0;
    }

    // ── Placing ───────────────────────────────────────────
    // Why a placement would fail, or "" if it is fine.
    //   strictSupport — Survival: pieces that need support
    //                   must touch a foundation structure
    std::string checkPlace(uint16_t type, glm::ivec3 origin, int rot,
                           bool strictSupport, uint32_t* supportOut = nullptr) const {
        const StructureType* t = gStructures.get(type);
        if (!t) return "unknown structure";
        const VoxelVolume& v = t->shape(0, rot);
        glm::ivec3 o = origin - v.anchor;
        if (o.y < 0 || o.y + v.size.y > WORLD_HEIGHT) return "out of the world's height";

        // Trees grow into air only; everything else needs
        // every one of its voxels to be free
        if (t->kind != StructureKind::Tree) {
            for (int y = 0; y < v.size.y; y++)
            for (int z = 0; z < v.size.z; z++)
            for (int x = 0; x < v.size.x; x++) {
                if (v.at(x, y, z) == BLOCK_AIR) continue;
                glm::ivec3 p = o + glm::ivec3(x, y, z);
                uint16_t here = world.typeAtVoxel(p.x, p.y, p.z);
                if (here == BLOCK_AIR || here == BLOCK_LEAVES) continue;
                // Foundations sink into the ground
                if (t->foundation && foundationCanReplace(p, here)) continue;
                return "blocked";
            }
        }
        uint32_t sup = 0;
        if (!t->foundation && t->kind != StructureKind::Tree)
            sup = findSupport(v, o, 0, strictSupport);
        if (supportOut) *supportOut = sup;
        if (strictSupport && t->needsSupport && !sup)
            return "must be built onto a foundation";
        return "";
    }

    // Place an instance. Assumes checkPlace() passed.
    StructureInstance* place(uint16_t type, glm::ivec3 origin, int rot,
                             EditSource src, uint32_t support) {
        const StructureType* t = gStructures.get(type);
        if (!t) return nullptr;
        StructureInstance s;
        s.type = type; s.origin = origin; s.rot = rot & 3;
        s.health = t->health; s.support = support;
        editor.submit(writeShape(s, 0, src, t->displayName));
        return &add(s);
    }

    // ── Removing ──────────────────────────────────────────
    // Takes the instance's voxels out of the world (only the
    // ones still matching its shape) and brings down anything
    // that was resting on it. Returns the removed instances
    // (the first is the one asked for) so the caller can
    // refund costs.
    std::vector<StructureInstance> remove(uint32_t id, EditSource src) {
        std::vector<StructureInstance> gone;
        removeRec(id, src, gone);
        return gone;
    }

    // ── Doors ─────────────────────────────────────────────
    // Swap between closed and open. Fails if the swing path
    // is blocked (or 'bodyBlocks' says a body is in the way).
    std::string toggleDoor(StructureInstance& s, EditSource src,
                           const std::function<bool(glm::ivec3)>& bodyBlocks) {
        const StructureType* t = typeOf(s);
        if (!t || t->kind != StructureKind::Door) return "not a door";
        int next = 1 - s.state;
        const VoxelVolume& to = t->shape(next, s.rot);
        glm::ivec3 o = s.origin - to.anchor;
        for (int y = 0; y < to.size.y; y++)
        for (int z = 0; z < to.size.z; z++)
        for (int x = 0; x < to.size.x; x++) {
            if (to.at(x, y, z) == BLOCK_AIR) continue;
            glm::ivec3 p = o + glm::ivec3(x, y, z);
            if (owns(s, p)) continue;                     // its own panel
            // Leaves don't stop a door (you can walk through
            // them too); the swing clears them
            uint16_t here = world.typeAtVoxel(p.x, p.y, p.z);
            if (here != BLOCK_AIR && here != BLOCK_LEAVES) return "the door is blocked";
            if (bodyBlocks && bodyBlocks(p)) return "something is in the way";
        }
        // One batch: clear the old panel, then write the new
        // one (the hinge column is in both; the write wins)
        EditBatch b = clearShape(s, src, next ? "open door" : "close door");
        for (int y = 0; y < to.size.y; y++)
        for (int z = 0; z < to.size.z; z++)
        for (int x = 0; x < to.size.x; x++)
            if (to.at(x, y, z) != BLOCK_AIR)
                b.set(o.x + x, o.y + y, o.z + z, to.at(x, y, z));
        editor.apply(std::move(b));
        s.state = next;
        return "";
    }

    // ── Trees ─────────────────────────────────────────────
    // Take health off a tree. Returns true when it falls.
    bool chop(StructureInstance& s, float damage, EditSource src) {
        s.health -= damage;
        if (s.health > 0.0f) return false;
        if (s.natural) feltNatural.insert(cellKey(s.cellX, s.cellZ));
        remove(s.id, src);
        return true;
    }

    // ── Save / load ───────────────────────────────────────
    bool save(const std::string& worldFolder) const {
        std::ofstream f(worldFolder + "/structures.dat");
        if (!f) return false;
        f << "structures 1\n";
        for (auto& [id, s] : instances) {
            const StructureType* t = typeOf(s);
            if (!t) continue;
            // Untouched natural trees come back from the seed
            if (s.natural && s.health >= t->health) continue;
            f << "inst " << s.id << " " << t->name << " " << s.origin.x << " "
              << s.origin.y << " " << s.origin.z << " " << s.rot << " " << s.state
              << " " << s.health << " " << s.support << " " << (s.natural ? 1 : 0)
              << " " << s.cellX << " " << s.cellZ << "\n";
        }
        for (int64_t k : feltNatural)
            f << "felled " << (int)(k >> 32) << " " << (int)(uint32_t)k << "\n";
        return (bool)f;
    }

    void load(const std::string& worldFolder) {
        instances.clear(); byChunk.clear(); feltNatural.clear(); nextId = 1;
        std::ifstream f(worldFolder + "/structures.dat");
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream in(line);
            std::string tag;
            in >> tag;
            if (tag == "inst") {
                StructureInstance s;
                std::string name;
                int nat = 0;
                in >> s.id >> name >> s.origin.x >> s.origin.y >> s.origin.z >> s.rot
                   >> s.state >> s.health >> s.support >> nat >> s.cellX >> s.cellZ;
                s.type = gStructures.find(name);
                s.natural = nat != 0;
                if (!s.type || !in) continue;          // type removed from the registry
                nextId = std::max(nextId, s.id + 1);
                index(s);
                instances[s.id] = s;
            } else if (tag == "felled") {
                int cx = 0, cz = 0;
                in >> cx >> cz;
                feltNatural.insert(cellKey(cx, cz));
            }
        }
    }

private:
    ChunkManager&           world;
    WorldEditor&            editor;
    const TerrainGenerator& gen;

    std::unordered_map<uint32_t, StructureInstance> instances;
    std::unordered_map<int64_t, std::vector<uint32_t>> byChunk;   // chunk → ids
    std::unordered_set<int64_t> feltNatural;                      // cells already cut
    uint32_t nextId = 1;

    static int64_t key(int cx, int cz) { return ((int64_t)cx << 32) ^ (uint32_t)cz; }
    static int64_t cellKey(int cx, int cz) { return key(cx, cz); }

    StructureInstance& add(StructureInstance s) {
        s.id = nextId++;
        index(s);
        return instances[s.id] = s;
    }

    // List the instance under every chunk its box touches
    void index(const StructureInstance& s) {
        glm::ivec3 lo, hi;
        bounds(s, lo, hi);
        for (int cx = ChunkManager::chunkCoord(lo.x); cx <= ChunkManager::chunkCoord(hi.x); cx++)
        for (int cz = ChunkManager::chunkCoord(lo.z); cz <= ChunkManager::chunkCoord(hi.z); cz++)
            byChunk[key(cx, cz)].push_back(s.id);
    }
    void unindex(const StructureInstance& s) {
        glm::ivec3 lo, hi;
        bounds(s, lo, hi);
        for (int cx = ChunkManager::chunkCoord(lo.x); cx <= ChunkManager::chunkCoord(hi.x); cx++)
        for (int cz = ChunkManager::chunkCoord(lo.z); cz <= ChunkManager::chunkCoord(hi.z); cz++) {
            auto& v = byChunk[key(cx, cz)];
            v.erase(std::remove(v.begin(), v.end(), s.id), v.end());
        }
    }

    // Batch that writes an instance's shape (state st)
    EditBatch writeShape(const StructureInstance& s, int st, EditSource src,
                         const std::string& label) const {
        EditBatch b;
        b.source = src;
        b.label  = label.empty() ? "structure" : label;
        // Structure edits stay out of undo: undo would put
        // voxels back without the instance that owns them
        b.undoable = false;
        const StructureType* t = typeOf(s);
        const VoxelVolume& v = t->shape(st, s.rot);
        glm::ivec3 o = s.origin - v.anchor;
        bool tree = t->kind == StructureKind::Tree;
        for (int y = 0; y < v.size.y; y++)
        for (int z = 0; z < v.size.z; z++)
        for (int x = 0; x < v.size.x; x++) {
            uint16_t id = v.at(x, y, z);
            if (id == BLOCK_AIR) continue;
            glm::ivec3 p = o + glm::ivec3(x, y, z);
            // A tree only grows into open air
            if (tree && world.typeAtVoxel(p.x, p.y, p.z) != BLOCK_AIR) continue;
            b.set(p.x, p.y, p.z, id);
        }
        return b;
    }

    // Batch that turns an instance's voxels back into air —
    // only where they still hold the instance's own block
    EditBatch clearShape(const StructureInstance& s, EditSource src,
                         const std::string& label) const {
        EditBatch b;
        b.source   = src;
        b.label    = label;
        b.undoable = false;
        const StructureType* t = typeOf(s);
        const VoxelVolume& v = t->shape(s.state, s.rot);
        glm::ivec3 o = s.origin - v.anchor;
        for (int y = 0; y < v.size.y; y++)
        for (int z = 0; z < v.size.z; z++)
        for (int x = 0; x < v.size.x; x++) {
            uint16_t id = v.at(x, y, z);
            if (id == BLOCK_AIR) continue;
            glm::ivec3 p = o + glm::ivec3(x, y, z);
            if (world.typeAtVoxel(p.x, p.y, p.z) == id) b.set(p.x, p.y, p.z, BLOCK_AIR);
        }
        return b;
    }

    // An instance with a voxel right under or beside the
    // shape (placed at corner o), other than 'skip'.
    //   rooted — only accept pieces that trace back to a
    //            foundation (so pieces can't hold each other
    //            up in mid-air)
    uint32_t findSupport(const VoxelVolume& v, glm::ivec3 o, uint32_t skip,
                         bool rooted) const {
        auto check = [&](glm::ivec3 p) -> uint32_t {
            auto it = byChunk.find(key(ChunkManager::chunkCoord(p.x),
                                       ChunkManager::chunkCoord(p.z)));
            if (it == byChunk.end()) return 0;
            for (uint32_t id : it->second) {
                if (id == skip) continue;
                auto f = instances.find(id);
                if (f == instances.end()) continue;
                const StructureType* t = typeOf(f->second);
                if (!t || t->kind == StructureKind::Tree) continue;
                if (!owns(f->second, p)) continue;
                if (rooted && !rootOf(f->second)) continue;
                return id;
            }
            return 0;
        };
        // Below the bottom layer
        for (int z = 0; z < v.size.z; z++)
        for (int x = 0; x < v.size.x; x++)
            if (v.at(x, 0, z) != BLOCK_AIR)
                if (uint32_t id = check(o + glm::ivec3(x, -1, z))) return id;
        // Touching the sides
        for (int y = 0; y < v.size.y; y++)
        for (int z = 0; z < v.size.z; z++)
        for (int x = 0; x < v.size.x; x++) {
            if (v.at(x, y, z) == BLOCK_AIR) continue;
            static const glm::ivec3 side[4] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
            for (auto d : side) {
                glm::ivec3 n(x + d.x, y, z + d.z);
                if (v.inside(n.x, n.y, n.z) && v.at(n.x, n.y, n.z) != BLOCK_AIR) continue;
                if (uint32_t id = check(o + n)) return id;
            }
        }
        return 0;
    }

    void removeRec(uint32_t id, EditSource src, std::vector<StructureInstance>& gone) {
        auto it = instances.find(id);
        if (it == instances.end()) return;
        StructureInstance s = it->second;
        const StructureType* t = typeOf(s);
        if (t) {
            EditBatch b = clearShape(s, src, "remove " + t->displayName);
            b.undoable = false;
            editor.submit(std::move(b));
        }
        unindex(s);
        instances.erase(it);
        gone.push_back(s);

        // Anything it was holding up: find other support, or fall
        std::vector<uint32_t> resting;
        for (auto& [oid, o] : instances) if (o.support == id) resting.push_back(oid);
        for (uint32_t rid : resting) {
            auto r = instances.find(rid);
            if (r == instances.end()) continue;
            const StructureType* rt = typeOf(r->second);
            const VoxelVolume& v = rt->shape(r->second.state, r->second.rot);
            // The removed piece is already gone, so anything
            // that only stood on it no longer traces back to
            // a foundation and is refused here
            uint32_t other = findSupport(v, r->second.origin - v.anchor, rid, true);
            if (other) { r->second.support = other; continue; }
            removeRec(rid, src, gone);
        }
    }
};
