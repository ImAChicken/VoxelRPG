#include "ChunkLoader.h"
#include "lz4/lz4.h"
#include <fstream>
#include <vector>
#include <iostream>

bool ChunkLoader::save(const Chunk&                 chunk,
                        const std::filesystem::path& path,
                        int32_t worldX,
                        int32_t worldY,
                        int32_t worldZ,
                        ChunkRole role)
{
    // --- Flatten voxels into raw little-endian bytes ---
    // Voxel order is Y-major, identical to Chunk::index()
    constexpr int RAW_SIZE_BYTES = VOXEL_COUNT * (int)sizeof(uint16_t);

    std::vector<uint16_t> voxels(VOXEL_COUNT);
    chunk.copyTo(voxels.data());

    std::vector<uint8_t> raw(RAW_SIZE_BYTES);
    for (int i = 0; i < VOXEL_COUNT; i++) {
        raw[i * 2]     = (uint8_t)(voxels[i] & 0xFF);
        raw[i * 2 + 1] = (uint8_t)(voxels[i] >> 8);
    }

    // --- LZ4 compress ---
    int maxCompressed = LZ4_compressBound(RAW_SIZE_BYTES);
    std::vector<uint8_t> compressed(maxCompressed);

    int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(raw.data()),
        reinterpret_cast<char*>(compressed.data()),
        RAW_SIZE_BYTES,
        maxCompressed);

    if (compressedSize <= 0) {
        std::cerr << "LZ4 compression failed\n";
        return false;
    }
    compressed.resize(compressedSize);

    // --- Build header ---
    ChunkHeader header{};
    header.magic           = CHUNK_MAGIC;
    header.version         = CHUNK_VERSION;
    header.role            = role;
    header.dimX            = VOXEL_DIM;
    header.dimY            = VOXEL_DIM;
    header.dimZ            = VOXEL_DIM;
    header.worldX          = worldX;
    header.worldY          = worldY;
    header.worldZ          = worldZ;
    header.voxelDataSize   = (uint32_t)compressedSize;
    header.voxelDataOffset = (uint32_t)sizeof(ChunkHeader);
    header.entityOffset    = 0;
    header.extDataOffset   = 0;
    header.checksum        = crc32(compressed.data(), compressedSize);

    // --- Write to disk ---
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "Cannot open for writing: " << path << "\n";
        return false;
    }

    file.write(reinterpret_cast<const char*>(&header),
               sizeof(header));
    file.write(reinterpret_cast<const char*>(compressed.data()),
               compressedSize);

    return file.good();
}

bool ChunkLoader::load(const std::filesystem::path& path,
                        Chunk& outChunk)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open: " << path << "\n";
        return false;
    }

    // --- Read header ---
    ChunkHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        std::cerr << "Failed to read header: " << path << "\n";
        return false;
    }

    // --- Validate ---
    if (header.magic != CHUNK_MAGIC) {
        std::cerr << "Invalid magic number: " << path << "\n";
        return false;
    }
    if (header.version != CHUNK_VERSION ||
        header.dimX != VOXEL_DIM ||
        header.dimY != VOXEL_DIM ||
        header.dimZ != VOXEL_DIM) {
        // Saved by an older build with a different chunk
        // layout — caller regenerates the chunk instead
        return false;
    }

    // --- Read compressed data ---
    file.seekg(header.voxelDataOffset);
    std::vector<uint8_t> compressed(header.voxelDataSize);
    file.read(reinterpret_cast<char*>(compressed.data()),
              header.voxelDataSize);
    if (!file) {
        std::cerr << "Failed to read voxel data: " << path << "\n";
        return false;
    }

    // --- Verify checksum ---
    uint32_t check = crc32(compressed.data(), compressed.size());
    if (check != header.checksum) {
        std::cerr << "Checksum mismatch: " << path << "\n";
        return false;
    }

    // --- Decompress ---
    constexpr int RAW_SIZE_BYTES = VOXEL_COUNT * (int)sizeof(uint16_t);

    std::vector<uint8_t> raw(RAW_SIZE_BYTES);
    int decompSize = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed.data()),
        reinterpret_cast<char*>(raw.data()),
        (int)compressed.size(),
        RAW_SIZE_BYTES);

    if (decompSize != RAW_SIZE_BYTES) {
        std::cerr << "Decompression failed: " << path << "\n";
        return false;
    }

    // --- Unpack into chunk, then drop back to RLE ---
    uint16_t* out = outChunk.rawData();
    for (int i = 0; i < VOXEL_COUNT; i++) {
        uint16_t lo = raw[i * 2];
        uint16_t hi = raw[i * 2 + 1];
        out[i] = (uint16_t)(lo | (hi << 8));
    }
    outChunk.compress();

    return true;
}

// Simple CRC32 implementation
uint32_t ChunkLoader::crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}