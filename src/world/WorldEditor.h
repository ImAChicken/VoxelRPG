#pragma once
#include "../chunk/ChunkManager.h"
#include "../voxel/Voxel.h"
#include "../voxel/VoxelRegistry.h"
#include "VoxelVolume.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// WorldEditor — the ONE way anything changes the world
// ══════════════════════════════════════════════════════════
// The player breaking a block, a dev tool filling a box, a
// typed command, an explosion, and later scripts and the
// server all go through here. Nothing else writes to chunks.
//
// Why one door:
//   - Batching. An edit is collected first, then applied one
//     chunk at a time: decompress once, write, recompress
//     once, remesh once. Writing voxel by voxel would remesh
//     the same chunk hundreds of times.
//   - Source tags. Every batch says who made it, so the same
//     edit can have different results (a Survival break drops
//     an item; a dev break does not) and, later, permissions
//     and multiplayer sync key off it.
//   - Undo. A batch knows every voxel's old value, so keeping
//     those gives undo/redo for free.
//   - Big edits never freeze the game: queued batches are
//     applied a few chunks per frame by update().
//
// Main thread only — chunks belong to the main thread.

// Who made an edit
enum class EditSource : uint8_t {
    Player,      // survival hands and tools
    Dev,         // dev tools held in Test Play
    Command,     // typed console commands
    Explosion,   // explosions (any cause)
    Script,      // future: scripted events
    Server,      // future: multiplayer authority
};

inline const char* editSourceName(EditSource s) {
    switch (s) {
    case EditSource::Player:    return "player";
    case EditSource::Dev:       return "dev";
    case EditSource::Command:   return "command";
    case EditSource::Explosion: return "explosion";
    case EditSource::Script:    return "script";
    case EditSource::Server:    return "server";
    }
    return "?";
}

// One voxel that actually changed
struct VoxelChange {
    glm::ivec3 pos;
    uint16_t   oldID;
    uint16_t   newID;
};

// What happened when a batch was applied
struct EditResult {
    size_t changed          = 0;  // voxels that took a new value
    size_t skippedUnloaded  = 0;  // target chunk not in memory
    size_t skippedProtected = 0;  // indestructible, or not the
                                  // block being replaced
    // Filled only when the batch asks for it (wantChanges)
    std::vector<VoxelChange> changes;
};

// ── EditBatch ─────────────────────────────────────────────
// A list of "set this voxel to that block", grouped by chunk.
// Build it with set(), then hand it to WorldEditor.
struct EditBatch {
    EditSource  source = EditSource::Dev;
    std::string label  = "edit";   // shown by undo: "undid fill"

    bool undoable    = true;   // keep old values for undo
    bool wantChanges = false;  // return every change (item drops)

    // Leave blocks without the "destructible" flag alone
    bool respectIndestructible = false;

    // Only overwrite voxels that are currently this block
    // (-1 = overwrite anything). Used by replace.
    int onlyReplace = -1;

    // Called when the batch has been fully applied
    std::function<void(const EditResult&)> onDone;

    // Add one voxel. Outside the world's height is ignored.
    void set(int wx, int wy, int wz, uint16_t id) {
        if (wy < 0 || wy >= WORLD_HEIGHT) return;
        int cx = ChunkManager::chunkCoord(wx);
        int cz = ChunkManager::chunkCoord(wz);
        int lx = wx - cx * VOXEL_DIM;
        int lz = wz - cz * VOXEL_DIM;
        ChunkEdits& c = chunkFor(cx, cz);
        c.idx.push_back((uint32_t)Chunk::index(lx, wy, lz));
        c.ids.push_back(id);
        count++;
    }

    size_t size()  const { return count; }
    bool   empty() const { return count == 0; }

    struct ChunkEdits {
        int cx = 0, cz = 0;
        std::vector<uint32_t> idx;
        std::vector<uint16_t> ids;
    };
    std::vector<ChunkEdits> chunks;

private:
    std::unordered_map<int64_t, size_t> lookup;
    size_t count = 0;

    ChunkEdits& chunkFor(int cx, int cz) {
        int64_t key = ((int64_t)cx << 32) ^ (uint32_t)cz;
        auto it = lookup.find(key);
        if (it != lookup.end()) return chunks[it->second];
        lookup[key] = chunks.size();
        chunks.push_back(ChunkEdits{ cx, cz, {}, {} });
        return chunks.back();
    }
};

// ── WorldEditor ───────────────────────────────────────────
class WorldEditor {
public:
    // Undo keeps at most this many batches, and drops the
    // oldest once their voxels add up to more than the cap
    static constexpr size_t UNDO_MAX_BATCHES = 64;
    static constexpr size_t UNDO_MAX_VOXELS  = 32u * 1024u * 1024u;

    // Batches bigger than this are queued and spread across
    // frames instead of applied on the spot
    static constexpr size_t IMMEDIATE_LIMIT = 200000;

    explicit WorldEditor(ChunkManager& w) : world(w) {}

    // Told about every voxel that changes (world position, new
    // block): the server sends them on to clients (server/ServerHost.h)
    std::function<void(glm::ivec3, uint16_t)> onVoxel;

    // ── Applying ──────────────────────────────────────────
    // Apply now, whatever the size. Use for small edits
    // that must finish before the caller continues (the
    // player breaking one block).
    EditResult apply(EditBatch batch) {
        Job job = makeJob(std::move(batch));
        while (job.next < job.batch.chunks.size())
            applyChunk(job, job.batch.chunks[job.next++]);
        return finish(job);
    }

    // Apply now if small, otherwise queue it. The batch's
    // onDone tells you when it has finished.
    void submit(EditBatch batch) {
        if (batch.size() <= IMMEDIATE_LIMIT && queue.empty()) {
            EditResult r = apply(std::move(batch));
            (void)r;
            return;
        }
        queue.push_back(makeJob(std::move(batch)));
    }

    // Work through queued batches for up to budgetMs. Call
    // once per frame.
    void update(float budgetMs = 4.0f) {
        auto start = Clock::now();
        while (!queue.empty()) {
            Job& job = queue.front();
            while (job.next < job.batch.chunks.size()) {
                applyChunk(job, job.batch.chunks[job.next++]);
                if (elapsedMs(start) > budgetMs) return;
            }
            Job done = std::move(queue.front());
            queue.pop_front();
            finish(done);
        }
    }

    // Finish every queued batch right now (before saving,
    // before undo)
    void flush() {
        while (!queue.empty()) update(1e9f);
    }

    bool   busy()        const { return !queue.empty(); }
    size_t queuedJobs()  const { return queue.size(); }

    // ── Undo / redo ───────────────────────────────────────
    // Undoes the most recent undoable batch, whoever made it.
    // Returns its label ("fill") or "" if there is nothing.
    std::string undo() {
        flush();
        if (undoStack.empty()) return "";
        UndoEntry e = std::move(undoStack.back());
        undoStack.pop_back();
        undoVoxels -= e.voxels;
        replay(e, /*forward=*/false);
        std::string label = e.label;
        redoStack.push_back(std::move(e));
        return label;
    }

    std::string redo() {
        flush();
        if (redoStack.empty()) return "";
        UndoEntry e = std::move(redoStack.back());
        redoStack.pop_back();
        replay(e, /*forward=*/true);
        std::string label = e.label;
        pushUndo(std::move(e), /*clearRedo=*/false);
        return label;
    }

    size_t undoCount() const { return undoStack.size(); }
    size_t redoCount() const { return redoStack.size(); }

    // ── Shapes ────────────────────────────────────────────
    // These only BUILD batches; pass the result to apply() or
    // submit(). Coordinates are world voxels.

    // Every voxel in the box between two corners (inclusive,
    // either order)
    static EditBatch box(glm::ivec3 a, glm::ivec3 b, uint16_t id,
                         EditSource src, const std::string& label) {
        EditBatch batch = start(src, label);
        glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
        lo.y = std::max(lo.y, 0);
        hi.y = std::min(hi.y, WORLD_HEIGHT - 1);
        for (int y = lo.y; y <= hi.y; y++)
        for (int z = lo.z; z <= hi.z; z++)
        for (int x = lo.x; x <= hi.x; x++)
            batch.set(x, y, z, id);
        return batch;
    }

    // Every voxel whose centre is within radius of centre
    static EditBatch sphere(glm::vec3 centre, float radius,
                            uint16_t id, EditSource src,
                            const std::string& label) {
        EditBatch batch = start(src, label);
        forSphere(centre, radius, [&](int x, int y, int z, float) {
            batch.set(x, y, z, id);
        });
        return batch;
    }

    // An explosion crater: the inner 70 % becomes air, the
    // outer shell becomes each block's "broken" form
    // (on_destroy in blocks.toml: stone → cracked stone),
    // and indestructible blocks are left alone.
    static EditBatch explosion(const ChunkManager& w,
                               glm::vec3 centre, float radius,
                               EditSource src) {
        EditBatch batch = start(src, "explosion");
        batch.respectIndestructible = true;
        float inner = radius * 0.7f;
        forSphere(centre, radius, [&](int x, int y, int z, float d) {
            uint16_t old = w.typeAtVoxel(x, y, z);
            if (old == BLOCK_AIR) return;
            uint16_t id = BLOCK_AIR;
            if (d > inner) id = gRegistry.get(old).onDestroyID;
            batch.set(x, y, z, id);
        });
        return batch;
    }

    // Place a volume so its anchor lands on 'target'.
    //   skipAir — air in the volume leaves the world alone,
    //             so a pasted tree does not carve a box out
    static EditBatch paste(const VoxelVolume& vol, glm::ivec3 target,
                           bool skipAir, EditSource src) {
        EditBatch batch = start(src, "paste");
        glm::ivec3 origin = target - vol.anchor;
        for (int y = 0; y < vol.size.y; y++)
        for (int z = 0; z < vol.size.z; z++)
        for (int x = 0; x < vol.size.x; x++) {
            uint16_t id = vol.at(x, y, z);
            if (skipAir && id == BLOCK_AIR) continue;
            batch.set(origin.x + x, origin.y + y, origin.z + z, id);
        }
        return batch;
    }

    // Copy the box between two corners out of the world.
    // Unloaded chunks read as air.
    VoxelVolume copy(glm::ivec3 a, glm::ivec3 b) const {
        glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
        lo.y = std::max(lo.y, 0);
        hi.y = std::min(hi.y, WORLD_HEIGHT - 1);
        VoxelVolume vol(hi - lo + glm::ivec3(1));
        for (int y = lo.y; y <= hi.y; y++)
        for (int z = lo.z; z <= hi.z; z++)
        for (int x = lo.x; x <= hi.x; x++)
            vol.set(x - lo.x, y - lo.y, z - lo.z,
                    world.typeAtVoxel(x, y, z));
        return vol;
    }

    const ChunkManager& worldRef() const { return world; }

    uint16_t typeAt(glm::ivec3 p) const {
        return world.typeAtVoxel(p.x, p.y, p.z);
    }

private:
    using Clock = std::chrono::steady_clock;

    ChunkManager& world;

    // Old and new value of every voxel a batch changed, per
    // chunk, in the order they were written
    struct ChunkUndo {
        int cx = 0, cz = 0;
        std::vector<uint32_t> idx;
        std::vector<uint16_t> oldIDs, newIDs;
    };
    struct UndoEntry {
        EditSource  source;
        std::string label;
        std::vector<ChunkUndo> chunks;
        size_t      voxels = 0;
    };

    struct Job {
        EditBatch  batch;
        size_t     next = 0;
        EditResult result;
        UndoEntry  undo;
    };

    std::deque<Job>        queue;
    std::vector<UndoEntry> undoStack, redoStack;
    size_t                 undoVoxels = 0;

    static EditBatch start(EditSource src, const std::string& label) {
        EditBatch b;
        b.source = src;
        b.label  = label;
        return b;
    }

    static float elapsedMs(Clock::time_point t) {
        return std::chrono::duration<float, std::milli>(
            Clock::now() - t).count();
    }

    // Calls fn(x, y, z, distance) for every voxel in a sphere
    template <typename Fn>
    static void forSphere(glm::vec3 c, float r, Fn&& fn) {
        int x0 = (int)std::floor(c.x - r), x1 = (int)std::ceil(c.x + r);
        int y0 = std::max(0, (int)std::floor(c.y - r));
        int y1 = std::min(WORLD_HEIGHT - 1, (int)std::ceil(c.y + r));
        int z0 = (int)std::floor(c.z - r), z1 = (int)std::ceil(c.z + r);
        for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            glm::vec3 d = glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) - c;
            float dist = glm::length(d);
            if (dist <= r) fn(x, y, z, dist);
        }
    }

    Job makeJob(EditBatch batch) {
        Job job;
        job.undo.source = batch.source;
        job.undo.label  = batch.label;
        job.batch       = std::move(batch);
        return job;
    }

    // Write one chunk's share of a batch
    void applyChunk(Job& job, EditBatch::ChunkEdits& ce) {
        const EditBatch& b = job.batch;
        Chunk* chunk = world.getChunk(ce.cx, ce.cz);
        if (!chunk) {
            job.result.skippedUnloaded += ce.idx.size();
            return;
        }

        size_t    changedHere = 0;
        uint8_t   sections = 0;             // which sections changed
        ChunkUndo cu;
        cu.cx = ce.cx;
        cu.cz = ce.cz;

        for (size_t i = 0; i < ce.idx.size(); i++) {
            uint32_t at  = ce.idx[i];
            uint16_t old = chunk->getIndex(at);
            uint16_t id  = ce.ids[i];
            if (old == id) continue;
            if (b.onlyReplace >= 0 && old != (uint16_t)b.onlyReplace) {
                job.result.skippedProtected++;
                continue;
            }
            if (b.respectIndestructible && old != BLOCK_AIR &&
                !gRegistry.get(old).isDestructible()) {
                job.result.skippedProtected++;
                continue;
            }
            chunk->setIndex(at, id);
            if (onVoxel) onVoxel(worldPos(ce.cx, ce.cz, at), id);
            sections |= (uint8_t)(1u << (at / VOXEL_COUNT));
            changedHere++;
            job.result.changed++;
            if (b.undoable) {
                cu.idx.push_back(at);
                cu.oldIDs.push_back(old);
                cu.newIDs.push_back(id);
            }
            if (b.wantChanges)
                job.result.changes.push_back(
                    { worldPos(ce.cx, ce.cz, at), old, id });
        }

        chunk->compress();                  // recompress once
        if (changedHere) world.markDirty(ce.cx, ce.cz, sections);  // save + remesh
        if (!cu.idx.empty()) {
            job.undo.voxels += cu.idx.size();
            job.undo.chunks.push_back(std::move(cu));
        }
    }

    EditResult finish(Job& job) {
        if (job.batch.undoable && job.undo.voxels > 0)
            pushUndo(std::move(job.undo), /*clearRedo=*/true);
        if (job.batch.onDone) job.batch.onDone(job.result);
        return std::move(job.result);
    }

    void pushUndo(UndoEntry e, bool clearRedo) {
        if (clearRedo) redoStack.clear();
        undoVoxels += e.voxels;
        undoStack.push_back(std::move(e));
        // Drop the oldest until within both limits
        while (!undoStack.empty() &&
               (undoStack.size() > UNDO_MAX_BATCHES ||
                (undoVoxels > UNDO_MAX_VOXELS && undoStack.size() > 1))) {
            undoVoxels -= undoStack.front().voxels;
            undoStack.erase(undoStack.begin());
        }
    }

    // Put an undo entry's old values back (forward = false)
    // or its new values again (forward = true). Old values
    // are written in reverse order so a voxel written twice
    // in one batch ends up with its original value.
    void replay(const UndoEntry& e, bool forward) {
        for (const ChunkUndo& cu : e.chunks) {
            Chunk* chunk = world.getChunk(cu.cx, cu.cz);
            if (!chunk) continue;
            size_t n = cu.idx.size();
            uint8_t sections = 0;
            if (forward)
                for (size_t i = 0; i < n; i++)
                    chunk->setIndex(cu.idx[i], cu.newIDs[i]);
            else
                for (size_t i = n; i-- > 0; )
                    chunk->setIndex(cu.idx[i], cu.oldIDs[i]);
            if (onVoxel)
                for (size_t i = 0; i < n; i++) onVoxel(worldPos(cu.cx, cu.cz, cu.idx[i]), forward ? cu.newIDs[i] : chunk->getIndex(cu.idx[i]));
            for (size_t i = 0; i < n; i++)
                sections |= (uint8_t)(1u << (cu.idx[i] / VOXEL_COUNT));
            chunk->compress();
            world.markDirty(cu.cx, cu.cz, sections);
        }
    }

    // Chunk + index in chunk → world voxel
    static glm::ivec3 worldPos(int cx, int cz, uint32_t idx) {
        int z = (int)(idx % VOXEL_DIM);
        int x = (int)((idx / VOXEL_DIM) % VOXEL_DIM);
        int y = (int)(idx / (VOXEL_DIM * VOXEL_DIM));
        return { cx * VOXEL_DIM + x, y, cz * VOXEL_DIM + z };
    }
};
