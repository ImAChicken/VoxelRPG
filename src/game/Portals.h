#pragma once
// ══════════════════════════════════════════════════════════
// Portals in play: the gates, their menu, instances, travel
// ══════════════════════════════════════════════════════════
// docs/dungeons.md step 6, first version: one world loaded at a time,
// travel is a fade (main.cpp does the fade and reopens the world when
// 'travel' is pending). Single player only for now: a client of a
// server has no portals (the server would have to run more than one
// world: step 7).
//
//   the main world   a gate near the spawn (world.vxrp portal=on),
//                    built once its ground is loaded. Interact: the
//                    menu (open a dungeon / close it). Open while the
//                    realm has an instance; walk in: into its portal
//                    room
//   an instance      the portal room's gate, always open: walk in to
//                    go home (to the front of the main world's gate);
//                    the exit gate once the boss falls does the same.
//                    Interact: close it early
// Every instance counts down (world/Realm.h); the game shows the
// clock while you're in one, warns at 10 and 1 minutes, and moves you
// out when it closes.

#include "Gate.h"
#include "../server/GameSession.h"
#include "../world/Realm.h"
#include "../entity/Player.h"
#include <functional>
#include <random>
#include <string>
#include <vector>

class PortalDirector {
public:
    realm::Realm realm;

    // A trip asked for: main.cpp fades out, reopens 'world', puts the
    // player at the far gate, fades in; then 'thenRemove' (an instance
    // that closed) is deleted
    struct Travel {
        bool        pending = false;
        std::string world;          // the world's name ("<main>/instances/<id>" or "<main>")
        std::string here;           // realm.here once there
        bool        toEntry = false;// into an instance's portal room (else: the main world's gate)
        std::string thenRemove;
    } travel;

    // The menu at a gate (the talk box: Hud::talkingTo / talkOptions)
    struct Option { std::string label, action; };
    bool menuOpen = false;
    std::string menuTitle, menuLine;
    std::vector<Option> options;
    int sel = 0;

    float cooldown = 0.0f;           // s before a gate can take you again (just arrived)
    // an instance's world is about to be deleted: close it if it's open
    // (server/Worlds.h)
    std::function<void(const std::string& worldName)> beforeRemove;
    static constexpr float PRELOAD = 90.0f;   // vox from a gate: its far side opens and loads

    // The far sides worth having open now: the world past a gate you're
    // near (entry: an instance's portal room, its point found once open)
    struct Wanted { std::string world; glm::vec3 point{ 0.0f }; bool entry = false; };
    std::vector<Wanted> wanted(GameSession& s, glm::vec3 me) {
        std::vector<Wanted> out;
        if (!realm.loaded() || travel.pending) return out;
        if (!inInstance()) {
            realm::Instance* i = realm.gateInstance();
            if (i && realm.hasGate && glm::length(me - mainGate().centre()) < PRELOAD) out.push_back({ realm.worldName(*i), glm::vec3(0.0f), true });
        } else if (const dungeon::Run* r = s.dungeon()) {
            bool near = (r->hasEntryGate() && glm::length(me - r->entryGate().centre()) < PRELOAD) ||
                        (r->exitOpen() && glm::length(me - r->exitGateRef().centre()) < PRELOAD);
            if (near && realm.hasGate) out.push_back({ realm.main, mainGate().arrival(), false });
        }
        return out;
    }

    bool inInstance() const { return !realm.here.empty(); }

    // ── The main world's gate ─────────────────────────────
    Gate mainGate() const { Gate g; g.base = realm.gate; g.axis = realm.gateAxis; return g; }
    // Pick its place once (a few steps from the spawn, on dry ground
    // that's fairly level), build it once its ground is loaded, and keep
    // its veil open while there's an instance to go to
    void tickMainGate(GameSession& s, bool wanted) {
        if (!wanted || !s.world || !s.gen || !s.editor) return;
        if (!realm.hasGate) {
            glm::ivec3 sp = s.gen->spawnPoint(s.world->sizeX(), s.world->sizeZ());
            int sea = s.gen->seaLevel();
            int best = -1; float bestScore = 1e9f; glm::ivec3 bestAt(0); int bestAxis = 0;
            // (on the -x / -z side, so the side you come out on, +normal,
            // faces the spawn)
            const glm::ivec2 offs[] = { { 0, -26 }, { -26, 0 }, { -18, -18 }, { 18, -18 }, { 0, -38 }, { -38, 0 } };
            for (int i = 0; i < 6; i++) {
                int x = sp.x + offs[i].x, z = sp.z + offs[i].y;
                if (!s.world->isColumnLoaded(x, z)) return;      // wait for the ground
                int axis = std::abs(offs[i].x) > std::abs(offs[i].y) ? 1 : 0;
                int lo = 1 << 20, hi = -(1 << 20);
                for (int u = -8; u <= 8; u += 4) for (int d = -4; d <= 4; d += 4) {
                    int wx = axis == 0 ? x + u : x + d, wz = axis == 0 ? z + d : z + u;
                    int y = s.gen->getSurfaceY(wx, wz);
                    lo = std::min(lo, y); hi = std::max(hi, y);
                }
                if (sea >= 0 && lo < sea) continue;              // in the water
                float score = (float)(hi - lo) + 0.1f * i;
                if (score < bestScore) { bestScore = score; best = i; bestAt = glm::ivec3(x, hi + 1, z); bestAxis = axis; }
            }
            if (best < 0) { bestAt = sp + glm::ivec3(0, 0, -26); bestAxis = 0; }
            realm.gate = bestAt; realm.gateAxis = bestAxis; realm.hasGate = true; realm.gateBuilt = false;
            realm.save();
        }
        Gate g = mainGate();
        if (!g.loaded(*s.world)) return;
        bool open = realm.gateInstance() != nullptr && !seamlessMain;   // (seamless: an open arch, the portal shows the far side)
        if (!realm.gateBuilt || !g.isBuilt(*s.world)) {
            EditBatch b; b.source = EditSource::Script; b.label = "portal gate"; b.undoable = false;
            g.build(b, *s.world, open);
            s.editor->submit(std::move(b));
            realm.gateBuilt = true;
            realm.save();
            return;
        }
        if (g.isOpen(*s.world) != open) {
            EditBatch b; b.source = EditSource::Script; b.label = open ? "portal opens" : "portal shuts"; b.undoable = false;
            g.setOpen(b, *s.world, open);
            s.editor->submit(std::move(b));
        }
    }

    // ── Every frame (single player) ───────────────────────
    //   say   a line for the player
    void tick(float dt, GameSession& s, Player& p, bool portalsOn, const std::function<void(const std::string&)>& say) {
        cooldown = std::max(0.0f, cooldown - dt);
        if (!realm.loaded() || travel.pending) return;
        // the clocks
        for (const auto& e : realm.tick(dt)) {
            if (e.kind == realm::Realm::Event::Kind::Warning) { if (e.id == realm.here || !inInstance()) say(e.text); continue; }
            // closed: out of it, then it goes
            if (e.id == realm.here) { say(e.text + ". You're pulled back out."); goHome(e.id); return; }
            removeInstance(e.id);
            say(e.text);
        }
        if (!inInstance()) {
            tickMainGate(s, portalsOn);
            if (realm.hasGate && realm.gateBuilt && cooldown <= 0.0f && !p.dead() && !seamlessMain) {
                Gate g = mainGate();
                if (realm::Instance* i = realm.gateInstance(); i && g.touches(p)) goInto(*i);
            }
            return;
        }
        // in an instance: its boss beaten → the clock shortens
        realm::Instance* in = realm.current();
        if (!in) { goHome(""); return; }
        if (const dungeon::Run* r = s.dungeon()) {
            if (r->isCleared() && !in->cleared) {
                realm.beaten(in->id);
                say("The way out is open. " + nameOf(*in) + " closes in " + realm::Realm::clock(in->left) + ".");
            }
            if (cooldown <= 0.0f && !p.dead() && !seamlessInstance) {
                bool through = (r->hasEntryGate() && r->entryGate().touches(p)) || (r->exitOpen() && r->exitGateRef().touches(p));
                if (through) goHome("");
            }
        }
    }

    // The trip into an instance / back home
    void goInto(const realm::Instance& i) {
        travel = Travel();
        travel.pending = true;
        travel.world = realm.worldName(i);
        travel.here = i.id;
        travel.toEntry = true;
    }
    void goHome(const std::string& thenRemove) {
        travel = Travel();
        travel.pending = true;
        travel.world = realm.main;
        travel.here = "";
        travel.toEntry = false;
        travel.thenRemove = thenRemove;
    }
    // After main.cpp has opened the far world: where to stand
    glm::vec3 arrival(GameSession& s, glm::vec3& facing) {
        cooldown = 1.5f;
        realm.here = travel.here;
        realm.save();
        if (travel.toEntry) {
            if (const dungeon::Run* r = s.dungeon(); r && r->hasEntryGate()) {
                facing = r->entryGate().arrivalFacing();
                return r->entryGate().arrival();
            }
            facing = glm::vec3(0, 0, 1);
            return glm::vec3(s.gen->spawnPoint(s.world->sizeX(), s.world->sizeZ()));
        }
        if (realm.hasGate) { Gate g = mainGate(); facing = g.arrivalFacing(); return g.arrival(); }
        facing = glm::vec3(0, 0, 1);
        return glm::vec3(s.gen->spawnPoint(s.world->sizeX(), s.world->sizeZ()));
    }
    // Once there: anything left to do (an instance that closed is deleted)
    void arrived() {
        if (!travel.thenRemove.empty()) removeInstance(travel.thenRemove);
        travel = Travel();
    }

    // ── Seamless gates (world/PortalLinks.h) ──────────────
    // While both worlds are open (server/Worlds.h) and both gates stand,
    // a gate is a portal: its opening shows the room in front of the far
    // gate and walking in takes you there, no fade (the veil goes). Until
    // then (the far world still opening) the old way: the veil and a
    // fade (tick, travel). Either side of a gate's arch leads out of
    // the far gate's front (PortalEnd::Back::Turn). Kept up every frame for both worlds, so NPCs
    // in the one you're not in can follow you through too.
    //   world   a world open by name (nullptr if it isn't)
    bool seamlessMain = false, seamlessInstance = false;
    void syncGates(const std::function<GameSession*(const std::string&)>& world) {
        if (!realm.loaded()) { seamlessMain = seamlessInstance = false; return; }
        GameSession* M = world(realm.main);
        realm::Instance* inst = realm.gateInstance();
        GameSession* I = inst ? world(realm.worldName(*inst)) : nullptr;
        const dungeon::Run* run = I && I->world ? I->dungeon() : nullptr;
        Gate mg = mainGate();
        bool mainUp = M && M->world && realm.hasGate && realm.gateBuilt && mg.loaded(*M->world) && mg.isBuilt(*M->world);
        bool entryUp = run && run->hasEntryGate() && run->entryGate().loaded(*I->world) && run->entryGate().isBuilt(*I->world);
        bool both = mainUp && entryUp;
        auto link = [&](GameSession& s, const std::string& kind, const Gate& near, const Gate& far, const std::string& farWorld) {
            PortalEnd a = near.end(), b = far.end();
            b.world = farWorld;
            a.back = PortalEnd::Back::Turn;                        // (either side of the arch leads in: out of the far gate's front)
            PortalLink* l = s.portals.findKind(kind);
            if (l && l->a.centre == a.centre && l->b.centre == b.centre && l->b.world == farWorld) return;
            s.portals.removeKind(kind);
            s.portals.add(a, b, false, kind);
        };
        auto shut = [&](GameSession& s, const Gate& g) {          // the veil away: an open arch
            if (!g.isOpen(*s.world)) return;
            EditBatch b; b.source = EditSource::Script; b.label = "gate opens through"; b.undoable = false;
            g.setOpen(b, *s.world, false);
            s.editor->submit(std::move(b));
        };
        seamlessMain = both;
        seamlessInstance = both;
        if (M && M->world) { if (both) { link(*M, "gate", mg, run->entryGate(), I->name()); shut(*M, mg); } else M->portals.removeKind("gate"); }
        if (I && I->world) {
            if (both) {
                link(*I, "gate", run->entryGate(), mg, M->name()); shut(*I, run->entryGate());
                if (run->exitOpen() && run->exitGateRef().loaded(*I->world) && run->exitGateRef().isBuilt(*I->world)) {
                    link(*I, "exit", run->exitGateRef(), mg, M->name()); shut(*I, run->exitGateRef());
                }
            } else { I->portals.removeKind("gate"); I->portals.removeKind("exit"); }
        }
    }
    // Through a gate without a fade: now in 'world' (its name)
    void arrivedSeamless(const std::string& worldName) {
        cooldown = 1.0f;
        realm.here.clear();
        for (const realm::Instance& i : realm.instances) if (realm.worldName(i) == worldName) realm.here = i.id;
        realm.save();
    }

    // ── The menu ──────────────────────────────────────────
    // Interact pressed: true if a gate is close enough (the menu opens)
    //   look  where the camera looks (a gate further off counts when you look at it)
    bool interact(GameSession& s, const Player& p, glm::vec3 look = glm::vec3(0.0f)) {
        if (!gateInReach(s, p, look)) return false;
        openMenu();
        return true;
    }
    bool gateInReach(GameSession& s, const Player& p, glm::vec3 look = glm::vec3(0.0f)) const {
        if (!realm.loaded() || travel.pending) return false;
        if (!inInstance()) return realm.hasGate && realm.gateBuilt && mainGate().near(p.position, look);
        if (const dungeon::Run* r = s.dungeon())
            return (r->hasEntryGate() && r->entryGate().near(p.position, look)) || (r->exitOpen() && r->exitGateRef().near(p.position, look));
        return false;
    }
    void openMenu() {
        menuOpen = true; sel = 0; options.clear();
        menuTitle = "Portal";
        if (!inInstance()) {
            if (realm::Instance* i = realm.gateInstance()) {
                menuLine = "It opens onto the " + nameOf(*i) + " (" + realm::Realm::clock(i->left) + " left" + (i->closing ? ", closing" : "") +
                           "). Walk through to enter.";
                options.push_back({ "Close the " + nameOf(*i), "close" });
            } else {
                menuLine = "The gate is dark. Which dungeon will you open?";
                for (const realm::DungeonKind& k : realm::gDungeonKinds.all())
                    options.push_back({ "Open: " + k.name + "  (" + std::to_string((int)k.minutes) + " min)", "open:" + k.id });
            }
        } else if (realm::Instance* i = realm.current()) {
            menuLine = "The way home. The " + nameOf(*i) + " closes in " + realm::Realm::clock(i->left) + ".";
            if (!i->closing) options.push_back({ "Close it now (everyone out)", "close" });
        }
        options.push_back({ "Leave", "leave" });
    }
    // An option picked: what happened, for the player
    std::string choose(int i) {
        if (i < 0 || i >= (int)options.size()) return "";
        std::string a = options[(size_t)i].action, out;
        menuOpen = false;
        if (a.rfind("open:", 0) == 0) {
            std::random_device rd;
            uint64_t seed = ((uint64_t)rd() << 32) ^ rd();
            std::string why;
            if (realm::Instance* in = realm.create(a.substr(5), seed, &why))
                out = "The gate opens onto the " + nameOf(*in) + ". Walk through. (" + realm::Realm::clock(in->left) + ")";
            else out = "The gate won't open: " + why;
        } else if (a == "close") {
            realm::Instance* in = inInstance() ? realm.current() : realm.gateInstance();
            if (in) {
                if (inInstance()) {
                    realm.close(in->id);
                    out = "The " + nameOf(*in) + " is closing: everyone out in " + realm::Realm::clock(in->left) + ".";
                } else {
                    // nobody's inside (one player): it goes now
                    std::string n = nameOf(*in);
                    removeInstance(in->id);
                    out = "The " + n + " is closed. The gate goes dark.";
                }
            }
        }
        return out;
    }

    // An instance goes: its world closed (if open), its folder deleted
    void removeInstance(const std::string& id) {
        if (realm::Instance* i = realm.find(id); i && beforeRemove) beforeRemove(realm.worldName(*i));
        realm.remove(id);
    }

    // ── For the HUD ───────────────────────────────────────
    // "Undead Crypt  54:07" while in an instance ("" otherwise)
    std::string timerText() {
        realm::Instance* i = realm.current();
        if (!i) return "";
        return nameOf(*i) + "   " + realm::Realm::clock(i->left) + (i->closing ? "   closing" : i->cleared ? "   cleared" : "");
    }
    std::string nameOf(const realm::Instance& i) const {
        const realm::DungeonKind* k = realm.kindOf(i);
        return k ? k->name : i.kind;
    }
};
