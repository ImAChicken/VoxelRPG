#pragma once
#include "IPhysics.h"
#include "../game/CombatMoves.h"
#include "../chunk/ChunkManager.h"
#include "../entity/Human.h"
#include "../voxel/VoxelRegistry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// ══════════════════════════════════════════════════════════
// VoxelPhysics — moves humans through the voxel world
// ══════════════════════════════════════════════════════════
// Deterministic: (state, intent, dt) always gives the same
// next state. Safe to run on a client and a server alike.
//
// The body is an upright cylinder (RADIUS wide, 'height'
// tall) standing on 'position'. Round, so it slides around
// corners instead of catching on them.
class VoxelPhysics : public IPhysics {
public:
    bool init(ChunkManager& w) override {
        world = &w;

        // Which blocks stop a body. Comes from the "solid"
        // flag in blocks.toml — leaves, water, fire and
        // torches are not solid, so you pass through them.
        solid.assign(std::max<size_t>(gRegistry.size(), 1), 0);
        for (size_t id = 1; id < gRegistry.size(); id++)
            solid[id] = gRegistry.get((uint16_t)id).isSolid()
                        ? 1 : 0;

        std::cout << "VoxelPhysics initialized\n";
        return true;
    }

    void shutdown() override { world = nullptr; }

    void setWorld(ChunkManager& w) override { world = &w; }

    // ── One tick of movement ──────────────────────────────
    void moveHuman(Human& h, const HumanIntent& intent,
                   float dt) override {
        if (!world) return;
        h.waitingForWorld = false;

        // ── Knocked down ──────────────────────────────────
        // Limp: the ragdoll carries the body (stepRagdoll moves
        // the capsule). Getting up: gravity, but no steering.
        if (h.ragdoll.isLimp() && !intent.fly) {
            h.velocity = glm::vec3(0.0f);
            return;
        }
        HumanIntent locked;
        locked.fly = intent.fly; locked.flyMove = intent.flyMove; locked.flyFast = intent.flyFast;
        const HumanIntent& in = (h.ragdoll.controlsLocked() || h.dead()) ? locked : intent;
        if (h.dead()) h.attack = AttackState();

        // ── Fly mode: no gravity, no collision ────────────
        if (in.fly) {
            flyTick(h, in, dt);
            return;
        }
        if (h.flying) {
            // Just left fly mode — start from a standstill
            h.flying = false;
            h.resetMotion();
        }

        // ── Wait for the ground to exist ──────────────────
        // Chunks stream in on background threads. Until the
        // ones under the body arrive they read as empty air,
        // and moving now would mean falling through the world.
        if (!groundLoaded(h)) {
            h.waitingForWorld = true;
            return;
        }

        // ── Below the bottom of the world ─────────────────
        if (h.position.y < KILL_Y) {
            h.fellOutOfWorld = true;
            return;
        }

        // Inside solid ground (noclip flight, mode switch,
        // a block placed on top of you) → pop up and out
        unstick(h);

        updateCrouch(h, in, dt);

        // ── Freeze: hang in the air, momentum stored ──────
        // An attack breaks the freeze (the window it was made for)
        if (h.freeze != Human::Freeze::None) {
            h.freezeTimer -= dt;
            bool attack = (in.attackLight || in.attackHeavy) && h.moveset && h.hasAirAttack;
            if (h.freezeTimer > TIMER_EPSILON && !attack) return;
            releaseFreeze(h);
        }

        // ── One-shot requests ─────────────────────────────
        bool busy = (h.move == Human::Move::Roll ||
                     h.move == Human::Move::Backstep);

        // ── Attacks (game/Combat.h) ───────────────────────
        combat::startAttack(h, in);
        bool attacking = h.attack.active();
        h.guarding = in.guard && !attacking && h.grounded && h.move == Human::Move::None &&
                     h.weaponDamage > 0.0f && h.exposed <= 0.0f;
        h.guardTime = h.guarding ? h.guardTime + dt : 0.0f;     // a hit just after raising it is parried

        if (in.jump && !busy && !attacking) {
            if (h.grounded) {
                h.velocity.y = launchSpeed(Human::JUMP_HEIGHT);
                h.grounded   = false;
            } else if (h.canDoubleJump && h.hasDoubleJump) {
                h.hasDoubleJump = false;
                startFreeze(h, Human::Freeze::DoubleJump,
                            Human::DOUBLE_JUMP_FREEZE);
                return;
            }
        }

        // A dodge can cancel an attack only late in its recovery
        bool canDodge = h.move == Human::Move::None &&
                        (!attacking || h.attack.t >= h.attack.def().cancelFrom);
        if (in.dodge && canDodge) {
            glm::vec2 wish = wishDir(in);
            bool moving = (wish.x != 0.0f || wish.y != 0.0f);
            float cost = !h.grounded ? combat::AIRDODGE_STAMINA
                       : moving      ? combat::ROLL_STAMINA : combat::BACKSTEP_STAMINA;
            bool allowed = (h.grounded || h.hasAirDodge) && h.spend(cost);
            if (allowed && attacking) { h.attack = AttackState(); attacking = false; }

            if (!allowed) {
                // out of stamina: nothing
            } else if (h.grounded) {
                if (moving) {
                    // Roll the way you are moving
                    h.move      = Human::Move::Roll;
                    h.moveTimer = Human::ROLL_TIME;
                    h.moveDir   = glm::vec3(wish.x, 0, wish.y);
                } else {
                    // Standing still → hop straight back
                    glm::vec2 back = -horizontal(h.facing);
                    h.move      = Human::Move::Backstep;
                    h.moveTimer = Human::BACKSTEP_TIME;
                    h.moveDir   = glm::vec3(back.x, 0, back.y);
                }
            } else if (h.hasAirDodge) {
                // Aerial dodge: freeze first, burst after.
                // No direction held → straight up.
                h.hasAirDodge = false;
                h.moveDir = moving
                    ? glm::vec3(wish.x, 0, wish.y)
                    : glm::vec3(0, 1, 0);
                startFreeze(h, Human::Freeze::AirDodge,
                            Human::AIR_DODGE_FREEZE);
                return;
            }
        }

        // ── Horizontal velocity ───────────────────────────
        glm::vec2 wish  = wishDir(in);
        bool moving     = (wish.x != 0.0f || wish.y != 0.0f);
        float speed     = (h.crouching ? Human::CROUCH_SPEED
                        : in.sprint   ? Human::SPRINT_SPEED
                                      : Human::WALK_SPEED) * in.speedScale
                          * (h.guarding ? combat::GUARD_SPEED : 1.0f);
        h.sprinting = in.sprint && moving && !h.crouching && in.speedScale >= 1.0f;

        glm::vec2 vh(h.velocity.x, h.velocity.z);

        if (attacking) {
            // Committed: the attack moves the body
            const AttackDef& d = h.attack.def();
            glm::vec2 dir2(h.attack.dir.x, h.attack.dir.z);
            if (h.attack.kind == AttackKind::AirLight) {
                if (h.attack.t < d.activeEnd + 0.05f && !h.grounded) vh = dir2 * combat::LURCH_SPEED;   // the lurch
                else vh *= std::exp(-dt * (h.grounded ? 12.0f : 3.0f));                             // then it slows
            } else if (h.attack.kind == AttackKind::AirHeavy) {
                vh = glm::vec2(0.0f);                                                   // straight down
            } else if (h.grounded) {
                bool step = h.attack.t >= d.lungeFrom && h.attack.t <= d.lungeTo;       // rooted, stepping in
                vh = dir2 * (step ? d.lunge : 0.0f);
            }
        } else if (h.move == Human::Move::Roll ||
            h.move == Human::Move::Backstep) {
            // Committed: fixed speed, fixed direction
            float moveSpeed = (h.move == Human::Move::Roll)
                ? Human::ROLL_DISTANCE     / Human::ROLL_TIME
                : Human::BACKSTEP_DISTANCE / Human::BACKSTEP_TIME;
            vh = glm::vec2(h.moveDir.x, h.moveDir.z) * moveSpeed;

        } else if (h.grounded) {
            // On the ground: head for the wanted velocity,
            // quickly. Also what brings you to a stop.
            glm::vec2 target = wish * speed;
            glm::vec2 diff   = target - vh;
            float len  = glm::length(diff);
            float step = Human::GROUND_ACCEL * dt;
            vh = (len <= step) ? target
                               : vh + diff * (step / len);

        } else if (moving) {
            // In the air: input can add speed in a direction
            // but never takes momentum away, so a dodge burst
            // or a running jump carries.
            float along = glm::dot(vh, wish);
            float add   = speed - along;
            if (add > 0.0f)
                vh += wish * std::min(Human::AIR_ACCEL * dt, add);
        }

        // ── Vertical velocity ─────────────────────────────
        // Exact for constant gravity, so a jump peaks at
        // exactly JUMP_HEIGHT whatever the tick rate
        float dy = 0.0f;
        float gravity = Human::GRAVITY;
        bool plunge = false;
        if (h.attack.kind == AttackKind::AirLight && h.attack.t < h.attack.def().activeEnd + 0.05f)
            gravity *= combat::LURCH_GRAVITY;                   // the lurch carries
        if (h.attack.kind == AttackKind::AirHeavy && !h.attack.landed) {
            gravity = 0.0f;
            plunge = h.attack.plunging;
            h.velocity.y = plunge ? -combat::PLUNGE_SPEED : 0.0f;   // hang, then straight down
        }
        if (h.grounded && !(plunge && h.velocity.y > 0.0f)) {
            h.velocity.y = 0.0f;
        } else {
            dy = h.velocity.y * dt
               - 0.5f * gravity * dt * dt;
            h.velocity.y -= gravity * dt;
            if (h.velocity.y < -Human::MAX_FALL_SPEED && !plunge)
                h.velocity.y = -Human::MAX_FALL_SPEED;
        }

        glm::vec3 delta(vh.x * dt, dy, vh.y * dt);

        // Aerial dodge burst rides on top of normal motion
        if (h.move == Human::Move::AirDodge) {
            delta += h.moveDir * (Human::AIR_DODGE_DISTANCE /
                                  Human::AIR_DODGE_TIME * dt);
        }

        // ── Move, colliding with the world ────────────────
        bool wasGrounded = h.grounded;
        h.velocity.x = vh.x;
        h.velocity.z = vh.y;
        moveAndCollide(h, delta);

        // ── Stay on the ground going down small steps ─────
        if (wasGrounded && h.grounded) followGround(h);

        // ── Landing refills the air moves ─────────────────
        if (h.grounded) {
            h.hasDoubleJump = true;
            h.hasAirDodge   = true;
            h.hasAirAttack  = true;
            if (h.attack.kind == AttackKind::AirHeavy && h.attack.plunging) {
                // the plunge lands: the slam (its hit window) starts now
                h.attack.plunging = false;
                h.attack.landed   = true;
                h.attack.t = h.attack.prevT = combat::SLAM_AT;
            }
        }

        // ── Finish a committed move ───────────────────────
        if (h.move != Human::Move::None) {
            h.moveTimer -= dt;
            if (h.moveTimer <= TIMER_EPSILON) {
                if (h.move != Human::Move::AirDodge) {
                    // Come out of a roll at normal speed, so
                    // it covers its distance and no more
                    h.velocity.x = wish.x * speed;
                    h.velocity.z = wish.y * speed;
                }
                h.move      = Human::Move::None;
                h.moveTimer = 0.0f;
            }
        }

        combat::advanceAttack(h, dt);
        combat::regainStamina(h, dt);
        combat::regainPoise(h, dt);
    }

    // ── The body: hits and the ragdoll ────────────────────
    void stepRagdoll(Human& h, float dt) override {
        Ragdoll& rd = h.ragdoll;
        if (!world || h.flying) {           // no body to knock about
            h.hits.clear();
            rd.reset();
            return;
        }
        rd.setMass(h.body);
        h.mass = h.body.total();
        for (const Human::Hit& hit : h.hits) {
            // the capsule takes the push too (the ragdoll shows
            // it), unless the body is already down
            if (!rd.controlsLocked()) {
                h.velocity += hit.impulse / std::max(h.mass, 1.0f);
                if (hit.impulse.y > 0.0f) h.grounded = false;
            }
            rd.hit(hit.point, hit.impulse, h.mass);
        }
        h.hits.clear();

        rd.step(dt, [this](int x, int y, int z) { return isSolid(x, y, z); });

        if (rd.isLimp()) {
            // the capsule lies under the body, so the camera and
            // anything looking for the player follow it
            glm::vec3 p = rd.point(RP_PELVIS);
            h.position = glm::vec3(p.x, rd.bottom(), p.z);
            h.velocity = glm::vec3(0.0f);
            h.grounded = true;
            if (p.y < KILL_Y) {             // tumbled out of the world
                rd.reset();
                h.fellOutOfWorld = true;
            }
        }
        if (rd.takeStandUp()) {
            // back on its feet where it lies (or as near as fits)
            glm::vec3 p = rd.point(RP_PELVIS), spot;
            if (findStandingSpot(p.x, p.z, Human::RADIUS, Human::STAND_HEIGHT, spot))
                h.position = spot;
            h.velocity = glm::vec3(0.0f);
        }
    }

    bool solidAt(int x, int y, int z) const override {
        return world && isSolid(x, y, z);
    }
    bool worldSolidAt(int x, int y, int z) const override {
        return world && isWorldSolid(x, y, z);
    }
    void setLooseSolids(std::function<bool(int, int, int)> fn) override { loose = std::move(fn); }

    bool isGrounded(const Human& h) const override {
        return world && supported(h.position, h.height);
    }

    bool isOnWall(const Human& h) const override {
        // TODO — ledge grab phase
        (void)h;
        return false;
    }

    // ── Raycast ───────────────────────────────────────────
    // Walks the ray voxel by voxel (no voxel can be skipped,
    // however thin), stopping at the first non-air block.
    RaycastResult raycast(glm::vec3 origin,
                          glm::vec3 direction,
                          float maxDist) const override {
        RaycastResult result;
        if (!world) return result;
        float len = glm::length(direction);
        if (len < 1e-6f) return result;
        glm::vec3 dir = direction / len;

        glm::ivec3 cell((int)std::floor(origin.x),
                        (int)std::floor(origin.y),
                        (int)std::floor(origin.z));
        glm::ivec3 step(0);
        glm::vec3  tMax(1e30f), tDelta(1e30f);

        for (int a = 0; a < 3; a++) {
            if (dir[a] > 0.0f) {
                step[a]   = 1;
                tDelta[a] = 1.0f / dir[a];
                tMax[a]   = ((float)cell[a] + 1.0f - origin[a])
                            / dir[a];
            } else if (dir[a] < 0.0f) {
                step[a]   = -1;
                tDelta[a] = -1.0f / dir[a];
                tMax[a]   = ((float)cell[a] - origin[a])
                            / dir[a];
            }
        }

        float     t = 0.0f;
        glm::vec3 normal(0.0f);
        while (t <= maxDist) {
            uint16_t id = world->typeAtVoxel(
                cell.x, cell.y, cell.z);
            if (id != BLOCK_AIR) {
                result.hit      = true;
                result.blockPos = cell;
                result.blockID  = id;
                result.distance = t;
                result.position = origin + dir * t;
                result.normal   = normal;
                return result;
            }
            // Step into whichever neighbour the ray reaches first
            int a = (tMax.x < tMax.y)
                  ? (tMax.x < tMax.z ? 0 : 2)
                  : (tMax.y < tMax.z ? 1 : 2);
            t        = tMax[a];
            tMax[a] += tDelta[a];
            cell[a] += step[a];
            normal   = glm::vec3(0.0f);
            normal[a] = (float)-step[a];
        }
        return result;
    }

    bool columnLoaded(int x, int z) const override { return world && world->isColumnLoaded(x, z); }

    bool capsuleIsClear(glm::vec3 position,
                        float radius,
                        float height) const override {
        return world && !overlaps(position, radius, height);
    }

    // Tries the spot itself and a ring of spots around it,
    // and picks the lowest — so asking for a spot that is
    // inside a tree gives the ground beside it, not the top
    // of the trunk.
    bool findStandingSpot(float x, float z,
                          float radius, float height,
                          glm::vec3& out) const override {
        if (!world) return false;
        static const float OFFSETS[9][2] = {
            {0,0}, {8,0}, {-8,0}, {0,8}, {0,-8},
            {8,8}, {-8,8}, {8,-8}, {-8,-8} };

        bool  found = false;
        for (const auto& o : OFFSETS) {
            float px = x + o[0], pz = z + o[1];
            int   vx = (int)std::floor(px);
            int   vz = (int)std::floor(pz);
            if (!world->isColumnLoaded(vx, vz)) continue;

            // Climb from the bottom of the world to the top
            // of the ground
            int y = 0;
            while (y < WORLD_HEIGHT && isSolid(vx, y, vz)) y++;
            if (y == 0) continue;   // nothing to stand on

            // Then up until the whole body fits
            glm::vec3 p(px, (float)y, pz);
            while (p.y < (float)WORLD_HEIGHT &&
                   overlaps(p, radius, height)) p.y += 1.0f;

            if (!found || p.y < out.y) {
                out   = p;
                found = true;
            }
        }
        return found;
    }

    void applyExplosion(glm::vec3 center,
                        float radius,
                        float force) override {
        // TODO — combat phase
        (void)center; (void)radius; (void)force;
    }

private:
    ChunkManager*        world = nullptr;
    std::vector<uint8_t> solid;   // indexed by block ID

    // Body fell this far below the world → respawn
    static constexpr float KILL_Y = -64.0f;

    // Largest move tested in one go. Smaller than a voxel so
    // a fast body cannot pass through a one-voxel wall.
    static constexpr float MAX_SUBSTEP = 0.4f;

    // Bodies rest this far from a surface at most
    static constexpr float SKIN = 0.001f;

    // How close below the feet counts as "standing on it"
    static constexpr float GROUND_PROBE = 0.05f;

    // Timers count down in float seconds; this absorbs the
    // rounding so 0.15 s is always the same number of ticks
    static constexpr float TIMER_EPSILON = 1e-4f;

    // ── Helpers ───────────────────────────────────────────
    static glm::vec2 horizontal(glm::vec3 v) {
        glm::vec2 h(v.x, v.z);
        float len = glm::length(h);
        return (len > 1e-6f) ? h / len : glm::vec2(0, -1);
    }

    static glm::vec2 wishDir(const HumanIntent& in) {
        float len = glm::length(in.move);
        return (len > 1e-6f) ? in.move / len : glm::vec2(0);
    }

    // Upward speed that peaks exactly 'height' voxels higher
    static float launchSpeed(float height) {
        return std::sqrt(2.0f * Human::GRAVITY * height);
    }

    bool isWorldSolid(int wx, int wy, int wz) const {
        uint16_t id = world->typeAtVoxel(wx, wy, wz);
        if (id == BLOCK_AIR) return false;
        return id < solid.size() ? solid[id] != 0 : true;
    }
    // The world, and loose objects in it (fallen logs)
    bool isSolid(int wx, int wy, int wz) const {
        return isWorldSolid(wx, wy, wz) || (loose && loose(wx, wy, wz));
    }
    std::function<bool(int, int, int)> loose;

    // Does an upright cylinder at 'pos' touch a solid voxel?
    // Exact test: for every voxel column the cylinder's
    // square bounds cover, measure from the cylinder's axis
    // to the nearest point of that column.
    bool overlaps(glm::vec3 pos, float radius,
                  float height) const {
        int x0 = (int)std::floor(pos.x - radius);
        int x1 = (int)std::floor(pos.x + radius);
        int z0 = (int)std::floor(pos.z - radius);
        int z1 = (int)std::floor(pos.z + radius);
        int y0 = (int)std::floor(pos.y + SKIN);
        int y1 = (int)std::floor(pos.y + height - SKIN);
        float r2 = (radius - SKIN) * (radius - SKIN);

        for (int x = x0; x <= x1; x++)
        for (int z = z0; z <= z1; z++) {
            float nx = std::max((float)x,
                       std::min(pos.x, (float)x + 1.0f));
            float nz = std::max((float)z,
                       std::min(pos.z, (float)z + 1.0f));
            float dx = pos.x - nx, dz = pos.z - nz;
            if (dx * dx + dz * dz >= r2) continue;

            for (int y = y0; y <= y1; y++)
                if (isSolid(x, y, z)) return true;
        }
        return false;
    }

    bool overlaps(const Human& h, glm::vec3 pos) const {
        return overlaps(pos, Human::RADIUS, h.height);
    }

    // Is there solid ground directly under the feet?
    bool supported(glm::vec3 pos, float height) const {
        return overlaps(pos - glm::vec3(0, GROUND_PROBE, 0),
                        Human::RADIUS, height);
    }

    // Are the chunks under and around the body loaded?
    bool groundLoaded(const Human& h) const {
        float m  = Human::RADIUS + 2.0f;
        int   x0 = (int)std::floor(h.position.x - m);
        int   x1 = (int)std::floor(h.position.x + m);
        int   z0 = (int)std::floor(h.position.z - m);
        int   z1 = (int)std::floor(h.position.z + m);
        return world->isColumnLoaded(x0, z0) &&
               world->isColumnLoaded(x1, z0) &&
               world->isColumnLoaded(x0, z1) &&
               world->isColumnLoaded(x1, z1);
    }

    void flyTick(Human& h, const HumanIntent& in, float dt) {
        if (!h.flying) {
            h.flying = true;
            h.resetMotion();
        }
        float speed = in.flyFast ? Human::FLY_SPEED_FAST
                                 : Human::FLY_SPEED;
        float len = glm::length(in.flyMove);
        h.velocity = (len > 1e-6f)
            ? in.flyMove * (speed / len) : glm::vec3(0.0f);
        h.position += h.velocity * dt;
        h.grounded  = false;
        h.crouching = false;
        h.height    = Human::STAND_HEIGHT;
        easeEye(h, dt);
    }

    // If the body is inside something solid, lift it to the
    // first free space above
    void unstick(Human& h) {
        if (!overlaps(h, h.position)) return;
        glm::vec3 p = h.position;
        p.y = std::floor(p.y);
        for (int i = 0; i < VOXEL_DIM * 2; i++) {
            p.y += 1.0f;
            if (!overlaps(h, p)) {
                h.position = p;
                h.resetMotion();
                return;
            }
        }
    }

    void updateCrouch(Human& h, const HumanIntent& in,
                      float dt) {
        if (in.crouch) {
            h.crouching = true;
            h.height    = Human::CROUCH_HEIGHT;
        } else if (h.crouching) {
            // Only stand up if there is headroom
            if (!overlaps(h.position, Human::RADIUS,
                          Human::STAND_HEIGHT)) {
                h.crouching = false;
                h.height    = Human::STAND_HEIGHT;
            }
        }
        easeEye(h, dt);
    }

    // The eye glides between standing and crouching height
    static void easeEye(Human& h, float dt) {
        float target = h.crouching ? Human::eyeCrouch()
                                   : Human::eyeStand;
        float step = 40.0f * dt;
        float diff = target - h.eyeHeight;
        h.eyeHeight = (std::fabs(diff) <= step)
            ? target
            : h.eyeHeight + (diff > 0 ? step : -step);
    }

    void startFreeze(Human& h, Human::Freeze kind,
                     float seconds) {
        h.freeze         = kind;
        h.freezeTimer    = seconds;
        h.storedVelocity = h.velocity;
        h.velocity       = glm::vec3(0.0f);
    }

    void releaseFreeze(Human& h) {
        h.velocity = h.storedVelocity;

        if (h.freeze == Human::Freeze::DoubleJump) {
            // The boost REPLACES vertical speed, so the best
            // moment to press is the top of the jump (15 + 20
            // = 35 voxels). Horizontal momentum carries on.
            // Exception: already rising faster than the boost
            // (launched by something) → the boost is added.
            float boost = launchSpeed(Human::DOUBLE_JUMP_HEIGHT);
            if (h.storedVelocity.y > boost)
                h.velocity.y = h.storedVelocity.y + boost;
            else
                h.velocity.y = boost;

        } else if (h.freeze == Human::Freeze::AirDodge) {
            // Stored momentum resumes; the burst runs on top
            // of it for AIR_DODGE_TIME (moveDir already set)
            h.move      = Human::Move::AirDodge;
            h.moveTimer = Human::AIR_DODGE_TIME;
        }

        h.freeze      = Human::Freeze::None;
        h.freezeTimer = 0.0f;
    }

    // ── Collision ─────────────────────────────────────────
    // Move by 'delta', one axis at a time, in pieces small
    // enough that nothing can be skipped over.
    void moveAndCollide(Human& h, glm::vec3 delta) {
        float biggest = std::max(std::fabs(delta.x),
                        std::max(std::fabs(delta.y),
                                 std::fabs(delta.z)));
        int steps = std::max(1,
            (int)std::ceil(biggest / MAX_SUBSTEP));
        glm::vec3 part = delta / (float)steps;

        for (int i = 0; i < steps; i++) {
            if (part.x != 0.0f) moveHorizontal(h, 0, part.x);
            if (part.z != 0.0f) moveHorizontal(h, 2, part.z);
            if (part.y != 0.0f) moveVertical(h, part.y);
        }
    }

    // Slide along X (axis 0) or Z (axis 2)
    void moveHorizontal(Human& h, int axis, float amount) {
        glm::vec3 target = h.position;
        target[axis] += amount;

        if (!overlaps(h, target)) {
            h.position = target;
            return;
        }

        // Blocked. On the ground, try stepping up onto it:
        // rise one voxel at a time until the way is clear.
        if (h.grounded) {
            for (float rise = 1.0f; rise <= Human::STEP_UP;
                 rise += 1.0f) {
                glm::vec3 up = h.position;
                up.y += rise;
                if (overlaps(h, up)) break;   // ceiling in the way

                glm::vec3 over = up;
                over[axis] += amount;
                if (!overlaps(h, over)) {
                    h.position  = over;
                    h.stepJolt += rise;
                    return;
                }
            }
        }

        // A wall. Close the remaining gap, then stop.
        h.position = closest(h, h.position, target);
        h.velocity[axis] = 0.0f;
    }

    void moveVertical(Human& h, float amount) {
        glm::vec3 target = h.position;
        target.y += amount;

        if (!overlaps(h, target)) {
            h.position = target;
            if (amount > 0.0f) h.grounded = false;
            return;
        }

        h.position   = closest(h, h.position, target);
        h.velocity.y = 0.0f;

        if (amount < 0.0f) {
            // Landed. Voxel tops are whole numbers — rest
            // exactly on one.
            glm::vec3 snapped = h.position;
            snapped.y = std::floor(snapped.y + 0.5f);
            if (std::fabs(snapped.y - h.position.y) < 0.1f &&
                !overlaps(h, snapped))
                h.position = snapped;
            h.grounded = true;
        }
        // amount > 0: bumped your head — velocity is zeroed,
        // gravity takes over next tick
    }

    // Furthest free point on the way from 'from' (free) to
    // 'to' (blocked), found by halving the gap
    glm::vec3 closest(const Human& h, glm::vec3 from,
                      glm::vec3 to) const {
        for (int i = 0; i < 6; i++) {
            glm::vec3 mid = (from + to) * 0.5f;
            if (overlaps(h, mid)) to = mid;
            else                  from = mid;
        }
        return from;
    }

    // Walked off an edge? If the ground is only a small step
    // below, drop onto it and stay grounded — this is what
    // makes walking down terraced hills smooth. Otherwise the
    // body is airborne.
    void followGround(Human& h) {
        if (supported(h.position, h.height)) return;

        for (float drop = 1.0f; drop <= Human::STEP_DOWN;
             drop += 1.0f) {
            glm::vec3 lower = h.position;
            lower.y -= drop;
            if (overlaps(h, lower)) break;
            if (supported(lower, h.height)) {
                h.position  = lower;
                h.stepJolt -= drop;
                return;
            }
        }
        h.grounded = false;
    }
};
