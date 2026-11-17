#pragma once
#include <algorithm>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "../chunk/Chunk.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/ChunkLoader.h"
#include "../game/GameMode.h"
#include "../worldgen/WorldType.h"
#include <functional>
#include <ostream>

// Worlds live in one of two folders:
//   saves/       ordinary worlds; saved every few minutes
//                (Settings → Auto save) and on leaving
//   devworlds/   dev worlds: stages for showcases and tests
//                (enemies, items, spawners). Never saved by
//                themselves: Esc → Save keeps what's there,
//                Quit without saving throws the visit away.
//                They can be copied into new worlds (New World
//                → Start: Copy a dev world).
class WorldSaveManager {
public:
    static const char* rootFor(bool dev) { return dev ? "devworlds" : "saves"; }

    // The worlds in a folder (folders holding a world.vxrp), sorted
    static std::vector<std::string> list(bool dev) {
        std::vector<std::string> out;
        std::error_code ec;
        if (!std::filesystem::exists(rootFor(dev), ec)) return out;
        for (auto& e : std::filesystem::directory_iterator(rootFor(dev), ec))
            if (e.is_directory() && std::filesystem::exists(e.path() / "world.vxrp"))
                out.push_back(e.path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }
    // 'name', or 'name 2', 'name 3' … if that's taken in the folder
    static std::string uniqueName(bool dev, const std::string& name) {
        std::string n = name;
        for (int i = 2; std::filesystem::exists(std::string(rootFor(dev)) + "/" + n); i++)
            n = name + " " + std::to_string(i);
        return n;
    }
    // A whole world copied to a new one (another folder or name):
    // its chunks, structures, NPCs, player and all. Returns false
    // with a reason if it can't.
    static bool copyWorld(bool srcDev, const std::string& src, bool dstDev, const std::string& dst, std::string& why) {
        std::filesystem::path from = std::string(rootFor(srcDev)) + "/" + src;
        std::filesystem::path to   = std::string(rootFor(dstDev)) + "/" + dst;
        std::error_code ec;
        if (!std::filesystem::exists(from / "world.vxrp")) { why = "no world '" + src + "'"; return false; }
        if (std::filesystem::exists(to)) { why = "'" + dst + "' already exists"; return false; }
        std::filesystem::create_directories(to.parent_path(), ec);
        std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
        if (ec) { why = ec.message(); return false; }
        // its own name in its descriptor
        std::ifstream in(to / "world.vxrp");
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);) lines.push_back(l.rfind("name=", 0) == 0 ? "name=" + dst : l);
        in.close();
        std::ofstream out(to / "world.vxrp");
        for (auto& l : lines) out << l << "\n";
        return true;
    }

    bool isDevWorld() const { return worldDev; }

    // Start a brand new world. If a save with this name
    // already exists its chunk files are removed — they
    // belong to the old world and would otherwise be loaded
    // into the new one.
    //   mode — the game mode players start in whenever this
    //          world is opened
    //   type, size — world type and its size in chunks
    bool createWorld(const std::string& name, uint64_t seed,
                     GameMode mode, WorldType type, int size, int biome = -1,
                     TerrainStyle terrain = TerrainStyle::GuidedRivers,
                     const WorldGenParams& params = WorldGenParams(),
                     const FlatOptions& flat = FlatOptions(), bool dev = false) {
        worldParams = params;
        worldFlat   = flat;
        worldDev    = dev;
        worldName   = name;
        worldBiome  = biome;
        worldTerrain = terrain;
        worldSeed   = seed;
        defaultMode = mode;
        worldType   = type;
        worldSize   = worldSizeFor(type, size);
        worldPath = std::string(rootFor(dev)) + "/" + name;
        std::error_code ec;
        std::filesystem::path chunkDir = worldPath + "/chunks";
        std::filesystem::create_directories(chunkDir, ec);

        // Remove only chunk files (*.vxrp) — nothing else
        std::vector<std::filesystem::path> stale;
        for (auto& e : std::filesystem::directory_iterator(
                           chunkDir, ec)) {
            if (e.is_regular_file() &&
                e.path().extension() == ".vxrp")
                stale.push_back(e.path());
        }
        for (auto& f : stale) std::filesystem::remove(f, ec);
        // …and the old world's player and structures
        std::filesystem::remove(worldPath + "/player.dat", ec);
        std::filesystem::remove(worldPath + "/structures.dat", ec);

        return saveDescriptor();
    }

    bool loadWorld(const std::string& name, bool dev = false) {
        worldDev  = dev;
        worldPath = std::string(rootFor(dev)) + "/" + name;
        worldName = name;
        // Saves made before worlds had a mode have no
        // "mode=" line and open in Survival
        defaultMode = GameMode::Survival;
        // Saves from before world types are Classic
        worldType = WorldType::Classic;
        worldSize = CLASSIC_WORLD_SIZE;
        worldBiome = -1;
        // worlds from before erosion keep their old ground
        worldTerrain = TerrainStyle::NoErosion;
        worldParams.reset();
        worldFlat = FlatOptions();
        return loadDescriptor();
    }

    // A world's settings from any folder holding its world.vxrp
    // (a client keeps the server's in servercache/<server>/)
    bool loadFolder(const std::string& folder) {
        worldDev = false;
        worldPath = folder;
        worldName = std::filesystem::path(folder).filename().string();
        defaultMode = GameMode::Survival;
        worldType = WorldType::Classic;
        worldSize = CLASSIC_WORLD_SIZE;
        worldBiome = -1;
        worldTerrain = TerrainStyle::NoErosion;
        worldParams.reset();
        worldFlat = FlatOptions();
        return loadDescriptor();
    }

    // Only chunks the player has changed are written. Every
    // other chunk is rebuilt from the seed on load, so a save
    // stays small no matter how much of the world was visited.
    void saveDirtyChunks(ChunkManager& world) {
        int saved = 0;
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            if (!world.isDirty(cx, cz)) continue;
            Chunk* chunk = world.getChunk(cx, cz);
            if (!chunk) continue;
            ChunkLoader::save(*chunk,
                chunkPath(worldPath, cx, cz), cx, 0, cz);
            world.clearDirty(cx, cz);
            saved++;
        }
        if (saved > 0)
            std::cout << "Saved " << saved << " chunks\n";
    }

    // ── Player file ───────────────────────────────────────
    // saves/<world>/player.dat — plain "key=value" lines:
    // position, mode, health, both inventories, dev tool
    // settings. 'write' fills the file; 'read' is handed
    // each key and value. Written to a temporary file first
    // and then renamed, so a crash mid-save cannot leave a
    // half-written player file behind.
    bool savePlayer(const std::function<void(std::ostream&)>& write) const {
        if (worldPath.empty()) return false;
        std::filesystem::path path = worldPath + "/player.dat";
        std::filesystem::path tmp  = worldPath + "/player.dat.tmp";
        {
            std::ofstream f(tmp);
            if (!f) return false;
            write(f);
            if (!f) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {   // some systems refuse to rename over a file
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path, ec);
        }
        return !ec;
    }

    // Returns false if this world has no player file yet
    // (a new world, or one saved before player files existed)
    bool loadPlayer(const std::function<void(const std::string&,
                                             const std::string&)>& read) const {
        std::ifstream f(worldPath + "/player.dat");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            read(line.substr(0, eq), line.substr(eq + 1));
        }
        return true;
    }

    // Folder of the open world, e.g. "saves/myworld" or "devworlds/arena"
    const std::string& getWorldPath() const { return worldPath; }

    // Where a chunk's save file lives. Static and takes the
    // world folder by value so worker threads can call it.
    static std::filesystem::path chunkPath(
            const std::string& worldFolder, int cx, int cz) {
        return worldFolder + "/chunks/c_"
            + std::to_string(cx) + "_0_"
            + std::to_string(cz) + ".vxrp";
    }

    uint64_t    getSeed()  const { return worldSeed; }
    GameMode    getDefaultMode() const { return defaultMode; }
    WorldType   getWorldType() const { return worldType; }
    int         getWorldBiome() const { return worldBiome; }
    TerrainStyle getTerrainStyle() const { return worldTerrain; }
    const WorldGenParams& getGenParams() const { return worldParams; }
    const FlatOptions& getFlatOptions() const { return worldFlat; }
    int         getWorldSize() const { return worldSize; }
    std::string getName()  const { return worldName; }
    bool        isLoaded() const { return !worldPath.empty(); }

private:
    std::string worldPath;
    std::string worldName;
    uint64_t    worldSeed = 42;
    GameMode    defaultMode = GameMode::Survival;
    WorldType   worldType   = WorldType::Classic;
    int         worldSize   = CLASSIC_WORLD_SIZE;
    int         worldBiome  = -1;     // single-biome worlds
    TerrainStyle worldTerrain = TerrainStyle::GuidedRivers;
    WorldGenParams worldParams;
    FlatOptions    worldFlat;          // flat worlds
    bool           worldDev = false;   // in devworlds/

    bool saveDescriptor() {
        std::ofstream f(worldPath + "/world.vxrp");
        if (!f) return false;
        f << "name="    << worldName << "\n";
        f << "seed="    << worldSeed << "\n";
        f << "mode="    << gameModeKey(defaultMode) << "\n";
        f << "type="    << worldTypeKey(worldType) << "\n";
        f << "size="    << worldSize << "\n";
        if (worldBiome >= 0) f << "biome=" << island::biomeInfo(worldBiome).name << "\n";
        if (worldType == WorldType::Flat) worldFlat.write(f);
        if (worldTypeShaped(worldType)) {
            f << "terrain=" << terrainStyleKey(worldTerrain) << "\n";
            worldParams.write(f, [](int b) { return island::biomeInfo(b).name; }, island::BIOME_COUNT);
        }
        f << "version=" << 2        << "\n";
        return true;
    }

    bool loadDescriptor() {
        std::ifstream f(worldPath + "/world.vxrp");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.substr(0,5) == "seed=")
                worldSeed = std::stoull(line.substr(5));
            if (line.substr(0,5) == "name=")
                worldName = line.substr(5);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.substr(0,5) == "type=")
                worldType = worldTypeFromKey(line.substr(5));
            if (line.substr(0,5) == "size=")
                worldSize = std::atoi(line.substr(5).c_str());
            {
                size_t eq = line.find('=');
                if (eq != std::string::npos && worldFlat.read(line.substr(0, eq), line.substr(eq + 1))) continue;
                if (eq != std::string::npos)
                    worldParams.read(line.substr(0, eq), line.substr(eq + 1),
                                     [](int b) { return island::biomeInfo(b).name; }, island::BIOME_COUNT);
            }
            if (line.substr(0,8) == "terrain=")
                worldTerrain = terrainStyleFromKey(line.substr(8));
            if (line.substr(0,6) == "biome=") {
                std::string bn = line.substr(6);
                for (int b = 0; b < island::BIOME_COUNT; b++)
                    if (bn == island::biomeInfo(b).name) worldBiome = b;
            }
            if (line.substr(0,5) == "mode=") {
                std::string key = line.substr(5);
                // Tolerate Windows line endings
                while (!key.empty() &&
                       (key.back() == '\r' || key.back() == ' '))
                    key.pop_back();
                defaultMode = gameModeFromKey(key, defaultMode);
            }
        }
        worldSize = worldSizeFor(worldType, worldSize);
        return true;
    }
};

extern WorldSaveManager gWorldSave;
