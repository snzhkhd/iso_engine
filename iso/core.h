#pragma once
// iso_engine — isometric engine core types & math
// C++20 / MSVC / RayLib-compatible
// ──────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <compare>
#include <concepts>
#include <functional>

namespace iso {

// ─── Basic vector types ───────────────────────

struct Vec2 {
    float x = 0.f, y = 0.f;

    Vec2 operator+(Vec2 o)  const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o)  const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }

    [[nodiscard]] float length_sq() const { return x * x + y * y; }
    [[nodiscard]] float length()    const { return std::sqrt(length_sq()); }

    [[nodiscard]] Vec2 normalized() const {
        float len = length();
        return (len > 1e-6f) ? Vec2{x / len, y / len} : Vec2{0.f, 0.f};
    }

    [[nodiscard]] float dot(Vec2 o) const { return x * o.x + y * o.y; }
};

struct Vec2i {
    int x = 0, y = 0;
    auto operator<=>(const Vec2i&) const = default;
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f; // x,y = ground plane; z = height

    Vec3 operator+(Vec3 o)  const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o)  const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }

    [[nodiscard]] Vec2 xz()  const { return {x, y}; }          // ground plane
    [[nodiscard]] float length_sq() const { return x*x + y*y + z*z; }
    [[nodiscard]] float length()    const { return std::sqrt(length_sq()); }
};

// ─── Tile coordinate (discrete position in the map grid) ──

struct TileCoord {
    int col   = 0;    // x-axis on grid
    int row   = 0;    // y-axis on grid
    int floor = 0;    // height level (negative = basement)

    auto operator<=>(const TileCoord&) const = default;
};

// ─── Engine configuration ─────────────────────

struct IsoConfig {
    // Tile diamond dimensions in pixels
    int tile_width  = 128;   // full width of the diamond
    int tile_height = 64;    // full height of the diamond (typically tile_width / 2)

    // Height
    float floor_height        = 1.0f;   // world units per floor
    float height_pixel_offset = 30.0f;  // screen pixels per floor level (vertical shift)

    // Rendering
    float tile_padding  = 2.0f;  // extra pixels added to tile size to prevent seams at fractional zoom
    bool  pixel_snap    = true;  // snap tile screen positions to integer pixels

    // Map limits (for default map allocation; 0 = dynamic)
    int default_map_cols = 64;
    int default_map_rows = 64;
    int min_floor        = -1;  // basement
    int max_floor        =  7;  // top floor

    [[nodiscard]] int   floor_count()  const { return max_floor - min_floor + 1; }
    [[nodiscard]] int   floor_index(int floor) const { return floor - min_floor; }
    [[nodiscard]] float half_tw()      const { return tile_width  * 0.5f; }
    [[nodiscard]] float half_th()      const { return tile_height * 0.5f; }
};

// ─── Coordinate conversions ───────────────────

/// World position (float, with height) → screen pixel position
inline Vec2 world_to_screen(Vec3 world, const IsoConfig& cfg) {
    const float sx = (world.x - world.y) * cfg.half_tw();
    float sy = (world.x + world.y) * cfg.half_th();
    sy -= world.z * cfg.height_pixel_offset;
    return {sx, sy};
}

/// Screen pixel → world position (at a given z height, default 0)
inline Vec3 screen_to_world(Vec2 screen, const IsoConfig& cfg, float z = 0.f) {
    const float sy_adj = screen.y + z * cfg.height_pixel_offset;
    const float htw = cfg.half_tw();
    const float hth = cfg.half_th();
    const float wx = (screen.x / htw + sy_adj / hth) * 0.5f;
    const float wy = (sy_adj / hth - screen.x / htw) * 0.5f;
    return {wx, wy, z};
}

/// World → tile coordinate
inline TileCoord world_to_tile(Vec3 world, const IsoConfig& cfg) {
    return {
        static_cast<int>(std::floor(world.x)),
        static_cast<int>(std::floor(world.y)),
        static_cast<int>(std::floor(world.z / cfg.floor_height))
    };
}

/// Tile → world center
inline Vec3 tile_to_world(TileCoord t, const IsoConfig& cfg) {
    return {
        t.col + 0.5f,
        t.row + 0.5f,
        t.floor * cfg.floor_height
    };
}

/// Tile → screen center
inline Vec2 tile_to_screen(TileCoord t, const IsoConfig& cfg) {
    return world_to_screen(tile_to_world(t, cfg), cfg);
}

// ─── Isometric depth (for sorting) ───────────

/// Depth value: higher = closer to camera = drawn later
inline float iso_depth(Vec3 world) {
    return world.x + world.y;
}

inline float iso_depth(TileCoord t) {
    return static_cast<float>(t.col + t.row);
}

// ─── Axis-Aligned Bounding Box (3D, world space) ─

struct AABB3 {
    Vec3 min{};
    Vec3 max{};

    [[nodiscard]] bool overlaps(const AABB3& o) const {
        return min.x < o.max.x && max.x > o.min.x
            && min.y < o.max.y && max.y > o.min.y
            && min.z < o.max.z && max.z > o.min.z;
    }

    [[nodiscard]] bool overlaps_xz(const AABB3& o) const {
        return min.x < o.max.x && max.x > o.min.x
            && min.y < o.max.y && max.y > o.min.y;
    }

    [[nodiscard]] bool contains(Vec3 p) const {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }

    [[nodiscard]] Vec3 center() const {
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }

    [[nodiscard]] Vec3 size() const { return max - min; }

    /// Build AABB from center, half-extents
    static AABB3 from_center(Vec3 center, Vec3 half) {
        return {center - half, center + half};
    }

    /// Build tile AABB (1x1 on ground, with floor height)
    static AABB3 from_tile(TileCoord t, const IsoConfig& cfg, float tile_height_units = 1.0f) {
        float zbase = t.floor * cfg.floor_height;
        return {
            {(float)t.col, (float)t.row, zbase},
            {(float)t.col + 1.0f, (float)t.row + 1.0f, zbase + tile_height_units}
        };
    }
};

// ─── Utility ──────────────────────────────────

/// Clamp value to [lo, hi]
template<typename T>
constexpr T clamp_val(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/// Linear interpolation
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/// Snap Vec2 to integer pixel coordinates (prevents tile seams at fractional zoom)
inline Vec2 pixel_snap(Vec2 v) {
    return {std::round(v.x), std::round(v.y)};
}

} // namespace iso
