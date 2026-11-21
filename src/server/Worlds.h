#pragma once
// ══════════════════════════════════════════════════════════
// Worlds — every world open at once
// ══════════════════════════════════════════════════════════
// docs/dungeons.md step 7 ("Servers", "WorldInstance"): a realm's main
// world, its instances, a hub … each a GameSession of its own (its
// settings, size, clock, simulation, NPCs, structures, saving), all
// running together. The game draws one of them (the one its player is
// in: the focus); the rest run headless here, like a server's world:
//
//   open(name)       open a world (or the one already open)
//   tick(dt, focus)  every other world: its simulation, its own
//                    changes, its dungeon; chunks kept loaded round
//                    its players and anything watched (a gate someone
//                    is walking toward, so its far side is there before
//                    they step through)
//   watch(name, p)   keep the ground round p loaded (this frame)
//   move(player)     a player from one world's simulation to another's
//
// A world nobody is in and nothing watches sleeps; after SLEEP_UNLOAD
// s it's saved and closed (unless pinned: the realm's main world).
//
// The lobby is an empty session for when no world is open (the main
// menu): the game's player lives in its simulation until a world opens.

#include "GameSession.h"
#include "ChunkKeeper.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Worlds {
public:
    static constexpr float SLEEP_UNLOAD = 60.0f;     // s with nobody in it and nothing watched

    struct Entry {
        std::unique_ptr<GameSession> session;
        std::unique_ptr<ChunkKeeper> keeper;          // background loading (not for the focus: the game streams it)
        std::vector<glm::vec3> watched;               // this frame
        float idle = 0.0f;
        bool  pinned = false;                         // never unloaded by sleeping
        bool  focused = false;
        bool  streamed = false;                       // the game streams it too (a portal's far side): no keeper
    };

    std::function<void(const std::string&)> log = [](const std::string&) {};

    GameSession& lobby() { return lobbySession; }

    // Open a world by name (begin, generator, attach); the one already
    // open if it is. nullptr with 'why' if it can't be
    GameSession* open(const std::string& name, bool dev, std::string* why = nullptr) {
        if (GameSession* s = get(name)) return s;
        auto e = std::make_unique<Entry>();
        e->session = std::make_unique<GameSession>();
        e->session->log = log;
        if (!e->session->begin(name, dev)) { if (why) *why = "no world '" + name + "'"; return nullptr; }
        e->session->buildGenerator();
        e->session->attach();
        GameSession* s = e->session.get();
        list.push_back(std::move(e));
        log("opened " + name);
        return s;
    }
    // A new world made on disk, then open (as GameSession::beginNew)
    GameSession* create(const std::string& name, uint64_t seed, GameMode mode, WorldType type, int size, int biome,
                        TerrainStyle terrain, const WorldGenParams& params, const FlatOptions& flat, bool dev) {
        close(name, false);
        auto e = std::make_unique<Entry>();
        e->session = std::make_unique<GameSession>();
        e->session->log = log;
        if (!e->session->beginNew(name, seed, mode, type, size, biome, terrain, params, flat, dev)) return nullptr;
        GameSession* s = e->session.get();
        list.push_back(std::move(e));
        return s;                                     // (the caller builds the generator and attaches: it may show progress)
    }
    // Begun but not attached yet (the game builds the generator on a
    // worker thread with a progress screen): the slot to fill
    GameSession* begin(const std::string& name, bool dev) {
        if (GameSession* s = get(name)) return s;
        auto e = std::make_unique<Entry>();
        e->session = std::make_unique<GameSession>();
        e->session->log = log;
        if (!e->session->begin(name, dev)) return nullptr;
        GameSession* s = e->session.get();
        list.push_back(std::move(e));
        return s;
    }

    GameSession* get(const std::string& name) {
        for (auto& e : list) if (e->session->name() == name) return e->session.get();
        return nullptr;
    }
    Entry* entryOf(const GameSession* s) {
        for (auto& e : list) if (e->session.get() == s) return e.get();
        return nullptr;
    }
    std::vector<GameSession*> all() {
        std::vector<GameSession*> out;
        for (auto& e : list) out.push_back(e->session.get());
        return out;
    }
    size_t count() const { return list.size(); }

    void pin(GameSession* s, bool on = true) { if (Entry* e = entryOf(s)) e->pinned = on; }
    // The world the game draws: it streams that one itself (no keeper)
    void setFocus(GameSession* s) {
        for (auto& e : list) {
            bool f = e->session.get() == s;
            if (f && e->keeper) { e->keeper->stop(); e->keeper.reset(); }
            e->focused = f;
            if (f) e->idle = 0.0f;
        }
    }
    // The game streams this world as well (it draws a portal's far side
    // in it): its own loader stops so the two don't fight over chunks
    void setStreamed(GameSession* s, bool on) {
        if (Entry* e = entryOf(s)) {
            e->streamed = on;
            if (on && e->keeper) { e->keeper->stop(); e->keeper.reset(); }
        }
    }
    void watch(const std::string& name, glm::vec3 p) { for (auto& e : list) if (e->session->name() == name) e->watched.push_back(p); }

    // A player from one world to another. Returns it in its new world
    static Player* move(EntityId id, GameSession& from, GameSession& to) {
        std::unique_ptr<Player> p = from.sim.takePlayer(id);
        if (!p) return nullptr;
        return &to.sim.adoptPlayer(std::move(p));
    }

    // A body through a portal into another world (Simulation::Transfer):
    // out of one simulation, into the other, moved and turned. A player
    // keeps its body; an NPC its brain (it carries on fighting there).
    // Returns it in its new world (nullptr if it couldn't go)
    static Human* transfer(GameSession& from, GameSession& to, const Simulation::Transfer& t) {
        glm::mat3 R(t.through);
        auto turn = [&](Human& h) {
            h.position = glm::vec3(t.through * glm::vec4(h.position, 1.0f));
            h.velocity = R * h.velocity;
            glm::vec3 f = R * h.facing; f.y = 0.0f;
            if (glm::length(f) > 1e-3f) h.facing = glm::normalize(f);
            if (h.attack.active()) { glm::vec3 d = R * h.attack.dir; d.y = 0.0f; if (glm::length(d) > 1e-3f) h.attack.dir = glm::normalize(d); }
            h.lastPortal = t.through;
            h.lastExitCentre = t.exitCentre; h.lastExitNormal = t.exitNormal; h.lastPortalFrom = from.name();
            h.portalTrips++;
            h.portalPrevSet = false;
        };
        if (std::unique_ptr<Player> p = from.sim.takePlayer(t.id)) {
            turn(*p);
            return &to.sim.adoptPlayer(std::move(p));
        }
        if (std::unique_ptr<Npc> n = from.sim.npcs().take(t.id)) {
            turn(*n);
            n->prevPosition = n->position;
            n->home = n->position;                    // (its home is over there now)
            Npc& m = to.sim.npcs().adopt(std::move(n));
            std::string st = m.stateName.empty() ? std::string("idle") : m.stateName;
            brain::change(m, to.sim, st);             // its state again, in this world (a fight: joins it here)
            return &m;
        }
        return nullptr;
    }
    // The bodies that went from a world through a portal to another open
    // world (its NPCs; on a server, its players)
    void moveNpcs(GameSession& from, const std::vector<Simulation::Transfer>& ts) {
        for (const Simulation::Transfer& t : ts) {
            GameSession* to = get(t.world);
            if (!to || !to->world) continue;
            transfer(from, *to, t);
        }
    }

    // ── Every frame: the worlds the game isn't showing ────
    //   steps   physics ticks to run this frame (the game's fixed step)
    void tick(float dt, int steps, float step) {
        for (size_t i = 0; i < list.size();) {
            Entry& e = *list[i];
            GameSession& s = *e.session;
            if (e.focused || !s.world) { e.watched.clear(); i++; continue; }
            // the ground round its players and what's watched
            std::vector<glm::vec3> pts = e.watched;
            for (auto& p : s.sim.players()) pts.push_back(p->position);
            if (!e.keeper && !pts.empty() && !e.streamed) e.keeper = std::make_unique<ChunkKeeper>(*s.world, s.chunkSource(), 1);
            if (e.keeper) e.keeper->update(pts);
            // its life goes on
            if (!s.sim.players().empty() || !e.watched.empty() || e.streamed) e.idle = 0.0f;
            else e.idle += dt;
            s.syncSpawners();
            std::vector<Human*> ps;
            for (auto& p : s.sim.players()) ps.push_back(p.get());
            s.tickDungeon(ps, dt);
            for (int k = 0; k < steps; k++) { s.sim.step(step); s.tickWorld(step); moveNpcs(s, s.sim.takeTransfers()); }
            s.sim.day.update(dt);
            s.updateEdits(2.0f);
            e.watched.clear();
            // asleep long enough: saved and closed
            if (!e.pinned && e.idle > SLEEP_UNLOAD) {
                log("closing " + s.name() + " (nobody there)");
                closeEntry(i, true);
                continue;
            }
            i++;
        }
    }

    // Save every open world (the focus's player file is the game's: extra)
    void saveAll(bool commit, const GameSession* skip = nullptr) {
        for (auto& e : list) if (e->session.get() != skip && e->session->open()) e->session->save(commit);
    }
    void close(const std::string& name, bool save) {
        for (size_t i = 0; i < list.size(); i++) if (list[i]->session->name() == name) { closeEntry(i, save); return; }
    }
    void close(GameSession* s, bool save) {
        for (size_t i = 0; i < list.size(); i++) if (list[i]->session.get() == s) { closeEntry(i, save); return; }
    }
    void closeAll(bool save) { while (!list.empty()) closeEntry(list.size() - 1, save); }

private:
    GameSession lobbySession;
    std::vector<std::unique_ptr<Entry>> list;
    void closeEntry(size_t i, bool save) {
        Entry& e = *list[i];
        if (e.keeper) { e.keeper->stop(); e.keeper.reset(); }
        if (save && e.session->open()) e.session->save(true);
        e.session->close();
        list.erase(list.begin() + (long)i);
    }
};
