#pragma once
// ══════════════════════════════════════════════════════════
// A dungeon coming alive (docs/dungeons.md, step 3)
// ══════════════════════════════════════════════════════════
// The markers in a Dungeon world's tiles, turned into the real thing
// as players get to them. Run by the game (or the server: whoever
// owns the world), never by a client.
//
//   spawner  its NPCs appear when its rule is met (once):
//              enter   a player steps into its tile
//              sight   a player comes within SIGHT voxels on its level
//              boss    the boss's arena is entered (with the boss)
//              wave    (for now) as enter
//            spawn = an npcs.toml type, count = "lo hi"
//   coffin   one of the dead bursts out when a player passes within
//            COFFIN_NEAR in its tile (rule boss: with the boss)
//   boss     the boss (spawn = its type), when its arena is entered.
//            While it lives and someone is in the arena, its doorways
//            are barred (iron bars; lifted if everyone's gone out, by
//            dying, for ARENA_LET_GO s). Coffins with rule "phase"
//            burst in its second phase (npcs.toml phase). When it
//            dies the dungeon is cleared: the bars lift for good, the
//            exit marker lights up (the way out: the portals, step 6)
//   chest    a wooden chest, put down once its ground is loaded
//            (empty for now: loot later), facing into the room
//   secret   every marker_secret block of a tile (cracked stone in
//            the world) is a wall that breaks: hit it with an attack
//            (BREAK_HITS lights, a heavy counts double) and it
//            crumbles, opening the way. A hit knocks bits out of it
//   portal   a gate (game/Gate.h), always open: the way home (the
//            game takes whoever walks into it back to the realm's
//            main world: main.cpp, world/Realm.h)
//   exit     a second gate, built open once the boss falls
//
// What has happened is kept in dungeon.dat in the world's folder, so
// a cleared room stays cleared.

#include "../worldgen/DungeonGenerator.h"
#include "../sim/Simulation.h"
#include "../world/StructureManager.h"
#include "../world/Chests.h"
#include "../world/WorldEditor.h"
#include "../game/Gate.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace dungeon {

class Run {
public:
    static constexpr float SIGHT       = 56.0f;
    static constexpr float COFFIN_NEAR = 22.0f;
    static constexpr int   BREAK_HITS  = 4;
    static constexpr float ARENA_LET_GO = 2.0f;    // s with nobody alive in the arena before its bars lift
    static constexpr float BOSS_LOST    = 5.0f;    // s with no boss at all (not even its body) before it may rise again

    struct Spot {
        int         tile = -1;
        Marker      m;
        glm::ivec3  world{0};       // the marker's voxel
        bool        done = false;   // spawned / placed
    };
    struct SecretWall {
        int tile = -1;
        std::vector<glm::ivec3> blocks;   // world voxels still standing
        glm::vec3 centre{0.0f};
        int  hits = 0;
        bool broken = false;
    };
    // Something to show (the game turns these into particles, sounds, a line)
    struct Effect { glm::vec3 at; std::string particle; float size = 1.0f; std::string line; };

    explicit Run(const DungeonGenerator& g) : gen(g) {
        const Layout& L = gen.dungeonLayout();
        for (size_t i = 0; i < L.tiles.size(); i++) {
            const PlacedTile& t = L.tiles[i];
            const TileVariant& v = L.variantOf(t);
            glm::ivec3 lo = gen.worldOf(t);
            for (const Marker& m : v.markers) spots.push_back({ (int)i, m, lo + m.at, false });
            SecretWall w; w.tile = (int)i;
            for (int y = 0; y < v.vol.size.y; y++) for (int z = 0; z < v.vol.size.z; z++) for (int x = 0; x < v.vol.size.x; x++)
                if (markerKindOf(v.vol.at(x, y, z)) == "secret") w.blocks.push_back(lo + glm::ivec3(x, y, z));
            if (!w.blocks.empty()) {
                for (auto& b : w.blocks) w.centre += glm::vec3(b);
                w.centre /= (float)w.blocks.size();
                walls.push_back(w);
            }
            // the arena's doorways: bars across them, every other column,
            // the whole height, in the wall's outer layer
            if ((int)i == L.boss)
                for (const Socket& sk : v.sockets) {
                    if (sk.type != "door") continue;
                    glm::ivec3 o = sk.cell * HALF;
                    int y0 = o.y + FLOOR, y1 = y0 + DOOR_H;
                    int a0 = (HALF - DOOR_W) / 2, a1 = a0 + DOOR_W;
                    glm::ivec3 s = v.vol.size;
                    for (int y = y0; y < y1; y++)
                        for (int a = a0; a < a1; a++) {
                            bool bar = (a - a0) % 2 == 0 || y == y0 + DOOR_H / 2 || y == y1 - 1;   // uprights, a crossbar, the top
                            if (!bar) continue;
                            glm::ivec3 c;
                            switch (sk.face) {
                            case North: c = { o.x + a, y, 1 }; break;
                            case South: c = { o.x + a, y, s.z - 2 }; break;
                            case West:  c = { 1, y, o.z + a }; break;
                            case East:  c = { s.x - 2, y, o.z + a }; break;
                            default: continue;
                            }
                            bars.push_back(lo + c);
                        }
                }
        }
        for (const Spot& s : spots) {
            if (s.m.kind == "exit") { exitAt = glm::vec3(s.world) + glm::vec3(0.5f, 0.0f, 0.5f); hasExit = true; exitGate.base = s.world; }
            if (s.m.kind == "portal") { entry.base = s.world; hasEntry = true; }
        }
        // each gate faces into its room (its arrival side toward the middle)
        for (Gate* g : { &entry, &exitGate }) {
            int t = tileAt(glm::vec3(g->base) + glm::vec3(0.5f, 1.0f, 0.5f));
            if (t < 0) continue;
            glm::vec3 c = roomCentre(t);
            g->side = c.z >= (float)g->base.z + 0.5f ? 1 : -1;
        }
    }
    // The gates: the one in the portal room (always), the exit once
    // cleared. Built once their ground is loaded
    bool hasEntryGate() const { return hasEntry; }
    const Gate& entryGate() const { return entry; }
    bool exitOpen() const { return hasExit && cleared; }
    const Gate& exitGateRef() const { return exitGate; }

    // ── Saving: what has happened ─────────────────────────
    void load(const std::string& folder) {
        std::ifstream f(folder + "/dungeon.dat");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string kind; int i;
            if (!(ls >> kind >> i)) continue;
            if (kind == "spot" && i >= 0 && i < (int)spots.size()) spots[(size_t)i].done = true;
            if (kind == "wall" && i >= 0 && i < (int)walls.size()) { walls[(size_t)i].broken = true; walls[(size_t)i].blocks.clear(); }
            if (kind == "entered") entered.insert(i);
            if (kind == "cleared") cleared = true;
        }
    }
    bool save(const std::string& folder) const {
        std::ofstream f(folder + "/dungeon.dat");
        f << "# what has happened in this dungeon (dungeon/Run.h): spots spawned or placed, walls broken, tiles entered\n";
        for (size_t i = 0; i < spots.size(); i++) if (spots[i].done) f << "spot " << i << "\n";
        for (size_t i = 0; i < walls.size(); i++) if (walls[i].broken) f << "wall " << i << "\n";
        for (int t : entered) f << "entered " << t << "\n";
        if (cleared) f << "cleared 1\n";
        return (bool)f;
    }

    // ── Every tick ────────────────────────────────────────
    std::vector<Effect> tick(const std::vector<Human*>& players, Simulation& sim, ChunkManager& world,
                             StructureManager& structs, ChestStore& chests, WorldEditor& editor, float dt = 1.0f / 60.0f) {
        std::vector<Effect> fx;
        const Layout& L = gen.dungeonLayout();
        // which tiles players are in
        std::vector<int> inTile;
        for (Human* p : players) {
            if (!p || p->dead()) continue;
            int t = tileAt(p->position);
            if (t >= 0) {
                inTile.push_back(t);
                if (!entered.count(t)) {
                    entered.insert(t);
                    if (t == L.boss) fx.push_back({ p->position, "", 1.0f, "Something stirs in the dark..." });
                }
            }
        }
        bool bossOn = L.boss >= 0 && entered.count(L.boss);
        // ── the boss: alive? in its second phase? gone? ──
        Npc* boss = nullptr;
        bool anyBossBody = false;
        if (L.boss >= 0)
            for (auto& n : sim.npcs().all())
                if (n->type && n->type->boss && tileAt(n->position) == L.boss) {
                    anyBossBody = true;
                    if (!n->dead()) { boss = n.get(); break; }
                }
        const bool bossSpawned = bossSpot() && bossSpot()->done;
        bool phaseNow = false;
        if (boss) {
            bossSeen = true;
            bossName = boss->name;
            if (boss->phaseTwo && !phaseTold) { phaseTold = true; fx.push_back({ boss->position + glm::vec3(0, boss->height, 0), "death_dust", 2.5f, "The " + boss->name + " roars! The dead answer..." }); }
            phaseNow = boss->phaseTwo;
        }
        if (bossSpawned && !cleared && !boss) {
            if (bossSeen && anyBossBody) {
                // it fell: the dungeon is cleared
                cleared = true;
                fx.push_back({ exitAt + glm::vec3(0, 4, 0), "perfect_dodge", 3.0f, "The " + (bossName.empty() ? std::string("boss") : bossName) + " falls. The way out opens." });
            } else if (!anyBossBody) {
                // no boss at all (lost in a save): it may rise again
                bossLost += dt;
                if (bossLost > BOSS_LOST) { if (Spot* b = bossSpot()) b->done = false; bossLost = 0.0f; bossSeen = false; }
            }
        } else bossLost = 0.0f;

        for (size_t i = 0; i < spots.size(); i++) {
            Spot& s = spots[i];
            if (s.done) continue;
            const std::string& k = s.m.kind;
            if (k == "chest") { placeChest(s, world, structs, chests); continue; }
            if (k != "spawner" && k != "coffin" && k != "boss") continue;
            std::string rule = s.m.prop("rule", k == "boss" ? "boss" : "enter");
            bool go = false;
            if (rule == "boss") go = bossOn && !cleared && (k == "boss" || std::find(inTile.begin(), inTile.end(), s.tile) != inTile.end());
            else if (rule == "phase") go = phaseNow && !cleared;
            else if (k == "coffin") {
                for (Human* p : players)
                    if (p && !p->dead() && tileAt(p->position) == s.tile && glm::length(p->position - glm::vec3(s.world)) < COFFIN_NEAR) go = true;
            } else if (rule == "sight") {
                for (Human* p : players)
                    if (p && !p->dead() && std::fabs(p->position.y - s.world.y) < 24.0f && glm::length(p->position - glm::vec3(s.world)) < SIGHT) go = true;
            } else go = std::find(inTile.begin(), inTile.end(), s.tile) != inTile.end();
            if (!go) continue;
            s.done = true;
            int n = 1;
            if (k == "spawner") {
                int lo = 1, hi = 1;
                std::sscanf(s.m.prop("count", "1 1").c_str(), "%d %d", &lo, &hi);
                hi = std::max(lo, hi);
                n = lo + (int)(rng() % (uint32_t)(hi - lo + 1));
            }
            std::string type = s.m.prop("spawn", k == "boss" ? "crypt_lord" : "undead");
            if (!gNpcTypes.find(type)) type = "undead";
            glm::vec3 base = glm::vec3(s.world) + glm::vec3(0.5f, 0.0f, 0.5f);
            glm::vec3 room = roomCentre(s.tile);
            for (int j = 0; j < n; j++) {
                glm::vec3 at = base;
                if (k == "coffin") at += glm::normalize(glm::vec3(room.x - base.x, 0.0f, room.z - base.z) + glm::vec3(1e-3f)) * 3.0f;
                else if (n > 1) {
                    float a = (float)j / n * 6.2831853f + (float)(rng() % 100) * 0.01f;
                    at += glm::vec3(std::cos(a), 0.0f, std::sin(a)) * (4.0f + (float)(rng() % 5));
                }
                glm::vec3 face = room - at; face.y = 0.0f;
                face = glm::length(face) > 1e-3f ? glm::normalize(face) : glm::vec3(0, 0, 1);
                if (Npc* npc = sim.npcs().spawn(type, at, face)) {
                    (void)npc;
                    fx.push_back({ at + glm::vec3(0, 3, 0), k == "coffin" ? "wall_dust" : "death_dust", k == "boss" ? 2.0f : 1.0f, "" });
                }
            }
            if (k == "boss") fx.push_back({ base, "", 1.0f, "The " + gNpcTypes.find(type)->name + " rises!" });
        }
        // ── the arena's bars (after spawning: down as the boss rises) ──
        if (!boss && !cleared && L.boss >= 0)
            for (auto& n : sim.npcs().all())
                if (n->type && n->type->boss && !n->dead() && tileAt(n->position) == L.boss) { boss = n.get(); break; }
        bool someoneIn = std::find(inTile.begin(), inTile.end(), L.boss) != inTile.end();
        arenaEmpty = someoneIn ? 0.0f : arenaEmpty + dt;
        bool wantBars = boss && !cleared && (someoneIn || arenaEmpty < ARENA_LET_GO);
        if (wantBars != barred && !bars.empty()) setBars(wantBars, world, editor, fx);
        // the gates: the portal room's, and the exit's once cleared
        if (hasEntry && entry.loaded(world) && !entry.isBuilt(world)) {
            EditBatch b; b.source = EditSource::Script; b.label = "gate"; b.undoable = false;
            entry.build(b, world, true, 1);
            editor.submit(std::move(b));
        }
        if (hasExit && cleared && exitGate.loaded(world) && !exitGate.isBuilt(world)) {
            EditBatch b; b.source = EditSource::Script; b.label = "exit gate"; b.undoable = false;
            exitGate.build(b, world, true, 1);
            editor.submit(std::move(b));
        }
        // the way out, once cleared: a glow over the exit
        if (cleared && hasExit) {
            exitGlow -= dt;
            if (exitGlow <= 0.0f) { exitGlow = 0.5f; fx.push_back({ exitAt + glm::vec3(0, 2, 0), "perfect_dodge", 1.2f, "" }); }
        }
        // secret walls: an attack in its hit window, close and facing one
        for (Human* p : players) {
            if (!p || p->dead()) continue;
            const AttackState& a = p->attack;
            bool live = a.active() && a.t >= a.def().activeStart && a.t <= a.def().activeEnd && a.def().effect.empty();
            int key = (int)(p->uuid & 0x7fffffff);
            bool was = swinging.count(key) > 0;
            if (live && !was) {
                swinging.insert(key);
                hitWall(*p, a, editor, fx);
            } else if (!live && was) swinging.erase(key);
        }
        return fx;
    }

    // The tile a point is in (-1: none)
    int tileAt(glm::vec3 p) const {
        glm::ivec3 c = glm::ivec3(glm::floor((p - glm::vec3(gen.gridOrigin())) / (float)HALF));
        return gen.dungeonLayout().tileAt(c);
    }
    const std::vector<Spot>& allSpots() const { return spots; }
    const std::vector<SecretWall>& allWalls() const { return walls; }
    bool wasEntered(int t) const { return entered.count(t) > 0; }
    bool isCleared() const { return cleared; }
    bool isBarred() const { return barred; }
    const std::vector<glm::ivec3>& barVoxels() const { return bars; }
    glm::vec3 exitPoint() const { return exitAt; }

private:
    const DungeonGenerator& gen;
    std::vector<Spot> spots;
    std::vector<SecretWall> walls;
    std::set<int> entered;
    std::set<int> swinging;
    // the boss and its arena
    std::vector<glm::ivec3> bars;       // where the bars go across the arena's doorways
    bool  barred = false, cleared = false, bossSeen = false, phaseTold = false, hasExit = false;
    float arenaEmpty = 99.0f, bossLost = 0.0f, exitGlow = 0.0f;
    std::string bossName;
    glm::vec3 exitAt{0.0f};
    Gate  entry, exitGate;
    bool  hasEntry = false;
    Spot* bossSpot() { for (Spot& s : spots) if (s.m.kind == "boss") return &s; return nullptr; }
    void setBars(bool on, ChunkManager& world, WorldEditor& editor, std::vector<Effect>& fx) {
        static const uint16_t IRON = gRegistry.getIDByName("iron_block");
        EditBatch b;
        b.source = EditSource::Script;
        b.label = on ? "arena barred" : "arena opened";
        glm::vec3 mid(0.0f);
        for (const glm::ivec3& v : bars) {
            uint16_t now = world.typeAtVoxel(v.x, v.y, v.z);
            if (on && now == BLOCK_AIR) b.set(v.x, v.y, v.z, IRON);
            if (!on && now == IRON) b.set(v.x, v.y, v.z, BLOCK_AIR);
            mid += glm::vec3(v);
        }
        mid /= (float)bars.size();
        editor.submit(std::move(b));
        barred = on;
        fx.push_back({ mid + glm::vec3(0.5f), "wall_dust", 2.0f, on ? "Bars slam down behind you!" : "" });
    }
    std::mt19937 rng{ 12345u };

    glm::vec3 roomCentre(int tile) const {
        const Layout& L = gen.dungeonLayout();
        const PlacedTile& t = L.tiles[(size_t)tile];
        return glm::vec3(gen.worldOf(t)) + glm::vec3(L.variantOf(t).vol.size) * glm::vec3(0.5f, 0.0f, 0.5f);
    }

    void placeChest(Spot& s, ChunkManager& world, StructureManager& structs, ChestStore& chests) {
        if (!world.isColumnLoaded(s.world.x, s.world.z)) return;
        uint16_t type = gStructures.find("wooden_chest");
        if (!type) { s.done = true; return; }
        // facing into the room: the way from the chest to its tile's middle
        glm::vec3 to = roomCentre(s.tile) - glm::vec3(s.world);
        int rot = std::fabs(to.x) > std::fabs(to.z) ? (to.x > 0 ? 1 : 3) : (to.z > 0 ? 0 : 2);
        s.done = true;
        uint32_t sup = 0;
        std::string why = structs.checkPlace(type, s.world, rot, false, &sup);
        if (!why.empty()) {
            // turned the other ways, in case that one doesn't fit
            for (int k = 1; k < 4 && !why.empty(); k++) { int r = (rot + k) & 3; why = structs.checkPlace(type, s.world, r, false, &sup); if (why.empty()) rot = r; }
            if (!why.empty()) { std::cerr << "dungeon: a chest at " << s.world.x << " " << s.world.y << " " << s.world.z << " doesn't fit: " << why << "\n"; return; }
        }
        if (StructureInstance* si = structs.place(type, s.world, rot, EditSource::Script, sup)) {
            const StructureType* t = gStructures.get(type);
            if (t && ChestStore::holds(t)) chests.get(si->id, *t).makeEmpty();
        }
    }

    void hitWall(const Human& p, const AttackState& a, WorldEditor& editor, std::vector<Effect>& fx) {
        glm::vec3 chest = p.position + glm::vec3(0.0f, p.height * 0.6f, 0.0f);
        glm::vec3 f(p.facing.x, 0.0f, p.facing.z);
        f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
        float reach = a.def().reach + 4.0f;
        for (size_t i = 0; i < walls.size(); i++) {
            SecretWall& w = walls[i];
            if (w.broken) continue;
            // the nearest of its blocks in front
            float best = 1e9f;
            glm::vec3 at(0.0f);
            for (const glm::ivec3& b : w.blocks) {
                glm::vec3 c = glm::vec3(b) + glm::vec3(0.5f);
                glm::vec3 d = c - chest;
                float dist = glm::length(d);
                if (dist > reach || dist < 1e-3f) continue;
                if (glm::dot(glm::normalize(glm::vec3(d.x, 0.0f, d.z) + glm::vec3(1e-4f)), f) < 0.4f) continue;
                if (dist < best) { best = dist; at = c; }
            }
            if (best > reach) continue;
            bool heavy = a.kind == AttackKind::Heavy || a.kind == AttackKind::AirHeavy;
            w.hits += heavy ? 2 : 1;
            EditBatch b;
            b.source = EditSource::Script;
            if (w.hits >= BREAK_HITS) {
                // it crumbles
                w.broken = true;
                for (const glm::ivec3& v : w.blocks) b.set(v.x, v.y, v.z, BLOCK_AIR);
                w.blocks.clear();
                b.label = "secret wall";
                editor.submit(std::move(b));
                fx.push_back({ w.centre, "wall_dust", 3.0f, "The wall crumbles!" });
            } else {
                // bits knocked out of it round where it was hit
                std::vector<glm::ivec3> keep;
                for (const glm::ivec3& v : w.blocks) {
                    bool near = glm::length(glm::vec3(v) + glm::vec3(0.5f) - at) < 3.5f;
                    if (near && rng() % 100 < 45) b.set(v.x, v.y, v.z, BLOCK_AIR);
                    else keep.push_back(v);
                }
                w.blocks.swap(keep);
                b.label = "secret wall hit";
                editor.submit(std::move(b));
                fx.push_back({ at, "wall_dust", 0.8f, "" });
            }
            return;                                         // one wall a swing
        }
    }
};

} // namespace dungeon
