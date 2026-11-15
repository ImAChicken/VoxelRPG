#pragma once
#include "Chunk.h"
#include "ChunkManager.h"

enum class PasteMode : uint8_t {
    // Air in source overwrites destination
    OverwriteAll        = 0,
    // Air in source is skipped — solid blocks paste, air doesn't
    AirIsTransparent    = 1,
    // Only paste where destination is air
    OverwriteIfAir      = 2,
};

class PasteSystem {
public:
    // Paste a structure chunk into the world at a specific position
    // cx, cz      = which world chunk to paste into
    // localX/Y/Z  = voxel offset within that chunk (paste origin)
    // sourceOriginX/Z = where in the structure chunk to read from
    //                   usually 32,32 — the center where tree trunk is
    static void paste(
        ChunkManager&   world,
        const Chunk&    structure,
        int             cx, int cz,
        int             localX, int localY, int localZ,
        int             sourceOriginX, int sourceOriginZ,
        PasteMode       mode = PasteMode::AirIsTransparent)
    {
        // We paste a region around the source origin
        // For a tree centered at 32,32 with radius 3
        // we paste from sourceOriginX-3 to sourceOriginX+3
        // This keeps paste operations small and fast

        // Calculate bounds of non-air content in structure
        // For simplicity paste a fixed 9x12x9 region
        // (wide enough for leaves, tall enough for tree)
        const int halfW = 4;   // half width  — covers radius 3 + 1
        const int height = 12; // tall enough for pine tree

        for (int dy = 0; dy < height; dy++)
        for (int dx = -halfW; dx <= halfW; dx++)
        for (int dz = -halfW; dz <= halfW; dz++) {
            // Source position in structure chunk
            int sx = sourceOriginX + dx;
            int sy = dy;
            int sz = sourceOriginZ + dz;

            uint16_t srcID = structure.typeAt(sx, sy, sz);

            // Skip air based on paste mode
            if (mode == PasteMode::AirIsTransparent &&
                srcID == BLOCK_AIR)
                continue;

            // Destination position in world
            int wx = localX + dx;
            int wy = localY + dy;
            int wz = localZ + dz;

            // Handle overflow into neighbour chunks
            pasteVoxel(world, cx, cz, wx, wy, wz, srcID, mode);
        }
    }

private:
    static void pasteVoxel(
        ChunkManager& world,
        int cx, int cz,
        int wx, int wy, int wz,
        uint16_t srcID,
        PasteMode mode)
    {
        // Resolve chunk and local coords handling overflow
        int tcx = cx, tcz = cz;
        int lx = wx, lz = wz;

        // Handle X overflow
        while (lx < 0)          { tcx--; lx += VOXEL_DIM; }
        while (lx >= VOXEL_DIM) { tcx++; lx -= VOXEL_DIM; }

        // Handle Z overflow
        while (lz < 0)          { tcz--; lz += VOXEL_DIM; }
        while (lz >= VOXEL_DIM) { tcz++; lz -= VOXEL_DIM; }

        // Y is clamped — trees don't go underground or above chunk
        if (wy < 0 || wy >= WORLD_HEIGHT) return;

        Chunk* dest = world.getChunk(tcx, tcz);
        if (!dest) return;

        uint16_t dstID = dest->typeAt(lx, wy, lz);

        switch (mode) {
            case PasteMode::OverwriteAll:
                dest->setBlock(lx, wy, lz, srcID);
                break;
            case PasteMode::AirIsTransparent:
                if (srcID != BLOCK_AIR)
                    dest->setBlock(lx, wy, lz, srcID);
                break;
            case PasteMode::OverwriteIfAir:
                if (dstID == BLOCK_AIR)
                    dest->setBlock(lx, wy, lz, srcID);
                break;
        }
    }
};