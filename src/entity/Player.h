#pragma once
#include <unordered_map>
#include "Human.h"
#include "../game/GameMode.h"
#include "../game/Inventory.h"
#include "../game/CombatKit.h"
#include "../character/Appearance.h"
#include "../game/AxeSwing.h"
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>

// ── Player ────────────────────────────────────────────────
// The human the person at the keyboard controls. Everything
// about how the body moves lives in Human; this adds only
// what is specific to a player.
class Player : public Human {
public:
    // Where the player comes back to after falling out of
    // the world
    glm::vec3 spawnPoint = {0, 0, 0};
    float     deadFor = 0.0f;          // s since dying (Simulation respawns)
    bool isPlayer() const override { return true; }

    // Reputation with each faction (npc/Factions.h), by faction
    // id; a faction not listed is at its player_standing
    std::unordered_map<std::string, float> reputation;

    // ── Game mode ─────────────────────────────────────────
    // Per player, never global — see game/GameMode.h
    GameMode mode = GameMode::Survival;

    // Weapon drawn. Only means anything in Survival, where
    // it switches the controls, HUD and camera to combat.
    bool combatStance = false;
    // What the combat buttons hold: weapon sets, X / Y / B lists,
    // the 8 quick options (game/CombatKit.h)
    CombatKit kit;

    // ── Two inventories ───────────────────────────────────
    // The player inventory is the real one: used in Survival
    // and never touched by switching modes, so testing in
    // dev mode cannot wipe it.
    // The dev inventory is a separate scratch toolbar for
    // Test Play: infinite blocks, dev tools. It is saved too,
    // but Survival cannot reach it.
    Inventory inventory;
    Inventory devInventory = makeDevInventory();

    // How the body looks (the collision body is the same
    // for everyone — see Human.h)
    Appearance appearance;

    // What this player may do right now. Holding an axe forces
    // the third-person camera (its swing is meant to be seen).
    // A weapon held in the toolbar fights the same as a drawn
    // one: third person, and the mouse attacks instead of building.
    GameModeRules rules() const {
        GameModeRules r = rulesFor(mode, combatStance);
        if (r.hasBody && !combatStance && holdingAxe()) r.forceThirdPerson = true;
        if (r.hasBody && !combatStance && holdingWeapon()) {
            r.forceThirdPerson = true;
            r.combatActions    = true;
            r.worldInteraction = false;
        }
        return r;
    }
    bool holdingWeapon() const {
        return activeInventory().heldDef().is(IC_WEAPON);
    }
    // The weapon in the right hand, or nullptr: the drawn weapon
    // slot in the combat stance, else a weapon in the toolbar
    const ItemDef* wielded() const {
        const Inventory& inv = activeInventory();
        if (combatStance) return inv.weapon().empty() ? nullptr : &gItems.get(inv.weapon().itemID);
        const ItemDef& d = inv.heldDef();
        return d.is(IC_WEAPON) ? &d : nullptr;
    }
    // Keep Human's combat numbers in step with the hand
    // (called every frame, like refreshMass)
    void refreshWeapon() {
        const ItemDef* w = wielded();
        weaponDamage = w ? w->damage : 0.0f;
        moveset = w ? gMovesets.find(w->moveset) : nullptr;
        weaponWeight = w ? w->weight : 0.0f;
        infiniteStamina = (mode == GameMode::Dev);
        // a shield in this loadout's off hand (combat stance): the
        // guard is the shield's
        const ItemDef* sh = shieldUp();
        if (sh) setGuard(1.0f - sh->shieldBlock, sh->shieldCost, sh->shieldArc, sh->shieldParry, true);
        else weaponGuard();
        if (!w) {                       // nothing to swing: no swing (a spell's cast needs no weapon)
            if (attack.kind != AttackKind::Cast) attack = AttackState();
            if (!sh) guarding = false;
        }
    }
    // The shield up in the off hand, if any: the drawn loadout's off
    // hand (game/CombatKit.h), while it's still carried
    const ItemDef* shieldUp() const {
        if (!combatStance || mode != GameMode::Survival) return nullptr;
        int set = std::clamp(inventory.activeWeapon, 0, CombatKit::SETS - 1);
        uint16_t off = kit.sets[(size_t)set].off;
        if (!off || inventory.find(off) < 0) return nullptr;
        const ItemDef& d = gItems.get(off);
        return d.shield ? &d : nullptr;
    }
    // What's in the left hand in combat: the loadout's off hand
    // (a shield, a second weapon, a spellbook), if carried
    uint16_t offhandInCombat() const {
        if (!combatStance) return 0;
        int set = std::clamp(inventory.activeWeapon, 0, CombatKit::SETS - 1);
        uint16_t off = kit.sets[(size_t)set].off;
        return off && inventory.find(off) >= 0 ? off : 0;
    }
    bool holdingAxe() const {
        const ItemDef& d = activeInventory().heldDef();
        return d.is(IC_TOOL) && d.tool == ToolKind::Axe;
    }

    // The axe swing in progress (game/AxeSwing.h)
    AxeSwing swing;

    // The toolbar this mode uses
    Inventory& activeInventory() {
        return mode == GameMode::Survival ? inventory : devInventory;
    }
    const Inventory& activeInventory() const {
        return mode == GameMode::Survival ? inventory : devInventory;
    }

    // ── Weight ────────────────────────────────────────────
    // The body's mass from its look, plus everything on it:
    //   worn gear     on the parts it covers
    //   weapon slot   in the right hand when drawn (combat
    //                 stance), on the back when sheathed
    //   held item     the toolbar item, in the right hand
    //                 (not while a weapon is drawn)
    // Cheap; called every frame so changes show at once.
    void refreshMass() {
        const Appearance& a = appearance;
        body.setBody(BodyMass::bodyKgFor(a.build, a.heightVoxels, a.shoulders, a.chest, a.hips, a.limbs));
        body.clearGear();
        const Inventory& inv = activeInventory();
        for (int s = 0; s < WORN_SLOTS; s++)
            if (!inv.worn((WornSlot)s).empty())
                body.addWorn((WornSlot)s, gItems.get(inv.worn((WornSlot)s).itemID).weight);
        // the equipped weapons: the drawn one in the hand, the
        // rest on the back (an equipped weapon that is also the
        // held hotbar item counts once, in the hand)
        int heldSlot = combatStance ? -1 : inv.selected;
        for (int w = 0; w < 2; w++) {
            EquipSlot e = (EquipSlot)((int)EquipSlot::Weapon1 + w);
            int s = inv.equip[(int)e];
            if (s < 0 || s == heldSlot || (w == 1 && s == inv.equip[0])) continue;
            float kg = gItems.get(inv.slots[s].itemID).weight;
            if (combatStance && e == inv.weaponSlot()) body.addGear(SEG_HAND_R, kg);
            else                                       body.addWorn(WornSlot::Back, kg);
        }
        if (!combatStance && !inv.held().empty())
            body.addGear(SEG_HAND_R, gItems.get(inv.held().itemID).weight);
        if (!inv.offhandItem().empty())
            body.addGear(SEG_HAND_L, gItems.get(inv.offhandItem().itemID).weight);
        mass = body.total();
        // poise: the base plus what is worn
        float wornKg = 0.0f;
        for (int s = 0; s < WORN_SLOTS; s++)
            if (!inv.worn((WornSlot)s).empty()) wornKg += gItems.get(inv.worn((WornSlot)s).itemID).weight;
        float mp = combat::POISE_BASE + combat::POISE_PER_KG * wornKg;
        if (std::fabs(mp - maxPoise) > 1e-3f) { poise = std::min(poise, mp) + std::max(0.0f, mp - maxPoise); maxPoise = mp; }
    }

    // Change mode. The combat stance never carries over;
    // inventories are left exactly as they are.
    void setMode(GameMode newMode) {
        mode         = newMode;
        combatStance = false;
    }

    // ── Save / load ───────────────────────────────────────
    // Plain "key=value" lines, written to saves/<world>/
    // player.dat by WorldSaveManager. Unknown keys are
    // ignored, so older and newer saves still load.
    void write(std::ostream& out) const {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "pos=%.3f %.3f %.3f\n",
                      position.x, position.y, position.z);
        out << buf;
        std::snprintf(buf, sizeof(buf), "facing=%.4f %.4f %.4f\n",
                      facing.x, facing.y, facing.z);
        out << buf;
        out << "mode="   << gameModeKey(mode) << "\n";
        out << "health=" << health << "\n";
        inventory.write(out, "inv");
        devInventory.write(out, "dev");
        appearance.write(out);
        kit.write(out);
        for (const auto& [f, v] : reputation) out << "rep." << f << "=" << v << "\n";
    }

    bool read(const std::string& key, const std::string& value) {
        if (key == "pos") {
            std::sscanf(value.c_str(), "%f %f %f",
                        &position.x, &position.y, &position.z);
        } else if (key == "facing") {
            std::sscanf(value.c_str(), "%f %f %f",
                        &facing.x, &facing.y, &facing.z);
        } else if (key == "mode") {
            setMode(gameModeFromKey(value, mode));
        } else if (key == "health") {
            health = (float)std::atof(value.c_str());
        } else if (key.rfind("rep.", 0) == 0) {
            reputation[key.substr(4)] = (float)std::atof(value.c_str());
        } else {
            return appearance.read(key, value) || kit.read(key, value) ||
                   inventory.read(key, value, "inv") ||
                   devInventory.read(key, value, "dev");
        }
        return true;
    }

    // Before a world is opened: a fresh player
    void resetForNewWorld() {
        Player fresh;
        *this = fresh;
    }

private:
    static Inventory makeDevInventory() {
        Inventory inv;
        inv.infinite = true;
        return inv;
    }
};
