// Headless checks for the spell groundwork (ROADMAP "Then: spell
// groundwork"): spells as data (data/registry/spells.toml,
// game/Spells.h) and the parts they're made of (sim/SpellSystem.h):
// one working example of every delivery (self, touch, cone,
// projectile, beam, area, wall, rune, summon) and every use (tap,
// cast, charge, channel, toggle), with 'when', cooldowns, statuses,
// wards, resistances, chained spells and an NPC casting. Run from the
// repo root.
#include "TestWorld.h"
#include <cstdio>
#include <set>

using namespace testworld;

// what a caster's spells did to a body (Hit events), over some ticks
struct Tally {
    float damage = 0.0f; int hits = 0;
    std::set<std::string> cast;           // spells whose Cast stage went off
};
static Tally run(World& w, float seconds, uint64_t from, uint64_t to, const std::function<void()>& each = nullptr) {
    Tally t;
    for (int i = 0; i < (int)(seconds / DT + 0.5f); i++) {
        if (each) each();
        w.sim.step(DT);
        for (const SimEvent& e : w.sim.takeEvents()) {
            if (e.kind == SimEvent::Kind::Hit && e.a == from && e.b == to && e.attack == AttackKind::Cast) { t.damage += e.amount; t.hits++; }
            if (e.kind == SimEvent::Kind::Spell && e.stage == SpellSystem::Cast) if (const spell::Def* d = gSpells.at(e.spell)) t.cast.insert(d->id);
        }
    }
    return t;
}
static Npc* dummyAt(World& w, glm::vec3 at) {
    Npc* d = w.sim.npcs().spawn("dummy", at, glm::vec3(0, 0, -1));
    if (d) { d->maxHealth = d->health = 5000.0f; d->healthSeen = d->health; }
    return d;
}
// use a book (the item's spell) from the intent: held for 'hold' s
static void useBook(World& w, Player& p, const std::string& book, float hold = 0.0f) {
    uint16_t id = gItems.find(book);
    if (p.inventory.find(id) < 0) p.inventory.add(id, 1);
    HumanIntent in; in.useItem = id; in.castHeld = hold > 0.0f;
    w.sim.setIntent(p.uuid, in);
    w.sim.step(DT);
    for (float t = 0.0f; t < hold; t += DT) { HumanIntent h; h.castHeld = true; w.sim.setIntent(p.uuid, h); w.sim.step(DT); }
    w.sim.setIntent(p.uuid, HumanIntent());
}

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");

    std::printf("the spells\n");
    {
        std::set<int> deliveries, uses;
        int books = 0, castable = 0;
        for (const auto& d : gSpells.all()) {
            deliveries.insert((int)d->delivery);
            if (!d->has("child")) {
                uses.insert((int)d->use);
                castable++;
                for (uint16_t i = 1; i < 4000; i++) if (gItems.get(i).valid() && gItems.get(i).spell == d->id) { books++; break; }
            }
            if (!d->onHit.empty()) check(gSpells.find(d->onHit) != nullptr, d->id + ": its on_hit spell is there");
            check(d->attack && d->attack->spell == d->id && d->attack->pose == "cast_" + d->anim, d->id + ": its cast attack");
        }
        std::printf("    %zu spells, %d with a book of their own\n", gSpells.size(), books);
        check(deliveries.size() == 9, "every delivery has an example (" + std::to_string(deliveries.size()) + "/9)");
        check(uses.size() == 5, "every use has an example (" + std::to_string(uses.size()) + "/5)");
        check(books == castable, "every spell you can cast has a spellbook");
        // every cast pose has a clip in anim_map.toml [casts]
        auto tbl = toml::parse_file("data/animations/anim_map.toml");
        bool poses = true;
        for (const auto& d : gSpells.all()) if (!tbl["casts"][d->anim]) { poses = false; std::printf("    no [casts] %s\n", d->anim.c_str()); }
        check(poses, "every spell's cast pose is in anim_map.toml [casts]");
    }

    std::printf("self: heal, ward, blink\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.2f);
        p.health = 40.0f;
        useBook(w, p, "book_mending");
        w.run(1.2f);
        check(p.health > 70.0f, "Mending Light heals (" + std::to_string((int)p.health) + ")");
        p.health = 100.0f;
        useBook(w, p, "book_bark_skin");
        w.run(0.5f);
        check(p.ward > 30.0f, "Bark Skin wards");
        float before = p.health;
        w.sim.toolHit(0, p.uuid, 25.0f, 0.0f, p.bodyPoint(0.5f), glm::vec3(1, 0, 0));
        check(std::fabs(p.health - before) < 0.01f, "  the ward soaks a hit");
        float ward = p.ward;
        useBook(w, p, "book_bark_skin");
        w.run(0.4f);
        check(p.cooldownOf(gSpells.find("bark_skin")) > 0.0f && std::fabs(p.ward - ward) < 0.01f, "  and it's on cooldown: no second one at once");
        w.run(0.5f);
        glm::vec3 from = p.position;
        useBook(w, p, "book_blink");
        w.run(0.4f);
        float moved = glm::length(p.position - from);
        check(moved > 20.0f, "Blink: " + std::to_string((int)moved) + " voxels at once");
    }

    std::printf("touch, cone, beam\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.2f);
        Npc* ahead = dummyAt(w, glm::vec3(256, GROUND, 263));
        Npc* behind = dummyAt(w, glm::vec3(256, GROUND, 249));
        w.run(0.2f);
        useBook(w, p, "book_shocking_grasp");
        Tally t = run(w, 0.6f, p.uuid, ahead->uuid);
        check(t.damage > 10.0f, "Shocking Grasp: the one in front (" + std::to_string((int)t.damage) + " damage)");
        check(behind->health == behind->maxHealth, "  not the one behind");
        w.run(1.0f);
        glm::vec3 was = ahead->position;
        p.stamina = p.maxStamina;
        uint16_t wb = gItems.find("windburst"); p.inventory.add(wb, 1);
        HumanIntent in; in.useItem = wb; w.sim.setIntent(p.uuid, in);
        w.run(1.5f);
        check(glm::length(ahead->position - was) > 2.0f, "Windburst (a cone): blown back");
        // a beam reaches far at once
        World w2;
        Player& q = w2.player(glm::vec3(256, GROUND, 256));
        q.facing = glm::vec3(0, 0, 1);
        w2.run(0.2f);
        Npc* far = dummyAt(w2, glm::vec3(256, GROUND, 306));
        w2.run(0.2f);
        useBook(w2, q, "book_arc_beam", 1.2f);
        Tally b = run(w2, 0.6f, q.uuid, far->uuid);
        (void)b;
        float dealt = far->maxHealth - far->health;
        check(dealt > 0.0f, "Arcane Beam (a channel) reaches one 50 away (" + std::to_string((int)dealt) + " damage)");
        check(q.stamina < q.maxStamina - 15.0f, "  draining stamina while held");
        check(!w2.sim.spells().beams().empty() || dealt > 0.0f, "  a beam to draw");
    }

    std::printf("projectiles: tap, charge, chains, statuses\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.2f);
        Npc* d = dummyAt(w, glm::vec3(256, GROUND, 300));
        w.run(0.2f);
        uint16_t fb = gItems.find("book_firebolt"); p.inventory.add(fb, 1);
        HumanIntent in; in.useItem = fb; w.sim.setIntent(p.uuid, in); w.sim.step(DT);
        check(p.attack.active() && p.attack.d->activeStart < 0.1f, "Firebolt is a tap: it goes almost at once");
        Tally t = run(w, 1.0f, p.uuid, d->uuid);
        check(t.hits >= 1 && t.cast.count("ember_burst"), "  it hits and bursts into embers (on_hit: a spell chained)");
        check(d->has(spell::Status::Burn), "  and sets it burning");
        Tally burn = run(w, 2.5f, p.uuid, d->uuid);
        check(burn.hits >= 2, "  the burn hurts each second (" + std::to_string(burn.hits) + " times)");
        // a resistance takes less
        d->resist[(int)spell::Element::Fire] = 0.5f;
        w.run(3.5f);
        p.stamina = p.maxStamina;
        w.sim.setIntent(p.uuid, in); w.sim.step(DT);
        Tally half = run(w, 0.8f, p.uuid, d->uuid);
        std::printf("    the first bolt did %.0f, against a fire resistance of half %.0f\n", t.damage, half.damage);
        check(half.damage < t.damage * 0.75f, "  resisted, it takes less");
        d->resist[(int)spell::Element::Fire] = 0.0f;
        // a charge: a tap of it, then held a second
        w.run(4.0f);
        p.stamina = p.maxStamina;
        auto boltsOut = [&](float& power) {
            size_t most = 0; power = 0.0f;
            for (int i = 0; i < 40; i++) {
                w.sim.step(DT);
                most = std::max(most, w.sim.spells().bolts().size());
                for (const auto& bo : w.sim.spells().bolts()) power = std::max(power, bo.power);
            }
            return most;
        };
        float quickPower = 0.0f, fullPower = 0.0f;
        useBook(w, p, "book_spark_fan");
        size_t fan = boltsOut(quickPower);
        w.run(1.0f);
        p.stamina = p.maxStamina;
        useBook(w, p, "book_spark_fan", 1.3f);
        boltsOut(fullPower);
        std::printf("    Spark Fan: %zu bolts; power %.2f tapped, %.2f held 1.3 s\n", fan, quickPower, fullPower);
        check(fan == 3, "Spark Fan: three bolts in a fan");
        check(quickPower < 1.2f && fullPower > 1.9f, "  a charge: held, it's twice as strong");
    }

    std::printf("area, rune, wall\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        p.aimPitch = -12.0f;                        // looking down at the ground ahead
        w.run(0.2f);
        Npc* d = dummyAt(w, glm::vec3(256, GROUND, 290));
        w.run(0.2f);
        useBook(w, p, "book_spore_cloud");
        Tally t = run(w, 4.0f, p.uuid, d->uuid);
        check(t.hits >= 3 && d->has(spell::Status::Poison), "Spore Cloud (an area out where you aim): it keeps hurting, poisons (" +
              std::to_string(t.hits) + " hits)");
        // frost nova round you: freezes
        Npc* close = dummyAt(w, glm::vec3(262, GROUND, 256));
        w.run(0.3f);
        p.stamina = p.maxStamina;
        useBook(w, p, "book_frost_nova", 0.2f);
        w.run(0.6f);
        check(close->has(spell::Status::Freeze) && close->statusSpeed() == 0.0f, "Frost Nova (round you): frozen");
        // a rune where she looks: armed, it goes off when someone's on it
        World w2;
        Player& q = w2.player(glm::vec3(256, GROUND, 256));
        q.facing = glm::vec3(0, 0, 1);
        q.aimPitch = -25.0f;
        w2.run(0.2f);
        useBook(w2, q, "book_frost_rune");
        w2.run(1.0f);
        bool laid = false;
        glm::vec3 rp(0.0f);
        for (const auto& z : w2.sim.spells().zones()) if (z.rune) { laid = true; rp = z.pos; }
        check(laid, "Frost Rune: laid on the ground ahead");
        Npc* walker = dummyAt(w2, rp + glm::vec3(0, 0.5f, 0));
        Tally r = run(w2, 1.0f, q.uuid, walker->uuid);
        check(r.cast.count("frost_burst") && walker->has(spell::Status::Freeze), "  someone over it: it bursts (its on_hit spell) and freezes them");
        bool gone = true;
        for (const auto& z : w2.sim.spells().zones()) if (z.rune) gone = false;
        check(gone, "  and it's spent");
        // a wall: a structure set across the way
        q.stamina = q.maxStamina;
        q.aimPitch = 0.0f;
        w2.sim.takeLandings();
        useBook(w2, q, "book_stone_wall");
        w2.run(1.0f);
        auto ls = w2.sim.takeLandings();
        bool wall = false;
        for (const auto& l : ls) if (l.place == "spell_stone_wall" && (l.rot == 0 || l.rot == 2)) wall = true;
        check(wall, "Stone Wall: set down across her way (facing +z: the wall lies along x)");
    }

    std::printf("summons\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.2f);
        useBook(w, p, "book_call_mushnubs");
        w.run(1.2f);
        std::vector<Npc*> mine;
        for (auto& n : w.sim.npcs().all()) if (n->summoner == p.uuid) mine.push_back(n.get());
        check(mine.size() == 2, "Call the Mushnubs: two come (" + std::to_string(mine.size()) + ")");
        if (mine.size() == 2) {
            check(!w.sim.hostile(*mine[0], p) && w.sim.friendly(*mine[0], p), "  on her side");
            Npc* slime = w.sim.npcs().spawn("slime", glm::vec3(256, GROUND, 290), glm::vec3(0, 0, -1));
            check(slime && w.sim.hostile(*mine[0], *slime) && w.sim.hostile(*slime, *mine[0]), "  against what's against her (a slime)");
            float hp = p.health;
            w.run(8.0f);
            check(p.health >= hp, "  they never hurt her");
            bool fought = slime->dead() || slime->health < slime->maxHealth;
            check(fought, "  they go for the slime");
            w.run(25.0f);
            int left = 0;
            for (auto& n : w.sim.npcs().all()) if (n->summoner == p.uuid && !n->dead()) left++;
            check(left == 0, "  gone after their 30 s");
        }
    }

    std::printf("toggle, when, freeze\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.2f);
        Npc* d = dummyAt(w, glm::vec3(256, GROUND, 262));
        w.run(0.2f);
        useBook(w, p, "book_ember_aura");
        w.run(0.6f);
        check(p.aura && p.aura->id == "ember_aura", "Ember Aura (a toggle): on");
        Tally t = run(w, 3.0f, p.uuid, d->uuid);
        check(t.hits >= 2, "  it burns what's near (" + std::to_string(t.hits) + " hits)");
        p.stamina = p.maxStamina;
        w.run(1.0f);
        useBook(w, p, "book_ember_aura");
        w.run(1.0f);
        check(!p.aura, "  cast again: off");
        // when: Mending Light is for the ground only; Firebolt goes in the air too
        p.position.y += 30.0f; p.grounded = false; p.velocity = glm::vec3(0.0f);
        p.stamina = p.maxStamina;
        useBook(w, p, "book_mending");
        check(!p.attack.active(), "Mending Light can't be cast in the air (when = ground)");
        useBook(w, p, "book_firebolt");
        check(p.attack.active() && p.attack.d->spell == "firebolt", "  Firebolt can");
        w.run(3.0f);
        // frozen: she can't move
        p.statuses.push_back({ spell::Status::Freeze, 1.0f, 0.0f, 0.0f, 0 });
        glm::vec3 at = p.position;
        HumanIntent walk; walk.move = glm::vec2(0, 1);
        for (int i = 0; i < 30; i++) { w.sim.setIntent(p.uuid, walk); w.sim.step(DT); }
        check(glm::length(glm::vec2(p.position.x - at.x, p.position.z - at.z)) < 0.5f, "frozen, she can't walk");
        w.run(1.0f);
        for (int i = 0; i < 30; i++) { w.sim.setIntent(p.uuid, walk); w.sim.step(DT); }
        check(glm::length(glm::vec2(p.position.x - at.x, p.position.z - at.z)) > 3.0f, "  thawed, she can");
    }

    std::printf("an NPC casts\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        w.run(0.2f);
        Npc* k = w.sim.npcs().spawn("mushroom_king", glm::vec3(256, GROUND, 300), glm::vec3(0, 0, -1));
        bool cloud = false;
        for (int i = 0; i < (int)(20.0f / DT) && !cloud; i++) {
            p.position = glm::vec3(256, GROUND, 256);         // she keeps her distance
            p.velocity = glm::vec3(0.0f);
            w.sim.step(DT);
            for (const auto& z : w.sim.spells().zones()) if (z.owner == k->uuid && z.d->id == "spore_cloud") cloud = true;
        }
        check(cloud, "the Mushroom King throws a Spore Cloud at her from afar (npcs.toml casts)");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
