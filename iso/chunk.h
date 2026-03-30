#pragma once
// iso_engine — chunk-based map system
// Map is divided into CHUNK_SIZE×CHUNK_SIZE chunks
// Chunks allocated lazily — only when you write to them
// ───────────────────────────────────────────────────

#include "core.h"
#include "map.h"    // TileData, TileFlags, LayerType, LAYER_COUNT
#include "entity.h"

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <span>
#include <functional>

namespace iso {

// ─── Constants ────────────────────────────────

inline constexpr int CHUNK_SIZE       = 16;  // tiles per chunk side
inline constexpr int CHUNK_TILE_COUNT = CHUNK_SIZE * CHUNK_SIZE; // 256

// ─── Chunk coordinate ─────────────────────────

struct ChunkCoord {
    int cx = 0, cy = 0;

    bool operator==(const ChunkCoord&) const = default;

    /// Tile position → chunk coordinate
    [[nodiscard]] static ChunkCoord from_tile(int col, int row) {
        return {
            (col >= 0) ? col / CHUNK_SIZE : (col - CHUNK_SIZE + 1) / CHUNK_SIZE,
            (row >= 0) ? row / CHUNK_SIZE : (row - CHUNK_SIZE + 1) / CHUNK_SIZE
        };
    }

    /// Tile position → local position within chunk (0..CHUNK_SIZE-1)
    [[nodiscard]] static Vec2i to_local(int col, int row) {
        int lx = col % CHUNK_SIZE;
        int ly = row % CHUNK_SIZE;
        if (lx < 0) lx += CHUNK_SIZE;
        if (ly < 0) ly += CHUNK_SIZE;
        return {lx, ly};
    }

    /// First tile col/row of this chunk in world space
    [[nodiscard]] int origin_col() const { return cx * CHUNK_SIZE; }
    [[nodiscard]] int origin_row() const { return cy * CHUNK_SIZE; }
};

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const {
        // Combine two ints into one 64-bit key
        auto h = std::hash<uint64_t>{};
        return h((static_cast<uint64_t>(static_cast<uint32_t>(c.cx)) << 32)
               | static_cast<uint64_t>(static_cast<uint32_t>(c.cy)));
    }
};

// ─── Chunk (CHUNK_SIZE × CHUNK_SIZE tile block) ─

class Chunk {
public:
    Chunk() = default;

    Chunk(int min_floor, int max_floor)
        : min_floor_(min_floor), max_floor_(max_floor)
    {
        int fc = floor_count();
        data_.resize(static_cast<size_t>(fc) * LAYER_COUNT * CHUNK_TILE_COUNT);
    }

    // ─── Tile access ──────────────────────────

    [[nodiscard]] TileData& tile(LayerType layer, int lc, int lr, int floor) {
        assert(in_bounds(lc, lr, floor));
        return data_[index(layer, lc, lr, floor)];
    }

    [[nodiscard]] const TileData& tile(LayerType layer, int lc, int lr, int floor) const {
        assert(in_bounds(lc, lr, floor));
        return data_[index(layer, lc, lr, floor)];
    }

    [[nodiscard]] TileData tile_safe(LayerType layer, int lc, int lr, int floor) const {
        if (!in_bounds(lc, lr, floor)) return {};
        return data_[index(layer, lc, lr, floor)];
    }

    // ─── Queries ──────────────────────────────

    [[nodiscard]] bool is_solid(int lc, int lr, int floor) const {
        if (!in_bounds(lc, lr, floor)) return true;
        for (int i = 0; i < LAYER_COUNT; ++i) {
            if (data_[index(static_cast<LayerType>(i), lc, lr, floor)].is_solid())
                return true;
        }
        return false;
    }

    /// Check if chunk has any non-empty tiles
    [[nodiscard]] bool is_empty() const {
        for (const auto& td : data_)
            if (td.tile_id != 0) return false;
        return true;
    }

    // ─── Floor info ───────────────────────────

    [[nodiscard]] int min_floor()   const { return min_floor_; }
    [[nodiscard]] int max_floor()   const { return max_floor_; }
    [[nodiscard]] int floor_count() const { return max_floor_ - min_floor_ + 1; }

    // ─── Raw data (for serialization) ─────────

    [[nodiscard]] std::span<TileData>       raw_data()       { return data_; }
    [[nodiscard]] std::span<const TileData> raw_data() const { return data_; }
    [[nodiscard]] size_t data_size() const { return data_.size(); }

    // ─── Iteration ────────────────────────────

    /// Iterate all non-empty tiles: fn(local_col, local_row, floor, layer, TileData&)
    template<typename Fn>
    void for_each_tile(Fn&& fn) const {
        for (int f = min_floor_; f <= max_floor_; ++f) {
            for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                auto lt = static_cast<LayerType>(layer);
                for (int lr = 0; lr < CHUNK_SIZE; ++lr) {
                    for (int lc = 0; lc < CHUNK_SIZE; ++lc) {
                        const auto& td = data_[index(lt, lc, lr, f)];
                        if (!td.is_empty())
                            fn(lc, lr, f, lt, td);
                    }
                }
            }
        }
    }

private:
    [[nodiscard]] bool in_bounds(int lc, int lr, int floor) const {
        return lc >= 0 && lc < CHUNK_SIZE
            && lr >= 0 && lr < CHUNK_SIZE
            && floor >= min_floor_ && floor <= max_floor_;
    }

    [[nodiscard]] size_t index(LayerType layer, int lc, int lr, int floor) const {
        int fi = floor - min_floor_;
        return static_cast<size_t>(fi) * (LAYER_COUNT * CHUNK_TILE_COUNT)
             + static_cast<int>(layer) * CHUNK_TILE_COUNT
             + lr * CHUNK_SIZE + lc;
    }

    int min_floor_ = 0;
    int max_floor_ = 0;
    std::vector<TileData> data_;
};

// ─── ChunkMap (replaces IsoMap for large worlds) ─

class ChunkMap {
public:
    ChunkMap() = default;
    explicit ChunkMap(const IsoConfig& cfg) : config_(cfg) {}

    // ─── Tile access (same API as IsoMap) ─────

    [[nodiscard]] TileData tile(LayerType layer, TileCoord tc) const {
        auto cc = ChunkCoord::from_tile(tc.col, tc.row);
        auto it = chunks_.find(cc);
        if (it == chunks_.end()) return {};
        auto local = ChunkCoord::to_local(tc.col, tc.row);
        return it->second.tile_safe(layer, local.x, local.y, tc.floor);
    }

    void set_tile(LayerType layer, TileCoord tc, TileData data) {
        auto cc = ChunkCoord::from_tile(tc.col, tc.row);
        auto& chunk = get_or_create(cc);
        auto local = ChunkCoord::to_local(tc.col, tc.row);
        if (local.x >= 0 && local.x < CHUNK_SIZE
         && local.y >= 0 && local.y < CHUNK_SIZE
         && tc.floor >= config_.min_floor && tc.floor <= config_.max_floor)
        {
            chunk.tile(layer, local.x, local.y, tc.floor) = data;
        }
    }

    [[nodiscard]] bool is_solid(TileCoord tc) const {
        auto cc = ChunkCoord::from_tile(tc.col, tc.row);
        auto it = chunks_.find(cc);
        if (it == chunks_.end()) return true; // unloaded = solid
        auto local = ChunkCoord::to_local(tc.col, tc.row);
        return it->second.is_solid(local.x, local.y, tc.floor);
    }

    // ─── Chunk management ─────────────────────

    [[nodiscard]] Chunk* chunk_at(ChunkCoord cc) {
        auto it = chunks_.find(cc);
        return (it != chunks_.end()) ? &it->second : nullptr;
    }

    [[nodiscard]] const Chunk* chunk_at(ChunkCoord cc) const {
        auto it = chunks_.find(cc);
        return (it != chunks_.end()) ? &it->second : nullptr;
    }

    Chunk& get_or_create(ChunkCoord cc) {
        auto it = chunks_.find(cc);
        if (it != chunks_.end()) return it->second;
        auto [new_it, ok] = chunks_.emplace(
            cc, Chunk(config_.min_floor, config_.max_floor));
        return new_it->second;
    }

    /// Remove chunks that have no tile data
    int prune_empty() {
        int removed = 0;
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (it->second.is_empty()) {
                it = chunks_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    // ─── Visible chunk range ──────────────────

    /// Get chunk coordinate range covering the given tile rectangle
    [[nodiscard]] static std::pair<ChunkCoord, ChunkCoord>
    chunk_range(int col_min, int row_min, int col_max, int row_max) {
        return {
            ChunkCoord::from_tile(col_min, row_min),
            ChunkCoord::from_tile(col_max, row_max)
        };
    }

    /// Iterate loaded chunks in a coordinate range
    template<typename Fn>
    void for_each_chunk_in(ChunkCoord cmin, ChunkCoord cmax, Fn&& fn) const {
        for (int cy = cmin.cy; cy <= cmax.cy; ++cy) {
            for (int cx = cmin.cx; cx <= cmax.cx; ++cx) {
                ChunkCoord cc{cx, cy};
                auto it = chunks_.find(cc);
                if (it != chunks_.end())
                    fn(cc, it->second);
            }
        }
    }

    /// Iterate ALL loaded chunks
    template<typename Fn>
    void for_each_chunk(Fn&& fn) const {
        for (const auto& [cc, chunk] : chunks_)
            fn(cc, chunk);
    }

    // ─── Entity integration ───────────────────

    [[nodiscard]] EntityLayer&       entities()       { return entities_; }
    [[nodiscard]] const EntityLayer& entities() const { return entities_; }

    // ─── Info ─────────────────────────────────

    [[nodiscard]] int chunk_count() const { return (int)chunks_.size(); }

    /// Compute bounding box of all loaded chunks (in tile coordinates)
    [[nodiscard]] std::pair<TileCoord, TileCoord> bounds() const {
        if (chunks_.empty())
            return {{0, 0, 0}, {0, 0, 0}};

        int min_cx = INT_MAX, min_cy = INT_MAX;
        int max_cx = INT_MIN, max_cy = INT_MIN;
        for (const auto& [cc, _] : chunks_) {
            min_cx = std::min(min_cx, cc.cx);
            min_cy = std::min(min_cy, cc.cy);
            max_cx = std::max(max_cx, cc.cx);
            max_cy = std::max(max_cy, cc.cy);
        }
        return {
            {min_cx * CHUNK_SIZE, min_cy * CHUNK_SIZE, config_.min_floor},
            {(max_cx + 1) * CHUNK_SIZE - 1, (max_cy + 1) * CHUNK_SIZE - 1, config_.max_floor}
        };
    }

    [[nodiscard]] const IsoConfig& config() const { return config_; }
    [[nodiscard]] IsoConfig&       config()       { return config_; }

    void clear() {
        chunks_.clear();
        entities_.clear();
    }

    // ─── Convert from IsoMap ──────────────────

    /// Import tiles from an existing IsoMap (for migration)
    void import_from(const IsoMap& map) {
        config_ = map.config();
        chunks_.clear();

        for (int f = map.min_floor(); f <= map.max_floor(); ++f) {
            const Floor* floor = map.floor_safe(f);
            if (!floor) continue;
            for (int row = 0; row < floor->rows(); ++row) {
                for (int col = 0; col < floor->cols(); ++col) {
                    for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                        auto lt = static_cast<LayerType>(layer);
                        TileData td = floor->tile_safe(lt, col, row);
                        if (!td.is_empty())
                            set_tile(lt, {col, row, f}, td);
                    }
                }
            }
        }
    }

    /// Export to an IsoMap (bounded, for backward compat)
    [[nodiscard]] IsoMap export_to_isomap() const {
        auto [bmin, bmax] = bounds();
        IsoConfig cfg = config_;
        cfg.default_map_cols = bmax.col - bmin.col + 1;
        cfg.default_map_rows = bmax.row - bmin.row + 1;

        IsoMap map(cfg.default_map_cols, cfg.default_map_rows,
                   cfg.min_floor, cfg.max_floor, cfg);

        for (const auto& [cc, chunk] : chunks_) {
            chunk.for_each_tile([&](int lc, int lr, int f, LayerType layer, const TileData& td) {
                int col = cc.origin_col() + lc - bmin.col;
                int row = cc.origin_row() + lr - bmin.row;
                if (col >= 0 && col < cfg.default_map_cols &&
                    row >= 0 && row < cfg.default_map_rows)
                {
                    map.set_tile(layer, {col, row, f}, td);
                }
            });
        }
        return map;
    }

private:
    IsoConfig config_;
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
    EntityLayer entities_;
};

} // namespace iso
