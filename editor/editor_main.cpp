// ═══════════════════════════════════════════════════════════════
//  iso_engine — Isometric Tile Map Editor
//  Build with RayLib 5.x + MSVC C++20
//
//  Controls:
//    LMB         — place tile / use tool
//    RMB         — erase tile
//    MMB drag    — pan camera
//    Scroll      — zoom
//    1-4         — select layer (Ground/Wall/Object/Overlay)
//    PgUp/PgDn   — change floor
//    Tab         — toggle grid
//    G           — toggle ghost (tile preview)
//    Ctrl+S      — save map
//    Ctrl+O      — open map  (via file drop)
//    Ctrl+Z      — undo
//    Ctrl+Y      — redo
//    Space       — eyedropper (pick tile under cursor)
//    F           — flood fill mode
//    R           — rectangle fill mode
//    B           — brush mode (default)
// ═══════════════════════════════════════════════════════════════
#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "..//iso/iso_raylib.h"
#include "tile_catalog.h"

#include "editor_ui.h"
#include "entity_layer.h"

#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <unordered_set>
// ─── Constants ────────────────────────────────

static constexpr int MAX_FILL = 4096;

static constexpr int SCREEN_W = 1600;
static constexpr int SCREEN_H = 900;

static constexpr int PALETTE_WIDTH = 260;     // left panel width
static constexpr int TOOLBAR_HEIGHT = 36;     // top toolbar height
static constexpr int STATUSBAR_HEIGHT = 26;   // bottom status bar
static constexpr int PROPS_WIDTH = 220;       // right panel (tile properties)

// ─── Editor tool ──────────────────────────────

enum class Tool : int {
    Brush     = 0,
    Eraser    = 1,
    Eyedropper= 2,
    RectFill  = 3,
    FloodFill = 4,
    Count     = 5
};

static const char* tool_names[] = {"Brush[B]", "Eraser[E]", "Pick[Space]", "Rect[R]", "Fill[F]"};
static const char* layer_names[] = {"Ground", "Wall", "Object", "Overlay"};
static const char* category_filter_names[] = {"All", "Ground", "Wall", "Cube", "Object", "Roof", "Tree", "Other"};

// ─── Undo entry ───────────────────────────────

struct UndoEntry 
{
    bool is_entity = false;

    iso::TileCoord coord;
    iso::LayerType layer;
    iso::TileData  old_data;
    iso::TileData  new_data;

    // Entity undo
    enum class EntityAction : uint8_t { Add, Remove, Modify };
    EntityAction   entity_action = EntityAction::Add;
    iso::MapEntity entity_data{};
};

struct UndoGroup {
    std::vector<UndoEntry> entries;
};

// ─── Editor state ─────────────────────────────

struct EditorState {
    // Map
    iso::ChunkMap map;

    iso::IsoConfig config;

    // Camera
    iso::IsoCamera camera;

    // Tile catalog
    iso_ed::TileCatalog catalog;

    // Current tool
    Tool         tool = Tool::Brush;
    int          current_layer = 0;  // 0=Ground, 1=Wall, 2=Object, 3=Overlay
    int          current_floor = 0;
    int          selected_tile_id = 0; // 0 = no tile selected
    iso::TileFlags paint_flags = iso::TileFlags::None;

    // Category filter for palette
    int          category_filter = 0; // 0 = All

    uint32_t selected_entity_id = 0;  // 0 = ничего не выбрано

    
    bool  entity_mode = false;  // клавиша O — переключает режим "объекты"
    float snap_size = 0.25f;  // snap шаг (0.25 = четверть тайла)

    // Display
    bool         show_grid      = true;
    bool         show_ghost     = true;   // preview tile under cursor
    bool         show_all_floors= false;
    bool         show_props_panel = false;

    // Palette scroll
    iso_ed::ui::ScrollState palette_scroll;

    bool show_all_opaque = false;  // V — все этажи видны на 100%

    bool show_minimap = false;  // M

    // Rect fill state
    bool         rect_dragging = false;
    iso::TileCoord rect_start{};
    iso::TileCoord rect_end{};

    // Undo/Redo
    std::deque<UndoGroup> undo_stack;
    std::deque<UndoGroup> redo_stack;
    UndoGroup    current_undo_group;
    bool         undo_grouping = false;
    static constexpr int MAX_UNDO = 200;

    // Status
    iso_ed::ui::StatusMessage status;
    std::string  current_filepath;
    bool         modified = false;


    // Tile flags toggles for painting
    bool flag_solid     = false;
    bool flag_blocks_los= false;
    bool flag_platform  = true;  // ground tiles default to platform
    bool flag_climbable = false;
    bool flag_half_height= false;
    bool flag_door       = false;
};

// ─── Undo system ──────────────────────────────

static void push_undo(EditorState& ed, iso::TileCoord coord, iso::LayerType layer,iso::TileData old_data, iso::TileData new_data)
{
    UndoEntry e;
    e.is_entity = false;
    e.coord = coord;
    e.layer = layer;
    e.old_data = old_data;
    e.new_data = new_data;
    ed.current_undo_group.entries.push_back(e);
}

static void commit_undo(EditorState& ed) {
    if (ed.current_undo_group.entries.empty()) return;
    ed.undo_stack.push_back(std::move(ed.current_undo_group));
    ed.current_undo_group = {};
    ed.redo_stack.clear();
    if ((int)ed.undo_stack.size() > ed.MAX_UNDO)
        ed.undo_stack.pop_front();
    ed.modified = true;
}

static void do_undo(EditorState& ed) {
    if (ed.undo_stack.empty()) return;
    auto& group = ed.undo_stack.back();
    for (auto& e : group.entries) {
        if (!e.is_entity) {
            ed.map.set_tile(e.layer, e.coord, e.old_data);
        }
        else {
            switch (e.entity_action) {
            case UndoEntry::EntityAction::Add:
                ed.map.entities().remove(e.entity_data.id);
                break;
            case UndoEntry::EntityAction::Remove:
                ed.map.entities().all().push_back(e.entity_data);
                break;
            case UndoEntry::EntityAction::Modify:
                for (auto& ent : ed.map.entities().all()) {
                    if (ent.id == e.entity_data.id) { ent = e.entity_data; break; }
                }
                break;
            }
        }
    }
    ed.redo_stack.push_back(std::move(group));
    ed.undo_stack.pop_back();
    ed.status.set("Undo", iso_ed::ui::theme.warning);
}

static void do_redo(EditorState& ed) {
    if (ed.redo_stack.empty()) return;
    auto& group = ed.redo_stack.back();
    for (auto& e : group.entries) {
        if (!e.is_entity) {
            ed.map.set_tile(e.layer, e.coord, e.new_data);
        }
        else {
            switch (e.entity_action) {
            case UndoEntry::EntityAction::Add:
                ed.map.entities().all().push_back(e.entity_data);
                break;
            case UndoEntry::EntityAction::Remove:
                ed.map.entities().remove(e.entity_data.id);
                break;
            case UndoEntry::EntityAction::Modify:
                for (auto& ent : ed.map.entities().all()) {
                    if (ent.id == e.entity_data.id) { ent = e.entity_data; break; }
                }
                break;
            }
        }
    }
    ed.undo_stack.push_back(std::move(group));
    ed.redo_stack.pop_back();
    ed.status.set("Redo", iso_ed::ui::theme.warning);
}

static void push_entity_add(EditorState& ed, const iso::MapEntity& ent) {
    UndoEntry e;
    e.is_entity = true;
    e.entity_action = UndoEntry::EntityAction::Add;
    e.entity_data = ent;
    ed.current_undo_group.entries.push_back(e);
}

static void push_entity_remove(EditorState& ed, const iso::MapEntity& ent) {
    UndoEntry e;
    e.is_entity = true;
    e.entity_action = UndoEntry::EntityAction::Remove;
    e.entity_data = ent;
    ed.current_undo_group.entries.push_back(e);
}

static void push_entity_modify(EditorState& ed, const iso::MapEntity& old_ent) {
    UndoEntry e;
    e.is_entity = true;
    e.entity_action = UndoEntry::EntityAction::Modify;
    e.entity_data = old_ent;  // save old state
    ed.current_undo_group.entries.push_back(e);
}
// ─── Tile placement ───────────────────────────

static iso::TileFlags build_flags(const EditorState& ed) {
    iso::TileFlags f = iso::TileFlags::None;
    if (ed.flag_solid)      f = f | iso::TileFlags::Solid;
    if (ed.flag_blocks_los) f = f | iso::TileFlags::BlocksLOS;
    if (ed.flag_platform)   f = f | iso::TileFlags::Platform;
    if (ed.flag_climbable)  f = f | iso::TileFlags::Climbable;
    if (ed.flag_half_height)f = f | iso::TileFlags::HalfHeight;
    if (ed.flag_door)       f = f | iso::TileFlags::Door;
    return f;
}

static void place_tile(EditorState& ed, iso::TileCoord tc) 
{
    auto layer = static_cast<iso::LayerType>(ed.current_layer);
    auto old_data = ed.map.tile(layer, tc);

    iso::TileData new_data;
    new_data.tile_id  = (uint16_t)ed.selected_tile_id;
    new_data.flags    = build_flags(ed);
    new_data.height   = 1;
    new_data.variant  = 0;

    if (old_data.tile_id == new_data.tile_id &&
        old_data.flags   == new_data.flags) return; // no change

    push_undo(ed, tc, layer, old_data, new_data);
    ed.map.set_tile(layer, tc, new_data);
}

static void erase_tile(EditorState& ed, iso::TileCoord tc) {
    auto layer = static_cast<iso::LayerType>(ed.current_layer);
    auto old_data = ed.map.tile(layer, tc);
    if (old_data.is_empty()) return;

    push_undo(ed, tc, layer, old_data, iso::TileData{});
    ed.map.set_tile(layer, tc, iso::TileData{});
}

static void flood_fill(EditorState& ed, iso::TileCoord start) {
    auto layer = static_cast<iso::LayerType>(ed.current_layer);
    auto target = ed.map.tile(layer, start);
    if (target.tile_id == ed.selected_tile_id) return;

    static constexpr int MAX_FILL = 4096; // safety limit

    std::vector<iso::TileCoord> queue;
    queue.push_back(start);

    // Use a set for visited (no fixed bounds with chunks)
    struct CoordHash {
        size_t operator()(iso::TileCoord tc) const {
            return std::hash<uint64_t>{}(
                (uint64_t)(uint32_t)tc.col << 32 | (uint32_t)tc.row);
        }
    };
    struct CoordEq {
        bool operator()(iso::TileCoord a, iso::TileCoord b) const {
            return a.col == b.col && a.row == b.row;
        }
    };
    std::unordered_set<iso::TileCoord, CoordHash, CoordEq> visited;

    int filled = 0;
    while (!queue.empty() && filled < MAX_FILL) {
        auto tc = queue.back();
        queue.pop_back();

        if (visited.count(tc)) continue;
        visited.insert(tc);

        auto cur = ed.map.tile(layer, tc);
        if (cur.tile_id != target.tile_id || cur.flags != target.flags) continue;

        place_tile(ed, tc);
        filled++;

        queue.push_back({ tc.col + 1, tc.row, tc.floor });
        queue.push_back({ tc.col - 1, tc.row, tc.floor });
        queue.push_back({ tc.col, tc.row + 1, tc.floor });
        queue.push_back({ tc.col, tc.row - 1, tc.floor });
    }
}

static void rect_fill(EditorState& ed, iso::TileCoord a, iso::TileCoord b) {
    int c0 = std::min(a.col, b.col), c1 = std::max(a.col, b.col);
    int r0 = std::min(a.row, b.row), r1 = std::max(a.row, b.row);

    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            place_tile(ed, {c, r, a.floor});
}

// ─── Palette drawing ──────────────────────────

static void draw_palette(EditorState& ed) {
    using namespace iso_ed::ui;

    Rectangle panel = {0, TOOLBAR_HEIGHT, PALETTE_WIDTH,
                       (float)(SCREEN_H - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT)};
    draw_panel(panel);

    // Category filter tabs
    float tab_x = panel.x + 4;
    float tab_y = panel.y + 4;
    for (int i = 0; i < 8; ++i) {
        Rectangle tab_r = {tab_x, tab_y, 58, 20};
        if (button(tab_r, category_filter_names[i], ed.category_filter == i, 10)) {
            ed.category_filter = i;
            ed.palette_scroll.offset = 0;
        }
        tab_x += 62;
        if (i == 3) { tab_x = panel.x + 4; tab_y += 22; }
    }

    float content_y = tab_y + 26;

    // Filter tiles
    const auto& tiles = ed.catalog.tiles();
    std::vector<const iso_ed::TileEntry*> filtered;
    for (const auto& t : tiles) {
        if (ed.category_filter == 0 ||
            (int)t.category == ed.category_filter - 1) {
            filtered.push_back(&t);
        }
    }

    // Tile grid
    int cols = 3;
    float cell_size = (PALETTE_WIDTH - 16) / (float)cols;
    float content_height = ((int)filtered.size() + cols - 1) / cols * cell_size + 8;

    Rectangle scroll_area = {panel.x, content_y, PALETTE_WIDTH, panel.height - (content_y - panel.y)};
    begin_scroll(scroll_area, ed.palette_scroll, content_height);

    for (int i = 0; i < (int)filtered.size(); ++i) {
        const auto* tile = filtered[i];
        int gc = i % cols;
        int gr = i / cols;
        float cx = panel.x + 8 + gc * cell_size;
        float cy = content_y + gr * cell_size - ed.palette_scroll.offset;

        // Skip if outside visible area
        if (cy + cell_size < content_y || cy > content_y + scroll_area.height) continue;

        Rectangle cell = {cx, cy, cell_size - 4, cell_size - 4};

        bool selected = (tile->id == ed.selected_tile_id);
        bool hover = CheckCollisionPointRec(GetMousePosition(), cell) &&
                     CheckCollisionPointRec(GetMousePosition(), scroll_area);

        // Background
        Color bg = selected ? theme.bg_selected : (hover ? theme.bg_hover : theme.bg_item);
        DrawRectangleRec(cell, bg);
        if (selected) DrawRectangleLinesEx(cell, 2.f, theme.accent);
        else DrawRectangleLinesEx(cell, 1.f, theme.border);

        // Thumbnail
        if (tile->loaded) {
            float scale = std::min(
                (cell.width - 4) / tile->thumbnail.width,
                (cell.height - 12) / tile->thumbnail.height
            );
            float tw = tile->thumbnail.width * scale;
            float th = tile->thumbnail.height * scale;
            float tx = cell.x + (cell.width - tw) * 0.5f;
            float ty = cell.y + (cell.height - 12 - th) * 0.5f;
            DrawTextureEx(tile->thumbnail, {tx, ty}, 0.f, scale, WHITE);
        }

        // Label
        const char* short_name = tile->name.c_str();
        if (tile->name.length() > 10)
            DrawText(TextFormat("%.8s..", short_name), (int)cell.x + 2, (int)(cell.y + cell.height - 12), 8, theme.text_dim);
        else
            DrawText(short_name, (int)cell.x + 2, (int)(cell.y + cell.height - 12), 8, theme.text_dim);

        // Click to select
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), scroll_area))
        {
            ed.selected_tile_id = tile->id;
            ed.tool = Tool::Brush;
        }

        // Tooltip on hover
        if (hover && CheckCollisionPointRec(GetMousePosition(), scroll_area)) {
            tooltip(TextFormat("%s (%dx%d) [%s]",
                    tile->name.c_str(), tile->src_width, tile->src_height,
                    iso_ed::TileCatalog::category_name(tile->category)));
        }
    }

    end_scroll();
    draw_scrollbar(scroll_area, ed.palette_scroll, content_height);

    // Tile count
    draw_label(TextFormat("%d tiles", (int)filtered.size()),
               (int)panel.x + 8, (int)(panel.y + panel.height - 18), 10, theme.text_dim);

   
}

// ─── Toolbar ──────────────────────────────────

static void draw_toolbar(EditorState& ed) {
    using namespace iso_ed::ui;

    Rectangle bar = {0, 0, (float)SCREEN_W, TOOLBAR_HEIGHT};
    draw_panel(bar, theme.bg_dark);

    float x = PALETTE_WIDTH + 8;

    // Tools
    for (int i = 0; i < (int)Tool::Count; ++i) {
        Rectangle btn = {x, 4, 80, 28};
        if (button(btn, tool_names[i], ed.tool == (Tool)i, 11)) {
            ed.tool = (Tool)i;
        }
        x += 84;
    }

    x += 16;

    // Layer selector
    draw_label("Layer:", (int)x, 11, 12, theme.text_dim);
    x += 46;
    for (int i = 0; i < 4; ++i) {
        Rectangle btn = {x, 4, 64, 28};
        if (button(btn, layer_names[i], ed.current_layer == i, 11)) {
            ed.current_layer = i;
            // Auto-set flags
            if (i == 0) { // Ground
                ed.flag_solid = false;
                ed.flag_platform = true;
                ed.flag_blocks_los = false;
            } else if (i == 1) { // Wall
                ed.flag_solid = true;
                ed.flag_platform = false;
                ed.flag_blocks_los = true;
            } else {
                ed.flag_solid = false;
                ed.flag_platform = false;
                ed.flag_blocks_los = false;
            }
        }
        x += 68;
    }

    x += 16;

    // Floor selector
    draw_label("Floor:", (int)x, 11, 12, theme.text_dim);
    x += 46;
    Rectangle floor_r = {x, 4, 120, 28};
    spinner(floor_r, "F", &ed.current_floor, ed.config.min_floor, ed.config.max_floor);
    x += 128;

    // Grid toggle
    x += 8;
    Rectangle grid_r = {x, 4, 50, 28};
    toggle(grid_r, "Grid", &ed.show_grid);
}

// ─── Properties panel ─────────────────────────

static void draw_props_panel(EditorState& ed) {
    using namespace iso_ed::ui;

    float px = SCREEN_W - PROPS_WIDTH;
    Rectangle panel = {px, TOOLBAR_HEIGHT, PROPS_WIDTH,
                       (float)(SCREEN_H - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT)};
    draw_panel(panel);

    float y = panel.y + 8;
    float x = panel.x + 8;
    float w = PROPS_WIDTH - 16;

    draw_label("Tile Flags", (int)x, (int)y, 14, theme.text_bright);
    y += 22;

    toggle({x, y, w, 22}, "Solid (blocks movement)", &ed.flag_solid); y += 26;
    toggle({x, y, w, 22}, "Blocks LOS", &ed.flag_blocks_los); y += 26;
    toggle({x, y, w, 22}, "Platform (walkable)", &ed.flag_platform); y += 26;
    toggle({x, y, w, 22}, "Climbable (stairs)", &ed.flag_climbable); y += 26;
    toggle({x, y, w, 22}, "Half-height (fence)", &ed.flag_half_height); y += 26;
    toggle({x, y, w, 22}, "Door", &ed.flag_door); y += 34;

    // Selected tile info
    draw_label("Selected Tile", (int)x, (int)y, 14, theme.text_bright);
    y += 22;
    if (ed.selected_tile_id > 0) {
        const auto* tile = ed.catalog.get(ed.selected_tile_id);
        if (tile) {
            draw_label(TextFormat("Name: %s", tile->name.c_str()), (int)x, (int)y, 12, theme.text);
            y += 16;
            draw_label(TextFormat("Size: %dx%d", tile->src_width, tile->src_height), (int)x, (int)y, 12, theme.text);
            y += 16;
            draw_label(TextFormat("Cat: %s", iso_ed::TileCatalog::category_name(tile->category)),
                       (int)x, (int)y, 12, theme.text);
            y += 22;

            // Preview
            float prev_h = 120;
            float scale = std::min(
                (w - 8) / (float)tile->texture.width,
                prev_h / (float)tile->texture.height
            );
            float tw = tile->texture.width * scale;
            float th = tile->texture.height * scale;
            float tx = x + (w - tw) * 0.5f;
            DrawRectangle((int)x, (int)y, (int)w, (int)prev_h + 8, theme.bg_item);
            DrawTextureEx(tile->texture, {tx, y + 4}, 0.f, scale, WHITE);
        }
    } else {
        draw_label("(none)", (int)x, (int)y, 12, theme.text_dim);
    }
}

// ─── Status bar ───────────────────────────────

static void draw_statusbar(EditorState& ed, iso::TileCoord hover_tile) {
    using namespace iso_ed::ui;

    Rectangle bar = {0, (float)(SCREEN_H - STATUSBAR_HEIGHT), (float)SCREEN_W, STATUSBAR_HEIGHT};
    draw_panel(bar, theme.bg_dark);

    float x = 8;
    draw_label(TextFormat("Tile: %d, %d  Floor: %d", hover_tile.col, hover_tile.row, ed.current_floor),
               (int)x, SCREEN_H - 20, 12, theme.text);

    x += 220;
    draw_label(TextFormat("Zoom: %.1f%%", ed.camera.zoom() * 100.f),
               (int)x, SCREEN_H - 20, 12, theme.text_dim);

    x += 110;
    draw_label(TextFormat("Layer: %s", layer_names[ed.current_layer]),
               (int)x, SCREEN_H - 20, 12, theme.text);

    x += 120;
    if (ed.modified)
        draw_label("* Modified", (int)x, SCREEN_H - 20, 12, theme.warning);

    // Status message (right side)
    ed.status.draw(SCREEN_W - 300, SCREEN_H - 20);

    // File path (center)
    if (!ed.current_filepath.empty()) {
        const char* fname = ed.current_filepath.c_str();
        int tw = MeasureText(fname, 11);
        draw_label(fname, SCREEN_W / 2 - tw / 2, SCREEN_H - 20, 11, theme.text_dim);
    }
    if (ed.entity_mode)
        draw_label("ENTITY[O]", (int)x + 120, SCREEN_H - 20, 12, iso_ed::ui::theme.accent);

    if (ed.show_all_opaque)
        draw_label("ALL[V]", (int)(x + 180), SCREEN_H - 20, 12, iso_ed::ui::theme.accent);


    auto [bmin, bmax] = ed.map.bounds();
    DrawText(TextFormat("Map: %d×%d  Chunks: %d",
        bmax.col - bmin.col + 1, bmax.row - bmin.row + 1, ed.map.chunk_count()),
        (int)(x + 100), SCREEN_H - 20, 12, theme.text_dim);
}

static void draw_minimap(EditorState& ed) {
    using namespace iso_ed::ui;
    if (!ed.show_minimap) return;

    const auto& cfg = ed.config;
    auto [bmin, bmax] = ed.map.bounds();
    int map_w = bmax.col - bmin.col + 1;
    int map_h = bmax.row - bmin.row + 1;
    if (map_w <= 0 || map_h <= 0) return;

    // Minimap size and position (bottom-right corner)
    float mm_max = 250.f;
    float scale = std::min(mm_max / map_w, mm_max / map_h);
    float mm_w = map_w * scale;
    float mm_h = map_h * scale;
    float mm_x = GetScreenWidth() - mm_w - 16 - (ed.show_props_panel ? PROPS_WIDTH : 0);
    float mm_y = GetScreenHeight() - mm_h - STATUSBAR_HEIGHT - 16;

    // Background
    DrawRectangle((int)mm_x - 2, (int)mm_y - 2, (int)mm_w + 4, (int)mm_h + 4,
        { 0, 0, 0, 180 });
    DrawRectangleLinesEx({ mm_x - 2, mm_y - 2, mm_w + 4, mm_h + 4 }, 1.f,
        theme.border);

    // Draw chunks as colored blocks
    ed.map.for_each_chunk([&](iso::ChunkCoord cc, const iso::Chunk& chunk) {
        if (chunk.is_empty()) return;

        float cx = mm_x + (cc.origin_col() - bmin.col) * scale;
        float cy = mm_y + (cc.origin_row() - bmin.row) * scale;
        float cw = iso::CHUNK_SIZE * scale;
        float ch = iso::CHUNK_SIZE * scale;

        // Count non-empty tiles for color intensity
        int filled = 0;
        for (int lr = 0; lr < iso::CHUNK_SIZE; ++lr)
            for (int lc = 0; lc < iso::CHUNK_SIZE; ++lc) {
                auto td = chunk.tile_safe(iso::LayerType::Ground, lc, lr, ed.current_floor);
                if (!td.is_empty()) filled++;
            }

        float density = (float)filled / iso::CHUNK_TILE_COUNT;
        unsigned char a = (unsigned char)(60 + 160 * density);

        // Ground = green, walls present = brownish
        bool has_walls = false;
        for (int lr = 0; lr < iso::CHUNK_SIZE && !has_walls; ++lr)
            for (int lc = 0; lc < iso::CHUNK_SIZE && !has_walls; ++lc) {
                auto td = chunk.tile_safe(iso::LayerType::Wall, lc, lr, ed.current_floor);
                if (!td.is_empty()) has_walls = true;
            }

        Color col = has_walls ? Color{ 160, 130, 90, a } : Color{ 70, 130, 60, a };
        DrawRectangle((int)cx, (int)cy, (int)cw, (int)ch, col);
        DrawRectangleLinesEx({ cx, cy, cw, ch }, 0.5f, { 100, 100, 120, 80 });
        });

    // Draw entities as dots
    for (const auto& ent : ed.map.entities().all()) {
        if (ent.floor != ed.current_floor) continue;
        float ex = mm_x + (ent.x - bmin.col) * scale;
        float ey = mm_y + (ent.y - bmin.row) * scale;
        DrawCircle((int)ex, (int)ey, 2.f, { 255, 200, 50, 200 });
    }

    // Camera viewport indicator
    iso::Vec3 cam_tl = ed.camera.screen_to_world({ (float)PALETTE_WIDTH, (float)TOOLBAR_HEIGHT });
    iso::Vec3 cam_br = ed.camera.screen_to_world(
        { (float)(GetScreenWidth() - (ed.show_props_panel ? PROPS_WIDTH : 0)),
         (float)(GetScreenHeight() - STATUSBAR_HEIGHT) });

    float vx = mm_x + (cam_tl.x - bmin.col) * scale;
    float vy = mm_y + (cam_tl.y - bmin.row) * scale;
    float vw = (cam_br.x - cam_tl.x) * scale;
    float vh = (cam_br.y - cam_tl.y) * scale;
    DrawRectangleLinesEx({ vx, vy, vw, vh }, 2.f, { 255, 255, 255, 200 });

    // Click to teleport camera
    Vector2 mp = GetMousePosition();
    if (CheckCollisionPointRec(mp, { mm_x, mm_y, mm_w, mm_h })
        && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        float wx = bmin.col + (mp.x - mm_x) / scale;
        float wy = bmin.row + (mp.y - mm_y) / scale;
        ed.camera.set_position({ wx, wy, ed.current_floor * cfg.floor_height });
    }

    // Label
    DrawText("MAP [M]", (int)mm_x, (int)mm_y - 14, 11, theme.text_dim);
}
// ─── Map viewport rendering ──────────────────

static void draw_map_viewport(EditorState& ed, iso::TileCoord hover_tile) 
{
    using namespace iso_ed::ui;
    float vp_x = PALETTE_WIDTH;
    float vp_w = SCREEN_W - PALETTE_WIDTH - (ed.show_props_panel ? PROPS_WIDTH : 0);
    float vp_y = TOOLBAR_HEIGHT;
    float vp_h = SCREEN_H - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT;

    BeginScissorMode((int)vp_x, (int)vp_y, (int)vp_w, (int)vp_h);
    const auto& cfg = ed.config;

    int draw_min_floor = cfg.min_floor;
    int draw_max_floor = ed.show_all_opaque ? cfg.max_floor : ed.current_floor;

    // Find visible tile range from camera
    iso::Vec3 tl = ed.camera.screen_to_world({ vp_x, vp_y });
    iso::Vec3 br = ed.camera.screen_to_world({ vp_x + vp_w, vp_y + vp_h });
    iso::Vec3 tr = ed.camera.screen_to_world({ vp_x + vp_w, vp_y });
    iso::Vec3 bl = ed.camera.screen_to_world({ vp_x, vp_y + vp_h });
    // Generous padding
    int vis_col_min = (int)std::floor(std::min({ tl.x, br.x, tr.x, bl.x })) - 4;
    int vis_col_max = (int)std::ceil(std::max({ tl.x, br.x, tr.x, bl.x })) + 4;
    int vis_row_min = (int)std::floor(std::min({ tl.y, br.y, tr.y, bl.y })) - 4;
    int vis_row_max = (int)std::ceil(std::max({ tl.y, br.y, tr.y, bl.y })) + 4;

    auto [cmin, cmax] = iso::ChunkMap::chunk_range(
        vis_col_min, vis_row_min, vis_col_max, vis_row_max);

    //zoom
    float zoom = ed.camera.zoom();
    float min_tile_screen = cfg.tile_width * zoom;
    bool skip_objects = min_tile_screen < 8.f;   // объекты < 8px — не рисовать
    bool skip_walls = min_tile_screen < 4.f;    // стены < 4px — не рисовать
    bool draw_simple = min_tile_screen < 16.f;   // < 16px — рисовать цветные ромбы вместо текстур

    for (int f = draw_min_floor; f <= draw_max_floor; ++f) 
    {
        float floor_alpha = (ed.show_all_opaque) ? 1.0f
            : (f < ed.current_floor) ? 0.3f : 1.0f;
        unsigned char alpha = (unsigned char)(255 * floor_alpha);

        // 1) Ground layer — always first
        ed.map.for_each_chunk_in(cmin, cmax, [&](iso::ChunkCoord cc, const iso::Chunk& chunk) 
            {
            int ox = cc.origin_col();
            int oy = cc.origin_row();
            for (int lr = 0; lr < iso::CHUNK_SIZE; ++lr) 
            {
                for (int lc = 0; lc < iso::CHUNK_SIZE; ++lc) 
                {
                    const iso::TileData& td = chunk.tile_safe(
                        iso::LayerType::Ground, lc, lr, f);
                    if (td.is_empty()) continue;

                    int col = ox + lc, row = oy + lr;
                    iso::Vec3 world = iso::tile_to_world({ col, row, f }, cfg);
                    iso::Vec2 sp = ed.camera.world_to_screen(world);
                    if (sp.x < vp_x - 300 || sp.x > vp_x + vp_w + 300 ||
                        sp.y < vp_y - 300 || sp.y > vp_y + vp_h + 300) continue;

                    if (draw_simple) {
                        // Дешёвый цветной ромб вместо текстуры
                        float hw = cfg.tile_width * 0.5f * zoom;
                        float hh = cfg.tile_height * 0.5f * zoom;
                        Color c = { 80, 130, 60, alpha };  // simplified color
                        Vector2 pts[4] = { {sp.x,sp.y - hh},{sp.x + hw,sp.y},{sp.x,sp.y + hh},{sp.x - hw,sp.y} };
                        DrawTriangle(pts[0], pts[3], pts[2], c);
                        DrawTriangle(pts[0], pts[2], pts[1], c);
                        continue;  // skip texture draw
                    }


                    const auto* te = ed.catalog.get(td.tile_id);
                    if (te && te->loaded) {
                        float zoom = ed.camera.zoom();
                        float tw = te->src_width * zoom;
                        float th = te->src_height * zoom;
                        Rectangle src = { 0, 0, (float)te->src_width, (float)te->src_height };
                        Rectangle dst = { sp.x - tw * 0.5f, sp.y - th * 0.5f, tw, th };
                        DrawTexturePro(te->texture, src, dst, { 0,0 }, 0.f, { 255,255,255,alpha });
                    }
                    else {
                        float hw = ed.camera.tile_draw_hw();
                        float hh = ed.camera.tile_draw_hh();
                        Color c = { 80, 140, 60, (unsigned char)(180 * floor_alpha) };
                        Vector2 pts[4] = { {sp.x,sp.y - hh},{sp.x + hw,sp.y},{sp.x,sp.y + hh},{sp.x - hw,sp.y} };
                        DrawTriangle(pts[0], pts[3], pts[2], c);
                        DrawTriangle(pts[0], pts[2], pts[1], c);
                    }
                }
            }
            });

        // 2) Collect walls/objects/overlays + entities into one list
        struct DrawItem {
            float depth;
            bool is_entity;
            int col, row, layer;
            const iso::MapEntity* ent;  
        };
        std::vector<DrawItem> items;

        // Add tile layers 1-3 from visible chunks
        ed.map.for_each_chunk_in(cmin, cmax, [&](iso::ChunkCoord cc, const iso::Chunk& chunk) 
            {
            int ox = cc.origin_col();
            int oy = cc.origin_row();

            iso::Vec2 chunk_center = ed.camera.world_to_screen(
                iso::tile_to_world({ ox + iso::CHUNK_SIZE / 2, oy + iso::CHUNK_SIZE / 2, f }, cfg));
            float chunk_radius = iso::CHUNK_SIZE * cfg.tile_width * zoom;
            if (chunk_center.x < vp_x - chunk_radius || chunk_center.x > vp_x + vp_w + chunk_radius ||
                chunk_center.y < vp_y - chunk_radius || chunk_center.y > vp_y + vp_h + chunk_radius)
                return;  // entire chunk off-screen

            for (int lr = 0; lr < iso::CHUNK_SIZE; ++lr) 
            {
                for (int lc = 0; lc < iso::CHUNK_SIZE; ++lc) 
                {
                    int col = ox + lc, row = oy + lr;
                    for (int layer = 1; layer < iso::LAYER_COUNT; ++layer) 
                    {
                        if (skip_objects && layer >= 2) continue;  // skip objects/overlay
                        if (skip_walls && layer == 1) continue;   // skip walls

                        const iso::TileData& td = chunk.tile_safe(
                            static_cast<iso::LayerType>(layer), lc, lr, f);
                        if (td.is_empty()) continue;
                        items.push_back({
                            (float)(col + row) + layer * 0.1f,
                            false, col, row, layer, nullptr
                            });
                    }
                }
            }
            });

        
        if (!skip_objects)
        {
            // Add entities on this floor
            for (const auto& ent : ed.map.entities().all()) {
                if (ent.floor != f) continue;
                items.push_back({
                    ent.iso_depth(),
                    true, 0, 0, 0, &ent
                    });
            }
        }
        

        // Sort by depth
        std::sort(items.begin(), items.end(),
            [](const DrawItem& a, const DrawItem& b) { return a.depth < b.depth; });

        // 3) Draw sorted
        for (const auto& item : items) {
            if (!item.is_entity) {
                iso::Vec3 world = iso::tile_to_world({ item.col, item.row, f }, cfg);
                iso::Vec2 sp = ed.camera.world_to_screen(world);
                if (sp.x < vp_x - 300 || sp.x > vp_x + vp_w + 300 ||
                    sp.y < vp_y - 300 || sp.y > vp_y + vp_h + 300) continue;

                iso::TileData td = ed.map.tile(
                    static_cast<iso::LayerType>(item.layer),
                    { item.col, item.row, f });
                const auto* te = ed.catalog.get(td.tile_id);
                if (te && te->loaded) {
                    float zoom = ed.camera.zoom();
                    float tw = te->src_width * zoom;
                    float th = te->src_height * zoom;
                    float dx = sp.x - tw * 0.5f;
                    float screen_hh = cfg.tile_height * 0.5f * zoom;
                    float dy = sp.y + screen_hh - th;
                    Rectangle src = { 0, 0, (float)te->src_width, (float)te->src_height };
                    Rectangle dst = { dx, dy, tw, th };
                    DrawTexturePro(te->texture, src, dst, { 0,0 }, 0.f, { 255,255,255,alpha });
                }
                else {
                    float hw = ed.camera.tile_draw_hw();
                    float hh = ed.camera.tile_draw_hh();
                    Color c = (item.layer == 1) ? Color{ 160,140,120,(unsigned char)(200 * floor_alpha) }
                        : (item.layer == 2) ? Color{ 120,100,80,(unsigned char)(180 * floor_alpha) }
                    : Color{ 140,80,80,(unsigned char)(120 * floor_alpha) };
                    Vector2 pts[4] = { {sp.x,sp.y - hh},{sp.x + hw,sp.y},{sp.x,sp.y + hh},{sp.x - hw,sp.y} };
                    DrawTriangle(pts[0], pts[3], pts[2], c);
                    DrawTriangle(pts[0], pts[2], pts[1], c);
                }
            }
            else {
                const auto* ent = item.ent;
                const auto* te = ed.catalog.get(ent->tile_id);
                if (!te || !te->loaded) continue;

                iso::Vec3 wpos = { ent->x, ent->y, f * cfg.floor_height };
                iso::Vec2 sp = ed.camera.world_to_screen(wpos);
                if (sp.x < vp_x - 300 || sp.x > vp_x + vp_w + 300 ||
                    sp.y < vp_y - 300 || sp.y > vp_y + vp_h + 300) continue;

                float zoom = ed.camera.zoom();
                float tw = te->src_width * zoom;
                float th = te->src_height * zoom;
                float dx = sp.x - tw * 0.5f;
                float screen_hh = cfg.tile_height * 0.5f * zoom;
                float dy = sp.y + screen_hh - th;
                Rectangle src = { 0, 0, (float)te->src_width, (float)te->src_height };
                Rectangle dst = { dx, dy, tw, th };
                DrawTexturePro(te->texture, src, dst, { 0,0 }, 0.f, { 255,255,255,alpha });

                if (ent->id == ed.selected_entity_id) {
                    DrawRectangleLinesEx(dst, 2.f, YELLOW);
                    DrawText(TextFormat("z: %d", ent->z_bias),
                        (int)dst.x, (int)dst.y - 16, 12, YELLOW);
                }
            }
        }
    }


    // Grid — рисуем вокруг видимой области
    if (ed.show_grid) {
        iso::draw_tile_grid(ed.camera, cfg,
            vis_col_min, vis_row_min, vis_col_max, vis_row_max,
            ed.current_floor, { 60, 60, 80, 40 });
    }

    // Hover highlight
    {
        Color highlight = {255, 255, 0, 50};
        if (ed.tool == Tool::Eraser) highlight = {255, 50, 50, 50};
        if (ed.tool == Tool::Eyedropper) highlight = {50, 200, 255, 50};
        iso::draw_tile_highlight(ed.camera, cfg, hover_tile, highlight);

        // Ghost tile preview
        if (!ed.entity_mode && ed.show_ghost && ed.tool == Tool::Brush && ed.selected_tile_id > 0)
        {
            const auto* tile_entry = ed.catalog.get(ed.selected_tile_id);
            if (tile_entry && tile_entry->loaded) 
            {
                iso::Vec3 ghost_world = iso::tile_to_world(hover_tile, cfg);
                iso::Vec2 gsp = ed.camera.world_to_screen(ghost_world);

                float zoom = ed.camera.zoom();
                float tw = tile_entry->src_width * zoom;
                float th = tile_entry->src_height * zoom;
               
                float dx = gsp.x - tw * 0.5f;
                float dy;
                float screen_hh = cfg.tile_height * 0.5f * zoom;

                if (ed.current_layer == 0) 
                {
                    // Ground: центрируем
                    dy = gsp.y - th * 0.5f;
                }
                else 
                {
                    // Wall/Object/Overlay: низ картинки = низ ромба
                    dy = gsp.y + screen_hh - th;
                }

                Rectangle src = {0, 0, (float)tile_entry->src_width, (float)tile_entry->src_height};
                Rectangle dst = {dx, dy, tw, th};
                DrawTexturePro(tile_entry->texture, src, dst, {0, 0}, 0.f, {255, 255, 255, 120});
            }
        }

        // Entity ghost preview
        if (ed.entity_mode && ed.show_ghost && ed.selected_tile_id > 0 ) 
        {
            const auto* tile_entry = ed.catalog.get(ed.selected_tile_id);
            if (tile_entry && tile_entry->loaded) {
                iso::Vec3 world = ed.camera.screen_to_world(
                    iso::from_rl(GetMousePosition()),
                    ed.current_floor * ed.config.floor_height
                );
                float sx = std::round(world.x / ed.snap_size) * ed.snap_size;
                float sy = std::round(world.y / ed.snap_size) * ed.snap_size;
                iso::Vec2 sp = ed.camera.world_to_screen({ sx, sy, ed.current_floor * cfg.floor_height });

                float zoom = ed.camera.zoom();
                float tw = tile_entry->src_width * zoom;
                float th = tile_entry->src_height * zoom;
                float dx = sp.x - tw * 0.5f;
                float screen_hh = cfg.tile_height * 0.5f * zoom;
                float dy = sp.y + screen_hh - th;

                Rectangle src = { 0, 0, (float)tile_entry->src_width, (float)tile_entry->src_height };
                Rectangle dst = { dx, dy, tw, th };
                DrawTexturePro(tile_entry->texture, src, dst, { 0, 0 }, 0.f, { 255, 255, 255, 100 });
            }
        }
    }

    // Rect fill preview
    if (ed.rect_dragging && ed.tool == Tool::RectFill) {
        int c0 = std::min(ed.rect_start.col, ed.rect_end.col);
        int c1 = std::max(ed.rect_start.col, ed.rect_end.col);
        int r0 = std::min(ed.rect_start.row, ed.rect_end.row);
        int r1 = std::max(ed.rect_start.row, ed.rect_end.row);

        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                iso::TileCoord tc = {c, r, ed.current_floor};
                iso::draw_tile_highlight(ed.camera, cfg, tc, {100, 180, 255, 40});
            }
        }
    }

    EndScissorMode();
}

// ─── Input handling ───────────────────────────

static bool is_viewport_hovered(const EditorState& ed) {
    Vector2 mp = GetMousePosition();
    float vp_x = PALETTE_WIDTH;
    float vp_w = SCREEN_W - PALETTE_WIDTH - (ed.show_props_panel ? PROPS_WIDTH : 0);
    float vp_y = TOOLBAR_HEIGHT;
    float vp_h = SCREEN_H - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT;
    return mp.x >= vp_x && mp.x < vp_x + vp_w &&
           mp.y >= vp_y && mp.y < vp_y + vp_h;
}

static void handle_input(EditorState& ed, iso::TileCoord hover_tile) 
{
    bool viewport_hover = is_viewport_hovered(ed);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    // ── Keyboard shortcuts ────────────────────
    if (IsKeyPressed(KEY_O)) ed.entity_mode = !ed.entity_mode;
    if (IsKeyPressed(KEY_B)) ed.tool = Tool::Brush;
    if (IsKeyPressed(KEY_E)) ed.tool = Tool::Eraser;
    if (IsKeyPressed(KEY_SPACE)) ed.tool = Tool::Eyedropper;
    if (IsKeyPressed(KEY_R)) ed.tool = Tool::RectFill;
    if (IsKeyPressed(KEY_F)) ed.tool = Tool::FloodFill;

    if (IsKeyPressed(KEY_ONE))   ed.current_layer = 0;
    if (IsKeyPressed(KEY_TWO))   ed.current_layer = 1;
    if (IsKeyPressed(KEY_THREE)) ed.current_layer = 2;
    if (IsKeyPressed(KEY_FOUR))  ed.current_layer = 3;

    if (IsKeyPressed(KEY_V)) ed.show_all_opaque = !ed.show_all_opaque;
    if (IsKeyPressed(KEY_M)) ed.show_minimap = !ed.show_minimap;

    if (IsKeyPressed(KEY_TAB)) ed.show_grid = !ed.show_grid;
    if (IsKeyPressed(KEY_G))   ed.show_ghost = !ed.show_ghost;
    if (IsKeyPressed(KEY_P))   ed.show_props_panel = !ed.show_props_panel;


    if (!ctrl && IsKeyPressed(KEY_PAGE_UP))   ed.current_floor = std::min(ed.current_floor + 1, ed.config.max_floor);
    if (!ctrl && IsKeyPressed(KEY_PAGE_DOWN)) ed.current_floor = std::max(ed.current_floor - 1, ed.config.min_floor);

    // Undo/Redo
    if (ctrl && IsKeyPressed(KEY_Z)) do_undo(ed);
    if (ctrl && IsKeyPressed(KEY_Y)) do_redo(ed);

    if (ctrl && IsKeyPressed(KEY_PAGE_UP))
    {
        ed.config.height_pixel_offset += 4.0f;
        printf("height_pixel_offset=%f\n", ed.config.height_pixel_offset);
    }
    if (ctrl && IsKeyPressed(KEY_PAGE_DOWN))
    {
        ed.config.height_pixel_offset -= 4.0f;
        printf("height_pixel_offset=%f\n", ed.config.height_pixel_offset);
    }

    // Save
    if (ctrl && IsKeyPressed(KEY_S)) 
    {
        if (ed.current_filepath.empty())
            ed.current_filepath = "map.isomap";  // новый формат
        ed.map.prune_empty();  // убрать пустые чанки перед сохранением!
        if (iso::save_isomap(ed.map, ed.current_filepath)) {
            ed.status.set(TextFormat("Saved! (%d chunks)", ed.map.chunk_count()),
                iso_ed::ui::theme.success);
            ed.modified = false;
        }
        else {
            ed.status.set("Save failed!", iso_ed::ui::theme.danger);
        }
    }

    // ── Camera ────────────────────────────────
    if (viewport_hover) {
        iso::handle_camera_zoom(ed.camera, 0.1f);
    }
    iso::handle_camera_drag(ed.camera);

    // WASD pan (when no text input active)
    float cam_speed = 10.f;
    iso::handle_camera_wasd(ed.camera, cam_speed, GetFrameTime());

    // ── Mouse tools (only in viewport) ────────
    if (!viewport_hover) return;

    bool lmb_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool lmb_down    = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool rmb_down    = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    // ── Entity mode (free placement) ──────────
    if (ed.entity_mode && viewport_hover) 
    {
        iso::Vec3 world = ed.camera.screen_to_world(
            iso::from_rl(GetMousePosition()),
            ed.current_floor * ed.config.floor_height
        );
        float sx = std::round(world.x / ed.snap_size) * ed.snap_size;
        float sy = std::round(world.y / ed.snap_size) * ed.snap_size;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (shift) {
                auto* hit = ed.map.entities().pick(sx, sy, ed.current_floor, 0.6f);
                if (hit) {
                    ed.selected_entity_id = hit->id;
                    ed.status.set(TextFormat("Selected entity #%d", hit->id),
                        iso_ed::ui::theme.accent);
                }
                else {
                    ed.selected_entity_id = 0;
                }
            }
            else if (ed.selected_tile_id > 0) {
                uint32_t new_id = ed.map.entities().add(
                    (uint16_t)ed.selected_tile_id, sx, sy, ed.current_floor);
                // Find the just-added entity for undo
                for (const auto& ent : ed.map.entities().all()) {
                    if (ent.id == new_id) {
                        push_entity_add(ed, ent);
                        break;
                    }
                }
                commit_undo(ed);
                ed.modified = true;
                ed.status.set(TextFormat("Entity placed at %.2f, %.2f", sx, sy),
                    iso_ed::ui::theme.success);
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            auto* hit = ed.map.entities().pick(sx, sy, ed.current_floor, 0.6f);
            if (hit) {
                push_entity_remove(ed, *hit);  // save before removing
                if (hit->id == ed.selected_entity_id) ed.selected_entity_id = 0;
                ed.map.entities().remove(hit->id);
                commit_undo(ed);
                ed.modified = true;
                ed.status.set("Entity removed", iso_ed::ui::theme.warning);
            }
        }

        // +/- adjust z_bias of selected entity
        if (ed.selected_entity_id != 0) {
            iso::MapEntity* sel = nullptr;
            for (auto& e : ed.map.entities().all())
                if (e.id == ed.selected_entity_id) { sel = &e; break; }

            if (sel) {
                if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
                    push_entity_modify(ed, *sel);  // save old state
                    sel->z_bias = std::min(sel->z_bias + 1, 10);
                    commit_undo(ed);
                    ed.modified = true;
                }
                if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
                    push_entity_modify(ed, *sel);
                    sel->z_bias = std::max(sel->z_bias - 1, -10);
                    commit_undo(ed);
                    ed.modified = true;
                }
                if (IsKeyPressed(KEY_ESCAPE)) {
                    ed.selected_entity_id = 0;
                }
            }
        }

        return;
    }

    iso::TileCoord tc = {hover_tile.col, hover_tile.row, ed.current_floor};

    switch (ed.tool) {
        case Tool::Brush:
            if (lmb_down && ed.selected_tile_id > 0) {
                place_tile(ed, tc);
                if (!ed.undo_grouping) ed.undo_grouping = true;
            }
            if (rmb_down) {
                erase_tile(ed, tc);
                if (!ed.undo_grouping) ed.undo_grouping = true;
            }
            break;

        case Tool::Eraser:
            if (lmb_down) {
                erase_tile(ed, tc);
                if (!ed.undo_grouping) ed.undo_grouping = true;
            }
            break;

        case Tool::Eyedropper:
            if (lmb_pressed) {
                auto layer = static_cast<iso::LayerType>(ed.current_layer);
                auto picked = ed.map.tile(layer, tc);
                if (!picked.is_empty()) {
                    ed.selected_tile_id = picked.tile_id;
                    ed.flag_solid      = iso::has_flag(picked.flags, iso::TileFlags::Solid);
                    ed.flag_blocks_los = iso::has_flag(picked.flags, iso::TileFlags::BlocksLOS);
                    ed.flag_platform   = iso::has_flag(picked.flags, iso::TileFlags::Platform);
                    ed.flag_climbable  = iso::has_flag(picked.flags, iso::TileFlags::Climbable);
                    ed.flag_half_height= iso::has_flag(picked.flags, iso::TileFlags::HalfHeight);
                    ed.flag_door       = iso::has_flag(picked.flags, iso::TileFlags::Door);
                    ed.tool = Tool::Brush;
                    ed.status.set(TextFormat("Picked tile %d", picked.tile_id), iso_ed::ui::theme.accent);
                }
            }
            break;

        case Tool::RectFill:
            if (lmb_pressed) {
                ed.rect_dragging = true;
                ed.rect_start = tc;
                ed.rect_end   = tc;
            }
            if (lmb_down && ed.rect_dragging) {
                ed.rect_end = tc;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && ed.rect_dragging) {
                ed.rect_dragging = false;
                rect_fill(ed, ed.rect_start, ed.rect_end);
                commit_undo(ed);
            }
            break;

        case Tool::FloodFill:
            if (lmb_pressed && ed.selected_tile_id > 0) {
                flood_fill(ed, tc);
                commit_undo(ed);
            }
            break;

        default: break;
    }

    // RMB erase in any tool
    if (ed.tool != Tool::Eraser && rmb_down) {
        erase_tile(ed, tc);
        if (!ed.undo_grouping) ed.undo_grouping = true;
    }

    // Commit undo group when mouse released
    if ((IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        && ed.undo_grouping && ed.tool != Tool::RectFill)
    {
        commit_undo(ed);
        ed.undo_grouping = false;
    }

    
}

// ─── File drop handling ───────────────────────

static void handle_file_drop(EditorState& ed) {
    if (!IsFileDropped()) return;

    FilePathList files = LoadDroppedFiles();
    for (int i = 0; i < (int)files.count; ++i) {
        std::string path = files.paths[i];
        std::string ext = GetFileExtension(path.c_str());

        if (ext == ".isomap") {
            if (iso::load_isomap(ed.map, path)) {
                ed.config = ed.map.config();
                ed.camera.set_config(ed.config);
                ed.current_filepath = path;
                ed.modified = false;
                ed.current_floor = 0;
                ed.undo_stack.clear();
                ed.redo_stack.clear();
                ed.status.set(TextFormat("Loaded! (%d chunks)", ed.map.chunk_count()),
                    iso_ed::ui::theme.success);
            }
        }
        else if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
            // Add tile to catalog
            int id = ed.catalog.add_tile(path);
            if (id >= 0) {
                ed.status.set(TextFormat("Added tile: %s", GetFileName(path.c_str())), iso_ed::ui::theme.success);
            }
        }
    }
    UnloadDroppedFiles(files);
}

// ─── Main ─────────────────────────────────────

int main(int argc, char* argv[]) {
    // Window
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "iso_engine — Tile Map Editor");
    SetTargetFPS(60);
    SetExitKey(0); // disable ESC close

    // ── Init editor state ─────────────────────

    EditorState ed;

    ed.config.tile_width       = 256;     // match your tileset dimensions!
    ed.config.tile_height      = 128;
    ed.config.floor_height     = 1.0f;
    ed.config.height_pixel_offset = 30.0f;
    ed.config.default_map_cols = 32;
    ed.config.default_map_rows = 32;
    ed.config.min_floor        = -1;
    ed.config.max_floor        = 4;

    ed.map = iso::ChunkMap(ed.config);
    ed.camera = iso::IsoCamera(ed.config, SCREEN_W, SCREEN_H);
    ed.camera.set_position({16.f, 16.f, 0.f});
    ed.camera.set_zoom(0.25f);

    // Load tiles from command line arg or default path
    std::string tiles_dir = (argc > 1) ? argv[1] : "assets/tiles";
    int loaded = ed.catalog.load_directory(tiles_dir);
    if (loaded == 0) {
        TraceLog(LOG_WARNING, "No tiles loaded from '%s'. Drop PNG files onto the editor, "
                              "or run with: editor.exe <tiles_directory>", tiles_dir.c_str());
    }

    // ── Main loop ─────────────────────────────

    while (!WindowShouldClose()) 
    {
        float dt = GetFrameTime();
        ed.status.update(dt);

        // Handle window resize
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        ed.camera.set_viewport(sw, sh);

        // Mouse → tile
        iso::TileCoord hover_tile = ed.camera.screen_to_tile(
            iso::from_rl(GetMousePosition()), ed.current_floor
        );

        // Input
        handle_input(ed, hover_tile);
        handle_file_drop(ed);

        // ── Draw ──────────────────────────────
        BeginDrawing();
        ClearBackground(iso_ed::ui::theme.bg_dark);

        // Map viewport
        draw_map_viewport(ed, hover_tile);

        // UI panels (drawn on top)
        draw_palette(ed);
        draw_toolbar(ed);
        if (ed.show_props_panel)
            draw_props_panel(ed);
        draw_statusbar(ed, hover_tile);

        draw_minimap(ed);
        // FPS (debug)
        DrawFPS(SCREEN_W - 80, 4);

        EndDrawing();
    }

    ed.catalog.unload_all();
    CloseWindow();
    return 0;
}
