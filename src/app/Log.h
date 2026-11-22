#pragma once
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>

// ══════════════════════════════════════════════════════════
// Log — everything printed also goes to a file
// ══════════════════════════════════════════════════════════
// The game and the server print to their console window. That window
// is gone when the program crashes or closes, and testers can't send
// what they never saw, so applog::start() copies std::cout and
// std::cerr into logs/ in the folder the program runs from (the repo
// root on Eddie's machine, the unzipped folder for testers):
// logs/game.txt for the game, logs/server.txt for the server, line by
// line, flushed as it goes (a crash keeps everything up to it). The
// run before is kept as game-previous.txt / server-previous.txt.
// Crash reports go there too (app/CrashReport.h).
//
// logs/ is tracked by git on purpose (Eddie, 2026-11-21: "make sure
// logs on my machine get pushed too so agents can see them"): agents
// read logs/ after he pushes. Only the crash dumps (*.dmp, binary and
// large) are ignored. The last lines are also kept in memory for the
// crash report.
namespace applog {

class Tee : public std::streambuf {
public:
    Tee(std::streambuf* console, std::ofstream* file, std::mutex* lock)
        : console(console), file(file), lock(lock) {}
protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        std::lock_guard<std::mutex> g(*lock);
        char ch = (char)c;
        if (console) console->sputc(ch);
        if (file && file->is_open()) {
            file->put(ch);
            if (ch == '\n') file->flush();
        }
        line += ch;
        if (ch == '\n' || line.size() > 400) keep();
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; i++) overflow((unsigned char)s[i]);
        return n;
    }
    int sync() override {
        std::lock_guard<std::mutex> g(*lock);
        if (console) console->pubsync();
        if (file && file->is_open()) file->flush();
        return 0;
    }
private:
    std::streambuf* console;
    std::ofstream* file;
    std::mutex* lock;
    std::string line;
    void keep();
};

struct State {
    std::ofstream file;
    std::mutex lock;
    std::deque<std::string> last;      // the last lines, for a crash report
    std::string path;
    std::streambuf* oldOut = nullptr;
    std::streambuf* oldErr = nullptr;
    Tee* out = nullptr;
    Tee* err = nullptr;
};
// Never destroyed: std::cout still points here while the program
// exits (static destructors, the final flush), so it must outlive them
inline State& state() { static State* s = new State(); return *s; }

inline void Tee::keep() {
    auto& q = state().last;
    q.push_back(line);
    if (q.size() > 60) q.pop_front();
    line.clear();
}

constexpr const char* DIR = "logs";

// Start copying std::cout / std::cerr into logs/<name> (e.g. "game.txt")
inline void start(const std::string& name) {
    State& s = state();
    if (s.out) return;
    std::error_code ec;
    std::filesystem::create_directories(DIR, ec);
    std::filesystem::path p = std::filesystem::path(DIR) / name;
    std::filesystem::path prev = p.parent_path() / (p.stem().string() + "-previous" + p.extension().string());
    if (std::filesystem::exists(p, ec)) {
        std::filesystem::remove(prev, ec);
        std::filesystem::rename(p, prev, ec);
    }
    s.file.open(p, std::ios::out | std::ios::trunc);
    s.path = p.string();
    s.oldOut = std::cout.rdbuf();
    s.oldErr = std::cerr.rdbuf();
    s.out = new Tee(s.oldOut, &s.file, &s.lock);
    s.err = new Tee(s.oldErr, &s.file, &s.lock);
    std::cout.rdbuf(s.out);
    std::cerr.rdbuf(s.err);
}

// The last lines printed (newest last), for the crash report
inline std::string lastLines() {
    State& s = state();
    std::string out;
    for (const std::string& l : s.last) out += l;
    return out;
}
inline const std::string& path() { return state().path; }

} // namespace applog
