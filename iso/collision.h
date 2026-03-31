#pragma once
// iso_engine — collision detection with height support
// Works with both IsoMap and ChunkMap
// ────────────────────────────────────────────────────

#include "core.h"
#include "map.h"
#include "tile_collision.h"

#include <vector>
#include <optional>
#include <functional>
#include <cmath>
#include <algorithm>

namespace iso {

    // ─── Collision body (entity's physical shape) ─

    struct CollisionBody {
        Vec3  position{};
        Vec2  half_extents{ 0.3f, 0.3f };
        float body_height = 1.8f;

        [[nodiscard]] AABB3 to_aabb() const {
            return {
                {position.x - half_extents.x, position.y - half_extents.y, position.z},
                {position.x + half_extents.x, position.y + half_extents.y, position.z + body_height}
            };
        }
        [[nodiscard]] float feet_z() const { return position.z; }
        [[nodiscard]] float head_z() const { return position.z + body_height; }
        [[nodiscard]] int current_floor(const IsoConfig& cfg) const {
            return static_cast<int>(std::floor(position.z / cfg.floor_height));
        }
    };

    // ─── Hit result ───────────────────────────────

    struct HitResult {
        bool      hit = false;
        Vec3      point{};
        Vec3      normal{};
        float     distance = 0.f;
        TileCoord tile{};
    };

    // ─── Movement result ──────────────────────────

    struct MoveResult {
        Vec3  new_position{};
        Vec3  velocity{};
        bool  on_ground = false;
        bool  hit_wall = false;
        bool  hit_ceiling = false;
        int   floor_level = 0;
    };

    // ─── Tile query interface ─────────────────────

    using TileQueryFn = std::function<TileData(LayerType layer, TileCoord tc)>;
    using SolidQueryFn = std::function<bool(TileCoord tc)>;

    // ─── Collision system ─────────────────────────

    class CollisionSystem {
    public:
        /// Construct from IsoMap
        explicit CollisionSystem(const IsoMap& map, const CollisionResolver& resolver = {})
            : cfg_(map.config()), resolver_(resolver)
        {
            tile_fn_ = [&map](LayerType layer, TileCoord tc) { return map.tile(layer, tc); };
            solid_fn_ = [&map](TileCoord tc) { return map.is_solid(tc); };
        }

        /// Construct from custom query functions
        CollisionSystem(IsoConfig cfg, TileQueryFn tile_fn, SolidQueryFn solid_fn,
            const CollisionResolver& resolver = {})
            : cfg_(cfg), tile_fn_(std::move(tile_fn)), solid_fn_(std::move(solid_fn)),
            resolver_(resolver)
        {}

        /// Set/change collision definitions at runtime
        void set_collision_defs(const TileCollisionDefs* defs) {
            resolver_.set_defs(defs);
        }
        void set_instance_overrides(const InstanceCollisionOverrides* ov) {
            resolver_.set_overrides(ov);
        }
        void set_resolver(const CollisionResolver& r) { resolver_ = r; }

        // ─── Overlap checks ──────────────────────

        [[nodiscard]] bool check_overlap(const CollisionBody& body) const {
            return check_overlap_at(body, body.position);
        }

        [[nodiscard]] bool check_overlap_at(const CollisionBody& body, Vec3 pos) const {
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
            int flr_min = static_cast<int>(std::floor(z0 / cfg_.floor_height));
            int flr_max = static_cast<int>(std::floor(z1 / cfg_.floor_height));

            AABB3 body_aabb = { {x0, y0, z0}, {x1, y1, z1} };

            for (int f = flr_min; f <= flr_max; ++f) {
                for (int r = row_min; r <= row_max; ++r) {
                    for (int c = col_min; c <= col_max; ++c) {
                        for (int layer = 1; layer < LAYER_COUNT; ++layer) {
                            TileData td = tile_fn_(static_cast<LayerType>(layer), { c, r, f });
                            if (td.is_solid()) {
                                auto shape = resolver_.resolve(td.tile_id, c, r, f);
                                if (shape.overlaps(body_aabb, c, r, f, cfg_.floor_height))
                                    return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        // ─── Movement with wall-sliding ──────────

        [[nodiscard]] MoveResult move(const CollisionBody& body, Vec3 delta,
            int max_iterations = 4) const
        {
            MoveResult result;
            Vec3 pos = body.position;
            Vec3 remaining = delta;
            result.on_ground = check_ground(body, pos);

            for (int iter = 0; iter < max_iterations; ++iter) {
                if (remaining.length_sq() < 1e-8f) break;

                Vec3 target = pos + remaining;
                if (!check_overlap_at(body, target)) { pos = target; break; }

                result.hit_wall = true;
                Vec3 step = remaining;

                Vec3 try_x = { pos.x + step.x, pos.y, pos.z };
                if (!check_overlap_at(body, try_x)) pos.x = try_x.x;
                else remaining.x = 0.f;

                Vec3 try_y = { pos.x, pos.y + step.y, pos.z };
                if (!check_overlap_at(body, try_y)) pos.y = try_y.y;
                else remaining.y = 0.f;

                Vec3 try_z = { pos.x, pos.y, pos.z + step.z };
                if (!check_overlap_at(body, try_z)) pos.z = try_z.z;
                else {
                    if (step.z < 0.f) result.on_ground = true;
                    if (step.z > 0.f) result.hit_ceiling = true;
                    remaining.z = 0.f;
                }
                break;
            }

            result.on_ground = check_ground(body, pos);
            if (result.on_ground && delta.z <= 0.f) {
                float gz = find_ground_z(body, pos);
                if (gz >= 0.f) pos.z = gz;
            }

            result.new_position = pos;
            result.velocity = remaining;
            result.floor_level = static_cast<int>(std::floor(pos.z / cfg_.floor_height));
            return result;
        }

        // ─── Step up ─────────────────────────────

        [[nodiscard]] std::optional<Vec3> try_step_up(const CollisionBody& body, Vec3 delta,
            float step_height = 0.3f) const
        {
            Vec3 up_pos = body.position;
            up_pos.z += step_height;
            if (check_overlap_at(body, up_pos)) return std::nullopt;

            Vec3 fwd_pos = up_pos + delta;
            if (check_overlap_at(body, fwd_pos)) return std::nullopt;

            float gz = find_ground_z(body, fwd_pos);
            if (gz >= 0.f && gz >= body.position.z - 0.1f) {
                fwd_pos.z = gz;
                return fwd_pos;
            }
            return std::nullopt;
        }

        // ─── Raycast ─────────────────────────────

        [[nodiscard]] HitResult raycast(Vec3 origin, Vec3 direction,
            float max_distance = 50.f) const
        {
            HitResult result;
            float dir_len = direction.length();
            if (dir_len < 1e-6f) return result;
            Vec3 dir_norm = direction * (1.0f / dir_len);

            const float step_size = 0.1f;
            float dist = 0.f;
            while (dist < max_distance) {
                Vec3 p = origin + dir_norm * dist;
                TileCoord tc = world_to_tile(p, cfg_);

                if (solid_fn_(tc)) {
                    for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                        TileData td = tile_fn_(static_cast<LayerType>(layer), tc);
                        if (td.blocks_los() || td.is_solid()) {
                            result.hit = true;
                            result.point = p;
                            result.distance = dist;
                            result.tile = tc;
                            result.normal = estimate_normal(p, tc);
                            return result;
                        }
                    }
                }
                dist += step_size;
            }
            return result;
        }

        // ─── Line of sight ───────────────────────

        [[nodiscard]] bool has_line_of_sight(Vec3 from, Vec3 to) const {
            Vec3 dir = to - from;
            float dist = dir.length();
            if (dist < 1e-6f) return true;
            return !raycast(from, dir * (1.0f / dist), dist).hit;
        }

        // ─── Neighbors ───────────────────────────

        [[nodiscard]] std::vector<TileCoord> get_solid_neighbors(TileCoord tc,
            bool diag = false) const
        {
            std::vector<TileCoord> result;
            constexpr int dx4[] = { 0, 0, -1, 1 }, dy4[] = { -1, 1, 0, 0 };
            constexpr int dx8[] = { -1,0,1,-1,1,-1,0,1 }, dy8[] = { -1,-1,-1,0,0,1,1,1 };
            int n = diag ? 8 : 4;
            const int* dx = diag ? dx8 : dx4, * dy = diag ? dy8 : dy4;
            for (int i = 0; i < n; ++i) {
                TileCoord nb = { tc.col + dx[i], tc.row + dy[i], tc.floor };
                if (solid_fn_(nb)) result.push_back(nb);
            }
            return result;
        }

        // ─── Static body-to-body ─────────────────

        [[nodiscard]] static bool bodies_overlap(const CollisionBody& a,
            const CollisionBody& b) {
            return a.to_aabb().overlaps(b.to_aabb());
        }

        [[nodiscard]] static std::optional<float> swept_bodies(
            const CollisionBody& a, Vec3 a_vel,
            const CollisionBody& b, Vec3 b_vel)
        {
            Vec3 rv = { a_vel.x - b_vel.x, a_vel.y - b_vel.y, a_vel.z - b_vel.z };
            AABB3 exp = {
                {b.position.x - b.half_extents.x - a.half_extents.x,
                 b.position.y - b.half_extents.y - a.half_extents.y, b.position.z},
                {b.position.x + b.half_extents.x + a.half_extents.x,
                 b.position.y + b.half_extents.y + a.half_extents.y,
                 b.position.z + b.body_height + a.body_height}
            };
            float te = 0.f, tx = 1.f;
            auto test = [&](float p, float v, float lo, float hi) {
                if (std::abs(v) < 1e-8f) return p >= lo && p <= hi;
                float t0 = (lo - p) / v, t1 = (hi - p) / v;
                if (t0 > t1) std::swap(t0, t1);
                te = std::max(te, t0); tx = std::min(tx, t1);
                return te <= tx;
            };
            if (!test(a.position.x, rv.x, exp.min.x, exp.max.x)) return std::nullopt;
            if (!test(a.position.y, rv.y, exp.min.y, exp.max.y)) return std::nullopt;
            if (!test(a.position.z, rv.z, exp.min.z, exp.max.z)) return std::nullopt;
            if (te >= 0.f && te <= 1.f) return te;
            return std::nullopt;
        }

        [[nodiscard]] const IsoConfig& config() const { return cfg_; }

    private:
        IsoConfig           cfg_;
        TileQueryFn         tile_fn_;
        SolidQueryFn        solid_fn_;
        CollisionResolver   resolver_;

        [[nodiscard]] AABB3 make_tile_aabb(int c, int r, int f, const TileData& td) const {
            float zb = f * cfg_.floor_height;
            float th = td.height * cfg_.floor_height;
            if (has_flag(td.flags, TileFlags::HalfHeight)) th *= 0.5f;
            return { {(float)c,(float)r,zb},{(float)(c + 1),(float)(r + 1),zb + th} };
        }

        [[nodiscard]] bool check_ground(const CollisionBody& body, Vec3 pos) const {
            int c = (int)std::floor(pos.x), r = (int)std::floor(pos.y);
            int f = (int)std::floor(pos.z / cfg_.floor_height);

            TileData gnd = tile_fn_(LayerType::Ground, { c, r, f });
            if (!gnd.is_empty() && gnd.is_platform())
                if (std::abs(pos.z - f * cfg_.floor_height) < 0.1f) return true;

            for (int l = 1; l < LAYER_COUNT; ++l) {
                TileData td = tile_fn_(static_cast<LayerType>(l), { c, r, f - 1 });
                if (td.is_solid()) {
                    float top = (f - 1) * cfg_.floor_height + td.height * cfg_.floor_height;
                    if (std::abs(pos.z - top) < 0.1f) return true;
                }
            }
            return pos.z <= 0.01f;
        }

        [[nodiscard]] float find_ground_z(const CollisionBody& body, Vec3 pos) const {
            int c = (int)std::floor(pos.x), r = (int)std::floor(pos.y);
            int f = (int)std::floor(pos.z / cfg_.floor_height);

            TileData gnd = tile_fn_(LayerType::Ground, { c, r, f });
            if (!gnd.is_empty() && gnd.is_platform()) return f * cfg_.floor_height;

            for (int sf = f - 1; sf >= cfg_.min_floor; --sf) {
                TileData g = tile_fn_(LayerType::Ground, { c, r, sf });
                if (!g.is_empty() && g.is_platform()) return sf * cfg_.floor_height;
                for (int l = 1; l < LAYER_COUNT; ++l) {
                    TileData td = tile_fn_(static_cast<LayerType>(l), { c, r, sf });
                    if (td.is_solid()) return sf * cfg_.floor_height + td.height * cfg_.floor_height;
                }
            }
            return 0.f;
        }

        [[nodiscard]] static Vec3 estimate_normal(Vec3 p, TileCoord t) {
            float dx = p.x - (t.col + 0.5f), dy = p.y - (t.row + 0.5f);
            if (std::abs(dx) > std::abs(dy)) return { dx > 0 ? 1.f : -1.f, 0.f, 0.f };
            return { 0.f, dy > 0 ? 1.f : -1.f, 0.f };
        }
    };

} // namespace iso