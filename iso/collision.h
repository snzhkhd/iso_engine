#pragma once
// iso_engine — collision detection with height support
// ────────────────────────────────────────────────────

#include "core.h"
#include "map.h"

#include <vector>
#include <optional>
#include <cmath>
#include <algorithm>
#include <limits>

namespace iso {

// ─── Collision body (entity's physical shape) ─

struct CollisionBody {
    Vec3  position{};        // center on ground plane, z = feet
    Vec2  half_extents{0.3f, 0.3f}; // half-size on ground plane
    float body_height = 1.8f;       // full height of body (for z-overlap)

    [[nodiscard]] AABB3 to_aabb() const {
        return {
            {position.x - half_extents.x, position.y - half_extents.y, position.z},
            {position.x + half_extents.x, position.y + half_extents.y, position.z + body_height}
        };
    }

    [[nodiscard]] float feet_z() const { return position.z; }
    [[nodiscard]] float head_z() const { return position.z + body_height; }

    /// Current floor level
    [[nodiscard]] int current_floor(const IsoConfig& cfg) const {
        return static_cast<int>(std::floor(position.z / cfg.floor_height));
    }
};

// ─── Hit result ───────────────────────────────

struct HitResult {
    bool    hit       = false;
    Vec3    point{};            // point of contact
    Vec3    normal{};           // surface normal
    float   distance  = 0.f;   // distance to hit along ray
    TileCoord tile{};           // tile that was hit
};

// ─── Movement result ──────────────────────────

struct MoveResult {
    Vec3  new_position{};       // resolved position after collision
    Vec3  velocity{};           // adjusted velocity (after sliding)
    bool  on_ground   = false;  // standing on a solid surface
    bool  hit_wall    = false;  // collided with a wall
    bool  hit_ceiling = false;  // bumped head
    int   floor_level = 0;     // current floor after movement
};

// ─── Collision system ─────────────────────────

class CollisionSystem {
public:
    explicit CollisionSystem(const IsoMap& map) : map_(&map) {}

    /// Check if a body overlaps any solid tile in the map
    [[nodiscard]] bool check_overlap(const CollisionBody& body) const {
        return check_overlap_at(body, body.position);
    }

    /// Check if a body WOULD overlap if placed at `pos`
    [[nodiscard]] bool check_overlap_at(const CollisionBody& body, Vec3 pos) const {
        const auto& cfg = map_->config();

        // Determine which tiles the body covers
        float x0 = pos.x - body.half_extents.x;
        float y0 = pos.y - body.half_extents.y;
        float x1 = pos.x + body.half_extents.x;
        float y1 = pos.y + body.half_extents.y;
        float z0 = pos.z;
        float z1 = pos.z + body.body_height;

        int col_min = static_cast<int>(std::floor(x0));
        int col_max = static_cast<int>(std::floor(x1));
        int row_min = static_cast<int>(std::floor(y0));
        int row_max = static_cast<int>(std::floor(y1));
        int flr_min = static_cast<int>(std::floor(z0 / cfg.floor_height));
        int flr_max = static_cast<int>(std::floor(z1 / cfg.floor_height));

        AABB3 body_aabb = {
            {x0, y0, z0},
            {x1, y1, z1}
        };

        for (int f = flr_min; f <= flr_max; ++f) {
            const Floor* flr = map_->floor_safe(f);
            if (!flr) continue;
            for (int r = row_min; r <= row_max; ++r) {
                for (int c = col_min; c <= col_max; ++c) {
                    if (!flr->in_bounds(c, r)) {
                        // Out of bounds = solid
                        return true;
                    }
                    // Check wall and object layers for solids
                    for (int layer = 1; layer < LAYER_COUNT; ++layer) {
                        const TileData& td = flr->tile_safe(static_cast<LayerType>(layer), c, r);
                        if (td.is_solid()) {
                            AABB3 tile_box = make_tile_aabb(c, r, f, td, cfg);
                            if (body_aabb.overlaps(tile_box))
                                return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    /// Move a body with collision resolution (slide along walls)
    /// Returns the resolved position and collision info
    [[nodiscard]] MoveResult move(const CollisionBody& body, Vec3 delta, int max_iterations = 4) const {
        const auto& cfg = map_->config();
        MoveResult result;
        Vec3 pos = body.position;
        Vec3 remaining = delta;

        result.on_ground = check_ground(body, pos, cfg);

        for (int iter = 0; iter < max_iterations; ++iter) {
            if (remaining.length_sq() < 1e-8f)
                break;

            // Try full movement
            Vec3 target = pos + remaining;
            CollisionBody test_body = body;
            test_body.position = target;

            if (!check_overlap_at(body, target)) {
                pos = target;
                break;
            }

            // Resolve axis by axis (x, y, z independently)
            // This gives natural wall-sliding behavior
            result.hit_wall = true;
            Vec3 step = remaining;

            // Try X axis
            Vec3 try_x = {pos.x + step.x, pos.y, pos.z};
            if (!check_overlap_at(body, try_x)) {
                pos.x = try_x.x;
            } else {
                remaining.x = 0.f;
            }

            // Try Y axis
            Vec3 try_y = {pos.x, pos.y + step.y, pos.z};
            if (!check_overlap_at(body, try_y)) {
                pos.y = try_y.y;
            } else {
                remaining.y = 0.f;
            }

            // Try Z axis (vertical: jumping / falling / stairs)
            Vec3 try_z = {pos.x, pos.y, pos.z + step.z};
            if (!check_overlap_at(body, try_z)) {
                pos.z = try_z.z;
            } else {
                if (step.z < 0.f) result.on_ground = true;
                if (step.z > 0.f) result.hit_ceiling = true;
                remaining.z = 0.f;
            }

            break; // one iteration of axis-separated resolve is usually enough
        }

        // Gravity / ground snap
        result.on_ground = check_ground(body, pos, cfg);
        if (result.on_ground && delta.z <= 0.f) {
            // Snap to floor surface
            float ground_z = find_ground_z(body, pos, cfg);
            if (ground_z >= 0.f) pos.z = ground_z;
        }

        result.new_position = pos;
        result.velocity     = remaining;
        result.floor_level  = static_cast<int>(std::floor(pos.z / cfg.floor_height));
        return result;
    }

    /// Step up logic: try to step up one sub-unit (for stairs/curbs)
    [[nodiscard]] std::optional<Vec3> try_step_up(const CollisionBody& body, Vec3 delta,
                                                   float step_height = 0.3f) const
    {
        // Move up, then forward, then down
        Vec3 up_pos = body.position;
        up_pos.z += step_height;

        if (check_overlap_at(body, up_pos))
            return std::nullopt; // can't move up

        Vec3 fwd_pos = up_pos + delta;
        if (check_overlap_at(body, fwd_pos))
            return std::nullopt; // blocked after stepping up

        // Find ground below
        const auto& cfg = map_->config();
        float gz = find_ground_z(body, fwd_pos, cfg);
        if (gz >= 0.f && gz >= body.position.z - 0.1f) {
            fwd_pos.z = gz;
            return fwd_pos;
        }
        return std::nullopt;
    }

    /// Raycast against solid tiles (for line of sight, shooting, etc.)
    [[nodiscard]] HitResult raycast(Vec3 origin, Vec3 direction, float max_distance = 50.f) const {
        const auto& cfg = map_->config();
        HitResult result;

        // Simple stepping raycast (DDA-like in 2D, step in Z)
        const float step_size = 0.1f;
        Vec3 dir_norm = direction;
        float dir_len = direction.length();
        if (dir_len < 1e-6f) return result;
        dir_norm = dir_norm * (1.0f / dir_len);

        float dist = 0.f;
        while (dist < max_distance) {
            Vec3 p = origin + dir_norm * dist;
            TileCoord tc = world_to_tile(p, cfg);

            if (map_->is_solid(tc)) {
                // Check if this tile blocks LOS
                const Floor* flr = map_->floor_safe(tc.floor);
                if (flr) {
                    for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                        const TileData& td = flr->tile_safe(static_cast<LayerType>(layer), tc.col, tc.row);
                        if (td.blocks_los() || td.is_solid()) {
                            result.hit = true;
                            result.point = p;
                            result.distance = dist;
                            result.tile = tc;
                            // Approximate normal
                            result.normal = estimate_normal(p, tc);
                            return result;
                        }
                    }
                }
            }
            dist += step_size;
        }
        return result;
    }

    /// Check if there's line of sight between two points
    [[nodiscard]] bool has_line_of_sight(Vec3 from, Vec3 to) const {
        Vec3 dir = to - from;
        float dist = dir.length();
        if (dist < 1e-6f) return true;
        auto hit = raycast(from, dir * (1.0f / dist), dist);
        return !hit.hit;
    }

    /// Find all solid tiles adjacent to a position (for debug/pathfinding)
    [[nodiscard]] std::vector<TileCoord> get_solid_neighbors(TileCoord tc, bool include_diagonals = false) const {
        std::vector<TileCoord> result;
        constexpr int dx4[] = { 0, 0, -1, 1};
        constexpr int dy4[] = {-1, 1,  0, 0};
        constexpr int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        constexpr int dy8[] = {-1,-1,-1,  0, 0,  1, 1, 1};

        int count = include_diagonals ? 8 : 4;
        const int* dx = include_diagonals ? dx8 : dx4;
        const int* dy = include_diagonals ? dy8 : dy4;

        for (int i = 0; i < count; ++i) {
            TileCoord n = {tc.col + dx[i], tc.row + dy[i], tc.floor};
            if (map_->is_solid(n))
                result.push_back(n);
        }
        return result;
    }

    /// Body-to-body collision check
    [[nodiscard]] static bool bodies_overlap(const CollisionBody& a, const CollisionBody& b) {
        return a.to_aabb().overlaps(b.to_aabb());
    }

    /// Swept body-to-body: returns time of first contact [0..1], or nullopt
    [[nodiscard]] static std::optional<float> swept_bodies(
        const CollisionBody& a, Vec3 a_vel,
        const CollisionBody& b, Vec3 b_vel)
    {
        // Minkowski difference approach
        Vec3 rel_vel = {a_vel.x - b_vel.x, a_vel.y - b_vel.y, a_vel.z - b_vel.z};
        AABB3 expanded = {
            {b.position.x - b.half_extents.x - a.half_extents.x,
             b.position.y - b.half_extents.y - a.half_extents.y,
             b.position.z},
            {b.position.x + b.half_extents.x + a.half_extents.x,
             b.position.y + b.half_extents.y + a.half_extents.y,
             b.position.z + b.body_height + a.body_height}
        };

        // Ray-AABB intersection
        float t_enter = 0.f;
        float t_exit  = 1.f;

        auto axis_test = [&](float pos, float vel, float bmin, float bmax) -> bool {
            if (std::abs(vel) < 1e-8f) {
                return pos >= bmin && pos <= bmax;
            }
            float t0 = (bmin - pos) / vel;
            float t1 = (bmax - pos) / vel;
            if (t0 > t1) std::swap(t0, t1);
            t_enter = std::max(t_enter, t0);
            t_exit  = std::min(t_exit,  t1);
            return t_enter <= t_exit;
        };

        if (!axis_test(a.position.x, rel_vel.x, expanded.min.x, expanded.max.x)) return std::nullopt;
        if (!axis_test(a.position.y, rel_vel.y, expanded.min.y, expanded.max.y)) return std::nullopt;
        if (!axis_test(a.position.z, rel_vel.z, expanded.min.z, expanded.max.z)) return std::nullopt;

        if (t_enter >= 0.f && t_enter <= 1.f)
            return t_enter;
        return std::nullopt;
    }

private:
    const IsoMap* map_;

    /// Build AABB for a solid tile
    [[nodiscard]] AABB3 make_tile_aabb(int col, int row, int floor, const TileData& td,
                                        const IsoConfig& cfg) const
    {
        float z_base = floor * cfg.floor_height;
        float tile_h = td.height * cfg.floor_height;
        if (has_flag(td.flags, TileFlags::HalfHeight))
            tile_h *= 0.5f;

        return {
            {static_cast<float>(col), static_cast<float>(row), z_base},
            {static_cast<float>(col + 1), static_cast<float>(row + 1), z_base + tile_h}
        };
    }

    /// Check if body is standing on ground at given position
    [[nodiscard]] bool check_ground(const CollisionBody& body, Vec3 pos, const IsoConfig& cfg) const {
        // Check slightly below feet
        Vec3 below = pos;
        below.z -= 0.05f;

        int col = static_cast<int>(std::floor(pos.x));
        int row = static_cast<int>(std::floor(pos.y));
        int flr = static_cast<int>(std::floor(pos.z / cfg.floor_height));

        // Check current floor's ground
        const Floor* f = map_->floor_safe(flr);
        if (f && f->in_bounds(col, row)) {
            const TileData& gnd = f->ground(col, row);
            if (!gnd.is_empty() && gnd.is_platform()) {
                float floor_z = flr * cfg.floor_height;
                if (std::abs(pos.z - floor_z) < 0.1f)
                    return true;
            }
        }

        // Check floor below (standing on top of wall from lower floor)
        const Floor* f_below = map_->floor_safe(flr - 1);
        if (f_below && f_below->in_bounds(col, row)) {
            for (int layer = 1; layer < LAYER_COUNT; ++layer) {
                const TileData& td = f_below->tile_safe(static_cast<LayerType>(layer), col, row);
                if (td.is_solid()) {
                    float top_z = (flr - 1) * cfg.floor_height + td.height * cfg.floor_height;
                    if (std::abs(pos.z - top_z) < 0.1f)
                        return true;
                }
            }
        }

        return pos.z <= 0.01f; // ground level
    }

    /// Find the Z height of the ground surface at given xz position
    [[nodiscard]] float find_ground_z(const CollisionBody& body, Vec3 pos, const IsoConfig& cfg) const {
        int col = static_cast<int>(std::floor(pos.x));
        int row = static_cast<int>(std::floor(pos.y));
        int flr = static_cast<int>(std::floor(pos.z / cfg.floor_height));

        // Check current floor
        const Floor* f = map_->floor_safe(flr);
        if (f && f->in_bounds(col, row)) {
            const TileData& gnd = f->ground(col, row);
            if (!gnd.is_empty() && gnd.is_platform()) {
                return flr * cfg.floor_height;
            }
        }

        // Search downward
        for (int search_f = flr - 1; search_f >= map_->min_floor(); --search_f) {
            const Floor* sf = map_->floor_safe(search_f);
            if (!sf || !sf->in_bounds(col, row)) continue;

            const TileData& gnd = sf->ground(col, row);
            if (!gnd.is_empty() && gnd.is_platform())
                return search_f * cfg.floor_height;

            // Top of solid object on lower floor
            for (int layer = 1; layer < LAYER_COUNT; ++layer) {
                const TileData& td = sf->tile_safe(static_cast<LayerType>(layer), col, row);
                if (td.is_solid())
                    return search_f * cfg.floor_height + td.height * cfg.floor_height;
            }
        }

        return 0.f; // absolute ground
    }

    /// Estimate collision normal from penetration point
    [[nodiscard]] Vec3 estimate_normal(Vec3 point, TileCoord tile) const {
        float cx = tile.col + 0.5f;
        float cy = tile.row + 0.5f;
        float dx = point.x - cx;
        float dy = point.y - cy;

        if (std::abs(dx) > std::abs(dy))
            return {dx > 0 ? 1.f : -1.f, 0.f, 0.f};
        else
            return {0.f, dy > 0 ? 1.f : -1.f, 0.f};
    }
};

} // namespace iso
