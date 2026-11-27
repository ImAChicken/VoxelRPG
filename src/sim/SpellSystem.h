#pragma once
// ══════════════════════════════════════════════════════════
// SpellSystem — spells happening (game/Spells.h has what they are)
// ══════════════════════════════════════════════════════════
// The C++ side of spells.toml, written once: every spell is these
// parts put together.
//   Deliveries (where it goes, at the cast's moment)
//     self        on the caster
//     touch       the nearest enemy in front, within range and arc
//     cone        everyone in front within range and arc (falloff)
//     projectile  bolts: count of them fanned by spread; speed,
//                 gravity, homing, bounces off walls, pierce bodies
//     beam        instant, straight along the aim to range: the first
//                 body (pierce: more); a channel repeats it
//     area        a circle out where you aim (or round you: at = "self")
//                 that goes off every 'every' s for 'duration' (0: once)
//     wall        across the way, range out: a structure (place, the
//                 world sets it down and takes it away), and/or a strip
//                 that does its effects to whoever is in it
//     rune        laid on the ground; armed after a moment, the first
//                 enemy over it sets it off (its on_hit spell, or its
//                 effects round it)
//     summon      npcs.toml types that fight for the caster a while
//   Effects (to each body it reaches; power: a charge's strength;
//   falloff toward a cone's / area's edge)
//     damage (a type, less what the body resists), push / pull,
//     poise, status, heal, ward, place, blink, light
//   Uses: a charge's power comes with the cast; a channel repeats the
//   delivery every 'every' s while held, draining stamina; a toggle
//   turns an aura on (its effects round the caster every 'every' s,
//   draining stamina) or off. Cooldowns start at the cast.
//   Triggers: on_hit / on_expire cast another spell from where it
//   happened (castAt; at most a few deep).
// The simulation is the world it works in (SpellWorld): who's who,
// hits, lines of sight, structures set down, summons, events.
// Deterministic, so a server runs it like everything else.
// (Claude, 2026-11-25, ROADMAP "Then: spell groundwork")
#include "../game/Spells.h"
#include "../entity/Human.h"
#include "../game/CombatHits.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

// What a spell asks of the world it's in (the Simulation)
struct SpellWorld {
    virtual ~SpellWorld() = default;
    virtual Human* spellBody(uint64_t id) = 0;
    virtual const std::vector<Human*>& spellBodies() = 0;        // every body this tick
    virtual bool spellEnemy(const Human& caster, const Human& b) = 0;
    virtual bool spellAlly(const Human& caster, const Human& b) = 0;
    // a hit by a spell (damage, poise): its events, aggro, kills
    virtual void spellHit(uint64_t from, Human& b, float damage, float poise, bool knockdown, glm::vec3 at, glm::vec3 dir, float knock, float lift) = 0;
    virtual bool spellClear(glm::vec3 a, glm::vec3 b) = 0;       // no solid voxel between
    virtual bool spellSolid(int x, int y, int z) = 0;
    virtual bool spellRoom(glm::vec3 feet, float radius, float height) = 0;   // a body fits there
    virtual void spellPlace(uint64_t owner, const std::string& structure, glm::vec3 at, glm::ivec3 voxel, int rot) = 0;
    virtual void spellSummon(const std::string& type, glm::vec3 at, glm::vec3 facing, uint64_t summoner, float life) = 0;
    // stage: SpellSystem::Stage; for the look (particles, sounds)
    virtual void spellFx(const spell::Def& d, uint8_t stage, glm::vec3 at, glm::vec3 dir, uint64_t a, uint64_t b) = 0;
};

class SpellSystem {
public:
    enum Stage : uint8_t { Cast, Hit, Tick, Expire, AuraOn, AuraOff, Light };
    static constexpr int MAX_DEPTH = 4;            // triggers starting triggers, at most
    static constexpr float TOUCH_REACH = 9.0f;     // vox: a touch's reach when its range is longer
    static constexpr float DOT_EVERY = 1.0f;       // s between a burn's / poison's hurts
    static constexpr float RUNE_ARM = 0.6f;        // s before a rune can go off
    static constexpr float STEP = 0.5f;            // vox: how finely paths are checked

    struct Bolt {
        uint32_t id = 0;
        const spell::Def* d = nullptr;
        uint64_t owner = 0;
        glm::vec3 pos{ 0.0f }, vel{ 0.0f };
        float travelled = 0.0f, power = 1.0f;
        int bounces = 0, pierce = 0, depth = 0;
        std::vector<uint64_t> passed;
    };
    // an area, a wall's strip, a rune: a circle at pos, or a strip
    // a → b (radius: its half-width)
    struct Zone {
        uint32_t id = 0;
        const spell::Def* d = nullptr;
        uint64_t owner = 0;
        glm::vec3 pos{ 0.0f }, a{ 0.0f }, b{ 0.0f };
        bool strip = false, rune = false;
        float radius = 6.0f, left = 0.0f, next = 0.0f, power = 1.0f, arm = 0.0f, age = 0.0f;
        int depth = 0;
    };
    struct Beam {
        const spell::Def* d = nullptr;
        glm::vec3 a{ 0.0f }, b{ 0.0f };
        float age = 0.0f;
    };
    static constexpr float BEAM_SHOW = 0.18f;      // s a beam stays drawn

    explicit SpellSystem(SpellWorld& w) : world(w) {}

    const std::vector<Bolt>& bolts() const { return boltList; }
    const std::vector<Zone>& zones() const { return zoneList; }
    const std::vector<Beam>& beams() const { return beamList; }
    size_t boltHits = 0;                            // (tests)
    void clear() { boltList.clear(); zoneList.clear(); beamList.clear(); }

    // ── The cast's moment: from a body ────────────────────
    void castFrom(Human& c, const spell::Def& d, float power, int depth = 0) {
        if (depth == 0 && d.cooldown > 0.0f) setCooldown(c, d);
        if (d.use == spell::Use::Toggle && depth == 0) { toggleAura(c, d); return; }
        glm::vec3 dir = aimOf(c, d);
        glm::vec3 from = c.position + glm::vec3(0.0f, c.height * 0.7f, 0.0f);
        world.spellFx(d, Cast, from, dir, c.uuid, 0);
        deliver(d, c.uuid, &c, from, dir, power, depth);
    }
    // ── A trigger: the spell from a point (no caster's pose) ──
    void castAt(const spell::Def& d, uint64_t owner, glm::vec3 at, glm::vec3 dir, float power, int depth) {
        if (depth > MAX_DEPTH) return;
        world.spellFx(d, Cast, at, dir, owner, 0);
        deliver(d, owner, nullptr, at, dir, power, depth);
    }
    void castChild(const std::string& id, uint64_t owner, glm::vec3 at, glm::vec3 dir, float power, int depth) {
        if (id.empty()) return;
        if (const spell::Def* c = gSpells.find(id)) castAt(*c, owner, at, dir, power, depth + 1);
    }

    // ── One tick ──────────────────────────────────────────
    void tick(float dt) {
        flyBolts(dt);
        runZones(dt);
        for (Beam& b : beamList) b.age += dt;
        beamList.erase(std::remove_if(beamList.begin(), beamList.end(), [](const Beam& b) { return b.age > BEAM_SHOW; }), beamList.end());
        for (Human* h : world.spellBodies()) {
            channelTick(*h, dt);
            auraTick(*h, dt);
            statusTick(*h, dt);
            for (auto& c : h->cooldowns) c.second -= dt;
            h->cooldowns.erase(std::remove_if(h->cooldowns.begin(), h->cooldowns.end(), [](auto& c) { return c.second <= 0.0f; }), h->cooldowns.end());
        }
    }

    // ── Effects on one body ───────────────────────────────
    //   k: 1 at the middle of a cone / area, 0 at its edge
    void apply(const spell::Def& d, uint64_t owner, Human& t, glm::vec3 at, glm::vec3 dir, float power, float k = 1.0f) {
        if (t.dead()) return;
        glm::vec3 flat(dir.x, 0.0f, dir.z);
        flat = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0, 0, 1);
        float dmg = 0.0f, poise = 0.0f, knock = 0.0f, lift = 0.2f;
        bool knockdown = false, struck = false;
        for (const spell::Effect& e : d.effects) {
            float f = e.falloff + (1.0f - e.falloff) * std::clamp(k, 0.0f, 1.0f);
            switch (e.kind) {
            case spell::EffectKind::Damage:
                dmg += e.amount * power * f * (1.0f - std::clamp(t.resist[(int)e.element], -1.0f, 1.0f));
                struck = true;
                break;
            case spell::EffectKind::Poise:
                poise += e.amount * power * f; knockdown = knockdown || e.knockdown; struck = true;
                break;
            case spell::EffectKind::Push: {
                float sp = e.amount * power * f;
                if (std::fabs(sp) > std::fabs(knock)) { knock = sp; lift = e.lift; }
                struck = true;
                break;
            }
            case spell::EffectKind::Status:
                addStatus(t, e.status, e.time, e.amount * power, owner);
                if (e.status == spell::Status::Freeze) { t.velocity.x = t.velocity.z = 0.0f; t.attack = AttackState(); }
                break;
            case spell::EffectKind::Heal:
                t.health = std::min(t.maxHealth, t.health + e.amount * power * f);
                break;
            case spell::EffectKind::Ward:
                t.ward = std::max(t.ward, e.amount * power);
                t.wardLeft = std::max(t.wardLeft, e.time > 0.0f ? e.time : 10.0f);
                break;
            case spell::EffectKind::Blink: blink(t, e.amount * power); break;
            case spell::EffectKind::Light: world.spellFx(d, Light, at, glm::vec3(0.0f, e.time, 0.0f), owner, t.uuid); break;
            case spell::EffectKind::Place: break;          // (where it lands, not on a body)
            }
        }
        if (struck) {
            glm::vec3 push = knock < 0.0f ? -flat : flat;
            world.spellHit(owner, t, dmg, poise, knockdown, at, push, std::fabs(knock), lift);
        }
    }

    // who an NPC caster is after (the Simulation sets it: Npc::target)
    std::function<uint64_t(const Human&)> npcTarget;

private:
    SpellWorld& world;
    float aimDist = 0.0f;          // how far the last aim reached (its target, or the range)
    std::vector<Bolt> boltList;
    std::vector<Zone> zoneList;
    std::vector<Beam> beamList;
    uint32_t nextId = 0;

    // ── Who it reaches ────────────────────────────────────
    bool reaches(const spell::Def& d, uint64_t owner, const Human& b) {
        if (b.dead()) return false;
        Human* c = world.spellBody(owner);
        switch (d.targets) {
        case spell::Targets::Self:    return b.uuid == owner;
        case spell::Targets::All:     return true;
        case spell::Targets::Allies:  return c ? (b.uuid == owner || world.spellAlly(*c, b)) : b.isPlayer();
        default:                      return b.uuid != owner && (c ? world.spellEnemy(*c, b) : b.isPlayer());
        }
    }

    // Where a caster aims: a player along its facing and aim pitch, an
    // NPC at its target; turned toward an enemy close to that line
    glm::vec3 aimOf(Human& c, const spell::Def& d) {
        glm::vec3 f = c.attack.active() ? c.attack.dir : c.facing;
        f.y = 0.0f;
        f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, 1);
        glm::vec3 from = c.position + glm::vec3(0.0f, c.height * 0.7f, 0.0f);
        glm::vec3 aim = f;
        aimDist = d.range;
        // an NPC: at its target (the spell's own aim help is for players)
        if (!c.isPlayer()) {
            if (Human* t = world.spellBody(npcTarget ? npcTarget(c) : 0); t && !t->dead()) {
                glm::vec3 to = t->position + glm::vec3(0.0f, t->height * 0.5f, 0.0f) - from;
                float dist = glm::length(to);
                if (dist > 1e-3f) {
                    aimDist = std::min(dist, d.range);
                    float tt = dist / std::max(d.speed, 1.0f);
                    return glm::normalize(to + glm::vec3(0.0f, 0.5f * d.gravity * tt * tt, 0.0f));
                }
            }
        }
        if (c.isPlayer()) {
            float pr = glm::radians(std::clamp(c.aimPitch, -80.0f, 80.0f));
            aim = glm::normalize(f * std::cos(pr) + glm::vec3(0.0f, std::sin(pr), 0.0f));
        }
        if (d.assist <= 0.0f) return aim;
        float best = std::cos(glm::radians(d.assist));
        glm::vec3 pick = aim;
        for (Human* b : world.spellBodies()) {
            if (b == &c || !reaches(d, c.uuid, *b) || d.targets != spell::Targets::Enemies) continue;
            glm::vec3 to = b->position + glm::vec3(0.0f, b->height * 0.5f, 0.0f) - from;
            float dist = glm::length(to);
            if (dist < 1e-3f || dist > d.range) continue;
            glm::vec3 fl = glm::normalize(glm::vec3(to.x, 0.0f, to.z) + glm::vec3(1e-6f));
            float c2 = glm::dot(fl, f);
            if (c2 < best || !world.spellClear(from, from + to)) continue;
            best = c2;
            aimDist = dist;
            float t = dist / std::max(d.speed, 1.0f);                    // lifted for the drop on the way
            pick = glm::normalize(to + glm::vec3(0.0f, 0.5f * d.gravity * t * t, 0.0f));
        }
        return pick;
    }

    // ── Deliveries ────────────────────────────────────────
    void deliver(const spell::Def& d, uint64_t owner, Human* c, glm::vec3 from, glm::vec3 dir, float power, int depth) {
        glm::vec3 flat(dir.x, 0.0f, dir.z);
        flat = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0, 0, 1);
        switch (d.delivery) {
        case spell::Delivery::Self:
            if (c) { apply(d, owner, *c, from, flat, power); world.spellFx(d, Hit, from, flat, owner, owner); }
            else burst(d, owner, from, flat, std::max(d.radius, 3.0f), power);    // from a point: those right there
            break;
        case spell::Delivery::Touch: {
            float reach = std::min(d.range, TOUCH_REACH);
            Human* best = nullptr; float bestD = 1e9f;
            float cosArc = std::cos(glm::radians(std::clamp(d.arc, 0.0f, 180.0f)));
            for (Human* b : world.spellBodies()) {
                if (!reaches(d, owner, *b)) continue;
                glm::vec3 off = b->position - (c ? c->position : from); off.y = 0.0f;
                float dist = glm::length(off) - b->hitRadius;
                if (dist > reach) continue;
                if (glm::length(off) > 1e-3f && glm::dot(glm::normalize(off), flat) < cosArc && dist > 1.0f) continue;
                if (dist < bestD) { bestD = dist; best = b; }
            }
            if (best) {
                glm::vec3 at = best->bodyPoint(0.6f);
                apply(d, owner, *best, at, flat, power);
                world.spellFx(d, Hit, at, flat, owner, best->uuid);
                castChild(d.onHit, owner, at, flat, power, depth);
            }
            break;
        }
        case spell::Delivery::Cone: {
            float cosArc = std::cos(glm::radians(std::clamp(d.arc, 0.0f, 180.0f)));
            glm::vec3 base = c ? c->position : from;
            for (Human* b : world.spellBodies()) {
                if (!reaches(d, owner, *b)) continue;
                glm::vec3 to = b->position - base; to.y = 0.0f;
                float dist = glm::length(to);
                if (dist > d.range || dist < 1e-3f) continue;
                glm::vec3 bd = to / dist;
                if (glm::dot(bd, flat) < cosArc) continue;
                if (!world.spellClear(from, b->position + glm::vec3(0.0f, b->height * 0.6f, 0.0f))) continue;
                glm::vec3 at = b->bodyPoint(0.6f);
                apply(d, owner, *b, at, bd, power, 1.0f - dist / d.range);
                world.spellFx(d, Hit, at, bd, owner, b->uuid);
            }
            castChild(d.onHit, owner, from + flat * d.range * 0.5f, flat, power, depth);
            break;
        }
        case spell::Delivery::Projectile: {
            int n = std::max(1, d.count);
            for (int i = 0; i < n; i++) {
                float yaw = (i - (n - 1) * 0.5f) * glm::radians(d.spread);
                glm::vec3 v(dir.x * std::cos(yaw) - dir.z * std::sin(yaw), dir.y, dir.x * std::sin(yaw) + dir.z * std::cos(yaw));
                Bolt b;
                b.id = ++nextId; b.d = &d; b.owner = owner; b.power = power; b.depth = depth;
                b.pos = from + glm::normalize(glm::vec3(v.x, 0.0f, v.z) + glm::vec3(1e-6f)) * ((c ? c->radius() : 0.0f) + 1.0f);
                b.vel = glm::normalize(v) * d.speed;
                b.bounces = d.bounces; b.pierce = d.pierce;
                boltList.push_back(b);
            }
            break;
        }
        case spell::Delivery::Beam: {
            glm::vec3 end = from + dir * d.range;
            int pierce = d.pierce;
            std::vector<uint64_t> hit;
            bool stop = false;
            for (float s = STEP; s <= d.range && !stop; s += STEP) {
                glm::vec3 q = from + dir * s;
                for (Human* b : world.spellBodies()) {
                    if (!reaches(d, owner, *b) || std::find(hit.begin(), hit.end(), b->uuid) != hit.end()) continue;
                    if (!inBody(*b, q, 0.5f)) continue;
                    hit.push_back(b->uuid);
                    apply(d, owner, *b, q, dir, power);
                    world.spellFx(d, Hit, q, dir, owner, b->uuid);
                    if (pierce-- <= 0) { end = q; stop = true; break; }
                }
                if (stop) break;
                if (world.spellSolid((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z))) { end = q; stop = true; }
            }
            Beam bm; bm.d = &d; bm.a = from; bm.b = end;
            beamList.push_back(bm);
            if (stop) castChild(d.onHit, owner, end, dir, power, depth);
            break;
        }
        case spell::Delivery::Area: {
            glm::vec3 at = c && d.atSelf ? c->position : !c ? from : groundAlong(from, dir, std::min(d.range, aimDist + 1.0f));
            Zone z = zone(d, owner, at, power, depth);
            z.radius = d.radius;
            placeEffects(d, owner, at, flat);
            zoneList.push_back(z);
            runZone(zoneList.back(), 0.0f);                          // its first go, now
            break;
        }
        case spell::Delivery::Wall: {
            glm::vec3 centre = (c ? c->position : from) + flat * std::max(d.range, 4.0f);
            centre = ground(centre);
            glm::vec3 side(-flat.z, 0.0f, flat.x);
            if (!d.place.empty()) {
                // turned to lie across the way (a quarter turn per 90°)
                int rot = ((int)std::lround(std::atan2(flat.z, flat.x) / 1.5707963f) + 1) % 4;
                rot = (rot + 4) % 4;
                world.spellPlace(owner, d.place, centre + glm::vec3(0, 1, 0),
                                 glm::ivec3((int)std::floor(centre.x), (int)std::floor(centre.y) - 1, (int)std::floor(centre.z)), rot);
            }
            if (!d.effects.empty()) {
                Zone z = zone(d, owner, centre, power, depth);
                z.strip = true; z.a = centre - side * (d.width * 0.5f); z.b = centre + side * (d.width * 0.5f);
                z.radius = 2.0f;
                zoneList.push_back(z);
            }
            world.spellFx(d, Hit, centre, flat, owner, 0);
            break;
        }
        case spell::Delivery::Rune: {
            glm::vec3 at = c ? groundAlong(from, dir, std::max(std::min(d.range, aimDist + 1.0f), 2.0f)) : ground(from);
            Zone z = zone(d, owner, at, power, depth);
            z.rune = true; z.arm = RUNE_ARM; z.radius = d.radius;
            if (z.left <= 0.0f) z.left = 60.0f;
            zoneList.push_back(z);
            break;
        }
        case spell::Delivery::Summon: {
            glm::vec3 base = c ? c->position : from;
            int n = std::max(1, d.count);
            for (int i = 0; i < n; i++) {
                float a = std::atan2(flat.z, flat.x) + (i - (n - 1) * 0.5f) * 0.9f;
                glm::vec3 at = ground(base + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 6.0f + glm::vec3(0, 2, 0));
                world.spellSummon(d.summon, at, flat, owner, d.duration > 0.0f ? d.duration : 30.0f);
                world.spellFx(d, Hit, at, flat, owner, 0);
            }
            break;
        }
        }
    }

    Zone zone(const spell::Def& d, uint64_t owner, glm::vec3 at, float power, int depth) {
        Zone z;
        z.id = ++nextId; z.d = &d; z.owner = owner; z.pos = at; z.a = z.b = at;
        z.power = power; z.depth = depth; z.left = d.duration; z.next = 0.0f;
        return z;
    }

    // Effects in a circle, at once (a self spell from a point)
    void burst(const spell::Def& d, uint64_t owner, glm::vec3 at, glm::vec3 dir, float radius, float power) {
        for (Human* b : world.spellBodies()) {
            if (!reaches(d, owner, *b)) continue;
            float dist = glm::length(b->bodyPoint(0.5f) - at);
            if (dist > radius + b->hitRadius) continue;
            apply(d, owner, *b, b->bodyPoint(0.5f), dir, power, 1.0f - dist / std::max(radius, 1e-3f));
        }
    }

    // place effects: the structure set down where it lands
    void placeEffects(const spell::Def& d, uint64_t owner, glm::vec3 at, glm::vec3 dir) {
        (void)dir;
        for (const spell::Effect& e : d.effects)
            if (e.kind == spell::EffectKind::Place && !e.place.empty())
                world.spellPlace(owner, e.place, at + glm::vec3(0, 0.5f, 0),
                                 glm::ivec3((int)std::floor(at.x), (int)std::floor(at.y) - 1, (int)std::floor(at.z)), 0);
    }

    // The ground under a point (the first open voxel over solid; down
    // up to 30, else the point)
    glm::vec3 ground(glm::vec3 p) {
        int x = (int)std::floor(p.x), z = (int)std::floor(p.z), y = (int)std::floor(p.y);
        if (world.spellSolid(x, y, z)) { for (int i = 0; i < 8 && world.spellSolid(x, y, z); i++) y++; return glm::vec3(p.x, (float)y, p.z); }
        for (int i = 0; i < 30; i++, y--) if (world.spellSolid(x, y - 1, z)) return glm::vec3(p.x, (float)y, p.z);
        return p;
    }
    // Along the aim to the first solid voxel (or range), then down to the ground
    glm::vec3 groundAlong(glm::vec3 from, glm::vec3 dir, float range) {
        glm::vec3 last = from;
        for (float s = STEP; s <= range; s += STEP) {
            glm::vec3 q = from + dir * s;
            if (world.spellSolid((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z))) break;
            last = q;
        }
        return ground(last);
    }
    static bool inBody(const Human& b, glm::vec3 q, float pad) {
        glm::vec2 off(q.x - b.position.x, q.z - b.position.z);
        float hh = b.hitHeight > 0.0f ? std::max(b.height, b.hitHeight) : b.height;
        return glm::length(off) <= b.hitRadius + pad && q.y >= b.position.y - 0.3f && q.y <= b.position.y + hh + 0.3f;
    }

    // ── Bolts ─────────────────────────────────────────────
    void flyBolts(float dt) {
        for (size_t i = 0; i < boltList.size();) {
            Bolt& p = boltList[i];
            const spell::Def& d = *p.d;
            // homing: turned toward an enemy ahead
            if (d.homing > 0.0f) {
                glm::vec3 v = glm::normalize(p.vel);
                Human* tgt = nullptr; float best = std::cos(glm::radians(40.0f));
                for (Human* b : world.spellBodies()) {
                    if (!reaches(d, p.owner, *b)) continue;
                    glm::vec3 to = b->bodyPoint(0.5f) - p.pos;
                    float l = glm::length(to);
                    if (l < 1e-3f || l > d.range) continue;
                    float c2 = glm::dot(to / l, v);
                    if (c2 > best) { best = c2; tgt = b; }
                }
                if (tgt) {
                    glm::vec3 want = glm::normalize(tgt->bodyPoint(0.5f) - p.pos);
                    float ang = std::acos(std::clamp(glm::dot(v, want), -1.0f, 1.0f));
                    float turn = glm::radians(d.homing) * dt;
                    if (ang > 1e-4f) {
                        float k = std::min(1.0f, turn / ang);
                        p.vel = glm::normalize(glm::mix(v, want, k)) * glm::length(p.vel);
                    }
                }
            }
            glm::vec3 from = p.pos;
            p.vel.y -= d.gravity * dt;
            glm::vec3 to = p.pos + p.vel * dt;
            float len = glm::length(to - from);
            int steps = std::max(1, (int)std::ceil(len / STEP));
            bool done = false;
            glm::vec3 prev = from;
            for (int s = 1; s <= steps && !done; s++) {
                glm::vec3 q = from + (to - from) * ((float)s / steps);
                for (Human* b : world.spellBodies()) {
                    if (b->uuid == p.owner || !reaches(d, p.owner, *b)) continue;
                    if (std::find(p.passed.begin(), p.passed.end(), b->uuid) != p.passed.end()) continue;
                    if (!inBody(*b, q, 0.6f)) continue;
                    glm::vec3 dir = glm::length(p.vel) > 1e-3f ? glm::normalize(p.vel) : glm::vec3(0, 0, 1);
                    boltHits++;
                    apply(d, p.owner, *b, q, dir, p.power);
                    world.spellFx(d, Hit, q, dir, p.owner, b->uuid);
                    castChild(d.onHit, p.owner, q, dir, p.power, p.depth);
                    if (p.pierce > 0) { p.pierce--; p.passed.push_back(b->uuid); continue; }
                    done = true;
                    break;
                }
                if (done) break;
                glm::ivec3 v((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z));
                if (world.spellSolid(v.x, v.y, v.z) || q.y < 0.0f) {
                    if (p.bounces > 0) {
                        // off the wall: the parts of its way that went into it turn back
                        glm::ivec3 pv((int)std::floor(prev.x), (int)std::floor(prev.y), (int)std::floor(prev.z));
                        bool flipped = false;
                        for (int ax = 0; ax < 3; ax++) {
                            if (pv[ax] == v[ax]) continue;
                            glm::ivec3 t = pv; t[ax] = v[ax];
                            if (world.spellSolid(t.x, t.y, t.z)) { p.vel[ax] = -p.vel[ax] * 0.85f; flipped = true; }
                        }
                        if (!flipped) p.vel = -p.vel * 0.85f;
                        p.bounces--;
                        to = prev;
                        world.spellFx(d, Tick, prev, glm::normalize(p.vel + glm::vec3(1e-6f)), p.owner, 0);
                        break;
                    }
                    glm::vec3 dir = glm::length(p.vel) > 1e-3f ? glm::normalize(p.vel) : glm::vec3(0, 0, 1);
                    world.spellFx(d, Hit, prev, dir, p.owner, 0);
                    for (const spell::Effect& e : d.effects)
                        if (e.kind == spell::EffectKind::Place && !e.place.empty()) world.spellPlace(p.owner, e.place, prev, v, 0);
                    castChild(d.onHit, p.owner, prev, dir, p.power, p.depth);
                    done = true;
                    break;
                }
                prev = q;
            }
            p.travelled += len;
            p.pos = to;
            if (!done && p.travelled > d.range) {
                world.spellFx(d, Expire, p.pos, glm::normalize(p.vel + glm::vec3(1e-6f)), p.owner, 0);
                castChild(d.onExpire, p.owner, p.pos, glm::normalize(p.vel + glm::vec3(1e-6f)), p.power, p.depth);
                done = true;
            }
            if (done) boltList.erase(boltList.begin() + (long)i);
            else i++;
        }
    }

    // ── Areas, walls, runes ───────────────────────────────
    void runZones(float dt) {
        for (size_t i = 0; i < zoneList.size();) {
            bool gone = runZone(zoneList[i], dt);
            if (gone) zoneList.erase(zoneList.begin() + (long)i);
            else i++;
        }
    }
    // false: it stays
    bool runZone(Zone& z, float dt) {
        const spell::Def& d = *z.d;
        z.age += dt;
        z.left -= dt;
        z.next -= dt;
        if (z.rune) {
            z.arm -= dt;
            if (z.left <= 0.0f) { world.spellFx(d, Expire, z.pos, glm::vec3(0, 1, 0), z.owner, 0); return true; }
            if (z.arm > 0.0f) return false;
            for (Human* b : world.spellBodies()) {
                if (!reaches(d, z.owner, *b)) continue;
                if (glm::length(glm::vec2(b->position.x - z.pos.x, b->position.z - z.pos.z)) > z.radius + b->hitRadius) continue;
                if (std::fabs(b->position.y - z.pos.y) > 4.0f) continue;
                // set off
                world.spellFx(d, Hit, z.pos, glm::vec3(0, 1, 0), z.owner, b->uuid);
                if (!d.onHit.empty()) castChild(d.onHit, z.owner, z.pos, glm::vec3(0, 1, 0), z.power, z.depth);
                else burst(d, z.owner, z.pos, glm::vec3(0, 1, 0), z.radius, z.power);
                return true;
            }
            return false;
        }
        if (z.next <= 0.0f) {
            z.next += std::max(d.every, 0.05f);
            for (Human* b : world.spellBodies()) {
                if (!reaches(d, z.owner, *b)) continue;
                glm::vec3 p = b->bodyPoint(0.3f);
                float dist;
                if (z.strip) {
                    glm::vec3 ab = z.b - z.a, ap = p - z.a;
                    float u = std::clamp(glm::dot(glm::vec2(ap.x, ap.z), glm::vec2(ab.x, ab.z)) / std::max(glm::dot(glm::vec2(ab.x, ab.z), glm::vec2(ab.x, ab.z)), 1e-3f), 0.0f, 1.0f);
                    glm::vec3 c = z.a + ab * u;
                    dist = glm::length(glm::vec2(p.x - c.x, p.z - c.z));
                    if (dist > z.radius + b->hitRadius || std::fabs(p.y - z.pos.y) > d.height + 2.0f) continue;
                } else {
                    dist = glm::length(glm::vec2(p.x - z.pos.x, p.z - z.pos.z));
                    if (dist > z.radius + b->hitRadius || p.y < z.pos.y - 4.0f || p.y > z.pos.y + std::max(z.radius, 8.0f)) continue;
                }
                glm::vec3 out = p - z.pos; out.y = 0.0f;
                out = glm::length(out) > 1e-3f ? glm::normalize(out) : glm::vec3(0, 0, 1);
                apply(d, z.owner, *b, p, out, z.power, 1.0f - dist / std::max(z.radius, 1e-3f));
            }
            world.spellFx(d, Tick, z.pos, glm::vec3(0, 1, 0), z.owner, 0);
        }
        if (z.left <= 0.0f) {
            world.spellFx(d, Expire, z.pos, glm::vec3(0, 1, 0), z.owner, 0);
            castChild(d.onExpire, z.owner, z.pos, glm::vec3(0, 1, 0), z.power, z.depth);
            return true;
        }
        return false;
    }

    // ── Uses that go on: channels and auras ───────────────
    void channelTick(Human& h, float dt) {
        AttackState& a = h.attack;
        if (!a.active() || h.dead() || !a.d->channel || a.d->spell.empty()) return;
        if (a.t < a.d->activeStart || !a.held || a.channelled >= a.d->channelMax) return;
        const spell::Def* d = gSpells.find(a.d->spell);
        if (!d) return;
        h.stamina -= d->drain * dt;
        if (h.stamina <= 0.0f) { h.stamina = 0.0f; a.channelled = a.d->channelMax; return; }   // spent: it ends
        float every = std::max(d->every, 0.05f);
        while ((float)(a.ticks + 1) * every <= a.channelled) {
            a.ticks++;
            castFrom(h, *d, a.power, 1);
        }
    }
    void toggleAura(Human& c, const spell::Def& d) {
        if (c.aura == &d) { c.aura = nullptr; world.spellFx(d, AuraOff, c.position, glm::vec3(0, 1, 0), c.uuid, 0); return; }
        c.aura = &d;
        c.auraTick = 0.0f;
        world.spellFx(d, AuraOn, c.position, glm::vec3(0, 1, 0), c.uuid, 0);
    }
    void auraTick(Human& h, float dt) {
        if (!h.aura) return;
        const spell::Def& d = *h.aura;
        if (h.dead()) { h.aura = nullptr; return; }
        h.stamina -= d.drain * dt;
        h.staminaWait = std::max(h.staminaWait, 0.2f);
        if (h.stamina <= 0.0f) { h.stamina = 0.0f; h.aura = nullptr; world.spellFx(d, AuraOff, h.position, glm::vec3(0, 1, 0), h.uuid, 0); return; }
        h.auraTick -= dt;
        if (h.auraTick > 0.0f) return;
        h.auraTick += std::max(d.every, 0.1f);
        glm::vec3 at = h.position + glm::vec3(0.0f, h.height * 0.5f, 0.0f);
        for (Human* b : world.spellBodies()) {
            if (!reaches(d, h.uuid, *b)) continue;
            float dist = glm::length(b->bodyPoint(0.5f) - at);
            if (dist > d.radius + b->hitRadius) continue;
            glm::vec3 out = b->position - h.position; out.y = 0.0f;
            apply(d, h.uuid, *b, b->bodyPoint(0.5f), glm::length(out) > 1e-3f ? glm::normalize(out) : glm::vec3(0, 0, 1), 1.0f,
                  1.0f - dist / std::max(d.radius, 1e-3f));
        }
        world.spellFx(d, Tick, h.position, glm::vec3(0, 1, 0), h.uuid, 0);
    }

    // ── Statuses ──────────────────────────────────────────
    static void addStatus(Human& t, spell::Status s, float time, float amount, uint64_t from) {
        if (s == spell::Status::None || time <= 0.0f) return;
        for (Human::StatusOn& x : t.statuses)
            if (x.kind == s) { x.left = std::max(x.left, time); x.amount = std::max(x.amount, amount); x.from = from; return; }
        Human::StatusOn x; x.kind = s; x.left = time; x.amount = amount; x.from = from; x.tick = DOT_EVERY;
        t.statuses.push_back(x);
    }
    void statusTick(Human& h, float dt) {
        if (h.wardLeft > 0.0f) { h.wardLeft -= dt; if (h.wardLeft <= 0.0f) { h.wardLeft = 0.0f; h.ward = 0.0f; } }
        if (h.statuses.empty()) return;
        for (Human::StatusOn& x : h.statuses) {
            x.left -= dt;
            if (h.dead()) continue;
            if (x.kind == spell::Status::Burn || x.kind == spell::Status::Poison) {
                x.tick -= dt;
                if (x.tick <= 0.0f && x.left > -dt) {
                    x.tick += DOT_EVERY;
                    spell::Element el = x.kind == spell::Status::Burn ? spell::Element::Fire : spell::Element::Nature;
                    float dmg = x.amount * (1.0f - std::clamp(h.resist[(int)el], -1.0f, 1.0f));
                    world.spellHit(x.from, h, dmg, 0.0f, false, h.bodyPoint(0.5f), glm::vec3(0.0f), 0.0f, 0.0f);
                }
            }
        }
        h.statuses.erase(std::remove_if(h.statuses.begin(), h.statuses.end(), [](const Human::StatusOn& x) { return x.left <= 0.0f; }), h.statuses.end());
    }

    void setCooldown(Human& c, const spell::Def& d) {
        for (auto& x : c.cooldowns) if (x.first == &d) { x.second = d.cooldown; return; }
        c.cooldowns.push_back({ &d, d.cooldown });
    }

    // Moved along its facing up to 'dist', to the furthest spot it fits
    void blink(Human& h, float dist) {
        glm::vec3 f = h.facing; f.y = 0.0f;
        f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, 1);
        glm::vec3 best = h.position;
        for (float s = 1.0f; s <= dist; s += 1.0f) {
            glm::vec3 p = h.position + f * s;
            if (!world.spellRoom(p, h.radius(), h.height)) {
                // a step up?
                if (world.spellRoom(p + glm::vec3(0, 3, 0), h.radius(), h.height)) { best = p + glm::vec3(0, 3, 0); continue; }
                break;
            }
            if (!world.spellClear(best + glm::vec3(0, h.height * 0.5f, 0), p + glm::vec3(0, h.height * 0.5f, 0))) break;
            best = p;
        }
        h.position = best;
        h.velocity = glm::vec3(f.x, 0.0f, f.z) * 10.0f;
    }
};
