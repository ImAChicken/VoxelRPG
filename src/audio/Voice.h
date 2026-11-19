#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

// ══════════════════════════════════════════════════════════
// Voice — how someone sounds (audio/VoiceSynth.h speaks with it)
// ══════════════════════════════════════════════════════════
// A handful of numbers, every one 0 .. 1 (sliders in the character
// creator's Voice tab), so every player and NPC can sound different
// with no recordings. Saved with the look ("look_voice" in player.dat
// and the character files: character/Appearance.h), so other players
// on a server hear it too; NPC types set theirs in npcs.toml.
struct Voice {
    float pitch  = 0.45f;   // low .. high (about 70 .. 380 Hz)
    float range  = 0.40f;   // flat .. sing-song (how far the pitch moves)
    float size   = 0.50f;   // tiny .. huge (the throat: formants up or down)
    float speed  = 0.50f;   // slow .. chattery
    float breath = 0.20f;   // clear .. breathy / whispery
    float rough  = 0.10f;   // smooth .. gravelly (jitter in the buzz)
    float nasal  = 0.10f;   // open .. nasal
    int   style  = 0;       // STYLES: how the words become mumble
    float volume = 0.80f;

    static constexpr int STYLE_COUNT = 3;
    static const char* styleName(int s) {
        static const char* N[STYLE_COUNT] = { "Soft", "Chirpy", "Grunty" };
        return N[std::clamp(s, 0, STYLE_COUNT - 1)];
    }

    // The buzz's base frequency, Hz
    float baseHz() const { return 70.0f * std::pow(380.0f / 70.0f, std::clamp(pitch, 0.0f, 1.0f)); }
    // Formants × this (a small throat: higher)
    float formantScale() const { return 1.40f - 0.70f * std::clamp(size, 0.0f, 1.0f); }
    // Syllables a second
    float syllablesPerSecond() const { return 3.2f + 6.0f * std::clamp(speed, 0.0f, 1.0f); }

    void clamp() {
        for (float* f : { &pitch, &range, &size, &speed, &breath, &rough, &nasal, &volume })
            *f = std::round(std::clamp(*f, 0.0f, 1.0f) * 1000.0f) / 1000.0f;
        style = std::clamp(style, 0, STYLE_COUNT - 1);
    }
    bool operator==(const Voice& o) const {
        return pitch == o.pitch && range == o.range && size == o.size && speed == o.speed && breath == o.breath &&
               rough == o.rough && nasal == o.nasal && style == o.style && volume == o.volume;
    }
    bool operator!=(const Voice& o) const { return !(*this == o); }

    // "pitch range size speed breath rough nasal style volume"
    std::string text() const {
        char b[160];
        std::snprintf(b, sizeof(b), "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %d %.3f",
                      pitch, range, size, speed, breath, rough, nasal, style, volume);
        return b;
    }
    bool parse(const std::string& s) {
        Voice v;
        int n = std::sscanf(s.c_str(), "%f %f %f %f %f %f %f %d %f", &v.pitch, &v.range, &v.size, &v.speed,
                            &v.breath, &v.rough, &v.nasal, &v.style, &v.volume);
        if (n < 8) return false;
        if (n < 9) v.volume = volume;
        *this = v;
        clamp();
        return true;
    }

    // Somebody new (the creator's Random button; NPCs without one)
    static Voice random(uint32_t seed) {
        std::mt19937 rng(seed);
        auto u = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
        Voice v;
        v.pitch = u(0.15f, 0.8f); v.range = u(0.2f, 0.7f); v.size = u(0.3f, 0.7f);
        v.speed = u(0.3f, 0.7f); v.breath = u(0.05f, 0.4f); v.rough = u(0.0f, 0.35f);
        v.nasal = u(0.0f, 0.35f); v.style = (int)(rng() % 2);   // grunty is for creatures, mostly
        v.clamp();
        return v;
    }
    // This voice, a little different (each NPC of a type)
    Voice varied(uint32_t seed, float amount) const {
        std::mt19937 rng(seed);
        auto d = [&]() { return std::uniform_real_distribution<float>(-amount, amount)(rng); };
        Voice v = *this;
        v.pitch += d(); v.range += d() * 0.5f; v.size += d(); v.speed += d() * 0.5f;
        v.breath += d() * 0.5f; v.rough += d() * 0.5f;
        v.clamp();
        return v;
    }
};
