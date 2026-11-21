#pragma once
#include <cmath>

// ══════════════════════════════════════════════════════════
// DayClock — the world's time of day (no graphics: the server
// keeps it; render/Sky.h turns it into a sky)
// ══════════════════════════════════════════════════════════
struct DayClock {
    float time = 0.35f;              // 0..1 through the day; starts mid-morning
    float dayLengthSeconds = 1200.0f; // 20 minutes
    bool  frozen = false;
    void update(float dt) {
        if (frozen || dayLengthSeconds <= 0) return;
        time = std::fmod(time + dt / dayLengthSeconds, 1.0f);
    }
};

// Every world keeps its own (Simulation::day); there's no global one
