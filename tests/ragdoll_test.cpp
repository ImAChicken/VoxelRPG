// ══════════════════════════════════════════════════════════
// ragdoll_test — the active ragdoll and body mass, headless
// ══════════════════════════════════════════════════════════
// Builds the default character, stands it on flat voxel
// ground and checks:
//   idle         follows the animation exactly, no simulation
//   stagger      a small shove reacts and hands back
//   knockdown    a big hit goes limp, lands on the ground, gets up
//   mass         the same hit knocks a light body down but only
//                staggers a heavy one
//   weapon       a heavy weight in the hand sags the arm more
//   wall         thrown into a wall, nothing ends up inside it
//   links        the parts keep their lengths
//   determinism  the same hits give the same result twice
//
// Build (from the repo root):
//   cmake -B build -DVOXELRPG_TESTS=ON && cmake --build build --target ragdoll_test
// or by hand:
//   g++ -std=c++17 -O2 -I src -I <glm> -I <toml++> tests/ragdoll_test.cpp -o ragdoll_test
#include "physics/Ragdoll.h"
#include "character/CharacterAnimator.h"
#include <chrono>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static const float DT = 1.0f / 60.0f;

// Flat ground: everything below y = 0 is solid. Optional wall
// at x >= wallX.
struct World {
    int wallX = 1 << 20;
    bool solid(int x, int y, int) const { return y < 0 || x >= wallX; }
};

struct Body {
    CharacterModel model;
    Pose pose;                 // default pose: standing straight
    glm::vec3 feet{0.0f, 0.0f, 0.0f};
    Ragdoll rd;
    BodyMass mass;
    World world;

    explicit Body(float bodyKg = BodyMass::BASE_KG) {
        model = CharacterModel::build(Appearance{});
        pose.bodyYaw = 90.0f;   // facing +Z
        mass.setBody(bodyKg);
        rd.setModel(model);
        rd.setMass(mass);
        feed();
    }
    void feed() {
        BoneMats b;
        worldBoneMatrices(model, pose, feet, b);
        rd.setTarget(b);
    }
    void tick(int n = 1) {
        SolidFn s = [this](int x, int y, int z) { return world.solid(x, y, z); };
        for (int i = 0; i < n; i++) { feed(); rd.step(DT, s); }
    }
    glm::vec3 target(int i) const {   // where the animation puts a point
        Ragdoll copy = rd;
        copy.reset();
        BoneMats b;
        worldBoneMatrices(model, pose, feet, b);
        copy.setTarget(b);
        return copy.point(i);
    }
    float chestHeight() const { return rd.point(RP_WAIST).y; }
    bool  anyInsideSolid() const {
        for (int i = 0; i < RP_COUNT; i++) {
            glm::vec3 p = rd.point(i);
            if (world.solid((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z))) return true;
        }
        return false;
    }
    bool finite() const {
        for (int i = 0; i < RP_COUNT; i++) {
            glm::vec3 p = rd.point(i);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        }
        return true;
    }
};

static const char* stateName(Ragdoll::State s) {
    switch (s) {
    case Ragdoll::State::Animated:   return "animated";
    case Ragdoll::State::Reacting:   return "reacting";
    case Ragdoll::State::Limp:       return "limp";
    case Ragdoll::State::Recovering: return "recovering";
    }
    return "?";
}

// Shove at chest height, backward (-Z), with a speed change of
// dv for a 70 kg body
static glm::vec3 shoveAt(const Body& b) { return b.rd.point(RP_WAIST) + glm::vec3(0, 1, 0.5f); }
static glm::vec3 shove(float dv) { return glm::vec3(0.0f, 0.0f, -1.0f) * dv * 70.0f; }

int main() {
    std::printf("ragdoll_test\n");

    // ── Body mass ──────────────────────────────────────────
    {
        std::printf("body mass\n");
        BodyMass m;
        float sum = 0;
        for (float f : BodyMass::FRACTION) sum += f;
        check(std::fabs(sum - 1.0f) < 1e-3f, "segment shares add up to 1 (" + std::to_string(sum) + ")");
        float slim  = BodyMass::bodyKgFor(0, 16, 0, 0, 0, 0);
        float avg   = BodyMass::bodyKgFor(1, 17, 0, 0, 0, 0);
        float heavy = BodyMass::bodyKgFor(2, 18, 0, 1, 1, 1);
        std::printf("    slim 5'4\" %.1f kg, average 5'8\" %.1f kg, sturdy 6'0\" maxed %.1f kg\n", slim, avg, heavy);
        check(slim < avg && avg < heavy, "slim < average < sturdy");
        check(std::fabs(avg - 68.0f) < 0.01f, "average build is 68 kg");
        m.addWorn(WornSlot::Torso, 10.0f);
        m.addGear(SEG_HAND_R, 2.0f);
        check(std::fabs(m.total() - 80.0f) < 1e-3f, "gear adds to the total");
    }

    // ── Idle ───────────────────────────────────────────────
    {
        std::printf("idle\n");
        Body b;
        b.tick(120);
        float err = 0;
        for (int i = 0; i < RP_COUNT; i++) err = std::max(err, glm::length(b.rd.point(i) - b.target(i)));
        check(b.rd.state() == Ragdoll::State::Animated, "stays animated");
        check(err < 1e-4f, "points sit on the animated skeleton");
        BoneMats anim, out;
        worldBoneMatrices(b.model, b.pose, b.feet, anim);
        check(!b.rd.renderBones(1.0f, anim, out), "draws the plain animation");
    }

    // ── Stagger ────────────────────────────────────────────
    {
        std::printf("stagger\n");
        Body b;
        b.tick(5);
        b.rd.hit(shoveAt(b), shove(15.0f), b.mass.total());
        check(b.rd.state() == Ragdoll::State::Reacting, "a 15 vox/s shove staggers");
        float maxOff = 0;
        int frames = 0;
        while (b.rd.state() == Ragdoll::State::Reacting && frames < 400) {
            b.tick();
            frames++;
            maxOff = std::max(maxOff, glm::length(b.rd.point(RP_NECK) - b.target(RP_NECK)));
        }
        std::printf("    neck pushed up to %.2f voxels, back to the animation after %.2f s\n", maxOff, frames * DT);
        check(maxOff > 0.5f, "the body visibly moves");
        check(b.rd.state() == Ragdoll::State::Animated, "hands back to the animation");
        check(frames * DT < 2.6f, "within 2.6 s");
        check(b.finite(), "no NaN");
    }

    // ── Knockdown ──────────────────────────────────────────
    {
        std::printf("knockdown\n");
        Body b;
        b.tick(5);
        b.rd.hit(shoveAt(b), shove(55.0f) + glm::vec3(0, 20 * 70.0f, 0), b.mass.total());
        check(b.rd.isLimp(), "a 55 vox/s hit knocks down");
        int frames = 0;
        float lowest = 1e9f;
        bool stood = false;
        while (frames < 60 * 8) {
            b.tick();
            frames++;
            lowest = std::min(lowest, b.chestHeight());
            if (b.rd.takeStandUp()) {
                stood = true;
                std::printf("    down for %.2f s, chest %.2f voxels off the ground, pelvis at (%.1f, %.1f, %.1f)\n",
                            frames * DT, b.chestHeight(), b.rd.point(RP_PELVIS).x, b.rd.point(RP_PELVIS).y,
                            b.rd.point(RP_PELVIS).z);
                check(b.chestHeight() < 3.0f, "lying on the ground when it gets up");
                // the game moves the capsule under the body here
                glm::vec3 p = b.rd.point(RP_PELVIS);
                b.feet = glm::vec3(p.x, 0.0f, p.z);
            }
            if (stood && b.rd.state() == Ragdoll::State::Animated) break;
            if (b.anyInsideSolid()) { check(false, "a point went into the ground"); break; }
        }
        check(stood, "gets up");
        check(b.rd.state() == Ragdoll::State::Animated, "back to the animation after getting up");
        check(b.finite(), "no NaN");
        check(b.rd.point(RP_PELVIS).z < -2.0f, "thrown backward");
    }

    // ── Shoved while sprinting ─────────────────────────────
    {
        std::printf("shoved while sprinting\n");
        Body b;
        b.tick(5);
        b.rd.hit(shoveAt(b), glm::vec3(-15.0f, 0.0f, 0.0f) * 70.0f, b.mass.total());   // from the side
        float lag = 0;
        int frames = 0;
        while (b.rd.state() == Ragdoll::State::Reacting && frames < 400) {
            b.feet.z += 50.0f * DT;           // the capsule keeps running at sprint speed
            b.tick();
            frames++;
            lag = std::max(lag, glm::length(b.rd.point(RP_PELVIS) - b.target(RP_PELVIS)));
        }
        std::printf("    pelvis trailed by up to %.2f voxels, back after %.2f s\n", lag, frames * DT);
        check(b.rd.state() == Ragdoll::State::Animated, "hands back while running");
        check(frames * DT < 1.5f, "within 1.5 s");
        check(lag < 3.0f, "never trails more than 3 voxels (1 ft)");
    }

    // ── Links keep their length while tumbling ─────────────
    {
        std::printf("links\n");
        Body b;
        b.tick(5);
        float rest = glm::length(b.rd.point(RP_KNEE_L) - b.rd.point(RP_ANKLE_L));
        float restArm = glm::length(b.rd.point(RP_SHOULDER_R) - b.rd.point(RP_ELBOW_R));
        b.rd.hit(b.rd.point(RP_HEAD), glm::vec3(30, 30, -50) * 70.0f, b.mass.total());
        float worst = 0;
        for (int i = 0; i < 90; i++) {
            b.tick();
            float a = glm::length(b.rd.point(RP_KNEE_L) - b.rd.point(RP_ANKLE_L));
            float c = glm::length(b.rd.point(RP_SHOULDER_R) - b.rd.point(RP_ELBOW_R));
            worst = std::max({ worst, std::fabs(a - rest) / rest, std::fabs(c - restArm) / restArm });
        }
        std::printf("    worst stretch %.1f%%\n", worst * 100);
        check(worst < 0.06f, "shin and upper arm stay within 6% of their length");
    }

    // ── Mass decides stagger vs knockdown ──────────────────
    {
        std::printf("mass\n");
        Body light(50.0f), heavy(110.0f);
        light.tick(5); heavy.tick(5);
        glm::vec3 J = shove(40.0f);   // 2800 kg·vox/s
        light.rd.hit(shoveAt(light), J, light.mass.total());
        heavy.rd.hit(shoveAt(heavy), J, heavy.mass.total());
        std::printf("    same hit: 50 kg is %s, 110 kg is %s\n", stateName(light.rd.state()), stateName(heavy.rd.state()));
        check(light.rd.isLimp(), "a 50 kg body goes down");
        check(heavy.rd.state() == Ragdoll::State::Reacting, "a 110 kg body only staggers");
        float lightMove = 0, heavyMove = 0;
        glm::vec3 l0 = light.rd.point(RP_WAIST), h0 = heavy.rd.point(RP_WAIST);
        for (int i = 0; i < 10; i++) { light.tick(); heavy.tick(); }
        lightMove = glm::length(light.rd.point(RP_WAIST) - l0);
        heavyMove = glm::length(heavy.rd.point(RP_WAIST) - h0);
        std::printf("    chest moved %.2f vs %.2f voxels in 1/6 s\n", lightMove, heavyMove);
        check(lightMove > heavyMove * 1.5f, "the light body is thrown further");
    }

    // ── Weight in the hand ─────────────────────────────────
    {
        std::printf("weapon weight\n");
        auto sag = [](float kg) {
            Body b;
            b.mass.addGear(SEG_HAND_R, kg);
            b.rd.setMass(b.mass);
            b.rd.alwaysActive = true;
            // arm held straight out in front (shoulder pitched forward)
            b.pose.rot[B_SHOULDER_R] = glm::vec3(-90.0f, 0.0f, 0.0f);
            b.tick(120);
            return b.target(RP_HAND_R).y - b.rd.point(RP_HAND_R).y;
        };
        float none = sag(0.0f), sword = sag(1.5f), anvil = sag(25.0f);
        std::printf("    hand below its animated height: empty %.2f, 1.5 kg %.2f, 25 kg %.2f voxels\n", none, sword, anvil);
        check(std::fabs(none) < 1.0f, "an empty hand holds its pose");
        check(sword < 1.5f, "a sword is easy to hold out");
        check(anvil > sword + 1.0f, "25 kg drags the arm down");
    }

    // ── Wall ───────────────────────────────────────────────
    {
        std::printf("wall\n");
        Body b;
        b.world.wallX = 4;
        b.tick(5);
        b.rd.hit(shoveAt(b), glm::vec3(70, 15, 0) * 70.0f, b.mass.total());
        bool inside = false;
        float maxX = -1e9f;
        for (int i = 0; i < 180; i++) {
            b.tick();
            if (b.anyInsideSolid()) inside = true;
            for (int k = 0; k < RP_COUNT; k++) maxX = std::max(maxX, b.rd.point(k).x + b.rd.pointRadius(k));
        }
        std::printf("    furthest point reached x = %.2f (wall at 4)\n", maxX);
        check(!inside, "no point inside the wall or the ground");
        check(maxX < 4.15f, "stopped at the wall");
    }

    // ── Determinism ────────────────────────────────────────
    {
        std::printf("determinism\n");
        auto run = []() {
            Body b;
            b.tick(3);
            b.rd.hit(shoveAt(b), glm::vec3(20, 25, -45) * 70.0f, b.mass.total());
            b.tick(150);
            return b.rd.point(RP_HEAD);
        };
        glm::vec3 a = run(), c = run();
        check(a == c, "two identical runs end identically");
    }

    // ── Cost ───────────────────────────────────────────────
    {
        std::printf("cost\n");
        Body b;
        b.tick(3);
        b.rd.hit(shoveAt(b), shove(60.0f), b.mass.total());
        auto t0 = std::chrono::steady_clock::now();
        const int N = 600;
        b.tick(N);
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
        std::printf("    %.1f microseconds per tick while simulated\n", us);
        check(us < 500.0, "under 0.5 ms per tick");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
