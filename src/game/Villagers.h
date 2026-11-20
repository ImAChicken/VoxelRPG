#pragma once
#include "Village.h"
#include "../npc/NpcManager.h"
#include "../npc/Pathfinder.h"
#include "../physics/VoxelPhysics.h"
#include "../npc/VillageLife.h"
#include "../world/StructureManager.h"
#include "../world/TerrainOps.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Villagers — the people a village is built with
// ══════════════════════════════════════════════════════════
// After village::build, populate() finds the village's beds,
// workplaces and haunts in the buildings it made and spawns a
// villager per sleeper (data/registry/villagers.toml): each gets a
// name, a job (a workplace) or none, and its places as a
// VillageLife, which the "routine" state (npc/Routine.h) lives by.
struct VillagerJob {
    std::string id, title, building;   // building: the blueprint tag of its workplace
};

class VillagerRegistry {
public:
    std::string npcType = "villager";
    std::string guardType = "guard";       // npcs.toml type of the village's guards
    int villagersPerGuard = 3;             // a guard for every this many villagers (at least 1, at most maxGuards)
    int maxGuards = 3;
    std::vector<std::string> names, hauntTags = { "plaza", "church", "hall" };
    std::vector<VillagerJob> jobs;

    bool load(const std::string& path = "data/registry/villagers.toml") {
        names.clear(); jobs.clear();
        try {
            toml::table t = toml::parse_file(path);
            npcType = t["npc_type"].value_or(std::string("villager"));
            guardType = t["guard_type"].value_or(std::string("guard"));
            villagersPerGuard = std::max(1, (int)t["villagers_per_guard"].value_or((int64_t)3));
            maxGuards = std::max(0, (int)t["max_guards"].value_or((int64_t)3));
            if (auto* a = t["names"].as_array()) for (auto& n : *a) if (auto v = n.value<std::string>()) names.push_back(*v);
            if (auto* a = t["haunt_tags"].as_array()) {
                hauntTags.clear();
                for (auto& n : *a) if (auto v = n.value<std::string>()) hauntTags.push_back(*v);
            }
            if (auto* a = t["job"].as_array())
                for (auto& e : *a)
                    if (auto* j = e.as_table())
                        jobs.push_back({ (*j)["id"].value_or(std::string()), (*j)["title"].value_or(std::string()),
                                         (*j)["building"].value_or(std::string()) });
        } catch (const std::exception&) { return false; }
        if (names.empty()) names = { "Villager" };
        return true;
    }
};
inline VillagerRegistry gVillagers;

namespace villagers {

// Room to stand, as the pathfinder sees it (npc/Pathfinder.h
// standAt): solid under the feet, a body's height of air over the
// middle, and out to 2 voxels round it above a low step
inline bool standable(const ChunkManager& w, int x, int y, int z) {
    auto solid = [&](int px, int py, int pz) {
        uint16_t id = w.typeAtVoxel(px, py, pz);
        return id != BLOCK_AIR && id != BLOCK_WATER && gRegistry.get(id).isSolid() && !gRegistry.get(id).sprite;
    };
    if (!solid(x, y - 1, z)) return false;
    for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dz * dz > 5) continue;
            bool near = std::abs(dx) <= 1 && std::abs(dz) <= 1;
            for (int dy = near ? 0 : 4; dy < 18; dy++)
                if (solid(x + dx, y + dy, z + dz)) return false;
        }
    return true;
}
// The nearest spot with room to stand, on the same floor (or a
// step up or down), within 'r'; the spot itself if none
inline glm::vec3 roomToStand(const ChunkManager& w, glm::vec3 p, int r = 8) {
    int x0 = (int)std::floor(p.x), z0 = (int)std::floor(p.z), y0 = (int)std::lround(p.y);
    for (int d = 0; d <= r; d++)
        for (int dz = -d; dz <= d; dz++)
            for (int dx = -d; dx <= d; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
                for (int dy : { 0, 1, -1, 2, -2 })
                    if (standable(w, x0 + dx, y0 + dy, z0 + dz)) return glm::vec3(x0 + dx + 0.5f, (float)(y0 + dy), z0 + dz + 0.5f);
            }
    return p;
}

// Where a lot's door is and the spots either side of it
struct Doorway {
    uint32_t  door = 0;
    glm::vec3 out{ 0.0f }, in{ 0.0f };
    glm::ivec2 front{ 0, 1 };
};

inline bool doorwayOf(const village::Lot& l, const std::vector<uint32_t>& made, const StructureManager& sm,
                      const ChunkManager& w, Doorway& out) {
    const StructureInstance* best = nullptr;
    for (uint32_t id : made) {
        const StructureInstance* s = sm.get(id);
        const StructureType* t = s ? sm.typeOf(*s) : nullptr;
        if (t && t->kind == StructureKind::Door && (!best || s->origin.y < best->origin.y)) best = s;
    }
    if (!best) return false;
    static const glm::ivec2 dirs[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    glm::ivec2 f = dirs[l.bp->frontTurned(l.rot)];
    glm::ivec3 lo, hi;
    sm.bounds(*best, lo, hi);
    glm::ivec2 c((lo.x + hi.x + 1) / 2, (lo.z + hi.z + 1) / 2);
    // both spots clear of the door's swing (it sweeps as far as the
    // door is wide), so shutting it never sweeps the body standing there
    int reach = std::max(hi.x - lo.x, hi.z - lo.z) + 4;
    // which way is out (village::doorFacing)
    f = village::doorFacing(w, c, reach, lo.y + 10, f);
    // outside: past the door's swing (out of the lot, for a front
    // door), on clear ground — not by a forge or up on an anvil
    int edge = f.x ? (f.x > 0 ? hi.x : lo.x) : (f.y > 0 ? hi.z : lo.z);
    glm::ivec2 o = f.x ? glm::ivec2(edge, c.y) : glm::ivec2(c.x, edge);
    o += f * 3;
    bool front = f == dirs[l.bp->frontTurned(l.rot)];
    if (front) {
        glm::ivec2 e = c;
        for (int k = 0; k < 60 && e.x >= l.lo.x && e.x < l.lo.x + l.size.x && e.y >= l.lo.z && e.y < l.lo.z + l.size.z; k++) e += f;
        e += f * 3;
        if (glm::dot(glm::vec2(e - o), glm::vec2(f)) > 0.0f) o = e;
    }
    glm::ivec2 side(-f.y, f.x);
    glm::ivec2 pick = o;
    int g = -1;
    for (int k = 0; k <= 10 && g < 0; k++)
        for (int lat : { 0, 3, -3, 6, -6 }) {
            glm::ivec2 q = o + f * k + side * lat;
            int gq = terrain::groundLevel(w, q.x, q.y, l.floor + 2, 120);
            if (gq < 0 || !standable(w, q.x, gq, q.y)) continue;
            pick = q; g = gq;
            break;
        }
    if (g < 0) g = terrain::groundLevel(w, o.x, o.y, l.floor + 2, 120);
    out.door = best->id;
    out.out = glm::vec3(pick.x + 0.5f, (float)(g < 0 ? l.floor : g), pick.y + 0.5f);
    glm::ivec2 i = c - f * std::max(6, reach);
    out.in = roomToStand(w, glm::vec3(i.x + 0.5f, (float)l.floor, i.y + 0.5f));
    out.out = roomToStand(w, out.out);
    out.front = f;
    return true;
}

// The middle of the lot, at its floor
inline glm::vec3 middleOf(const village::Lot& l) {
    return glm::vec3(l.lo.x + l.size.x * 0.5f, (float)l.floor, l.lo.z + l.size.z * 0.5f);
}

// Where to stand to work: in front of the lot's work station (a
// forge, a workbench … not the hearth) or a piece tagged
// "workspot" (an altar), facing it; else further in from the door
// The first of these spots (in order of liking) a body can walk to
// from 'from' (the inside of the door), given room to stand; 'fallback'
// if none
inline glm::vec3 firstReachable(const nav::Pathfinder* pf, const ChunkManager& w, glm::vec3 from,
                                const std::vector<glm::vec3>& spots, glm::vec3 fallback) {
    if (!pf) return roomToStand(w, fallback);
    for (const glm::vec3& c : spots) {
        glm::vec3 s = roomToStand(w, c, 3);
        if (!standable(w, (int)std::floor(s.x), (int)std::lround(s.y), (int)std::floor(s.z))) continue;
        if (pf->find(from, s).reached) return s;
    }
    return roomToStand(w, fallback);
}
// Spots around a box, nearest the given way first
inline std::vector<glm::vec3> spotsAround(glm::vec3 c, float r, glm::vec2 first, float y) {
    std::vector<glm::vec3> out;
    float a0 = std::atan2(first.y, first.x);
    for (float extra : { 3.0f, 4.5f, 6.0f })
        for (int k : { 0, 1, -1, 2, -2, 3, -3, 4 }) {
            float a = a0 + k * 0.7854f;
            out.push_back(glm::vec3(c.x + std::cos(a) * (r + extra), y, c.z + std::sin(a) * (r + extra)));
        }
    return out;
}

inline LifePlace workPlace(const village::Lot& l, const std::vector<uint32_t>& made, const StructureManager& sm,
                           const ChunkManager& w, const Doorway& d, const nav::Pathfinder* pf = nullptr) {
    LifePlace p;
    p.door = d.door; p.out = d.out; p.in = d.in;
    p.spot = d.in - glm::vec3(d.front.x, 0, d.front.y) * 6.0f;
    p.facing = -glm::vec3(d.front.x, 0, d.front.y);
    for (uint32_t id : made) {
        const StructureInstance* s = sm.get(id);
        const StructureType* t = s ? sm.typeOf(*s) : nullptr;
        bool station = t && t->kind == StructureKind::Station && t->station != "hearth" && t->station != "campfire";
        if (!t || !(station || t->hasTag("workspot"))) continue;
        glm::ivec3 lo, hi;
        sm.bounds(*s, lo, hi);
        glm::vec3 c = glm::vec3(lo + hi + 1) * 0.5f;
        glm::vec2 v(d.in.x - c.x, d.in.z - c.z);
        v = glm::length(v) > 0.1f ? glm::normalize(v) : glm::vec2(d.front);
        float r = 0.5f * std::max(hi.x - lo.x + 1, hi.z - lo.z + 1);
        p.spot = glm::vec3(c.x + v.x * (r + 3.0f), (float)lo.y, c.z + v.y * (r + 3.0f));
        p.spot = firstReachable(pf, w, d.in, spotsAround(c, r, v, (float)lo.y), p.spot);
        glm::vec2 look(c.x - p.spot.x, c.z - p.spot.z);
        p.facing = glm::length(look) > 0.1f ? glm::vec3(glm::normalize(look).x, 0, glm::normalize(look).y) : -glm::vec3(v.x, 0, v.y);
        p.valid = true;
        return p;
    }
    p.spot = firstReachable(pf, w, d.in, { p.spot }, p.spot);
    p.valid = true;
    return p;
}

// The sleeping places in a lot's beds (two in a double bed): where
// to stand beside it, and where the feet go lying down (its head on
// the pillow: StructureManager::outsideDir turned the unturned +z
// headboard). heightVoxels: the sleeper's height
struct Sleeper { LifePlace place; uint32_t bed = 0; int side = 0; };
inline std::vector<Sleeper> bedsIn(const village::Lot& l, const std::vector<uint32_t>& made, const StructureManager& sm,
                                   const ChunkManager& w, const Doorway& d, const nav::Pathfinder* pf = nullptr) {
    std::vector<Sleeper> out;
    glm::vec3 mid = middleOf(l);
    for (uint32_t id : made) {
        const StructureInstance* s = sm.get(id);
        const StructureType* t = s ? sm.typeOf(*s) : nullptr;
        if (!t || !t->hasTag("bed")) continue;
        glm::ivec3 lo, hi;
        sm.bounds(*s, lo, hi);
        glm::ivec2 o = StructureManager::outsideDir(s->rot);
        glm::vec2 head(-o.x, -o.y);                          // toward the headboard
        glm::vec2 side(-head.y, head.x);
        float width = std::abs(side.x) > 0.5f ? (float)(hi.x - lo.x + 1) : (float)(hi.z - lo.z + 1);
        glm::vec3 c = glm::vec3(lo + hi + 1) * 0.5f;
        int sleepers = width >= 11.0f ? 2 : 1;
        // stand on the side toward the room's middle
        glm::vec2 toMid(mid.x - c.x, mid.z - c.z);
        float sgn = glm::dot(toMid, side) >= 0.0f ? 1.0f : -1.0f;
        for (int k = 0; k < sleepers; k++) {
            Sleeper sl;
            sl.bed = id; sl.side = k;
            LifePlace& p = sl.place;
            p.door = d.door; p.out = d.out; p.in = d.in;
            float lateral = sleepers == 2 ? (k == 0 ? -width * 0.25f : width * 0.25f) : 0.0f;
            // beside it on the side toward the room's middle, else the
            // other side, else the foot: the first a body can walk to
            float halfLen = std::abs(head.x) > 0.5f ? (hi.x - lo.x + 1) * 0.5f : (hi.z - lo.z + 1) * 0.5f;
            std::vector<glm::vec3> cands;
            for (float s2 : { sgn, -sgn })
                for (float extra : { 3.0f, 4.5f, 6.0f })
                    for (float along : { 0.0f, -halfLen * 0.5f, halfLen * 0.5f }) {
                        glm::vec2 q = glm::vec2(c.x, c.z) + side * (s2 * (width * 0.5f + extra)) + head * along;
                        cands.push_back(glm::vec3(q.x, (float)lo.y, q.y));
                    }
            for (float extra : { 3.0f, 4.5f, 6.0f }) {
                glm::vec2 q = glm::vec2(c.x, c.z) - head * (halfLen + extra);
                cands.push_back(glm::vec3(q.x, (float)lo.y, q.y));
            }
            p.spot = firstReachable(pf, w, d.in, cands, cands[0]);   // on the bed's own floor (upstairs too)
            p.facing = glm::vec3(-head.x, 0, -head.y);       // lying: toward its feet
            p.lie = true;
            // the headboard's inner face, then down the bed
            float half = std::abs(head.x) > 0.5f ? (hi.x - lo.x + 1) * 0.5f : (hi.z - lo.z + 1) * 0.5f;
            glm::vec2 headIn = glm::vec2(c.x, c.z) + head * (half - 1.0f) + side * lateral;
            p.lieAt = glm::vec3(headIn.x, (float)lo.y + 4.0f, headIn.y);   // finished per sleeper (its height)
            p.valid = true;
            out.push_back(sl);
        }
    }
    return out;
}

// Where the feet go for a body of this height lying with its head at
// the headboard: the body pivots about its hips (0.35 of its height
// up), so the feet sit that far below the mattress and the hips that
// far from the head end
inline glm::vec3 lieFeet(const LifePlace& p, int heightVoxels) {
    float H = (float)heightVoxels, mid = 0.35f * H;
    glm::vec3 toFeet(p.facing.x, 0, p.facing.z);
    glm::vec3 hips = p.lieAt + toFeet * (H - mid + 0.5f);
    return glm::vec3(hips.x, p.lieAt.y + 1.2f - mid, hips.z);
}

// The way from outside a lot's door to the square: down its path to
// the road, along the road to where it starts (a lane: on along the
// road it leaves)
inline std::vector<glm::vec3> wayToSquare(const village::Plan& p, size_t lot, const ChunkManager& w) {
    std::vector<glm::vec3> out;
    if (lot >= p.pathOf.size() || p.pathOf[lot] < 0) return out;
    int pi = p.pathOf[lot];
    int ri = pi < (int)p.pathRoad.size() ? p.pathRoad[(size_t)pi] : -1;
    auto at = [&](glm::ivec2 c) {
        int g = terrain::groundLevel(w, c.x, c.y, p.center.y + 96, 192);
        return glm::vec3(c.x + 0.5f, (float)(g < 0 ? p.center.y : g), c.y + 0.5f);
    };
    // the nearest point on a road's middle line
    auto onRoad = [&](const village::Road& r, glm::ivec2 q) {
        glm::ivec2 lo = glm::min(r.a, r.b), hi = glm::max(r.a, r.b);
        return glm::clamp(q, lo, hi);
    };
    const village::Road& path = p.paths[(size_t)pi];
    out.push_back(at(path.b));
    if (ri < 0 || ri >= (int)p.roads.size()) return out;
    out.push_back(at(onRoad(p.roads[(size_t)ri], path.b)));
    float half = p.lots.empty() ? 16.0f : 0.5f * std::max(p.lots[0].size.x, p.lots[0].size.z);
    for (int guard = 0; guard < 4; guard++) {
        const village::Road& r = p.roads[(size_t)ri];
        out.push_back(at(r.a));
        if (glm::length(glm::vec2(r.a - glm::ivec2(p.center.x, p.center.z))) <= half + 4.0f) break;   // at the square
        int parent = -1;
        for (size_t j = 0; j < p.roads.size(); j++) {
            if ((int)j == ri) continue;
            glm::ivec2 q = onRoad(p.roads[j], r.a);
            if (glm::length(glm::vec2(q - r.a)) <= p.roads[j].width * 0.5f + 3.0f) { parent = (int)j; break; }
        }
        if (parent < 0) break;
        out.push_back(at(onRoad(p.roads[(size_t)parent], r.a)));
        ri = parent;
    }
    return out;
}

struct Populated;
inline void postGuards(const village::Plan& p, const ChunkManager& w, NpcManager& npcs, std::mt19937& rng, int& nameAt, Populated& out);

struct Populated {
    int villagers = 0, jobs = 0, homes = 0;
    std::vector<std::string> who;         // "Name the Title: bed x y z" (for the village command)
    int guards = 0;
};

// Spawn the village's people. made: village::Built::made
inline Populated populate(const village::Plan& p, const village::Built& b, const StructureManager& sm,
                          const ChunkManager& w, NpcManager& npcs, uint32_t seed) {
    Populated out;
    std::mt19937 rng(seed ^ 0x51ed27u);
    auto rnd = [&](int n) { return n > 0 ? (int)(rng() % (uint32_t)n) : 0; };
    if (gVillagers.names.empty()) gVillagers.load();

    // spots inside are ones a body can walk to from the door
    VoxelPhysics phys;
    phys.init(const_cast<ChunkManager&>(w));
    nav::Pathfinder pf(phys);

    // the places: homes (beds), workplaces, haunts
    struct Home { std::vector<Sleeper> beds; };
    std::vector<Home> homes;
    struct Work { LifePlace place; std::string tag; bool taken = false; };
    std::vector<Work> works;
    std::vector<LifePlace> haunts;
    for (size_t i = 0; i < p.lots.size() && i < b.made.size(); i++) {
        const village::Lot& l = p.lots[i];
        const std::vector<uint32_t>& made = b.made[i];
        if (made.empty()) continue;
        bool haunt = false;
        for (const std::string& t : gVillagers.hauntTags) haunt = haunt || l.bp->hasTag(t);
        Doorway d;
        bool hasDoor = doorwayOf(l, made, sm, w, d);
        if (haunt) {
            LifePlace h;
            glm::vec3 mid = middleOf(l);
            if (hasDoor) {
                h.door = d.door; h.out = d.out; h.in = d.in;
                h.spot = d.in - glm::vec3(d.front.x, 0, d.front.y) * 10.0f;
                h.facing = -glm::vec3(d.front.x, 0, d.front.y);
            } else { h.spot = mid; h.facing = glm::vec3(0, 0, 1); }
            h.spot = roomToStand(w, h.spot);
            h.toSquare = wayToSquare(p, i, w);
            h.valid = true;
            haunts.push_back(h);
        }
        if (!hasDoor) continue;
        std::vector<glm::vec3> way = wayToSquare(p, i, w);
        std::vector<Sleeper> beds = bedsIn(l, made, sm, w, d, &pf);
        for (Sleeper& sl : beds) sl.place.toSquare = way;
        if (!beds.empty()) { homes.push_back({ beds }); continue; }
        for (const VillagerJob& j : gVillagers.jobs)
            if (l.bp->hasTag(j.building)) {
                works.push_back({ workPlace(l, made, sm, w, d, &pf), j.building, false });
                works.back().place.toSquare = way;
                break;
            }
    }
    // the well: stand beside it
    for (const village::Decor& dc : p.decor) {
        if (dc.type != "well") continue;
        LifePlace h;
        int g = terrain::groundLevel(w, dc.lo.x - 3, dc.lo.y + 6, p.center.y + 96, 192);
        h.spot = glm::vec3(dc.lo.x - 3.5f, (float)(g < 0 ? p.center.y : g), dc.lo.y + 6.5f);
        h.facing = glm::vec3(1, 0, 0);
        h.valid = true;
        haunts.push_back(h);
    }

    // a villager per sleeper; jobs handed out in turn
    std::vector<int> jobOrder;
    for (size_t j = 0; j < gVillagers.jobs.size(); j++) jobOrder.push_back((int)j);
    int nameAt = rnd((int)gVillagers.names.size());
    for (const Home& h : homes) {
        out.homes++;
        for (const Sleeper& s : h.beds) {
            uint32_t look = (uint32_t)rng();
            Npc* n = npcs.spawn(gVillagers.npcType, s.place.spot, s.place.facing, look | 1u);
            if (!n) return out;
            VillageLife& life = n->life;
            life.on = true;
            life.bed = s.place;
            life.bed.lieAt = lieFeet(s.place, n->appearance.heightVoxels);
            life.lateBy = (rnd(100) - 50) / 66.0f;          // up to 3/4 hour early or late
            {
                int g = terrain::groundLevel(w, p.center.x, p.center.z, p.center.y + 96, 192);
                life.square = glm::vec3(p.center.x + 0.5f, (float)(g < 0 ? p.center.y : g), p.center.z + 0.5f);
            }
            // a job, if a workplace for one is free
            for (int j : jobOrder) {
                const VillagerJob& job = gVillagers.jobs[(size_t)j];
                auto it = std::find_if(works.begin(), works.end(), [&](const Work& wk) { return !wk.taken && wk.tag == job.building; });
                if (it == works.end()) continue;
                it->taken = true;
                life.job = job.id; life.title = job.title; life.work = it->place;
                out.jobs++;
                break;
            }
            std::rotate(jobOrder.begin(), jobOrder.begin() + (jobOrder.empty() ? 0 : 1), jobOrder.end());
            if (!life.work.valid) life.work = life.bed;     // keeps house
            life.haunts = haunts;
            const std::string& nm = gVillagers.names[(size_t)(nameAt++ % (int)gVillagers.names.size())];
            n->name = life.title.empty() ? nm : nm + " the " + life.title;
            n->home = life.bed.in;
            n->homeFacing = life.bed.facing;
            out.villagers++;
            char line[160];
            std::snprintf(line, sizeof line, "%s: bed %.0f %.0f %.0f", n->name.c_str(), life.bed.spot.x, life.bed.spot.y, life.bed.spot.z);
            out.who.push_back(line);
        }
    }
    postGuards(p, w, npcs, rng, nameAt, out);
    return out;
}

// The village's guards: one for every few villagers, each walking a
// round from the square out along a road to its end and back, then
// the next road. Their faction defends villagers (a hurt villager
// calls the guards near: NpcManager::callForHelp)
inline void postGuards(const village::Plan& p, const ChunkManager& w, NpcManager& npcs, std::mt19937& rng, int& nameAt, Populated& out) {
    if (out.villagers <= 0 || gVillagers.maxGuards <= 0 || !gNpcTypes.find(gVillagers.guardType) || p.roads.empty()) return;
    int count = std::clamp(out.villagers / gVillagers.villagersPerGuard, 1, gVillagers.maxGuards);
    auto ground = [&](glm::ivec2 c) {
        int g = terrain::groundLevel(w, c.x, c.y, p.center.y + 96, 192);
        return glm::vec3(c.x + 0.5f, (float)(g < 0 ? p.center.y : g), c.y + 0.5f);
    };
    glm::vec3 square = ground({ p.center.x, p.center.z });
    int mains = std::max(1, std::min(p.mainRoads, (int)p.roads.size()));
    for (int k = 0; k < count; k++) {
        const village::Road& r1 = p.roads[(size_t)(k % mains)];
        const village::Road& r2 = p.roads[(size_t)((k + 1) % mains)];
        // out the far side of the square, so they don't start in a crowd
        glm::vec2 d = glm::vec2(r1.a - glm::ivec2(p.center.x, p.center.z));
        glm::vec3 at = square + (glm::length(d) > 0.1f ? glm::vec3(d.x, 0, d.y) / glm::length(d) * 6.0f : glm::vec3(0));
        at = roomToStand(w, at);
        Npc* n = npcs.spawn(gVillagers.guardType, at, glm::vec3(d.x, 0, d.y), (uint32_t)rng() | 1u);
        if (!n) return;
        n->home = square;
        n->patrol = { ground(r1.a), ground(r1.b), ground(r1.a), square, ground(r2.a), ground(r2.b), ground(r2.a), square };
        n->patrolAt = 1;
        const std::string& nm = gVillagers.names[(size_t)(nameAt++ % (int)gVillagers.names.size())];
        n->name = nm + " the Guard";
        out.guards++;
        out.who.push_back(n->name + ": walks the roads");
    }
}

} // namespace villagers
