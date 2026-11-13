#pragma once
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "../chunk/Chunk.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/ChunkLoader.h"

class WorldSaveManager {
public:
    // Start a brand new world. If a save with this name
    // already exists its chunk files are removed — they
    // belong to the old world and would otherwise be loaded
    // into the new one.
    bool createWorld(const std::string& name, uint64_t seed) {
        worldName = name;
        worldSeed = seed;
        worldPath = "saves/" + name;
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

        return saveDescriptor();
    }

    bool loadWorld(const std::string& name) {
        worldPath = "saves/" + name;
        worldName = name;
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

    // Folder of the open world, e.g. "saves/myworld"
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
    std::string getName()  const { return worldName; }
    bool        isLoaded() const { return !worldPath.empty(); }

private:
    std::string worldPath;
    std::string worldName;
    uint64_t    worldSeed = 42;

    bool saveDescriptor() {
        std::ofstream f(worldPath + "/world.vxrp");
        if (!f) return false;
        f << "name="    << worldName << "\n";
        f << "seed="    << worldSeed << "\n";
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
        }
        return true;
    }
};

extern WorldSaveManager gWorldSave;
