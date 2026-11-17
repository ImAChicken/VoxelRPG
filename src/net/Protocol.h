#pragma once
#include "Wire.h"
#include "../entity/Human.h"
#include "../chunk/Chunk.h"
#include "../game/Inventory.h"
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Protocol — what server and client say to each other
// ══════════════════════════════════════════════════════════
// Every message is one packet: a Msg byte, then its fields
// (net/Wire.h). The server is the authority; a client sends only
// what its player wants (Input) and asks for things (later:
// Request). VERSION goes up whenever a message changes shape: a
// client with another version is refused at Hello.
//
//   client → server
//     Hello       version, name, look            (reliable, once)
//     Input       the last few ticks' intents     (unreliable, every tick)
//     Bye                                        (reliable)
//     Loadout     how you've arranged your inventory: every slot,
//                 what's equipped, the selected slots, weapon drawn.
//                 The server accepts it only if it holds exactly
//                 the items the server has for you (moved, not made)
//   server → client
//     Welcome     your id, the world's settings (world.vxrp), the
//                 server's tick, where you are    (reliable, once)
//     Refused     why                             (reliable, then closed)
//     ChunkData   a column that differs from what the seed makes
//     Edits       voxels changed (x, y, z, block)
//     Structures  every structure instance (structures.dat text)
//     Spawn       a body comes into view: id, kind, name, type, look
//     Despawn     it's gone (out of range, removed, left)
//     Snapshot    the server's tick, your last input it used, the
//                 time of day, and every body in view    (unreliable, 20 Hz)
//     Event       something happened (a hit, a death …)
//     Notice      a line of text for the player
//     PlayerState your inventory as the server has it (after a
//                 pickup, or a Loadout it refused)
//     Modified    the columns that differ from the seed: the client
//                 waits for their ChunkData instead of generating them
namespace proto {

constexpr uint32_t VERSION = 1;
constexpr int TICK_HZ = 60;            // the simulation
constexpr int SNAPSHOT_EVERY = 3;      // ticks between snapshots (20 Hz)
constexpr int INPUT_REDUNDANCY = 4;    // inputs repeated in each Input packet (loss)
constexpr float VIEW_RANGE = 220.0f;   // voxels: bodies further away aren't sent

enum class Msg : uint8_t {
    Hello = 1, Input = 2, Bye = 3, Loadout = 4,
    Welcome = 20, Refused = 21, ChunkData = 22, Edits = 23, Structures = 24,
    Spawn = 25, Despawn = 26, Snapshot = 27, Event = 28, Notice = 29, Modified = 30, PlayerState = 31,
};

// key=value text (player files, looks) ↔ a string
inline std::string kvText(const std::function<void(std::ostream&)>& write) {
    std::ostringstream s; write(s); return s.str();
}
inline void kvRead(const std::string& text, const std::function<void(const std::string&, const std::string&)>& read) {
    std::istringstream s(text);
    std::string line;
    while (std::getline(s, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) read(line.substr(0, eq), line.substr(eq + 1));
    }
}

// ── Intents ───────────────────────────────────────────────
inline void putIntent(wire::Writer& w, const HumanIntent& in) {
    uint16_t f = (in.sprint ? 1 : 0) | (in.crouch ? 2 : 0) | (in.jump ? 4 : 0) | (in.dodge ? 8 : 0) | (in.fly ? 16 : 0) |
                 (in.flyFast ? 32 : 0) | (in.attackLight ? 64 : 0) | (in.attackHeavy ? 128 : 0) | (in.guard ? 256 : 0);
    w.u16(f);
    w.vec2(in.move);
    w.f32(in.speedScale);
    if (in.fly) w.vec3(in.flyMove);
}
inline HumanIntent getIntent(wire::Reader& r) {
    HumanIntent in;
    uint16_t f = r.u16();
    in.sprint = f & 1; in.crouch = f & 2; in.jump = f & 4; in.dodge = f & 8; in.fly = f & 16;
    in.flyFast = f & 32; in.attackLight = f & 64; in.attackHeavy = f & 128; in.guard = f & 256;
    in.move = r.vec2();
    if (glm::length(in.move) > 1.001f) in.move = glm::normalize(in.move);        // no faster than walking allows
    in.speedScale = std::clamp(r.f32(), 0.0f, 1.0f);
    if (in.fly) in.flyMove = r.vec3();
    return in;
}

struct InputFrame {
    uint32_t    tick = 0;           // the client's tick it was made on
    HumanIntent intent;
    glm::vec3   facing{0, 0, -1};
    float       pitch = 0.0f;       // degrees (aim)
    uint32_t    viewTick = 0;       // the server tick the client was showing others at (lag compensation)
};
inline std::vector<uint8_t> input(const std::vector<InputFrame>& frames) {
    wire::Writer w; w.u8((uint8_t)Msg::Input);
    w.u8((uint8_t)frames.size());
    for (const InputFrame& f : frames) { w.u32(f.tick); putIntent(w, f.intent); w.vec3(f.facing); w.f32(f.pitch); w.u32(f.viewTick); }
    return w.buf;
}
inline std::vector<InputFrame> readInput(wire::Reader& r) {
    std::vector<InputFrame> out;
    int n = r.u8();
    for (int i = 0; i < n && r.ok() && i < 16; i++) {
        InputFrame f; f.tick = r.u32(); f.intent = getIntent(r); f.facing = r.vec3(); f.pitch = r.f32(); f.viewTick = r.u32();
        if (r.ok()) out.push_back(f);
    }
    return out;
}

// ── Hello / Welcome ───────────────────────────────────────
struct Hello { uint32_t version = VERSION; std::string name, look; };
inline std::vector<uint8_t> hello(const Hello& h) {
    wire::Writer w; w.u8((uint8_t)Msg::Hello); w.u32(h.version); w.str(h.name); w.str(h.look); return w.buf;
}
inline Hello readHello(wire::Reader& r) { Hello h; h.version = r.u32(); h.name = r.str(64); h.look = r.str(4096); return h; }

struct Welcome {
    uint64_t    you = 0;
    std::string worldName, worldInfo;   // world.vxrp text: the client builds the same generator
    std::string player;                  // your player file (key=value): position, inventory, mode …
    uint32_t    serverTick = 0;
};
inline std::vector<uint8_t> welcome(const Welcome& m) {
    wire::Writer w; w.u8((uint8_t)Msg::Welcome); w.u64(m.you); w.str(m.worldName); w.str(m.worldInfo); w.str(m.player); w.u32(m.serverTick);
    return w.buf;
}
inline Welcome readWelcome(wire::Reader& r) {
    Welcome m; m.you = r.u64(); m.worldName = r.str(256); m.worldInfo = r.str(1 << 16); m.player = r.str(1 << 16); m.serverTick = r.u32(); return m;
}

inline std::vector<uint8_t> text(Msg kind, const std::string& s) {
    wire::Writer w; w.u8((uint8_t)kind); w.str(s); return w.buf;
}

// ── Voxels ────────────────────────────────────────────────
// A whole column: each section's runs (chunk/Chunk.h)
inline std::vector<uint8_t> chunkData(int cx, int cz, Chunk& ch) {
    wire::Writer w; w.u8((uint8_t)Msg::ChunkData); w.u32((uint32_t)cx); w.u32((uint32_t)cz);
    w.u8((uint8_t)SECTIONS);
    for (int sct = 0; sct < SECTIONS; sct++) {
        auto rle = ch.snapshot(sct);
        w.var(rle->ends.size());
        for (size_t i = 0; i < rle->ends.size(); i++) { w.var(rle->ends[i]); w.u16(rle->types[i]); }
    }
    return w.buf;
}
inline bool readChunkData(wire::Reader& r, int& cx, int& cz, Chunk& out) {
    cx = (int)r.u32(); cz = (int)r.u32();
    if (r.u8() != (uint8_t)SECTIONS) return false;
    for (int sct = 0; sct < SECTIONS; sct++) {
        auto rle = std::make_shared<RleData>();
        uint64_t n = r.var();
        if (!r.ok() || n > (uint64_t)VOXEL_COUNT) return false;
        rle->ends.resize((size_t)n); rle->types.resize((size_t)n);
        uint32_t last = 0;
        for (size_t i = 0; i < n; i++) {
            rle->ends[i] = (uint32_t)r.var(); rle->types[i] = r.u16();
            if (i > 0 && rle->ends[i] <= last) return false;      // runs only go forward
            if (rle->types[i] >= gRegistry.size()) rle->types[i] = BLOCK_AIR;   // a block this client doesn't know
            last = rle->ends[i];
        }
        if (!r.ok() || last > (uint32_t)VOXEL_COUNT) return false;
        out.section(sct).setRle(rle);
    }
    return true;
}

struct VoxelEdit { glm::ivec3 p; uint16_t block; };
inline std::vector<uint8_t> edits(const std::vector<VoxelEdit>& e) {
    wire::Writer w; w.u8((uint8_t)Msg::Edits); w.var(e.size());
    for (const VoxelEdit& v : e) { w.ivec3(v.p); w.u16(v.block); }
    return w.buf;
}
inline std::vector<VoxelEdit> readEdits(wire::Reader& r) {
    std::vector<VoxelEdit> out;
    uint64_t n = r.var();
    if (n > (1u << 22)) return out;
    for (uint64_t i = 0; i < n && r.ok(); i++) { VoxelEdit v; v.p = r.ivec3(); v.block = r.u16(); out.push_back(v); }
    return out;
}

inline std::vector<uint8_t> modified(const std::vector<std::pair<int, int>>& cols) {
    wire::Writer w; w.u8((uint8_t)Msg::Modified); w.var(cols.size());
    for (auto& [cx, cz] : cols) { w.u32((uint32_t)cx); w.u32((uint32_t)cz); }
    return w.buf;
}
inline std::vector<std::pair<int, int>> readModified(wire::Reader& r) {
    std::vector<std::pair<int, int>> out;
    uint64_t n = r.var();
    for (uint64_t i = 0; i < n && i < (1u << 20) && r.ok(); i++) { int cx = (int)r.u32(); out.push_back({ cx, (int)r.u32() }); }
    return out;
}

// ── Bodies ────────────────────────────────────────────────
struct SpawnMsg {
    uint64_t    id = 0;
    uint8_t     kind = 0;            // 0 player, 1 NPC
    std::string name, type, look;    // look: Appearance as key=value text
    std::string gear;                // players: the worn items' names, space separated (sent again when it changes)
};
inline std::vector<uint8_t> spawn(const SpawnMsg& s) {
    wire::Writer w; w.u8((uint8_t)Msg::Spawn); w.u64(s.id); w.u8(s.kind); w.str(s.name); w.str(s.type); w.str(s.look); w.str(s.gear); return w.buf;
}
inline SpawnMsg readSpawn(wire::Reader& r) {
    SpawnMsg s; s.id = r.u64(); s.kind = r.u8(); s.name = r.str(128); s.type = r.str(128); s.look = r.str(4096); s.gear = r.str(1024); return s;
}
inline std::vector<uint8_t> despawn(uint64_t id) { wire::Writer w; w.u8((uint8_t)Msg::Despawn); w.u64(id); return w.buf; }

// Everything a body's animation needs (character/HumanAnim.h),
// and its health
struct BodyState {
    uint64_t    id = 0;
    glm::vec3   pos{0}, vel{0}, facing{0, 0, -1};
    float       health = 100, maxHealth = 100, stamina = 100;
    uint16_t    flags = 0;           // see F_*
    uint8_t     move = 0;            // Human::Move
    float       moveTimer = 0;
    std::string attack, held;        // attack id ("" = none), held model id
    float       attackT = 0;
    glm::vec3   attackDir{0, 0, -1};
    enum : uint16_t { F_GROUNDED = 1, F_CROUCH = 2, F_SPRINT = 4, F_FLY = 8, F_GUARD = 16, F_DEAD = 32, F_COMBAT = 64, F_LIMP = 128 };
};
inline void putBody(wire::Writer& w, const BodyState& b) {
    w.u64(b.id); w.vec3(b.pos); w.vec3(b.vel); w.vec3(b.facing);
    w.f32(b.health); w.f32(b.maxHealth); w.f32(b.stamina);
    w.u16(b.flags); w.u8(b.move); w.f32(b.moveTimer);
    w.str(b.held); w.str(b.attack);
    if (!b.attack.empty()) { w.f32(b.attackT); w.vec3(b.attackDir); }
}
inline BodyState getBody(wire::Reader& r) {
    BodyState b; b.id = r.u64(); b.pos = r.vec3(); b.vel = r.vec3(); b.facing = r.vec3();
    b.health = r.f32(); b.maxHealth = r.f32(); b.stamina = r.f32();
    b.flags = r.u16(); b.move = r.u8(); b.moveTimer = r.f32();
    b.held = r.str(64); b.attack = r.str(64);
    if (!b.attack.empty()) { b.attackT = r.f32(); b.attackDir = r.vec3(); }
    return b;
}

struct Snapshot {
    uint32_t serverTick = 0;
    uint32_t ackTick = 0;            // the last of your inputs the server used
    float    clock = 0;              // time of day
    BodyState you;
    std::vector<BodyState> bodies;   // everyone else in view
};
inline std::vector<uint8_t> snapshot(const Snapshot& s) {
    wire::Writer w; w.u8((uint8_t)Msg::Snapshot); w.u32(s.serverTick); w.u32(s.ackTick); w.f32(s.clock);
    putBody(w, s.you);
    w.var(s.bodies.size());
    for (const BodyState& b : s.bodies) putBody(w, b);
    return w.buf;
}
inline Snapshot readSnapshot(wire::Reader& r) {
    Snapshot s; s.serverTick = r.u32(); s.ackTick = r.u32(); s.clock = r.f32(); s.you = getBody(r);
    uint64_t n = r.var();
    for (uint64_t i = 0; i < n && i < 4096 && r.ok(); i++) s.bodies.push_back(getBody(r));
    return s;
}

// ── Inventory ─────────────────────────────────────────────
struct LoadoutMsg {
    ItemStack slots[Inventory::SLOTS];
    int  equip[EQUIP_SLOTS];
    int  selected = Inventory::FIRST_MAIN, offhand = -1, activeWeapon = 0;
    bool combatStance = false;
};
inline std::vector<uint8_t> loadout(const Inventory& inv, bool combatStance) {
    wire::Writer w; w.u8((uint8_t)Msg::Loadout);
    for (const ItemStack& s : inv.slots) { w.u16(s.itemID); w.u16(s.count); }
    for (int e : inv.equip) w.u8((uint8_t)(int8_t)e);
    w.u8((uint8_t)inv.selected); w.u8((uint8_t)(int8_t)inv.offhand); w.u8((uint8_t)inv.activeWeapon); w.boolean(combatStance);
    return w.buf;
}
inline LoadoutMsg readLoadout(wire::Reader& r) {
    LoadoutMsg m;
    for (ItemStack& s : m.slots) { s.itemID = r.u16(); s.count = r.u16(); }
    for (int& e : m.equip) e = (int8_t)r.u8();
    m.selected = r.u8(); m.offhand = (int8_t)r.u8(); m.activeWeapon = r.u8() ? 1 : 0; m.combatStance = r.boolean();
    return m;
}
// Does the arrangement hold exactly the items 'have' does, in
// legal stacks? Then take it (the server's check on a Loadout).
inline bool applyLoadout(Inventory& have, const LoadoutMsg& m) {
    std::map<uint16_t, int> a, b;
    for (const ItemStack& s : have.slots) if (!s.empty()) a[s.itemID] += s.count;
    for (const ItemStack& s : m.slots) {
        if (s.count == 0 || s.itemID == 0) continue;
        const ItemDef& d = gItems.get(s.itemID);
        if (!d.valid() || s.count > std::max<int>(1, d.maxStack)) return false;
        b[s.itemID] += s.count;
    }
    if (a != b) return false;
    for (int i = 0; i < Inventory::SLOTS; i++) have.slots[i] = (m.slots[i].count && m.slots[i].itemID) ? m.slots[i] : ItemStack();
    for (int e = 0; e < EQUIP_SLOTS; e++) {
        int s = m.equip[e];
        have.equip[e] = (Inventory::valid(s) && !have.slots[s].empty() && Inventory::fits(have.slots[s].itemID, (EquipSlot)e)) ? s : -1;
    }
    have.select(m.selected);
    have.offhand = (m.offhand >= 0 && m.offhand < Inventory::OFFHAND && !have.slots[m.offhand].empty()) ? m.offhand : -1;
    have.activeWeapon = m.activeWeapon;
    return true;
}

// ── Events ────────────────────────────────────────────────
struct EventMsg {
    uint8_t  kind = 0;               // SimEvent::Kind
    uint64_t a = 0, b = 0;
    uint8_t  attack = 0;
    float    amount = 0;
    uint16_t flags = 0;              // 1 blocked, 2 guardBroken, 4 poiseBroken, 8 knockedDown, 16 parried, 32 riposte
};
inline std::vector<uint8_t> event(const EventMsg& e) {
    wire::Writer w; w.u8((uint8_t)Msg::Event); w.u8(e.kind); w.u64(e.a); w.u64(e.b); w.u8(e.attack); w.f32(e.amount); w.u16(e.flags);
    return w.buf;
}
inline EventMsg readEvent(wire::Reader& r) {
    EventMsg e; e.kind = r.u8(); e.a = r.u64(); e.b = r.u64(); e.attack = r.u8(); e.amount = r.f32(); e.flags = r.u16(); return e;
}

} // namespace proto
