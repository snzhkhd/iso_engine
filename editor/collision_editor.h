#pragma once
// Collision editor — visual collision box editing
// Embedded in the tile map editor
// ────────────────────────────────────────────────

#include "raylib.h"
#include "..//iso/iso.h"
#include "tile_catalog.h"
#include "editor_ui.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace iso_ed {

    class CollisionEditor {
    public:
        bool active = false;

        // Selected tile for editing
        uint16_t  selected_tile_id = 0;
        int       selected_box = -1;    // which box is selected (-1 = none)
        int       drag_handle = -1;     // 0=move, 1..4=resize corners

        // Drag state
        bool   dragging = false;
        float  drag_start_mx = 0, drag_start_my = 0;
        iso::CollisionBox drag_start_box{};

        // View
        float preview_zoom = 2.0f;

        void toggle() { active = !active; }

        void draw(iso::TileCollisionDefs& defs, const TileCatalog& catalog, const iso::IsoConfig& cfg,
            float panel_x, float panel_y, float panel_w, float panel_h)
        {
            using namespace iso_ed::ui;
            cfg_ = cfg;
            Rectangle panel = { panel_x, panel_y, panel_w, panel_h };
            draw_panel(panel, { 35, 35, 42, 255 });

            float x = panel_x + 8;
            float y = panel_y + 8;
            float w = panel_w - 16;

            draw_label("COLLISION EDITOR", (int)x, (int)y, 15, theme.text_bright);
            y += 22;

            if (selected_tile_id == 0) {
                draw_label("Click a tile in the palette", (int)x, (int)y, 12, theme.text_dim);
                draw_label("to edit its collision", (int)x, (int)y + 16, 12, theme.text_dim);
                return;
            }

            // ── Tile info ─────────────────────────
            const auto* tile = catalog.get(selected_tile_id);
            draw_label(TextFormat("Tile: %d %s", selected_tile_id,
                tile ? tile->name.c_str() : "?"),
                (int)x, (int)y, 12, theme.text);
            y += 18;

            // ── Preview area (tile sprite + collision boxes) ──

            float preview_x = x;
            float preview_y = y;
            float preview_w = w;
            float preview_h = 150;

            DrawRectangle((int)preview_x, (int)preview_y,
                (int)preview_w, (int)preview_h, { 25, 25, 30, 255 });
            DrawRectangleLinesEx({ preview_x, preview_y, preview_w, preview_h },
                1.f, theme.border);

            // Clip sprite to preview area
            BeginScissorMode((int)preview_x, (int)preview_y, (int)preview_w, (int)preview_h);

            if (tile && tile->loaded) {
                // Fit tile sprite into preview area
                float scale = std::min(
                    (preview_w - 16) / tile->src_width,
                    (preview_h - 16) / tile->src_height
                );
                float tw = tile->src_width * scale;
                float th = tile->src_height * scale;
                float tx = preview_x + (preview_w - tw) * 0.5f;
                float ty = preview_y + (preview_h - th) * 0.5f;

                // Footprint = square area at bottom, sized proportionally to tile
                float foot_w = tw;  // full width
                float foot_h = tw * ((float)cfg_.tile_height / cfg_.tile_width); // proportional height
                float foot_x = tx;
                float foot_y = ty + th - foot_h;  // bottom of sprite

                // Draw tile sprite
                Rectangle src = { 0, 0, (float)tile->src_width, (float)tile->src_height };
                Rectangle dst = { tx, ty, tw, th };
                DrawTexturePro(tile->texture, src, dst, { 0, 0 }, 0.f, { 255, 255, 255, 180 });

                // Draw collision boxes
                auto* shape = defs.get(selected_tile_id);
                if (shape) {
                    for (int i = 0; i < (int)shape->boxes.size(); ++i) {
                        const auto& box = shape->boxes[i];
                        if (box.is_none()) continue;

                       

                        float bx = foot_x + box.x * foot_w;
                        float by = foot_y + box.y * foot_h;
                        float bw = box.width * foot_w;
                        float bh = box.height * foot_h;

                        bool is_selected = (i == selected_box);
                        Color fill = is_selected ? Color{ 255, 200, 0, 50 } : Color{ 255, 60, 60, 40 };
                        Color border = is_selected ? YELLOW : RED;

                        DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, fill);
                        DrawRectangleLinesEx({ bx, by, bw, bh }, is_selected ? 2.f : 1.f, border);

                        // Resize handles (corners)
                        if (is_selected) {
                            float hs = 5.f; // handle size
                            DrawRectangle((int)(bx - hs), (int)(by - hs), (int)(hs * 2), (int)(hs * 2), YELLOW);
                            DrawRectangle((int)(bx + bw - hs), (int)(by - hs), (int)(hs * 2), (int)(hs * 2), YELLOW);
                            DrawRectangle((int)(bx - hs), (int)(by + bh - hs), (int)(hs * 2), (int)(hs * 2), YELLOW);
                            DrawRectangle((int)(bx + bw - hs), (int)(by + bh - hs), (int)(hs * 2), (int)(hs * 2), YELLOW);
                        }

                        // Box index label
                        DrawText(TextFormat("%d", i), (int)(bx + 3), (int)(by + 2), 10, border);
                    }
                }
            }

            y += preview_h + 8;
            EndScissorMode();
            auto* shape = defs.get(selected_tile_id);

            // ── Top-down collision view (interactive) ──
            float grid_size = std::min(preview_w - 8, 180.f);
            float grid_x = preview_x + (preview_w - grid_size) * 0.5f;
            float grid_y = y;
            y += grid_size + 8;

            // Background
            DrawRectangle((int)grid_x, (int)grid_y, (int)grid_size, (int)grid_size, { 20, 20, 25, 255 });
            // Grid lines (4x4)
            for (int g = 1; g < 4; ++g) {
                float gp = grid_size * g / 4.f;
                DrawLine((int)(grid_x + gp), (int)grid_y,
                    (int)(grid_x + gp), (int)(grid_y + grid_size), { 40, 40, 50, 255 });
                DrawLine((int)grid_x, (int)(grid_y + gp),
                    (int)(grid_x + grid_size), (int)(grid_y + gp), { 40, 40, 50, 255 });
            }
            DrawRectangleLinesEx({ grid_x, grid_y, grid_size, grid_size }, 1.f, { 80, 80, 90, 255 });
            draw_label("Top-down 1x1 (drag to edit)", (int)grid_x, (int)(grid_y - 12), 9, theme.text_dim);

            // Draw boxes
            if (shape) {
                for (int i = 0; i < (int)shape->boxes.size(); ++i) {
                    const auto& box = shape->boxes[i];
                    if (box.is_none()) continue;

                    float bx = grid_x + box.x * grid_size;
                    float by = grid_y + box.y * grid_size;
                    float bw = box.width * grid_size;
                    float bh = box.height * grid_size;

                    bool is_sel = (i == selected_box);
                    DrawRectangle((int)bx, (int)by, (int)bw, (int)bh,
                        is_sel ? Color{ 255, 200, 0, 60 } : Color{ 255, 60, 60, 50 });
                    DrawRectangleLinesEx({ bx, by, bw, bh },
                        is_sel ? 2.f : 1.f,
                        is_sel ? YELLOW : RED);

                    // Resize handles
                    if (is_sel) {
                        float hs = 4.f;
                        Color hc = { 255, 255, 100, 255 };
                        DrawRectangle((int)(bx - hs), (int)(by - hs), (int)(hs * 2), (int)(hs * 2), hc);
                        DrawRectangle((int)(bx + bw - hs), (int)(by - hs), (int)(hs * 2), (int)(hs * 2), hc);
                        DrawRectangle((int)(bx - hs), (int)(by + bh - hs), (int)(hs * 2), (int)(hs * 2), hc);
                        DrawRectangle((int)(bx + bw - hs), (int)(by + bh - hs), (int)(hs * 2), (int)(hs * 2), hc);
                    }

                    DrawText(TextFormat("%d", i), (int)(bx + 3), (int)(by + 2), 10,
                        is_sel ? YELLOW : RED);
                }
            }

            // ── Mouse interaction in top-down grid ──
            Vector2 mp = GetMousePosition();
            bool in_grid = CheckCollisionPointRec(mp, { grid_x, grid_y, grid_size, grid_size });

            if (in_grid && shape) {
                float mx_norm = (mp.x - grid_x) / grid_size;
                float my_norm = (mp.y - grid_y) / grid_size;

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    selected_box = -1;
                    drag_handle = -1;
                    float hs_norm = 5.f / grid_size;

                    for (int i = (int)shape->boxes.size() - 1; i >= 0; --i) {
                        const auto& box = shape->boxes[i];

                        if (near(mx_norm, box.x, hs_norm) && near(my_norm, box.y, hs_norm)) {
                            selected_box = i; drag_handle = 1; break;
                        }
                        if (near(mx_norm, box.x + box.width, hs_norm) && near(my_norm, box.y, hs_norm)) {
                            selected_box = i; drag_handle = 2; break;
                        }
                        if (near(mx_norm, box.x, hs_norm) && near(my_norm, box.y + box.height, hs_norm)) {
                            selected_box = i; drag_handle = 3; break;
                        }
                        if (near(mx_norm, box.x + box.width, hs_norm) && near(my_norm, box.y + box.height, hs_norm)) {
                            selected_box = i; drag_handle = 4; break;
                        }

                        if (mx_norm >= box.x && mx_norm <= box.x + box.width &&
                            my_norm >= box.y && my_norm <= box.y + box.height) {
                            selected_box = i; drag_handle = 0; break;
                        }
                    }

                    if (selected_box >= 0) {
                        dragging = true;
                        drag_start_mx = mx_norm;
                        drag_start_my = my_norm;
                        drag_start_box = shape->boxes[selected_box];
                    }
                }

                if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected_box >= 0) {
                    float dx = mx_norm - drag_start_mx;
                    float dy = my_norm - drag_start_my;
                    auto& box = const_cast<iso::TileCollisionShape*>(shape)->boxes[selected_box];

                    float snap = 0.05f;
                    auto snap_val = [snap](float v) { return std::round(v / snap) * snap; };

                    switch (drag_handle) {
                    case 0:
                        box.x = snap_val(std::clamp(drag_start_box.x + dx, 0.f, 1.f - box.width));
                        box.y = snap_val(std::clamp(drag_start_box.y + dy, 0.f, 1.f - box.height));
                        break;
                    case 1:
                        box.x = snap_val(std::clamp(drag_start_box.x + dx, 0.f, drag_start_box.x + drag_start_box.width - 0.05f));
                        box.y = snap_val(std::clamp(drag_start_box.y + dy, 0.f, drag_start_box.y + drag_start_box.height - 0.05f));
                        box.width = snap_val(drag_start_box.x + drag_start_box.width - box.x);
                        box.height = snap_val(drag_start_box.y + drag_start_box.height - box.y);
                        break;
                    case 2:
                        box.width = snap_val(std::clamp(drag_start_box.width + dx, 0.05f, 1.f - box.x));
                        box.y = snap_val(std::clamp(drag_start_box.y + dy, 0.f, drag_start_box.y + drag_start_box.height - 0.05f));
                        box.height = snap_val(drag_start_box.y + drag_start_box.height - box.y);
                        break;
                    case 3:
                        box.x = snap_val(std::clamp(drag_start_box.x + dx, 0.f, drag_start_box.x + drag_start_box.width - 0.05f));
                        box.width = snap_val(drag_start_box.x + drag_start_box.width - box.x);
                        box.height = snap_val(std::clamp(drag_start_box.height + dy, 0.05f, 1.f - box.y));
                        break;
                    case 4:
                        box.width = snap_val(std::clamp(drag_start_box.width + dx, 0.05f, 1.f - box.x));
                        box.height = snap_val(std::clamp(drag_start_box.height + dy, 0.05f, 1.f - box.y));
                        break;
                    }
                }

                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    dragging = false;
                }
            }
            // ── Presets ───────────────────────────
            draw_label("Presets:", (int)x, (int)y, 13, theme.text_bright);
            y += 18;

            // Scrollable preset list (3 columns)
            for (int i = 0; i < iso::COLLISION_PRESET_COUNT; ++i) {
                float bw = (w - 8) / 3.f;
                Rectangle btn = { x + (i % 3) * (bw + 2), y + (i / 3) * 22.f, bw, 20 };
                if (button(btn, iso::COLLISION_PRESETS[i].name, false, 9)) {
                    auto shape = iso::COLLISION_PRESETS[i].create();
                    defs.set(selected_tile_id, shape);
                    selected_box = -1;
                }
            }
            y += ((iso::COLLISION_PRESET_COUNT + 2) / 3) * 22.f + 10;

            // ── Box management ────────────────────
            draw_label("Boxes:", (int)x, (int)y, 13, theme.text_bright);
            y += 18;

            
            int box_count = shape ? (int)shape->boxes.size() : 0;


            // Add box button
            float btn_w = (w - 4) * 0.5f;
            if (button({ x, y, btn_w, 22 }, "+ Add box", false, 11)) {
                iso::TileCollisionShape* s = const_cast<iso::TileCollisionShape*>(shape);
                if (!s) {
                    defs.set(selected_tile_id, iso::TileCollisionShape::none());
                    s = const_cast<iso::TileCollisionShape*>(defs.get(selected_tile_id));
                }
                s->boxes.push_back({ 0.25f, 0.25f, 0.5f, 0.5f, 1.f });
                selected_box = (int)s->boxes.size() - 1;
            }

            // Remove selected box
            if (button({ x + btn_w + 4, y, btn_w, 22 }, "- Remove", selected_box >= 0, 11)) {
                if (shape && selected_box >= 0 && selected_box < box_count) {
                    auto* s = const_cast<iso::TileCollisionShape*>(shape);
                    s->boxes.erase(s->boxes.begin() + selected_box);
                    selected_box = -1;
                }
            }
            y += 28;

            // ── Selected box values ───────────────
            if (shape && selected_box >= 0 && selected_box < (int)shape->boxes.size()) {
                const auto& box = shape->boxes[selected_box];
                draw_label(TextFormat("Box [%d]:", selected_box), (int)x, (int)y, 12, YELLOW);
                y += 16;
                draw_label(TextFormat("  x: %.2f  y: %.2f", box.x, box.y),
                    (int)x, (int)y, 11, theme.text); y += 14;
                draw_label(TextFormat("  w: %.2f  h: %.2f", box.width, box.height),
                    (int)x, (int)y, 11, theme.text); y += 14;
                draw_label(TextFormat("  z_height: %.2f", box.z_height),
                    (int)x, (int)y, 11, theme.text); y += 18;

                // Z-height spinner
                auto* s = const_cast<iso::TileCollisionShape*>(shape);
                float zh = s->boxes[selected_box].z_height;
                Rectangle zh_r = { x, y, w, 22 };
                if (button({ x, y, 30, 22 }, "-", false, 14)) {
                    s->boxes[selected_box].z_height = std::max(0.1f, zh - 0.1f);
                }
                draw_label(TextFormat("Z-Height: %.1f", s->boxes[selected_box].z_height),
                    (int)(x + 36), (int)(y + 4), 11, theme.text);
                if (button({ x + w - 30, y, 30, 22 }, "+", false, 14)) {
                    s->boxes[selected_box].z_height = std::min(3.f, zh + 0.1f);
                }
                y += 28;
            }
            else if (box_count == 0) {
                draw_label("No collision (passable)", (int)x, (int)y, 12, theme.text_dim);
                y += 20;
            }
            else {
                draw_label(TextFormat("%d box(es) - click to select", box_count),
                    (int)x, (int)y, 12, theme.text_dim);
                y += 20;
            }

            // ── Delete all ────────────────────────
            y += 4;
            if (button({ x, y, w, 22 }, "Clear all (no collision)", false, 11)) {
                defs.set(selected_tile_id, iso::TileCollisionShape::none());
                selected_box = -1;
            }
            y += 28;

            // ── Total defs count ──────────────────
            draw_label(TextFormat("%d tile defs total", defs.count()),
                (int)x, (int)y, 11, theme.text_dim);
        }

        // ── Draw collision outlines on map ────────

        void draw_on_map(const iso::ChunkMap& map, const iso::TileCollisionDefs& defs,
            const iso::IsoCamera& camera, const iso::IsoConfig& cfg,
            int current_floor)
        {
            if (!active) return;

            auto va = iso::VisibleArea::from_camera(camera, cfg, 0, 0,
                (float)camera.viewport_width(), (float)camera.viewport_height());

            // Tile collisions
            map.for_each_chunk_in(va.chunk_min, va.chunk_max,
                [&](iso::ChunkCoord cc, const iso::Chunk& chunk) {
                    int ox = cc.origin_col(), oy = cc.origin_row();
                    for (int lr = 0; lr < iso::CHUNK_SIZE; ++lr) {
                        for (int lc = 0; lc < iso::CHUNK_SIZE; ++lc) {
                            for (int layer = 1; layer < iso::LAYER_COUNT; ++layer) {
                                auto t = chunk.tile_safe(
                                    static_cast<iso::LayerType>(layer), lc, lr, current_floor);
                                if (!t.is_solid() && !defs.has(t.tile_id)) continue;

                                int col = ox + lc, row = oy + lr;
                                const auto* shape = defs.get(t.tile_id);
                                if (!shape || shape->is_none()) {
                                    // Default full tile
                                    draw_box_on_map(camera, cfg, col, row, current_floor,
                                        { 0,0,1,1,1 }, t.tile_id == selected_tile_id);
                                }
                                else {
                                    for (const auto& box : shape->boxes) {
                                        if (box.is_none()) continue;
                                        draw_box_on_map(camera, cfg, col, row, current_floor,
                                            box, t.tile_id == selected_tile_id);
                                    }
                                }
                            }
                        }
                    }
                });

            // Entity collisions
            for (const auto& ent : map.entities().all()) {
                if (ent.floor != current_floor) continue;
                const auto* shape = defs.get(ent.tile_id);
                if (!shape || shape->is_none()) continue;

                iso::Vec2 sp = camera.world_to_screen({ ent.x, ent.y, 0 });
                if (!camera.is_on_screen(sp, 200.f)) continue;

                bool sel = (ent.tile_id == selected_tile_id);
                for (const auto& box : shape->boxes) {
                    if (box.is_none()) continue;
                    float ex0 = ent.x + box.x, ey0 = ent.y + box.y;
                    float ex1 = ex0 + box.width, ey1 = ey0 + box.height;
                    draw_quad(camera, ex0, ey0, ex1, ey1, sel);
                }
            }
        }

    private:
        iso::IsoConfig cfg_;
        static bool near(float a, float b, float eps) {
            return std::abs(a - b) < eps;
        }

        void draw_box_on_map(const iso::IsoCamera& cam, const iso::IsoConfig& cfg,
            int col, int row, int floor,
            const iso::CollisionBox& box, bool highlight)
        {
            float x0 = col + box.x, y0 = row + box.y;
            float x1 = x0 + box.width, y1 = y0 + box.height;
            draw_quad(cam, x0, y0, x1, y1, highlight);
        }

        void draw_quad(const iso::IsoCamera& cam, float x0, float y0, float x1, float y1,
            bool highlight)
        {
            iso::Vec2 p0 = cam.world_to_screen({ x0, y0, 0 });
            iso::Vec2 p1 = cam.world_to_screen({ x1, y0, 0 });
            iso::Vec2 p2 = cam.world_to_screen({ x1, y1, 0 });
            iso::Vec2 p3 = cam.world_to_screen({ x0, y1, 0 });
            Color c = highlight ? YELLOW : Color{ 255, 80, 80, 200 };
            DrawLine((int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y, c);
            DrawLine((int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y, c);
            DrawLine((int)p2.x, (int)p2.y, (int)p3.x, (int)p3.y, c);
            DrawLine((int)p3.x, (int)p3.y, (int)p0.x, (int)p0.y, c);
        }
    };

} // namespace iso_ed