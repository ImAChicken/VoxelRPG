#pragma once
#include "../physics/RigidVoxelBody.h"
#include "../physics/VoxelBall.h"
#include "../entity/Player.h"
#include "../game/ItemRegistry.h"
#include "../voxel/VoxelRegistry.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Woodcutting — felled trees (logs) and wood on the ground
// ══════════════════════════════════════════════════════════
// What happens after an axe takes a tree's health to zero:
//
//   1. The tree comes out of the world (StructureManager::fell):
//      its wood becomes a LOG, a heavy rigid body (physics/
//      RigidVoxelBody.h, 0.6 kg a voxel: an oak is a few hundred
//      kilograms to tonnes) that turns over the cut away from
//      the axe, falls, lands and settles. A short stump stays
//      standing. The leaves break off: falling leaf particles.
//   2. A falling log that hits a body knocks it about through
//      the ragdoll (Human::hit), by its weight and speed.
//   3. The log stays a loose object for good: it is never
//      written back into the world's voxels. Bodies, dropped
//      items and other logs collide with it (solidAt), the axe
//      hits it, and it is drawn textured and lit like the world
//      (render/PropRenderer.h).
//   4. Chopping the log drops wood on the ground, a share of the
//      tree's wood per hit, by damage; the last hit breaks it up.
//   5. Wood on the ground falls, settles, and is picked up by
//      walking near it (it slides toward you from a few feet).
//
// Particles (leaves, wood chips) are only for show.
// Logs and drops are saved per world (logs.dat, drops.dat).
// Headless: the world is reached only through the solid test.
class Woodcutting {
public:
    // ── Tuning ────────────────────────────────────────────
    static constexpr int   STUMP_HEIGHT  = 2;       // voxels of trunk left standing
    static constexpr float LOG_HEALTH    = 0.75f;   // a log's health, as a share of its tree's
    static constexpr float HURT_SPEED    = 10.0f;   // vox/s: a falling log this fast hurts
    static constexpr float HURT_MAX      = 6000.0f; // kg·vox/s most impulse one blow passes on
    static constexpr float DROP_RADIUS   = 0.6f;
    static constexpr float DROP_GRAVITY  = 80.0f;
    static constexpr float PICKUP_RANGE  = 3.0f;    // vox from the body (feet to head)
    static constexpr float MAGNET_RANGE  = 9.0f;    // drops slide toward you from here
    static constexpr float DROP_LIFE     = 600.0f;  // s before a drop on the ground is gone
    static constexpr int   MAX_LEAVES    = 220;     // leaf particles per tree

    struct TreeInfo {
        std::string name;           // "Oak"
        uint16_t    dropItem = 0;   // the wood it gives
        int         dropCount = 0;  // in all
        float       health = 40.0f; // the tree's; its log gets LOG_HEALTH of it
    };
    // A felled tree: falling, then lying as a log
    struct Log {
        uint32_t       id = 0;
        RigidVoxelBody body;
        std::string    name;        // "Fallen Oak"
        uint16_t       block = 0;   // its main wood block (chips, drops' look)
        float    health = 0.0f, maxHealth = 1.0f;
        uint16_t dropItem = 0;
        int      totalWood = 0;     // all it gives
        int      woodLeft = 0;      // still to drop
        float    owed = 0.0f;       // part of a piece earned by damage
        float    hurtCooldown = 0.0f;
        uint32_t moved = 0;         // counts pose changes, so the renderer relights
    };
    struct ItemDrop {
        glm::vec3 pos{0}, vel{0};
        uint16_t  item = 0;
        int       count = 0;
        float     age = 0.0f;
        bool      resting = false;
    };
    struct Particle {
        glm::vec3 p{0}, v{0};
        uint16_t  block = 0;        // drawn as a little piece of this block
        float life = 0.0f, maxLife = 1.0f, size = 0.5f, spin = 0.0f, drag = 0.0f;
    };
    struct LogHit {
        bool  broken = false;       // the log is gone
        int   dropped = 0;          // pieces of wood dropped
    };

    // ── Felling ───────────────────────────────────────────
    // A tree's voxels have been taken out (StructureManager::fell).
    // push: the way it should fall; pivot: the cut.
    void fellTree(std::vector<VoxelPiece> wood, const std::vector<VoxelPiece>& foliage,
                  glm::vec3 push, glm::vec3 pivot, const TreeInfo& info) {
        for (size_t i = 0; i < foliage.size(); i++) {
            if (foliage.size() > MAX_LEAVES && (rng() % foliage.size()) >= MAX_LEAVES) continue;
            const VoxelPiece& f = foliage[i];
            Particle pt;
            pt.p = glm::vec3(f.p) + 0.5f;
            pt.v = glm::vec3(frand(-3, 3), frand(-2, 2), frand(-3, 3)) + glm::vec3(push.x, 0, push.z) * 2.0f;
            pt.block = f.id;
            pt.maxLife = pt.life = frand(1.4f, 3.2f);
            pt.size = frand(0.5f, 0.9f);
            pt.drag = 2.5f;
            pt.spin = frand(-6, 6);
            particles.push_back(pt);
        }
        if (wood.empty()) return;
        auto lg = std::make_unique<Log>();
        lg->id = nextId++;
        lg->name = "Fallen " + info.name;
        lg->block = mainBlock(wood);
        lg->maxHealth = lg->health = std::max(10.0f, info.health * LOG_HEALTH);
        lg->dropItem = info.dropItem;
        lg->totalWood = lg->woodLeft = info.dropCount;
        lg->body.build(wood, push, pivot);
        logs.push_back(std::move(lg));
    }

    // ── One physics tick ──────────────────────────────────
    //   worldSolid: the world's voxels only
    //   player: may be hit by falling logs and picks up drops
    void tick(float dt, const SolidFn& worldSolid, Player* player) {
        for (auto& lg : logs) {
            if (lg->body.settled()) continue;
            const RigidVoxelBody* self = &lg->body;
            lg->body.step(dt, [&](int x, int y, int z) { return worldSolid(x, y, z) || solidAt(x, y, z, self); });
            lg->moved++;
            if (player) hurt(*lg, *player, dt);
        }
        SolidFn all = [&](int x, int y, int z) { return worldSolid(x, y, z) || solidAt(x, y, z); };
        tickDrops(dt, all, player);
        tickParticles(dt, all);
    }

    // ── Logs as solid things ──────────────────────────────
    // Is voxel (x, y, z) filled by a log (its centre inside one)?
    // skip: a body to leave out (itself, when it moves)
    bool solidAt(int x, int y, int z, const RigidVoxelBody* skip = nullptr) const {
        glm::vec3 p(x + 0.5f, y + 0.5f, z + 0.5f);
        for (const auto& lg : logs)
            if (&lg->body != skip && lg->body.occupies(p)) return true;
        return false;
    }
    // The log a world point is in (or within half a voxel of)
    Log* logAt(glm::vec3 p) {
        for (auto& lg : logs)
            if (lg->body.occupies(p) || lg->body.occupies(p + glm::vec3(0, 0.5f, 0)) ||
                lg->body.occupies(p - glm::vec3(0, 0.5f, 0)))
                return lg.get();
        return nullptr;
    }
    Log* logAt(glm::ivec3 v) { return logAt(glm::vec3(v) + 0.5f); }

    // An axe hit on a log: health down by dmg, wood out in
    // proportion (dropped at 'point', thrown back along -dir).
    // instant: Test Play, breaks it in one go.
    LogHit hitLog(Log& log, float dmg, glm::vec3 point, glm::vec3 dir, bool instant) {
        LogHit r;
        if (instant) dmg = log.health + 1.0f;
        float before = log.health;
        log.health = std::max(0.0f, log.health - dmg);
        chips(point, log.block, 8, -dir);
        int give;
        if (log.health <= 0.0f) {
            give = log.woodLeft;
        } else {
            // each hit earns its share of all the log's wood
            log.owed += (before - log.health) / log.maxHealth * (float)log.totalWood;
            give = std::min((int)std::floor(log.owed), log.woodLeft);
            log.owed -= (float)give;
        }
        if (give > 0 && log.dropItem) {
            drop(log.dropItem, give, point - dir * 1.5f + glm::vec3(0, 1, 0), -dir);
            log.woodLeft -= give;
            r.dropped = give;
        }
        if (log.health <= 0.0f) {
            // breaks up: a burst of chips along it
            glm::mat4 T = log.body.transform();
            const auto& vox = log.body.voxels();
            for (size_t i = 0; i < vox.size(); i += 6)
                chips(glm::vec3(T * glm::vec4(glm::vec3(vox[i].p) + 0.5f, 1.0f)), vox[i].id, 1, glm::vec3(0, 1, 0));
            uint32_t id = log.id;
            logs.erase(std::remove_if(logs.begin(), logs.end(), [&](const std::unique_ptr<Log>& l) { return l->id == id; }),
                       logs.end());
            for (auto& l : logs) l->body.wake();       // anything lying on it settles again
            r.broken = true;
        }
        return r;
    }

    // Wood chips (or any splinters) flying from a hit
    void chips(glm::vec3 at, uint16_t block, int n, glm::vec3 away) {
        for (int i = 0; i < n; i++) {
            Particle pt;
            pt.p = at;
            pt.v = away * frand(6, 14) + glm::vec3(frand(-6, 6), frand(4, 12), frand(-6, 6));
            pt.block = block;
            pt.maxLife = pt.life = frand(0.5f, 1.1f);
            pt.size = frand(0.25f, 0.45f);
            pt.spin = frand(-12, 12);
            particles.push_back(pt);
        }
    }

    // Something dropped on the ground
    void drop(uint16_t item, int count, glm::vec3 at, glm::vec3 away) {
        const ItemDef& d = gItems.get(item);
        int cap = std::max<int>(1, d.maxStack);
        int pieces = std::clamp(count, 1, 4);           // a few pieces look better than one
        for (int k = 0; k < pieces && count > 0; k++) {
            int n = std::min(cap, (count + (pieces - k) - 1) / (pieces - k));
            ItemDrop dr;
            dr.item = item;
            dr.count = n;
            dr.pos = at + glm::vec3(frand(-0.5f, 0.5f), frand(0, 0.5f), frand(-0.5f, 0.5f));
            dr.vel = away * frand(4, 9) + glm::vec3(frand(-4, 4), frand(10, 16), frand(-4, 4));
            drops.push_back(dr);
            count -= n;
        }
    }

    // ── For the renderer and the HUD ──────────────────────
    const std::vector<std::unique_ptr<Log>>& allLogs() const { return logs; }
    const std::vector<ItemDrop>& itemDrops() const { return drops; }
    const std::vector<Particle>& allParticles() const { return particles; }
    size_t logCount() const { return logs.size(); }
    std::string lastPickup;          // "+3 Log" when wood is picked up
    float       lastPickupAge = 99.0f;

    // ── Save / load ───────────────────────────────────────
    // A log: its voxels as they were in the tree, and its pose
    bool save(const std::string& folder) const {
        std::ofstream f(folder + "/logs.dat");
        if (!f) return false;
        f << "logs 2\n";
        for (const auto& lg : logs) {
            glm::vec3 c = lg->body.centre();
            glm::quat q = lg->body.rotation();
            f << "log " << lg->id << " " << lg->health << " " << lg->maxHealth << " "
              << (lg->dropItem ? gItems.get(lg->dropItem).name : std::string("-")) << " "
              << lg->woodLeft << " " << lg->totalWood << " "
              << c.x << " " << c.y << " " << c.z << " " << q.w << " " << q.x << " " << q.y << " " << q.z << " "
              << lg->body.voxels().size();
            for (const VoxelPiece& v : lg->body.voxels())
                f << " " << v.p.x << " " << v.p.y << " " << v.p.z << " " << gRegistry.get(v.id).name;
            f << " | " << lg->name << "\n";
        }
        std::ofstream d(folder + "/drops.dat");
        d << "drops 1\n";
        for (const ItemDrop& dr : drops)
            d << "drop " << gItems.get(dr.item).name << " " << dr.count << " "
              << dr.pos.x << " " << dr.pos.y << " " << dr.pos.z << " " << dr.age << "\n";
        return (bool)f;
    }
    void load(const std::string& folder) {
        logs.clear(); drops.clear(); particles.clear();
        std::ifstream f(folder + "/logs.dat");
        std::string line;
        std::getline(f, line);
        bool v2 = line.rfind("logs 2", 0) == 0;          // version 1 logs were voxels in the world
        while (v2 && std::getline(f, line)) {
            std::istringstream in(line);
            std::string tag; in >> tag;
            if (tag != "log") continue;
            auto lg = std::make_unique<Log>();
            std::string item; size_t n = 0;
            glm::vec3 c; glm::quat q;
            in >> lg->id >> lg->health >> lg->maxHealth >> item >> lg->woodLeft >> lg->totalWood
               >> c.x >> c.y >> c.z >> q.w >> q.x >> q.y >> q.z >> n;
            if (!in) continue;
            lg->dropItem = item == "-" ? 0 : gItems.find(item);
            std::vector<VoxelPiece> vox;
            for (size_t i = 0; i < n; i++) {
                VoxelPiece v; std::string b;
                in >> v.p.x >> v.p.y >> v.p.z >> b;
                v.id = gRegistry.getIDByName(b);
                if (v.id) vox.push_back(v);
            }
            std::string bar; in >> bar;
            std::getline(in, lg->name);
            if (!lg->name.empty() && lg->name[0] == ' ') lg->name.erase(0, 1);
            if (vox.empty()) continue;
            lg->block = mainBlock(vox);
            lg->body.build(vox, glm::vec3(1, 0, 0), glm::vec3(0));
            lg->body.setPose(c, q);
            nextId = std::max(nextId, lg->id + 1);
            logs.push_back(std::move(lg));
        }
        std::ifstream d(folder + "/drops.dat");
        while (std::getline(d, line)) {
            std::istringstream in(line);
            std::string tag, name; in >> tag;
            if (tag != "drop") continue;
            ItemDrop dr;
            in >> name >> dr.count >> dr.pos.x >> dr.pos.y >> dr.pos.z >> dr.age;
            dr.item = gItems.find(name);
            if (in && dr.item && dr.count > 0) drops.push_back(dr);
        }
    }

private:
    std::vector<std::unique_ptr<Log>> logs;
    std::vector<ItemDrop> drops;
    std::vector<Particle> particles;
    uint32_t nextId = 1;
    uint32_t seed = 12345u;

    uint32_t rng() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }
    float frand(float a, float b) { return a + (b - a) * (float)(rng() & 0xFFFF) / 65535.0f; }

    static uint16_t mainBlock(const std::vector<VoxelPiece>& v) {
        std::vector<std::pair<uint16_t, int>> n;
        for (const VoxelPiece& p : v) {
            auto it = std::find_if(n.begin(), n.end(), [&](auto& e) { return e.first == p.id; });
            if (it == n.end()) n.push_back({ p.id, 1 }); else it->second++;
        }
        return n.empty() ? 0 : std::max_element(n.begin(), n.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
    }

    // A falling log meeting a body: the blow goes to the
    // ragdoll (Human::hit), by the log's speed and weight
    void hurt(Log& t, Player& player, float dt) {
        t.hurtCooldown = std::max(0.0f, t.hurtCooldown - dt);
        if (t.hurtCooldown > 0.0f || !player.rules().hasBody) return;
        const float r = t.body.pointRadius();
        for (size_t i = 0; i < t.body.pointCount(); i++) {
            glm::vec3 p = t.body.point(i);
            if (p.y + r < player.position.y || p.y - r > player.position.y + player.height) continue;
            glm::vec2 d(p.x - player.position.x, p.z - player.position.z);
            if (glm::length(d) > Human::RADIUS + r) continue;
            glm::vec3 v = t.body.pointVelocity(i, dt);
            float speed = glm::length(v);
            if (speed < HURT_SPEED) continue;
            // the part of the log that lands on you: up to 400 kg
            // of it, at most HURT_MAX
            float J = std::min(HURT_MAX, speed * std::min(t.body.massKg(), 400.0f) * 0.6f);
            glm::vec3 dir = glm::normalize(v + glm::vec3(0, 0.2f * speed, 0));
            player.hit(p, dir * J);
            t.hurtCooldown = 1.0f;
            return;
        }
    }

    void tickDrops(float dt, const SolidFn& solid, Player* player) {
        bool canTake = player && player->rules().hasBody && !player->ragdoll.controlsLocked();
        auto bodyPointNear = [&](glm::vec3 p) {      // closest point on the body's feet-to-head line
            float y = std::clamp(p.y, player->position.y, player->position.y + player->height);
            return glm::vec3(player->position.x, y, player->position.z);
        };
        for (size_t i = 0; i < drops.size();) {
            ItemDrop& d = drops[i];
            d.age += dt;
            if (d.age > DROP_LIFE) { drops.erase(drops.begin() + i); continue; }
            if (canTake && d.age > 0.6f) {
                glm::vec3 to = bodyPointNear(d.pos) - d.pos;
                float dist = glm::length(to);
                if (dist < PICKUP_RANGE) {
                    int before = d.count;
                    d.count = player->activeInventory().add(d.item, d.count);
                    if (d.count < before) {
                        lastPickup = "+" + std::to_string(before - d.count) + " " + gItems.get(d.item).displayName;
                        lastPickupAge = 0.0f;
                    }
                    if (d.count <= 0) { drops.erase(drops.begin() + i); continue; }
                } else if (dist < MAGNET_RANGE) {
                    // slide toward you along the ground (and up a little)
                    glm::vec3 pull(to.x, std::max(to.y, 0.0f) + 2.0f, to.z);
                    d.vel += glm::normalize(pull) * 90.0f * dt;
                    d.resting = false;
                }
            }
            if (!d.resting) {
                d.vel.y -= DROP_GRAVITY * dt;
                d.pos += d.vel * dt;
                glm::vec3 n;
                if (collideBall(d.pos, DROP_RADIUS, solid, n)) {
                    float vn = glm::dot(d.vel, n);
                    if (vn < 0.0f) d.vel -= vn * n;
                    d.vel *= 0.85f;
                    if (glm::length(d.vel) < 0.8f && n.y > 0.5f) { d.vel = glm::vec3(0); d.resting = true; }
                }
            } else if (solid && !solid((int)std::floor(d.pos.x), (int)std::floor(d.pos.y - DROP_RADIUS - 0.05f),
                                       (int)std::floor(d.pos.z))) {
                d.resting = false;                                 // ground taken away
            }
            i++;
        }
        lastPickupAge += dt;
    }

    void tickParticles(float dt, const SolidFn& solid) {
        for (size_t i = 0; i < particles.size();) {
            Particle& p = particles[i];
            p.life -= dt;
            if (p.life <= 0.0f) { particles[i] = particles.back(); particles.pop_back(); continue; }
            p.v.y -= (p.drag > 0.0f ? 18.0f : 60.0f) * dt;
            p.v *= std::exp(-p.drag * dt);
            if (p.drag > 0.0f)                                     // leaves flutter
                p.v.x += std::sin(p.life * 7.0f + p.spin) * 6.0f * dt;
            glm::vec3 next = p.p + p.v * dt;
            if (solid && solid((int)std::floor(next.x), (int)std::floor(next.y), (int)std::floor(next.z))) {
                p.v = glm::vec3(0);                                // landed
            } else p.p = next;
            i++;
        }
    }
};
