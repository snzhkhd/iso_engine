#pragma once
// iso_engine — map renderer
// Collects tiles + entities from ChunkMap, sorts by depth,
// yields DrawItems through callbacks. No RayLib dependency.
// ──────────────────────────────────────────────────────────

#include "core.h"
#include "map.h"
#include "entity.h"
#include "chunk.h"
#include "camera.h"

#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>

namespace iso {

    // ─── Draw item (everything that gets rendered) ─

    struct DrawItem {
        enum class Kind : uint8_t {
            TileGround,
            TileWall,
            TileObject,
            TileOverlay,
            Entity
        };

        Kind     kind = Kind::TileGround;
        float    depth = 0.f;    // sort key (iso_depth + sub-priority)

        // World & screen position
        Vec3     world_pos = {};
        Vec2     screen_pos = {};     // filled by renderer

        // Tile info (valid when kind != Entity)
        TileData tile = {};
        int      col = 0;      // world tile col
        int      row = 0;

        // Entity info (valid when kind == Entity)
        const MapEntity* entity = nullptr;

        // Floor & alpha
        int      floor = 0;
        float    alpha = 1.0f;   // floor-based fade
    };

    // ─── Floor visibility settings ────────────────

    struct FloorVis {
        int   current_floor = 0;
        bool  show_all_opaque = false;    // V key — all floors at 100%
        int   min_floor = -1;
        int   max_floor = 4;
        float below_alpha = 0.3f;     // alpha for floors below current
        float above_alpha = 0.0f;     // alpha for floors above current (0 = hidden)

        [[nodiscard]] int draw_min_floor() const { return min_floor; }
        [[nodiscard]] int draw_max_floor() const {
            return show_all_opaque ? max_floor : current_floor;
        }
        [[nodiscard]] float get_alpha(int floor) const {
            if (show_all_opaque) return 1.0f;
            if (floor == current_floor) return 1.0f;
            if (floor < current_floor)  return below_alpha;
            return above_alpha;
        }
    };

    // ─── LOD settings ─────────────────────────────

    struct RenderLOD {
        float skip_objects_below = 8.f;   // skip objects when tile < Npx on screen
        float skip_walls_below = 4.f;   // skip walls when tile < Npx
        float simple_draw_below = 16.f;  // draw colored diamonds instead of textures
        bool  skip_entities = false; // computed from zoom

        /// Update from current zoom level
        void update(float zoom, int tile_width) {
            float tile_screen = tile_width * zoom;
            skip_entities = tile_screen < skip_objects_below;
        }

        [[nodiscard]] bool should_skip_layer(int layer, float tile_screen_size) const {
            if (layer >= 2 && tile_screen_size < skip_objects_below) return true;
            if (layer == 1 && tile_screen_size < skip_walls_below)   return true;
            return false;
        }

        [[nodiscard]] bool use_simple_draw(float tile_screen_size) const {
            return tile_screen_size < simple_draw_below;
        }
    };

    // ─── Visible area (computed from camera) ──────

    struct VisibleArea {
        int col_min = 0, col_max = 0;
        int row_min = 0, row_max = 0;
        ChunkCoord chunk_min{}, chunk_max{};
        float tile_screen_size = 0.f;  // tile width in screen pixels

        /// Compute from camera and viewport bounds
        static VisibleArea from_camera(const IsoCamera& cam, const IsoConfig& cfg,
            float vp_x, float vp_y, float vp_w, float vp_h)
        {
            VisibleArea va;

            Vec3 tl = cam.screen_to_world({ vp_x, vp_y });
            Vec3 br = cam.screen_to_world({ vp_x + vp_w, vp_y + vp_h });
            Vec3 tr = cam.screen_to_world({ vp_x + vp_w, vp_y });
            Vec3 bl = cam.screen_to_world({ vp_x, vp_y + vp_h });

            va.col_min = (int)std::floor(std::min({ tl.x, br.x, tr.x, bl.x })) - 4;
            va.col_max = (int)std::ceil(std::max({ tl.x, br.x, tr.x, bl.x })) + 4;
            va.row_min = (int)std::floor(std::min({ tl.y, br.y, tr.y, bl.y })) - 4;
            va.row_max = (int)std::ceil(std::max({ tl.y, br.y, tr.y, bl.y })) + 4;

            auto [cmin, cmax] = ChunkMap::chunk_range(
                va.col_min, va.row_min, va.col_max, va.row_max);
            va.chunk_min = cmin;
            va.chunk_max = cmax;
            va.tile_screen_size = cfg.tile_width * cam.zoom();

            return va;
        }
    };

    // ─── Map renderer ─────────────────────────────

    class MapRenderer {
    public:
        MapRenderer() { items_.reserve(8192); }

        /// Collect all visible draw items from the map
        void collect(const ChunkMap& map, const IsoCamera& cam,
            const VisibleArea& va, const FloorVis& fv,
            const RenderLOD& lod = {})
        {
            items_.clear();
            const auto& cfg = map.config();

            for (int f = fv.draw_min_floor(); f <= fv.draw_max_floor(); ++f) {
                float alpha = fv.get_alpha(f);
                if (alpha <= 0.f) continue;

                // ── Ground tiles (no depth sorting needed — always behind) ──
                map.for_each_chunk_in(va.chunk_min, va.chunk_max,
                    [&](ChunkCoord cc, const Chunk& chunk)
                    {
                        int ox = cc.origin_col();
                        int oy = cc.origin_row();

                        // Quick chunk-level screen cull
                        Vec2 chunk_center = cam.world_to_screen(
                            tile_to_world({ ox + CHUNK_SIZE / 2, oy + CHUNK_SIZE / 2, f }, cfg));
                        float chunk_radius = CHUNK_SIZE * va.tile_screen_size;
                        float vp_l = va.col_min * va.tile_screen_size;  // approximate
                        // Use generous bounds
                        if (chunk_center.x < -chunk_radius * 2 || chunk_center.x > cam.viewport_width() + chunk_radius * 2 ||
                            chunk_center.y < -chunk_radius * 2 || chunk_center.y > cam.viewport_height() + chunk_radius * 2)
                            return;

                        for (int lr = 0; lr < CHUNK_SIZE; ++lr) {
                            for (int lc = 0; lc < CHUNK_SIZE; ++lc) {
                                int col = ox + lc, row = oy + lr;

                                // Ground
                                TileData td = chunk.tile_safe(LayerType::Ground, lc, lr, f);
                                if (!td.is_empty()) {
                                    Vec3 world = tile_to_world({ col, row, f }, cfg);
                                    Vec2 sp = cam.world_to_screen(world);

                                    DrawItem item;
                                    item.kind = DrawItem::Kind::TileGround;
                                    item.depth = (float)(col + row);
                                    item.world_pos = world;
                                    item.screen_pos = sp;
                                    item.tile = td;
                                    item.col = col;
                                    item.row = row;
                                    item.floor = f;
                                    item.alpha = alpha;
                                    items_.push_back(item);
                                }

                                // Wall / Object / Overlay (sorted layer)
                                for (int layer = 1; layer < LAYER_COUNT; ++layer) {
                                    if (lod.should_skip_layer(layer, va.tile_screen_size))
                                        continue;

                                    td = chunk.tile_safe(static_cast<LayerType>(layer), lc, lr, f);
                                    if (td.is_empty()) continue;

                                    Vec3 world = tile_to_world({ col, row, f }, cfg);
                                    Vec2 sp = cam.world_to_screen(world);

                                    DrawItem item;
                                    switch (layer) {
                                    case 1: item.kind = DrawItem::Kind::TileWall;    break;
                                    case 2: item.kind = DrawItem::Kind::TileObject;  break;
                                    case 3: item.kind = DrawItem::Kind::TileOverlay; break;
                                    }
                                    item.depth = (float)(col + row) + layer * 0.1f;
                                    item.world_pos = world;
                                    item.screen_pos = sp;
                                    item.tile = td;
                                    item.col = col;
                                    item.row = row;
                                    item.floor = f;
                                    item.alpha = alpha;
                                    items_.push_back(item);
                                }
                            }
                        }
                    });

                // ── Entities ──
                if (!lod.skip_entities) {
                    for (const auto& ent : map.entities().all()) {
                        if (ent.floor != f) continue;
                        Vec3 world = { ent.x, ent.y, f * cfg.floor_height };
                        Vec2 sp = cam.world_to_screen(world);

                        DrawItem item;
                        item.kind = DrawItem::Kind::Entity;
                        item.depth = ent.iso_depth();
                        item.world_pos = world;
                        item.screen_pos = sp;
                        item.entity = &ent;
                        item.floor = f;
                        item.alpha = alpha;
                        items_.push_back(item);
                    }
                }
            }
        }

        /// Sort collected items for correct draw order
        void sort() {
            // Ground first (by depth), then sorted layer (by depth + layer priority)
            std::stable_sort(items_.begin(), items_.end(),
                [](const DrawItem& a, const DrawItem& b)
                {
                    // Primary: floor
                    if (a.floor != b.floor) return a.floor < b.floor;
                    // Secondary: ground always before non-ground on same depth
                    bool a_ground = (a.kind == DrawItem::Kind::TileGround);
                    bool b_ground = (b.kind == DrawItem::Kind::TileGround);
                    if (a_ground != b_ground) return a_ground;
                    // Tertiary: depth (back to front)
                    return a.depth < b.depth;
                });
        }

        /// Iterate all items in draw order
        void for_each(std::function<void(const DrawItem&)> fn) const {
            for (const auto& item : items_)
                fn(item);
        }

        /// Iterate with separate callbacks per type
        struct Callbacks {
            std::function<void(const DrawItem&)> on_ground;
            std::function<void(const DrawItem&)> on_wall;
            std::function<void(const DrawItem&)> on_object;
            std::function<void(const DrawItem&)> on_overlay;
            std::function<void(const DrawItem&)> on_entity;
        };

        void render(const Callbacks& cb) const {
            for (const auto& item : items_) {
                switch (item.kind) {
                case DrawItem::Kind::TileGround:
                    if (cb.on_ground)  cb.on_ground(item);  break;
                case DrawItem::Kind::TileWall:
                    if (cb.on_wall)    cb.on_wall(item);    break;
                case DrawItem::Kind::TileObject:
                    if (cb.on_object)  cb.on_object(item);  break;
                case DrawItem::Kind::TileOverlay:
                    if (cb.on_overlay) cb.on_overlay(item); break;
                case DrawItem::Kind::Entity:
                    if (cb.on_entity)  cb.on_entity(item);  break;
                }
            }
        }

        [[nodiscard]] size_t item_count() const { return items_.size(); }
        [[nodiscard]] std::span<const DrawItem> items() const { return items_; }

    private:
        std::vector<DrawItem> items_;
    };

} // namespace iso