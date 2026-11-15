#pragma once
#include "../voxel/Voxel.h"
#include "../voxel/VoxelRegistry.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// VoxelVolume — a box of voxels that is not part of the world
// ══════════════════════════════════════════════════════════
// What copy produces and paste consumes. The same object is
// used for:
//   - the dev clipboard (copy tool, /copy)
//   - structures saved to disk (/save, /load)
//   - later: trees, buildings and anything placed by scripts
//
// Layout matches chunks (Y-major): index = (y*sz + z)*sx + x
//
// A volume has an ANCHOR: the voxel inside it that lands on
// the target when pasted. Copy sets it to the bottom centre,
// so a structure pastes standing on the block you click.
struct VoxelVolume {
    glm::ivec3            size   = {0, 0, 0};
    glm::ivec3            anchor = {0, 0, 0};
    std::vector<uint16_t> data;

    VoxelVolume() = default;
    explicit VoxelVolume(glm::ivec3 s) { resize(s); }

    void resize(glm::ivec3 s) {
        size = s;
        data.assign((size_t)s.x * s.y * s.z, BLOCK_AIR);
        anchor = { s.x / 2, 0, s.z / 2 };
    }

    bool   empty()  const { return data.empty(); }
    size_t voxels() const { return data.size(); }

    size_t index(int x, int y, int z) const {
        return ((size_t)y * size.z + z) * size.x + x;
    }
    bool inside(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 &&
               x < size.x && y < size.y && z < size.z;
    }
    uint16_t at(int x, int y, int z) const {
        return inside(x, y, z) ? data[index(x, y, z)] : BLOCK_AIR;
    }
    void set(int x, int y, int z, uint16_t id) {
        if (inside(x, y, z)) data[index(x, y, z)] = id;
    }

    // ── Turning and flipping ──────────────────────────────
    // Quarter turns about the vertical axis (clockwise seen
    // from above). Sideways logs are swapped so a log lying
    // along X lies along Z after a quarter turn.
    VoxelVolume rotatedY(int quarterTurns) const {
        int q = ((quarterTurns % 4) + 4) % 4;
        if (q == 0 || empty()) return *this;

        VoxelVolume out;
        glm::ivec3 s = (q % 2) ? glm::ivec3(size.z, size.y, size.x)
                               : size;
        out.resize(s);
        bool swapLogs = (q % 2) == 1;

        for (int y = 0; y < size.y; y++)
        for (int z = 0; z < size.z; z++)
        for (int x = 0; x < size.x; x++) {
            glm::ivec2 p = turn(x, z, q);
            uint16_t id = at(x, y, z);
            if (swapLogs) id = swapLogAxis(id);
            out.set(p.x, y, p.y, id);
        }
        glm::ivec2 a = turn(anchor.x, anchor.z, q);
        out.anchor = { a.x, anchor.y, a.y };
        return out;
    }

    // Mirror along one horizontal axis ('x' or 'z')
    VoxelVolume mirrored(char axis) const {
        VoxelVolume out = *this;
        for (int y = 0; y < size.y; y++)
        for (int z = 0; z < size.z; z++)
        for (int x = 0; x < size.x; x++) {
            int nx = (axis == 'x') ? size.x - 1 - x : x;
            int nz = (axis == 'z') ? size.z - 1 - z : z;
            out.set(nx, y, nz, at(x, y, z));
        }
        if (axis == 'x') out.anchor.x = size.x - 1 - anchor.x;
        else             out.anchor.z = size.z - 1 - anchor.z;
        return out;
    }

    // ── Save / load ───────────────────────────────────────
    // Blocks are stored by NAME (a small palette at the top
    // of the file), so a structure still loads correctly if
    // block IDs are renumbered in blocks.toml.
    //
    // File: "VXVL" u32 version, i32×3 size, i32×3 anchor,
    //       u16 palette count, palette names (u8 len + text),
    //       u32 run count, runs (u32 length, u16 palette index)
    bool save(const std::filesystem::path& path) const {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;

        // Palette: block ID → slot in the file
        std::vector<int> slotOf(65536, -1);
        std::vector<uint16_t> palette;
        for (uint16_t id : data)
            if (slotOf[id] < 0) {
                slotOf[id] = (int)palette.size();
                palette.push_back(id);
            }

        f.write("VXVL", 4);
        put<uint32_t>(f, 1);
        put<int32_t>(f, size.x);   put<int32_t>(f, size.y);
        put<int32_t>(f, size.z);
        put<int32_t>(f, anchor.x); put<int32_t>(f, anchor.y);
        put<int32_t>(f, anchor.z);

        put<uint16_t>(f, (uint16_t)palette.size());
        for (uint16_t id : palette) {
            std::string name = gRegistry.get(id).name;
            if (id == BLOCK_AIR) name = "air";
            put<uint8_t>(f, (uint8_t)name.size());
            f.write(name.data(), (std::streamsize)name.size());
        }

        // Runs of the same block
        std::vector<std::pair<uint32_t, uint16_t>> runs;
        for (size_t i = 0; i < data.size(); ) {
            size_t j = i;
            while (j < data.size() && data[j] == data[i]) j++;
            runs.push_back({ (uint32_t)(j - i),
                             (uint16_t)slotOf[data[i]] });
            i = j;
        }
        put<uint32_t>(f, (uint32_t)runs.size());
        for (auto& r : runs) {
            put<uint32_t>(f, r.first);
            put<uint16_t>(f, r.second);
        }
        return (bool)f;
    }

    bool load(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        char magic[4];
        f.read(magic, 4);
        if (std::memcmp(magic, "VXVL", 4) != 0) return false;
        if (get<uint32_t>(f) != 1) return false;

        glm::ivec3 s, a;
        s.x = get<int32_t>(f); s.y = get<int32_t>(f); s.z = get<int32_t>(f);
        a.x = get<int32_t>(f); a.y = get<int32_t>(f); a.z = get<int32_t>(f);
        // Refuse absurd sizes from a damaged file
        if (s.x <= 0 || s.y <= 0 || s.z <= 0 ||
            (int64_t)s.x * s.y * s.z > 512ll * 512 * 512)
            return false;

        uint16_t n = get<uint16_t>(f);
        std::vector<uint16_t> palette(n, BLOCK_AIR);
        for (uint16_t i = 0; i < n; i++) {
            uint8_t len = get<uint8_t>(f);
            std::string name(len, ' ');
            f.read(&name[0], len);
            // Unknown block names become air
            palette[i] = (name == "air") ? BLOCK_AIR
                                         : gRegistry.getIDByName(name);
        }

        resize(s);
        anchor = a;
        uint32_t runCount = get<uint32_t>(f);
        size_t pos = 0;
        for (uint32_t r = 0; r < runCount && f; r++) {
            uint32_t len  = get<uint32_t>(f);
            uint16_t slot = get<uint16_t>(f);
            uint16_t id   = slot < palette.size() ? palette[slot]
                                                  : BLOCK_AIR;
            for (uint32_t k = 0; k < len && pos < data.size(); k++)
                data[pos++] = id;
        }
        return pos == data.size();
    }

private:
    // Where (x, z) ends up after q clockwise quarter turns
    glm::ivec2 turn(int x, int z, int q) const {
        switch (q) {
        case 1:  return { size.z - 1 - z, x };
        case 2:  return { size.x - 1 - x, size.z - 1 - z };
        case 3:  return { z, size.x - 1 - x };
        default: return { x, z };
        }
    }

    static uint16_t swapLogAxis(uint16_t id) {
        if (id == BLOCK_LOG_X) return BLOCK_LOG_Z;
        if (id == BLOCK_LOG_Z) return BLOCK_LOG_X;
        return id;
    }

    template <typename T>
    static void put(std::ofstream& f, T v) {
        f.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T>
    static T get(std::ifstream& f) {
        T v{};
        f.read(reinterpret_cast<char*>(&v), sizeof(T));
        return v;
    }
};
