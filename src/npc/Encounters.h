#pragma once
#include "NpcTypes.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Encounters — fights as things the game keeps track of
// ══════════════════════════════════════════════════════════
// An ENCOUNTER opens when the first NPC goes from alert into
// combat against a player, and closes when every NPC in it is
// dead, has fled or gone home — or the players in it are dead or
// got away (no NPC in it has seen them for ESCAPE_AFTER). Opening
// and closing are events (the client shows its popups then).
//
// One encounter per player while they're apart; when one NPC
// fights players from two encounters, they MERGE into one.
// Encounters don't split; each player gets their own review of
// the shared fight (their kills, damage, and a share of XP).
//
// POSITIONS around a target ("slots"). Each NPC type has a list
// of preferred slots (npcs.toml: slots), best first; joining, it
// takes the first one free around its target, so the first
// swordsman goes in front and the next one to the rear flank.
// Slots are angles from the target's FRONT, which is anchored to
// where the front-slot holder is (not the target's camera, which
// would spin everyone around), turning slowly toward the
// target's facing. Slots sit at two distances: ENGAGE (in reach,
// for the one attacking) and HOLD (waiting a turn). A slot inside
// a wall or off a cliff moves to the nearest place to stand.
//
// ATTACK TOKENS: at most TOKENS_PER_TARGET NPCs attack one target
// at a time; the rest hold their slots. An NPC asks for a token
// when it wants to swing and gives it back when the attack ends.
//
// BREADCRUMBS: every player drops one each second (position and
// time) — the truth of where they went. What an NPC *knows* is
// separate: the time it last saw them; its last known position is
// the crumb from then. Battle cries send the newest crumb.
namespace encounter {

constexpr float ESCAPE_AFTER      = 15.0f;   // s unseen by everyone in it → the player got away
constexpr int   TOKENS_PER_TARGET = 2;
constexpr float ENGAGE_DIST       = 9.0f;    // vox from the target
constexpr float HOLD_DIST         = 18.0f;
constexpr float FRONT_TURN        = 0.6f;    // rad/s the front turns toward the target's facing
constexpr float CRUMB_EVERY       = 1.0f;    // s
constexpr size_t CRUMBS_KEPT      = 60;

inline float slotAngle(Slot s) {               // degrees from the front, positive to the target's left
    switch (s) {
    case Slot::Front: return 0.0f;
    case Slot::FrontLeft: return 45.0f;
    case Slot::FrontRight: return -45.0f;
    case Slot::Left: return 90.0f;
    case Slot::Right: return -90.0f;
    case Slot::RearLeft: return 135.0f;
    case Slot::RearRight: return -135.0f;
    case Slot::RearFlank: return 180.0f;
    default: return 0.0f;
    }
}

struct Crumb { glm::vec3 at; float time; };

// What one player did in an encounter (their review)
struct PlayerTally {
    EntityId player = 0;
    int   kills = 0;
    float dealt = 0.0f, taken = 0.0f;
    bool  died = false, escaped = false;
    float xp = 0.0f;                           // filled when it ends
    std::vector<std::pair<uint16_t, int>> loot; // item, count (none yet: drop tables later)
};

struct Encounter {
    uint32_t id = 0;
    float    started = 0.0f, ended = -1.0f;
    std::vector<EntityId> players;             // in it (fighting or fought)
    std::vector<EntityId> npcs;                // ever in it
    std::vector<EntityId> active;              // still fighting (combat / search)
    std::vector<std::string> enemyNames;       // for the banner
    float    xpPool = 0.0f;                    // from the enemies killed
    int      killed = 0, fled = 0;
    std::unordered_map<EntityId, PlayerTally> tally;

    struct Claim { EntityId npc; EntityId target; Slot slot; };
    std::vector<Claim> claims;
    std::unordered_map<EntityId, glm::vec2> front;           // per target: the front direction (x, z)
    std::unordered_map<EntityId, std::vector<EntityId>> tokens;   // per target: who may attack now
    std::unordered_map<EntityId, float> lastSeen;            // per player: when anyone in it last saw them

    bool has(const std::vector<EntityId>& v, EntityId x) const { return std::find(v.begin(), v.end(), x) != v.end(); }
};

// What the registry needs from the game
struct Hooks {
    std::function<bool(glm::vec3, glm::vec3&)> standNear;    // a place to stand near a point
    std::function<float()> now;
};

class Registry {
public:
    Hooks hooks;

    // ── Breadcrumbs ───────────────────────────────────────
    void dropCrumbs(EntityId player, glm::vec3 at, float now) {
        auto& t = trails[player];
        if (!t.empty() && now - t.back().time < CRUMB_EVERY) return;
        t.push_back({ at, now });
        while (t.size() > CRUMBS_KEPT) t.pop_front();
    }
    const Crumb* latestCrumb(EntityId player) const {
        auto it = trails.find(player);
        return it == trails.end() || it->second.empty() ? nullptr : &it->second.back();
    }
    // The crumb at (or just before) a time: where they were then
    const Crumb* crumbAt(EntityId player, float time) const {
        auto it = trails.find(player);
        if (it == trails.end()) return nullptr;
        const Crumb* best = nullptr;
        for (const Crumb& c : it->second) { if (c.time <= time + 1e-3f) best = &c; else break; }
        return best ? best : (it->second.empty() ? nullptr : &it->second.front());
    }
    // The first crumb after a time (following a trail)
    const Crumb* crumbAfter(EntityId player, float time) const {
        auto it = trails.find(player);
        if (it == trails.end()) return nullptr;
        for (const Crumb& c : it->second) if (c.time > time + 1e-3f) return &c;
        return nullptr;
    }
    const std::deque<Crumb>* trail(EntityId player) const {
        auto it = trails.find(player);
        return it == trails.end() ? nullptr : &it->second;
    }
    void forgetPlayer(EntityId p) { trails.erase(p); }

    // ── Joining and leaving ───────────────────────────────
    // An NPC starts fighting a player. Returns its encounter.
    Encounter& join(EntityId npc, const std::string& npcName, const NpcType* type, EntityId player) {
        Encounter* withNpc = of(npc);
        Encounter* withPlayer = ofPlayer(player);
        Encounter* e;
        if (withNpc && withPlayer && withNpc != withPlayer) e = merge(*withNpc, *withPlayer);
        else if (withNpc) e = withNpc;
        else if (withPlayer) e = withPlayer;
        else e = open();
        if (!e->has(e->players, player)) { e->players.push_back(player); e->tally[player].player = player; }
        if (!e->has(e->npcs, npc)) { e->npcs.push_back(npc); e->enemyNames.push_back(npcName); }
        if (!e->has(e->active, npc)) e->active.push_back(npc);
        e->lastSeen[player] = hooks.now ? hooks.now() : 0.0f;
        claimSlot(*e, npc, type, player);
        return *e;
    }
    // An NPC stops fighting: reason "dead", "fled", "return", …
    void leave(EntityId npc, const std::string& reason) {
        Encounter* e = of(npc);
        if (!e) return;
        e->active.erase(std::remove(e->active.begin(), e->active.end(), npc), e->active.end());
        e->claims.erase(std::remove_if(e->claims.begin(), e->claims.end(), [&](auto& c) { return c.npc == npc; }), e->claims.end());
        for (auto& [t, v] : e->tokens) v.erase(std::remove(v.begin(), v.end(), npc), v.end());
        if (reason == "fled") e->fled++;
    }
    // Someone in the encounter sees this player now
    void seen(EntityId npc, EntityId player) {
        if (Encounter* e = of(npc)) e->lastSeen[player] = hooks.now ? hooks.now() : 0.0f;
    }

    // ── Score ─────────────────────────────────────────────
    void hit(EntityId attacker, EntityId target, float damage) {
        for (auto& e : list) {
            if (e.ended >= 0.0f) continue;
            if (e.has(e.players, attacker) && e.has(e.npcs, target)) e.tally[attacker].dealt += damage;
            if (e.has(e.players, target) && e.has(e.npcs, attacker)) e.tally[target].taken += damage;
        }
    }
    void killed(EntityId killer, EntityId victim, float xp) {
        for (auto& e : list) {
            if (e.ended >= 0.0f) continue;
            if (e.has(e.npcs, victim)) {
                e.killed++;
                e.xpPool += xp;
                if (e.has(e.players, killer)) e.tally[killer].kills++;
            }
            if (e.has(e.players, victim)) e.tally[victim].died = true;
        }
        leave(victim, "dead");
    }

    // ── Each tick: close what's over ──────────────────────
    // isGone(player): dead or left the world
    std::vector<uint32_t> update(const std::function<bool(EntityId)>& playerGone) {
        std::vector<uint32_t> closed;
        float now = hooks.now ? hooks.now() : 0.0f;
        for (auto& e : list) {
            if (e.ended >= 0.0f) continue;
            bool anyPlayer = false;
            for (EntityId p : e.players) {
                PlayerTally& t = e.tally[p];
                if (t.escaped || t.died) continue;
                if (playerGone(p)) { t.died = true; continue; }
                if (now - e.lastSeen[p] > ESCAPE_AFTER) { t.escaped = true; continue; }
                anyPlayer = true;
            }
            if (e.active.empty() || !anyPlayer) {
                e.ended = now;
                int share = 0;
                for (EntityId p : e.players) if (!e.tally[p].died) share++;
                for (EntityId p : e.players) {
                    PlayerTally& t = e.tally[p];
                    t.xp = (!t.died && share) ? e.xpPool / share : 0.0f;   // shared by those still standing
                }
                e.claims.clear(); e.tokens.clear();
                closed.push_back(e.id);
            }
        }
        // forget old ones
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Encounter& e) { return e.ended >= 0.0f && now - e.ended > 30.0f; }), list.end());
        return closed;
    }

    // ── Positions ─────────────────────────────────────────
    // Where this NPC should stand: its slot around its target, at
    // the engage distance if it holds an attack token, else hold.
    // false if it has no slot (not in an encounter).
    bool slotSpot(EntityId npc, glm::vec3 targetPos, glm::vec3 targetFacing, glm::vec3 npcPos,
                  glm::vec3& out, bool& engage, Slot& slot, float dt) {
        Encounter* e = of(npc);
        if (!e) return false;
        const Encounter::Claim* c = nullptr;
        for (auto& x : e->claims) if (x.npc == npc) c = &x;
        if (!c) return false;
        slot = c->slot;
        engage = hasToken(*e, c->target, npc);
        float dist = engage ? ENGAGE_DIST : HOLD_DIST;
        glm::vec2& f = frontFor(*e, c->target, targetPos, npcPos);
        // the front holder defines the front; it turns slowly toward the target's facing
        glm::vec2 tf(targetFacing.x, targetFacing.z);
        if (glm::length(tf) > 1e-3f) {
            tf = glm::normalize(tf);
            float a = std::atan2(f.y, f.x), b = std::atan2(tf.y, tf.x);
            float d = std::remainder(b - a, 6.2831853f);
            a += std::clamp(d, -FRONT_TURN * dt, FRONT_TURN * dt);
            f = glm::vec2(std::cos(a), std::sin(a));
        }
        glm::vec2 dir;
        if (c->slot == Slot::Ring) {
            glm::vec2 off(npcPos.x - targetPos.x, npcPos.z - targetPos.z);
            dir = glm::length(off) > 1e-3f ? glm::normalize(off) : f;
        } else {
            float r = glm::radians(slotAngle(c->slot));
            glm::vec2 left(f.y, -f.x);
            dir = f * std::cos(r) + left * std::sin(r);
        }
        glm::vec3 want = targetPos + glm::vec3(dir.x, 0.0f, dir.y) * dist;
        glm::vec3 stand;
        out = (hooks.standNear && hooks.standNear(want, stand)) ? stand : want;
        return true;
    }
    // Ask to attack the target now; true = go
    bool requestAttack(EntityId npc) {
        Encounter* e = of(npc);
        if (!e) return true;                    // not in one: free to swing
        const Encounter::Claim* c = claimOf(*e, npc);
        if (!c) return true;
        auto& v = e->tokens[c->target];
        if (std::find(v.begin(), v.end(), npc) != v.end()) return true;
        if ((int)v.size() >= TOKENS_PER_TARGET) return false;
        v.push_back(npc);
        return true;
    }
    void releaseAttack(EntityId npc) {
        Encounter* e = of(npc);
        if (!e) return;
        for (auto& [t, v] : e->tokens) v.erase(std::remove(v.begin(), v.end(), npc), v.end());
    }
    bool holdsToken(EntityId npc) {
        Encounter* e = of(npc);
        const Encounter::Claim* c = e ? claimOf(*e, npc) : nullptr;
        return c && hasToken(*e, c->target, npc);
    }

    // ── Looking things up ─────────────────────────────────
    Encounter* of(EntityId npc) {
        for (auto& e : list) if (e.ended < 0.0f && e.has(e.npcs, npc) && e.has(e.active, npc)) return &e;
        return nullptr;
    }
    Encounter* ofPlayer(EntityId player) {
        for (auto& e : list) if (e.ended < 0.0f && e.has(e.players, player)) return &e;
        return nullptr;
    }
    Encounter* byId(uint32_t id) { for (auto& e : list) if (e.id == id) return &e; return nullptr; }
    const std::vector<Encounter>& all() const { return list; }
    std::vector<uint32_t> takeOpened() { std::vector<uint32_t> v; v.swap(opened); return v; }
    std::vector<std::pair<uint32_t, uint32_t>> takeMerged() { std::vector<std::pair<uint32_t, uint32_t>> v; v.swap(merged); return v; }
    void clear() { list.clear(); trails.clear(); opened.clear(); merged.clear(); }

private:
    std::vector<Encounter> list;
    std::unordered_map<EntityId, std::deque<Crumb>> trails;
    std::vector<uint32_t> opened;
    std::vector<std::pair<uint32_t, uint32_t>> merged;      // (into, from)
    uint32_t nextId = 1;

    Encounter* open() {
        Encounter e;
        e.id = nextId++;
        e.started = hooks.now ? hooks.now() : 0.0f;
        list.push_back(e);
        opened.push_back(e.id);
        return &list.back();
    }
    Encounter* merge(Encounter& a, Encounter& b) {
        bool aOlder = a.started < b.started || (a.started == b.started && a.id < b.id);
        uint32_t keep = aOlder ? a.id : b.id, drop = aOlder ? b.id : a.id;
        Encounter& k = *byId(keep);
        Encounter& d = *byId(drop);
        for (EntityId p : d.players) if (!k.has(k.players, p)) k.players.push_back(p);
        for (EntityId n : d.npcs) if (!k.has(k.npcs, n)) k.npcs.push_back(n);
        for (EntityId n : d.active) if (!k.has(k.active, n)) k.active.push_back(n);
        for (auto& s : d.enemyNames) k.enemyNames.push_back(s);
        for (auto& c : d.claims) k.claims.push_back(c);
        for (auto& [t, f] : d.front) if (!k.front.count(t)) k.front[t] = f;
        for (auto& [t, v] : d.tokens) for (EntityId n : v) k.tokens[t].push_back(n);
        for (auto& [p, t] : d.lastSeen) k.lastSeen[p] = std::max(k.lastSeen[p], t);
        for (auto& [p, t] : d.tally) {
            PlayerTally& x = k.tally[p];
            x.player = p; x.kills += t.kills; x.dealt += t.dealt; x.taken += t.taken; x.died |= t.died;
        }
        k.xpPool += d.xpPool; k.killed += d.killed; k.fled += d.fled;
        merged.push_back({ keep, drop });
        uint32_t keepId = keep;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Encounter& e) { return e.id == drop; }), list.end());
        opened.erase(std::remove(opened.begin(), opened.end(), drop), opened.end());
        return byId(keepId);
    }
    const Encounter::Claim* claimOf(Encounter& e, EntityId npc) {
        for (auto& c : e.claims) if (c.npc == npc) return &c;
        return nullptr;
    }
    bool hasToken(Encounter& e, EntityId target, EntityId npc) {
        auto it = e.tokens.find(target);
        return it != e.tokens.end() && std::find(it->second.begin(), it->second.end(), npc) != it->second.end();
    }
    void claimSlot(Encounter& e, EntityId npc, const NpcType* type, EntityId target) {
        for (auto& c : e.claims) if (c.npc == npc) { if (c.target == target) return; c.target = target; }
        e.claims.erase(std::remove_if(e.claims.begin(), e.claims.end(), [&](auto& c) { return c.npc == npc; }), e.claims.end());
        std::vector<Slot> prefs = type && !type->slots.empty() ? type->slots : std::vector<Slot>{ Slot::Front, Slot::Ring };
        auto taken = [&](Slot s) {
            if (s == Slot::Ring) return false;              // the ring holds any number
            for (auto& c : e.claims) if (c.target == target && c.slot == s) return true;
            return false;
        };
        Slot pick = Slot::Ring;
        for (Slot s : prefs) if (!taken(s)) { pick = s; break; }
        if (pick == Slot::Ring)                              // all of its favourites taken: any free one
            for (int i = 0; i < (int)Slot::Ring; i++) if (!taken((Slot)i)) { pick = (Slot)i; break; }
        e.claims.push_back({ npc, target, pick });
    }
    glm::vec2& frontFor(Encounter& e, EntityId target, glm::vec3 targetPos, glm::vec3 npcPos) {
        auto it = e.front.find(target);
        if (it != e.front.end()) return it->second;
        glm::vec2 off(npcPos.x - targetPos.x, npcPos.z - targetPos.z);   // the first to arrive defines it
        return e.front[target] = glm::length(off) > 1e-3f ? glm::normalize(off) : glm::vec2(0, 1);
    }
};

} // namespace encounter
