#pragma once
#include "../npc/NpcManager.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Dialogue — what villagers and guards say
// ══════════════════════════════════════════════════════════
// Talking ({interact} on one) puts it in its 'unavailable' state;
// it greets you by the hour, then answers four questions: what it
// does (by its job), any news (roaming enemies near, the night, or
// nothing), who lives here (its neighbours, where to find them),
// goodbye. The lines are in data/registry/dialogue.toml; the news
// and the neighbours are worked out from the world now.
// main.cpp shows it (HudScreen::talk) and speaks each line in the
// NPC's voice.

namespace dialogue {

enum class Topic : uint8_t { Greet, About, News, People, Bye };

// The questions you can ask, in order (picked with the D-pad / arrow keys)
inline const std::vector<std::pair<Topic, std::string>>& questions() {
    static const std::vector<std::pair<Topic, std::string>> q = {
        { Topic::About, "What do you do here?" }, { Topic::News, "Any news?" },
        { Topic::People, "Who lives here?" }, { Topic::Bye, "Goodbye." } };
    return q;
}

class Library {
public:
    static constexpr const char* PATH = "data/registry/dialogue.toml";
    std::map<std::string, std::vector<std::string>> lists;   // "greet.morning", "about.smith", "news.danger" …
    std::map<std::string, std::string> people;               // "smith" → "{name} the Smith, at …"
    std::map<std::string, std::string> plural;               // npc type → "bandits"
    bool loaded = false;

    bool load(const char* path = PATH) {
        toml::table tbl;
        try { tbl = toml::parse_file(path); }
        catch (const toml::parse_error& e) { std::cerr << "dialogue.toml: " << e.description() << "\n"; return false; }
        lists.clear(); people.clear(); plural.clear();
        for (auto& [sect, node] : tbl) {
            const toml::table* t = node.as_table();
            if (!t) continue;
            std::string s(sect.str());
            for (auto& [k, v] : *t) {
                std::string key(k.str());
                if (const toml::array* a = v.as_array()) {
                    std::vector<std::string> out;
                    for (auto& x : *a) if (auto str = x.value<std::string>()) out.push_back(*str);
                    lists[s + "." + key] = out;
                } else if (auto str = v.value<std::string>()) {
                    if (s == "people") people[key] = *str;
                    else if (s == "plural") plural[key] = *str;
                }
            }
        }
        loaded = true;
        return true;
    }
    const std::vector<std::string>* list(const std::string& k) const {
        auto it = lists.find(k);
        return it == lists.end() || it->second.empty() ? nullptr : &it->second;
    }
};
inline Library gDialogue;

// "north", "south-east" … for a direction (world -z is north)
inline std::string compass(glm::vec2 d) {
    static const char* names[8] = { "east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east" };
    float a = std::atan2(d.y, d.x);                       // 0 east, +pi/2 south
    int k = (int)std::lround(a / 0.785398f);
    return names[((k % 8) + 8) % 8];
}

inline std::string fill(std::string s, const std::map<std::string, std::string>& v) {
    for (auto& [k, val] : v) {
        std::string tag = "{" + k + "}";
        for (size_t p; (p = s.find(tag)) != std::string::npos;) s.replace(p, tag.size(), val);
    }
    return s;
}

// What it says about a topic, now
inline std::string say(const Npc& n, Topic topic, float hour, NpcManager& npcs, uint32_t& rng) {
    if (!gDialogue.loaded) gDialogue.load();
    auto pick = [&](const std::vector<std::string>* l, const std::string& fallback) -> std::string {
        if (!l) return fallback;
        rng = rng * 1664525u + 1013904223u;
        return (*l)[(rng >> 8) % l->size()];
    };
    const bool guard = n.typeId == "guard";
    std::string first = n.name.substr(0, n.name.find(' '));
    std::map<std::string, std::string> vars = { { "name", first }, { "title", n.life.title } };
    switch (topic) {
    case Topic::Greet: {
        if (guard) return fill(pick(gDialogue.list("greet.guard"), "Halt."), vars);
        const char* when = (hour >= 22.0f || hour < 5.0f) ? "night" : hour < 11.0f ? "morning" : hour < 18.0f ? "day" : "evening";
        return fill(pick(gDialogue.list(std::string("greet.") + when), "Hello."), vars);
    }
    case Topic::About: {
        std::string job = guard ? "guard" : n.life.job.empty() ? "none" : n.life.job;
        const std::vector<std::string>* l = gDialogue.list("about." + job);
        if (!l) l = gDialogue.list("about.none");
        return fill(pick(l, "I live here."), vars);
    }
    case Topic::News: {
        // roaming enemies near: the most of one kind, where they are
        std::map<std::string, int> count;
        std::map<std::string, glm::vec3> sum;
        for (auto& o : npcs.all()) {
            if (!o->roamGroup || o->dead()) continue;
            if (glm::length(glm::vec2(o->position.x - n.position.x, o->position.z - n.position.z)) > 450.0f) continue;
            count[o->typeId]++;
            sum[o->typeId] += o->position;
        }
        std::string most; int best = 0;
        for (auto& [t, c] : count) if (c > best) { best = c; most = t; }
        if (best > 0) {
            glm::vec3 at = sum[most] / (float)best;
            glm::vec2 d(at.x - n.position.x, at.z - n.position.z);
            auto pl = gDialogue.plural.find(most);
            vars["what"] = pl != gDialogue.plural.end() ? pl->second : most + "s";
            vars["dir"] = compass(d);
            int ft = (int)std::lround(glm::length(d) / 3.0f / 50.0f) * 50;
            vars["dist"] = std::to_string(std::max(50, ft)) + " ft";
            return fill(pick(gDialogue.list("news.danger"), "Trouble about. Careful."), vars);
        }
        if (hour >= 18.0f || hour < 6.0f) return fill(pick(gDialogue.list("news.night"), "Mind the dark."), vars);
        return fill(pick(gDialogue.list("news.quiet"), "All quiet."), vars);
    }
    case Topic::People: {
        std::vector<std::string> parts;
        for (auto& o : npcs.all()) {
            if (o.get() == &n || o->dead()) continue;
            bool isGuard = o->typeId == "guard" && o->roamGroup == 0 && !o->name.empty() && o->name != (o->type ? o->type->name : "");
            if (!o->life.on && !isGuard) continue;
            if (glm::length(glm::vec2(o->position.x - n.position.x, o->position.z - n.position.z)) > 300.0f &&
                glm::length(glm::vec2(o->home.x - n.position.x, o->home.z - n.position.z)) > 300.0f) continue;
            std::string job = isGuard ? "guard" : o->life.job.empty() ? "none" : o->life.job;
            auto it = gDialogue.people.find(job);
            std::string pat = it != gDialogue.people.end() ? it->second : "{name} to the {dir}";
            glm::vec3 where = o->life.work.valid ? o->life.work.spot : o->life.bed.valid ? o->life.bed.spot : o->home;
            std::map<std::string, std::string> v = { { "name", o->name.substr(0, o->name.find(' ')) }, { "title", o->life.title },
                                                     { "dir", compass(glm::vec2(where.x - n.position.x, where.z - n.position.z)) } };
            parts.push_back(fill(pat, v));
            if (parts.size() >= 4) break;
        }
        if (parts.empty()) return fill(pick(gDialogue.list("people.alone"), "Just me."), vars);
        std::string s = pick(gDialogue.list("people.intro"), "There's");
        s += s.back() == '.' ? " " : " ";
        for (size_t i = 0; i < parts.size(); i++) {
            if (i) s += i + 1 == parts.size() ? ", and " : ", ";
            s += parts[i];
        }
        return s + ".";
    }
    case Topic::Bye:
        return fill(pick(gDialogue.list(guard ? "bye.guard" : "bye.lines"), "Farewell."), vars);
    }
    return "";
}

} // namespace dialogue
