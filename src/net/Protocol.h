#pragma once
#include "Wire.h"
#include "../entity/Human.h"
#include "../chunk/Chunk.h"
#include "../game/Inventory.h"
#include "../game/CombatKit.h"
#include "../game/WorldActions.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Protocol — what server and client say to each other
// ══════════════════════════════════════════════════════════
// Every message is one packet: a Msg byte, then its fields
// (net/Wire.h). The server is the authority; a client sends only
// what its player wants (Input) and asks for changes to the
// world (Request). VERSION goes up whenever a message changes
// shape: a client with another version is refused at Hello.
//
// Blocks and items travel as numbers, but two builds can number
// them differently (a block added to blocks.toml, an item moved
// in items.toml). So the Welcome carries the server's names for
// its numbers, and the client turns every number it's sent, and
// every one it sends, into the other side's (IdMap). Only a
// block or item one side has never heard of is lost: a block
// becomes stone if it was solid, else air, and the client is
// told which names it's missing.
//
//   client → server
//     Hello       version, name, look            (reliable, once)
//     Input       the last few ticks' intents     (unreliable, every tick)
//     Bye                                        (reliable)
//     Request     a change to the world: break, place, build,
//                 hoe, blast, door … (game/WorldActions.h). The
//                 server checks it and does it        (reliable)
//     Loadout     how you've arranged your inventory: every slot,
//                 what's equipped, the selected slots, weapon drawn.
//                 The server accepts it only if it holds exactly
//                 the items the server has for you (moved, not made).
//                 Also each loadout's off hand (a shield guards)
//   server → client
//     Welcome     your id, the world's settings (world.vxrp), the
//                 server's tick, where you are, its names for
//                 block and item numbers           (reliable, once)
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

constexpr uint32_t VERSION = 4;       // 2: Request, the names in Welcome; 3: off hands (Loadout, BodyState); 4: a piece's spec in Request
constexpr int TICK_HZ = 60;            // the simulation
constexpr int SNAPSHOT_EVERY = 3;      // ticks between snapshots (20 Hz)
constexpr int INPUT_REDUNDANCY = 4;    // inputs repeated in each Input packet (loss)
constexpr float VIEW_RANGE = 220.0f;   // voxels: bodies further away aren't sent

enum class Msg : uint8_t {
    Hello = 1, Input = 2, Bye = 3, Loadout = 4, Request = 5,
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

// ── Names for numbers ─────────────────────────────────────
struct Names {
    struct Block { uint16_t id; std::string name; bool solid; };
    struct Item  { uint16_t id; std::string name; };
    std::vector<Block> blocks;
    std::vector<Item>  items;
};
// This build's (the server sends its own)
inline Names localNames() {
    Names n;
    for (size_t i = 1; i < gRegistry.size(); i++) {
        const VoxelTypeDef& d = gRegistry.get((uint16_t)i);
        if (d.id == i && !d.name.empty()) n.blocks.push_back({ (uint16_t)i, d.name, d.isSolid() });
    }
    for (const ItemDef& d : gItems.all())
        if (d.valid()) n.items.push_back({ d.id, d.name });
    return n;
}
inline void putNames(wire::Writer& w, const Names& n) {
    w.var(n.blocks.size());
    for (const auto& b : n.blocks) { w.u16(b.id); w.str(b.name); w.boolean(b.solid); }
    w.var(n.items.size());
    for (const auto& it : n.items) { w.u16(it.id); w.str(it.name); }
}
inline Names getNames(wire::Reader& r) {
    Names n;
    uint64_t nb = r.var();
    for (uint64_t i = 0; i < nb && i < 65536 && r.ok(); i++) { Names::Block b; b.id = r.u16(); b.name = r.str(128); b.solid = r.boolean(); n.blocks.push_back(b); }
    uint64_t ni = r.var();
    for (uint64_t i = 0; i < ni && i < 65536 && r.ok(); i++) { Names::Item it; it.id = r.u16(); it.name = r.str(128); n.items.push_back(it); }
    return n;
}

// The client's: server numbers ↔ its own. Empty (before the
// Welcome, or the server's own side): numbers pass unchanged.
struct IdMap {
    std::vector<uint16_t> blockIn, itemIn;                // server id → ours (0: none)
    std::unordered_map<uint16_t, uint16_t> itemOut;       // ours → the server's
    std::vector<std::string> missingBlocks, missingItems; // names the server has and we don't
    std::vector<std::string> extraItems;                  // ours the server doesn't have
    bool active = false;
    int  renumbered = 0;                                  // known to both, under another number

    void build(const Names& server) {
        *this = IdMap();
        active = true;
        uint16_t top = 0;
        for (const auto& b : server.blocks) top = std::max(top, b.id);
        blockIn.assign((size_t)top + 1, 0);
        for (const auto& b : server.blocks) {
            uint16_t ours = gRegistry.getIDByName(b.name);
            if (!ours && b.name != "air") { missingBlocks.push_back(b.name); ours = b.solid ? BLOCK_STONE : BLOCK_AIR; }
            else if (ours != b.id) renumbered++;
            blockIn[b.id] = ours;
        }
        top = 0;
        for (const auto& it : server.items) top = std::max(top, it.id);
        itemIn.assign((size_t)top + 1, 0);
        for (const auto& it : server.items) {
            uint16_t ours = gItems.find(it.name);
            if (!ours) { missingItems.push_back(it.name); continue; }
            if (ours != it.id) renumbered++;
            itemIn[it.id] = ours;
            itemOut[ours] = it.id;
        }
        for (const ItemDef& d : gItems.all())
            if (d.valid() && !itemOut.count(d.id)) extraItems.push_back(d.name);
    }
    uint16_t block(uint16_t s) const {
        if (!active) return s < gRegistry.size() ? s : (uint16_t)BLOCK_AIR;
        return s < blockIn.size() ? blockIn[s] : (uint16_t)BLOCK_AIR;
    }
    uint16_t item(uint16_t s) const {
        if (!active) return s;
        return s < itemIn.size() ? itemIn[s] : 0;
    }
    uint16_t itemToServer(uint16_t ours) const {
        if (!active || !ours) return ours;
        auto it = itemOut.find(ours);
        return it == itemOut.end() ? 0 : it->second;
    }
};

// ── Intents ───────────────────────────────────────────────
//   ids   the client's IdMap: the item used, as the server numbers it
inline void putIntent(wire::Writer& w, const HumanIntent& in, const IdMap* ids = nullptr) {
    uint16_t use = ids ? ids->itemToServer(in.useItem) : in.useItem;
    uint16_t f = (in.sprint ? 1 : 0) | (in.crouch ? 2 : 0) | (in.jump ? 4 : 0) | (in.dodge ? 8 : 0) | (in.fly ? 16 : 0) |
                 (in.flyFast ? 32 : 0) | (in.attackLight ? 64 : 0) | (in.attackHeavy ? 128 : 0) | (in.guard ? 256 : 0) |
                 (use ? 512 : 0);
    w.u16(f);
    w.vec2(in.move);
    w.f32(in.speedScale);
    if (in.fly) w.vec3(in.flyMove);
    if (use) w.u16(use);
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
    if (f & 512) in.useItem = r.u16();
    return in;
}

struct InputFrame {
    uint32_t    tick = 0;           // the client's tick it was made on
    HumanIntent intent;
    glm::vec3   facing{0, 0, -1};
    float       pitch = 0.0f;       // degrees (aim)
    uint32_t    viewTick = 0;       // the server tick the client was showing others at (lag compensation)
};
inline std::vector<uint8_t> input(const std::vector<InputFrame>& frames, const IdMap* ids = nullptr) {
    wire::Writer w; w.u8((uint8_t)Msg::Input);
    w.u8((uint8_t)frames.size());
    for (const InputFrame& f : frames) { w.u32(f.tick); putIntent(w, f.intent, ids); w.vec3(f.facing); w.f32(f.pitch); w.u32(f.viewTick); }
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
    Names       names;                   // the server's block and item numbers
};
inline std::vector<uint8_t> welcome(const Welcome& m) {
    wire::Writer w; w.u8((uint8_t)Msg::Welcome); w.u64(m.you); w.str(m.worldName); w.str(m.worldInfo); w.str(m.player); w.u32(m.serverTick);
    putNames(w, m.names);
    return w.buf;
}
inline Welcome readWelcome(wire::Reader& r) {
    Welcome m; m.you = r.u64(); m.worldName = r.str(256); m.worldInfo = r.str(1 << 16); m.player = r.str(1 << 16); m.serverTick = r.u32();
    m.names = getNames(r);
    return m;
}

// ── Requests (game/WorldActions.h) ────────────────────────
// Items as the server numbers them; a piece by its name; a
// blueprint as its text (a player's copies are only on their
// computer)
inline std::vector<uint8_t> request(const WorldRequest& q, const IdMap* ids = nullptr) {
    wire::Writer w; w.u8((uint8_t)Msg::Request);
    w.u8((uint8_t)q.op); w.u16(ids ? ids->itemToServer(q.item) : q.item);
    w.ivec3(q.pos); w.ivec3(q.normal); w.ivec3(q.lo); w.ivec3(q.hi);
    w.u8((uint8_t)(q.rot & 3)); w.u8((uint8_t)q.mode);
    w.str(q.name);
    w.boolean(q.style.rollLoot); w.boolean(q.style.sitePrep); w.u8((uint8_t)std::clamp(q.style.sink, 0, 255));
    w.var(q.style.materials.size());
    for (auto& [k, v] : q.style.materials) { w.str(k); w.str(v); }
    w.vec3(q.point); w.vec3(q.dir); w.f32(q.speed);
    w.str(q.spec); w.str(q.style.palette.text());
    return w.buf;
}
inline bool readRequest(wire::Reader& r, WorldRequest& q) {
    uint8_t op = r.u8();
    if (op == 0 || op >= (uint8_t)WorldRequest::Op::COUNT) return false;
    q.op = (WorldRequest::Op)op; q.item = r.u16();
    q.pos = r.ivec3(); q.normal = glm::clamp(r.ivec3(), glm::ivec3(-1), glm::ivec3(1)); q.lo = r.ivec3(); q.hi = r.ivec3();
    q.rot = r.u8() & 3; q.mode = r.u8();
    q.name = r.str(1 << 20);
    q.style.rollLoot = r.boolean(); q.style.sitePrep = r.boolean(); q.style.sink = std::min<int>(r.u8(), 24);
    uint64_t n = r.var();
    for (uint64_t i = 0; i < n && i < 64 && r.ok(); i++) { std::string k = r.str(64); q.style.materials[k] = r.str(64); }
    q.point = r.vec3(); q.dir = r.vec3(); q.speed = r.f32();
    q.spec = r.str(1024); q.style.palette = PieceSpec::parse(r.str(1024));
    if (!std::isfinite(q.speed) || !std::isfinite(q.point.x + q.point.y + q.point.z) || !std::isfinite(q.dir.x + q.dir.y + q.dir.z)) return false;
    q.speed = std::clamp(q.speed, 0.0f, 400.0f);
    return r.ok();
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
inline bool readChunkData(wire::Reader& r, int& cx, int& cz, Chunk& out, const IdMap* ids = nullptr) {
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
            rle->types[i] = ids ? ids->block(rle->types[i])                        // the server's number → ours
                          : rle->types[i] < gRegistry.size() ? rle->types[i] : (uint16_t)BLOCK_AIR;
            last = rle->ends[i];
        }
        if (!r.ok() || last > (uint32_t)VOXEL_COUNT) return false;
        rle->indexRuns();
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
inline std::vector<VoxelEdit> readEdits(wire::Reader& r, const IdMap* ids = nullptr) {
    std::vector<VoxelEdit> out;
    uint64_t n = r.var();
    if (n > (1u << 22)) return out;
    for (uint64_t i = 0; i < n && r.ok(); i++) { VoxelEdit v; v.p = r.ivec3(); v.block = r.u16(); if (ids) v.block = ids->block(v.block); out.push_back(v); }
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
    std::string off;                 // the left hand's model id (a shield …)
    float       attackT = 0;
    glm::vec3   attackDir{0, 0, -1};
    enum : uint16_t { F_GROUNDED = 1, F_CROUCH = 2, F_SPRINT = 4, F_FLY = 8, F_GUARD = 16, F_DEAD = 32, F_COMBAT = 64, F_LIMP = 128 };
};
inline void putBody(wire::Writer& w, const BodyState& b) {
    w.u64(b.id); w.vec3(b.pos); w.vec3(b.vel); w.vec3(b.facing);
    w.f32(b.health); w.f32(b.maxHealth); w.f32(b.stamina);
    w.u16(b.flags); w.u8(b.move); w.f32(b.moveTimer);
    w.str(b.held); w.str(b.attack); w.str(b.off);
    if (!b.attack.empty()) { w.f32(b.attackT); w.vec3(b.attackDir); }
}
inline BodyState getBody(wire::Reader& r) {
    BodyState b; b.id = r.u64(); b.pos = r.vec3(); b.vel = r.vec3(); b.facing = r.vec3();
    b.health = r.f32(); b.maxHealth = r.f32(); b.stamina = r.f32();
    b.flags = r.u16(); b.move = r.u8(); b.moveTimer = r.f32();
    b.held = r.str(64); b.attack = r.str(64); b.off = r.str(64);
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
    uint16_t kitOff[CombatKit::SETS] = {};   // each loadout's off hand (item ids)
};
inline std::vector<uint8_t> loadout(const Inventory& inv, bool combatStance, const IdMap* ids = nullptr,
                                    const CombatKit* kit = nullptr) {
    wire::Writer w; w.u8((uint8_t)Msg::Loadout);
    for (const ItemStack& s : inv.slots) { w.u16(ids ? ids->itemToServer(s.itemID) : s.itemID); w.u16(s.count); }
    for (int e : inv.equip) w.u8((uint8_t)(int8_t)e);
    w.u8((uint8_t)inv.selected); w.u8((uint8_t)(int8_t)inv.offhand); w.u8((uint8_t)inv.activeWeapon); w.boolean(combatStance);
    for (int i = 0; i < CombatKit::SETS; i++) {
        uint16_t off = kit ? kit->sets[(size_t)i].off : 0;
        w.u16(ids ? ids->itemToServer(off) : off);
    }
    return w.buf;
}
inline LoadoutMsg readLoadout(wire::Reader& r) {
    LoadoutMsg m;
    for (ItemStack& s : m.slots) { s.itemID = r.u16(); s.count = r.u16(); }
    for (int& e : m.equip) e = (int8_t)r.u8();
    m.selected = r.u8(); m.offhand = (int8_t)r.u8(); m.activeWeapon = r.u8() ? 1 : 0; m.combatStance = r.boolean();
    for (uint16_t& o : m.kitOff) o = r.u16();
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
