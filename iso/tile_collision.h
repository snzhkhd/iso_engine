#pragma once
// iso_engine — tile collision shapes
// Custom AABB per tile_id, editable, serializable
// ────────────────────────────────────────────────

#include "core.h"

#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>

namespace iso {

    // ─── Collision shape for a single tile type ───

    struct TileCollisionShape {
        // Local AABB within the tile (0..1 range)
        // (0,0) = top-left corner, (1,1) = bottom-right
        float x = 0.f;    // offset from tile origin
        float y = 0.f;
        float width = 1.f;    // size (1.0 = full tile)
        float height = 1.f;
        float z_height = 1.f;  // vertical height in floor units

        // Presets
        static TileCollisionShape full() { return { 0.f,  0.f,  1.f,  1.f,  1.f }; }
        static TileCollisionShape none() { return { 0.f,  0.f,  0.f,  0.f,  0.f }; }
        static TileCollisionShape wall_north() { return { 0.f,  0.f,  1.f,  0.15f, 1.f }; }
        static TileCollisionShape wall_south() { return { 0.f,  0.85f, 1.f, 0.15f, 1.f }; }
        static TileCollisionShape wall_west() { return { 0.f,  0.f,  0.15f, 1.f,  1.f }; }
        static TileCollisionShape wall_east() { return { 0.85f, 0.f, 0.15f, 1.f,  1.f }; }
        static TileCollisionShape half_north() { return { 0.f,  0.f,  1.f,  0.5f,  1.f }; }
        static TileCollisionShape half_south() { return { 0.f,  0.5f, 1.f,  0.5f,  1.f }; }
        static TileCollisionShape half_west() { return { 0.f,  0.f,  0.5f, 1.f,   1.f }; }
        static TileCollisionShape half_east() { return { 0.5f, 0.f,  0.5f, 1.f,   1.f }; }
        static TileCollisionShape fence() { return { 0.f,  0.4f, 1.f,  0.2f,  0.5f }; }

        [[nodiscard]] bool is_none() const {
            return width <= 0.f || height <= 0.f || z_height <= 0.f;
        }

        /// Convert to world-space AABB given tile position
        [[nodiscard]] AABB3 to_world_aabb(int col, int row, int floor,
            float floor_height) const
        {
            float z_base = floor * floor_height;
            return {
                {col + x,         row + y,          z_base},
                {col + x + width, row + y + height, z_base + z_height * floor_height}
            };
        }
    };

    // ─── Collision definition registry ────────────

    class TileCollisionDefs {
    public:
        TileCollisionDefs() = default;

        /// Set collision shape for a tile_id
        void set(uint16_t tile_id, const TileCollisionShape& shape) {
            defs_[tile_id] = shape;
        }

        /// Get collision shape for a tile_id
        /// Returns full-tile collision if not defined and tile is solid
        [[nodiscard]] TileCollisionShape get(uint16_t tile_id) const {
            auto it = defs_.find(tile_id);
            if (it != defs_.end()) return it->second;
            return TileCollisionShape::full(); // default
        }

        /// Check if a tile_id has custom collision defined
        [[nodiscard]] bool has(uint16_t tile_id) const {
            return defs_.find(tile_id) != defs_.end();
        }

        /// Remove custom definition (reverts to default full tile)
        void remove(uint16_t tile_id) {
            defs_.erase(tile_id);
        }

        /// Clear all definitions
        void clear() { defs_.clear(); }

        [[nodiscard]] int count() const { return (int)defs_.size(); }

        /// Iterate all definitions
        [[nodiscard]] const std::unordered_map<uint16_t, TileCollisionShape>& all() const {
            return defs_;
        }

        // ─── Serialization (.isocol file) ─────────

        bool save(const std::string& filepath) const {
            std::ofstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4] = { 'I','S','C','L' };
            f.write(magic, 4);
            uint32_t version = 1;
            f.write(reinterpret_cast<const char*>(&version), 4);
            uint32_t count = (uint32_t)defs_.size();
            f.write(reinterpret_cast<const char*>(&count), 4);

            for (const auto& [id, shape] : defs_) {
                f.write(reinterpret_cast<const char*>(&id), 2);
                f.write(reinterpret_cast<const char*>(&shape.x), 4);
                f.write(reinterpret_cast<const char*>(&shape.y), 4);
                f.write(reinterpret_cast<const char*>(&shape.width), 4);
                f.write(reinterpret_cast<const char*>(&shape.height), 4);
                f.write(reinterpret_cast<const char*>(&shape.z_height), 4);
            }
            return f.good();
        }

        bool load(const std::string& filepath) {
            std::ifstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4];
            f.read(magic, 4);
            if (std::memcmp(magic, "ISCL", 4) != 0) return false;

            uint32_t version;
            f.read(reinterpret_cast<char*>(&version), 4);
            if (version != 1) return false;

            uint32_t count;
            f.read(reinterpret_cast<char*>(&count), 4);

            defs_.clear();
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t id;
                TileCollisionShape shape;
                f.read(reinterpret_cast<char*>(&id), 2);
                f.read(reinterpret_cast<char*>(&shape.x), 4);
                f.read(reinterpret_cast<char*>(&shape.y), 4);
                f.read(reinterpret_cast<char*>(&shape.width), 4);
                f.read(reinterpret_cast<char*>(&shape.height), 4);
                f.read(reinterpret_cast<char*>(&shape.z_height), 4);
                defs_[id] = shape;
            }
            return f.good();
        }

    private:
        std::unordered_map<uint16_t, TileCollisionShape> defs_;
    };

} // namespace iso