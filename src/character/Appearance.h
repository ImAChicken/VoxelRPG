#pragma once
#include <glm/glm.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Appearance — how a human looks
// ══════════════════════════════════════════════════════════
// Only looks: every human collides as the same 17-voxel
// capsule (Human.h) whatever its body, so they all fit the
// same doorways. Combat hitboxes may follow the body later.
//
// Everything about the body is generated from these settings
// (CharacterModel.h). Faces are put together from painted
// parts in the resource pack (FaceComposer.h).
//
// Colours are stored as the colour itself, not as a swatch
// number, so changing the palettes file never changes a
// character that already exists.

namespace look {
    inline constexpr const char* BUILDS[]     = { "Slim", "Average", "Sturdy" };
    inline constexpr const char* HEIGHTS[]    = { "5'4\"", "5'8\"", "6'0\"" };
    inline constexpr const char* FACES[]      = { "Soft", "Pointed", "Round", "Square", "Long" };
    inline constexpr const char* EYES[]       = { "Classic", "Sparkle", "Fantasy", "Sharp", "Anime",
                                                  "Gentle", "Fierce", "Sleepy", "Starry", "Tsurime",
                                                  "Tareme", "Dull", "Noble", "Wide",
                                                  "Story Round", "Story Almond", "Story Lidded",
                                                  "Story Bright", "Story Sly" };
    inline constexpr const char* EXPRESSIONS[] = { "Smile", "Grin", "Wink", "Surprised", "Smug" };
    inline constexpr const char* HAIRSTYLES[] = { "Bob", "Spiky", "Long", "Twin tails", "Ponytail", "Messy",
                                                  "Hime", "Side swept", "Bun", "Braids", "Shonen", "Pixie",
                                                  "Crew cut", "Undercut", "Slicked back", "Curtains", "Mohawk",
                                                  "Mullet", "Bald" };
    inline constexpr const char* EXTRAS[]     = { "None", "Cat ears", "Horns", "Witch hat", "Straw hat", "Headscarf" };
    inline constexpr const char* TOPS[]       = { "Tee", "Hoodie", "Crop jacket", "Tunic", "Vest", "Bodice" };
    inline constexpr const char* BOTTOMS[]    = { "Skirt", "Shorts", "Pants", "Overalls", "Long skirt" };
    inline constexpr const char* SHOES[]      = { "Sneakers", "Boots", "Clogs" };
    template <size_t N> constexpr int count(const char* const (&)[N]) { return (int)N; }

    inline glm::u8vec3 hex(const char* s) {
        unsigned v = 0;
        if (s && *s == '#') std::sscanf(s + 1, "%x", &v);
        return { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    }
}

// ── Palettes ──────────────────────────────────────────────
// The creator's colour swatches, from data/characters/
// palettes.toml (or the active resource pack's copy)
struct CharacterPalettes {
    std::vector<glm::u8vec3> skin, eyes, hair, cloth;

    CharacterPalettes() { setDefaults(); }

    void setDefaults() {
        auto fill = [](std::vector<glm::u8vec3>& v, std::initializer_list<const char*> l) {
            v.clear(); for (const char* s : l) v.push_back(look::hex(s));
        };
        fill(skin,  { "#f2dccb", "#e3c2a8", "#cfa585", "#b0866a", "#8c6852", "#654a3c", "#c8cfdc", "#c9d6c4" });
        fill(eyes,  { "#6f8fb0", "#7d6c9e", "#a86a78", "#6f9a80", "#b08d57", "#6e5443", "#9aa0a8" });
        fill(hair,  { "#d9a3b5", "#d8c08a", "#3a3140", "#9fb6c6", "#a7b88f", "#d9d4dc", "#b8735a", "#7f6fa6", "#5a4033" });
        fill(cloth, { "#b85c5c", "#e9e4da", "#3b3a4a", "#6f8db0", "#d1b06b", "#7f9a74", "#8f7aa8", "#c58a5a", "#9a9fa8",
                      "#a8607a", "#ddd2bb", "#7a5a44", "#6c7350", "#9c5a3c", "#4d5f78" });
    }

    bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto t = toml::parse_file(path.string());
            auto read = [&](const char* key, std::vector<glm::u8vec3>& out) {
                auto* arr = t[key].as_array();
                if (!arr || arr->empty()) return;
                out.clear();
                for (auto& e : *arr) if (auto s = e.value<std::string>()) out.push_back(look::hex(s->c_str()));
            };
            read("skin", skin); read("eyes", eyes); read("hair", hair); read("cloth", cloth);
            return true;
        } catch (...) { return false; }
    }
};
inline CharacterPalettes gPalettes;

struct Appearance {
    // ── Body ──────────────────────────────────────────────
    int   build        = 1;       // look::BUILDS
    int   heightVoxels = 17;      // 16, 17 or 18 (5'4" / 5'8" / 6'0")
    float shoulders = 0.0f;       // -1 .. 1
    float chest     = 0.0f;       //  0 .. 1
    float hips      = 0.0f;       // -1 .. 1
    float limbs     = 0.0f;       // -1 .. 1 (thickness)
    float head      = 0.0f;       // -1 .. 1 (size)
    int   face      = 0;          // look::FACES

    // ── Face ──────────────────────────────────────────────
    int eyeStyle   = 14;          // look::EYES
    int expression = 0;           // look::EXPRESSIONS (resting face)
    int eyeHeight  = 0;           // where the eyes (and brows) sit on the face, in face pixels: - up, + down
    static constexpr int EYE_SHIFT = 8;   // eyeHeight goes -EYE_SHIFT … +EYE_SHIFT

    // ── Hair and extras ───────────────────────────────────
    int hairStyle = 0;            // look::HAIRSTYLES
    int extra     = 0;            // look::EXTRAS

    // ── Outfit ────────────────────────────────────────────
    int top = 0, bottom = 2, shoes = 0;

    // ── Colours ───────────────────────────────────────────
    glm::u8vec3 skin       = look::hex("#e3c2a8");
    glm::u8vec3 eyes       = look::hex("#6f8fb0");
    glm::u8vec3 hair       = look::hex("#5a4033");
    glm::u8vec3 topColor   = look::hex("#6f8db0");
    glm::u8vec3 bottomColor= look::hex("#3b3a4a");

    static constexpr int MIN_HEIGHT = 16, MAX_HEIGHT = 18;

    void clamp() {
        build        = std::clamp(build, 0, look::count(look::BUILDS) - 1);
        heightVoxels = std::clamp(heightVoxels, MIN_HEIGHT, MAX_HEIGHT);
        // sliders keep three decimals, so a saved look reads
        // back exactly the same
        for (float* f : { &shoulders, &chest, &hips, &limbs, &head })
            *f = std::round(*f * 1000.0f) / 1000.0f;
        shoulders = std::clamp(shoulders, -1.0f, 1.0f);
        chest     = std::clamp(chest,      0.0f, 1.0f);
        hips      = std::clamp(hips,      -1.0f, 1.0f);
        limbs     = std::clamp(limbs,     -1.0f, 1.0f);
        head      = std::clamp(head,      -1.0f, 1.0f);
        face       = std::clamp(face,       0, look::count(look::FACES) - 1);
        eyeStyle   = std::clamp(eyeStyle,   0, look::count(look::EYES) - 1);
        expression = std::clamp(expression, 0, look::count(look::EXPRESSIONS) - 1);
        eyeHeight  = std::clamp(eyeHeight, -EYE_SHIFT, EYE_SHIFT);
        hairStyle  = std::clamp(hairStyle,  0, look::count(look::HAIRSTYLES) - 1);
        extra      = std::clamp(extra,      0, look::count(look::EXTRAS) - 1);
        top        = std::clamp(top,        0, look::count(look::TOPS) - 1);
        bottom     = std::clamp(bottom,     0, look::count(look::BOTTOMS) - 1);
        shoes      = std::clamp(shoes,      0, look::count(look::SHOES) - 1);
    }

    bool operator==(const Appearance& o) const {
        return build == o.build && heightVoxels == o.heightVoxels && shoulders == o.shoulders &&
               chest == o.chest && hips == o.hips && limbs == o.limbs && head == o.head && face == o.face &&
               eyeStyle == o.eyeStyle && expression == o.expression && eyeHeight == o.eyeHeight && hairStyle == o.hairStyle &&
               extra == o.extra && top == o.top && bottom == o.bottom && shoes == o.shoes &&
               skin == o.skin && eyes == o.eyes && hair == o.hair && topColor == o.topColor &&
               bottomColor == o.bottomColor;
    }
    bool operator!=(const Appearance& o) const { return !(*this == o); }

    // A random but sensible look — for NPCs and the creator's
    // Random button
    static Appearance random(uint32_t seed) {
        std::mt19937 rng(seed);
        auto pick = [&](int n) { return n > 0 ? (int)(rng() % (uint32_t)n) : 0; };
        auto unit = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
        auto from = [&](const std::vector<glm::u8vec3>& v) { return v.empty() ? glm::u8vec3(200) : v[(size_t)pick((int)v.size())]; };
        Appearance a;
        a.build = pick(3); a.heightVoxels = MIN_HEIGHT + pick(3);
        a.shoulders = unit(-0.8f, 0.8f); a.chest = unit(0.0f, 1.0f); a.hips = unit(-0.8f, 0.8f);
        a.limbs = unit(-0.6f, 0.6f); a.head = unit(-0.5f, 0.5f);
        a.face = pick(look::count(look::FACES));
        a.eyeStyle = pick(look::count(look::EYES));
        a.expression = pick(look::count(look::EXPRESSIONS));
        a.hairStyle = pick(look::count(look::HAIRSTYLES));
        a.extra = pick(4) == 0 ? 1 + pick(look::count(look::EXTRAS) - 1) : 0;
        a.top = pick(look::count(look::TOPS)); a.bottom = pick(look::count(look::BOTTOMS));
        a.shoes = pick(look::count(look::SHOES));
        a.clamp();
        a.skin = from(gPalettes.skin); a.eyes = from(gPalettes.eyes); a.hair = from(gPalettes.hair);
        a.topColor = from(gPalettes.cloth);
        do { a.bottomColor = from(gPalettes.cloth); } while (gPalettes.cloth.size() > 1 && a.bottomColor == a.topColor);
        return a;
    }

    // ── Save / load ───────────────────────────────────────
    // "look_*" lines in player.dat, the default character file
    // and the favorites file. Older saves (the voxel bodies)
    // are read too: whatever still means something carries over.
    void write(std::ostream& out) const {
        char b[220];
        std::snprintf(b, sizeof(b), "look_v=2\nlook_body=%d %d %.3f %.3f %.3f %.3f %.3f %d\n",
                      build, heightVoxels, shoulders, chest, hips, limbs, head, face);
        out << b;
        std::snprintf(b, sizeof(b), "look_face=%d %d %d\nlook_hair=%d %d\nlook_outfit=%d %d %d\n",
                      eyeStyle, expression, eyeHeight, hairStyle, extra, top, bottom, shoes);
        out << b;
        auto col = [&](const char* k, glm::u8vec3 c) {
            std::snprintf(b, sizeof(b), "look_%s=%d %d %d\n", k, c.r, c.g, c.b);
            out << b;
        };
        col("skin", skin); col("eyes", eyes); col("hairc", hair);
        col("topc", topColor); col("bottomc", bottomColor);
    }

    bool read(const std::string& key, const std::string& value) {
        if (key.rfind("look_", 0) != 0) return false;
        std::string k = key.substr(5);
        const char* v = value.c_str();
        if (k == "v") {
            // version marker; nothing to do
        } else if (k == "body") {
            float f[5] = {0, 0, 0, 0, 0};
            int n = std::sscanf(v, "%d %d %f %f %f %f %f %d", &build, &heightVoxels,
                                &f[0], &f[1], &f[2], &f[3], &f[4], &face);
            if (n >= 8) { shoulders = f[0]; chest = f[1]; hips = f[2]; limbs = f[3]; head = f[4]; }
            else if (n >= 6) { shoulders = f[0]; hips = f[1]; limbs = f[2]; head = f[3]; }   // old: no chest
        } else if (k == "face")   { eyeHeight = 0; std::sscanf(v, "%d %d %d", &eyeStyle, &expression, &eyeHeight);
        } else if (k == "hair") {
            // new: "style extra"; old saves: the hair colour "r g b"
            int a = 0, b = 0, c = 0;
            int n = std::sscanf(v, "%d %d %d", &a, &b, &c);
            if (n == 3) hair = glm::u8vec3((uint8_t)a, (uint8_t)b, (uint8_t)c);
            else if (n >= 1) { hairStyle = a; extra = n >= 2 ? b : 0; }
        } else if (k == "outfit") { std::sscanf(v, "%d %d %d", &top, &bottom, &shoes);
        } else if (k == "hairstyle") {                 // old voxel-body styles
            static const int MAP[5] = { 12, 2, 4, 12, 18 };  // short, long, ponytail, buzz, bald
            int o = std::atoi(v);
            hairStyle = MAP[std::clamp(o, 0, 4)];
        } else {
            int r = 0, g = 0, bl = 0;
            if (std::sscanf(v, "%d %d %d", &r, &g, &bl) != 3) return true;
            glm::u8vec3 c((uint8_t)r, (uint8_t)g, (uint8_t)bl);
            if      (k == "skin")                  skin = c;
            else if (k == "eyes")                  eyes = c;
            else if (k == "hairc")                 hair = c;
            else if (k == "topc"  || k == "shirt") topColor = c;
            else if (k == "bottomc" || k == "pants") bottomColor = c;
        }
        clamp();
        return true;
    }
};

// ── Favorite looks ────────────────────────────────────────
// Named looks saved from the character creator, shared by
// every world: saves/character_favorites.txt
//   [Name]
//   look_... lines
struct FavoriteLooks {
    std::vector<std::pair<std::string, Appearance>> list;
    std::filesystem::path path = "saves/character_favorites.txt";

    void load() {
        list.clear();
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                list.push_back({ line.substr(1, line.size() - 2), Appearance() });
                continue;
            }
            size_t eq = line.find('=');
            if (eq != std::string::npos && !list.empty())
                list.back().second.read(line.substr(0, eq), line.substr(eq + 1));
        }
    }

    bool save() const {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path);
        if (!f) return false;
        for (const auto& [name, a] : list) {
            f << "[" << name << "]\n";
            a.write(f);
            f << "\n";
        }
        return (bool)f;
    }

    // Save under a name (replaces a favorite with the same name)
    void put(const std::string& name, const Appearance& a) {
        for (auto& e : list) if (e.first == name) { e.second = a; save(); return; }
        list.push_back({ name, a });
        save();
    }

    void remove(size_t i) {
        if (i < list.size()) { list.erase(list.begin() + (long)i); save(); }
    }
};
