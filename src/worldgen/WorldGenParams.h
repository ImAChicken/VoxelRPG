#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>

// ══════════════════════════════════════════════════════════
// WorldGenParams — the knobs on the New World screen
// ══════════════════════════════════════════════════════════
// Every value tuned by hand while building the generator is a
// slider here, and its default IS that tuned value, so leaving
// everything alone gives the standard world. Saved in the
// world's world.vxrp ("gen.<key>=<value>"), so a world always
// regenerates the same way.
//
// Adding a knob: one line in the table in params(), then read
// it where it's used with get("key").
struct WorldGenParam {
    const char* key;
    const char* name;      // shown on the slider
    float lo, hi, def;
    bool  whole;           // whole numbers only
    const char* page;      // "Terrain" or "Features"
};

struct WorldGenParams {
    static const std::array<WorldGenParam, 14>& params() {
        static const std::array<WorldGenParam, 14> P = {{
            { "hills",       "Hilliness",          0.0f,  2.5f, 1.0f,  false, "Terrain"  },
            { "erosion",     "Erosion strength",   0.0f,  3.0f, 1.0f,  false, "Terrain"  },
            { "passes",      "Erosion passes",     1.0f, 10.0f, 5.0f,  true,  "Terrain"  },
            { "scree",       "Scree angle"      , 35.0f, 75.0f, 58.0f, true,  "Terrain"  },
            { "edges",       "Biome edge width",   0.2f,  2.5f, 1.0f,  false, "Terrain"  },
            { "banks",       "Flat banks",         0.0f,  2.5f, 1.0f,  false, "Terrain"  },
            { "streams",     "Streams",            0.2f,  3.0f, 1.0f,  false, "Features" },
            { "lakes",       "Natural lakes",      0.0f,  3.0f, 1.0f,  false, "Features" },
            { "falls",       "Waterfall height",   6.0f, 30.0f, 10.0f, true,  "Features" },
            { "trees",       "Trees",              0.0f,  3.0f, 1.0f,  false, "Features" },
            { "rocks",       "Rocks",              0.0f,  3.0f, 1.0f,  false, "Features" },
            { "decor",       "Bushes and logs",    0.0f,  3.0f, 1.0f,  false, "Features" },
            { "alcoves",     "Cliff alcoves",      0.0f,  2.0f, 1.0f,  false, "Features" },
            { "relief_mtn",  "Mountain ruggedness",0.0f,  2.0f, 1.0f,  false, "Terrain"  },
        }};
        return P;
    }
    static constexpr int BIOMES = 64;          // ≥ island::BIOME_COUNT

    std::array<float, 14> v{};
    std::array<float, BIOMES> biomeWeight{};   // 1 = as tuned; 0 = as rare as the rules allow

    WorldGenParams() { reset(); }
    void reset() {
        for (size_t i = 0; i < v.size(); i++) v[i] = params()[i].def;
        biomeWeight.fill(1.0f);
    }
    bool isDefault() const {
        for (size_t i = 0; i < v.size(); i++) if (v[i] != params()[i].def) return false;
        for (float w : biomeWeight) if (w != 1.0f) return false;
        return true;
    }
    int indexOf(const char* key) const {
        for (size_t i = 0; i < v.size(); i++) if (!std::strcmp(params()[i].key, key)) return (int)i;
        return -1;
    }
    float get(const char* key) const {
        int i = indexOf(key);
        return i >= 0 ? v[(size_t)i] : 1.0f;
    }
    float weight(int biome) const {
        return (biome >= 0 && biome < BIOMES) ? biomeWeight[(size_t)biome] : 1.0f;
    }
    void clamp() {
        for (size_t i = 0; i < v.size(); i++) {
            const auto& p = params()[i];
            v[i] = std::clamp(v[i], p.lo, p.hi);
            if (p.whole) v[i] = (float)(int)(v[i] + 0.5f);
        }
        for (float& w : biomeWeight) w = std::clamp(w, 0.0f, 3.0f);
    }

    // For the bake cache key: changes when any value changes
    uint64_t hash() const {
        uint64_t h = 1469598103934665603ull;
        auto mixIn = [&](float f) {
            uint32_t u; std::memcpy(&u, &f, 4);
            h = (h ^ u) * 1099511628211ull;
        };
        for (float f : v) mixIn(f);
        for (float f : biomeWeight) mixIn(f);
        return h;
    }

    // ── Save / load ───────────────────────────────────────
    //   biomeName(i) gives the biome's name for the biome lines
    template <class NameFn>
    void write(std::ostream& out, NameFn biomeName, int biomeCount) const {
        char b[96];
        for (size_t i = 0; i < v.size(); i++) {
            std::snprintf(b, sizeof(b), "gen.%s=%g\n", params()[i].key, v[i]);
            out << b;
        }
        for (int i = 0; i < biomeCount && i < BIOMES; i++)
            if (biomeWeight[(size_t)i] != 1.0f) {
                std::snprintf(b, sizeof(b), "genbiome.%s=%g\n", biomeName(i), biomeWeight[(size_t)i]);
                out << b;
            }
    }
    template <class NameFn>
    bool read(const std::string& key, const std::string& value, NameFn biomeName, int biomeCount) {
        if (key.rfind("gen.", 0) == 0) {
            int i = indexOf(key.substr(4).c_str());
            if (i >= 0) { v[(size_t)i] = (float)std::atof(value.c_str()); clamp(); }
            return true;
        }
        if (key.rfind("genbiome.", 0) == 0) {
            std::string n = key.substr(9);
            for (int b = 0; b < biomeCount && b < BIOMES; b++)
                if (n == biomeName(b)) biomeWeight[(size_t)b] = (float)std::atof(value.c_str());
            clamp();
            return true;
        }
        return false;
    }
};
