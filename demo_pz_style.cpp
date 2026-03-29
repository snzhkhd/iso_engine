//// ═══════════════════════════════════════════════════════
////  iso_engine example — Project Zomboid-style demo
////  Build with RayLib 5.x + MSVC C++20
//// ═══════════════════════════════════════════════════════
//
//#include "raylib.h"
//#include "iso/iso_raylib.h"  // includes everything + RayLib helpers
//
//#include <cstdio>
//
//// ─── Game state ───────────────────────────────
//
//struct Player {
//    iso::CollisionBody body;
//    iso::Vec3 velocity{};
//    float speed = 3.0f;
//    int current_floor = 0;
//    bool on_ground = true;
//};
//
//// ─── Build a test map ─────────────────────────
//
//void build_test_map(iso::IsoMap& map) {
//    // Floor 0: ground level
//    {
//        auto& f = map.floor(0);
//
//        // Fill entire floor with grass
//        iso::fill_ground(f, 0, 0, map.cols()-1, map.rows()-1, 1); // tile_id=1 = grass
//
//        // A building (10x8) with walls
//        iso::fill_ground(f, 5, 5, 14, 12, 2); // tile_id=2 = wooden floor
//        iso::place_walls(f, 5, 5, 14, 12, 10); // tile_id=10 = wall
//
//        // Door on the south wall
//        f.tile(iso::LayerType::Wall, 9, 12) = {}; // remove wall = door opening
//        f.tile(iso::LayerType::Wall, 10, 12) = {};
//
//        // Interior wall dividing rooms
//        for (int r = 5; r <= 9; ++r)
//            f.tile(iso::LayerType::Wall, 10, r) = {10, iso::TileFlags::Solid | iso::TileFlags::BlocksLOS};
//
//        // Door in interior wall
//        f.tile(iso::LayerType::Wall, 10, 7) = {};
//
//        // Furniture (objects — solid but shorter)
//        iso::TileData table = {20, iso::TileFlags::Solid, 1, 0, 0}; // tile_id=20 = table
//        f.tile(iso::LayerType::Object, 7, 7) = table;
//        f.tile(iso::LayerType::Object, 8, 7) = table;
//
//        // Stairs going up (inside building)
//        iso::TileData stairs = {30, iso::TileFlags::Climbable | iso::TileFlags::Platform, 1, 0, 0};
//        f.tile(iso::LayerType::Object, 12, 6) = stairs;
//        f.tile(iso::LayerType::Object, 12, 7) = stairs;
//
//        // Another small building
//        iso::fill_ground(f, 20, 3, 26, 8, 2);
//        iso::place_walls(f, 20, 3, 26, 8, 10);
//        f.tile(iso::LayerType::Wall, 23, 8) = {}; // door
//
//        // Fence/low wall example
//        for (int c = 16; c <= 19; ++c) {
//            f.tile(iso::LayerType::Wall, c, 10) = {
//                11, iso::TileFlags::Solid | iso::TileFlags::HalfHeight
//            };
//        }
//    }
//
//    // Floor 1: second story of the building
//    {
//        auto& f = map.floor(1);
//
//        // Second floor of main building
//        iso::fill_ground(f, 5, 5, 14, 12, 3); // tile_id=3 = second floor
//        iso::place_walls(f, 5, 5, 14, 12, 10);
//
//        // Stairs landing
//        iso::TileData stairs_top = {31, iso::TileFlags::Climbable | iso::TileFlags::Platform, 1, 0, 0};
//        f.tile(iso::LayerType::Object, 12, 6) = stairs_top;
//        f.tile(iso::LayerType::Object, 12, 7) = stairs_top;
//
//        // Roof tiles (overlay — drawn above entities)
//        for (int r = 4; r <= 13; ++r)
//            for (int c = 4; c <= 15; ++c)
//                f.tile(iso::LayerType::Overlay, c, r) = {40, iso::TileFlags::None}; // roof
//    }
//
//    // Floor -1: basement
//    {
//        auto& f = map.floor(-1);
//
//        // Small basement under main building
//        iso::fill_ground(f, 6, 6, 13, 11, 4); // tile_id=4 = basement floor
//        iso::place_walls(f, 6, 6, 13, 11, 12); // tile_id=12 = stone wall
//    }
//}
//
//// ─── Main ─────────────────────────────────────
//
//int main() {
//    // RayLib init
//    const int screen_w = 1600;
//    const int screen_h = 900;
//    InitWindow(screen_w, screen_h, "iso_engine demo — Project Zomboid Style");
//    SetTargetFPS(60);
//
//    // ── Engine setup ──────────────────────────
//
//    iso::IsoConfig config;
//    config.tile_width          = 128;
//    config.tile_height         = 64;
//    config.floor_height        = 1.0f;
//    config.height_pixel_offset = 64.0f;
//    config.default_map_cols    = 32;
//    config.default_map_rows    = 32;
//    config.min_floor           = -1;
//    config.max_floor           = 2;
//
//    iso::IsoMap map(config);
//    build_test_map(map);
//
//    iso::CollisionSystem collision(map);
//    iso::ZSorter sorter;
//    iso::IsoCamera camera(config, screen_w, screen_h);
//
//    iso::FloorVisibility floor_vis;
//
//    // ── Player ────────────────────────────────
//
//    Player player;
//    player.body.position    = {16.5f, 16.5f, 0.f}; // start outside
//    player.body.half_extents = {0.3f, 0.3f};
//    player.body.body_height = 1.8f;
//
//    // 3D entity sprite (for the player character)
//    iso::EntitySprite player_sprite;
//    player_sprite.init(128, 256);
//
//    // ── Camera init ───────────────────────────
//
//    camera.set_position({player.body.position.x, player.body.position.y, 0.f});
//    camera.set_zoom(1.0f);
//
//    // ── Colors for tile types (placeholder rendering) ──
//
//    auto tile_color = [](uint16_t tile_id, float alpha) -> Color {
//        switch (tile_id) {
//            case 1:  return {80, 140, 60, (unsigned char)(255*alpha)};   // grass
//            case 2:  return {160, 120, 80, (unsigned char)(255*alpha)};  // wood floor
//            case 3:  return {140, 100, 70, (unsigned char)(255*alpha)};  // 2nd floor
//            case 4:  return {100, 100, 110, (unsigned char)(255*alpha)}; // basement
//            case 10: return {180, 160, 140, (unsigned char)(255*alpha)}; // wall
//            case 11: return {150, 140, 120, (unsigned char)(255*alpha)}; // fence
//            case 12: return {120, 120, 130, (unsigned char)(255*alpha)}; // stone wall
//            case 20: return {120, 80, 40, (unsigned char)(255*alpha)};   // table
//            case 30: case 31: return {200, 180, 100, (unsigned char)(255*alpha)}; // stairs
//            case 40: return {140, 80, 80, (unsigned char)(160*alpha)};   // roof
//            default: return {200, 200, 200, (unsigned char)(255*alpha)};
//        }
//    };
//
//    // ── Main loop ─────────────────────────────
//
//    while (!WindowShouldClose()) {
//        float dt = GetFrameTime();
//
//        // ── Input: player movement ────────────
//        iso::Vec3 move_dir = {};
//        if (IsKeyDown(KEY_W)) { move_dir.x -= 1; move_dir.y -= 1; }
//        if (IsKeyDown(KEY_S)) { move_dir.x += 1; move_dir.y += 1; }
//        if (IsKeyDown(KEY_A)) { move_dir.x -= 1; move_dir.y += 1; }
//        if (IsKeyDown(KEY_D)) { move_dir.x += 1; move_dir.y -= 1; }
//
//        if (move_dir.xz().length_sq() > 0.f) {
//            iso::Vec2 dir2 = move_dir.xz().normalized();
//            move_dir.x = dir2.x;
//            move_dir.y = dir2.y;
//        }
//
//        // Floor change (stairs)
//        if (IsKeyPressed(KEY_PAGE_UP)) {
//            iso::Vec3 up_pos = player.body.position;
//            up_pos.z += config.floor_height;
//            if (!collision.check_overlap_at(player.body, up_pos))
//                player.body.position.z += config.floor_height;
//        }
//        if (IsKeyPressed(KEY_PAGE_DOWN)) {
//            iso::Vec3 dn_pos = player.body.position;
//            dn_pos.z -= config.floor_height;
//            if (dn_pos.z >= config.min_floor * config.floor_height &&
//                !collision.check_overlap_at(player.body, dn_pos))
//                player.body.position.z -= config.floor_height;
//        }
//
//        // Move with collision
//        iso::Vec3 delta = move_dir * player.speed * dt;
//        auto move_result = collision.move(player.body, delta);
//        player.body.position = move_result.new_position;
//        player.on_ground     = move_result.on_ground;
//        player.current_floor = move_result.floor_level;
//
//        // ── Camera ────────────────────────────
//        camera.follow(player.body.position, dt, 8.0f);
//        iso::handle_camera_zoom(camera, 0.1f);
//
//        // Floor visibility
//        floor_vis.player_floor = player.current_floor;
//
//        // ── Z-Sort ────────────────────────────
//        sorter.begin_frame();
//
//        auto frustum = camera.get_frustum(config.min_floor, config.max_floor);
//        // Only submit visible floors
//        frustum.min_floor = std::max(frustum.min_floor, config.min_floor);
//        frustum.max_floor = std::min(frustum.max_floor, player.current_floor + floor_vis.floors_above);
//
//        sorter.submit_tiles(map, frustum, config);
//
//        // Submit player entity
//        sorter.submit_entity(
//            0,                      // entity id
//            player.body.position,
//            config,
//            -1,                     // texture_id (custom rendering)
//            0,                      // sprite_index
//            {64.f, 230.f},          // origin offset (feet position in sprite)
//            1.0f                    // scale
//        );
//
//        sorter.sort();
//
//        // ── Render 3D player into sprite texture ──
//        // (In real game: set up 3D camera, render model here)
//        player_sprite.begin_render(BLANK);
//        // Placeholder: draw a colored rectangle as "3D character"
//        DrawRectangle(32, 40, 64, 180, BLUE);
//        DrawRectangle(40, 20, 48, 48, {200, 170, 140, 255}); // head
//        DrawCircle(64, 44, 20, {200, 170, 140, 255});
//        player_sprite.end_render();
//
//        // ── Draw ──────────────────────────────
//        BeginDrawing();
//        ClearBackground({30, 30, 35, 255});
//
//        // Draw all renderables in sorted order
//        sorter.render({
//            // Ground tiles
//            .on_tile_ground = [&](const iso::Renderable& r) {
//                float alpha = floor_vis.get_alpha(r.floor_level);
//                if (alpha <= 0.f) return;
//   
//                iso::Vec2 sp = camera.world_to_screen(r.world_pos);  // уже snap'нутая
//                Color c = tile_color(r.sprite_index, alpha);
//                iso::draw_iso_diamond(camera, sp, c);
//            },
//
//            // Walls
//            .on_tile_wall = [&](const iso::Renderable& r) {
//                float alpha = floor_vis.get_alpha(r.floor_level);
//                if (alpha <= 0.f) return;
//                iso::Vec2 base = camera.world_to_screen(r.world_pos);
//                float wall_h = config.height_pixel_offset * camera.zoom();
//                Color c = tile_color(r.sprite_index, alpha);
//                DrawRectangle((int)(base.x - 8), (int)(base.y - wall_h),
//                              16, (int)wall_h, c);
//            },
//
//            // Objects
//            .on_tile_object = [&](const iso::Renderable& r) {
//                float alpha = floor_vis.get_alpha(r.floor_level);
//                if (alpha <= 0.f) return;
//                iso::Vec2 sp = camera.world_to_screen(r.world_pos);
//                Color c = tile_color(r.sprite_index, alpha);
//                DrawRectangle((int)(sp.x - 12), (int)(sp.y - 16), 24, 24, c);
//            },
//
//            // Entities (player, NPCs)
//            .on_entity = [&](const iso::Renderable& r) {
//                iso::Vec2 sp = camera.world_to_screen(r.world_pos);
//                // Draw the pre-rendered 3D sprite
//                player_sprite.draw_at(sp, {64.f, 230.f}, camera.zoom());
//            },
//
//            // Roof overlay
//            .on_tile_overlay = [&](const iso::Renderable& r) {
//                float alpha = floor_vis.get_alpha(r.floor_level);
//                if (alpha <= 0.f) return;
//                iso::Vec2 sp = camera.world_to_screen(r.world_pos);
//                int hw = config.tile_width / 2;
//                int hh = config.tile_height / 2;
//                Vector2 pts[4] = {
//                    {sp.x, sp.y - (float)hh},
//                    {sp.x + (float)hw, sp.y},
//                    {sp.x, sp.y + (float)hh},
//                    {sp.x - (float)hw, sp.y}
//                };
//                Color c = tile_color(r.sprite_index, alpha);
//                DrawTriangle(pts[0], pts[3], pts[2], c);
//                DrawTriangle(pts[0], pts[2], pts[1], c);
//            },
//
//            // Effects
//            .on_effect = [&](const iso::Renderable& r) {
//                // draw particles, shadows, etc.
//            }
//        });
//
//        // ── Debug overlay ─────────────────────
//        iso::TileCoord hover = camera.screen_to_tile(
//            iso::from_rl(GetMousePosition()), player.current_floor
//        );
//        iso::draw_tile_highlight(camera, config, hover, {255, 255, 0, 60});
//        iso::draw_collision_body(camera, config, player.body, GREEN);
//
//        // HUD
//        DrawFPS(10, 10);
//        DrawText(TextFormat("Player: %.1f, %.1f, z=%.1f  Floor: %d",
//                 player.body.position.x, player.body.position.y,
//                 player.body.position.z, player.current_floor),
//                 10, 30, 18, WHITE);
//        DrawText(TextFormat("Tile: %d, %d", hover.col, hover.row), 10, 52, 18, WHITE);
//        DrawText(TextFormat("Renderables: %d  Zoom: %.2f",
//                 (int)sorter.count(), camera.zoom()),
//                 10, 74, 18, WHITE);
//        DrawText("WASD=move  PgUp/Dn=floor  Scroll=zoom", 10, screen_h - 25, 16, GRAY);
//
//        EndDrawing();
//    }
//
//    player_sprite.unload();
//    CloseWindow();
//    return 0;
//}
