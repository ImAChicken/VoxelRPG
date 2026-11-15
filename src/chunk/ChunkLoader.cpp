#include "ChunkLoader.h"
#include "lz4/lz4.h"
#include <fstream>
#include <vector>
#include <iostream>

namespace {
    constexpr int SECTION_BYTES = VOXEL_COUNT * (int)sizeof(uint16_t);

    // One section → LZ4 bytes
    bool packSection(const ChunkSection& sec, std::vector<uint8_t>& out) {
        static thread_local std::vector<uint16_t> voxels;
        static thread_local std::vector<uint8_t>  raw;
        voxels.resize(VOXEL_COUNT);
        raw.resize(SECTION_BYTES);
        sec.copyTo(voxels.data());
        for (int i = 0; i < VOXEL_COUNT; i++) {
            raw[i * 2]     = (uint8_t)(voxels[i] & 0xFF);
            raw[i * 2 + 1] = (uint8_t)(voxels[i] >> 8);
        }
        int bound = LZ4_compressBound(SECTION_BYTES);
        size_t at = out.size();
        out.resize(at + 4 + bound);
        int n = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
                                     reinterpret_cast<char*>(out.data() + at + 4),
                                     SECTION_BYTES, bound);
        if (n <= 0) return false;
        out[at] = (uint8_t)n; out[at + 1] = (uint8_t)(n >> 8);
        out[at + 2] = (uint8_t)(n >> 16); out[at + 3] = (uint8_t)(n >> 24);
        out.resize(at + 4 + n);
        return true;
    }

    // LZ4 bytes → one section
    bool unpackSection(const uint8_t* src, int size, ChunkSection& sec) {
        static thread_local std::vector<uint8_t> raw;
        raw.resize(SECTION_BYTES);
        int got = LZ4_decompress_safe(reinterpret_cast<const char*>(src),
                                      reinterpret_cast<char*>(raw.data()),
                                      size, SECTION_BYTES);
        if (got != SECTION_BYTES) return false;
        uint16_t* out = sec.rawData();
        for (int i = 0; i < VOXEL_COUNT; i++)
            out[i] = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
        sec.compress();
        return true;
    }
}

bool ChunkLoader::save(const Chunk&                 chunk,
                        const std::filesystem::path& path,
                        int32_t worldX,
                        int32_t worldY,
                        int32_t worldZ,
                        ChunkRole role)
{
    std::vector<uint8_t> data;
    for (int s = 0; s < SECTIONS; s++) {
        if (!packSection(chunk.section(s), data)) {
            std::cerr << "LZ4 compression failed\n";
            return false;
        }
    }

    ChunkHeader header{};
    header.magic           = CHUNK_MAGIC;
    header.version         = CHUNK_VERSION;
    header.role            = role;
    header.dimX            = VOXEL_DIM;
    header.dimY            = SECTIONS;
    header.dimZ            = VOXEL_DIM;
    header.worldX          = worldX;
    header.worldY          = worldY;
    header.worldZ          = worldZ;
    header.voxelDataSize   = (uint32_t)data.size();
    header.voxelDataOffset = (uint32_t)sizeof(ChunkHeader);
    header.entityOffset    = 0;
    header.extDataOffset   = 0;
    header.checksum        = crc32(data.data(), data.size());

    // Written to a temporary file and renamed, so a crash
    // mid-save never leaves a half-written chunk behind
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "Cannot open for writing: " << tmp << "\n";
            return false;
        }
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
        if (!file.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

bool ChunkLoader::load(const std::filesystem::path& path,
                        Chunk& outChunk)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open: " << path << "\n";
        return false;
    }

    ChunkHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        std::cerr << "Failed to read header: " << path << "\n";
        return false;
    }
    if (header.magic != CHUNK_MAGIC) {
        std::cerr << "Invalid magic number: " << path << "\n";
        return false;
    }
    bool v2 = header.version == 2 && header.dimY == VOXEL_DIM;
    bool v3 = header.version == 3 && header.dimY == SECTIONS;
    if ((!v2 && !v3) || header.dimX != VOXEL_DIM || header.dimZ != VOXEL_DIM) {
        // Saved by an older build with a different chunk
        // layout — caller regenerates the chunk instead
        return false;
    }

    file.seekg(header.voxelDataOffset);
    std::vector<uint8_t> data(header.voxelDataSize);
    file.read(reinterpret_cast<char*>(data.data()), header.voxelDataSize);
    if (!file) {
        std::cerr << "Failed to read voxel data: " << path << "\n";
        return false;
    }
    if (crc32(data.data(), data.size()) != header.checksum) {
        std::cerr << "Checksum mismatch: " << path << "\n";
        return false;
    }

    if (v2) {
        // One 128³ chunk from before sections: the bottom
        // section, open sky above it
        if (!unpackSection(data.data(), (int)data.size(), outChunk.section(0))) {
            std::cerr << "Decompression failed: " << path << "\n";
            return false;
        }
        for (int s = 1; s < SECTIONS; s++)
            outChunk.section(s).setRle(ChunkSection::allAirShared());
        return true;
    }

    size_t at = 0;
    for (int s = 0; s < SECTIONS; s++) {
        if (at + 4 > data.size()) return false;
        int n = (int)(data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) |
                      ((uint32_t)data[at + 3] << 24));
        at += 4;
        if (n <= 0 || at + (size_t)n > data.size() ||
            !unpackSection(data.data() + at, n, outChunk.section(s))) {
            std::cerr << "Decompression failed: " << path << "\n";
            return false;
        }
        at += (size_t)n;
    }
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