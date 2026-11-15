#pragma once
#include "Human.h"
#include "../game/GameMode.h"
#include "../game/Inventory.h"
#include "../character/Appearance.h"
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

    // ── Game mode ─────────────────────────────────────────
    // Per player, never global — see game/GameMode.h
    GameMode mode = GameMode::Survival;

    // Weapon drawn. Only means anything in Survival, where
    // it switches the controls, HUD and camera to combat.
    bool combatStance = false;

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

    // What this player may do right now
    GameModeRules rules() const {
        return rulesFor(mode, combatStance);
    }

    // The toolbar this mode uses
    Inventory& activeInventory() {
        return mode == GameMode::Survival ? inventory : devInventory;
    }
    const Inventory& activeInventory() const {
        return mode == GameMode::Survival ? inventory : devInventory;
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
        } else {
            return appearance.read(key, value) ||
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
