#pragma once
// Controller rumble: the pad shakes when a hit lands, when you're hit,
// on a block and a parry.
//
// GLFW has no rumble, so on Windows this talks to XInput directly
// (loaded at run time: xinput1_4, else 1_3, else 9_1_0, so nothing
// links against it and a machine without it just doesn't shake). Any
// XInput pad works, which covers Xbox pads and most others through
// Steam Input. Elsewhere it does nothing.
//
// The XInput call lives in input/Rumble.cpp: windows.h must never be
// included by a header (its near / far / small macros break code
// after it). That file sets rumble::gMotors when the game starts; the
// server and the tests don't build it, so for them it stays empty.
//
// Use: kick() on an event, update() once a frame. A kick is two motor
// strengths (low: the heavy left motor, a thud; high: the light right
// one, a buzz) for some seconds; overlapping kicks take the stronger.
// The setting (Settings > General > Controller rumble) scales them:
// off, light (half), full.

#include "game/Settings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rumble {

// sets every connected pad's motors (0..1); set by input/Rumble.cpp
inline void (*gMotors)(float low, float high) = nullptr;

struct Kick { float low = 0.0f, high = 0.0f, left = 0.0f, time = 0.0f; };

class Rumble {
public:
    // Shake for 'seconds': low / high 0..1. Fades out over the time.
    void kick(float low, float high, float seconds) {
        if (gSettings.rumble <= 0 || seconds <= 0.0f) return;
        float scale = gSettings.rumble == 1 ? 0.5f : 1.0f;
        for (Kick& k : kicks) if (k.left <= 0.0f) { k = { low * scale, high * scale, seconds, seconds }; return; }
        // all busy: replace the one closest to done
        Kick* soon = &kicks[0];
        for (Kick& k : kicks) if (k.left < soon->left) soon = &k;
        *soon = { low * scale, high * scale, seconds, seconds };
    }

    // Once a frame. 'usingPad': only a pad in use shakes (not one lying
    // on the desk while you play with the mouse)
    void update(float dt, bool usingPad) {
        float lo = 0.0f, hi = 0.0f;
        for (Kick& k : kicks) {
            if (k.left <= 0.0f) continue;
            k.left -= dt;
            float f = std::clamp(k.left / std::max(k.time, 1e-3f), 0.0f, 1.0f);
            f = std::sqrt(f);                      // holds, then drops away
            lo = std::max(lo, k.low * f);
            hi = std::max(hi, k.high * f);
        }
        if (!usingPad || gSettings.rumble <= 0) lo = hi = 0.0f;
        set(lo, hi);
    }

    // Stop at once (pausing, quitting)
    void stop() { for (Kick& k : kicks) k.left = 0.0f; set(0.0f, 0.0f); }

    float lowNow() const { return lastLo; }       // (tests, the debug view)
    float highNow() const { return lastHi; }

    ~Rumble() { set(0.0f, 0.0f); }

private:
    Kick kicks[4];
    float lastLo = -1.0f, lastHi = -1.0f;

    void set(float lo, float hi) {
        // only send a change (XInputSetState isn't free, and a level
        // within a step of the last needn't go out)
        auto q = [](float v) { return (uint16_t)std::clamp(v * 65535.0f, 0.0f, 65535.0f); };
        if (lastLo >= 0.0f && q(lo) / 1024 == q(lastLo) / 1024 && q(hi) / 1024 == q(lastHi) / 1024 && (lo > 0.0f) == (lastLo > 0.0f) && (hi > 0.0f) == (lastHi > 0.0f)) return;
        lastLo = lo; lastHi = hi;
        if (gMotors) gMotors(lo, hi);
    }
};

inline Rumble gRumble;

} // namespace rumble
