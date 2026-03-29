#pragma once
// iso_engine — z-sort: correct isometric draw order
// Handles multi-floor rendering with entities interleaved among tiles
// ─────────────────────────────────────────────────────────────────

#include "core.h"
#include "map.h"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>

namespace iso {

// ─── Renderable type ──────────────────────────

enum class RenderableType : uint8_t {
    TileGround  = 0,   // floor tiles — drawn first
    TileWall    = 1,   // walls — after ground
    TileObject  = 2,   // furniture, props
    Entity      = 3,   // characters, NPCs, items on ground
    TileOverlay = 4,   // roof, ceiling — drawn last on this floor
    Effect      = 5,   // particles, shadows
};

// ─── Renderable item (anything that gets drawn) ─

struct Renderable {
    // Identification
    RenderableType type = RenderableType::TileGround;
    uint32_t       id   = 0;      // entity ID or tile index — your game uses this

    // Sort keys (set by the ZSorter)
    int            floor_level = 0;
    float          iso_depth   = 0.f;   // x + y in world space
    int            sub_priority = 0;    // within same depth, type-based ordering

    // Position & drawing info
    Vec3           world_pos{};          // world position (for entities: feet position)
    Vec2           screen_pos{};         // computed screen position
    TileCoord      tile_coord{};         // grid coordinate (for tiles)

    // Texture / sprite info (opaque to the sort system — your renderer fills these)
    int            texture_id   = -1;
    int            sprite_index = 0;
    float          alpha        = 1.0f;  // for fade-in/out effects

    // Additional rendering hints
    Vec2           origin_offset{};      // sprite origin offset from screen_pos
    float          scale = 1.0f;

    // Comparison for sorting
    [[nodiscard]] auto sort_key() const {
        // Primary: floor level (lower floors drawn first)
        // Secondary: iso_depth (back-to-front: lower depth = further away = drawn first)
        // Tertiary: sub_priority (type-based, to interleave correctly)
        return std::tuple{floor_level, iso_depth, sub_priority};
    }

    bool operator<(const Renderable& o) const {
        return sort_key() < o.sort_key();
    }
};

// ─── View frustum for culling ─────────────────

struct ViewFrustum {
    Vec2 screen_min{};   // top-left of viewport in screen coords
    Vec2 screen_max{};   // bottom-right
    int  min_floor = 0;  // lowest visible floor
    int  max_floor = 0;  // highest visible floor
};

// ─── Z-Sort Engine ────────────────────────────

class ZSorter {
public:
    ZSorter() { renderables_.reserve(4096); }

    /// Clear all renderables for a new frame
    void begin_frame() {
        renderables_.clear();
    }

    /// Submit map tiles for the visible area
    void submit_tiles(const IsoMap& map, const ViewFrustum& frustum, const IsoConfig& cfg) {
        // For each visible floor
        for (int f = frustum.min_floor; f <= frustum.max_floor; ++f) {
            const Floor* floor = map.floor_safe(f);
            if (!floor) continue;

            // Determine tile range from screen bounds
            // Convert screen corners to world to find visible tile range
            // (with generous padding to account for tall sprites)
            Vec3 tl = screen_to_world(frustum.screen_min, cfg, f * cfg.floor_height);
            Vec3 br = screen_to_world(frustum.screen_max, cfg, f * cfg.floor_height);
            Vec3 tr = screen_to_world({frustum.screen_max.x, frustum.screen_min.y}, cfg, f * cfg.floor_height);
            Vec3 bl = screen_to_world({frustum.screen_min.x, frustum.screen_max.y}, cfg, f * cfg.floor_height);

            // Find world-space bounds with padding
            float min_col_f = std::min({tl.x, br.x, tr.x, bl.x}) - 2.f;
            float max_col_f = std::max({tl.x, br.x, tr.x, bl.x}) + 2.f;
            float min_row_f = std::min({tl.y, br.y, tr.y, bl.y}) - 2.f;
            float max_row_f = std::max({tl.y, br.y, tr.y, bl.y}) + 2.f;

            int min_col = std::max(0, static_cast<int>(std::floor(min_col_f)));
            int max_col = std::min(floor->cols() - 1, static_cast<int>(std::ceil(max_col_f)));
            int min_row = std::max(0, static_cast<int>(std::floor(min_row_f)));
            int max_row = std::min(floor->rows() - 1, static_cast<int>(std::ceil(max_row_f)));

            for (int row = min_row; row <= max_row; ++row) {
                for (int col = min_col; col <= max_col; ++col) {
                    TileCoord tc = {col, row, f};
                    Vec3 world = tile_to_world(tc, cfg);
                    Vec2 screen = world_to_screen(world, cfg);

                    // Submit each non-empty layer
                    submit_tile_layer(floor, LayerType::Ground,  tc, world, screen, RenderableType::TileGround);
                    submit_tile_layer(floor, LayerType::Wall,    tc, world, screen, RenderableType::TileWall);
                    submit_tile_layer(floor, LayerType::Object,  tc, world, screen, RenderableType::TileObject);
                    submit_tile_layer(floor, LayerType::Overlay, tc, world, screen, RenderableType::TileOverlay);
                }
            }
        }
    }

    /// Submit an entity (3D character rendered to texture, item, NPC)
    void submit_entity(uint32_t id, Vec3 world_pos, const IsoConfig& cfg,
                       int texture_id = -1, int sprite_index = 0,
                       Vec2 origin_offset = {}, float scale = 1.0f)
    {
        Renderable r;
        r.type         = RenderableType::Entity;
        r.id           = id;
        r.world_pos    = world_pos;
        r.screen_pos   = world_to_screen(world_pos, cfg);
        r.tile_coord   = world_to_tile(world_pos, cfg);
        r.floor_level  = r.tile_coord.floor;
        r.iso_depth    = iso_depth(world_pos);
        r.sub_priority = priority_for_type(RenderableType::Entity);
        r.texture_id   = texture_id;
        r.sprite_index = sprite_index;
        r.origin_offset= origin_offset;
        r.scale        = scale;
        renderables_.push_back(r);
    }

    /// Submit an arbitrary renderable (effects, shadows, etc.)
    void submit(Renderable r) {
        renderables_.push_back(r);
    }

    /// Sort all renderables for correct draw order
    void sort() {
        std::stable_sort(renderables_.begin(), renderables_.end());
    }

    /// Get sorted renderables (call after sort())
    [[nodiscard]] std::span<const Renderable> renderables() const {
        return renderables_;
    }

    [[nodiscard]] std::span<Renderable> renderables() {
        return renderables_;
    }

    [[nodiscard]] size_t count() const { return renderables_.size(); }

    /// Get renderables for a specific floor only
    [[nodiscard]] std::vector<const Renderable*> renderables_for_floor(int floor) const {
        std::vector<const Renderable*> result;
        for (const auto& r : renderables_) {
            if (r.floor_level == floor)
                result.push_back(&r);
        }
        return result;
    }

    // ─── Rendering helpers ────────────────────

    /// Iterate renderables with a callback (use after sort)
    void for_each(std::function<void(const Renderable&)> fn) const {
        for (const auto& r : renderables_)
            fn(r);
    }

    /// Iterate with separate callbacks per type
    struct RenderCallbacks {
        std::function<void(const Renderable&)> on_tile_ground;
        std::function<void(const Renderable&)> on_tile_wall;
        std::function<void(const Renderable&)> on_tile_object;
        std::function<void(const Renderable&)> on_entity;
        std::function<void(const Renderable&)> on_tile_overlay;
        std::function<void(const Renderable&)> on_effect;
    };

    void render(const RenderCallbacks& cb) const {
        for (const auto& r : renderables_) {
            switch (r.type) {
                case RenderableType::TileGround:
                    if (cb.on_tile_ground) cb.on_tile_ground(r);
                    break;
                case RenderableType::TileWall:
                    if (cb.on_tile_wall) cb.on_tile_wall(r);
                    break;
                case RenderableType::TileObject:
                    if (cb.on_tile_object) cb.on_tile_object(r);
                    break;
                case RenderableType::Entity:
                    if (cb.on_entity) cb.on_entity(r);
                    break;
                case RenderableType::TileOverlay:
                    if (cb.on_tile_overlay) cb.on_tile_overlay(r);
                    break;
                case RenderableType::Effect:
                    if (cb.on_effect) cb.on_effect(r);
                    break;
            }
        }
    }

private:
    std::vector<Renderable> renderables_;

    /// Type-based sub-priority for correct interleaving
    static constexpr int priority_for_type(RenderableType type) {
        switch (type) {
            case RenderableType::TileGround:  return 0;
            case RenderableType::Effect:      return 1;  // shadows under entities
            case RenderableType::TileWall:    return 2;
            case RenderableType::TileObject:  return 3;
            case RenderableType::Entity:      return 4;  // entities between walls and overlay
            case RenderableType::TileOverlay: return 5;
            default: return 4;
        }
    }

    void submit_tile_layer(const Floor* floor, LayerType layer, TileCoord tc,
                           Vec3 world, Vec2 screen, RenderableType rtype)
    {
        const TileData& td = floor->tile_safe(layer, tc.col, tc.row);
        if (td.is_empty()) return;

        Renderable r;
        r.type         = rtype;
        r.id           = td.tile_id;
        r.world_pos    = world;
        r.screen_pos   = screen;
        r.tile_coord   = tc;
        r.floor_level  = tc.floor;
        r.iso_depth    = iso_depth(world);
        r.sub_priority = priority_for_type(rtype);
        r.sprite_index = td.tile_id;
        r.alpha        = 1.0f;
        renderables_.push_back(r);
    }
};

// ─── Floor visibility helper ──────────────────
// In PZ-style games, you typically fade/hide floors above the player

struct FloorVisibility {
    int   player_floor    = 0;       // floor the player is on
    int   floors_above    = 1;       // how many floors above player to show
    float fade_alpha      = 0.3f;    // alpha for floors above player

    /// Get alpha for a given floor level
    [[nodiscard]] float get_alpha(int floor_level) const {
        if (floor_level <= player_floor)
            return 1.0f;
        int diff = floor_level - player_floor;
        if (diff > floors_above)
            return 0.0f;    // fully hidden
        return fade_alpha;   // partially visible
    }

    /// Should this floor be rendered at all?
    [[nodiscard]] bool is_visible(int floor_level) const {
        return floor_level <= player_floor + floors_above;
    }
};

} // namespace iso
