#pragma once
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../chunk/Chunk.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/ChunkLoader.h"
#include "../chunk/TerrainGenerator.h"

class WorldSaveManager {
public:
    bool createWorld(const std::string& name, uint64_t seed) {
        worldName = name;
        worldSeed = seed;
        worldPath = "saves/" + name;
        std::filesystem::create_directories(
            worldPath + "/chunks");
        return saveDescriptor();
    }

    bool loadWorld(const std::string& name) {
        worldPath = "saves/" + name;
        worldName = name;
        return loadDescriptor();
    }

    void initChunks(ChunkManager& world,
                    TerrainGenerator& gen) {
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            auto path = chunkPath(cx, cz);
            if (std::filesystem::exists(path)) {
                ChunkPtr loaded = makeChunk();
                if (ChunkLoader::load(path, *loaded)) {
                    world.chunks[world.index(cx,cz)]
                        = std::move(loaded);
                    continue;
                }
            }
            world.chunks[world.index(cx,cz)]
                = gen.generateChunk(cx, cz);
        }
    }

    void saveDirtyChunks(ChunkManager& world) {
        int saved = 0;
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            if (!world.isDirty(cx, cz)) continue;
            Chunk* chunk = world.getChunk(cx, cz);
            if (!chunk) continue;
            ChunkLoader::save(*chunk,
                chunkPath(cx, cz), cx, 0, cz);
            world.clearDirty(cx, cz);
            saved++;
        }
        if (saved > 0)
            std::cout << "Saved " << saved << " chunks\n";
    }

    uint64_t    getSeed()  const { return worldSeed; }
    std::string getName()  const { return worldName; }
    bool        isLoaded() const { return !worldPath.empty(); }

private:
    std::string worldPath;
    std::string worldName;
    uint64_t    worldSeed = 42;

    std::filesystem::path chunkPath(int cx, int cz) const {
        return worldPath + "/chunks/c_"
            + std::to_string(cx) + "_0_"
            + std::to_string(cz) + ".vxrp";
    }

    bool saveDescriptor() {
        std::ofstream f(worldPath + "/world.vxrp");
        if (!f) return false;
        f << "name="    << worldName << "\n";
        f << "seed="    << worldSeed << "\n";
        f << "version=" << 1        << "\n";
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
