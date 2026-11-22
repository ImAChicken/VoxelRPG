// Crash reports (app/CrashReport.h). The only file with the platform's
// crash APIs: windows.h and dbghelp here, signals and execinfo on Linux.
#include "CrashReport.h"
#include "Log.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#if defined(__GLIBC__)
#include <execinfo.h>
#endif
#include <unistd.h>
#endif

namespace crash {
namespace {

std::string gProgram = "VoxelRPG";
bool gBox = false;
volatile bool gWriting = false;        // a crash inside the handler: don't loop

std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d-%H%M%S", &tm);
    return b;
}

std::string build() {
    std::ifstream f("version.txt");
    if (!f) return "(no version.txt: a developer build)\n";
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The report's text around the platform's part (what, where, stack)
void write(const std::string& base, const std::string& what, const std::string& stack) {
    std::error_code ec;
    std::filesystem::create_directories(applog::DIR, ec);
    std::ofstream f(base + ".txt");
    f << gProgram << " crashed\n";
    f << "when:  " << stamp() << "\n";
    f << "what:  " << what << "\n\n";
    f << "build:\n" << build() << "\n";
    f << "stack (newest first):\n" << stack << "\n";
    f << "last lines printed (" << applog::path() << "):\n" << applog::lastLines() << "\n";
    f.flush();
    std::cerr << "\n*** " << gProgram << " crashed: " << what << "\n*** report: " << base << ".txt\n";
    std::cerr.flush();
}

#ifdef _WIN32
// ── Windows ──────────────────────────────────────────────
const char* codeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
    case 0xE06D7363:                      return "C++ exception";
    default:                              return "exception";
    }
}

// "module+0xoffset (function)" for an address
std::string where(HANDLE proc, DWORD64 addr) {
    std::ostringstream s;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod) {
        GetModuleFileNameA(mod, name, MAX_PATH);
        const char* slash = std::strrchr(name, '\\');
        s << (slash ? slash + 1 : name) << "+0x" << std::hex << (addr - (DWORD64)mod) << std::dec;
    } else {
        s << "0x" << std::hex << addr << std::dec;
    }
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    if (SymFromAddr(proc, addr, &disp, sym)) {
        s << "  " << sym->Name;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        if (SymGetLineFromAddr64(proc, addr, &ldisp, &line)) {
            const char* f = std::strrchr(line.FileName, '\\');
            s << "  " << (f ? f + 1 : line.FileName) << ":" << line.LineNumber;
        }
    }
    return s.str();
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    if (gWriting) return EXCEPTION_CONTINUE_SEARCH;
    gWriting = true;
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    const std::string base = std::string(applog::DIR) + "/crash-" + stamp();

    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    std::ostringstream what;
    what << codeName(code) << " (0x" << std::hex << code << std::dec << ")";
    if (ep && ep->ExceptionRecord) {
        what << " at " << where(proc, (DWORD64)ep->ExceptionRecord->ExceptionAddress);
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
            what << (ep->ExceptionRecord->ExceptionInformation[0] ? ", writing" : ", reading") << " address 0x"
                 << std::hex << ep->ExceptionRecord->ExceptionInformation[1] << std::dec;
    }

    // the stack, from the crash's own registers
    std::ostringstream stack;
    if (ep && ep->ContextRecord) {
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 fr{};
        DWORD machine;
#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        fr.AddrPC.Offset = ctx.Rip; fr.AddrFrame.Offset = ctx.Rbp; fr.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
        machine = IMAGE_FILE_MACHINE_ARM64;
        fr.AddrPC.Offset = ctx.Pc; fr.AddrFrame.Offset = ctx.Fp; fr.AddrStack.Offset = ctx.Sp;
#else
        machine = IMAGE_FILE_MACHINE_I386;
        fr.AddrPC.Offset = ctx.Eip; fr.AddrFrame.Offset = ctx.Ebp; fr.AddrStack.Offset = ctx.Esp;
#endif
        fr.AddrPC.Mode = fr.AddrFrame.Mode = fr.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 48; i++) {
            if (!StackWalk64(machine, proc, GetCurrentThread(), &fr, &ctx, nullptr, SymFunctionTableAccess64,
                             SymGetModuleBase64, nullptr) || !fr.AddrPC.Offset)
                break;
            stack << "  " << i << "  " << where(proc, fr.AddrPC.Offset) << "\n";
        }
    }
    write(base, what.str(), stack.str());

    // a minidump for the debugger
    HANDLE f = CreateFileA((base + ".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(proc, GetCurrentProcessId(), f,
                          (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(f);
    }
    if (gBox) {
        std::string msg = gProgram + " crashed. A report was saved to\n" + base +
                          ".txt\n\nPlease send it (and what you were doing) to whoever gave you the game.";
        MessageBoxA(nullptr, msg.c_str(), (gProgram + " crashed").c_str(), MB_OK | MB_ICONERROR);
    }
    return EXCEPTION_EXECUTE_HANDLER;      // end the program
}

void onAbort(int) {
    // abort() (a failed assert, std::terminate): no exception record, so raise one to get a stack
    RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}
#else
// ── Linux / others ───────────────────────────────────────
void onSignal(int sig) {
    if (gWriting) { std::_Exit(128 + sig); }
    gWriting = true;
    const std::string base = std::string(applog::DIR) + "/crash-" + stamp();
    std::ostringstream stack;
#if defined(__GLIBC__)
    void* frames[64];
    int n = backtrace(frames, 64);
    char** names = backtrace_symbols(frames, n);
    for (int i = 0; i < n; i++) stack << "  " << i << "  " << (names ? names[i] : "?") << "\n";
#endif
    const char* what = sig == SIGSEGV ? "segmentation fault (SIGSEGV)" : sig == SIGABRT ? "abort (SIGABRT)" :
                       sig == SIGFPE ? "arithmetic error (SIGFPE)" : sig == SIGILL ? "illegal instruction (SIGILL)" :
                       sig == SIGBUS ? "bus error (SIGBUS)" : "signal";
    write(base, what, stack.str());
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

// An exception nobody caught: say what it was, then crash as usual
void onTerminate() {
    std::string what = "uncaught exception";
    if (std::exception_ptr e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& x) { what += ": " + std::string(x.what()); }
        catch (...) { what += " (not a std::exception)"; }
    }
    std::cerr << "\n*** " << what << "\n";
    std::cerr.flush();
    std::abort();
}

} // namespace

void install(const std::string& program, bool showBox) {
    gProgram = program;
    gBox = showBox;
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onCrash);
    std::signal(SIGABRT, onAbort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // no "abort() was called" box first
#else
    for (int s : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) std::signal(s, onSignal);
#endif
}

} // namespace crash
