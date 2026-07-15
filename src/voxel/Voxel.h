#pragma once
#include <cstdint>

// Chunk dimensions
constexpr int VOXEL_DIM = 64;

// Block IDs
constexpr uint16_t BLOCK_AIR    = 0;
constexpr uint16_t BLOCK_STONE  = 1;
constexpr uint16_t BLOCK_DIRT   = 2;
constexpr uint16_t BLOCK_GRASS  = 3;
constexpr uint16_t BLOCK_LOG_Y  = 4;
constexpr uint16_t BLOCK_PLANKS = 5;

struct Voxel {
    uint16_t typeID = BLOCK_AIR;
};