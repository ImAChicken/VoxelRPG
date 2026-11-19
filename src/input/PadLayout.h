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
//   both      left stick move, right stick camera, A jump, L3
//             crouch / sneak, R3 lock on
//             (right stick left / right while locked: next
//             target), Select (tapped): combat mode on / off,
//             Select + Start together: the inventory (its left
//             panel has the pause menu's buttons)
//   combat    LB sprint (hold) / dodge (tap), RB light, RT heavy,
//             LT special (the off hand's, or the weapon's: guard),
//             X / Y / B use the item in that slot, D-pad left /
//             up / right the next item in that slot, down the next
//             weapon set. No menus: Select out of combat first.
//   quick     Start held, in combat or out: D-pad and X / Y / B /
//             A pick one of the 8 quick options (the same for
//             every weapon set), so the slotted spells work
//             without drawing a weapon; LB interacts (talk, open,
//             pick up: E on the keyboard)
//   explore   out of combat: LB / RB the hotbar, LT / RT like the
//             left / right mouse buttons, B sprint / roll, X the
//             held tool's option (turn the hammer's piece, the
//             hoe's mode). The inventory and the pause menu's
//             buttons: Select + Start
//   tool      a tool that needs more buttons is in hand (the
//             hammer; dev tools): explore, but B lets go / cancels,
//             the D-pad moves the piece (or sizes the tool), and Y
//             held turns the D-pad's up / down into up / down
//             (tool alt). Each layer only overrides the buttons it
//             uses: the rest come from the one under it (chain())
//
// The table below is the default layout. data/controls/pad.toml
// (InputManager::loadPad) rebinds any of it, and so does the
// Controls screen's Controller tab. The button that opens the
// quick layer is wherever QuickLayer is. The controller
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

enum class Layer : uint8_t { Both, Combat, Quick, Explore, Tool, ToolAlt };
inline const std::vector<Layer>& allLayers() {
    static const std::vector<Layer> L = { Layer::Both, Layer::Combat, Layer::Quick, Layer::Explore, Layer::Tool, Layer::ToolAlt };
    return L;
}

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
        // quick: Start held (the hotbar number actions,
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
        { LB,    Layer::Quick,   InputAction::Interact },
        // explore
        { LB,    Layer::Explore, InputAction::ToolbarPrev },
        { RB,    Layer::Explore, InputAction::ToolbarNext },
        { LT,    Layer::Explore, InputAction::UseLeft },
        { RT,    Layer::Explore, InputAction::UseRight },
        { B,     Layer::Explore, InputAction::Sprint },
        { B,     Layer::Explore, InputAction::Roll },
        { X,     Layer::Explore, InputAction::RotatePiece },
        // tool: over explore (a hammer or dev tool in hand)
        { B,     Layer::Tool,    InputAction::ToolCancel },
        { Y,     Layer::Tool,    InputAction::ToolAlt },
        { Up,    Layer::Tool,    InputAction::NudgeForward },
        { Down,  Layer::Tool,    InputAction::NudgeBack },
        { Left,  Layer::Tool,    InputAction::NudgeLeft },
        { Right, Layer::Tool,    InputAction::NudgeRight },
        // tool alt: over tool, while its Y is held
        { Up,    Layer::ToolAlt, InputAction::NudgeUp },
        { Down,  Layer::ToolAlt, InputAction::NudgeDown },
    };
    return M;
}
// The layout in use (defaults, then pad.toml / the Controls screen)
inline std::vector<Map>& current() { static std::vector<Map> m = defaults(); return m; }
inline const std::vector<Map>& layout() { return current(); }
inline void resetLayout() { current() = defaults(); }

inline const char* layerKey(Layer l) {
    switch (l) {
    case Layer::Both: return "both"; case Layer::Combat: return "combat"; case Layer::Quick: return "quick";
    case Layer::Tool: return "tool"; case Layer::ToolAlt: return "tool_alt"; default: return "explore";
    }
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
// The jump button and the one that opens the quick layer (held)
inline Button jumpButton() { int b = buttonOf(InputAction::Jump, Layer::Both); return b >= 0 ? (Button)b : A; }
inline Button quickButton() { int b = buttonOf(InputAction::QuickLayer, Layer::Quick); return b >= 0 ? (Button)b : Start; }
// The tool alt button (Y unless rebound): held with a tool, the tool alt layer
inline Button toolAltButton() { int b = buttonOf(InputAction::ToolAlt, Layer::Tool); return b >= 0 ? (Button)b : Y; }
// Two actions meant to share a button in a layer (one press, both:
// sprint held / roll tapped)
inline bool mayShare(InputAction x, InputAction y) {
    auto pair = [&](InputAction a, InputAction b) { return (x == a && y == b) || (x == b && y == a); };
    return pair(InputAction::Sprint, InputAction::Roll);
}

constexpr float DEADZONE     = 0.18f;
constexpr float TRIGGER_DOWN = 0.35f;   // a trigger past this counts as pressed

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
//   tool     a tool that takes the pad is in hand (out of combat)
//   altHeld  the tool alt button (Y) is down
inline Layer layerFor(bool combat, bool startHeld, bool tool = false, bool altHeld = false) {
    if (startHeld) return Layer::Quick;
    if (combat) return Layer::Combat;
    if (tool) return altHeld ? Layer::ToolAlt : Layer::Tool;
    return Layer::Explore;
}
// The layers in force, top first: a button means what the first of
// them to use it says (the quick layer takes every button)
inline const std::vector<Layer>& chain(Layer now) {
    static const std::vector<Layer> Q = { Layer::Quick }, C = { Layer::Combat, Layer::Both },
        E = { Layer::Explore, Layer::Both }, T = { Layer::Tool, Layer::Explore, Layer::Both },
        TA = { Layer::ToolAlt, Layer::Tool, Layer::Explore, Layer::Both };
    switch (now) {
    case Layer::Quick: return Q; case Layer::Combat: return C; case Layer::Tool: return T;
    case Layer::ToolAlt: return TA; default: return E;
    }
}
// Does an entry of layer m, on button b, count now (not covered by
// a layer above it in the chain)
inline bool inLayer(Layer m, Layer now, int b = -1) {
    if (m == Layer::Both) return now != Layer::Quick;     // always (a clash with it is flagged, not covered)
    const std::vector<Layer>& ch = chain(now);
    size_t at = 0;
    while (at < ch.size() && ch[at] != m) at++;
    if (at == ch.size()) return false;
    if (b >= 0)
        for (size_t j = 0; j < at; j++)
            for (const Map& o : layout()) if (o.layer == ch[j] && o.button == b) return false;
    return true;
}

// Set the actions the pad holds this frame (ORed into the
// keyboard's)
inline void apply(const State& s, Layer now, std::array<bool, (int)InputAction::COUNT>& held) {
    if (!s.connected) return;
    for (const Map& m : layout()) {
        if (m.action == InputAction::DrawWeapon) continue;   // a tap: chord()
        if (s.down[m.button] && inLayer(m.layer, now, m.button)) held[(int)m.action] = true;
    }
    // the quick layer: its button itself is held too
    if (now == Layer::Quick) held[(int)InputAction::QuickLayer] = true;
}

// The draw button (Select unless rebound): a TAP draws / sheathes,
// on release, so it can also be half of a chord. Held together
// with the quick button (Start) it's the inventory instead, and
// then its release draws nothing. Either can go down first.
struct ChordState { bool selWas = false, chorded = false; };
inline Button drawButton() {
    int b = buttonOf(InputAction::DrawWeapon, Layer::Both);
    if (b < 0) for (const Map& m : layout()) if (m.action == InputAction::DrawWeapon) { b = m.button; break; }
    return b >= 0 ? (Button)b : Back;
}
inline void chord(const State& s, ChordState& c, std::array<bool, (int)InputAction::COUNT>& held) {
    if (!s.connected) { c = ChordState(); return; }
    const bool same = drawButton() == quickButton();     // rebound onto one button: no chord, just the tap
    bool sel = s.down[drawButton()], q = !same && s.down[quickButton()];
    if (sel && q) { c.chorded = true; held[(int)InputAction::Inventory] = true; }
    if (!sel && c.selWas && !c.chorded) held[(int)InputAction::DrawWeapon] = true;
    if (!sel) c.chorded = false;
    c.selWas = sel;
}

// For the debug overlay: is this pad button "pressed", by the pad
// or by whatever keyboard key drives the same action in this layer
inline bool lit(const State& s, Button b, Layer now, const std::array<bool, (int)InputAction::COUNT>& held) {
    if (s.connected && s.down[b]) return true;
    for (const Map& m : layout())
        if (m.button == b && inLayer(m.layer, now, b) && held[(int)m.action]) return true;
    return false;
}

} // namespace pad
