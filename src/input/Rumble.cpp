// The pad's motors on Windows: XInput, loaded at run time (xinput1_4,
// else 1_3, else 9_1_0), so nothing links against it and a machine
// without it just doesn't shake. Kept out of the headers: windows.h's
// macros (near, far, small …) would break code included after it.
#include "input/Rumble.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

struct Vibration { WORD left, right; };              // XINPUT_VIBRATION
typedef DWORD (WINAPI* SetFn)(DWORD, Vibration*);

void setMotors(float lo, float hi) {
    static SetFn setState = []() -> SetFn {
        for (const char* dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" })
            if (HMODULE m = LoadLibraryA(dll))
                if (auto f = (SetFn)(void*)GetProcAddress(m, "XInputSetState")) return f;
        return nullptr;
    }();
    if (!setState) return;
    auto q = [](float v) { return (WORD)std::clamp(v * 65535.0f, 0.0f, 65535.0f); };
    Vibration v{ q(lo), q(hi) };
    // every connected pad (XInput's user index doesn't match GLFW's joystick number)
    for (DWORD i = 0; i < 4; i++) setState(i, &v);
}

// hooked up when the game starts
const bool hooked = (rumble::gMotors = &setMotors, true);

} // namespace
#endif
