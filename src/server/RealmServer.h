#pragma once
// ══════════════════════════════════════════════════════════
// RealmServer — a server running a realm: several worlds at once
// ══════════════════════════════════════════════════════════
// docs/dungeons.md step 7 "Servers". The dedicated server's worlds:
// the realm's main world (always open) and every instance its gate has
// opened (world/Realm.h), each a GameSession run by server/Worlds.h,
// each with its own ServerHost (the protocol for the players in it).
//
//   connections   all come in through the main world's host; a player
//                 whose file says it's in an open instance is handed to
//                 that world's host (ServerHost::route)
//   the gate      built by the main world's spawn (PortalDirector::
//                 tickMainGate) and joined to the open instance's
//                 entry gate as a seamless portal (syncGates) once both
//                 are loaded; walking through moves the body to the
//                 other world's simulation (Worlds::transfer) and its
//                 client joins again there (Msg::Travel: for now a short
//                 load on the client, not seamless)
//   the menu      a player at a gate picks from its menu in the game
//                 (Msg::Gate: open a dungeon, close it), checked here
//                 (near a gate of its world); each world's players are
//                 told the realm as they see it (Msg::RealmState: their
//                 gates, the instance, its clock; PortalDirector::
//                 readRemote), which also puts the clock on their HUD
//   instances     opened and closed at the gate or the server's console
//                 (portal open crypt | close); their clocks run here:
//                 warnings go to the players inside, and when one closes
//                 everyone in it is moved out to the main gate
//
// Not yet: the far side seen through the gate on a client (the client
// has one world).

#include "Worlds.h"
#include "ServerHost.h"
#include "../game/Portals.h"
#include <map>
#include <memory>
#include <random>
#include <string>

class RealmServer {
public:
    Worlds worlds;
    PortalDirector portals;
    std::function<void(const std::string&)> log = [](const std::string& s) { std::cout << s << "\n"; };
    // set before start(): every host made gets these
    int maxPlayers = 20;
    std::vector<std::string> starterKit;

    // The main world is open (worlds.open / create, generator built,
    // attached): its realm, its host, taking connections from l
    bool start(GameSession& mainWorld, bool dev, net::IListener& l) {
        listener = &l;
        main = &mainWorld;
        worlds.pin(main);
        worlds.log = log;
        realm::gDungeonKinds.load();
        portals.realm.load(main->name(), dev);
        portals.beforeRemove = [this](const std::string& worldName) {
            if (GameSession* s = worlds.get(worldName)) { evacuate(*s); dropHost(s); worlds.close(s, false); }
        };
        worlds.onPlayerMoved = [this](Player& p, GameSession& from, GameSession& to) { moved(p, from, to); };
        hostFor(*main);
        return true;
    }

    GameSession& mainWorld() { return *main; }
    ServerHost& mainHost() { return *hosts.at(main); }
    ServerHost* hostOf(GameSession* s) { auto it = hosts.find(s); return it == hosts.end() ? nullptr : it->second.get(); }
    size_t players() const { size_t n = 0; for (auto& h : hosts) n += h.second->players(); return n; }

    // One server tick (dt = one step)
    void tick(float dt) {
        // who's come and gone, and who belongs elsewhere
        for (auto& h : hosts) h.second->poll();
        for (auto& h : hosts)
            for (auto& ho : h.second->takeHandoffs())
                if (ho.to) ho.to->adopt(std::move(ho.client));
        // the realm: clocks, the gate, the instances' worlds open
        tickRealm(dt);
        // the ground round the spawn and the gates
        worlds.watch(main->name(), glm::vec3(main->gen->spawnPoint(main->world->sizeX(), main->world->sizeZ())));
        // every world steps (each host round its steps), bodies cross
        worlds.tick(dt, 1, dt);
        for (auto& h : hosts) h.second->takeEvents();
    }

    // Save every world; the players' files go with the main world's
    bool save(bool commit) {
        for (GameSession* s : worlds.all()) if (s != main && s->open()) s->save(commit);
        portals.realm.save();
        return main->save(commit, [&](const std::string& stage) { for (auto& h : hosts) h.second->savePlayers(stage); });
    }
    void shutdown(const std::string& why) {
        for (auto& h : hosts) h.second->kickAll(why);
    }

    // The server console's "portal ..." (what it did, for the log)
    std::string portalCommand(const std::vector<std::string>& a) {
        auto& R = portals.realm;
        if (a.empty()) {
            std::string s = "gate " + (R.hasGate ? std::to_string(R.gate.x) + " " + std::to_string(R.gate.y) + " " + std::to_string(R.gate.z) : std::string("not placed"));
            s += portals.seamlessMain ? " (seamless)" : "";
            for (GameSession* w : worlds.all())
                for (const PortalLink& l : w->portals.links) {
                    char b[256];
                    std::snprintf(b, sizeof(b), "; link %s in %s: %.1f %.1f %.1f n %.0f %.0f -> %.1f %.1f %.1f n %.0f %.0f", l.kind.c_str(), w->name().c_str(),
                                  l.a.centre.x, l.a.centre.y, l.a.centre.z, l.a.normal.x, l.a.normal.z, l.b.centre.x, l.b.centre.y, l.b.centre.z, l.b.normal.x, l.b.normal.z);
                    s += b;
                }
            if (R.instances.empty()) s += "; no instances open";
            for (auto& i : R.instances) {
                GameSession* w = worlds.get(R.worldName(i));
                s += "; " + i.id + " " + realm::Realm::clock(i.left) + (i.cleared ? " cleared" : "") + (i.closing ? " closing" : "") +
                     ", " + std::to_string(w ? w->sim.players().size() : 0) + " inside";
            }
            return s;
        }
        if (a[0] == "open") {
            if (R.gateInstance()) return "one's open already (portal close)";
            portals.options = { { "", "open:" + (a.size() > 1 ? a[1] : std::string("crypt")) } };
            return portals.choose(0);
        }
        if (a[0] == "close") {
            realm::Instance* in = R.gateInstance();
            if (!in) return "nothing open";
            std::string n = portals.nameOf(*in);
            portals.removeInstance(in->id);                 // (everyone in it out first: beforeRemove)
            return "the " + n + " is closed";
        }
        if (a[0] == "time" && a.size() > 1) {
            realm::Instance* in = R.gateInstance();
            if (!in) return "nothing open";
            in->left = std::max(1.0f, (float)std::atof(a[1].c_str()) * 60.0f);
            in->warned10 = in->left <= 600.0f; in->warned1 = in->left <= 60.0f;
            return in->id + ": " + realm::Realm::clock(in->left) + " left";
        }
        return "portal [open <dungeon>|close|time <minutes>]";
    }

private:
    net::IListener* listener = nullptr;
    GameSession* main = nullptr;
    std::map<GameSession*, std::unique_ptr<ServerHost>> hosts;

    ServerHost& hostFor(GameSession& s) {
        if (ServerHost* h = hostOf(&s)) return *h;
        auto h = std::make_unique<ServerHost>(s, &s == main ? listener : nullptr);
        h->playerFolder = main->folder();
        h->maxPlayers = maxPlayers;
        h->starterKit = starterKit;
        h->log = log;
        h->route = [this](const std::string& name, const std::string& fileWorld) -> ServerHost* {
            for (auto& o : hosts) if (o.second->expecting(name)) return o.second.get();    // through a portal: its body's there
            if (fileWorld.empty()) return hostOf(main);
            GameSession* w = worlds.get(fileWorld);
            return w ? hostOf(w) : hostOf(main);            // (its world gone: the main one, by the gate)
        };
        h->arrivalFromElsewhere = [this]() { return homeArrival(); };
        h->routeWatch = [this](const std::string& world) -> ServerHost* {
            GameSession* w = worlds.get(world);
            return w ? hostOf(w) : nullptr;
        };
        GameSession* sp = &s;
        h->onGate = [this, sp](Player& p, const std::string& action) { return gateAction(*sp, p, action); };
        ServerHost* hp = h.get();
        if (Worlds::Entry* e = worlds.entryOf(&s)) {
            e->beforeStep = [hp] { hp->beforeStep(); };
            e->afterStep = [hp] { hp->afterStep(); };
        }
        hosts[&s] = std::move(h);
        return *hp;
    }
    void dropHost(GameSession* s) {
        auto it = hosts.find(s);
        if (it == hosts.end()) return;
        if (Worlds::Entry* e = worlds.entryOf(s)) { e->beforeStep = nullptr; e->afterStep = nullptr; }
        hosts.erase(it);
    }

    // Where someone put out of an instance stands: by the main gate
    glm::vec3 homeArrival() {
        if (portals.realm.hasGate) return portals.mainGate().arrival() + glm::vec3(0.0f, 0.5f, 0.0f);
        return glm::vec3(main->gen->spawnPoint(main->world->sizeX(), main->world->sizeZ())) + glm::vec3(0.0f, 2.0f, 0.0f);
    }

    // A player's body went through a portal (Worlds::transfer): its
    // client follows it to the other world's host
    void moved(Player& p, GameSession& from, GameSession& to) {
        ServerHost* a = hostOf(&from);
        ServerHost& b = hostFor(to);
        if (!a) return;
        std::string name;
        for (auto& c : a->clients()) if (c->player == &p) name = c->name;
        // seamless: its client watches this world already (the far side of
        // the portal): the two connections swap
        if (!name.empty())
            if (ServerHost::Client* w = b.watcherNamed(name)) {
                glm::mat4 back = glm::inverse(p.lastPortal);
                glm::vec3 at = glm::vec3(back * glm::vec4(p.lastExitCentre, 1.0f));
                if (glm::length(p.lastExitNormal) < 0.5f) at = glm::vec3(back * glm::vec4(p.position, 1.0f));
                a->demote(&p, at, to.name(), p.lastPortal);
                b.promote(*w, p);
                log(name + " went through to " + to.name() + " (seamless)");
                return;
            }
        if (!name.empty()) a->release(&p);
        else name = a->forget(&p);                        // (on its way here still: on to there instead)
        if (name.empty()) return;                         // (not a player of a client: it just lives there now)
        b.expect(name, &p);
        log(name + " went through to " + to.name());
    }
    // Everyone in a world that's closing: out by the main gate
    void evacuate(GameSession& s) {
        if (&s == main) return;
        std::vector<EntityId> ids;
        for (auto& p : s.sim.players()) ids.push_back(p->uuid);
        for (EntityId id : ids) {
            Simulation::Transfer t;
            t.id = id; t.world = main->name();
            Player* found = nullptr;
            for (auto& p : s.sim.players()) if (p->uuid == id) found = p.get();
            if (!found) continue;
            glm::vec3 at = homeArrival();
            t.through = glm::translate(glm::mat4(1.0f), at - found->position);
            if (Human* h = Worlds::transfer(s, *main, t)) {
                h->velocity = glm::vec3(0.0f);
                h->lastExitNormal = glm::vec3(0.0f);
                moved(static_cast<Player&>(*h), s, *main);
            }
        }
    }

    // ── The gates as each world's players see them ────────
    std::vector<Gate> gatesIn(GameSession& s) {
        std::vector<Gate> out;
        if (&s == main) { if (portals.realm.hasGate && portals.realm.gateBuilt) out.push_back(portals.mainGate()); return out; }
        if (const dungeon::Run* r = s.dungeon()) {
            if (r->hasEntryGate()) out.push_back(r->entryGate());
            if (r->exitOpen()) out.push_back(r->exitGateRef());
        }
        return out;
    }
    realm::Instance* instanceOf(GameSession& s) {
        for (auto& i : portals.realm.instances) if (portals.realm.worldName(i) == s.name()) return &i;
        return nullptr;
    }
    std::string realmTextFor(GameSession& s) {
        std::string t;
        realm::Instance* in = instanceOf(s);
        t += "here " + (in ? in->id : std::string("-")) + "\n";
        t += "world " + s.name() + "\n";                 // (its name here: the links' far worlds are named the same way)
        for (auto& i : portals.realm.instances)
            t += "inst " + i.id + " " + i.kind + " " + std::to_string((int)std::ceil(i.left)) + " " + (i.cleared ? "1" : "0") + " " + (i.closing ? "1" : "0") + "\n";
        for (const Gate& g : gatesIn(s)) t += PortalDirector::remoteLine(g);
        // the seamless links out of this world: the client watches each far
        // side (a watch connection: Hello's watch) and draws it through
        for (const PortalLink& l : s.portals.links)
            if (!l.b.world.empty()) t += PortalDirector::remoteLink(l);
        return t;
    }
    // An option a player picked at a gate's menu in world s
    std::string gateAction(GameSession& s, Player& p, const std::string& action) {
        bool near = false;
        for (const Gate& g : gatesIn(s)) near = near || g.near(p.position);
        if (!near || p.dead()) return "";                     // (not at a gate: nothing)
        auto& R = portals.realm;
        if (action.rfind("open:", 0) == 0) {
            if (&s != main) return "";
            if (R.gateInstance()) return "The gate is open already.";
            portals.options = { { "", action } };
            std::string said = portals.choose(0);
            if (ServerHost* h = hostOf(main)) h->tell(said);
            log("the gate opened (" + action.substr(5) + ")");
            return "";
        }
        if (action == "close") {
            realm::Instance* in = &s == main ? R.gateInstance() : instanceOf(s);
            if (!in || in->closing) return "";
            GameSession* w = worlds.get(R.worldName(*in));
            bool empty = !w || w->sim.players().empty();
            std::string n = portals.nameOf(*in);
            if (empty) {
                portals.removeInstance(in->id);
                if (ServerHost* h = hostOf(main)) h->tell("The " + n + " is closed. The gate goes dark.");
                return "";
            }
            R.close(in->id);
            std::string said = "The " + n + " is closing: everyone out in " + realm::Realm::clock(in->left) + ".";
            if (ServerHost* h = w ? hostOf(w) : nullptr) h->tell(said);
            if (&s == main) return said;
            return "";
        }
        return "";
    }

    float realmSaveTimer = 0.0f;
    void tickRealm(float dt) {
        auto& R = portals.realm;
        if (!R.loaded()) return;
        // the clocks: warnings to those inside; a closed one is emptied, then goes
        for (const auto& e : R.tick(dt)) {
            realm::Instance* in = R.find(e.id);
            GameSession* w = in ? worlds.get(R.worldName(*in)) : nullptr;
            if (e.kind == realm::Realm::Event::Kind::Warning) {
                if (ServerHost* h = w ? hostOf(w) : nullptr) h->tell(e.text);
                continue;
            }
            if (ServerHost* h = w ? hostOf(w) : nullptr) h->tell(e.text + ". You're pulled back out.");
            portals.removeInstance(e.id);
        }
        // a cleared dungeon: its clock shortens
        for (auto& i : R.instances) {
            GameSession* w = worlds.get(R.worldName(i));
            if (const dungeon::Run* r = w ? w->dungeon() : nullptr; r && r->isCleared() && !i.cleared) {
                R.beaten(i.id);
                if (ServerHost* h = hostOf(w)) h->tell("The way out is open. " + portals.nameOf(i) + " closes in " + realm::Realm::clock(i.left) + ".");
                break;                                       // (beaten() may change the list's clocks: once a tick)
            }
        }
        // the gate by the spawn, and every instance's world open with its host
        portals.tickMainGate(*main, main->info.getSpawnPortal());
        for (auto& i : R.instances) {
            const std::string n = R.worldName(i);
            GameSession* w = worlds.get(n);
            if (!w) {
                w = worlds.open(n, R.dev);
                if (!w) continue;
                worlds.pin(w);
                log("instance " + i.id + " open");
            }
            hostFor(*w);
            // its entry gate's ground kept loaded (the portal joins it to the main gate)
            if (const dungeon::Run* r = w->dungeon(); r && r->hasEntryGate()) worlds.watch(n, r->entryGate().arrival());
        }
        if (R.hasGate) worlds.watch(main->name(), portals.mainGate().arrival());
        portals.syncGates([&](const std::string& n) { return worlds.get(n); });
        for (auto& h : hosts) h.second->realmText = realmTextFor(h.second->world());
        realmSaveTimer += dt;
        if (realmSaveTimer > 10.0f) { realmSaveTimer = 0.0f; R.save(); }
    }
};
