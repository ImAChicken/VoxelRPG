#pragma once
#include "Voice.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// VoiceSynth — mumble speech from text (no recordings)
// ══════════════════════════════════════════════════════════
// A formant synth, the classic source-filter voice:
//
//   source   a buzz at the voice's pitch (the vocal cords: a
//            Rosenberg glottal pulse), plus breath noise
//   filter   four resonances in a row (the formants). A vowel is
//            only where they sit: "ah" 730 / 1090 / 2440 Hz,
//            "ee" 270 / 2290 / 3010 … moved up or down by the
//            voice's size
//   text     each word becomes a few mumbled syllables: its vowel
//            groups pick the vowels, the consonant before each one
//            a short burst (p t k), hiss (s f h), hum (m n) or glide
//            (l r w). Not real words: the rhythm, length and tune of
//            the line, like Animal Crossing or Banjo-Kazooie. The
//            same line in the same voice always sounds the same
//
// It also says how open the mouth is at every moment (from the
// vowel and the loudness), so the face moves with it: mouthAt(t).
// Pure C++: the game plays the samples (audio/AudioEngine.h); the
// tests check them (tests/sound_test.cpp).
namespace voicesynth {

struct Utterance {
    int rate = 22050;
    std::vector<float> samples;          // mono, -1 .. 1
    std::vector<float> mouth;            // how open, 0 .. 1, MOUTH_HZ a second
    int syllables = 0;
    static constexpr int MOUTH_HZ = 60;
    float seconds() const { return rate > 0 ? (float)samples.size() / rate : 0.0f; }
    float mouthAt(float t) const {
        if (mouth.empty() || t < 0.0f) return 0.0f;
        float f = t * MOUTH_HZ;
        size_t i = (size_t)f;
        if (i + 1 >= mouth.size()) return i < mouth.size() ? mouth[i] : 0.0f;
        float k = f - (float)i;
        return mouth[i] * (1 - k) + mouth[i + 1] * k;
    }
};

// ── Sounds ───────────────────────────────────────────────
enum class V : uint8_t { AA, AE, EH, IY, OW, UW, UH };       // vowels
struct VowelShape { float f[4]; float open; };
inline const VowelShape& vowel(V v) {
    static const VowelShape S[] = {
        { { 730, 1090, 2440, 3400 }, 1.00f },   // AA  "father"
        { { 660, 1720, 2410, 3400 }, 0.85f },   // AE  "cat"
        { { 530, 1840, 2480, 3500 }, 0.60f },   // EH  "bed"
        { { 270, 2290, 3010, 3600 }, 0.30f },   // IY  "see"
        { { 570,  840, 2410, 3300 }, 0.70f },   // OW  "go"
        { { 300,  870, 2240, 3300 }, 0.35f },   // UW  "too"
        { { 520, 1190, 2390, 3400 }, 0.55f },   // UH  "but"
    };
    return S[(int)v];
}
enum class C : uint8_t { None, Plosive, VoicedPlosive, Fricative, VoicedFricative, Nasal, Liquid, Aspirate };
struct Consonant { C kind = C::None; float noiseHz = 0.0f; };
inline Consonant consonant(char c) {
    switch (c) {
    case 'p': return { C::Plosive, 900 };   case 't': return { C::Plosive, 3500 };
    case 'k': case 'c': case 'q': return { C::Plosive, 1800 };
    case 'b': return { C::VoicedPlosive, 800 }; case 'd': return { C::VoicedPlosive, 3000 };
    case 'g': return { C::VoicedPlosive, 1600 };
    case 's': case 'x': return { C::Fricative, 5800 }; case 'f': return { C::Fricative, 4000 };
    case 'h': return { C::Aspirate, 2000 };
    case 'z': return { C::VoicedFricative, 5200 }; case 'v': return { C::VoicedFricative, 3500 };
    case 'j': return { C::VoicedFricative, 2600 };
    case 'm': case 'n': return { C::Nasal, 0 };
    case 'l': case 'r': case 'w': return { C::Liquid, 0 };
    default: return { C::None, 0 };
    }
}
inline bool isVowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
inline V vowelFor(const std::string& g) {
    if (g.rfind("ee", 0) == 0 || g.rfind("ea", 0) == 0 || g.rfind("ie", 0) == 0) return V::IY;
    if (g.rfind("oo", 0) == 0 || g.rfind("ou", 0) == 0) return V::UW;
    switch (g[0]) {
    case 'a': return g.size() > 1 ? V::EH : V::AE;
    case 'e': return V::EH; case 'i': case 'y': return V::IY;
    case 'o': return V::OW; default: return V::UH;
    }
}

// ── The plan: syllables, then samples ────────────────────
struct Syl {
    Consonant onset, coda;
    V v = V::UH;
    float semis = 0.0f;       // pitch, semitones from the voice's
    float glide = 0.0f;       // more semitones by its end (a question rises)
    float loud = 1.0f;
    float pauseAfter = 0.0f;  // s of silence after it
    bool wordStart = false;
};

// Small, fast, repeatable noise
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() & 0xffffff) / 16777216.0f; }     // 0 .. 1
    float signedUnit() { return unit() * 2.0f - 1.0f; }            // -1 .. 1
};
inline uint32_t hashText(const std::string& s, uint32_t h = 2166136261u) {
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

inline std::vector<Syl> plan(const Voice& voice, const std::string& text, Rng& rng) {
    std::vector<Syl> out;
    std::string word;
    const float range = 1.0f + voice.range * 5.0f;               // semitones a syllable may wander
    auto flushWord = [&]() {
        if (word.empty()) return;
        // vowel groups, and the consonants around them
        std::vector<std::pair<std::string, std::string>> groups;   // (consonants before, vowels)
        std::string cons, tail;
        for (size_t i = 0; i < word.size(); i++) {
            char c = word[i];
            bool vw = isVowel(c) && !(c == 'y' && i == 0);
            if (vw) {
                std::string g(1, c);
                while (i + 1 < word.size() && isVowel(word[i + 1])) g += word[++i];
                groups.push_back({ cons, g });
                cons.clear();
            } else cons += c;
        }
        tail = cons;
        // a silent final e ("make", "stone")
        if (groups.size() > 1 && groups.back().second == "e" && tail.empty()) groups.pop_back();
        if (groups.empty()) groups.push_back({ word.substr(0, 1), "u" });   // "hmm", "shh"
        if (groups.size() > 4) groups.resize(4);                            // a mumble, not a reading
        for (size_t g = 0; g < groups.size(); g++) {
            Syl s;
            const std::string& c = groups[g].first;
            if (!c.empty()) s.onset = consonant(c.back());
            s.v = vowelFor(groups[g].second);
            s.semis = rng.signedUnit() * range * 0.5f + (g == 0 ? range * 0.35f : 0.0f);   // first syllable stressed
            s.wordStart = g == 0;
            out.push_back(s);
        }
        if (!tail.empty()) out.back().coda = consonant(tail[0]);
        out.back().pauseAfter = 0.035f;
        word.clear();
    };
    for (char ch : text) {
        char c = (char)std::tolower((unsigned char)ch);
        if (std::isalpha((unsigned char)c)) { word += c; continue; }
        flushWord();
        if (out.empty()) continue;
        if (c == ',' || c == ';' || c == ':' || c == '-') out.back().pauseAfter = 0.16f;
        if (c == '.' || c == '!' || c == '?') {
            out.back().pauseAfter = 0.3f;
            if (c == '?') out.back().glide = 3.0f + voice.range * 6.0f;         // rises
            if (c == '!') for (size_t i = out.size(); i-- > 0 && out.size() - i <= 4;) { out[i].loud = 1.25f; out[i].semis += 2.0f; }
            if (c == '.') out.back().glide = -1.5f - voice.range * 2.0f;        // falls
        }
    }
    flushWord();
    // the line drifts down as it goes (declination), the style shapes it
    for (size_t i = 0; i < out.size(); i++) {
        float t = out.size() > 1 ? (float)i / (out.size() - 1) : 0.0f;
        out[i].semis -= t * (1.0f + voice.range * 2.0f);
        if (voice.style == 1) { out[i].semis += 3.0f + rng.signedUnit() * 2.0f; out[i].glide += 2.0f; }   // chirpy: up, skipping
        if (voice.style == 2) {                                                                             // grunty: low, open, rough
            out[i].semis -= 4.0f;
            if (out[i].v == V::IY || out[i].v == V::EH) out[i].v = V::UH;
            if (out[i].onset.kind == C::Fricative || out[i].onset.kind == C::Liquid) out[i].onset = { C::Aspirate, 900 };
        }
    }
    return out;
}

// A two-pole resonator (Klatt)
struct Reso {
    float a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
    void set(float hz, float bw, float rate) {
        float T = 1.0f / rate;
        c = -std::exp(-2.0f * 3.14159265f * bw * T);
        b = 2.0f * std::exp(-3.14159265f * bw * T) * std::cos(2.0f * 3.14159265f * hz * T);
        a = 1.0f - b - c;
    }
    float run(float x) { float y = a * x + b * y1 + c * y2; y2 = y1; y1 = y; return y; }
    // Its gain at hz (a narrow one rings far louder at its centre
    // than at DC, which 'a' is set for)
    float gainAt(float hz, float rate) const {
        float w = 2.0f * 3.14159265f * hz / rate;
        float re = 1.0f - b * std::cos(w) - c * std::cos(2 * w), im = b * std::sin(w) + c * std::sin(2 * w);
        return std::fabs(a) / std::max(1e-6f, std::sqrt(re * re + im * im));
    }
};

// What the filter is asked for at one moment
struct Target {
    float f[4] = { 500, 1500, 2500, 3500 };
    float voiced = 0.0f;      // buzz loudness
    float noise = 0.0f;       // frication loudness
    float noiseHz = 3000.0f;
    float asp = 0.0f;         // aspiration (an 'h')
    float nasal = 0.0f;       // nasal murmur (m n)
    float open = 0.0f;        // the mouth
    float semis = 0.0f;
};

inline Utterance speak(const Voice& voiceIn, const std::string& text, int rate = 22050, uint32_t seed = 0) {
    Voice voice = voiceIn;
    voice.clamp();
    Utterance u;
    u.rate = rate;
    Rng rng(hashText(text, seed ^ hashText(voice.text())));
    std::vector<Syl> syls = plan(voice, text, rng);
    u.syllables = (int)syls.size();
    if (syls.empty()) return u;

    // ── the timeline: (start time, target) keys ──────────
    struct Key { float t; Target tg; };
    std::vector<Key> keys;
    const float syl = 1.0f / voice.syllablesPerSecond() * (voice.style == 1 ? 0.8f : voice.style == 2 ? 1.15f : 1.0f);
    const float fs = voice.formantScale();
    const bool soft = voice.style == 0;
    float t = 0.05f;
    Target rest;                                        // silence, mouth shut
    keys.push_back({ 0.0f, rest });
    auto shaped = [&](V v) {
        Target tg;
        const VowelShape& s = vowel(v);
        for (int i = 0; i < 4; i++) tg.f[i] = s.f[i] * fs;
        tg.open = s.open;
        return tg;
    };
    for (const Syl& s : syls) {
        Target vw = shaped(s.v);
        vw.voiced = s.loud;
        vw.semis = s.semis;
        vw.nasal = voice.nasal * 0.5f;
        // the consonant before it
        float cons = 0.0f;
        Target ct = vw;
        ct.open = 0.15f;
        switch (s.onset.kind) {
        case C::Plosive: case C::VoicedPlosive: {
            // closure (quiet, mouth shut), then a burst into the vowel
            Target close = vw; close.voiced = s.onset.kind == C::VoicedPlosive ? 0.25f : 0.0f; close.open = 0.0f;
            keys.push_back({ t, close });
            float hold = syl * 0.12f;
            Target burst = vw; burst.voiced = s.onset.kind == C::VoicedPlosive ? 0.4f : 0.0f;
            burst.noise = soft ? 0.15f : 0.32f; burst.noiseHz = s.onset.noiseHz; burst.open = 0.3f;
            keys.push_back({ t + hold, burst });
            cons = hold + syl * 0.06f;
            break;
        }
        case C::Fricative: case C::VoicedFricative:
            ct.voiced = s.onset.kind == C::VoicedFricative ? 0.35f : 0.0f;
            ct.noise = soft ? 0.16f : 0.3f; ct.noiseHz = s.onset.noiseHz; ct.open = 0.2f;
            keys.push_back({ t, ct });
            cons = syl * 0.2f;
            break;
        case C::Aspirate:
            ct.voiced = 0.0f; ct.asp = 0.6f; ct.open = vw.open * 0.7f;
            keys.push_back({ t, ct });
            cons = syl * 0.16f;
            break;
        case C::Nasal:
            ct.voiced = 0.6f; ct.nasal = 1.0f; ct.open = 0.0f;
            ct.f[0] = 260 * fs; ct.f[1] = 1000 * fs;
            keys.push_back({ t, ct });
            cons = syl * 0.18f;
            break;
        case C::Liquid:
            ct.voiced = 0.8f; ct.open = 0.35f;
            ct.f[0] = 350 * fs; ct.f[1] = 1100 * fs; ct.f[2] = 2000 * fs;
            keys.push_back({ t, ct });
            cons = syl * 0.14f;
            break;
        default: break;
        }
        t += cons;
        // the vowel, gliding in pitch to its end
        keys.push_back({ t, vw });
        float vowelLen = std::max(0.05f, syl - cons - (s.coda.kind != C::None ? syl * 0.15f : 0.0f));
        Target end = vw; end.semis = s.semis + s.glide;
        keys.push_back({ t + vowelLen * 0.85f, end });
        t += vowelLen;
        // a dip between syllables, so they're heard as beats (the
        // rhythm is most of what makes mumble sound like talk)
        if (s.coda.kind == C::None && s.pauseAfter <= 0.0f) {
            Target dip = end; dip.voiced *= 0.3f; dip.open *= 0.4f;
            keys.push_back({ t - syl * 0.1f, dip });
        }
        // a closing consonant (the word's last)
        if (s.coda.kind != C::None) {
            Target cd = end;
            cd.open = 0.1f;
            if (s.coda.kind == C::Nasal) { cd.nasal = 1.0f; cd.voiced = 0.5f; cd.open = 0.0f; }
            else if (s.coda.kind == C::Fricative || s.coda.kind == C::VoicedFricative) { cd.voiced *= 0.2f; cd.noise = 0.2f; cd.noiseHz = s.coda.noiseHz; }
            else if (s.coda.kind == C::Plosive || s.coda.kind == C::VoicedPlosive) { cd.voiced = 0.0f; cd.open = 0.0f; }
            keys.push_back({ t, cd });
            t += syl * 0.15f;
        }
        if (s.pauseAfter > 0.0f) {
            Target quiet = end; quiet.voiced = 0.0f; quiet.noise = 0.0f; quiet.asp = 0.0f; quiet.nasal = 0.0f; quiet.open = 0.0f;
            keys.push_back({ t, quiet });
            t += s.pauseAfter;
        }
    }
    Target last = keys.back().tg; last.voiced = 0; last.noise = 0; last.asp = 0; last.nasal = 0; last.open = 0;
    keys.push_back({ t, last });
    t += 0.12f;

    // ── render ───────────────────────────────────────────
    const size_t n = (size_t)(t * rate);
    u.samples.assign(n, 0.0f);
    Reso cascade[4], fric, nasalR;
    float fricNorm = 1.0f;
    nasalR.set(260.0f * fs, 100.0f, (float)rate);
    Target cur = rest;
    const float smooth = 1.0f - std::exp(-1.0f / (rate * 0.012f));     // 12 ms glides between sounds
    const float f0Base = voice.baseHz();
    float phase = 0.0f, gPrev = 0.0f, shimmer = 1.0f, jitter = 1.0f, f0 = f0Base;
    float dcIn = 0.0f, dcOut = 0.0f;
    size_t k = 0;
    int retune = 0;
    float mouthSum = 0.0f;
    int mouthN = 0;
    const int perMouth = rate / Utterance::MOUTH_HZ;
    for (size_t i = 0; i < n; i++) {
        float time = (float)i / rate;
        while (k + 1 < keys.size() && keys[k + 1].t <= time) k++;
        const Target& tg = keys[k].tg;
        // ease every value toward the current key
        for (int j = 0; j < 4; j++) cur.f[j] += (tg.f[j] - cur.f[j]) * smooth;
        cur.voiced += (tg.voiced - cur.voiced) * smooth;
        cur.noise += (tg.noise - cur.noise) * smooth * 2.0f;
        cur.noiseHz += (tg.noiseHz - cur.noiseHz) * smooth;
        cur.asp += (tg.asp - cur.asp) * smooth;
        cur.nasal += (tg.nasal - cur.nasal) * smooth;
        cur.open += (tg.open * std::min(1.0f, tg.voiced + tg.asp + 0.2f) - cur.open) * smooth;
        cur.semis += (tg.semis - cur.semis) * smooth * 0.6f;
        // the filters follow (every 32 samples is plenty)
        if (retune-- <= 0) {
            retune = 32;
            const float bw[4] = { 60.0f + voice.nasal * 90.0f + voice.breath * 60.0f, 90.0f, 140.0f, 220.0f };
            for (int j = 0; j < 4; j++) cascade[j].set(std::min(cur.f[j], rate * 0.45f), bw[j] * fs, (float)rate);
            float fh = std::min(cur.noiseHz, rate * 0.45f);
            fric.set(fh, cur.noiseHz * 0.35f, (float)rate);
            fricNorm = 1.0f / std::max(1.0f, fric.gainAt(fh, (float)rate));   // the hiss as loud whatever its pitch
        }
        // the buzz: a glottal pulse each period, jittered by roughness
        f0 = f0Base * std::pow(2.0f, cur.semis / 12.0f);
        phase += f0 * jitter / rate;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            jitter = 1.0f + rng.signedUnit() * voice.rough * 0.06f;
            shimmer = 1.0f + rng.signedUnit() * voice.rough * 0.5f;
        }
        const float open = 0.45f, close = 0.18f;
        float g = phase < open ? 0.5f * (1.0f - std::cos(3.14159265f * phase / open))
                : phase < open + close ? std::cos(1.5707963f * (phase - open) / close) : 0.0f;
        float buzz = (g - gPrev) * 6.0f;                   // lips radiate the change
        gPrev = g;
        float noise = rng.signedUnit();
        float breathy = voice.breath * 0.5f + cur.asp;
        float src = buzz * cur.voiced * shimmer * (1.0f - voice.breath * 0.5f) + noise * breathy * (0.25f + g * 0.5f) * (cur.voiced + cur.asp);
        float y = src;
        for (int j = 0; j < 4; j++) y = cascade[j].run(y);
        y *= 0.35f;
        y += fric.run(noise) * fricNorm * cur.noise * 0.5f;
        y += nasalR.run(buzz * cur.voiced) * cur.nasal * 0.6f;
        // remove any DC
        float hp = y - dcIn + 0.995f * dcOut;
        dcIn = y; dcOut = hp;
        u.samples[i] = hp;
        // the mouth, 60 times a second
        mouthSum += cur.open;
        if (++mouthN >= perMouth) { u.mouth.push_back(std::clamp(mouthSum / mouthN, 0.0f, 1.0f)); mouthSum = 0.0f; mouthN = 0; }
    }
    // loud enough, never clipping
    float peak = 0.0f;
    for (float s : u.samples) peak = std::max(peak, std::fabs(s));
    if (peak > 1e-6f) {
        float gain = 0.85f * voice.volume / peak;
        for (float& s : u.samples) s *= gain;
    }
    return u;
}

} // namespace voicesynth
