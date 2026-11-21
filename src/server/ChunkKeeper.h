#pragma once
#include "../chunk/ChunkManager.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// ChunkKeeper — chunks loaded around everyone, no graphics
// ══════════════════════════════════════════════════════════
// The game's WorldStreamer loads AND meshes chunks around one
// camera. A server has no meshes and many players, so it keeps
// voxel data only, for every column within RADIUS of any point
// it's given (players; later busy NPCs), on worker threads:
//
//   update(points)   hands out loads for missing columns (nearest
//                    first), puts finished ones into the world,
//                    and drops columns no one is near (unless
//                    they hold unsaved edits)
//
// The ChunkManager is only touched by the thread calling update.
class ChunkKeeper {
public:
    using Source = std::function<ChunkPtr(int cx, int cz)>;
    static constexpr int RADIUS = 2;          // columns around each point (2 = 256 voxels)
    static constexpr int SLACK  = 1;          // further out before dropping

    ChunkKeeper(ChunkManager& w, Source src, int threads = 2) : world(w), source(std::move(src)) {
        for (int i = 0; i < std::max(1, threads); i++) workers.emplace_back([this] { work(); });
    }
    ~ChunkKeeper() { stop(); }

    // Stop the workers (waiting for a chunk being made). Call it
    // before the world or the generator goes away: the workers
    // use them.
    void stop() {
        { std::lock_guard<std::mutex> l(m); quit = true; jobs.clear(); }
        cv.notify_all();
        for (auto& t : workers) if (t.joinable()) t.join();
        workers.clear();
    }

    // points: world positions (voxels) that need ground around them
    void update(const std::vector<glm::vec3>& points) {
        // finished loads in
        std::vector<Done> got;
        { std::lock_guard<std::mutex> l(m); got.swap(done); }
        for (Done& d : got) {
            pending.erase({ d.cx, d.cz });
            if (d.chunk && !world.isLoaded(d.cx, d.cz)) world.setChunk(d.cx, d.cz, std::move(d.chunk));
        }
        // what's wanted
        std::vector<std::pair<int, std::pair<int, int>>> want;   // distance, column
        auto near = [&](int cx, int cz, int r) {
            for (const glm::vec3& p : points) {
                int pcx = ChunkManager::chunkCoord((int)std::floor(p.x)), pcz = ChunkManager::chunkCoord((int)std::floor(p.z));
                if (std::max(std::abs(cx - pcx), std::abs(cz - pcz)) <= r) return true;
            }
            return false;
        };
        for (const glm::vec3& p : points) {
            int pcx = ChunkManager::chunkCoord((int)std::floor(p.x)), pcz = ChunkManager::chunkCoord((int)std::floor(p.z));
            for (int dz = -RADIUS; dz <= RADIUS; dz++)
                for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                    int cx = pcx + dx, cz = pcz + dz;
                    if (!world.inWorld(cx, cz) || world.isLoaded(cx, cz) || pending.count({ cx, cz })) continue;
                    want.push_back({ std::max(std::abs(dx), std::abs(dz)), { cx, cz } });
                }
        }
        std::sort(want.begin(), want.end());
        if (!want.empty()) {
            std::lock_guard<std::mutex> l(m);
            for (auto& [d, c] : want) if (pending.insert(c).second) jobs.push_back(c);
        }
        cv.notify_all();
        // drop columns no one is near
        for (int cx = 0; cx < world.sizeX(); cx++)
            for (int cz = 0; cz < world.sizeZ(); cz++)
                if (world.isLoaded(cx, cz) && !world.isDirty(cx, cz) && !near(cx, cz, RADIUS + SLACK))
                    world.setChunk(cx, cz, nullptr);
    }

    // Wait until every column around the points is in (startup, tests)
    void loadNow(const std::vector<glm::vec3>& points) {
        for (int i = 0; i < 20000; i++) {
            update(points);
            bool all = true;
            for (const glm::vec3& p : points) {
                int pcx = ChunkManager::chunkCoord((int)std::floor(p.x)), pcz = ChunkManager::chunkCoord((int)std::floor(p.z));
                for (int dz = -RADIUS; dz <= RADIUS && all; dz++)
                    for (int dx = -RADIUS; dx <= RADIUS && all; dx++)
                        if (world.inWorld(pcx + dx, pcz + dz) && !world.isLoaded(pcx + dx, pcz + dz)) all = false;
            }
            if (all) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    size_t loading() const { return pending.size(); }

private:
    struct Done { int cx, cz; ChunkPtr chunk; };
    ChunkManager& world;
    Source source;
    std::vector<std::thread> workers;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<int, int>> jobs;
    std::vector<Done> done;
    std::set<std::pair<int, int>> pending;     // asked for, not back yet (update's thread only)
    bool quit = false;

    void work() {
        for (;;) {
            std::pair<int, int> c;
            {
                std::unique_lock<std::mutex> l(m);
                cv.wait(l, [&] { return quit || !jobs.empty(); });
                if (quit) return;
                c = jobs.front(); jobs.pop_front();
            }
            ChunkPtr chunk = source(c.first, c.second);
            std::lock_guard<std::mutex> l(m);
            done.push_back({ c.first, c.second, std::move(chunk) });
        }
    }
};
