#pragma once
#include "VoiceSynth.h"     // Rng, Reso
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Sfx — sound effects made from what things are made of
// ══════════════════════════════════════════════════════════
// No recordings yet: every effect is built from noise, clicks and
// ringing tones, shaped by the MATERIAL (blocks.toml material:
// stone, dirt, wood, organic, fluid, metal …; audio/Materials.h).
// A footstep on stone is a short bright click with grit; on wood a
// hollow knock; on dirt a soft thud; in leaves and grass a crackle;
// in water a splash. Breaking is the same, bigger, with debris. A
// few variants each (seed), so steps don't repeat exactly. Pure
// C++ like the voice (tests/sound_test.cpp checks them); recordings
// can replace any of them later (data/audio/materials.toml).
namespace sfx {

enum class Kind : uint8_t {
    Step,       // a foot down
    Land,       // a jump comes down
    Break,      // a block breaks
    Place,      // a block / piece goes down
    Chop,       // an axe in wood
    Hit,        // a blow lands on a body
    Block,      // a blow on a guard (material: the guard's)
    Swing,      // a weapon cuts the air
    Cast,       // a spell leaves the hand
    Door,       // a door opens / shuts
    Blast,      // an explosion
    Splash,     // into water
    Wind,       // the wind bed (a long loop)
    COUNT
};

// How a material sounds when struck (audio/Materials.h fills these
// from the material's name)
struct Tone {
    float clickHz = 3000.0f;   // the bright tick at the start
    float body = 600.0f;       // the main resonance (a knock's note)
    float bodyQ = 6.0f;        // how long it rings (0: no note, just noise)
    float noiseHz = 1500.0f;   // the grit / rustle
    float decay = 0.05f;       // s
    float crackle = 0.0f;      // 0 .. 1: grains (leaves, gravel, snow)
    float wet = 0.0f;          // 0 .. 1: bubbles (water)
    float ring = 0.0f;         // 0 .. 1: metal partials
};

struct Buffer {
    int rate = 22050;
    std::vector<float> samples;
    bool loop = false;
};

// A short shaped burst: click + resonant body + filtered noise
inline void strike(std::vector<float>& out, int rate, const Tone& t, float at, float loud, float length, voicesynth::Rng& rng) {
    size_t start = (size_t)(at * rate), n = (size_t)(length * rate);
    if (out.size() < start + n) out.resize(start + n, 0.0f);
    voicesynth::Reso body, grit, click;
    body.set(t.body, t.bodyQ > 0 ? t.body / t.bodyQ : 400.0f, (float)rate);
    grit.set(std::min(t.noiseHz, rate * 0.45f), t.noiseHz * 0.8f, (float)rate);
    click.set(std::min(t.clickHz, rate * 0.45f), t.clickHz * 0.5f, (float)rate);
    float bodyNorm = 1.0f / std::max(1.0f, body.gainAt(t.body, (float)rate));
    float gritNorm = 1.0f / std::max(1.0f, grit.gainAt(std::min(t.noiseHz, rate * 0.45f), (float)rate));
    float clickNorm = 1.0f / std::max(1.0f, click.gainAt(std::min(t.clickHz, rate * 0.45f), (float)rate));
    for (size_t i = 0; i < n; i++) {
        float s = (float)i / rate;
        float env = std::exp(-s / std::max(0.005f, t.decay));
        float attack = std::min(1.0f, s / 0.002f);
        float noise = rng.signedUnit();
        float v = 0.0f;
        v += click.run(s < 0.004f ? noise : 0.0f) * clickNorm * 1.2f;
        if (t.bodyQ > 0) v += body.run(s < 0.003f ? noise : 0.0f) * bodyNorm * 3.0f;
        float g = grit.run(noise) * gritNorm;
        if (t.crackle > 0.0f) g *= (rng.unit() < 0.04f + t.crackle * 0.1f) ? 3.0f : 0.25f;   // grains
        v += g * 0.7f;
        out[start + i] += v * env * attack * loud;
    }
}
// Metal: a few inharmonic partials ringing on
inline void ringOut(std::vector<float>& out, int rate, float at, float baseHz, float loud, float length, voicesynth::Rng& rng) {
    static const float P[] = { 1.0f, 2.76f, 5.40f, 8.93f };
    size_t start = (size_t)(at * rate), n = (size_t)(length * rate);
    if (out.size() < start + n) out.resize(start + n, 0.0f);
    float ph[4] = { rng.unit(), rng.unit(), rng.unit(), rng.unit() };
    for (size_t i = 0; i < n; i++) {
        float s = (float)i / rate, v = 0.0f;
        for (int k = 0; k < 4; k++) {
            ph[k] += baseHz * P[k] / rate;
            v += std::sin(6.2831853f * ph[k]) * std::exp(-s * (3.0f + k * 4.0f)) / (1 + k);
        }
        out[start + i] += v * loud * 0.6f * std::min(1.0f, s / 0.001f);
    }
}
// Bubbles: short rising sine chirps (water)
inline void bubbles(std::vector<float>& out, int rate, float at, int count, float loud, voicesynth::Rng& rng) {
    for (int b = 0; b < count; b++) {
        float t0 = at + rng.unit() * 0.25f, f = 400.0f + rng.unit() * 900.0f, len = 0.03f + rng.unit() * 0.04f;
        size_t start = (size_t)(t0 * rate), n = (size_t)(len * rate);
        if (out.size() < start + n) out.resize(start + n, 0.0f);
        float ph = 0.0f;
        for (size_t i = 0; i < n; i++) {
            float s = (float)i / rate;
            ph += f * (1.0f + s * 12.0f) / rate;
            out[start + i] += std::sin(6.2831853f * ph) * std::exp(-s * 40.0f) * loud * 0.4f;
        }
    }
}
// A filtered noise sweep (a swing, a cast's whoosh, the wind)
inline void sweep(std::vector<float>& out, int rate, float at, float length, float fromHz, float toHz, float loud,
                  voicesynth::Rng& rng, float q = 1.5f) {
    size_t start = (size_t)(at * rate), n = (size_t)(length * rate);
    if (out.size() < start + n) out.resize(start + n, 0.0f);
    voicesynth::Reso r;
    for (size_t i = 0; i < n; i++) {
        float k = (float)i / n;
        if ((i & 31) == 0) { float f = fromHz * std::pow(toHz / fromHz, k); r.set(std::min(f, rate * 0.45f), f / q, (float)rate); }
        float env = std::sin(3.14159265f * k);
        out[start + i] += r.run(rng.signedUnit()) * env * loud * 0.25f;
    }
}

inline void normalise(std::vector<float>& s, float to) {
    float peak = 0.0f;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    if (peak > 1e-6f) for (float& v : s) v *= to / peak;
}

// One effect. 'tone': the material struck (or underfoot); 'variant'
// picks one of a few takes; 'size' scales it (a big break, a heavy landing)
inline Buffer make(Kind kind, const Tone& toneIn, uint32_t variant, float size = 1.0f, int rate = 22050) {
    Buffer b;
    b.rate = rate;
    voicesynth::Rng rng(0x51ed270bu ^ (uint32_t)kind * 2654435761u ^ variant * 40503u);
    Tone t = toneIn;
    // each take a little different
    float detune = 1.0f + (rng.unit() - 0.5f) * 0.16f;
    t.body *= detune; t.noiseHz *= detune; t.clickHz *= detune;
    std::vector<float>& s = b.samples;
    switch (kind) {
    case Kind::Step:
        strike(s, rate, t, 0.0f, 0.7f, t.decay * 4 + 0.03f, rng);
        if (t.crackle > 0.0f) strike(s, rate, t, 0.015f + rng.unit() * 0.02f, 0.35f, t.decay * 3 + 0.03f, rng);
        if (t.wet > 0.0f) bubbles(s, rate, 0.01f, 3, t.wet, rng);
        if (t.ring > 0.0f) ringOut(s, rate, 0.0f, t.body * 1.5f, t.ring * 0.4f, 0.25f, rng);
        normalise(s, 0.55f);
        break;
    case Kind::Land: {
        Tone heavy = t; heavy.body *= 0.6f; heavy.decay *= 1.6f; heavy.noiseHz *= 0.7f;
        strike(s, rate, heavy, 0.0f, 1.0f, heavy.decay * 4 + 0.05f, rng);
        if (t.wet > 0.0f) bubbles(s, rate, 0.02f, 8, t.wet, rng);
        normalise(s, 0.7f * std::min(1.0f, 0.6f + size * 0.4f));
        break;
    }
    case Kind::Break: {
        // a crack, then pieces landing
        Tone crack = t; crack.decay *= 1.5f;
        strike(s, rate, crack, 0.0f, 1.0f, crack.decay * 5 + 0.05f, rng);
        int bits = 3 + (int)(size * 3);
        for (int i = 0; i < bits; i++) strike(s, rate, t, 0.05f + rng.unit() * 0.22f, 0.25f + rng.unit() * 0.3f, t.decay * 2 + 0.03f, rng);
        if (t.ring > 0.0f) ringOut(s, rate, 0.0f, t.body * 1.2f, t.ring, 0.6f, rng);
        if (t.wet > 0.0f) bubbles(s, rate, 0.0f, 10, t.wet, rng);
        normalise(s, 0.8f);
        break;
    }
    case Kind::Place: {
        Tone thunk = t; thunk.body *= 0.8f;
        strike(s, rate, thunk, 0.0f, 1.0f, thunk.decay * 3 + 0.04f, rng);
        normalise(s, 0.6f);
        break;
    }
    case Kind::Chop: {
        Tone wood = t; wood.bodyQ = std::max(wood.bodyQ, 8.0f); wood.decay = std::max(wood.decay, 0.07f);
        strike(s, rate, wood, 0.0f, 1.0f, 0.3f, rng);
        normalise(s, 0.8f);
        break;
    }
    case Kind::Hit: {
        // a thump into a body: a low knock with a falling pitch, and a slap
        size_t n = (size_t)(0.18f * rate);
        s.resize(n, 0.0f);
        float ph = 0.0f;
        for (size_t i = 0; i < n; i++) {
            float x = (float)i / rate;
            ph += (110.0f - 50.0f * std::min(1.0f, x / 0.08f)) * detune / rate;
            s[i] += std::sin(6.2831853f * ph) * std::exp(-x * 22.0f) * 0.9f;
        }
        Tone slap; slap.bodyQ = 0; slap.noiseHz = 1400; slap.decay = 0.03f;
        strike(s, rate, slap, 0.0f, 0.6f, 0.1f, rng);
        normalise(s, 0.8f * std::min(1.0f, 0.5f + size * 0.5f));
        break;
    }
    case Kind::Block: {
        // on a guard: the guard's material clanks (a shield's wood, a blade's metal)
        strike(s, rate, t, 0.0f, 1.0f, t.decay * 4 + 0.05f, rng);
        if (t.ring > 0.0f) ringOut(s, rate, 0.0f, 900.0f * detune, t.ring, 0.5f, rng);
        normalise(s, 0.75f);
        break;
    }
    case Kind::Swing:
        sweep(s, rate, 0.0f, 0.22f, 500.0f, 2200.0f, 1.0f, rng, 2.0f);
        normalise(s, 0.35f);
        break;
    case Kind::Cast: {
        sweep(s, rate, 0.0f, 0.4f, 300.0f, 3000.0f, 1.0f, rng, 3.0f);
        size_t n = (size_t)(0.45f * rate);
        if (s.size() < n) s.resize(n, 0.0f);
        float ph[3] = { 0, 0, 0 };
        for (size_t i = 0; i < n; i++) {
            float x = (float)i / rate, env = std::sin(3.14159265f * x / 0.45f);
            for (int k = 0; k < 3; k++) { ph[k] += (600.0f + k * 450.0f) * (1.0f + x * 2.0f) * detune / rate; s[i] += std::sin(6.2831853f * ph[k]) * env * 0.15f; }
        }
        normalise(s, 0.5f);
        break;
    }
    case Kind::Door: {
        // a creak (a slow, wobbling saw through a wood body), then the knock
        size_t n = (size_t)(0.45f * rate);
        s.resize(n, 0.0f);
        voicesynth::Reso r; r.set(t.body, 120.0f, (float)rate);
        float norm = 1.0f / std::max(1.0f, r.gainAt(t.body, (float)rate));
        float ph = 0.0f;
        for (size_t i = 0; i < n; i++) {
            float x = (float)i / rate;
            ph += (35.0f + 25.0f * std::sin(x * 18.0f) + rng.signedUnit() * 6.0f) / rate;
            float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
            s[i] += r.run(saw) * norm * std::sin(3.14159265f * std::min(1.0f, x / 0.35f)) * 0.5f;
        }
        strike(s, rate, t, 0.36f, 0.9f, 0.12f, rng);
        normalise(s, 0.6f);
        break;
    }
    case Kind::Blast: {
        size_t n = (size_t)(1.4f * rate);
        s.resize(n, 0.0f);
        voicesynth::Reso lo; lo.set(80.0f, 120.0f, (float)rate);
        float norm = 1.0f / std::max(1.0f, lo.gainAt(80.0f, (float)rate));
        for (size_t i = 0; i < n; i++) {
            float x = (float)i / rate;
            float v = lo.run(rng.signedUnit()) * norm * 2.5f + rng.signedUnit() * std::exp(-x * 30.0f) * 0.5f;
            s[i] = v * std::exp(-x * 3.0f) * std::min(1.0f, x / 0.004f);
        }
        for (int i = 0; i < 8; i++) strike(s, rate, t, 0.1f + rng.unit() * 0.8f, 0.2f, 0.08f, rng);   // debris
        normalise(s, 0.95f);
        break;
    }
    case Kind::Splash: {
        Tone w; w.bodyQ = 0; w.noiseHz = 2200; w.decay = 0.12f;
        strike(s, rate, w, 0.0f, 1.0f, 0.4f, rng);
        bubbles(s, rate, 0.03f, 12, 1.0f, rng);
        normalise(s, 0.6f);
        break;
    }
    case Kind::Wind: {
        // a 4 s loop of soft gusting noise (the engine fades it with exposure)
        size_t n = (size_t)(4.0f * rate);
        s.resize(n, 0.0f);
        voicesynth::Reso band;
        float brown = 0.0f;
        for (size_t i = 0; i < n; i++) {
            float x = (float)i / n;
            float gust = 0.6f + 0.25f * std::sin(6.2831853f * x * 2) + 0.15f * std::sin(6.2831853f * x * 5 + 1.3f);   // whole cycles: loops cleanly
            if ((i & 63) == 0) band.set(250.0f + gust * 500.0f, 300.0f, (float)rate);
            brown = brown * 0.985f + rng.signedUnit() * 0.15f;
            s[i] = (band.run(rng.signedUnit()) * 0.12f + brown * 0.5f) * gust;
        }
        // fold the end into the start so the loop has no seam
        const size_t F = (size_t)(0.25f * rate);
        for (size_t i = 0; i < F; i++) {
            float k = (float)i / F;
            s[i] = s[i] * k + s[n - F + i] * (1.0f - k);
        }
        s.resize(n - F);
        normalise(s, 0.5f);
        b.loop = true;
        break;
    }
    default: break;
    }
    return b;
}

} // namespace sfx
