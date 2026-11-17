#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

// ══════════════════════════════════════════════════════════
// SaveStaging — saves that can't hurt the world on disk
// ══════════════════════════════════════════════════════════
// A running world never writes over its own files. Every save
// goes into a staging folder beside them:
//
//   <world>/.staging/          the changed files, as they'll be
//
// and only an intentional save COMMITS it: the server shutting
// down with a save, a forced save (the save command), or (in
// single player, which trusts itself) every save. Committing:
//
//   1. write a COMMIT marker into .staging (the set is complete)
//   2. move each committed file it replaces into .backup-1
//      (older backups shift to .backup-2, .backup-3; the oldest
//      goes)
//   3. move each staged file into place
//   4. delete .staging
//
// A crash anywhere in 2-4 is finished on the next open (the
// marker says the set was complete). A crash before 1 — or a
// hostile session, or one that never saved — leaves the world
// exactly as last committed; its unfinished staging is moved
// aside to .staging-abandoned-<time> for anyone who wants it.
//
// While a world runs, reading a file prefers the staged copy
// (readPath): a chunk saved at a checkpoint and then unloaded
// comes back as saved, not as last committed.
namespace savestage {

namespace fs = std::filesystem;

constexpr const char* STAGE  = ".staging";
constexpr const char* MARKER = "COMMIT";
constexpr int BACKUPS = 3;

inline fs::path stageDir(const std::string& world) { return fs::path(world) / STAGE; }

// Where a world file should be written now (inside .staging),
// creating the folders on the way. 'rel' is relative to the world.
inline fs::path writePath(const std::string& world, const fs::path& rel) {
    fs::path p = stageDir(world) / rel;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return p;
}
// Where to read a world file: the staged copy if there is one
inline fs::path readPath(const std::string& world, const fs::path& rel) {
    fs::path s = stageDir(world) / rel;
    std::error_code ec;
    if (fs::exists(s, ec)) return s;
    return fs::path(world) / rel;
}
inline bool hasStaged(const std::string& world) {
    std::error_code ec;
    return fs::exists(stageDir(world), ec);
}

// Every file under the staging folder (relative paths), but not the marker
inline std::vector<fs::path> stagedFiles(const std::string& world) {
    std::vector<fs::path> out;
    std::error_code ec;
    fs::path root = stageDir(world);
    if (!fs::exists(root, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        fs::path rel = fs::relative(it->path(), root, ec);
        if (rel == MARKER) continue;
        out.push_back(rel);
    }
    return out;
}

// Rename, replacing what's there (some systems refuse to)
inline bool replaceFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec) { fs::remove(to, ec); fs::rename(from, to, ec); }
    if (ec) { fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec); if (!ec) fs::remove(from, ec); }
    return !ec;
}

// Steps 2-4: the set is marked complete; put it in place
inline bool applyCommitted(const std::string& world) {
    std::error_code ec;
    fs::path root = stageDir(world);
    std::vector<fs::path> files = stagedFiles(world);
    // the newest backup is .backup-1; it holds the files this commit replaces
    fs::path b1 = fs::path(world) / ".backup-1";
    if (!fs::exists(root / "BACKED_UP", ec)) {
        fs::remove_all(fs::path(world) / (".backup-" + std::to_string(BACKUPS)), ec);
        for (int i = BACKUPS - 1; i >= 1; i--) {
            fs::path from = fs::path(world) / (".backup-" + std::to_string(i));
            if (fs::exists(from, ec)) fs::rename(from, fs::path(world) / (".backup-" + std::to_string(i + 1)), ec);
        }
        for (const fs::path& rel : files) {
            fs::path live = fs::path(world) / rel;
            if (!fs::exists(live, ec)) continue;
            fs::create_directories((b1 / rel).parent_path(), ec);
            fs::copy_file(live, b1 / rel, fs::copy_options::overwrite_existing, ec);
        }
        std::ofstream(root / "BACKED_UP") << "1\n";
    }
    bool ok = true;
    for (const fs::path& rel : files) ok = replaceFile(root / rel, fs::path(world) / rel) && ok;
    if (ok) fs::remove_all(root, ec);
    return ok;
}

// Commit what's staged (step 1, then the rest). Nothing staged:
// nothing to do.
inline bool commit(const std::string& world) {
    if (!hasStaged(world)) return true;
    { std::ofstream m(stageDir(world) / MARKER); m << "complete\n"; if (!m) return false; }
    bool ok = applyCommitted(world);
    std::cout << (ok ? "Save committed: " : "Save commit FAILED (will finish on next open): ") << world << "\n";
    return ok;
}

// On opening a world: finish a commit a crash interrupted, or set
// aside a staging set that was never committed. Returns what it did.
inline std::string recover(const std::string& world) {
    if (!hasStaged(world)) return "";
    std::error_code ec;
    if (fs::exists(stageDir(world) / MARKER, ec)) {
        applyCommitted(world);
        return "finished a save that was interrupted";
    }
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    fs::path to = fs::path(world) / (std::string(".staging-abandoned-") + stamp);
    fs::rename(stageDir(world), to, ec);
    return "an uncommitted save was set aside (" + to.filename().string() + "); the world is as last committed";
}

} // namespace savestage
