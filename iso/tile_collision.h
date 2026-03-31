#pragma once
// iso_engine — tile collision shapes
// Two-level system:
//   1) TileCollisionDefs  — defaults per tile_id (configure once)
//   2) InstanceOverrides  — per-tile-position overrides on the map
// ────────────────────────────────────────────────────────────────

#include "core.h"

#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace iso {

    // ─── Single collision box (local coords 0..1) ─

    struct CollisionBox {
        float x = 0.f;
        float y = 0.f;
        float width = 1.f;
        float height = 1.f;
        float z_height = 1.f;

        [[nodiscard]] bool is_none() const {
            return width <= 0.f || height <= 0.f || z_height <= 0.f;
        }

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

    // ─── Collision shape (1 or more boxes) ────────

    struct TileCollisionShape {
        std::vector<CollisionBox> boxes;

        [[nodiscard]] bool is_none() const {
            return boxes.empty() || (boxes.size() == 1 && boxes[0].is_none());
        }

        [[nodiscard]] bool is_single() const { return boxes.size() == 1; }

        /// Check if any box in this shape overlaps with an AABB
        [[nodiscard]] bool overlaps(const AABB3& body, int col, int row, int floor,
            float floor_height) const
        {
            for (const auto& box : boxes) {
                if (box.is_none()) continue;
                AABB3 tile_aabb = box.to_world_aabb(col, row, floor, floor_height);
                if (body.overlaps(tile_aabb)) return true;
            }
            return false;
        }

        /// Get all world AABBs
        [[nodiscard]] std::vector<AABB3> to_world_aabbs(int col, int row, int floor,
            float floor_height) const
        {
            std::vector<AABB3> result;
            for (const auto& box : boxes) {
                if (!box.is_none())
                    result.push_back(box.to_world_aabb(col, row, floor, floor_height));
            }
            return result;
        }

        // ─── Presets ──────────────────────────────

        static TileCollisionShape full() {
            return { {{0.f, 0.f, 1.f, 1.f, 1.f}} };
        }
        static TileCollisionShape none() {
            return { {} };
        }

        // Thin walls (on tile edges)
        static TileCollisionShape wall_north() { return { {{0.f,  0.f,   1.f,  0.15f, 1.f}} }; }
        static TileCollisionShape wall_south() { return { {{0.f,  0.85f, 1.f,  0.15f, 1.f}} }; }
        static TileCollisionShape wall_west() { return { {{0.f,  0.f,   0.15f, 1.f,  1.f}} }; }
        static TileCollisionShape wall_east() { return { {{0.85f, 0.f,  0.15f, 1.f,  1.f}} }; }

        // Half walls
        static TileCollisionShape half_north() { return { {{0.f, 0.f,  1.f, 0.5f, 1.f}} }; }
        static TileCollisionShape half_south() { return { {{0.f, 0.5f, 1.f, 0.5f, 1.f}} }; }
        static TileCollisionShape half_west() { return { {{0.f, 0.f,  0.5f, 1.f, 1.f}} }; }
        static TileCollisionShape half_east() { return { {{0.5f, 0.f, 0.5f, 1.f, 1.f}} }; }

        // Corners (L-shaped — two boxes)
        static TileCollisionShape corner_nw() {
            return { {{0.f, 0.f, 1.f, 0.15f, 1.f},     // north wall
                     {0.f, 0.f, 0.15f, 1.f, 1.f}} };   // west wall
        }
        static TileCollisionShape corner_ne() {
            return { {{0.f,  0.f,  1.f,   0.15f, 1.f},  // north wall
                     {0.85f, 0.f, 0.15f, 1.f,   1.f}} }; // east wall
        }
        static TileCollisionShape corner_sw() {
            return { {{0.f,  0.85f, 1.f,  0.15f, 1.f},  // south wall
                     {0.f,  0.f,   0.15f, 1.f,  1.f}} }; // west wall
        }
        static TileCollisionShape corner_se() {
            return { {{0.f,   0.85f, 1.f,   0.15f, 1.f}, // south wall
                     {0.85f, 0.f,   0.15f, 1.f,   1.f}} }; // east wall
        }

        // Doorway (wall with gap in middle)
        static TileCollisionShape door_north() {
            return { {{0.f,  0.f, 0.3f, 0.15f, 1.f},    // left part
                     {0.7f, 0.f, 0.3f, 0.15f, 1.f}} };  // right part
        }
        static TileCollisionShape door_south() {
            return { {{0.f,  0.85f, 0.3f, 0.15f, 1.f},
                     {0.7f, 0.85f, 0.3f, 0.15f, 1.f}} };
        }
        static TileCollisionShape door_west() {
            return { {{0.f, 0.f,  0.15f, 0.3f, 1.f},
                     {0.f, 0.7f, 0.15f, 0.3f, 1.f}} };
        }
        static TileCollisionShape door_east() {
            return { {{0.85f, 0.f,  0.15f, 0.3f, 1.f},
                     {0.85f, 0.7f, 0.15f, 0.3f, 1.f}} };
        }

        // Window (like wall but half height)
        static TileCollisionShape window_north() { return { {{0.f,  0.f,   1.f,  0.15f, 0.5f}} }; }
        static TileCollisionShape window_south() { return { {{0.f,  0.85f, 1.f,  0.15f, 0.5f}} }; }

        // Fence / railing
        static TileCollisionShape fence_ns() { return { {{0.4f, 0.f,  0.2f, 1.f,  0.5f}} }; }
        static TileCollisionShape fence_ew() { return { {{0.f,  0.4f, 1.f,  0.2f, 0.5f}} }; }

        // Pillar (small center box)
        static TileCollisionShape pillar() { return { {{0.35f, 0.35f, 0.3f, 0.3f, 1.f}} }; }

        // Table / furniture (center, low)
        static TileCollisionShape furniture() { return { {{0.1f, 0.1f, 0.8f, 0.8f, 0.5f}} }; }
    };

    // ─── Preset info (for editor UI) ──────────────

    struct PresetInfo {
        const char* name;
        TileCollisionShape(*create)();
    };

    inline const PresetInfo COLLISION_PRESETS[] = {
        {"Full",       TileCollisionShape::full},
        {"None",       TileCollisionShape::none},
        {"Wall N",     TileCollisionShape::wall_north},
        {"Wall S",     TileCollisionShape::wall_south},
        {"Wall W",     TileCollisionShape::wall_west},
        {"Wall E",     TileCollisionShape::wall_east},
        {"Corner NW",  TileCollisionShape::corner_nw},
        {"Corner NE",  TileCollisionShape::corner_ne},
        {"Corner SW",  TileCollisionShape::corner_sw},
        {"Corner SE",  TileCollisionShape::corner_se},
        {"Door N",     TileCollisionShape::door_north},
        {"Door S",     TileCollisionShape::door_south},
        {"Door W",     TileCollisionShape::door_west},
        {"Door E",     TileCollisionShape::door_east},
        {"Half N",     TileCollisionShape::half_north},
        {"Half S",     TileCollisionShape::half_south},
        {"Half W",     TileCollisionShape::half_west},
        {"Half E",     TileCollisionShape::half_east},
        {"Window N",   TileCollisionShape::window_north},
        {"Window S",   TileCollisionShape::window_south},
        {"Fence NS",   TileCollisionShape::fence_ns},
        {"Fence EW",   TileCollisionShape::fence_ew},
        {"Pillar",     TileCollisionShape::pillar},
        {"Furniture",  TileCollisionShape::furniture},
    };
    inline constexpr int COLLISION_PRESET_COUNT = sizeof(COLLISION_PRESETS) / sizeof(COLLISION_PRESETS[0]);

    // ─── Default collision defs (per tile_id) ─────

    class TileCollisionDefs {
    public:
        TileCollisionDefs() = default;

        void set(uint16_t tile_id, const TileCollisionShape& shape) {
            defs_[tile_id] = shape;
        }

        [[nodiscard]] const TileCollisionShape* get(uint16_t tile_id) const {
            auto it = defs_.find(tile_id);
            return (it != defs_.end()) ? &it->second : nullptr;
        }

        [[nodiscard]] bool has(uint16_t tile_id) const {
            return defs_.find(tile_id) != defs_.end();
        }

        void remove(uint16_t tile_id) { defs_.erase(tile_id); }
        void clear() { defs_.clear(); }
        [[nodiscard]] int count() const { return (int)defs_.size(); }
        [[nodiscard]] const auto& all() const { return defs_; }

        // ─── Serialization ────────────────────────

        bool save(const std::string& filepath) const {
            std::ofstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4] = { 'I','S','C','L' };
            f.write(magic, 4);
            uint32_t version = 2;
            f.write(reinterpret_cast<const char*>(&version), 4);
            uint32_t count = (uint32_t)defs_.size();
            f.write(reinterpret_cast<const char*>(&count), 4);

            for (const auto& [id, shape] : defs_) {
                f.write(reinterpret_cast<const char*>(&id), 2);
                uint8_t box_count = (uint8_t)shape.boxes.size();
                f.write(reinterpret_cast<const char*>(&box_count), 1);
                for (const auto& box : shape.boxes) {
                    f.write(reinterpret_cast<const char*>(&box.x), 4);
                    f.write(reinterpret_cast<const char*>(&box.y), 4);
                    f.write(reinterpret_cast<const char*>(&box.width), 4);
                    f.write(reinterpret_cast<const char*>(&box.height), 4);
                    f.write(reinterpret_cast<const char*>(&box.z_height), 4);
                }
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

            uint32_t count;
            f.read(reinterpret_cast<char*>(&count), 4);

            defs_.clear();
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t id;
                f.read(reinterpret_cast<char*>(&id), 2);

                TileCollisionShape shape;
                if (version >= 2) {
                    uint8_t box_count;
                    f.read(reinterpret_cast<char*>(&box_count), 1);
                    shape.boxes.resize(box_count);
                    for (auto& box : shape.boxes) {
                        f.read(reinterpret_cast<char*>(&box.x), 4);
                        f.read(reinterpret_cast<char*>(&box.y), 4);
                        f.read(reinterpret_cast<char*>(&box.width), 4);
                        f.read(reinterpret_cast<char*>(&box.height), 4);
                        f.read(reinterpret_cast<char*>(&box.z_height), 4);
                    }
                }
                else {
                    // v1 compat: single box
                    CollisionBox box;
                    f.read(reinterpret_cast<char*>(&box.x), 4);
                    f.read(reinterpret_cast<char*>(&box.y), 4);
                    f.read(reinterpret_cast<char*>(&box.width), 4);
                    f.read(reinterpret_cast<char*>(&box.height), 4);
                    f.read(reinterpret_cast<char*>(&box.z_height), 4);
                    shape.boxes.push_back(box);
                }
                defs_[id] = std::move(shape);
            }
            return f.good();
        }

    private:
        std::unordered_map<uint16_t, TileCollisionShape> defs_;
    };

    // ─── Instance overrides (per tile position) ───

    struct InstanceKey {
        int col, row, floor;
        bool operator==(const InstanceKey&) const = default;
    };

    struct InstanceKeyHash {
        size_t operator()(const InstanceKey& k) const {
            return std::hash<uint64_t>{}(
                ((uint64_t)(uint32_t)k.col << 32) | (uint32_t)k.row
                ) ^ std::hash<int>{}(k.floor);
        }
    };

    class InstanceCollisionOverrides {
    public:
        void set(int col, int row, int floor, const TileCollisionShape& shape) {
            overrides_[{col, row, floor}] = shape;
        }

        [[nodiscard]] const TileCollisionShape* get(int col, int row, int floor) const {
            auto it = overrides_.find({ col, row, floor });
            return (it != overrides_.end()) ? &it->second : nullptr;
        }

        void remove(int col, int row, int floor) {
            overrides_.erase({ col, row, floor });
        }

        void clear() { overrides_.clear(); }
        [[nodiscard]] int count() const { return (int)overrides_.size(); }

        // ─── Entity overrides (by entity ID) ─────
        void set_entity(uint32_t entity_id, const TileCollisionShape& shape) {
            entity_overrides_[entity_id] = shape;
        }

        const TileCollisionShape* get_entity(uint32_t entity_id) const {
            auto it = entity_overrides_.find(entity_id);
            return (it != entity_overrides_.end()) ? &it->second : nullptr;
        }

        void remove_entity(uint32_t entity_id) {
            entity_overrides_.erase(entity_id);
        }

        // Serialization (saved alongside .isomap)
        bool save(const std::string& filepath) const {
            std::ofstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4] = { 'I','S','C','O' }; // overrides
            f.write(magic, 4);
            uint32_t version = 1;
            f.write(reinterpret_cast<const char*>(&version), 4);
            uint32_t count = (uint32_t)overrides_.size();
            f.write(reinterpret_cast<const char*>(&count), 4);

            for (const auto& [key, shape] : overrides_) 
            {
                f.write(reinterpret_cast<const char*>(&key.col), 4);
                f.write(reinterpret_cast<const char*>(&key.row), 4);
                f.write(reinterpret_cast<const char*>(&key.floor), 4);
                uint8_t box_count = (uint8_t)shape.boxes.size();
                f.write(reinterpret_cast<const char*>(&box_count), 1);
                for (const auto& box : shape.boxes) {
                    f.write(reinterpret_cast<const char*>(&box.x), 4);
                    f.write(reinterpret_cast<const char*>(&box.y), 4);
                    f.write(reinterpret_cast<const char*>(&box.width), 4);
                    f.write(reinterpret_cast<const char*>(&box.height), 4);
                    f.write(reinterpret_cast<const char*>(&box.z_height), 4);
                }
            }
            uint32_t ent_count = (uint32_t)entity_overrides_.size();
            f.write(reinterpret_cast<const char*>(&ent_count), 4);
            for (const auto& [id, shape] : entity_overrides_) {
                f.write(reinterpret_cast<const char*>(&id), 4);
                uint8_t box_count = (uint8_t)shape.boxes.size();
                f.write(reinterpret_cast<const char*>(&box_count), 1);
                for (const auto& box : shape.boxes) {
                    f.write(reinterpret_cast<const char*>(&box.x), 4);
                    f.write(reinterpret_cast<const char*>(&box.y), 4);
                    f.write(reinterpret_cast<const char*>(&box.width), 4);
                    f.write(reinterpret_cast<const char*>(&box.height), 4);
                    f.write(reinterpret_cast<const char*>(&box.z_height), 4);
                }
            }
            return f.good();
        }

        bool load(const std::string& filepath) {
            std::ifstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4];
            f.read(magic, 4);
            if (std::memcmp(magic, "ISCO", 4) != 0) return false;

            uint32_t version, count;
            f.read(reinterpret_cast<char*>(&version), 4);
            f.read(reinterpret_cast<char*>(&count), 4);

            overrides_.clear();
            for (uint32_t i = 0; i < count; ++i) {
                InstanceKey key;
                f.read(reinterpret_cast<char*>(&key.col), 4);
                f.read(reinterpret_cast<char*>(&key.row), 4);
                f.read(reinterpret_cast<char*>(&key.floor), 4);

                TileCollisionShape shape;
                uint8_t box_count;
                f.read(reinterpret_cast<char*>(&box_count), 1);
                shape.boxes.resize(box_count);
                for (auto& box : shape.boxes) {
                    f.read(reinterpret_cast<char*>(&box.x), 4);
                    f.read(reinterpret_cast<char*>(&box.y), 4);
                    f.read(reinterpret_cast<char*>(&box.width), 4);
                    f.read(reinterpret_cast<char*>(&box.height), 4);
                    f.read(reinterpret_cast<char*>(&box.z_height), 4);
                }
                overrides_[key] = std::move(shape);
            }
            uint32_t ent_count;
            f.read(reinterpret_cast<char*>(&ent_count), 4);
            for (uint32_t i = 0; i < ent_count; ++i) {
                uint32_t id;
                f.read(reinterpret_cast<char*>(&id), 4);
                TileCollisionShape shape;
                uint8_t box_count;
                f.read(reinterpret_cast<char*>(&box_count), 1);
                shape.boxes.resize(box_count);
                for (auto& box : shape.boxes) {
                    f.read(reinterpret_cast<char*>(&box.x), 4);
                    f.read(reinterpret_cast<char*>(&box.y), 4);
                    f.read(reinterpret_cast<char*>(&box.width), 4);
                    f.read(reinterpret_cast<char*>(&box.height), 4);
                    f.read(reinterpret_cast<char*>(&box.z_height), 4);
                }
                entity_overrides_[id] = std::move(shape);
            }
            return f.good();
        }

    private:
        std::unordered_map<InstanceKey, TileCollisionShape, InstanceKeyHash> overrides_;
        std::unordered_map<uint32_t, TileCollisionShape> entity_overrides_;
    };

    // ─── Resolver: instance override → default → full tile ─

    class CollisionResolver {
    public:
        CollisionResolver() = default;
        CollisionResolver(const TileCollisionDefs* defs,
            const InstanceCollisionOverrides* overrides = nullptr)
            : defs_(defs), overrides_(overrides) {}

        void set_defs(const TileCollisionDefs* d) { defs_ = d; }
        void set_overrides(const InstanceCollisionOverrides* o) { overrides_ = o; }

        /// Resolve collision shape for a specific tile at a specific position
        /// Priority: instance override → tile_id default → single full box
        [[nodiscard]] TileCollisionShape resolve(uint16_t tile_id,
            int col, int row, int floor) const
        {
            // 1) Check instance override
            if (overrides_) {
                const auto* over = overrides_->get(col, row, floor);
                if (over) return *over;
            }

            // 2) Check tile_id default
            if (defs_) {
                const auto* def = defs_->get(tile_id);
                if (def) return *def;
            }

            // 3) Fallback: full tile
            return TileCollisionShape::full();
        }

        [[nodiscard]] const TileCollisionDefs* defs() const { return defs_; }
        [[nodiscard]] const InstanceCollisionOverrides* overrides() const { return overrides_; }

    private:
        const TileCollisionDefs* defs_ = nullptr;
        const InstanceCollisionOverrides* overrides_ = nullptr;
    };

} // namespace iso