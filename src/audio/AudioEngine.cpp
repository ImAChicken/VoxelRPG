// The only file that compiles miniaudio (audio/AudioEngine.h says why).
// Our headers first: on Windows miniaudio brings in windows.h, whose
// macros (near, far, FAR, small, min, max …) would break code after it
#include "AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <miniaudio.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace sound {

static void onAudio(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)in;
    static_cast<AudioEngine*>(dev->pUserData)->mix(static_cast<float*>(out), frames);
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    if (running) return true;
    auto* dev = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = onAudio;
    cfg.pUserData = this;
    cfg.periodSizeInMilliseconds = 15;
    // VOXELRPG_NULL_AUDIO=1: a pretend device (no sound card: tests,
    // a headless box). The mixer runs and mouths move; nothing is heard
    static ma_context nullCtx;
    ma_context* ctx = nullptr;
    if (std::getenv("VOXELRPG_NULL_AUDIO")) {
        ma_backend b[] = { ma_backend_null };
        if (ma_context_init(b, 1, nullptr, &nullCtx) == MA_SUCCESS) ctx = &nullCtx;
    }
    if (ma_device_init(ctx, &cfg, dev) != MA_SUCCESS) {
        std::cerr << "No sound device: playing without sound\n";
        delete dev;
        return false;
    }
    deviceRate = (int)dev->sampleRate;
    if (ma_device_start(dev) != MA_SUCCESS) {
        std::cerr << "The sound device wouldn't start: playing without sound\n";
        ma_device_uninit(dev);
        delete dev;
        return false;
    }
    device = dev;
    running = true;
    std::cout << "Sound: " << dev->playback.name << " at " << deviceRate << " Hz\n";
    return true;
}

void AudioEngine::shutdown() {
    if (!device) return;
    auto* dev = static_cast<ma_device*>(device);
    ma_device_uninit(dev);
    delete dev;
    device = nullptr;
    running = false;
    std::lock_guard<std::mutex> g(lock);
    sources.clear();
}

void AudioEngine::setListener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up) {
    ear = pos;
    if (glm::length(forward) > 1e-4f) fwd = glm::normalize(forward);
    if (glm::length(up) > 1e-4f) upv = glm::normalize(up);
}

uint32_t AudioEngine::play(const SampleData& s, int rate, bool loop, const PlayDesc& d) {
    if (!running || !s || s->empty()) return 0;
    Source src;
    src.data = s;
    src.rate = rate;
    src.loop = loop;
    src.desc = d;
    std::lock_guard<std::mutex> g(lock);
    src.id = nextId++;
    if (nextId == 0) nextId = 1;
    sources.push_back(std::move(src));
    place(sources.back(), 0.0f, nullptr);      // its first gains, before it's heard
    return sources.back().id;
}

uint32_t AudioEngine::play(const sfx::Buffer& b, const PlayDesc& d) {
    // the bank keeps the buffer; share its samples without a copy
    SampleData s(std::shared_ptr<const std::vector<float>>(), &b.samples);
    return play(s, b.rate, b.loop, d);
}

uint32_t AudioEngine::speak(std::shared_ptr<const voicesynth::Utterance> u, const PlayDesc& d) {
    if (!running || !u || u->samples.empty()) return 0;
    SampleData s(u, &u->samples);              // the samples live as long as the utterance
    PlayDesc v = d;
    v.bus = Bus::Voices;
    if (v.follow) stopFollowing(v.follow, Bus::Voices);   // one line at a time per mouth
    uint32_t id = play(s, u->rate, false, v);
    std::lock_guard<std::mutex> g(lock);
    for (Source& src : sources) if (src.id == id) src.utter = u;
    return id;
}

void AudioEngine::stop(uint32_t id) {
    std::lock_guard<std::mutex> g(lock);
    for (Source& s : sources) if (s.id == id) s.done = true;
}
void AudioEngine::stopFollowing(uint64_t entity, Bus bus) {
    std::lock_guard<std::mutex> g(lock);
    for (Source& s : sources) if (s.desc.follow == entity && s.desc.bus == bus) s.done = true;
}
void AudioEngine::setGain(uint32_t id, float gain) {
    std::lock_guard<std::mutex> g(lock);
    for (Source& s : sources) if (s.id == id) s.desc.gain = gain;
}
void AudioEngine::setPos(uint32_t id, glm::vec3 pos) {
    std::lock_guard<std::mutex> g(lock);
    for (Source& s : sources) if (s.id == id) s.desc.pos = pos;
}
bool AudioEngine::playing(uint32_t id) const {
    std::lock_guard<std::mutex> g(lock);
    for (const Source& s : sources) if (s.id == id) return !s.done;
    return false;
}
size_t AudioEngine::active() const {
    std::lock_guard<std::mutex> g(lock);
    return sources.size();
}
float AudioEngine::mouthOf(uint64_t entity) const {
    std::lock_guard<std::mutex> g(lock);
    for (const Source& s : sources)
        if (s.utter && s.desc.follow == entity && !s.done) return s.utter->mouthAt((float)(s.cursor / s.rate));
    return 0.0f;
}
bool AudioEngine::speaking(uint64_t entity) const {
    std::lock_guard<std::mutex> g(lock);
    for (const Source& s : sources) if (s.utter && s.desc.follow == entity && !s.done) return true;
    return false;
}

// Where it is to the ears: loudness in each, and how dull
void AudioEngine::place(Source& s, float dt, const std::function<uint16_t(int, int, int)>& blockAt) {
    const float busGain = master * busVol[(int)s.desc.bus] * s.desc.gain;
    if (!s.desc.inWorld) {
        s.tgtL = s.tgtR = busGain * 0.75f;
        s.tgtCut = 20000.0f;
        return;
    }
    glm::vec3 to = s.desc.pos - ear;
    float dist = glm::length(to);
    // distance: full within REF, then falling (voxels: 10 to a metre)
    const float REF = 15.0f, MAX_DIST = 520.0f;
    float att = dist <= REF ? 1.0f : REF / (REF + (dist - REF) * 0.9f);
    if (dist > MAX_DIST * 0.8f) att *= std::max(0.0f, (MAX_DIST - dist) / (MAX_DIST * 0.2f));   // fades to nothing at MAX_DIST
    // walls, ten times a second (spread over frames)
    s.occT -= dt;
    if (blockAt && muffling && s.occT <= 0.0f && dist < MAX_DIST) {
        s.through = throughWalls(ear, s.desc.pos, blockAt);
        s.occT = 0.1f;
    }
    if (!muffling) s.through = Through();
    // direction: which ear, and behind is a little duller
    float pan = 0.0f, front = 1.0f;
    if (dist > 0.5f) {
        glm::vec3 dir = to / dist;
        glm::vec3 right = glm::normalize(glm::cross(fwd, upv));
        pan = std::clamp(glm::dot(dir, right), -1.0f, 1.0f);
        front = glm::dot(dir, fwd);
    }
    float a = (pan + 1.0f) * 0.25f * 3.14159265f;              // equal power…
    float l = std::cos(a), r = std::sin(a);
    l = 0.35f * 0.7071f + 0.65f * l;                            // …never fully in one ear
    r = 0.35f * 0.7071f + 0.65f * r;
    float g = busGain * att * s.through.gain;
    s.tgtL = g * l;
    s.tgtR = g * r;
    float behind = front < 0.0f ? 1.0f + front * 0.45f : 1.0f;  // straight behind: 55% of the highs
    s.tgtCut = std::min(20000.0f, s.through.cutoffHz * behind);
}

void AudioEngine::update(float dt, const std::function<bool(uint64_t, glm::vec3&)>& where,
                         const std::function<uint16_t(int, int, int)>& blockAt) {
    if (!running) return;
    clock += dt;
    std::lock_guard<std::mutex> g(lock);
    // finished ones go (the mixer only marks them)
    sources.erase(std::remove_if(sources.begin(), sources.end(), [](const Source& s) { return s.done; }), sources.end());
    for (Source& s : sources) {
        if (s.desc.follow && where) {
            glm::vec3 p;
            if (where(s.desc.follow, p)) s.desc.pos = p;
            else if (s.desc.bus == Bus::Voices) { s.done = true; continue; }   // the speaker is gone
        }
        place(s, dt, blockAt);
    }
}

void AudioEngine::mix(float* out, uint32_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    std::lock_guard<std::mutex> g(lock);
    const float ease = 1.0f - std::exp(-1.0f / (deviceRate * 0.02f));   // 20 ms gain glides
    for (Source& s : sources) {
        if (s.done || !s.data) continue;
        const std::vector<float>& d = *s.data;
        const double step = (double)s.rate / deviceRate * s.desc.pitch;
        if (s.fresh) { s.curL = s.tgtL; s.curR = s.tgtR; s.curCut = s.tgtCut; s.fresh = false; }
        for (uint32_t f = 0; f < frames; f++) {
            size_t i = (size_t)s.cursor;
            if (i + 1 >= d.size()) {
                if (s.loop) { s.cursor = std::fmod(s.cursor, (double)d.size() - 1.0); i = (size_t)s.cursor; }
                else { s.done = true; break; }
            }
            float k = (float)(s.cursor - (double)i);
            float x = d[i] * (1.0f - k) + d[i + 1] * k;
            // the walls / behind: two gentle low-pass poles
            s.curCut += (s.tgtCut - s.curCut) * ease;
            float c = 1.0f - std::exp(-6.2831853f * s.curCut / deviceRate);
            s.lp1 += (x - s.lp1) * c;
            s.lp2 += (s.lp1 - s.lp2) * c;
            float y = s.curCut >= 19000.0f ? x : s.lp2;
            s.curL += (s.tgtL - s.curL) * ease;
            s.curR += (s.tgtR - s.curR) * ease;
            out[f * 2] += y * s.curL;
            out[f * 2 + 1] += y * s.curR;
            s.cursor += step;
        }
    }
    // never clip: a soft knee over the loud part
    for (uint32_t i = 0; i < frames * 2; i++) {
        float v = out[i];
        if (std::fabs(v) > 0.8f) v = std::copysign(0.8f + 0.2f * std::tanh((std::fabs(v) - 0.8f) / 0.2f), v);
        out[i] = v;
    }
}

// ── The banks ────────────────────────────────────────────
const sfx::Buffer& SfxBank::get(sfx::Kind k, const std::string& material, uint32_t variant, float size) {
    variant %= VARIANTS;
    int sizeKey = (int)std::lround(size * 4.0f);
    auto key = std::make_tuple((int)k, material, variant, sizeKey);
    auto it = made.find(key);
    if (it != made.end()) return it->second;
    return made[key] = sfx::make(k, gMaterials.get(material).tone, variant, size);
}

std::shared_ptr<const voicesynth::Utterance> LineCache::get(const Voice& v, const std::string& text) {
    std::string key = v.text() + "|" + text;
    auto it = made.find(key);
    if (it != made.end()) return it->second;
    if (made.size() > 200) made.clear();       // plenty for barks; typed lines come and go
    auto u = std::make_shared<voicesynth::Utterance>(voicesynth::speak(v, text));
    made[key] = u;
    return u;
}

} // namespace sound
