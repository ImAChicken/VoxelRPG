// Headless checks for sound (audio/): the voice synth (audio/VoiceSynth.h),
// the material effects (audio/Sfx.h, audio/Materials.h), sound through
// walls (audio/Acoustics.h), voices saved with a look, NPC voices and
// barks (npcs.toml), and the engine on a pretend device (no sound card
// needed: VOXELRPG_NULL_AUDIO). Run from the repo root.
#include "TestWorld.h"
#include "audio/AudioEngine.h"
#include "audio/VoiceSynth.h"
#include "audio/Sfx.h"
#include "audio/Acoustics.h"
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

using namespace testworld;

static float peak(const std::vector<float>& s) { float p = 0; for (float v : s) p = std::max(p, std::fabs(v)); return p; }
static bool finite(const std::vector<float>& s) { for (float v : s) if (!std::isfinite(v)) return false; return true; }

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    sound::gMaterials.bind();

    std::printf("the voice\n");
    {
        Voice v;
        auto u = voicesynth::speak(v, "Hello there, traveller. Are you lost?");
        check(u.syllables == 9, "a line becomes its syllables (" + std::to_string(u.syllables) + ")");
        check(u.seconds() > 1.0f && u.seconds() < 4.0f && finite(u.samples) && peak(u.samples) > 0.3f && peak(u.samples) <= 0.86f,
              "  about the length of saying it (" + std::to_string(u.seconds()).substr(0, 4) + " s), loud, never clipping");
        float open = 0, shut = 1;
        for (float m : u.mouth) { open = std::max(open, m); shut = std::min(shut, m); }
        check(u.mouth.size() > 60 && open > 0.6f && shut < 0.05f, "  the mouth opens and shuts with it");
        auto again = voicesynth::speak(v, "Hello there, traveller. Are you lost?");
        check(again.samples == u.samples, "  the same line in the same voice sounds the same");
        Voice fast = v; fast.speed = 1.0f;
        Voice slow = v; slow.speed = 0.0f;
        float tf = voicesynth::speak(fast, "one two three four five").seconds(), ts = voicesynth::speak(slow, "one two three four five").seconds();
        check(ts > tf * 1.6f, "speed: slow takes longer (" + std::to_string(ts).substr(0, 4) + " s vs " + std::to_string(tf).substr(0, 4) + ")");
        // pitch: the buzz's period, found by autocorrelation in the middle of a long vowel
        auto f0 = [](const voicesynth::Utterance& w) {
            size_t mid = w.samples.size() / 3, N = (size_t)w.rate / 10;
            float best = 0; int lag = 0;
            for (int L = w.rate / 450; L < w.rate / 55; L++) {
                float c = 0, e1 = 0, e2 = 0;
                for (size_t i = mid; i < mid + N; i++) { c += w.samples[i] * w.samples[i + L]; e1 += w.samples[i] * w.samples[i]; e2 += w.samples[i + L] * w.samples[i + L]; }
                c /= std::sqrt(e1 * e2) + 1e-9f;
                if (c > best) { best = c; lag = L; }
            }
            return lag ? (float)w.rate / lag : 0.0f;
        };
        Voice hi = v; hi.pitch = 0.95f; Voice lo = v; lo.pitch = 0.05f;
        float fh = f0(voicesynth::speak(hi, "aaaa aaaa")), fl = f0(voicesynth::speak(lo, "aaaa aaaa"));
        check(std::fabs(fh - hi.baseHz()) < hi.baseHz() * 0.15f && std::fabs(fl - lo.baseHz()) < lo.baseHz() * 0.15f,
              "pitch: the buzz is where the slider says (" + std::to_string((int)fh) + " Hz high, " + std::to_string((int)fl) + " Hz low)");
        check(voicesynth::speak(v, "").samples.empty() && voicesynth::speak(v, "...").samples.empty(), "  nothing to say: silence");
        for (int st = 0; st < Voice::STYLE_COUNT; st++) {
            Voice s2 = v; s2.style = st;
            auto w = voicesynth::speak(s2, "Bloop! What was that?");
            check(finite(w.samples) && peak(w.samples) > 0.3f, std::string("  style ") + Voice::styleName(st) + " speaks");
        }
    }

    std::printf("voices are saved with a look\n");
    {
        Appearance a;
        a.voice.pitch = 0.8f; a.voice.style = 2; a.voice.rough = 0.66f;
        std::stringstream ss;
        a.write(ss);
        Appearance b;
        std::string line;
        while (std::getline(ss, line)) { size_t eq = line.find('='); b.read(line.substr(0, eq), line.substr(eq + 1)); }
        check(b.voice == a.voice && b == a, "look_voice: written and read back");
        Appearance old;
        old.read("look_body", "1 17 0 0 0 0 0 0");
        check(old.voice == Voice(), "  an old save (no voice line) gets the default voice");
    }

    std::printf("NPC voices\n");
    {
        const NpcType* bandit = gNpcTypes.find("bandit");
        const NpcType* slime = gNpcTypes.find("slime");
        check(bandit && bandit->hasVoice && bandit->barksFor("spot") && bandit->barksFor("hurt") && bandit->barksFor("die"),
              "npcs.toml: the bandit has a voice and barks");
        check(slime && slime->hasVoice && slime->voice.style == 1 && slime->voice.pitch > 0.6f, "  the slime: high and chirpy");
        if (bandit) {
            Voice a = bandit->voice.varied(1, bandit->voiceVary), b2 = bandit->voice.varied(2, bandit->voiceVary);
            check(a != b2 && std::fabs(a.pitch - bandit->voice.pitch) <= bandit->voiceVary + 1e-3f, "  each bandit a little different");
        }
    }

    std::printf("effects by material\n");
    {
        const char* mats[] = { "stone", "wood", "dirt", "leaves", "fluid", "metal", "snow", "slime" };
        bool all = true;
        for (int k = 0; k < (int)sfx::Kind::COUNT; k++)
            for (const char* m : mats) {
                sfx::Buffer b = sfx::make((sfx::Kind)k, sound::gMaterials.get(m).tone, 1);
                all = all && !b.samples.empty() && finite(b.samples) && peak(b.samples) > 0.05f && peak(b.samples) <= 1.0f;
            }
        check(all, "every effect, every material: made, audible, not clipping");
        auto bright = [](const std::vector<float>& s) {      // zero crossings a sample: higher is brighter
            int zc = 0; float p = 0;
            for (float x : s) { if ((x < 0) != (p < 0)) zc++; p = x; }
            return s.empty() ? 0.0f : (float)zc / s.size();
        };
        float stone = bright(sfx::make(sfx::Kind::Step, sound::gMaterials.get("stone").tone, 0).samples);
        float dirt = bright(sfx::make(sfx::Kind::Step, sound::gMaterials.get("dirt").tone, 0).samples);
        check(stone > dirt * 2.0f, "  a step on stone is brighter than on dirt");
        auto a = sfx::make(sfx::Kind::Step, sound::gMaterials.get("stone").tone, 0).samples;
        auto b = sfx::make(sfx::Kind::Step, sound::gMaterials.get("stone").tone, 1).samples;
        check(a != b, "  takes differ (steps don't repeat exactly)");
        auto wind = sfx::make(sfx::Kind::Wind, sound::gMaterials.get("air").tone, 0);
        check(wind.loop && wind.samples.size() > 22050 * 3, "the wind: a loop several seconds long");
        uint16_t stoneId = gRegistry.getIDByName("stone"), leaves = gRegistry.getIDByName("leaves"), grass = gRegistry.getIDByName("grass_blades");
        check(sound::MaterialTable::nameFor(stoneId) == "stone" && sound::MaterialTable::nameFor(leaves) == "leaves" &&
              sound::MaterialTable::nameFor(grass) == "plant", "blocks find their sound by material (and leaves, plants)");
    }

    std::printf("through walls\n");
    {
        uint16_t STONE = gRegistry.getIDByName("stone"), PLANKS = gRegistry.getIDByName("planks"), LEAVES = gRegistry.getIDByName("leaves");
        auto wallOf = [&](uint16_t id, int thick) {
            return [=](int x, int y, int z) -> uint16_t { (void)y; (void)z; return x >= 10 && x < 10 + thick ? id : 0; };
        };
        glm::vec3 ear(0.5f, 5.5f, 0.5f), src(30.5f, 5.5f, 0.5f);
        auto open = sound::throughWalls(ear, src, [](int, int, int) -> uint16_t { return 0; });
        auto stone3 = sound::throughWalls(ear, src, wallOf(STONE, 3));
        auto wood3 = sound::throughWalls(ear, src, wallOf(PLANKS, 3));
        auto leaf3 = sound::throughWalls(ear, src, wallOf(LEAVES, 3));
        auto hill = sound::throughWalls(ear, src, wallOf(STONE, 15));
        check(open.gain == 1.0f && open.solid == 0, "open air: nothing lost");
        check(stone3.solid == 3 && stone3.gain < wood3.gain && wood3.gain < leaf3.gain && leaf3.gain > 0.85f,
              "  stone muffles more than wood, leaves barely (" + std::to_string(stone3.gain).substr(0, 4) + " / " +
              std::to_string(wood3.gain).substr(0, 4) + " / " + std::to_string(leaf3.gain).substr(0, 4) + ")");
        check(stone3.cutoffHz < 3000.0f && open.cutoffHz > 15000.0f, "  and takes the highs first (" + std::to_string((int)stone3.cutoffHz) + " Hz left)");
        check(hill.gain < 0.05f, "  a hillside of stone: silence");
    }

    std::printf("the engine (a pretend device)\n");
    {
#ifdef _WIN32
        _putenv_s("VOXELRPG_NULL_AUDIO", "1");
#else
        setenv("VOXELRPG_NULL_AUDIO", "1", 1);
#endif
        sound::AudioEngine eng;
        bool ok = eng.init();
        check(ok, "it starts (no sound card needed)");
        if (ok) {
            eng.setListener(glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
            auto noWorld = [](int, int, int) -> uint16_t { return 0; };
            glm::vec3 mouth(0, 0, -20);
            auto where = [&](uint64_t who, glm::vec3& at) { if (who != 7) return false; at = mouth; return true; };
            sound::PlayDesc d; d.follow = 7; d.pos = mouth;
            auto line = std::make_shared<voicesynth::Utterance>(voicesynth::speak(Voice(), "Hello there, how are you today?"));
            eng.speak(line, d);
            sound::gSfx.get(sfx::Kind::Step, "stone", 0);
            sound::PlayDesc s; s.pos = glm::vec3(5, 0, 0);
            eng.play(sound::gSfx.get(sfx::Kind::Step, "stone", 0), s);
            check(eng.active() == 2 && eng.speaking(7), "  a line and a step play");
            float most = 0.0f;
            for (int i = 0; i < 60; i++) {
                eng.update(1.0f / 60, where, noWorld);
                most = std::max(most, eng.mouthOf(7));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            check(most > 0.4f, "  the speaker's mouth opens as it plays (" + std::to_string(most).substr(0, 4) + ")");
            check(eng.active() == 1, "  the step has finished; the line goes on");
            mouth = glm::vec3(0, 0, -25);       // gone: the speaker's sound stops
            auto gone = [](uint64_t, glm::vec3&) { return false; };
            eng.update(0.016f, gone, noWorld);
            eng.update(0.016f, gone, noWorld);
            check(eng.active() == 0 && !eng.speaking(7), "  the speaker gone: its line stops");
            eng.shutdown();
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
