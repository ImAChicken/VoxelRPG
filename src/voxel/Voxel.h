#pragma once
#include <cstdint>

// ── World scale ───────────────────────────────────────────
// 4 inches per voxel, 128 voxels per chunk side
//   chunk  = 128 voxels = 512 inches = 42.7 ft per side
//   player = 17 voxels  = 68 inches  = 5'8"
constexpr int   VOXEL_DIM       = 128;
constexpr int   VOXEL_COUNT     = VOXEL_DIM * VOXEL_DIM * VOXEL_DIM;
constexpr float VOXEL_SIZE_IN   = 4.0f;

// Block IDs — must match data/registry/blocks.toml exactly
constexpr uint16_t BLOCK_AIR          = 0;
constexpr uint16_t BLOCK_STONE        = 1;
constexpr uint16_t BLOCK_CRACKED_STONE= 2;
constexpr uint16_t BLOCK_RUBBLE       = 3;
constexpr uint16_t BLOCK_DIRT         = 4;
constexpr uint16_t BLOCK_GRASS        = 5;
constexpr uint16_t BLOCK_FARMLAND     = 6;
constexpr uint16_t BLOCK_LOG_Y        = 7;
constexpr uint16_t BLOCK_LOG_X        = 8;
constexpr uint16_t BLOCK_LOG_Z        = 9;
constexpr uint16_t BLOCK_PLANKS       = 10;
constexpr uint16_t BLOCK_THATCH       = 11;
constexpr uint16_t BLOCK_BRICKS       = 12;
constexpr uint16_t BLOCK_CRACKED_BRICK= 13;
constexpr uint16_t BLOCK_LEAVES       = 14;
constexpr uint16_t BLOCK_GLASS        = 15;
constexpr uint16_t BLOCK_FIRE         = 16;
constexpr uint16_t BLOCK_WATER        = 17;
constexpr uint16_t BLOCK_LAVA         = 18;
constexpr uint16_t BLOCK_TORCH        = 19;
constexpr uint16_t BLOCK_SNOW         = 20;
constexpr uint16_t BLOCK_SAND         = 21;
constexpr uint16_t BLOCK_COBBLESTONE  = 22;
constexpr uint16_t BLOCK_MOSS_STONE   = 23;
constexpr uint16_t BLOCK_MUD          = 24;
constexpr uint16_t BLOCK_GRAVEL       = 25;
constexpr uint16_t BLOCK_VOLCANIC_ROCK= 26;

struct Voxel {
    uint16_t typeID = BLOCK_AIR;
};
