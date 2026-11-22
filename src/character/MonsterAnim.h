#pragma once
// ══════════════════════════════════════════════════════════
// MonsterAnimator — which of a monster's own clips plays
// ══════════════════════════════════════════════════════════
// A monster (character/MonsterModel.h) moves with the clips it came
// with, picked from what its body is doing: dead, just hit, attacking,
// in the air, walking or running, standing. A new clip fades in over
// the old one. Headless (the renderer and tests use it).
// (Claude, 2026-11-22: the mushroom monsters)
#include "MonsterModel.h"

// The clips a monster plays, by name (npcs.toml body.clips; a name
// the model doesn't have: that part falls back as noted)
struct MonsterClipNames {
    std::string idle = "Idle", walk = "Walk", run = "Run", jump = "Jump";
    std::string attack = "Bite_Front", attack2;      // attack2: a second attack, if any
    std::string hit = "HitRecieve", death = "Death";
    float walkSpeed = 10.0f;                          // vox/s its walk clip shows (faster plays it faster)
    float runSpeed  = 20.0f;
};

// What the body is doing this frame
struct MonsterAnimInput {
    bool  dead = false;
    float sinceHurt = 99.0f;        // s since the last hit landed
    bool  attacking = false;        // in an attack (a lunge, a swing)
    bool  secondAttack = false;     // … and it's the second kind
    const std::string* attackClip = nullptr;   // … or this clip (a swing's: NpcType::swings)
    float attackRate = 1.0f;        // how fast the attack clip plays
    bool  airborne = false;
    float speed = 0.0f;             // vox/s along the ground
};

class MonsterAnimator {
public:
    enum class Role { None, Idle, Walk, Run, Air, Attack, Hit, Dead };
    static constexpr float FADE = 0.15f;

    void update(const MonsterModel& m, const MonsterClipNames& names, const MonsterAnimInput& in, float dt) {
        if (&m != model) { model = &m; ids(names); role = Role::None; }
        Role want = Role::Idle;
        int attackId = in.attackClip ? model->clip(*in.attackClip) : (in.secondAttack ? clAttack2 : clAttack);
        // (an attack plays through a hit: the swing is still coming)
        if (in.dead && clDeath >= 0) want = Role::Dead;
        else if (in.attacking && attackId >= 0) want = Role::Attack;
        else if (!in.dead && in.sinceHurt < hitFor() && clHit >= 0) want = Role::Hit;
        else if (in.airborne && clJump >= 0) want = Role::Air;
        else if (in.speed > names.walkSpeed * 1.35f && clRun >= 0) want = Role::Run;
        else if (in.speed > 0.8f && clWalk >= 0) want = Role::Walk;
        // a fresh hit restarts the flinch; a new attack its swing
        bool again = (want == Role::Hit && in.sinceHurt + 1e-4f < lastHurt) ||
                     (want == Role::Attack && !wasAttacking);
        lastHurt = in.sinceHurt; wasAttacking = in.attacking;
        if (want != role || again) {
            prev = cur; prevT = t; fade = role == Role::None ? 1.0f : 0.0f;
            role = want; t = 0.0f;
            switch (want) {
            case Role::Dead:   cur = clDeath; loop = false; break;
            case Role::Hit:    cur = clHit;   loop = false; break;
            case Role::Attack: cur = attackId; loop = false; break;
            case Role::Air:    cur = clJump;  loop = false; t = model->seconds(clJump) * 0.3f; break;   // (past the crouch)
            case Role::Run:    cur = clRun;   loop = true; break;
            case Role::Walk:   cur = clWalk;  loop = true; break;
            default:           cur = clIdle;  loop = true; break;
            }
        }
        float rate = 1.0f;
        if (role == Role::Walk) rate = std::clamp(in.speed / std::max(names.walkSpeed, 0.1f), 0.6f, 1.8f);
        if (role == Role::Run)  rate = std::clamp(in.speed / std::max(names.runSpeed, 0.1f), 0.7f, 1.6f);
        if (role == Role::Attack) rate = in.attackRate;
        t += dt * rate;
        prevT += dt;
        fade = std::min(1.0f, fade + dt / FADE);
    }

    void pose(MonsterPose& out) const {
        if (!model) return;
        model->sample(cur, t, loop, out);
        if (fade < 1.0f && prev >= 0) {
            MonsterPose old;
            model->sample(prev, prevT, true, old);
            MonsterModel::blend(old, out, fade);
            out.swap(old);
        }
    }
    Role now() const { return role; }
    float time() const { return t; }

private:
    const MonsterModel* model = nullptr;
    int clIdle = -1, clWalk = -1, clRun = -1, clJump = -1, clAttack = -1, clAttack2 = -1, clHit = -1, clDeath = -1;
    Role role = Role::None;
    int cur = -1, prev = -1;
    float t = 0.0f, prevT = 0.0f, fade = 1.0f;
    bool loop = true, wasAttacking = false;
    float lastHurt = 99.0f;

    float hitFor() const { return clHit >= 0 ? std::min(model->seconds(clHit), 0.7f) : 0.0f; }
    void ids(const MonsterClipNames& n) {
        auto f = [&](const std::string& s) { return s.empty() ? -1 : model->clip(s); };
        clIdle = f(n.idle); clWalk = f(n.walk); clRun = f(n.run); clJump = f(n.jump);
        clAttack = f(n.attack); clAttack2 = f(n.attack2); clHit = f(n.hit); clDeath = f(n.death);
        if (clHit < 0) clHit = f("HitReact");          // the kit spells it both ways
    }
};
