#pragma once
// iso_engine — RayLib integration helpers
// Include this AFTER raylib.h
// ────────────────────────────────────────

#include "iso.h"
#include "raylib.h"

// Check that raylib is available
#ifndef RAYLIB_H
    #error "Include raylib.h before iso_raylib.h"
#endif

namespace iso {

// ─── Type conversions ─────────────────────────

inline Vector2 to_rl(Vec2 v) { return {v.x, v.y}; }
inline Vector3 to_rl(Vec3 v) { return {v.x, v.y, v.z}; }
inline Vec2 from_rl(Vector2 v) { return {v.x, v.y}; }
inline Vec3 from_rl(Vector3 v) { return {v.x, v.y, v.z}; }

// ─── Input helpers ────────────────────────────

/// Get mouse position in world space through the camera
inline Vec3 get_mouse_world(const IsoCamera& cam, int floor = 0) {
    Vector2 mp = GetMousePosition();
    return cam.screen_to_world({mp.x, mp.y}, floor * cam.position().z);
}

/// Get tile under mouse cursor
inline TileCoord get_mouse_tile(const IsoCamera& cam, const IsoConfig& cfg, int floor = 0) {
    Vector2 mp = GetMousePosition();
    return cam.screen_to_tile({mp.x, mp.y}, floor);
}

/// Handle camera zoom with mouse wheel
inline void handle_camera_zoom(IsoCamera& cam, float sensitivity = 0.1f) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0.f)
        cam.zoom_by(wheel * sensitivity * cam.zoom());
}

/// Handle camera pan with middle mouse or right mouse drag
inline void handle_camera_drag(IsoCamera& cam, int button = MOUSE_BUTTON_MIDDLE) {
    if (IsMouseButtonDown(button)) {
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0.f || delta.y != 0.f) {
            float inv_zoom = 1.0f / cam.zoom();
            // Convert screen delta to world delta
            // In isometric, screen movement maps non-trivially to world
            // Approximate: move camera target to counteract screen drag
            cam.position().x -= (delta.x + delta.y) * inv_zoom * 0.005f;
            cam.position().y -= (-delta.x + delta.y) * inv_zoom * 0.005f;
        }
    }
}

/// Handle WASD camera movement
inline void handle_camera_wasd(IsoCamera& cam, float speed, float dt) {
    Vec2 dir = {};
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    { dir.x -= 1; dir.y -= 1; }
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   { dir.x += 1; dir.y += 1; }
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   { dir.x -= 1; dir.y += 1; }
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  { dir.x += 1; dir.y -= 1; }

    if (dir.length_sq() > 0.f) {
        dir = dir.normalized();
        cam.move(dir * speed * dt / cam.zoom());
    }
}

// ─── Tile rendering (seam-free) ───────────────

/// Draw a filled isometric diamond at screen position, using camera's padded dimensions.
/// This is the primary way to draw floor tiles without seams.
inline void draw_iso_diamond(const IsoCamera& cam, Vec2 screen_center, Color fill) {
    float hw = cam.tile_draw_hw();
    float hh = cam.tile_draw_hh();
    Vector2 pts[4] = {
        {screen_center.x,      screen_center.y - hh},  // top
        {screen_center.x + hw, screen_center.y},        // right
        {screen_center.x,      screen_center.y + hh},  // bottom
        {screen_center.x - hw, screen_center.y},        // left
    };
    DrawTriangle(pts[0], pts[3], pts[2], fill);
    DrawTriangle(pts[0], pts[2], pts[1], fill);
}

/// Draw a filled diamond using a texture/sprite (source rect from tileset)
inline void draw_iso_tile_sprite(const IsoCamera& cam, Vec2 screen_center,
                                  Texture2D tileset, Rectangle src_rect,
                                  Color tint = WHITE) {
    float dw = cam.tile_draw_width();
    float dh = cam.tile_draw_height();
    Rectangle dst = {
        screen_center.x - dw * 0.5f,
        screen_center.y - dh * 0.5f,
        dw, dh
    };
    DrawTexturePro(tileset, src_rect, dst, {0, 0}, 0.f, tint);
}

// ─── Debug drawing ────────────────────────────

/// Draw tile grid overlay (debug)
inline void draw_tile_grid(const IsoCamera& cam, const IsoConfig& cfg,
                           int min_col, int min_row, int max_col, int max_row,
                           int floor = 0, Color color = {100, 100, 100, 80})
{
    for (int row = min_row; row <= max_row; ++row) {
        for (int col = min_col; col <= max_col; ++col) {
            TileCoord tc = {col, row, floor};
            Vec3 center = tile_to_world(tc, cfg);

            // Diamond corners
            Vec3 top    = {center.x, center.y - 0.5f, center.z};
            Vec3 right  = {center.x + 0.5f, center.y, center.z};
            Vec3 bottom = {center.x, center.y + 0.5f, center.z};
            Vec3 left   = {center.x - 0.5f, center.y, center.z};

            Vec2 st = cam.world_to_screen(top);
            Vec2 sr = cam.world_to_screen(right);
            Vec2 sb = cam.world_to_screen(bottom);
            Vec2 sl = cam.world_to_screen(left);

            DrawLine((int)st.x, (int)st.y, (int)sr.x, (int)sr.y, color);
            DrawLine((int)sr.x, (int)sr.y, (int)sb.x, (int)sb.y, color);
            DrawLine((int)sb.x, (int)sb.y, (int)sl.x, (int)sl.y, color);
            DrawLine((int)sl.x, (int)sl.y, (int)st.x, (int)st.y, color);
        }
    }
}

/// Draw collision body (debug)
inline void draw_collision_body(const IsoCamera& cam, const IsoConfig& cfg,
                                 const CollisionBody& body, Color color = RED)
{
    AABB3 aabb = body.to_aabb();

    // Draw ground rectangle (diamond in iso)
    Vec2 corners[4] = {
        cam.world_to_screen({aabb.min.x, aabb.min.y, body.position.z}),
        cam.world_to_screen({aabb.max.x, aabb.min.y, body.position.z}),
        cam.world_to_screen({aabb.max.x, aabb.max.y, body.position.z}),
        cam.world_to_screen({aabb.min.x, aabb.max.y, body.position.z}),
    };

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        DrawLine((int)corners[i].x, (int)corners[i].y,
                 (int)corners[j].x, (int)corners[j].y, color);
    }

    // Draw vertical lines (height)
    for (int i = 0; i < 4; ++i) {
        Vec3 top_corner = (i == 0) ? Vec3{aabb.min.x, aabb.min.y, aabb.max.z}
                        : (i == 1) ? Vec3{aabb.max.x, aabb.min.y, aabb.max.z}
                        : (i == 2) ? Vec3{aabb.max.x, aabb.max.y, aabb.max.z}
                        : Vec3{aabb.min.x, aabb.max.y, aabb.max.z};
        Vec2 top_s = cam.world_to_screen(top_corner);
        DrawLine((int)corners[i].x, (int)corners[i].y,
                 (int)top_s.x, (int)top_s.y, color);
    }

    // Draw center marker
    Vec2 center_s = cam.world_to_screen(body.position);
    DrawCircle((int)center_s.x, (int)center_s.y, 3.f, color);
}

/// Draw a tile highlight (e.g., mouse-hovered tile)
inline void draw_tile_highlight(const IsoCamera& cam, const IsoConfig& cfg,
                                 TileCoord tc, Color color = {255, 255, 0, 100})
{
    Vec3 center = tile_to_world(tc, cfg);
    Vec2 st = cam.world_to_screen({center.x, center.y - 0.5f, center.z});
    Vec2 sr = cam.world_to_screen({center.x + 0.5f, center.y, center.z});
    Vec2 sb = cam.world_to_screen({center.x, center.y + 0.5f, center.z});
    Vec2 sl = cam.world_to_screen({center.x - 0.5f, center.y, center.z});

    // Filled diamond
    DrawTriangle(to_rl(st), to_rl(sl), to_rl(sb), color);
    DrawTriangle(to_rl(st), to_rl(sb), to_rl(sr), color);

    // Outline
    Color outline = {color.r, color.g, color.b, 200};
    DrawLine((int)st.x, (int)st.y, (int)sr.x, (int)sr.y, outline);
    DrawLine((int)sr.x, (int)sr.y, (int)sb.x, (int)sb.y, outline);
    DrawLine((int)sb.x, (int)sb.y, (int)sl.x, (int)sl.y, outline);
    DrawLine((int)sl.x, (int)sl.y, (int)st.x, (int)st.y, outline);
}

/// Draw AABB wireframe (debug)
inline void draw_aabb(const IsoCamera& cam, const AABB3& box, Color color = GREEN) {
    // Bottom face
    Vec2 b0 = cam.world_to_screen({box.min.x, box.min.y, box.min.z});
    Vec2 b1 = cam.world_to_screen({box.max.x, box.min.y, box.min.z});
    Vec2 b2 = cam.world_to_screen({box.max.x, box.max.y, box.min.z});
    Vec2 b3 = cam.world_to_screen({box.min.x, box.max.y, box.min.z});

    DrawLine((int)b0.x,(int)b0.y, (int)b1.x,(int)b1.y, color);
    DrawLine((int)b1.x,(int)b1.y, (int)b2.x,(int)b2.y, color);
    DrawLine((int)b2.x,(int)b2.y, (int)b3.x,(int)b3.y, color);
    DrawLine((int)b3.x,(int)b3.y, (int)b0.x,(int)b0.y, color);

    // Top face
    Vec2 t0 = cam.world_to_screen({box.min.x, box.min.y, box.max.z});
    Vec2 t1 = cam.world_to_screen({box.max.x, box.min.y, box.max.z});
    Vec2 t2 = cam.world_to_screen({box.max.x, box.max.y, box.max.z});
    Vec2 t3 = cam.world_to_screen({box.min.x, box.max.y, box.max.z});

    DrawLine((int)t0.x,(int)t0.y, (int)t1.x,(int)t1.y, color);
    DrawLine((int)t1.x,(int)t1.y, (int)t2.x,(int)t2.y, color);
    DrawLine((int)t2.x,(int)t2.y, (int)t3.x,(int)t3.y, color);
    DrawLine((int)t3.x,(int)t3.y, (int)t0.x,(int)t0.y, color);

    // Verticals
    DrawLine((int)b0.x,(int)b0.y, (int)t0.x,(int)t0.y, color);
    DrawLine((int)b1.x,(int)b1.y, (int)t1.x,(int)t1.y, color);
    DrawLine((int)b2.x,(int)b2.y, (int)t2.x,(int)t2.y, color);
    DrawLine((int)b3.x,(int)b3.y, (int)t3.x,(int)t3.y, color);
}

// ─── Render-to-texture helper for 3D entities ─

/// Helper: render a 3D model to a RayLib RenderTexture, then draw it
/// as a sprite in the isometric world.
/// Call this inside your draw loop for each 3D character.
struct EntitySprite {
    RenderTexture2D render_target{};
    int width  = 128;
    int height = 256;
    bool initialized = false;

    void init(int w = 128, int h = 256) {
        width  = w;
        height = h;
        render_target = LoadRenderTexture(w, h);
        initialized = true;
    }

    void unload() {
        if (initialized) {
            UnloadRenderTexture(render_target);
            initialized = false;
        }
    }

    /// Begin rendering the 3D model into this sprite's texture
    void begin_render(Color clear_color = BLANK) {
        BeginTextureMode(render_target);
        ClearBackground(clear_color);
    }

    /// End rendering into texture
    void end_render() {
        EndTextureMode();
    }

    /// Draw this sprite at the given screen position
    /// (typically called from ZSorter's entity callback)
    void draw_at(Vec2 screen_pos, Vec2 origin = {}, float scale = 1.0f,
                 Color tint = WHITE) const
    {
        if (!initialized) return;

        // RenderTexture is flipped vertically in RayLib
        Rectangle src = {0, 0, (float)width, -(float)height};
        Rectangle dst = {
            screen_pos.x - origin.x * scale,
            screen_pos.y - origin.y * scale,
            width * scale,
            height * scale
        };
        DrawTexturePro(render_target.texture, src, dst, {0, 0}, 0.f, tint);
    }
};

} // namespace iso
