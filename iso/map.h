#pragma once
// iso_engine — isometric map with multi-floor support
// ───────────────────────────────────────────────────

#include "core.h"

#include <vector>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <cassert>
#include <cstring>

namespace iso {

// ─── Tile flags (bitmask) ─────────────────────

enum class TileFlags : uint32_t {
    None          = 0,
    Solid         = 1 << 0,   // blocks movement (wall, rock)
    BlocksLOS     = 1 << 1,   // blocks line of sight
    Climbable     = 1 << 2,   // stairs / ladder
    Platform      = 1 << 3,   // can stand on top (floor/bridge)
    HalfHeight    = 1 << 4,   // low wall / fence (blocks movement, partial LOS)
    Water         = 1 << 5,   // water tile
    Destructible  = 1 << 6,   // can be destroyed
    Door          = 1 << 7,   // door (can toggle Solid)
    Window        = 1 << 8,   // window
    Slope         = 1 << 9,   // ramp connecting floors
    UserFlag0     = 1 << 24,  // game-specific flags
    UserFlag1     = 1 << 25,
    UserFlag2     = 1 << 26,
    UserFlag3     = 1 << 27,
};

constexpr TileFlags operator|(TileFlags a, TileFlags b) {
    return static_cast<TileFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr TileFlags operator&(TileFlags a, TileFlags b) {
    return static_cast<TileFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr TileFlags operator~(TileFlags a) {
    return static_cast<TileFlags>(~static_cast<uint32_t>(a));
}
constexpr bool has_flag(TileFlags val, TileFlags flag) {
    return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

// ─── Tile data ────────────────────────────────

struct TileData {
    uint16_t  tile_id    = 0;        // tileset sprite index (0 = empty/air)
    TileFlags flags      = TileFlags::None;
    uint8_t   height     = 1;        // tile height in sub-units (1 = standard, 2 = tall wall, etc.)
    uint8_t   variant    = 0;        // visual variant / rotation
    uint16_t  user_data  = 0;        // game-specific data

    // Sub-tile offset (0 = centered, 128 = +0.5 tile)
    uint8_t   sub_x = 0;   // 0..255 → 0.0..1.0 offset within tile
    uint8_t   sub_y = 0;   // 0..255 → 0.0..1.0 offset within tile

    [[nodiscard]] bool is_empty()   const { return tile_id == 0; }
    [[nodiscard]] bool is_solid()   const { return has_flag(flags, TileFlags::Solid); }
    [[nodiscard]] bool blocks_los() const { return has_flag(flags, TileFlags::BlocksLOS); }
    [[nodiscard]] bool is_platform()const { return has_flag(flags, TileFlags::Platform); }
};

// ─── Tile layer types ─────────────────────────

enum class LayerType : uint8_t {
    Ground   = 0,   // floor / terrain
    Wall     = 1,   // walls
    Object   = 2,   // furniture, props
    Overlay  = 3,   // roof / ceiling (rendered above entities)
    Count    = 4
};

static constexpr int LAYER_COUNT = static_cast<int>(LayerType::Count);

// ─── Floor (one height level of the map) ──────

class Floor {
public:
    Floor() = default;
    Floor(int cols, int rows, int floor_level)
        : cols_(cols), rows_(rows), floor_(floor_level)
    {
        for (auto& layer : layers_)
            layer.resize(static_cast<size_t>(cols) * rows);
    }

    // Layer access
    [[nodiscard]] TileData& tile(LayerType layer, int col, int row) {
        assert(in_bounds(col, row));
        return layers_[static_cast<int>(layer)][idx(col, row)];
    }
    [[nodiscard]] const TileData& tile(LayerType layer, int col, int row) const {
        assert(in_bounds(col, row));
        return layers_[static_cast<int>(layer)][idx(col, row)];
    }

    // Safe access (returns empty tile if out of bounds)
    [[nodiscard]] TileData tile_safe(LayerType layer, int col, int row) const {
        if (!in_bounds(col, row)) return {};
        return layers_[static_cast<int>(layer)][idx(col, row)];
    }

    // Ground-layer shortcut
    [[nodiscard]] TileData& ground(int col, int row)             { return tile(LayerType::Ground, col, row); }
    [[nodiscard]] const TileData& ground(int col, int row) const { return tile(LayerType::Ground, col, row); }

    // Check if any solid tile at this position (across all layers)
    [[nodiscard]] bool is_solid(int col, int row) const {
        if (!in_bounds(col, row)) return true; // out of bounds = solid
        for (int i = 0; i < LAYER_COUNT; ++i) {
            if (layers_[i][idx(col, row)].is_solid())
                return true;
        }
        return false;
    }

    // Check if position has a walkable floor
    [[nodiscard]] bool is_walkable(int col, int row) const {
        if (!in_bounds(col, row)) return false;
        const auto& g = ground(col, row);
        return !g.is_empty() && g.is_platform() && !is_solid(col, row);
    }

    // Raw layer data access
    [[nodiscard]] std::span<TileData> layer_data(LayerType layer) {
        return layers_[static_cast<int>(layer)];
    }
    [[nodiscard]] std::span<const TileData> layer_data(LayerType layer) const {
        return layers_[static_cast<int>(layer)];
    }

    [[nodiscard]] int cols()  const { return cols_; }
    [[nodiscard]] int rows()  const { return rows_; }
    [[nodiscard]] int floor_level() const { return floor_; }

    [[nodiscard]] bool in_bounds(int col, int row) const {
        return col >= 0 && col < cols_ && row >= 0 && row < rows_;
    }

private:
    [[nodiscard]] size_t idx(int col, int row) const {
        return static_cast<size_t>(row) * cols_ + col;
    }

    int cols_  = 0;
    int rows_  = 0;
    int floor_ = 0;
    std::array<std::vector<TileData>, LAYER_COUNT> layers_;
};

// ─── IsoMap (all floors) ──────────────────────

class IsoMap {
public:
    IsoMap() = default;

    /// Initialize map with config
    explicit IsoMap(const IsoConfig& cfg)
        : config_(cfg)
    {
        floors_.reserve(cfg.floor_count());
        for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
            floors_.emplace_back(cfg.default_map_cols, cfg.default_map_rows, f);
        }
    }

    /// Initialize with explicit dimensions
    IsoMap(int cols, int rows, int min_floor, int max_floor, const IsoConfig& cfg)
        : config_(cfg)
    {
        config_.default_map_cols = cols;
        config_.default_map_rows = rows;
        config_.min_floor = min_floor;
        config_.max_floor = max_floor;
        floors_.reserve(cfg.floor_count());
        for (int f = min_floor; f <= max_floor; ++f) {
            floors_.emplace_back(cols, rows, f);
        }
    }

    // Floor access
    [[nodiscard]] Floor& floor(int level) {
        int idx = config_.floor_index(level);
        assert(idx >= 0 && idx < static_cast<int>(floors_.size()));
        return floors_[idx];
    }
    [[nodiscard]] const Floor& floor(int level) const {
        int idx = config_.floor_index(level);
        assert(idx >= 0 && idx < static_cast<int>(floors_.size()));
        return floors_[idx];
    }

    [[nodiscard]] Floor* floor_safe(int level) {
        int idx = config_.floor_index(level);
        if (idx < 0 || idx >= static_cast<int>(floors_.size())) return nullptr;
        return &floors_[idx];
    }
    [[nodiscard]] const Floor* floor_safe(int level) const {
        int idx = config_.floor_index(level);
        if (idx < 0 || idx >= static_cast<int>(floors_.size())) return nullptr;
        return &floors_[idx];
    }

    // Direct tile access via TileCoord
    [[nodiscard]] TileData tile(LayerType layer, TileCoord tc) const {
        const Floor* f = floor_safe(tc.floor);
        if (!f) return {};
        return f->tile_safe(layer, tc.col, tc.row);
    }

    void set_tile(LayerType layer, TileCoord tc, TileData data) {
        Floor* f = floor_safe(tc.floor);
        if (f && f->in_bounds(tc.col, tc.row))
            f->tile(layer, tc.col, tc.row) = data;
    }

    // Check if position is solid (any layer at given floor)
    [[nodiscard]] bool is_solid(TileCoord tc) const {
        const Floor* f = floor_safe(tc.floor);
        return !f || f->is_solid(tc.col, tc.row);
    }

    // Check if entity can stand at this world position
    [[nodiscard]] bool is_walkable(Vec3 world) const {
        TileCoord tc = world_to_tile(world, config_);
        const Floor* f = floor_safe(tc.floor);
        if (!f) return false;
        // Must have ground AND not be blocked by walls/objects
        bool has_ground = !f->ground(tc.col, tc.row).is_empty();
        bool blocked = false;
        for (int i = 1; i < LAYER_COUNT; ++i) { // skip ground
            if (f->tile_safe(static_cast<LayerType>(i), tc.col, tc.row).is_solid()) {
                blocked = true;
                break;
            }
        }
        return has_ground && !blocked;
    }

    // Dimensions
    [[nodiscard]] int cols()       const { return config_.default_map_cols; }
    [[nodiscard]] int rows()       const { return config_.default_map_rows; }
    [[nodiscard]] int min_floor()  const { return config_.min_floor; }
    [[nodiscard]] int max_floor()  const { return config_.max_floor; }
    [[nodiscard]] int floor_count()const { return static_cast<int>(floors_.size()); }
    [[nodiscard]] bool has_floor(int level) const {
        return level >= config_.min_floor && level <= config_.max_floor;
    }

    [[nodiscard]] const IsoConfig& config() const { return config_; }
    [[nodiscard]] IsoConfig& config() { return config_; }

    [[nodiscard]] std::span<Floor>       floors()       { return floors_; }
    [[nodiscard]] std::span<const Floor> floors() const { return floors_; }

private:
    IsoConfig config_;
    std::vector<Floor> floors_;
};

// ─── Helper: fill a rectangular region ────────

inline void fill_rect(Floor& floor, LayerType layer,
                      int col0, int row0, int col1, int row1,
                      TileData data)
{
    for (int r = row0; r <= row1; ++r)
        for (int c = col0; c <= col1; ++c)
            if (floor.in_bounds(c, r))
                floor.tile(layer, c, r) = data;
}

/// Fill ground with a platform tile
inline void fill_ground(Floor& floor, int col0, int row0, int col1, int row1,
                         uint16_t tile_id)
{
    TileData td;
    td.tile_id = tile_id;
    td.flags   = TileFlags::Platform;
    fill_rect(floor, LayerType::Ground, col0, row0, col1, row1, td);
}

/// Place walls along a rectangle border
inline void place_walls(Floor& floor, int col0, int row0, int col1, int row1,
                        uint16_t tile_id)
{
    TileData wall;
    wall.tile_id = tile_id;
    wall.flags   = TileFlags::Solid | TileFlags::BlocksLOS;

    for (int c = col0; c <= col1; ++c) {
        if (floor.in_bounds(c, row0)) floor.tile(LayerType::Wall, c, row0) = wall;
        if (floor.in_bounds(c, row1)) floor.tile(LayerType::Wall, c, row1) = wall;
    }
    for (int r = row0; r <= row1; ++r) {
        if (floor.in_bounds(col0, r)) floor.tile(LayerType::Wall, col0, r) = wall;
        if (floor.in_bounds(col1, r)) floor.tile(LayerType::Wall, col1, r) = wall;
    }
}

} // namespace iso
