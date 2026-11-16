#pragma once
#include "AnimClips.h"
#include <toml++/toml.hpp>
#include <iostream>
#include <string>
#include <unordered_map>

// ══════════════════════════════════════════════════════════
// AnimMap — which clip plays for which state
// ══════════════════════════════════════════════════════════
// data/animations/anim_map.toml: locomotion states and moveset
// attacks → clips in gAnims. -1 = no clip (the procedural
// animation plays instead). Resolve again after gAnims reloads.
struct AnimMap {
    bool useClips = true;
    int idle = -1, walk = -1, run = -1, sprint = -1, crouchIdle = -1, crouchWalk = -1,
        combatIdle = -1, airUp = -1, airDown = -1, roll = -1, guard = -1;
    float walkSpeed = 9.6f, runSpeed = 52.7f, sprintSpeed = 81.2f, crouchSpeed = 7.4f;   // the clips' own speeds
    float walkToRun = 18.0f, runToSprint = 46.0f, blendWidth = 5.0f;                       // where they hand over
    std::unordered_map<std::string, int> attacks;      // moveset attack id → clip

    bool load(const char* path) {
        *this = AnimMap();
        try {
            auto t = toml::parse_file(path);
            useClips = t["use_clips"].value_or(true);
            auto clip = [&](const char* key) {
                std::string n = t["locomotion"][key].value_or(std::string());
                int c = n.empty() ? -1 : gAnims.find(n);
                if (!n.empty() && c < 0) std::cerr << path << ": no clip '" << n << "' (" << key << ")\n";
                return c;
            };
            idle = clip("idle"); walk = clip("walk"); run = clip("run"); sprint = clip("sprint");
            crouchIdle = clip("crouch_idle"); crouchWalk = clip("crouch_walk"); combatIdle = clip("combat_idle");
            airUp = clip("air_up"); airDown = clip("air_down"); roll = clip("roll"); guard = clip("guard");
            walkSpeed = (float)t["speeds"]["walk"].value_or(9.6);
            runSpeed = (float)t["speeds"]["run"].value_or(52.7);
            sprintSpeed = (float)t["speeds"]["sprint"].value_or(81.2);
            crouchSpeed = (float)t["speeds"]["crouch_walk"].value_or(7.4);
            walkToRun = (float)t["switch"]["walk_to_run"].value_or(18.0);
            runToSprint = (float)t["switch"]["run_to_sprint"].value_or(46.0);
            blendWidth = std::max(0.5f, (float)t["switch"]["blend"].value_or(5.0));
            if (auto* a = t["attacks"].as_table())
                for (auto& [k, v] : *a) {
                    std::string n = v.value_or(std::string());
                    int c = gAnims.find(n);
                    if (c < 0) std::cerr << path << ": no clip '" << n << "' (attack " << k.str() << ")\n";
                    else attacks[std::string(k.str())] = c;
                }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
        return true;
    }
    int attackClip(const std::string& id) const {
        if (!useClips) return -1;
        auto it = attacks.find(id);
        return it == attacks.end() ? -1 : it->second;
    }
};
inline AnimMap gAnimMap;
