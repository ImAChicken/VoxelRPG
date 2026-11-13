#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../chunk/ChunkManager.h"
#include "../render/ChunkMesh.h"
#include "../render/Mesher.h"

// ══════════════════════════════════════════════════════════
// WorldStreamer — loads and meshes chunks around the player
// ══════════════════════════════════════════════════════════
// Generating and meshing a 128³ chunk takes tens of
// milliseconds. Doing that for dozens of chunks on the main
// thread is what froze the window ("Not Responding").
//
// Here all the heavy work runs on background threads:
//
//   main thread                     worker threads
//   ───────────                     ──────────────
//   update(): pick the nearest      load or generate a chunk
//   chunks that need work    ──▶    build a chunk's mesh
//
//   update(): take finished  ◀──    hand back the result
//   chunks, upload meshes
//   to the GPU
//
// The main thread never waits for a worker. Chunks simply
// appear, nearest first, as they finish.
//
// Threading rules:
//   - ChunkManager is only ever touched by the main thread
//   - workers get immutable snapshots (shared_ptr<const RleData>)
//   - OpenGL is only ever called from the main thread
class WorldStreamer {
public:
    // Supplies the voxel data for one chunk — reads it from
    // the save folder or generates it from the seed.
    // Called on worker threads, so it must be thread-safe.
    using ChunkSource = std::function<ChunkPtr(int cx, int cz)>;

    // workerThreads = 0 picks a count from the CPU
    WorldStreamer(ChunkManager& world,
                  ChunkSource source,
                  std::shared_ptr<const BlockRenderTable> table,
                  int workerThreads = 0)
        : world(world)
        , source(std::move(source))
        , table(std::move(table))
    {
        slots.resize((size_t)WORLD_SIZE_X * WORLD_SIZE_Z);

        int count = workerThreads;
        if (count <= 0) {
            // Leave one core for the main thread
            unsigned cores = std::thread::hardware_concurrency();
            count = (cores > 1) ? (int)cores - 1 : 1;
            count = std::min(count, 8);
        }

        // Only queue a little ahead of what the workers can
        // do, so the queue reacts quickly when the player
        // changes direction
        maxInFlight = count * 2;

        for (int i = 0; i < count; i++)
            workers.emplace_back([this] { workerLoop(); });
    }

    // Stops the workers. Must run on the main thread while
    // the OpenGL context is still alive (it frees the meshes).
    ~WorldStreamer() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& t : workers) t.join();
        for (Slot& s : slots) s.mesh.cleanup();
    }

    WorldStreamer(const WorldStreamer&)            = delete;
    WorldStreamer& operator=(const WorldStreamer&) = delete;

    // Call once per frame with the chunk the player is in
    void update(int playerCX, int playerCZ) {
        collectResults(playerCX, playerCZ);
        collectEdits();
        unloadFarMeshes(playerCX, playerCZ);
        scheduleWork(playerCX, playerCZ);
    }

    // Mesh for a chunk, or nullptr if it is not ready yet
    const ChunkMesh* getMesh(int cx, int cz) const {
        if (!ChunkManager::inWorld(cx, cz)) return nullptr;
        const Slot& s = slots[(size_t)world.index(cx, cz)];
        return s.hasMesh ? &s.mesh : nullptr;
    }

    // Resource pack changed — every mesh needs new tile info
    void setRenderTable(
            std::shared_ptr<const BlockRenderTable> newTable) {
        table = std::move(newTable);
        for (Slot& s : slots) {
            s.version++;
            s.needsMesh = true;
        }
    }

    // True when nothing is queued or running
    bool isIdle()        const { return inFlight == 0 && idle; }
    int  jobsInFlight()  const { return inFlight; }
    int  workerCount()   const { return (int)workers.size(); }

    int meshCount() const {
        int n = 0;
        for (const Slot& s : slots) if (s.hasMesh) n++;
        return n;
    }

private:
    // Uploading a mesh is a few MB copied to the GPU. Limit
    // how many happen per frame so a burst of finished
    // chunks cannot cause a hitch.
    static constexpr int MAX_UPLOADS_PER_FRAME = 4;

    // Per-chunk bookkeeping (main thread only)
    struct Slot {
        ChunkMesh mesh;
        bool      hasMesh   = false;  // mesh is on the GPU
        bool      needsMesh = true;   // mesh missing or out of date
        bool      loading   = false;  // a load job is running
        bool      meshing   = false;  // a mesh job is running
        uint32_t  version   = 0;      // bumped on every change
    };

    enum class JobKind { Load, Mesh };

    struct Job {
        JobKind   kind = JobKind::Load;
        int       cx = 0, cz = 0;
        uint32_t  version = 0;
        MeshInput input;              // Mesh jobs only
    };

    struct Result {
        JobKind       kind = JobKind::Load;
        int           cx = 0, cz = 0;
        uint32_t      version = 0;
        ChunkPtr      chunk;          // Load results
        ChunkMeshData mesh;           // Mesh results
    };

    ChunkManager& world;
    ChunkSource   source;
    std::shared_ptr<const BlockRenderTable> table;

    std::vector<Slot> slots;
    int  inFlight    = 0;
    int  maxInFlight = 2;
    bool idle        = false;

    // Finished meshes waiting for their turn to upload
    std::deque<Result> uploadQueue;

    // ── Shared with the workers (guarded by 'mutex') ──────
    std::vector<std::thread> workers;
    std::mutex               mutex;
    std::condition_variable  wake;
    std::deque<Job>          jobs;
    std::vector<Result>      results;
    bool                     stopping = false;

    Slot& slot(int cx, int cz) {
        return slots[(size_t)world.index(cx, cz)];
    }

    // Distance in chunks — a square ring around the player
    static int ringDistance(int cx, int cz, int pcx, int pcz) {
        return std::max(std::abs(cx - pcx), std::abs(cz - pcz));
    }

    // ── Worker thread ─────────────────────────────────────
    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] {
                    return stopping || !jobs.empty();
                });
                if (stopping) return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }

            Result result;
            result.kind    = job.kind;
            result.cx      = job.cx;
            result.cz      = job.cz;
            result.version = job.version;

            if (job.kind == JobKind::Load) {
                try {
                    result.chunk = source(job.cx, job.cz);
                } catch (...) {
                    result.chunk.reset();
                }
                // Never hand back a missing chunk — an empty
                // one keeps the world consistent
                if (!result.chunk) result.chunk = makeChunk();
                result.chunk->compress();
            } else {
                ChunkMesher::build(job.input, result.mesh);
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                results.push_back(std::move(result));
            }
        }
    }

    // ── 1. Take finished work from the workers ────────────
    void collectResults(int pcx, int pcz) {
        std::vector<Result> done;
        {
            std::lock_guard<std::mutex> lock(mutex);
            done.swap(results);
        }

        for (Result& r : done) {
            inFlight--;
            Slot& s = slot(r.cx, r.cz);
            if (r.kind == JobKind::Load) {
                // New voxel data — cheap, apply right away
                s.loading = false;
                world.setChunk(r.cx, r.cz, std::move(r.chunk));
            } else {
                s.meshing = false;
                uploadQueue.push_back(std::move(r));
            }
        }

        // Upload a few meshes per frame
        int uploads = 0;
        while (!uploadQueue.empty() &&
               uploads < MAX_UPLOADS_PER_FRAME) {
            Result r = std::move(uploadQueue.front());
            uploadQueue.pop_front();

            Slot& s = slot(r.cx, r.cz);
            // Skip meshes that went out of date while they
            // were being built (chunk edited, pack reloaded)
            // or whose chunk the player has since left behind
            if (r.version != s.version) continue;
            if (ringDistance(r.cx, r.cz, pcx, pcz)
                    > MESH_DISTANCE + 1) {
                s.needsMesh = true;
                continue;
            }

            s.mesh.upload(r.mesh);
            s.hasMesh = true;
            uploads++;
        }
    }

    // ── 2. Notice chunks that were edited ─────────────────
    void collectEdits() {
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            if (!world.isMeshDirty(cx, cz)) continue;
            world.clearMeshDirty(cx, cz);
            Slot& s = slot(cx, cz);
            s.version++;
            s.needsMesh = true;
            // The old mesh stays on screen until the new
            // one is ready, so edits never flicker
        }
    }

    // ── 3. Free meshes the player has left behind ─────────
    // One extra ring of slack so walking back and forth over
    // a chunk border does not rebuild the same meshes
    void unloadFarMeshes(int pcx, int pcz) {
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            Slot& s = slot(cx, cz);
            if (!s.hasMesh) continue;
            if (ringDistance(cx, cz, pcx, pcz)
                    <= MESH_DISTANCE + 1) continue;
            s.mesh.cleanup();
            s.hasMesh   = false;
            s.needsMesh = true;
        }
    }

    // ── 4. Hand out new work, nearest chunks first ────────
    struct Candidate {
        int     priority;
        JobKind kind;
        int     cx, cz;
    };

    bool neighboursLoaded(int cx, int cz) const {
        static const int OFF[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (const auto& o : OFF) {
            int nx = cx + o[0], nz = cz + o[1];
            if (ChunkManager::inWorld(nx, nz) &&
                !world.isLoaded(nx, nz))
                return false;
        }
        return true;
    }

    void scheduleWork(int pcx, int pcz) {
        std::vector<Candidate> candidates;

        for (int cx = pcx - LOAD_DISTANCE;
                 cx <= pcx + LOAD_DISTANCE; cx++)
        for (int cz = pcz - LOAD_DISTANCE;
                 cz <= pcz + LOAD_DISTANCE; cz++) {
            if (!ChunkManager::inWorld(cx, cz)) continue;
            const Slot& s = slot(cx, cz);
            int d = ringDistance(cx, cz, pcx, pcz);

            if (!world.isLoaded(cx, cz)) {
                if (!s.loading)
                    candidates.push_back(
                        { d * 2, JobKind::Load, cx, cz });
                continue;
            }

            // A chunk at ring d can be meshed once ring d + 1
            // is loaded, so its mesh sorts just after those
            if (d <= MESH_DISTANCE && s.needsMesh &&
                !s.meshing && neighboursLoaded(cx, cz))
                candidates.push_back(
                    { (d + 1) * 2 + 1, JobKind::Mesh, cx, cz });
        }

        idle = candidates.empty() && uploadQueue.empty();

        int room = maxInFlight - inFlight;
        if (room <= 0 || candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) {
                return a.priority < b.priority;
            });
        if ((int)candidates.size() > room)
            candidates.resize((size_t)room);

        std::vector<Job> newJobs;
        for (const Candidate& c : candidates) {
            Slot& s = slot(c.cx, c.cz);
            Job job;
            job.kind = c.kind;
            job.cx   = c.cx;
            job.cz   = c.cz;

            if (c.kind == JobKind::Load) {
                s.loading = true;
            } else {
                s.meshing   = true;
                s.needsMesh = false;
                job.version = s.version;
                job.input   = makeMeshInput(c.cx, c.cz);
            }
            newJobs.push_back(std::move(job));
        }

        inFlight += (int)newJobs.size();
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (Job& j : newJobs) jobs.push_back(std::move(j));
        }
        wake.notify_all();
    }

    // Snapshot a chunk and its four neighbours for a worker
    MeshInput makeMeshInput(int cx, int cz) {
        auto snap = [this](int x, int z)
                -> std::shared_ptr<const RleData> {
            Chunk* c = world.getChunk(x, z);
            return c ? c->snapshot() : nullptr;
        };
        MeshInput in;
        in.center = snap(cx,     cz);
        in.west   = snap(cx - 1, cz);
        in.east   = snap(cx + 1, cz);
        in.south  = snap(cx, cz - 1);
        in.north  = snap(cx, cz + 1);
        in.table  = table;
        return in;
    }
};
