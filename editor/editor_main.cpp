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
#include "..//iso/map.h"
#include "editor_ui.h"
#include "entity_layer.h"

#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <unordered_set>
#include "..//iso/renderer.h"

#include "collision_editor.h"
static iso::MapRenderer renderer;
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
    iso::TileCollisionDefs colDefs;
    iso::InstanceCollisionOverrides colOverrides;

    iso_ed::CollisionEditor colEditor;

    iso::IsoConfig config;

    bool visibleLayer[5] = {true,true,true,true ,true };    //LayerType

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

    // Selection
    struct Selection {
        enum class Type { None, Tile, Entity } type = Type::None;

        // Tile selection
        iso::TileCoord tile_coord{};
        iso::LayerType tile_layer = iso::LayerType::Ground;
        iso::TileData  tile_data{};

        // Entity selection
        uint32_t entity_id = 0;

        void clear() { type = Type::None; entity_id = 0; }
        bool has() const { return type != Type::None; }
        bool is_tile() const { return type == Type::Tile; }
        bool is_entity() const { return type == Type::Entity; }
    } selection;
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

static void draw_palette(EditorState& ed) 
{
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

            if (ed.colEditor.active)
                ed.colEditor.selected_tile_id = tile->id;
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

static void draw_toolbar(EditorState& ed) 
{
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
    Rectangle grid_r = { x, 4, 50, 28 };
    toggle(grid_r, "Grid", &ed.show_grid);
    x += 58;

    // Layer visibility toggles
    draw_label("Vis:", (int)x, 11, 12, theme.text_dim);
    x += 30;
    const char* vis_labels[] = { "G", "W", "O", "R", "E"};  // Ground, Wall, Object, Roof
    const Color vis_colors[] = {
        {80, 180, 80, 255},    // green
        {180, 140, 100, 255},  // brown
        {100, 160, 220, 255},  // blue
        {180, 100, 100, 255},   // red
        {180, 200, 100, 255}   // red
    };
    for (int i = 0; i < 5; ++i) {
        Rectangle btn = { x, 4, 28, 28 };
        Color bg = ed.visibleLayer[i]
            ? Color{ vis_colors[i].r, vis_colors[i].g, vis_colors[i].b, 180 }
        : Color{ 50, 50, 60, 255 };
        DrawRectangleRec(btn, bg);
        DrawRectangleLinesEx(btn, 1.f, ed.visibleLayer[i] ? vis_colors[i] : theme.border);
        draw_label_centered(vis_labels[i], btn, 12,
            ed.visibleLayer[i] ? theme.text_bright : theme.text_dim);
        Vector2 mp = GetMousePosition();
        if (CheckCollisionPointRec(mp, btn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            ed.visibleLayer[i] = !ed.visibleLayer[i];
        x += 30;
    }
}

// ─── Properties panel ─────────────────────────

static void draw_props_panel(EditorState& ed) {
    using namespace iso_ed::ui;
    if (!ed.show_props_panel) return;

    float px = GetScreenWidth() - PROPS_WIDTH;
    Rectangle panel = { px, TOOLBAR_HEIGHT, PROPS_WIDTH,
                       (float)(GetScreenHeight() - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT) };
    draw_panel(panel);

    float y = panel.y + 8;
    float x = panel.x + 8;
    float w = PROPS_WIDTH - 16;

    if (!ed.selection.has()) {
        // ── No selection — show paint flags ───
        draw_label("Paint flags", (int)x, (int)y, 14, theme.text_bright); y += 22;
        toggle({ x, y, w, 22 }, "Solid", &ed.flag_solid); y += 26;
        toggle({ x, y, w, 22 }, "Blocks LOS", &ed.flag_blocks_los); y += 26;
        toggle({ x, y, w, 22 }, "Platform", &ed.flag_platform); y += 26;
        toggle({ x, y, w, 22 }, "Climbable", &ed.flag_climbable); y += 26;
        toggle({ x, y, w, 22 }, "Half-height", &ed.flag_half_height); y += 26;
        toggle({ x, y, w, 22 }, "Door", &ed.flag_door); y += 34;

        // Selected tile preview
        draw_label("Selected tile", (int)x, (int)y, 14, theme.text_bright); y += 22;
        if (ed.selected_tile_id > 0) {
            const auto* te = ed.catalog.get(ed.selected_tile_id);
            if (te) {
                draw_label(TextFormat("ID: %d  %s", te->id, te->name.c_str()),
                    (int)x, (int)y, 12, theme.text); y += 16;
                draw_label(TextFormat("Size: %dx%d", te->src_width, te->src_height),
                    (int)x, (int)y, 12, theme.text); y += 22;
                float scale = std::min((w - 8) / (float)te->texture.width, 100.f / (float)te->texture.height);
                DrawTextureEx(te->texture, { x, y }, 0.f, scale, WHITE);
            }
        }
        return;
    }

    if (ed.selection.is_tile()) {
        // ── Tile properties ───────────────────
        auto& sel = ed.selection;
        auto td = ed.map.tile(sel.tile_layer, sel.tile_coord);

        draw_label("TILE PROPERTIES", (int)x, (int)y, 14, theme.text_bright); y += 20;
        draw_label(TextFormat("Pos: %d, %d  Floor: %d",
            sel.tile_coord.col, sel.tile_coord.row, sel.tile_coord.floor),
            (int)x, (int)y, 12, theme.text); y += 16;
        draw_label(TextFormat("Layer: %s  ID: %d",
            layer_names[(int)sel.tile_layer], td.tile_id),
            (int)x, (int)y, 12, theme.text); y += 16;
        draw_label(TextFormat("Height: %d  Variant: %d",
            td.height, td.variant),
            (int)x, (int)y, 12, theme.text); y += 22;

        // Editable flags
        draw_label("Flags:", (int)x, (int)y, 13, theme.text_bright); y += 18;

        bool solid = td.is_solid();
        bool los = td.blocks_los();
        bool platform = td.is_platform();
        bool climb = iso::has_flag(td.flags, iso::TileFlags::Climbable);
        bool half = iso::has_flag(td.flags, iso::TileFlags::HalfHeight);
        bool door = iso::has_flag(td.flags, iso::TileFlags::Door);

        bool changed = false;
        if (toggle({ x, y, w, 20 }, "Solid", &solid)) changed = true; y += 24;
        if (toggle({ x, y, w, 20 }, "Blocks LOS", &los)) changed = true; y += 24;
        if (toggle({ x, y, w, 20 }, "Platform", &platform)) changed = true; y += 24;
        if (toggle({ x, y, w, 20 }, "Climbable", &climb)) changed = true; y += 24;
        if (toggle({ x, y, w, 20 }, "Half-height", &half)) changed = true; y += 24;
        if (toggle({ x, y, w, 20 }, "Door", &door)) changed = true; y += 28;

        if (changed) {
            iso::TileFlags flags = iso::TileFlags::None;
            if (solid)    flags = flags | iso::TileFlags::Solid;
            if (los)      flags = flags | iso::TileFlags::BlocksLOS;
            if (platform) flags = flags | iso::TileFlags::Platform;
            if (climb)    flags = flags | iso::TileFlags::Climbable;
            if (half)     flags = flags | iso::TileFlags::HalfHeight;
            if (door)     flags = flags | iso::TileFlags::Door;
            td.flags = flags;
            ed.map.set_tile(sel.tile_layer, sel.tile_coord, td);
            ed.modified = true;
        }

        // Tile preview
        const auto* te = ed.catalog.get(td.tile_id);
        if (te && te->loaded) {
            float scale = std::min((w - 8) / (float)te->texture.width, 80.f / (float)te->texture.height);
            DrawRectangle((int)x, (int)y, (int)w, (int)(te->texture.height * scale) + 8, theme.bg_item);
            DrawTextureEx(te->texture, { x + 4, y + 4 }, 0.f, scale, WHITE);
            y += te->texture.height * scale + 16;
        }

        // Delete tile button
        Rectangle del_btn = { x, y, w, 24 };
        if (button(del_btn, "Delete tile")) {
            ed.map.set_tile(sel.tile_layer, sel.tile_coord, iso::TileData{});
            ed.selection.clear();
            ed.modified = true;
        }

    }
    else if (ed.selection.is_entity()) {
        // ── Entity properties ─────────────────
        iso::MapEntity* ent = ed.map.entities().find(ed.selection.entity_id);
        if (!ent) { ed.selection.clear(); return; }

        draw_label("ENTITY PROPERTIES", (int)x, (int)y, 14, theme.text_bright); y += 20;
        draw_label(TextFormat("ID: %d  Tile: %d", ent->id, ent->tile_id),
            (int)x, (int)y, 12, theme.text); y += 16;
        draw_label(TextFormat("Pos: %.2f, %.2f  Floor: %d", ent->x, ent->y, ent->floor),
            (int)x, (int)y, 12, theme.text); y += 16;
        draw_label(TextFormat("z_bias: %d  rot: %.0f", ent->z_bias, ent->rotation),
            (int)x, (int)y, 12, theme.text); y += 22;

        // Type
        draw_label("Type:", (int)x, (int)y, 13, theme.text_bright); y += 18;
        const char* types[] = { "Static", "Dynamic", "Networked" };
        for (int i = 0; i < 3; ++i) {
            Rectangle btn = { x + i * 68.f, y, 64, 20 };
            if (button(btn, types[i], (int)ent->type == i, 10)) {
                ent->type = static_cast<iso::EntityType>(i);
                ed.modified = true;
            }
        }
        y += 26;

        // Flags
        draw_label("Flags:", (int)x, (int)y, 13, theme.text_bright); y += 18;
        bool solid = ent->is_solid();
        bool interact = ent->is_interactable();
        if (toggle({ x, y, w, 20 }, "Solid", &solid)) {
            ent->set_solid(solid); ed.modified = true;
        } y += 24;
        if (toggle({ x, y, w, 20 }, "Interactable", &interact)) {
            if (interact) ent->flags = ent->flags | iso::EntityFlags::Interactable;
            else ent->flags = static_cast<iso::EntityFlags>(
                (uint8_t)ent->flags & ~(uint8_t)iso::EntityFlags::Interactable);
            ed.modified = true;
        } y += 24;

        // z_bias
        draw_label("z_bias:", (int)x, (int)y, 13, theme.text_bright); y += 18;
        Rectangle zbias_r = { x, y, w, 24 };
        int zb = ent->z_bias;
        if (spinner(zbias_r, "Z", &zb, -10, 10)) {
            ent->z_bias = zb;
            ed.modified = true;
        }
        y += 30;

        // Preview
        const auto* te = ed.catalog.get(ent->tile_id);
        if (te && te->loaded) {
            float scale = std::min((w - 8) / (float)te->texture.width, 80.f / (float)te->texture.height);
            DrawRectangle((int)x, (int)y, (int)w, (int)(te->texture.height * scale) + 8, theme.bg_item);
            DrawTextureEx(te->texture, { x + 4, y + 4 }, 0.f, scale, WHITE);
            y += te->texture.height * scale + 16;
        }

        // Delete
        Rectangle del_btn = { x, y, w, 24 };
        if (button(del_btn, "Delete entity")) {
            ed.map.entities().remove(ent->id);
            ed.selection.clear();
            ed.modified = true;
        }
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

    if (ed.colEditor.active)
        draw_label("COLLISION[C]", (int)(x + 240), SCREEN_H - 20, 12, iso_ed::ui::theme.danger);
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

    // Compute visible area
    auto va = iso::VisibleArea::from_camera(ed.camera, cfg, vp_x, vp_y, vp_w, vp_h);

    // Floor visibility
    iso::FloorVis fv;
    fv.current_floor = ed.current_floor;
    fv.show_all_opaque = ed.show_all_opaque;
    fv.min_floor = cfg.min_floor;
    fv.max_floor = cfg.max_floor;

    // LOD
    iso::RenderLOD lod;
    lod.update(ed.camera.zoom(), cfg.tile_width);

    // Collect & sort
    renderer.collect(ed.map, ed.camera, va, fv, lod);
    renderer.sort();

    // Draw — just provide your draw functions
    float zoom = ed.camera.zoom();
    renderer.render({

        
        .on_ground = [&](const iso::DrawItem& item) 
        {
            if (!ed.visibleLayer[0])
                return;

            const auto* te = ed.catalog.get(item.tile.tile_id);
            if (te && te->loaded && !lod.use_simple_draw(va.tile_screen_size)) {
                float tw = te->src_width * zoom + cfg.tile_padding;
                float th = te->src_height * zoom + cfg.tile_padding;
                unsigned char a = (unsigned char)(255 * item.alpha);
                // floor вместо просто вычитания — гарантирует что тайлы всегда перекрываются
                float dx = std::floor(item.screen_pos.x - tw * 0.5f);
                float dy = std::floor(item.screen_pos.y - th * 0.5f);
                Rectangle src = { 0, 0, (float)te->src_width, (float)te->src_height };
                Rectangle dst = { dx, dy, std::ceil(tw), std::ceil(th) };
                DrawTexturePro(te->texture, src, dst, { 0,0 }, 0.f, { 255,255,255,a });
            }
         else {
          float hw = cfg.tile_width * 0.5f * zoom;
          float hh = cfg.tile_height * 0.5f * zoom;
          unsigned char a = (unsigned char)(180 * item.alpha);
          Vector2 pts[4] = {
              {item.screen_pos.x, item.screen_pos.y - hh},
              {item.screen_pos.x + hw, item.screen_pos.y},
              {item.screen_pos.x, item.screen_pos.y + hh},
              {item.screen_pos.x - hw, item.screen_pos.y}};
          DrawTriangle(pts[0], pts[3], pts[2], {80,130,60,a});
          DrawTriangle(pts[0], pts[2], pts[1], {80,130,60,a});
        }
        },
        .on_wall = [&](const iso::DrawItem& item) 
        {
            if (!ed.visibleLayer[1])
                return;

            const auto* te = ed.catalog.get(item.tile.tile_id);
            if (!te || !te->loaded) return;
            float tw = te->src_width * zoom;
            float th = te->src_height * zoom;
            float screen_hh = cfg.tile_height * 0.5f * zoom;
            unsigned char a = (unsigned char)(255 * item.alpha);
            Rectangle src = {0, 0, (float)te->src_width, (float)te->src_height};
            Rectangle dst = {item.screen_pos.x - tw * 0.5f,
                            item.screen_pos.y + screen_hh - th, tw, th};
            DrawTexturePro(te->texture, src, dst, {0,0}, 0.f, {255,255,255,a});
        },
        .on_object = [&](const iso::DrawItem& item) 
        {
            if (!ed.visibleLayer[2])
                return;
            // Same as on_wall (bottom-center anchor)
            const auto* te = ed.catalog.get(item.tile.tile_id);
            if (!te || !te->loaded) return;
            float tw = te->src_width * zoom;
            float th = te->src_height * zoom;
            float screen_hh = cfg.tile_height * 0.5f * zoom;
            unsigned char a = (unsigned char)(255 * item.alpha);
            Rectangle src = {0, 0, (float)te->src_width, (float)te->src_height};
            Rectangle dst = {item.screen_pos.x - tw * 0.5f,
                            item.screen_pos.y + screen_hh - th, tw, th};
            DrawTexturePro(te->texture, src, dst, {0,0}, 0.f, {255,255,255,a});
        },
        .on_overlay = [&](const iso::DrawItem& item) 
        {
            if (!ed.visibleLayer[3])
                return;
            // Same anchor as wall
            const auto* te = ed.catalog.get(item.tile.tile_id);
            if (!te || !te->loaded) return;
            float tw = te->src_width * zoom;
            float th = te->src_height * zoom;
            float screen_hh = cfg.tile_height * 0.5f * zoom;
            unsigned char a = (unsigned char)(255 * item.alpha);
            Rectangle src = {0, 0, (float)te->src_width, (float)te->src_height};
            Rectangle dst = {item.screen_pos.x - tw * 0.5f,
                            item.screen_pos.y + screen_hh - th, tw, th};
            DrawTexturePro(te->texture, src, dst, {0,0}, 0.f, {255,255,255,a});
        },
        .on_entity = [&](const iso::DrawItem& item) 
        {
            if (!ed.visibleLayer[4])
                return;
            const auto* te = ed.catalog.get(item.entity->tile_id);
            if (!te || !te->loaded) return;
            float tw = te->src_width * zoom;
            float th = te->src_height * zoom;
            float screen_hh = cfg.tile_height * 0.5f * zoom;
            unsigned char a = (unsigned char)(255 * item.alpha);
            Rectangle src = {0, 0, (float)te->src_width, (float)te->src_height};
            Rectangle dst = {item.screen_pos.x - tw * 0.5f,
                            item.screen_pos.y + screen_hh - th, tw, th};
            DrawTexturePro(te->texture, src, dst, {0,0}, 0.f, {255,255,255,a});

            if (item.entity->id == ed.selected_entity_id) {
                DrawRectangleLinesEx(dst, 2.f, YELLOW);
                DrawText(TextFormat("z: %d", item.entity->z_bias),
                    (int)dst.x, (int)dst.y - 16, 12, YELLOW);
            }
        }
        });


    // Grid — рисуем вокруг видимой области
    if (ed.show_grid) 
    {
        
        iso::draw_tile_grid(ed.camera, cfg, va.col_min, va.row_min, va.col_max, va.row_max, ed.current_floor, { 60, 60, 80, 40 });
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

    ed.colEditor.draw_on_map(ed.map, ed.colDefs, ed.camera, ed.config, ed.current_floor);

    EndScissorMode();


    // Selection highlight
    if (ed.selection.is_tile()) {
        iso::draw_tile_highlight(ed.camera, cfg, ed.selection.tile_coord, { 0, 200, 255, 80 });
    }
    if (ed.selection.is_entity()) {
        iso::MapEntity* ent = ed.map.entities().find(ed.selection.entity_id);
        if (ent) {
            iso::Vec2 sp = ed.camera.world_to_screen({ ent->x, ent->y,
                ed.current_floor * cfg.floor_height });
            DrawCircle((int)sp.x, (int)sp.y, 8.f * ed.camera.zoom(), { 0, 200, 255, 120 });
        }
    }
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
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);


    bool lmb_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool lmb_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool rmb_down = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

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

    if (IsKeyPressed(KEY_C)) ed.colEditor.toggle();

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

    // ── Shift+Click = select for properties ───
    if (!ed.colEditor.active && !ed.entity_mode && lmb_pressed && shift)
    {
        // Try entity first
        iso::Vec3 world = ed.camera.screen_to_world(
            iso::from_rl(GetMousePosition()),
            ed.current_floor * ed.config.floor_height);
        float sx = world.x, sy = world.y;

        auto* hit = ed.map.entities().pick(sx, sy, ed.current_floor, 0.6f);
        if (hit) {
            ed.selection.type = EditorState::Selection::Type::Entity;
            ed.selection.entity_id = hit->id;
            ed.show_props_panel = true;
            ed.status.set(TextFormat("Selected entity #%d", hit->id),
                iso_ed::ui::theme.accent);
        }
        else {
            // Try tile on current layer
            auto layer = static_cast<iso::LayerType>(ed.current_layer);
            auto td = ed.map.tile(layer, { hover_tile.col, hover_tile.row, ed.current_floor });
            if (!td.is_empty()) {
                ed.selection.type = EditorState::Selection::Type::Tile;
                ed.selection.tile_coord = { hover_tile.col, hover_tile.row, ed.current_floor };
                ed.selection.tile_layer = layer;
                ed.selection.tile_data = td;
                ed.show_props_panel = true;
                ed.status.set(TextFormat("Selected tile %d at %d,%d",
                    td.tile_id, hover_tile.col, hover_tile.row),
                    iso_ed::ui::theme.accent);
            }
        }
        return; // don't process other tools
    }

    // ESC clears selection
    if (IsKeyPressed(KEY_ESCAPE) && ed.selection.has()) {
        ed.selection.clear();
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

            std::string col_path = ed.current_filepath.substr(0, ed.current_filepath.rfind('.')) + ".isocol";
            ed.colDefs.save(col_path);

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

    if (ed.colEditor.active)  return;


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

static void handle_file_drop(EditorState& ed) 
{
    if (!IsFileDropped()) return;

    FilePathList files = LoadDroppedFiles();
    for (int i = 0; i < (int)files.count; ++i) 
    {
        std::string path = files.paths[i];
        std::string ext = GetFileExtension(path.c_str());

        if (ext == ".isomap") 
        {
            if (iso::load_isomap(ed.map, path)) 
            {
                ed.config = ed.map.config();
                ed.camera.set_config(ed.config);
                ed.current_filepath = path;
                ed.modified = false;
                ed.current_floor = 0;
                ed.undo_stack.clear();
                ed.redo_stack.clear();
                ed.status.set(TextFormat("Loaded! (%d chunks)", ed.map.chunk_count()), iso_ed::ui::theme.success);

                std::string col_path = path.substr(0, path.rfind('.')) + ".isocol";
                bool col = ed.colDefs.load(col_path);  // ok if file doesn't exist
                ed.colOverrides.load("assets/maps/map.iscov");
                if (col)
                {
                    ed.status.set("collisions Loaded! ", iso_ed::ui::theme.success);

                }
            }
        }
        else if (ext == ".png" || ext == ".jpg" || ext == ".bmp") 
        {
            // Add tile to catalog
            int id = ed.catalog.add_tile(path);
            if (id >= 0) 
            {
                ed.status.set(TextFormat("Added tile: %s", GetFileName(path.c_str())), iso_ed::ui::theme.success);
            }
        }
    }
    UnloadDroppedFiles(files);
}

// ─── Main ─────────────────────────────────────

int main(int argc, char* argv[]) 
{
    // Window
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "iso_engine — Tile Map Editor");
    SetTargetFPS(60);
    SetExitKey(0); // disable ESC close

    // ── Init editor state ─────────────────────

    EditorState ed;

    ed.config.tile_width       = 256 - ed.config.tile_padding;     // match your tileset dimensions!
    ed.config.tile_height      = 128 - ed.config.tile_padding;
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

  //  ed.colResolver = iso::CollisionResolver(&ed.colDefs, &ed.colOverrides);

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

        if (ed.colEditor.active) {
            float col_panel_w = 320;
            float col_panel_x = GetScreenWidth() - col_panel_w - 4;
            float col_panel_y = TOOLBAR_HEIGHT + 4;
            float col_panel_h = GetScreenHeight() - TOOLBAR_HEIGHT - STATUSBAR_HEIGHT - 8;
            ed.colEditor.draw(ed.colDefs, ed.catalog, ed.config, col_panel_x, col_panel_y, col_panel_w, col_panel_h);
        }
        // FPS (debug)
        DrawFPS(SCREEN_W - 80, 4);

        EndDrawing();
    }

    ed.catalog.unload_all();
    CloseWindow();
    return 0;
}
