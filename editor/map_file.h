#define _CRT_SECURE_NO_WARNINGS
#pragma once

// iso_engine editor — map file format (save/load)
// ────────────────────────────────────────────────

#include "..//iso/iso.h"
#include <fstream>
#include <string>
#include <cstring>

namespace iso_ed {

// ─── File format ──────────────────────────────
// Header:
//   magic[4]     "ISOM"
//   version      uint32
//   cols         int32
//   rows         int32
//   min_floor    int32
//   max_floor    int32
//   tile_width   int32
//   tile_height  int32
//   reserved[32] (future use)
//
// Then for each floor (min_floor to max_floor):
//   For each layer (0..3):
//     For each row, col:
//       TileData { tile_id(u16), flags(u32), height(u8), variant(u8), user_data(u16) }
#pragma pack(push, 1)
struct MapFileHeader {
    char     magic[4]    = {'I','S','O','M'};
    uint32_t version     = 1;
    int32_t  cols        = 0;
    int32_t  rows        = 0;
    int32_t  min_floor   = 0;
    int32_t  max_floor   = 0;
    int32_t  tile_width  = 128;
    int32_t  tile_height = 64;
    uint8_t  reserved[32] = {};
};

static_assert(sizeof(MapFileHeader) == 64);

struct TileDataPacked {
    uint16_t tile_id;
    uint32_t flags;
    uint8_t  height;
    uint8_t  variant;
    uint16_t user_data;
};

static_assert(sizeof(TileDataPacked) == 10);
#pragma pack(pop)
// ─── Save ─────────────────────────────────────

inline bool save_map(const iso::IsoMap& map, const std::string& filepath) 
{
    sizeof(MapFileHeader);
    sizeof(TileDataPacked);

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    const auto& cfg = map.config();

    MapFileHeader header;
    header.cols        = cfg.default_map_cols;
    header.rows        = cfg.default_map_rows;
    header.min_floor   = cfg.min_floor;
    header.max_floor   = cfg.max_floor;
    header.tile_width  = cfg.tile_width;
    header.tile_height = cfg.tile_height;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
        const iso::Floor& floor = map.floor(f);
        for (int layer = 0; layer < iso::LAYER_COUNT; ++layer) {
            auto data = floor.layer_data(static_cast<iso::LayerType>(layer));
            for (const auto& td : data) {
                TileDataPacked packed;
                packed.tile_id   = td.tile_id;
                packed.flags     = static_cast<uint32_t>(td.flags);
                packed.height    = td.height;
                packed.variant   = td.variant;
                packed.user_data = td.user_data;
                file.write(reinterpret_cast<const char*>(&packed), sizeof(packed));
            }
        }
    }

    return file.good();
}

// ─── Load ─────────────────────────────────────

inline bool load_map(iso::IsoMap& map, const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    MapFileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (std::memcmp(header.magic, "ISOM", 4) != 0) {
        TraceLog(LOG_ERROR, "MapFile: invalid magic in %s", filepath.c_str());
        return false;
    }
    if (header.version != 1) {
        TraceLog(LOG_ERROR, "MapFile: unsupported version %d", header.version);
        return false;
    }

    iso::IsoConfig cfg;
    cfg.default_map_cols = header.cols;
    cfg.default_map_rows = header.rows;
    cfg.min_floor        = header.min_floor;
    cfg.max_floor        = header.max_floor;
    cfg.tile_width       = header.tile_width;
    cfg.tile_height      = header.tile_height;

    map = iso::IsoMap(cfg);

    for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
        iso::Floor& floor = map.floor(f);
        for (int layer = 0; layer < iso::LAYER_COUNT; ++layer) {
            auto data = floor.layer_data(static_cast<iso::LayerType>(layer));
            for (auto& td : data) {
                TileDataPacked packed;
                file.read(reinterpret_cast<char*>(&packed), sizeof(packed));
                td.tile_id   = packed.tile_id;
                td.flags     = static_cast<iso::TileFlags>(packed.flags);
                td.height    = packed.height;
                td.variant   = packed.variant;
                td.user_data = packed.user_data;
            }
        }
    }

    return file.good();
}

// ─── New map ──────────────────────────────────

inline iso::IsoMap create_new_map(int cols, int rows, int min_floor, int max_floor,
                                   int tile_width = 128, int tile_height = 64)
{
    iso::IsoConfig cfg;
    cfg.default_map_cols = cols;
    cfg.default_map_rows = rows;
    cfg.min_floor        = min_floor;
    cfg.max_floor        = max_floor;
    cfg.tile_width       = tile_width;
    cfg.tile_height      = tile_height;
    return iso::IsoMap(cfg);
}

} // namespace iso_ed
