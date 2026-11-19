#pragma once
#include "Brain.h"
#include "VillageLife.h"
#include <cmath>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// routine — a villager's day (npc/VillageLife.h has its places)
// ══════════════════════════════════════════════════════════
// By the hour (NpcWorld::hourOfDay, each villager a little early
// or late):
//   22 - 6   asleep in its bed (lying down)
//    6 - 8   at home
//    8 - 12  at work (no job: about the house or out)
//   12 - 13.5 out: the square, the well or the church
//   13.5 - 18 at work
//   18 - 22  at home (some go out first)
// It walks there along a route through doors: to the inside of
// the building it's in, opens the door, steps out, shuts it; to
// the outside of the next, opens, steps in, shuts; then to its
// spot. Seeing an enemy or hearing a fight sends it to alert
// (villagers then flee); after, "return" brings it home and the
// routine picks up again from wherever it is. Asleep, it hears
// and sees nothing until it's hurt.
namespace npcstates {

class Routine : public NpcState {
public:
    enum class Doing : uint8_t { Sleep, Home, Work, Out };
    static constexpr float WALK  = 0.45f;     // strolling
    static constexpr float GIVE_UP = 40.0f;   // s on one leg of a route before skipping it
    static constexpr float INSIDE_NEAR = 7.0f;

    const char* name() const override { return "routine"; }

    void enter(Npc& n, NpcWorld&) override {
        steps.clear();
        goal = nullptr;
        goalHaunt = -2;
        settled = false;
        locate(n);
    }
    // Which building it's in now (near the inside of one of its
    // doors, or its bed): worked out from where it stands
    void locate(Npc& n) {
        inside = LifePlace();
        auto near = [&](const LifePlace& p) {
            return p.valid && p.door && glm::length(glm::vec2(n.position.x - p.in.x, n.position.z - p.in.z)) < INSIDE_NEAR;
        };
        at = LifePlace();
        if (near(n.life.bed)) inside = n.life.bed;
        else if (near(n.life.work)) inside = n.life.work;
        else for (const LifePlace& h : n.life.haunts) if (near(h)) { inside = h; break; }
        if (inside.valid) at = inside;
        // or near its spot inside
        if (!inside.door && n.life.bed.valid && n.life.bed.door &&
            glm::length(glm::vec2(n.position.x - n.life.bed.spot.x, n.position.z - n.life.bed.spot.z)) < 14.0f)
            inside = at = n.life.bed;
        // outdoors at one of its places (the square, the well)?
        if (!at.valid)
            for (const LifePlace& h : n.life.haunts)
                if (!h.door && glm::length(glm::vec2(n.position.x - h.spot.x, n.position.z - h.spot.z)) < 14.0f) { at = h; break; }
    }
    void exit(Npc& n, NpcWorld&) override { wake(n); }

    void tick(Npc& n, NpcWorld& w, float dt) override {
        if (!n.life.on) { brain::change(n, w, "idle"); return; }
        float h = std::fmod(w.hourOfDay() - n.life.lateBy + 48.0f, 24.0f);
        Doing want = doing(n, w, h);
        int haunt = -1;
        const LifePlace* to = placeFor(n, w, want, haunt);
        if (!to || !to->valid) { n.intent.move = glm::vec2(0); return; }
        if (to != goal || haunt != goalHaunt) plan(n, *to, haunt, want);
        if (want != Doing::Sleep && n.lying) wake(n);
        // a door it couldn't shut behind it (someone in the way): try
        // again for a while
        if (toClose) {
            closeFor -= dt;
            if (w.setDoor(toClose, false) || closeFor <= 0.0f) toClose = 0;
        }

        // the route
        if (!steps.empty()) {
            Step& s = steps.front();
            legTime += dt;
            float left = n.walkTo(w, s.to, WALK, false, 1.2f);
            // close enough: there, or stuck short of it (furniture, a
            // crowd) — a bed is reached from across the room
            bool last = steps.size() == 1;
            float enough = last && to->lie ? 16.0f : 5.0f;
            if (s.road && left <= 3.5f) left = 0.0f;           // on the road: near is enough
            if (s.road && sinceBetter > 6.0f) left = 0.0f;     // (or stuck on the way: skip it)
            if (left < bestLeft - 0.5f) { bestLeft = left; sinceBetter = 0.0f; }
            else sinceBetter += dt;
            // in the right building but can't get the last few steps
            // (someone in the way): this'll do
            if (last && inside.door == to->door && left <= 12.0f && sinceBetter > 8.0f) enough = 12.0f;
            // through a door: really through it (not stuck against it,
            // shut by someone else just ahead) — open it again
            if (s.close) {
                enough = 2.5f;
                if (sinceBetter > 2.0f) w.setDoor(s.close, true);
            }
            if (left <= 1.6f || (left <= enough && sinceBetter > 3.0f)) {
                if (s.open)  w.setDoor(s.open, true);
                if (s.close && !w.setDoor(s.close, false)) { toClose = s.close; closeFor = 15.0f; }
                if (s.enters) inside = s.enteredPlace;
                if (s.leaves) inside = LifePlace();
                if (steps.size() == 1) at = *to;                // arrived
                steps.erase(steps.begin());
                legTime = 0.0f;
                bestLeft = 1e9f; sinceBetter = 0.0f;
                n.intent.move = glm::vec2(0);
            } else if (legTime > GIVE_UP || sinceBetter > 15.0f) {
                // couldn't get there (a shut door, pushed out of the way):
                // work out where it is and plan again
                locate(n);
                goal = nullptr;
                steps.clear();
                legTime = 0.0f;
                bestLeft = 1e9f; sinceBetter = 0.0f;
                retries++;
            }
            return;
        }
        // there
        settle(n, w, *to, want, dt);
    }

    bool onEvent(Npc& n, NpcWorld& w, const NpcEvent& e) override {
        using K = NpcEvent::Kind;
        if (n.lying && (e.kind == K::SawEnemy || e.kind == K::HeardNoise || e.kind == K::AllyDied)) return true;   // asleep
        if (e.kind == K::SawEnemy) { brain::know(n, w, e.who, e.at); brain::change(n, w, "alert"); return true; }
        if (e.kind == K::HeardNoise && e.noise.kind != Noise::Kind::Step) {
            n.lastKnown = e.noise.at;
            if (e.noise.kind == Noise::Kind::BattleCry && e.noise.about) brain::know(n, w, e.noise.about, e.noise.aboutAt);
            brain::change(n, w, "alert");
            return true;
        }
        if (e.kind == K::AllyDied) { n.lastKnown = e.at; brain::change(n, w, "alert"); return true; }
        return false;   // hurt: flee; a player talking: the menu
    }

    std::string debug(const Npc& n) const override {
        static const char* D[] = { "asleep", "home", "work", "out" };
        std::string s = std::string("routine: ") + D[(int)lastDoing];
        if (!steps.empty()) s += " (walking, " + std::to_string(steps.size()) + " legs)";
        if (n.lying) s += ", lying";
        return s;
    }

    // What it wants to be doing at this hour
    static Doing doingAt(const Npc& n, float h, bool evenDay) {
        if (h >= 22.0f || h < 6.0f) return Doing::Sleep;
        if (h < 8.0f) return Doing::Home;
        if (h < 12.0f || (h >= 13.5f && h < 18.0f)) {
            if (n.life.job.empty()) return ((int)h % 2 == 0) == evenDay ? Doing::Out : Doing::Home;
            return Doing::Work;
        }
        if (h < 13.5f) return Doing::Out;
        if (h < 19.0f && evenDay && !n.life.haunts.empty()) return Doing::Out;
        return Doing::Home;
    }

private:
    struct Step {
        glm::vec3 to{ 0.0f };
        uint32_t  open = 0, close = 0;   // a door to open / shut on arriving
        bool      enters = false, leaves = false;
        bool      road = false;          // a waypoint on the roads: near is enough
        LifePlace enteredPlace;
    };
    std::vector<Step> steps;
    const LifePlace* goal = nullptr;
    int   goalHaunt = -2;
    LifePlace inside;                    // the building it's in (door 0: outdoors)
    LifePlace at;                        // the place it last reached (its way to the square)
    float legTime = 0.0f;
    float fidgetWalk = 0.0f;             // how long the current few steps have taken
    uint32_t toClose = 0;                // a door still to shut behind it
    float closeFor = 0.0f;
    float bestLeft = 1e9f, sinceBetter = 0.0f;   // getting any closer?
    int   retries = 0;
    bool  settled = false;
    float fidget = 0.0f;
    glm::vec3 fidgetTo{ 0.0f };
    Doing lastDoing = Doing::Home;

    // A different outing each part of the day, per villager
    static uint32_t mix(uint32_t a) { a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16; return a; }
    bool evenDay(const Npc& n, NpcWorld& w) const {
        return (mix((uint32_t)n.uuid ^ (uint32_t)(w.now() / 600.0f)) & 1u) == 0;
    }
    Doing doing(const Npc& n, NpcWorld& w, float h) {
        lastDoing = doingAt(n, h, evenDay(n, w));
        return lastDoing;
    }
    const LifePlace* placeFor(const Npc& n, NpcWorld& w, Doing d, int& haunt) const {
        haunt = -1;
        switch (d) {
        case Doing::Sleep: case Doing::Home: return &n.life.bed;
        case Doing::Work:  return n.life.work.valid ? &n.life.work : &n.life.bed;
        case Doing::Out:
            if (n.life.haunts.empty()) return &n.life.bed;
            haunt = (int)(mix((uint32_t)n.uuid * 31u + (uint32_t)(w.now() / 300.0f)) % n.life.haunts.size());
            return &n.life.haunts[(size_t)haunt];
        }
        return &n.life.bed;
    }

    void plan(Npc& n, const LifePlace& to, int haunt, Doing d) {
        goal = &to;
        goalHaunt = haunt;
        steps.clear();
        settled = false;
        legTime = 0.0f;
        bestLeft = 1e9f; sinceBetter = 0.0f;
        (void)d;
        // out of where it is, unless that's where it's going
        if (inside.door && inside.door != to.door) {
            Step a; a.to = inside.in; a.open = inside.door;
            Step b; b.to = inside.out; b.close = inside.door; b.leaves = true;
            steps.push_back(a); steps.push_back(b);
        }
        // along the roads: from where it was, down its path, along the
        // roads (through the square if they're different roads), up the
        // path to where it's going. Not when it's staying in the building
        bool moving = !(inside.door && inside.door == to.door);
        if (moving) {
            std::vector<glm::vec3> from = at.valid ? at.toSquare : std::vector<glm::vec3>();
            std::vector<glm::vec3> into = to.toSquare;
            auto same = [](glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)) < 2.0f; };
            bool shared = false;
            while (!from.empty() && !into.empty() && same(from.back(), into.back())) { from.pop_back(); into.pop_back(); shared = true; }
            auto road = [&](glm::vec3 p) { Step s; s.to = p; s.road = true; steps.push_back(s); };
            for (const glm::vec3& p : from) road(p);
            bool viaSquare = !shared && (!at.toSquare.empty() || !to.toSquare.empty()) && n.life.square != glm::vec3(0.0f);
            if (viaSquare) road(n.life.square);
            for (auto it = into.rbegin(); it != into.rend(); ++it) road(*it);
        }
        // into where it's going
        if (to.door && to.door != (steps.empty() ? inside.door : 0u)) {
            Step a; a.to = to.out; a.open = to.door;
            Step b; b.to = to.in; b.close = to.door; b.enters = true; b.enteredPlace = to;
            steps.push_back(a); steps.push_back(b);
        }
        Step s; s.to = to.spot;
        steps.push_back(s);
        if (n.lying) wake(n);
    }

    void settle(Npc& n, NpcWorld& w, const LifePlace& p, Doing d, float dt) {
        if (d == Doing::Sleep && p.lie) {
            if (!n.lying) {
                n.lying = true;
                n.position = n.prevPosition = p.lieAt;
                n.velocity = glm::vec3(0.0f);
                n.face(n.position + p.facing);
            }
            n.intent.move = glm::vec2(0);
            return;
        }
        // about the place: stand a while facing the work, now and then
        // take a few steps (busy, not frozen)
        if (!settled) { settled = true; fidget = n.randRange(4.0f, 10.0f); fidgetTo = p.spot; fidgetWalk = 0.0f; }
        fidget -= dt;
        if (fidget <= 0.0f) {
            float r = d == Doing::Out ? 9.0f : 1.5f, a = n.randRange(0.0f, 6.2832f);
            fidgetTo = p.spot + glm::vec3(std::cos(a) * r * n.rand01(), 0, std::sin(a) * r * n.rand01());
            fidget = n.randRange(6.0f, 16.0f);
            fidgetWalk = 0.0f;
        }
        // a few steps, not a climb: something in the way, stay put
        fidgetWalk += dt;
        if (n.position.y > p.spot.y + 3.0f) fidgetTo = p.spot;                  // up on something: back down
        else if (fidgetWalk > 4.0f) fidgetTo = glm::vec3(n.position.x, p.spot.y, n.position.z);
        // straight there, no path and no jumping (it's a step or two)
        glm::vec2 dd(fidgetTo.x - n.position.x, fidgetTo.z - n.position.z);
        float left = glm::length(dd);
        if (n.position.y > p.spot.y + 3.0f) left = n.walkTo(w, fidgetTo, 0.35f, false, 1.2f);
        else if (left > 1.2f) {
            dd /= left;
            n.face(n.position + glm::vec3(dd.x, 0, dd.y));
            n.intent.move = dd;
            n.intent.speedScale = 0.35f;
        }
        if (left <= 1.3f) {
            n.intent.move = glm::vec2(0);
            n.face(n.position + p.facing);
        }
    }

    // Up out of bed: standing beside it
    void wake(Npc& n) {
        if (!n.lying) return;
        n.lying = false;
        if (n.life.bed.valid) n.position = n.prevPosition = n.life.bed.spot;
        n.velocity = glm::vec3(0.0f);
    }
};

} // namespace npcstates
