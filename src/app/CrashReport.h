#pragma once
#include <string>

// ══════════════════════════════════════════════════════════
// CrashReport — what's left behind when the program dies
// ══════════════════════════════════════════════════════════
// crash::install() (first thing in main) catches a crash (an access
// violation, a divide by zero, an abort, an exception nobody caught)
// and writes logs/crash-<date>-<time>.txt before the program goes:
//   - what happened (the exception or signal) and where (the module
//     and offset, which a matching .pdb turns into a line of code)
//   - the call stack, named where symbols are found
//   - the last lines the program printed (app/Log.h)
//   - the build (version.txt, if there is one)
// On Windows also logs/crash-<…>.dmp (a minidump: open it in Visual
// Studio with the build's .pdb next to the .exe to see the crash as in
// the debugger; ignored by git, too big) and a message box saying where
// the report went. The .txt goes in logs/ and is pushed with the rest.
//
// Windows code lives in CrashReport.cpp (never windows.h in a header).
namespace crash {

// 'program' names it in the report ("VoxelRPG", "VoxelRPG-Server");
// 'showBox': a message box on Windows (the game; not the server)
void install(const std::string& program, bool showBox);

}
