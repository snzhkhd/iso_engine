#pragma once
// iso_engine editor — entity layer (free-positioned objects)
// ──────────────────────────────────────────────────────────

#include "..//iso/iso.h"
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdint>

namespace iso_ed {

    struct MapEntity {
        uint32_t id = 0;      // unique ID
        uint16_t tile_id = 0;      // tile catalog ID (sprite)
        float    x = 0.f;    // world position (float!)
        float    y = 0.f;
        int      floor = 0;      // which floor
        float    rotation = 0.f;    // degrees (0, 90, 180, 270 for iso)
        bool     solid = false;  // blocks movement?
        uint16_t user_data = 0;      // game-specific
        int      z_bias = 0;      // -10..+10, ниже = рисуется раньше (позади)

        [[nodiscard]] iso::Vec3 world_pos(const iso::IsoConfig& cfg) const {
            return { x, y, floor * cfg.floor_height };
        }

        [[nodiscard]] float iso_depth() const {
            return x + y + z_bias * 0.5f;
        }
    };

    class EntityLayer {
    public:
        EntityLayer() = default;

        uint32_t add(uint16_t tile_id, float x, float y, int floor, bool solid = false) {
            MapEntity e;
            e.id = next_id_++;
            e.tile_id = tile_id;
            e.x = x;
            e.y = y;
            e.floor = floor;
            e.solid = solid;
            entities_.push_back(e);
            return e.id;
        }

        void remove(uint32_t id) {
            entities_.erase(
                std::remove_if(entities_.begin(), entities_.end(),
                    [id](const MapEntity& e) { return e.id == id; }),
                entities_.end()
            );
        }

        /// Find entity nearest to world position (within radius)
        MapEntity* pick(float wx, float wy, int floor, float radius = 0.5f) {
            MapEntity* best = nullptr;
            float best_dist = radius * radius;
            for (auto& e : entities_) {
                if (e.floor != floor) continue;
                float dx = e.x - wx;
                float dy = e.y - wy;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_dist) {
                    best_dist = d2;
                    best = &e;
                }
            }
            return best;
        }

        /// Get all entities on a given floor
        [[nodiscard]] std::vector<MapEntity*> on_floor(int floor) {
            std::vector<MapEntity*> result;
            for (auto& e : entities_)
                if (e.floor == floor) result.push_back(&e);
            return result;
        }

        [[nodiscard]] const std::vector<MapEntity>& all() const { return entities_; }
        [[nodiscard]] std::vector<MapEntity>& all() { return entities_; }
        [[nodiscard]] int count() const { return (int)entities_.size(); }

        void clear() { entities_.clear(); next_id_ = 1; }

        // ─── Serialization ────────────────────────

        bool save(const std::string& filepath) const 
        {
            std::ofstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4] = { 'I','S','O','E' };
            f.write(magic, 4);
            uint32_t version = 1;
            f.write(reinterpret_cast<const char*>(&version), 4);
            uint32_t count = (uint32_t)entities_.size();
            f.write(reinterpret_cast<const char*>(&count), 4);

            for (const auto& e : entities_) {
                f.write(reinterpret_cast<const char*>(&e.id), 4);
                f.write(reinterpret_cast<const char*>(&e.tile_id), 2);
                f.write(reinterpret_cast<const char*>(&e.x), 4);
                f.write(reinterpret_cast<const char*>(&e.y), 4);
                f.write(reinterpret_cast<const char*>(&e.floor), 4);
                f.write(reinterpret_cast<const char*>(&e.rotation), 4);
                uint8_t flags = e.solid ? 1 : 0;
                f.write(reinterpret_cast<const char*>(&flags), 1);
                f.write(reinterpret_cast<const char*>(&e.user_data), 2);
                f.write(reinterpret_cast<const char*>(&e.z_bias), 4);
            }
            return f.good();
        }

        bool load(const std::string& filepath) {
            std::ifstream f(filepath, std::ios::binary);
            if (!f.is_open()) return false;

            char magic[4];
            f.read(magic, 4);
            if (memcmp(magic, "ISOE", 4) != 0) return false;

            uint32_t version;
            f.read(reinterpret_cast<char*>(&version), 4);
            if (version != 1) return false;

            uint32_t count;
            f.read(reinterpret_cast<char*>(&count), 4);

            entities_.clear();
            entities_.reserve(count);
            uint32_t max_id = 0;

            for (uint32_t i = 0; i < count; ++i) {
                MapEntity e;
                f.read(reinterpret_cast<char*>(&e.id), 4);
                f.read(reinterpret_cast<char*>(&e.tile_id), 2);
                f.read(reinterpret_cast<char*>(&e.x), 4);
                f.read(reinterpret_cast<char*>(&e.y), 4);
                f.read(reinterpret_cast<char*>(&e.floor), 4);
                f.read(reinterpret_cast<char*>(&e.rotation), 4);
                uint8_t flags;
                f.read(reinterpret_cast<char*>(&flags), 1);
                e.solid = (flags & 1) != 0;
                f.read(reinterpret_cast<char*>(&e.user_data), 2);
                f.read(reinterpret_cast<char*>(&e.z_bias), 4);
                entities_.push_back(e);
                if (e.id > max_id) max_id = e.id;
            }
            next_id_ = max_id + 1;
            return f.good();
        }

    private:
        std::vector<MapEntity> entities_;
        uint32_t next_id_ = 1;
    };

} // namespace iso_ed