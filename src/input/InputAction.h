#pragma once

// Master list of all player actions
// Never use GLFW key codes directly in gameplay code
// Always go through InputManager::isHeld / isPressed
// Rebind any action by editing data/controls/keyboard.toml

enum class InputAction {

    // ── Movement ─────────────────────────────────────────
    MoveForward,
    MoveBackward,
    StrafeLeft,
    StrafeRight,
    Jump,
    Crouch,
    Sprint,
    Roll,           // ground dodge / aerial dodge

    // ── Combat actions ───────────────────────────────────
    UseLeft,        // primary attack / place block
    UseRight,       // secondary attack / remove block
    Block,          // hold = guard, tap = timed block
    DrawWeapon,     // toggle draw/sheathe
                    // switches survival ↔ combat mode

    // ── Interaction ──────────────────────────────────────
    Interact,       // use/open/talk
    LedgeGrab,      // burst to face, hang, second press = lift

    // ── Toolbar ──────────────────────────────────────────
    Inventory,      // open full inventory
    ToolbarNext,    // scroll down / next slot
    ToolbarPrev,    // scroll up / prev slot
    ToolbarSlot1,
    ToolbarSlot2,
    ToolbarSlot3,
    ToolbarSlot4,
    ToolbarSlot5,
    ToolbarSlot6,
    ToolbarSlot7,
    ToolbarSlot8,
    ToolbarSlot9,
    ToolbarSlot10,
    SwapWeapon,     // Weapon 1 <-> Weapon 2

    // ── Camera ───────────────────────────────────────────
    CameraToggle,   // first/third person toggle F5

    // ── Dev mode ─────────────────────────────────────────
    ToggleFly,      // V — fly on/off in dev mode
    ToggleDebug,    // F3 — debug overlay
    ToggleConsole,  // ` — dev console
    GamemodeCycle,  // F4 — cycle gamemodes
    ReloadRegistry, // F6 — hot reload blocks.toml
    ReloadPack,     // F7 — hot reload resource pack
    PickBlock,      // middle click — copy the block you look at
    Undo,           // Z — undo the last dev edit
    Redo,           // Y — redo
    BrushBigger,    // = — dev brush / explosion radius up
    BrushSmaller,   // - — dev brush / explosion radius down
    NudgeForward,   // ↑ — move the paste away / tool size up
    NudgeBack,      // ↓ — move the paste closer / tool size down
    NudgeLeft,      // ← — move the paste left
    NudgeRight,     // → — move the paste right
    NudgeUp,        // Page Up — move the paste up
    NudgeDown,      // Page Down — move the paste down
    ToolCancel,     // Backspace — release a locked paste / cancel a prompt
    RotatePiece,    // T — turn the hammer's structure a quarter turn

    // ── System ───────────────────────────────────────────
    Pause,
    Screenshot,

    // Always last — used for array sizing
    COUNT
};
