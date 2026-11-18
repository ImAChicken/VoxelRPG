#pragma once
#include "InputAction.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// PadLayout — a game controller, as actions
// ══════════════════════════════════════════════════════════
// The controller drives the same InputActions as the keyboard,
// so gameplay code never asks which one is in use. What each
// button means depends on the LAYER (Eddie's layout, 2026-11-18):
//
//   both      left stick move, right stick camera, A jump (held
//             0.4 s: interact), L3 crouch / sneak, R3 lock on
//             (right stick left / right while locked: next
//             target), Select: combat mode on / off
//   combat    LB sprint (hold) / dodge (tap), RB light, RT heavy,
//             LT special (the off hand's, or the weapon's: guard),
//             X / Y / B use the item in that slot, D-pad left /
//             up / right the next item in that slot, down the next
//             weapon set. No menus: Select out of combat first.
//   quick     combat with Start held: D-pad and X / Y / B / A pick
//             one of the 8 quick options (the same for every
//             weapon set)
//   explore   out of combat: LB / RB the hotbar, LT / RT like the
//             left / right mouse buttons, B sprint / roll, Y the
//             inventory, X turn the hammer's piece, D-pad nudge,
//             Start the pause menu
//
// The table below is the default layout. data/controls/pad.toml
// (InputManager::loadPad) rebinds any of it, and so does the
// Controls screen's Controller tab. The button that jumps also
// interacts when held (wherever Jump is put), and the one that
// opens the quick layer is wherever QuickLayer is. The controller
// debug overlay (debug pad) draws the same table, so a keyboard
// press lights the pad button it stands for.
namespace pad {

// Our names for the buttons (GLFW's gamepad order, then the two
// triggers as buttons). No GLFW here, so tests can use it; the
// pad itself is read by InputManager (readPad).
enum Button : int {
    A, B, X, Y, LB, RB, Back, Start, Guide, L3, R3, Up, Right, Down, Left, LT, RT, COUNT
};
inline const char* buttonName(int b) {
    static const char* N[] = { "A", "B", "X", "Y", "LB", "RB", "Select", "Start", "Guide", "L3", "R3",
                               "Up", "Right", "Down", "Left", "LT", "RT" };
    return b >= 0 && b < COUNT ? N[b] : "?";
}

enum class Layer : uint8_t { Both, Combat, Quick, Explore };

struct Map { Button button; Layer layer; InputAction action; };

inline const std::vector<Map>& defaults() {
    static const std::vector<Map> M = {
        // both
        { A,     Layer::Both,    InputAction::Jump },
        { L3,    Layer::Both,    InputAction::Crouch },
        { R3,    Layer::Both,    InputAction::LockOn },
        { Back,  Layer::Both,    InputAction::DrawWeapon },
        // combat
        { LB,    Layer::Combat,  InputAction::Sprint },
        { LB,    Layer::Combat,  InputAction::Roll },
        { RB,    Layer::Combat,  InputAction::UseLeft },
        { RT,    Layer::Combat,  InputAction::UseRight },
        { LT,    Layer::Combat,  InputAction::Block },
        { X,     Layer::Combat,  InputAction::UseSlotX },
        { Y,     Layer::Combat,  InputAction::UseSlotY },
        { B,     Layer::Combat,  InputAction::UseSlotB },
        { Left,  Layer::Combat,  InputAction::CycleSlotX },
        { Up,    Layer::Combat,  InputAction::CycleSlotY },
        { Right, Layer::Combat,  InputAction::CycleSlotB },
        { Down,  Layer::Combat,  InputAction::SwapWeapon },
        // quick: Start held in combat (the hotbar number actions,
        // read with QuickLayer held: game/CombatKit.h)
        { Start, Layer::Quick,   InputAction::QuickLayer },
        { Left,  Layer::Quick,   InputAction::ToolbarSlot1 },
        { Up,    Layer::Quick,   InputAction::ToolbarSlot2 },
        { Right, Layer::Quick,   InputAction::ToolbarSlot3 },
        { Down,  Layer::Quick,   InputAction::ToolbarSlot4 },
        { X,     Layer::Quick,   InputAction::ToolbarSlot5 },
        { Y,     Layer::Quick,   InputAction::ToolbarSlot6 },
        { B,     Layer::Quick,   InputAction::ToolbarSlot7 },
        { A,     Layer::Quick,   InputAction::ToolbarSlot8 },
        // explore
        { LB,    Layer::Explore, InputAction::ToolbarPrev },
        { RB,    Layer::Explore, InputAction::ToolbarNext },
        { LT,    Layer::Explore, InputAction::UseLeft },
        { RT,    Layer::Explore, InputAction::UseRight },
        { B,     Layer::Explore, InputAction::Sprint },
        { B,     Layer::Explore, InputAction::Roll },
        { Y,     Layer::Explore, InputAction::Inventory },
        { X,     Layer::Explore, InputAction::RotatePiece },
        { Up,    Layer::Explore, InputAction::NudgeForward },
        { Down,  Layer::Explore, InputAction::NudgeBack },
        { Left,  Layer::Explore, InputAction::NudgeLeft },
        { Right, Layer::Explore, InputAction::NudgeRight },
        { Start, Layer::Explore, InputAction::Pause },
    };
    return M;
}
// The layout in use (defaults, then pad.toml / the Controls screen)
inline std::vector<Map>& current() { static std::vector<Map> m = defaults(); return m; }
inline const std::vector<Map>& layout() { return current(); }
inline void resetLayout() { current() = defaults(); }

inline const char* layerKey(Layer l) {
    switch (l) { case Layer::Both: return "both"; case Layer::Combat: return "combat"; case Layer::Quick: return "quick"; default: return "explore"; }
}
inline int buttonFromName(const std::string& n) {
    for (int b = 0; b < COUNT; b++) if (n == buttonName(b)) return b;
    if (n == "Back") return Back;
    return -1;
}
// The button an action is on in a layer (the first), or -1
inline int buttonOf(InputAction a, Layer l) {
    for (const Map& m : layout()) if (m.action == a && m.layer == l) return m.button;
    return -1;
}
// The jump button (a tap jumps, a hold interacts) and the one that
// opens the quick layer (held, in combat)
inline Button jumpButton() { int b = buttonOf(InputAction::Jump, Layer::Both); return b >= 0 ? (Button)b : A; }
inline Button quickButton() { int b = buttonOf(InputAction::QuickLayer, Layer::Quick); return b >= 0 ? (Button)b : Start; }
// Two actions meant to share a button in a layer (one press, both:
// sprint held / roll tapped)
inline bool mayShare(InputAction x, InputAction y) {
    auto pair = [&](InputAction a, InputAction b) { return (x == a && y == b) || (x == b && y == a); };
    return pair(InputAction::Sprint, InputAction::Roll);
}

constexpr float DEADZONE     = 0.18f;
constexpr float TRIGGER_DOWN = 0.35f;   // a trigger past this counts as pressed
constexpr float HOLD_A       = 0.40f;   // s: A held this long interacts (once)

// One controller, read each frame
struct State {
    bool connected = false;
    std::array<bool, COUNT> down{};
    glm::vec2 left{ 0.0f }, right{ 0.0f };   // sticks, deadzoned (y down = forward is negative)
    bool any = false;                       // something pressed or a stick moved this frame
};

inline float dead(float v) {
    float a = std::fabs(v);
    if (a < DEADZONE) return 0.0f;
    return std::copysign((a - DEADZONE) / (1.0f - DEADZONE), v);
}

// Which layer the buttons are in now
inline Layer layerFor(bool combat, bool startHeld) {
    if (!combat) return Layer::Explore;
    return startHeld ? Layer::Quick : Layer::Combat;
}
inline bool inLayer(Layer m, Layer now) {
    if (m == Layer::Both) return now != Layer::Quick;     // the quick layer takes the face buttons for itself
    return m == now;
}

// Set the actions the pad holds this frame (ORed into the
// keyboard's).
//   aHeld  s A has been held so far (or was, if it just came up)
//   aTap   A just came up after less than HOLD_A: a jump
// A is a tap (jump, on release) or a hold (interact, once held
// HOLD_A), so a jump waits for the button to come up.
//   (aHeld / aTap: the jump button's, wherever it is: jumpButton())
inline void apply(const State& s, Layer now, float aHeld, bool aTap, std::array<bool, (int)InputAction::COUNT>& held) {
    if (!s.connected) return;
    const Button jb = jumpButton();
    for (const Map& m : layout()) {
        if (m.action == InputAction::Jump) continue;     // below
        if (inLayer(m.layer, now) && s.down[m.button]) held[(int)m.action] = true;
    }
    // the quick layer: its button itself is held too
    if (now == Layer::Quick) held[(int)InputAction::QuickLayer] = true;
    if (now != Layer::Quick) {
        if (aTap) held[(int)InputAction::Jump] = true;
        if (s.down[jb] && aHeld >= HOLD_A) held[(int)InputAction::Interact] = true;
    }
}

// For the debug overlay: is this pad button "pressed", by the pad
// or by whatever keyboard key drives the same action in this layer
inline bool lit(const State& s, Button b, Layer now, const std::array<bool, (int)InputAction::COUNT>& held) {
    if (s.connected && s.down[b]) return true;
    for (const Map& m : layout())
        if (m.button == b && inLayer(m.layer, now) && held[(int)m.action]) return true;
    return false;
}

} // namespace pad
