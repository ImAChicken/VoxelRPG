#pragma once
#include "Acoustics.h"
#include "Sfx.h"
#include "VoiceSynth.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

// ══════════════════════════════════════════════════════════
// AudioEngine — every sound the game makes, placed in the world
// ══════════════════════════════════════════════════════════
// Everything but the menus (and music, later) plays FROM somewhere:
// a voice from a mouth, a footstep from a foot, a hit from where it
// landed, a block from its voxel. Each frame the game tells it where
// the ears are (the camera); each sound gets:
//
//   distance   quieter further off (none past ~50 m)
//   direction  louder in the ear it's on, a little duller behind
//   walls      the voxels between muffle it by material
//              (audio/Acoustics.h), checked ten times a second
//
// A sound can follow an entity (a speaking NPC walks and its voice
// goes with it), and a voice keeps its mouth track, so mouthOf(id)
// says how open that body's mouth is right now (the face shows it).
//
// miniaudio does the device (audio/AudioEngine.cpp is the only file
// that compiles it); the mixing is ours. No device (a server, a
// machine without sound): everything is a quiet no-op.
namespace sound {

enum class Bus : uint8_t { Effects, Voices, Ambience, Interface, COUNT };

struct PlayDesc {
    glm::vec3 pos{ 0.0f };
    bool      inWorld = true;      // false: the menus (no place, no walls)
    float     gain = 1.0f;
    float     pitch = 1.0f;        // playback speed
    uint64_t  follow = 0;          // an entity it moves with (update's 'where')
    Bus       bus = Bus::Effects;
};

using SampleData = std::shared_ptr<const std::vector<float>>;

class AudioEngine {
public:
    ~AudioEngine();
    bool init();
    void shutdown();
    bool ok() const { return running; }

    // ── Each frame ────────────────────────────────────────
    // where(entity, pos): where a followed entity's sound comes from
    // now (false: gone — the sound stops). blockAt: the world, for walls.
    void setListener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up);
    void update(float dt, const std::function<bool(uint64_t, glm::vec3&)>& where,
                const std::function<uint16_t(int, int, int)>& blockAt);

    // ── Playing ───────────────────────────────────────────
    uint32_t play(const SampleData& s, int rate, bool loop, const PlayDesc& d);
    uint32_t play(const sfx::Buffer& b, const PlayDesc& d);
    // A spoken line: its mouth track goes with it
    uint32_t speak(std::shared_ptr<const voicesynth::Utterance> u, const PlayDesc& d);
    void stop(uint32_t id);
    void stopFollowing(uint64_t entity, Bus bus);
    void setGain(uint32_t id, float gain);
    void setPos(uint32_t id, glm::vec3 pos);
    bool playing(uint32_t id) const;

    // How open the mouth of a speaking entity is now (0: shut / not speaking)
    float mouthOf(uint64_t entity) const;
    bool speaking(uint64_t entity) const;

    // ── Settings ──────────────────────────────────────────
    void setMaster(float v) { master = v; }
    void setBus(Bus b, float v) { busVol[(int)b] = v; }
    void setMuffling(bool on) { muffling = on; }   // off: straight distance and direction only

    // How many are playing (tests, the debug overlay)
    size_t active() const;

    // The mixer: miniaudio calls this from its own thread
    void mix(float* out, uint32_t frames);

private:
    struct Source {
        uint32_t id = 0;
        SampleData data;
        std::shared_ptr<const voicesynth::Utterance> utter;   // a voice: its mouth
        int rate = 22050;
        bool loop = false;
        PlayDesc desc;
        double cursor = 0.0;       // in the data's samples
        bool done = false;
        // set by update (main thread), eased toward by mix
        float tgtL = 0, tgtR = 0, tgtCut = 20000.0f;
        float curL = 0, curR = 0, curCut = 20000.0f;
        bool  fresh = true;        // first mix: start at the targets (no fade-in smear)
        float lp1 = 0.0f, lp2 = 0.0f;
        float occT = 0.0f;         // until its next wall check
        Through through;
    };

    void* device = nullptr;        // ma_device (only AudioEngine.cpp knows it)
    bool running = false;
    int  deviceRate = 48000;
    mutable std::mutex lock;
    std::vector<Source> sources;
    uint32_t nextId = 1;
    glm::vec3 ear{ 0.0f }, fwd{ 0, 0, -1 }, upv{ 0, 1, 0 };
    float master = 0.8f;
    std::array<float, (int)Bus::COUNT> busVol{ 1.0f, 1.0f, 0.7f, 0.8f };
    bool muffling = true;
    float clock = 0.0f;

    void place(Source& s, float dt, const std::function<uint16_t(int, int, int)>& blockAt);
};

// ── Ready-made effects ───────────────────────────────────
// Each (kind, material, variant) is made once, on first use.
// Recordings come first: data/audio/sounds.toml names .ogg files for
// a kind and material ([hit.dirt], [step.stone] …; files in
// data/audio/sfx). Anything it doesn't name is made by audio/Sfx.h as
// before. (Claude, 2026-11-22: the Kenney packs, CC0)
class SfxBank {
public:
    const sfx::Buffer& get(sfx::Kind k, const std::string& material, uint32_t variant, float size = 1.0f);
    static constexpr uint32_t VARIANTS = 4;
    // read sounds.toml and decode its files (get() does it the first time); how many sets
    int loadRecorded(const std::string& toml = "data/audio/sounds.toml", const std::string& dir = "data/audio/sfx");
    // the recordings for a kind and material, or null (tests)
    const std::vector<sfx::Buffer>* recorded(sfx::Kind k, const std::string& material) const;
    static const char* kindName(sfx::Kind k);
private:
    std::map<std::tuple<int, std::string, uint32_t, int>, sfx::Buffer> made;
    std::map<std::string, std::vector<sfx::Buffer>> takes;     // "hit.dirt" → its recordings
    bool recordedRead = false;
};

// ── Spoken lines ─────────────────────────────────────────
// The same line in the same voice is made once (a bark repeats).
class LineCache {
public:
    std::shared_ptr<const voicesynth::Utterance> get(const Voice& v, const std::string& text);
private:
    std::map<std::string, std::shared_ptr<const voicesynth::Utterance>> made;
};

inline AudioEngine gAudio;
inline SfxBank gSfx;
inline LineCache gLines;

} // namespace sound
