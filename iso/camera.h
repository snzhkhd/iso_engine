#pragma once
// iso_engine — isometric camera
// ─────────────────────────────

#include "core.h"
#include "zsort.h"  // for ViewFrustum

#include <algorithm>
#include <cmath>

namespace iso {

class IsoCamera {
public:
    IsoCamera() = default;

    explicit IsoCamera(const IsoConfig& cfg, int viewport_w = 1920, int viewport_h = 1080)
        : cfg_(&cfg)
        , viewport_w_(viewport_w)
        , viewport_h_(viewport_h)
    {}

    // ─── Configuration ────────────────────────

    void set_config(const IsoConfig& cfg) { cfg_ = &cfg; }
    void set_viewport(int w, int h) { viewport_w_ = w; viewport_h_ = h; }

    // ─── Position (world space, z = height level being viewed) ──

    void set_position(Vec3 pos)  { target_ = pos; }
    void set_target(Vec3 pos)    { target_ = pos; }   // alias
    void move(Vec2 delta)        { target_.x += delta.x; target_.y += delta.y; }
    void set_height(float z)     { target_.z = z; }

    [[nodiscard]] Vec3 position()  const { return target_; }
    [[nodiscard]] Vec3& position()       { return target_; }

    // ─── Zoom ─────────────────────────────────

    void set_zoom(float z)  { zoom_ = clamp_val(z, min_zoom_, max_zoom_); }
    void zoom_by(float dz)  { set_zoom(zoom_ + dz); }
    void set_zoom_limits(float mn, float mx) { min_zoom_ = mn; max_zoom_ = mx; }

    [[nodiscard]] float zoom() const { return zoom_; }

    // ─── Smooth follow ────────────────────────

    /// Call each frame to smoothly follow a target
    void follow(Vec3 target, float dt, float speed = 5.0f) {
        target_.x = lerp(target_.x, target.x, 1.0f - std::exp(-speed * dt));
        target_.y = lerp(target_.y, target.y, 1.0f - std::exp(-speed * dt));
        target_.z = lerp(target_.z, target.z, 1.0f - std::exp(-speed * dt));
    }

    void smooth_zoom(float target_zoom, float dt, float speed = 5.0f) {
        zoom_ = lerp(zoom_, clamp_val(target_zoom, min_zoom_, max_zoom_),
                      1.0f - std::exp(-speed * dt));
    }

    // ─── Coordinate transforms ────────────────

    /// World position → screen pixel (relative to viewport top-left)
    [[nodiscard]] Vec2 world_to_screen(Vec3 world, bool snap = true) const {
        Vec2 iso = iso::world_to_screen(world, *cfg_);
        Vec2 cam = iso::world_to_screen(target_, *cfg_);

        // Snap camera position in iso-space to prevent sub-pixel jitter
        // This ensures all tiles shift by the same whole-pixel amount
        if (cfg_->pixel_snap) {
            cam = pixel_snap(cam * zoom_) / zoom_;
        }

        Vec2 result = {
            (iso.x - cam.x) * zoom_ + viewport_w_ * 0.5f,
            (iso.y - cam.y) * zoom_ + viewport_h_ * 0.5f
        };

        if (snap && cfg_->pixel_snap)
            result = pixel_snap(result);

        return result;
    }

    /// Screen pixel → world position (at given height z)
    [[nodiscard]] Vec3 screen_to_world(Vec2 screen, float z = 0.f) const {
        Vec2 cam = iso::world_to_screen(target_, *cfg_);
        Vec2 iso_screen = {
            (screen.x - viewport_w_ * 0.5f) / zoom_ + cam.x,
            (screen.y - viewport_h_ * 0.5f) / zoom_ + cam.y
        };
        return iso::screen_to_world(iso_screen, *cfg_, z);
    }

    /// Screen pixel → tile coordinate (at given floor)
    [[nodiscard]] TileCoord screen_to_tile(Vec2 screen, int floor = 0) const {
        float z = floor * cfg_->floor_height;
        Vec3 world = screen_to_world(screen, z);
        return iso::world_to_tile(world, *cfg_);
    }

    // ─── View frustum ─────────────────────────

    /// Get the view frustum for culling (in ISO screen space, not viewport)
    [[nodiscard]] ViewFrustum get_frustum(int min_floor, int max_floor) const {
        Vec2 cam_screen = iso::world_to_screen(target_, *cfg_);
        float hw = (viewport_w_ * 0.5f) / zoom_;
        float hh = (viewport_h_ * 0.5f) / zoom_;

        // Extra padding for tall sprites
        float padding = cfg_->height_pixel_offset * 3.f;

        return ViewFrustum{
            .screen_min = {cam_screen.x - hw - padding, cam_screen.y - hh - padding},
            .screen_max = {cam_screen.x + hw + padding, cam_screen.y + hh + padding},
            .min_floor  = min_floor,
            .max_floor  = max_floor
        };
    }

    /// Check if a screen position is within the viewport
    [[nodiscard]] bool is_on_screen(Vec2 screen_pos, float margin = 64.f) const {
        return screen_pos.x >= -margin && screen_pos.x <= viewport_w_ + margin
            && screen_pos.y >= -margin && screen_pos.y <= viewport_h_ + margin;
    }

    /// Check if a world position is visible
    [[nodiscard]] bool is_visible(Vec3 world_pos, float margin = 128.f) const {
        Vec2 sp = world_to_screen(world_pos);
        return is_on_screen(sp, margin);
    }

    // ─── Viewport info ────────────────────────

    [[nodiscard]] int viewport_width()  const { return viewport_w_; }
    [[nodiscard]] int viewport_height() const { return viewport_h_; }

    /// Get tile draw dimensions (with zoom and padding applied)
    /// Use these when rendering tiles to prevent seams
    [[nodiscard]] float tile_draw_width()  const {
        return cfg_->tile_width  * zoom_ + cfg_->tile_padding * 2.f;
    }
    [[nodiscard]] float tile_draw_height() const {
        return cfg_->tile_height * zoom_ + cfg_->tile_padding * 2.f;
    }
    /// Half-sizes for convenience (used to position diamond corners)
    [[nodiscard]] float tile_draw_hw() const { return tile_draw_width()  * 0.5f; }
    [[nodiscard]] float tile_draw_hh() const { return tile_draw_height() * 0.5f; }

    /// Get the tile at the center of the screen
    [[nodiscard]] TileCoord center_tile(int floor = 0) const {
        return screen_to_tile({viewport_w_ * 0.5f, viewport_h_ * 0.5f}, floor);
    }

    // ─── Screen shake ─────────────────────────

    void add_shake(float intensity, float duration) {
        shake_intensity_ = intensity;
        shake_timer_     = duration;
    }

    void update_shake(float dt) {
        if (shake_timer_ > 0.f) {
            shake_timer_ -= dt;
            if (shake_timer_ <= 0.f) {
                shake_intensity_ = 0.f;
                shake_offset_ = {};
            } else {
                // Simple random shake (you'd replace with your RNG)
                float t = shake_timer_ * 47.f;
                shake_offset_.x = std::sin(t * 13.7f) * shake_intensity_;
                shake_offset_.y = std::cos(t * 9.3f) * shake_intensity_;
            }
        }
    }

    [[nodiscard]] Vec2 shake_offset() const { return shake_offset_; }

private:
    const IsoConfig* cfg_ = nullptr;
    Vec3  target_     = {};
    float zoom_       = 1.0f;
    float min_zoom_   = 0.25f;
    float max_zoom_   = 4.0f;
    int   viewport_w_ = 1920;
    int   viewport_h_ = 1080;

    // Shake
    float shake_intensity_ = 0.f;
    float shake_timer_     = 0.f;
    Vec2  shake_offset_    = {};
};

} // namespace iso
