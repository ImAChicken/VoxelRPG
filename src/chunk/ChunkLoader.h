#pragma once
#include <cstdint>
#include <filesystem>
#include "Chunk.h"

// Magic number identifies valid .vxrp files
// Spells VXRP in little-endian bytes
constexpr uint32_t CHUNK_MAGIC   = 0x50525856;
constexpr uint16_t CHUNK_VERSION = 1;

enum class ChunkRole : uint8_t {
    World     = 0,
    Structure = 1,
};

// Binary file header — exactly 48 bytes
// #pragma pack ensures no padding between fields
#pragma pack(push, 1)
struct ChunkHeader {
    uint32_t  magic;
    uint16_t  version;
    ChunkRole role;
    uint8_t   dimX, dimY, dimZ;
    uint8_t   reserved0;
    uint8_t   reserved1;
    int32_t   worldX;
    int32_t   worldY;
    int32_t   worldZ;
    uint32_t  voxelDataSize;
    uint32_t  voxelDataOffset;
    uint32_t  entityOffset;
    uint32_t  extDataOffset;
    uint32_t  checksum;
    uint32_t  reserved2;
};
#pragma pack(pop)

static_assert(sizeof(ChunkHeader) == 48,
    "ChunkHeader must be exactly 48 bytes");

class ChunkLoader {
public:
    // Save chunk to disk
    // worldX/Y/Z are the chunk grid coordinates
    static bool save(const Chunk&                  chunk,
                     const std::filesystem::path&  path,
                     int32_t worldX, int32_t worldY, int32_t worldZ,
                     ChunkRole role = ChunkRole::World);

    // Load chunk from disk into outChunk
    // Returns false if file missing, corrupt, or wrong version
    static bool load(const std::filesystem::path& path,
                     Chunk& outChunk);

private:
    static uint32_t crc32(const uint8_t* data, size_t size);
};