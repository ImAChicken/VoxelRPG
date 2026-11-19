#pragma once
#include "AudioEngine.h"
#include "../game/WorldActions.h"
#include "../game/ItemRegistry.h"
#include "../npc/NpcManager.h"
#include "../sim/Simulation.h"
#include "../render/Wind.h"
#include <glm/glm.hpp>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// SoundDirector — what the world sounds like, each frame
// ══════════════════════════════════════════════════════════
// Watches the bodies and the events and plays the sounds, each from
// where it happens (audio/AudioEngine.h places and muffles them):
//
//   footsteps   every stride a body takes on the ground, by what's
//               under the foot (and the grass it walks through);
//               quieter crouched, louder sprinting. A slime squelches
//               where it lands
//   landings    a jump or fall coming down, harder the faster
//   swings      a blade cutting the air as its hit window opens; a
//               spell's whoosh as it leaves the hand
//   hits        a thump where a blow lands; a guard clanks or knocks
//               (a shield is wood, a blade is metal)
//   the world   blocks breaking and going down, doors, chops, blasts,
//               from the voxel (onRequest: the player's own edits)
//   barks       NPCs speak short lines in their own voices (npcs.toml
//               voice / barks): spot, hurt, die, greet, idle. From the
//               mouth, no box; the face's mouth moves with it
//   the wind    a bed of wind, louder on open high ground, quiet in
//               a forest, a valley or indoors (the baked sky light)
//
// Voices come from npcs.toml (a type's voice, a little different for
// each NPC) or, for people without one and for players, their look's
// own voice (the character creator's Voice tab).
namespace sound {

class SoundDirector {
public:
    // A spoken line from a body (it follows them; their mouth moves)
    void say(uint64_t who, const Voice& v, const std::string& line, glm::vec3 at) {
        if (!gAudio.ok() || line.empty()) return;
        PlayDesc d;
        d.pos = at;
        d.follow = who;
        gAudio.speak(gLines.get(v, line), d);
    }
    // The character creator's test line: not in the world (a menu),
    // but following PREVIEW so the preview's face moves with it
    static constexpr uint64_t PREVIEW = ~0ull;
    void sayPreview(const Voice& v, const std::string& line) {
        PlayDesc d;
        d.inWorld = false;
        d.follow = PREVIEW;
        gAudio.speak(gLines.get(v, line), d);
    }

    Voice voiceOf(const Npc& n) const {
        if (n.type && n.type->hasVoice) return n.type->voice.varied((uint32_t)(n.uuid * 2654435761u), n.type->voiceVary);
        return n.appearance.voice;
    }
    // Where a body's voice comes from
    static glm::vec3 mouthOf(const Human& h) {
        if (h.noRagdoll) return h.position + glm::vec3(0.0f, h.height * 0.6f, 0.0f);    // a blob: its middle
        return h.position + glm::vec3(0.0f, h.eyeHeight - 1.6f, 0.0f) + h.facing * 1.2f;
    }

    // An NPC says one of its type's lines for 'when' (spot, hurt, die,
    // greet, idle), not too often
    void bark(const Npc& n, const std::string& when, float chance = 1.0f) {
        if (!n.type) return;
        const std::vector<std::string>* lines = n.type->barksFor(when);
        if (!lines) return;
        Body& b = bodies[n.uuid];
        if (clock - b.lastBark < (when == "die" ? 0.0f : 1.6f)) return;
        if (unit() > chance) return;
        b.lastBark = clock;
        say(n.uuid, voiceOf(n), (*lines)[rng() % lines->size()], mouthOf(n));
    }

    // The player's own edits (PlayerInteraction::onRequest): typeBefore
    // is what was at q.pos before it happened
    void onRequest(const WorldRequest& q, uint16_t typeBefore, uint16_t placed) {
        using Op = WorldRequest::Op;
        glm::vec3 at = glm::vec3(q.pos) + 0.5f;
        auto play = [&](sfx::Kind k, const std::string& mat, float gain = 1.0f, float size = 1.0f) {
            PlayDesc d; d.pos = at; d.gain = gain;
            gAudio.play(gSfx.get(k, mat, rng(), size), d);
        };
        switch (q.op) {
        case Op::Break:
            if (typeBefore) play(sfx::Kind::Break, MaterialTable::nameFor(typeBefore));
            break;
        case Op::BreakCube:
            at = (glm::vec3(q.lo) + glm::vec3(q.hi)) * 0.5f + 0.5f;
            play(sfx::Kind::Break, MaterialTable::nameFor(typeBefore ? typeBefore : 1), 1.0f, 2.0f);
            break;
        case Op::Place:
            play(sfx::Kind::Place, placed ? MaterialTable::nameFor(placed) : std::string("wood"), 0.8f);
            break;
        case Op::Piece: case Op::Blueprint:
            at = glm::vec3(q.lo) + 0.5f;
            play(sfx::Kind::Place, "wood", 1.0f, 2.0f);
            break;
        case Op::Door:      play(sfx::Kind::Door, "wood"); break;
        case Op::AxeHit:    at = q.point; play(sfx::Kind::Chop, "wood"); break;
        case Op::Blast:     at = glm::vec3(q.pos); play(sfx::Kind::Blast, "stone", 1.0f); break;
        case Op::Hoe:       play(sfx::Kind::Step, "dirt", 0.8f); break;
        default: break;
        }
    }

    // Each frame. blockAt: the world (for what's underfoot). exposure:
    // 0 .. 1 how open to the sky the ears are (the wind bed)
    void frame(float dt, Simulation& sim, NpcManager& npcs, const Player& self, const std::vector<SimEvent>& events,
               const std::function<uint16_t(int, int, int)>& blockAt, float exposure, double seconds) {
        clock += dt;
        if (!gAudio.ok()) return;
        // ── bodies: steps, landings, swings, spotting, idle chatter
        auto body = [&](const Human& h, bool person, const Npc* npc) {
            Body& b = bodies[h.uuid];
            b.seen = clock;
            glm::vec2 v(h.velocity.x, h.velocity.z);
            float speed = glm::length(v);
            // far bodies only keep their state (no sound)
            float dist = glm::length(h.position - self.position);
            bool inEarshot = dist < 360.0f;
            // landing
            if (h.grounded && !b.grounded && b.fall < -18.0f && inEarshot && !h.dead()) {
                float size = std::clamp(-b.fall / 60.0f, 0.3f, 1.5f);
                std::string mat = person ? underfoot(h, blockAt) : std::string("slime");
                PlayDesc d; d.pos = h.position; d.gain = 0.7f + size * 0.3f;
                gAudio.play(gSfx.get(person ? sfx::Kind::Land : sfx::Kind::Land, mat, rng(), size), d);
                b.stride = 0.0f;
            }
            b.fall = h.grounded ? 0.0f : std::min(b.fall, h.velocity.y);
            b.grounded = h.grounded;
            // footsteps: a stride gets longer the faster you go
            if (person && h.grounded && speed > 2.0f && !h.dead()) {
                b.stride += speed * dt;
                float len = 4.0f + speed * 0.25f;
                if (b.stride >= len) {
                    b.stride -= len;
                    if (inEarshot) {
                        PlayDesc d;
                        d.pos = h.position + glm::vec3(0.0f, 0.3f, 0.0f);
                        d.gain = h.crouching ? 0.25f : h.sprinting ? 0.9f : 0.55f;
                        d.pitch = 0.94f + unit() * 0.12f;
                        gAudio.play(gSfx.get(sfx::Kind::Step, underfoot(h, blockAt), rng()), d);
                        // walking through grass / plants: a rustle too
                        glm::ivec3 f = glm::ivec3(glm::floor(h.position + glm::vec3(0, 0.6f, 0)));
                        if (uint16_t id = blockAt(f.x, f.y, f.z); id && (gRegistry.get(id).flags & VF_PLANT)) {
                            PlayDesc r = d; r.gain *= 0.7f;
                            gAudio.play(gSfx.get(sfx::Kind::Step, "plant", rng()), r);
                        }
                    }
                }
            } else if (h.grounded) b.stride = std::min(b.stride, 2.0f);
            // a swing as the hit window opens; a spell as it leaves the hand
            if (h.attack.active()) {
                const AttackDef& ad = h.attack.def();
                if (b.attackT < ad.activeStart && h.attack.t >= ad.activeStart && inEarshot) {
                    PlayDesc d; d.pos = h.eyePosition(); d.pitch = 0.9f + unit() * 0.2f;
                    bool cast = h.attack.kind == AttackKind::Cast;
                    gAudio.play(gSfx.get(cast ? sfx::Kind::Cast : sfx::Kind::Swing, "air", rng()), d);
                }
                b.attackT = h.attack.t;
            } else b.attackT = -1.0f;
            // an NPC: barks when it notices someone, and now and then by itself
            if (npc && inEarshot && !h.dead()) {
                const std::string& st = npc->stateName;
                if (st != b.state) {
                    bool calm = b.state.empty() || b.state == "idle" || b.state == "patrol" || b.state == "return";
                    if ((st == "alert" || st == "combat") && calm && clock - b.lastSpot > 20.0f) { bark(*npc, "spot", 0.85f); b.lastSpot = clock; }
                    b.state = st;
                }
                if ((st == "idle" || st == "patrol") && clock >= b.nextIdle) {
                    if (b.nextIdle > 0.0f && dist < 140.0f) bark(*npc, "idle", 0.5f);
                    b.nextIdle = clock + 14.0f + unit() * 30.0f;
                }
            }
        };
        for (const auto& p : sim.players()) body(*p, true, nullptr);
        for (auto& np : npcs.all()) body(*np, np->type ? np->type->person() : true, np.get());
        // forget bodies gone a while
        for (auto it = bodies.begin(); it != bodies.end();)
            if (clock - it->second.seen > 30.0f) it = bodies.erase(it); else ++it;

        // ── what happened: hits, guards, deaths
        for (const SimEvent& e : events) {
            if (e.kind == SimEvent::Kind::Hit) {
                Human* target = sim.find(e.b);
                if (!target) continue;
                glm::vec3 at = target->position + glm::vec3(0.0f, target->height * 0.6f, 0.0f);
                PlayDesc d; d.pos = at;
                if (e.blocked) {
                    // the guard: a shield is wood, a blade metal
                    bool shield = target->guardStyle.shield;
                    gAudio.play(gSfx.get(sfx::Kind::Block, shield ? "wood" : "metal", rng()), d);
                } else if (e.amount > 0.0f) {
                    d.gain = std::clamp(0.5f + e.amount / 30.0f, 0.5f, 1.0f);
                    gAudio.play(gSfx.get(sfx::Kind::Hit, target->noRagdoll ? "slime" : "dirt", rng(), std::clamp(e.amount / 20.0f, 0.5f, 2.0f)), d);
                    if (Npc* n = npcs.find(e.b)) { if (!n->dead()) bark(*n, "hurt", 0.6f); }
                    else if (target->isPlayer() && !target->dead()) {
                        // a player grunts in their own voice
                        Body& b = bodies[e.b];
                        if (clock - b.lastBark > 2.0f && unit() < 0.5f) {
                            static const char* GRUNT[] = { "Ugh!", "Agh!", "Hngh!", "Oof!" };
                            b.lastBark = clock;
                            say(e.b, static_cast<const Player*>(target)->appearance.voice, GRUNT[rng() % 4], mouthOf(*target));
                        }
                    }
                }
            } else if (e.kind == SimEvent::Kind::Killed) {
                if (Npc* n = npcs.find(e.b)) bark(*n, "die", 1.0f);
            }
        }

        // ── the wind: a bed that swells with the gusts where you're exposed
        if (!windId || !gAudio.playing(windId)) {
            PlayDesc d; d.inWorld = false; d.bus = Bus::Ambience; d.gain = 0.0f;
            windId = gAudio.play(gSfx.get(sfx::Kind::Wind, "air", 0), d);
        }
        wind::Now w = wind::at(seconds);
        float target = std::clamp(exposure, 0.0f, 1.0f) * (0.25f + w.strength * 0.75f) * 0.6f;
        windGain += (target - windGain) * (1.0f - std::exp(-dt * 1.5f));
        gAudio.setGain(windId, windGain);
    }

    // Where a voice is now (AudioEngine::update's 'where')
    bool where(uint64_t who, glm::vec3& at, Simulation& sim) const {
        if (who == PREVIEW) { at = glm::vec3(0.0f); return true; }
        if (const Human* h = sim.find(who)) { at = mouthOf(*h); return true; }
        return false;
    }

private:
    struct Body {
        bool  grounded = true;
        float fall = 0.0f;
        float stride = 0.0f;
        float attackT = -1.0f;
        float lastBark = -100.0f, lastSpot = -100.0f, nextIdle = 0.0f;
        float seen = 0.0f;
        std::string state;
    };
    std::unordered_map<uint64_t, Body> bodies;
    float clock = 0.0f;
    uint32_t windId = 0;
    float windGain = 0.0f;
    std::mt19937 rng{ 1234567u };
    float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    // What's under the feet: the block below, or the one under that
    static std::string underfoot(const Human& h, const std::function<uint16_t(int, int, int)>& blockAt) {
        glm::ivec3 f = glm::ivec3(glm::floor(h.position));
        for (int dy = 1; dy <= 2; dy++)
            if (uint16_t id = blockAt(f.x, f.y - dy, f.z); id && !(gRegistry.get(id).flags & VF_PLANT))
                return MaterialTable::nameFor(id);
        return "dirt";
    }
};

inline SoundDirector gSounds;

} // namespace sound
