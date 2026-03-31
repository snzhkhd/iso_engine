#pragma once
// iso_engine — entity system (free-positioned objects)
// Shared by editor and game
// ─────────────────────────────────────────────────────

#include "core.h"

#include <vector>
#include <algorithm>
#include <cstdint>

namespace iso {

    // ─── Entity type (static vs dynamic) ──────────

    enum class EntityType : uint8_t {
        Static = 0,   // furniture, decoration — saved in map file, never changes at runtime
        Dynamic = 1,   // items on ground, doors — saved in map, can change state
        Networked = 2,   // players, NPCs, projectiles — NOT saved, synced over network
    };

    // ─── Entity flags ─────────────────────────────

    enum class EntityFlags : uint8_t {
        None = 0,
        Solid = 1 << 0,   // blocks movement
        Interactable = 1 << 1, // can be used/opened/picked up
        Visible = 1 << 2,   // currently visible (for network cull)
        Dirty = 1 << 3,   // state changed, needs network sync
    };

    constexpr EntityFlags operator|(EntityFlags a, EntityFlags b) {
        return static_cast<EntityFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
    constexpr EntityFlags operator&(EntityFlags a, EntityFlags b) {
        return static_cast<EntityFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }
    constexpr bool has_eflag(EntityFlags val, EntityFlags flag) {
        return (static_cast<uint8_t>(val) & static_cast<uint8_t>(flag)) != 0;
    }

    // ─── Map entity (free-float positioned object) ─

    struct MapEntity {
        uint32_t    id = 0;      // unique ID (must be unique across all types)
        uint16_t    tile_id = 0;      // sprite / tile catalog ID
        EntityType  type = EntityType::Static;
        float       x = 0.f;    // world position (continuous)
        float       y = 0.f;
        int         floor = 0;      // floor level
        int         z_bias = 0;      // draw order adjustment (-10..+10)
        float       rotation = 0.f;    // degrees (0, 90, 180, 270)
        EntityFlags flags = EntityFlags::None;
        uint16_t    user_data = 0;      // game-specific (item type, door state, etc.)

        [[nodiscard]] bool is_solid()       const { return has_eflag(flags, EntityFlags::Solid); }
        [[nodiscard]] bool is_interactable()const { return has_eflag(flags, EntityFlags::Interactable); }
        [[nodiscard]] bool is_dirty()       const { return has_eflag(flags, EntityFlags::Dirty); }
        [[nodiscard]] bool is_networked()   const { return type == EntityType::Networked; }
        [[nodiscard]] bool is_static()      const { return type == EntityType::Static; }

        void set_solid(bool v) {
            flags = v ? (flags | EntityFlags::Solid) : static_cast<EntityFlags>(
                static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(EntityFlags::Solid));
        }
        void mark_dirty() {
            flags = flags | EntityFlags::Dirty;
        }
        void clear_dirty() {
            flags = static_cast<EntityFlags>(
                static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(EntityFlags::Dirty));
        }

        [[nodiscard]] Vec3 world_pos(const IsoConfig& cfg) const {
            return { x, y, floor * cfg.floor_height };
        }

        [[nodiscard]] float iso_depth() const {
            return x + y + z_bias * 0.5f;
        }

        /// Which chunk this entity belongs to (for chunk system)
        [[nodiscard]] Vec2i chunk_coord(int chunk_size) const {
            int cx = (x >= 0) ? (int)(x) / chunk_size : (int)(x) / chunk_size - 1;
            int cy = (y >= 0) ? (int)(y) / chunk_size : (int)(y) / chunk_size - 1;
            return { cx, cy };
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
            e.id = next_id_++;
            e.tile_id = tile_id;
            e.x = x;
            e.y = y;
            e.floor = floor;
            e.z_bias = z_bias;
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

        // ─── Type-filtered access ─────────────────

        /// Get by ID (returns nullptr if not found)
        [[nodiscard]] MapEntity* find(uint32_t id) {
            for (auto& e : entities_)
                if (e.id == id) return &e;
            return nullptr;
        }

        [[nodiscard]] const MapEntity* find(uint32_t id) const {
            for (const auto& e : entities_)
                if (e.id == id) return &e;
            return nullptr;
        }

        /// Get all entities of a specific type
        [[nodiscard]] std::vector<MapEntity*> by_type(EntityType type) {
            std::vector<MapEntity*> result;
            for (auto& e : entities_)
                if (e.type == type) result.push_back(&e);
            return result;
        }

        /// Get all dirty (changed) entities — for network sync
        [[nodiscard]] std::vector<MapEntity*> dirty() {
            std::vector<MapEntity*> result;
            for (auto& e : entities_)
                if (e.is_dirty()) result.push_back(&e);
            return result;
        }

        /// Clear dirty flag on all entities
        void clear_all_dirty() {
            for (auto& e : entities_)
                e.clear_dirty();
        }

        /// Remove all networked entities (on disconnect / cleanup)
        void remove_networked() {
            entities_.erase(
                std::remove_if(entities_.begin(), entities_.end(),
                    [](const MapEntity& e) { return e.is_networked(); }),
                entities_.end()
            );
        }

        /// Update a networked entity (from server packet). Adds if not found.
        void update_or_add(const MapEntity& incoming) {
            MapEntity* existing = find(incoming.id);
            if (existing) {
                *existing = incoming;
            }
            else {
                add_raw(incoming);
            }
        }

        /// Count entities by type
        [[nodiscard]] int count_type(EntityType type) const {
            int n = 0;
            for (const auto& e : entities_)
                if (e.type == type) n++;
            return n;
        }

    private:
        std::vector<MapEntity> entities_;
        uint32_t next_id_ = 1;
    };

} // namespace iso