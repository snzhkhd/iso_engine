#pragma once
// iso_engine — entity system (free-positioned objects)
// Shared by editor and game
// ─────────────────────────────────────────────────────

#include "core.h"

#include <vector>
#include <algorithm>
#include <cstdint>

namespace iso {

// ─── Map entity (free-float positioned object) ─

struct MapEntity {
    uint32_t id        = 0;      // unique ID
    uint16_t tile_id   = 0;      // sprite / tile catalog ID
    float    x         = 0.f;    // world position (continuous)
    float    y         = 0.f;
    int      floor     = 0;      // floor level
    int      z_bias    = 0;      // draw order adjustment (-10..+10)
    float    rotation  = 0.f;    // degrees (0, 90, 180, 270)
    uint8_t  flags     = 0;      // bit 0 = solid
    uint16_t user_data = 0;      // game-specific

    [[nodiscard]] bool is_solid() const { return (flags & 1) != 0; }
    void set_solid(bool v) { flags = v ? (flags | 1) : (flags & ~1); }

    [[nodiscard]] Vec3 world_pos(const IsoConfig& cfg) const {
        return {x, y, floor * cfg.floor_height};
    }

    [[nodiscard]] float iso_depth() const {
        return x + y + z_bias * 0.5f;
    }

    /// Which chunk this entity belongs to (for chunk system)
    [[nodiscard]] Vec2i chunk_coord(int chunk_size) const {
        int cx = (x >= 0) ? (int)(x) / chunk_size : (int)(x) / chunk_size - 1;
        int cy = (y >= 0) ? (int)(y) / chunk_size : (int)(y) / chunk_size - 1;
        return {cx, cy};
    }
};

// ─── Entity layer (collection with ID management) ─

class EntityLayer {
public:
    EntityLayer() = default;

    /// Add entity, returns its ID
    uint32_t add(uint16_t tile_id, float x, float y, int floor,
                 bool solid = false, int z_bias = 0)
    {
        MapEntity e;
        e.id      = next_id_++;
        e.tile_id = tile_id;
        e.x       = x;
        e.y       = y;
        e.floor   = floor;
        e.z_bias  = z_bias;
        e.set_solid(solid);
        entities_.push_back(e);
        return e.id;
    }

    /// Add pre-built entity (keeps its ID, updates next_id)
    void add_raw(const MapEntity& e) {
        entities_.push_back(e);
        if (e.id >= next_id_) next_id_ = e.id + 1;
    }

    void remove(uint32_t id) {
        entities_.erase(
            std::remove_if(entities_.begin(), entities_.end(),
                [id](const MapEntity& e) { return e.id == id; }),
            entities_.end()
        );
    }

    /// Find entity nearest to position within radius
    [[nodiscard]] MapEntity* pick(float wx, float wy, int floor, float radius = 0.5f) {
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

    [[nodiscard]] const MapEntity* pick(float wx, float wy, int floor, float radius = 0.5f) const {
        const MapEntity* best = nullptr;
        float best_dist = radius * radius;
        for (const auto& e : entities_) {
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

    /// Get entities on a floor (pointers into internal storage)
    [[nodiscard]] std::vector<MapEntity*> on_floor(int floor) {
        std::vector<MapEntity*> result;
        for (auto& e : entities_)
            if (e.floor == floor) result.push_back(&e);
        return result;
    }

    /// Get entities within a world-space rectangle
    [[nodiscard]] std::vector<MapEntity*> in_rect(float x0, float y0, float x1, float y1, int floor) {
        std::vector<MapEntity*> result;
        for (auto& e : entities_) {
            if (e.floor != floor) continue;
            if (e.x >= x0 && e.x < x1 && e.y >= y0 && e.y < y1)
                result.push_back(&e);
        }
        return result;
    }

    [[nodiscard]] const std::vector<MapEntity>& all() const { return entities_; }
    [[nodiscard]] std::vector<MapEntity>& all() { return entities_; }
    [[nodiscard]] int count() const { return (int)entities_.size(); }
    [[nodiscard]] uint32_t next_id() const { return next_id_; }

    void clear() { entities_.clear(); next_id_ = 1; }

private:
    std::vector<MapEntity> entities_;
    uint32_t next_id_ = 1;
};

} // namespace iso
