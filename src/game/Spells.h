#pragma once
// ══════════════════════════════════════════════════════════
// Spells as data (data/registry/spells.toml)
// ══════════════════════════════════════════════════════════
// A spell is put together from parts (ROADMAP "Spells"):
//   delivery  how it reaches what it touches: self, touch, cone,
//             projectile, beam, area, wall, rune, summon
//   use       how the button works: tap, cast, charge, channel, toggle
//   when      where it can be cast: ground, air, sprinting, grappling,
//             rolling
//   shape     range, radius, width, arc, duration, count, speed …
//   effects   what it does to each body it reaches: damage (a type
//             against resistances), push / pull, poise, a status
//             (burn, slow, freeze, poison, silence, haste), heal, ward,
//             place (a structure, like the ice), blink, light
//   triggers  on_hit / on_expire start another spell where it happened
//             (spells chain); 'every' repeats an area, a beam, an aura
//   look      colour and particles (particles.toml), the cast pose
//             (anim_map.toml [casts]), one or two hands, tags
//   cost      stamina (mana later), cooldown
// The effects are C++ written once (sim/SpellSystem.h); a new spell
// is a new entry here, no code. Each spell gets an attack of its own
// (AttackDef, id "spell:<id>") for the cast's timing, stamina,
// charge and animation, so it plays like any other attack.
// Spellbooks (items.toml spell = "<id>") cast it; a combat slot holds
// the book for now (a spell id later, from a skill tree).
// (Claude, 2026-11-25, ROADMAP "Then: spell groundwork")
#include "Combat.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spell {

enum class Delivery : uint8_t { Self, Touch, Cone, Projectile, Beam, Area, Wall, Rune, Summon };
enum class Use : uint8_t { Tap, Cast, Charge, Channel, Toggle };
enum class EffectKind : uint8_t { Damage, Push, Poise, Status, Heal, Ward, Place, Blink, Light };
enum class Status : uint8_t { None, Burn, Slow, Freeze, Poison, Silence, Haste, COUNT };
// who its effects reach
enum class Targets : uint8_t { Enemies, Allies, All, Self };

// where it can be cast (when = [...])
constexpr uint8_t WHEN_GROUND = 1, WHEN_AIR = 2, WHEN_SPRINT = 4, WHEN_GRAPPLE = 8, WHEN_ROLL = 16;

// damage types (schools, and plain): resistances are per type
enum class Element : uint8_t { Physical, Fire, Frost, Shock, Arcane, Wind, Nature, COUNT };
inline Element elementFrom(const std::string& s) {
    if (s == "fire") return Element::Fire;
    if (s == "frost") return Element::Frost;
    if (s == "shock") return Element::Shock;
    if (s == "arcane") return Element::Arcane;
    if (s == "wind") return Element::Wind;
    if (s == "nature") return Element::Nature;
    return Element::Physical;
}
inline const char* elementName(Element e) {
    static const char* N[] = { "physical", "fire", "frost", "shock", "arcane", "wind", "nature" };
    return N[(int)e < (int)Element::COUNT ? (int)e : 0];
}
inline Status statusFrom(const std::string& s) {
    if (s == "burn") return Status::Burn;
    if (s == "slow") return Status::Slow;
    if (s == "freeze") return Status::Freeze;
    if (s == "poison") return Status::Poison;
    if (s == "silence") return Status::Silence;
    if (s == "haste") return Status::Haste;
    return Status::None;
}
inline const char* statusName(Status s) {
    static const char* N[] = { "none", "burn", "slow", "freeze", "poison", "silence", "haste" };
    return N[(int)s < (int)Status::COUNT ? (int)s : 0];
}

// One thing a spell does to a body it reaches
struct Effect {
    EffectKind kind = EffectKind::Damage;
    float amount = 0.0f;           // damage, push speed (vox/s; < 0 pulls), poise, heal, ward, blink distance, a status's strength
    float time = 0.0f;             // s: a status, a ward, a light
    float lift = 0.2f;             // push: share of it that goes up
    float falloff = 1.0f;          // cone / area: what's left at the edge (1: no falloff)
    bool  knockdown = false;       // poise: a poise break knocks down
    Element element = Element::Physical;   // damage
    Status status = Status::None;
    std::string place;             // place: a structure (structures.toml), set down where it lands
};

struct Look {
    glm::vec3 color{ 1.0f };       // the projectile, the beam, the area's glow
    std::string cast, trail, hit, tick;   // particles.toml emitters (default: spell_<school>_cast / _trail / _hit / _tick)
    float light = 0.0f;            // > 0: it glows (a projectile's size of light)
};

struct Def {
    std::string id, name, school = "arcane";
    Delivery delivery = Delivery::Projectile;
    Use use = Use::Cast;
    uint8_t when = WHEN_GROUND | WHEN_AIR;
    int hands = 1;
    std::string anim = "push";     // a cast pose (anim_map.toml [casts])
    std::vector<std::string> tags;
    Targets targets = Targets::Enemies;
    // cost
    float stamina = 15.0f, cooldown = 0.0f;
    float castTime = 0.35f, recover = 0.35f;   // s: the wind-up, the time after
    float drain = 0.0f;            // stamina a second (channel, toggle)
    // charge: held up to this long, a full one × chargePower
    float chargeTime = 1.0f, chargePower = 2.0f;
    // shape
    float range = 40.0f;           // vox: how far it reaches (a projectile: flies; a wall / area / rune: how far out it goes)
    float radius = 6.0f;           // vox: an area, a rune's trigger, an aura, a projectile's splash on its body
    float width = 12.0f, height = 6.0f;   // a wall
    float arc = 45.0f;             // degrees either side: a cone, a touch
    float duration = 0.0f;         // s: an area, a wall, a rune, a summon, a channel's longest
    float every = 0.5f;            // s between an area's / aura's / channel's ticks
    float speed = 60.0f, gravity = 0.0f;   // a projectile
    float homing = 0.0f;           // degrees a second it turns toward an enemy ahead
    int   count = 1;               // projectiles (a fan), summons
    float spread = 12.0f;          // degrees between a fan's projectiles
    int   bounces = 0, pierce = 0; // a projectile: off walls, through bodies
    float assist = 12.0f;          // degrees: turns toward an enemy this close to its line
    bool  atSelf = false;          // an area: round the caster, not out where it aims
    // what it does, and what it starts
    std::vector<Effect> effects;
    std::string onHit, onExpire;   // spells cast where it hits / where it ends
    std::string summon;            // npcs.toml type it calls up (count of them)
    std::string place;             // a wall: the structure set across the way (effect kind, with its lifetime)
    Look look;

    const AttackDef* attack = nullptr;   // its cast (filled at load)
    int index = -1;                // in the registry (events carry it)

    bool has(const std::string& tag) const { return std::find(tags.begin(), tags.end(), tag) != tags.end(); }
    // Can it be cast in this situation (one WHEN_ bit: the body's
    // main one, grappling > rolling > air > sprinting > ground)? A
    // spell for the ground can be cast from a sprint (the sprint ends)
    bool castable(uint8_t situation) const {
        if (situation == WHEN_SPRINT) return (when & (WHEN_SPRINT | WHEN_GROUND)) != 0;
        return (when & situation) != 0;
    }
};

inline Delivery deliveryFrom(const std::string& s, bool& ok) {
    ok = true;
    if (s == "self") return Delivery::Self;
    if (s == "touch") return Delivery::Touch;
    if (s == "cone") return Delivery::Cone;
    if (s == "projectile") return Delivery::Projectile;
    if (s == "beam") return Delivery::Beam;
    if (s == "area") return Delivery::Area;
    if (s == "wall") return Delivery::Wall;
    if (s == "rune" || s == "trap") return Delivery::Rune;
    if (s == "summon") return Delivery::Summon;
    ok = false;
    return Delivery::Projectile;
}
inline const char* deliveryName(Delivery d) {
    static const char* N[] = { "self", "touch", "cone", "projectile", "beam", "area", "wall", "rune", "summon" };
    return N[(int)d];
}
inline Use useFrom(const std::string& s, bool& ok) {
    ok = true;
    if (s == "tap") return Use::Tap;
    if (s == "cast") return Use::Cast;
    if (s == "charge") return Use::Charge;
    if (s == "channel") return Use::Channel;
    if (s == "toggle") return Use::Toggle;
    ok = false;
    return Use::Cast;
}
inline const char* useName(Use u) {
    static const char* N[] = { "tap", "cast", "charge", "channel", "toggle" };
    return N[(int)u];
}

class Registry {
public:
    static constexpr const char* PATH = "data/registry/spells.toml";

    bool load(const char* path = PATH) {
        std::vector<std::unique_ptr<Def>> defs;
        std::vector<std::unique_ptr<AttackDef>> atks;
        try {
            auto tbl = toml::parse_file(path);
            auto* arr = tbl["spell"].as_array();
            if (!arr) { std::cerr << path << ": no [[spell]] entries\n"; return false; }
            for (auto& node : *arr) {
                auto* t = node.as_table();
                if (!t) continue;
                auto d = std::make_unique<Def>();
                toml::table& v = *t;
                d->id = v["id"].value_or(std::string());
                if (d->id.empty()) continue;
                d->name = v["name"].value_or(d->id);
                d->school = v["school"].value_or(std::string("arcane"));
                bool ok = true;
                d->delivery = deliveryFrom(v["delivery"].value_or(std::string("projectile")), ok);
                if (!ok) std::cerr << path << ": " << d->id << ": unknown delivery\n";
                d->use = useFrom(v["use"].value_or(std::string("cast")), ok);
                if (!ok) std::cerr << path << ": " << d->id << ": unknown use\n";
                if (auto* w = v["when"].as_array()) {
                    d->when = 0;
                    for (auto& x : *w) {
                        std::string s = x.value_or(std::string());
                        if (s == "ground") d->when |= WHEN_GROUND;
                        else if (s == "air") d->when |= WHEN_AIR;
                        else if (s == "sprinting") d->when |= WHEN_SPRINT;
                        else if (s == "grappling") d->when |= WHEN_GRAPPLE;
                        else if (s == "rolling") d->when |= WHEN_ROLL;
                        else std::cerr << path << ": " << d->id << ": unknown when '" << s << "'\n";
                    }
                }
                d->hands = (int)v["hands"].value_or((int64_t)1);
                d->anim = v["anim"].value_or(std::string("push"));
                if (auto* tg = v["tags"].as_array()) for (auto& x : *tg) if (auto s = x.value<std::string>()) d->tags.push_back(*s);
                std::string tgt = v["targets"].value_or(std::string(d->delivery == Delivery::Self ? "self" : "enemies"));
                d->targets = tgt == "allies" ? Targets::Allies : tgt == "all" ? Targets::All : tgt == "self" ? Targets::Self : Targets::Enemies;
                if (auto* c = v["cost"].as_table()) {
                    d->stamina = (float)(*c)["stamina"].value_or((double)d->stamina);
                    d->cooldown = (float)(*c)["cooldown"].value_or(0.0);
                    d->drain = (float)(*c)["drain"].value_or(0.0);
                }
                d->castTime = (float)v["cast_time"].value_or(d->use == Use::Tap ? 0.08 : 0.35);
                d->recover = (float)v["recover"].value_or(d->use == Use::Tap ? 0.25 : 0.35);
                d->chargeTime = (float)v["charge_time"].value_or(1.0);
                d->chargePower = (float)v["charge_power"].value_or(2.0);
                auto f = [&](const char* k, float& into) { into = (float)v[k].value_or((double)into); };
                f("range", d->range); f("radius", d->radius); f("width", d->width); f("height", d->height);
                f("arc", d->arc); f("duration", d->duration); f("every", d->every); f("speed", d->speed);
                f("gravity", d->gravity); f("homing", d->homing); f("spread", d->spread); f("assist", d->assist);
                d->count = (int)v["count"].value_or((int64_t)1);
                d->bounces = (int)v["bounces"].value_or((int64_t)0);
                d->pierce = (int)v["pierce"].value_or((int64_t)0);
                d->atSelf = v["at"].value_or(std::string()) == "self";
                d->onHit = v["on_hit"].value_or(std::string());
                d->onExpire = v["on_expire"].value_or(std::string());
                d->summon = v["summon"].value_or(std::string());
                d->place = v["place"].value_or(std::string());
                if (auto* ef = v["effects"].as_array())
                    for (auto& e : *ef) {
                        auto* et = e.as_table();
                        if (!et) continue;
                        Effect x;
                        std::string k = (*et)["kind"].value_or(std::string("damage"));
                        if (k == "damage") x.kind = EffectKind::Damage;
                        else if (k == "push") x.kind = EffectKind::Push;
                        else if (k == "pull") { x.kind = EffectKind::Push; }
                        else if (k == "poise") x.kind = EffectKind::Poise;
                        else if (k == "status") x.kind = EffectKind::Status;
                        else if (k == "heal") x.kind = EffectKind::Heal;
                        else if (k == "ward") x.kind = EffectKind::Ward;
                        else if (k == "place") x.kind = EffectKind::Place;
                        else if (k == "blink") x.kind = EffectKind::Blink;
                        else if (k == "light") x.kind = EffectKind::Light;
                        else { std::cerr << path << ": " << d->id << ": unknown effect '" << k << "'\n"; continue; }
                        x.amount = (float)(*et)["amount"].value_or(0.0);
                        if (k == "pull") x.amount = -std::fabs(x.amount);
                        x.time = (float)(*et)["time"].value_or(0.0);
                        x.lift = (float)(*et)["lift"].value_or(0.2);
                        x.falloff = (float)(*et)["falloff"].value_or(1.0);
                        x.knockdown = (*et)["knockdown"].value_or(false);
                        x.element = elementFrom((*et)["type"].value_or(d->school));
                        if (x.kind == EffectKind::Status) {
                            x.status = statusFrom((*et)["status"].value_or(std::string()));
                            if (x.status == Status::None) std::cerr << path << ": " << d->id << ": unknown status\n";
                        }
                        x.place = (*et)["structure"].value_or(std::string());
                        d->effects.push_back(x);
                    }
                // the look: by school unless named
                std::string sc = d->school;
                if (auto* lk = v["look"].as_table()) {
                    if (auto* c = (*lk)["color"].as_array(); c && c->size() >= 3)
                        d->look.color = glm::vec3((float)(*c)[0].value_or(1.0), (float)(*c)[1].value_or(1.0), (float)(*c)[2].value_or(1.0));
                    d->look.cast = (*lk)["cast"].value_or(std::string());
                    d->look.trail = (*lk)["trail"].value_or(std::string());
                    d->look.hit = (*lk)["hit"].value_or(std::string());
                    d->look.tick = (*lk)["tick"].value_or(std::string());
                    d->look.light = (float)(*lk)["light"].value_or(0.0);
                }
                if (d->look.cast.empty()) d->look.cast = "spell_" + sc + "_cast";
                if (d->look.trail.empty()) d->look.trail = "spell_" + sc + "_trail";
                if (d->look.hit.empty()) d->look.hit = "spell_" + sc + "_hit";
                if (d->look.tick.empty()) d->look.tick = "spell_" + sc + "_tick";
                // its cast: an attack like any other
                auto a = std::make_unique<AttackDef>();
                a->id = "spell:" + d->id;
                a->pose = "cast_" + d->anim;
                a->spell = d->id;
                a->effect = "spell";
                a->blade = false;
                a->damage = 0.0f; a->poise = 0.0f; a->reach = 0.0f; a->knock = 0.0f;
                a->stamina = d->stamina;
                a->activeStart = d->castTime;
                a->activeEnd = d->castTime + 0.05f;
                a->duration = d->castTime + 0.05f + d->recover;
                a->cancelFrom = d->castTime + 0.05f + d->recover * 0.4f;
                a->track = 540.0f;
                a->cooldown = d->cooldown;
                a->when = d->when;
                if (d->use == Use::Charge) {
                    a->chargeAt = d->castTime * 0.9f;
                    a->chargeTime = d->chargeTime;
                    a->chargeDamage = d->chargePower;
                    a->chargePoise = d->chargePower;
                }
                if (d->use == Use::Channel) {
                    a->channel = true;
                    a->channelMax = d->duration > 0.0f ? d->duration : 6.0f;
                }
                d->attack = a.get();
                atks.push_back(std::move(a));
                defs.push_back(std::move(d));
            }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
        // the old ones are kept (a body may be mid-cast with one)
        for (auto& d : list) retiredDefs.push_back(std::move(d));
        for (auto& a : attacks) retiredAtks.push_back(std::move(a));
        list = std::move(defs);
        attacks = std::move(atks);
        byId.clear();
        for (size_t i = 0; i < list.size(); i++) { list[i]->index = (int)i; byId[list[i]->id] = list[i].get(); }
        std::cout << "Spells: " << list.size() << " from " << path << "\n";
        return true;
    }

    const Def* find(const std::string& id) const {
        auto it = byId.find(id);
        return it == byId.end() ? nullptr : it->second;
    }
    const Def* at(int index) const { return index >= 0 && index < (int)list.size() ? list[(size_t)index].get() : nullptr; }
    // the spell an attack casts (its AttackDef::spell), if any
    const Def* of(const AttackDef& a) const { return a.spell.empty() ? nullptr : find(a.spell); }
    const AttackDef* attack(const std::string& id) const { const Def* d = find(id); return d ? d->attack : nullptr; }
    const std::vector<std::unique_ptr<Def>>& all() const { return list; }
    size_t size() const { return list.size(); }

private:
    std::vector<std::unique_ptr<Def>> list;
    std::vector<std::unique_ptr<AttackDef>> attacks;
    std::vector<std::unique_ptr<Def>> retiredDefs;
    std::vector<std::unique_ptr<AttackDef>> retiredAtks;
    std::unordered_map<std::string, Def*> byId;
};

} // namespace spell

inline spell::Registry gSpells;
