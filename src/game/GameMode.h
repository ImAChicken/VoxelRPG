#pragma once
#include <cstdint>
#include <string>

// ══════════════════════════════════════════════════════════
// Game modes
// ══════════════════════════════════════════════════════════
// A mode belongs to a PLAYER, not to the game: in
// multiplayer one person can be spectating while another
// plays. Each world also stores a default mode, which is
// what a player starts in when the world is opened.
//
//   Spectator — free-flying camera, no body, no collision
//   Dev       — test play: a body with physics, flying
//               allowed (V), nothing tracked
//   Survival  — the real game: health and inventory
//
// Combat is NOT a mode. It is a stance inside Survival
// (weapon drawn), stored as a flag on the player. Both
// stances track health and inventory; they differ in what
// the buttons do, which HUD is shown and where the camera
// sits.
enum class GameMode : uint8_t {
    Spectator = 0,
    Dev       = 1,
    Survival  = 2,
    COUNT
};

// Which heads-up display to draw
enum class HudKind : uint8_t {
    Spectator,   // mode label only
    Dev,         // label + crosshair
    Survival,    // health, toolbar, crosshair
    Combat,      // health, weapon/guard panel, target marker
};

// ── What a mode allows ────────────────────────────────────
// Game code asks these questions instead of testing the
// mode directly ("if (rules.canFly)" rather than
// "if (mode == Dev)"), so adding or changing a mode means
// editing only the table in rulesFor() below.
struct GameModeRules {
    const char* name;           // shown on the HUD and title bar
    bool hasBody;               // physics body (false = free camera)
    bool canFly;                // V toggles flying
    bool tracksHealth;
    bool tracksInventory;
    bool worldInteraction;      // place / remove / interact
    bool combatActions;         // attack / guard
    bool forceThirdPerson;      // camera locked behind the player
    HudKind hud;
};

// The one table that defines every mode.
//   combatStance — the player's weapon-drawn flag; only
//                  means anything in Survival
inline GameModeRules rulesFor(GameMode mode, bool combatStance) {
    GameModeRules r;
    switch (mode) {
    case GameMode::Dev:
        r.name             = "Test Play";
        r.hasBody          = true;
        r.canFly           = true;
        r.tracksHealth     = false;
        r.tracksInventory  = false;
        r.worldInteraction = true;
        r.combatActions    = false;
        r.forceThirdPerson = false;
        r.hud              = HudKind::Dev;
        return r;

    case GameMode::Survival:
        r.hasBody          = true;
        r.canFly           = false;
        r.tracksHealth     = true;    // both stances
        r.tracksInventory  = true;    // both stances
        if (combatStance) {
            r.name             = "Combat";
            r.worldInteraction = false;
            r.combatActions    = true;
            r.forceThirdPerson = true;
            r.hud              = HudKind::Combat;
        } else {
            r.name             = "Survival";
            r.worldInteraction = true;
            r.combatActions    = false;
            r.forceThirdPerson = false;
            r.hud              = HudKind::Survival;
        }
        return r;

    case GameMode::Spectator:
    default:
        r.name             = "Spectator";
        r.hasBody          = false;
        r.canFly           = true;
        r.tracksHealth     = false;
        r.tracksInventory  = false;
        r.worldInteraction = false;
        r.combatActions    = false;
        r.forceThirdPerson = false;
        r.hud              = HudKind::Spectator;
        return r;
    }
}

// ── Names used in the world save file ─────────────────────
inline const char* gameModeKey(GameMode mode) {
    switch (mode) {
    case GameMode::Dev:      return "dev";
    case GameMode::Survival: return "survival";
    default:                 return "spectator";
    }
}

// Unknown or missing text gives 'fallback'
inline GameMode gameModeFromKey(const std::string& key,
                                GameMode fallback) {
    if (key == "spectator") return GameMode::Spectator;
    if (key == "dev")       return GameMode::Dev;
    if (key == "survival")  return GameMode::Survival;
    return fallback;
}

// F4 — the mode after this one, wrapping round
inline GameMode nextGameMode(GameMode mode) {
    int n = ((int)mode + 1) % (int)GameMode::COUNT;
    return (GameMode)n;
}
