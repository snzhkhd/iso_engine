#pragma once
// iso_engine — unified map file format (.isomap)
// Single file containing chunk tiles + entities
// ─────────────────────────────────────────────

#include "core.h"
#include "map.h"
#include "entity.h"
#include "chunk.h"

#include <fstream>
#include <string>
#include <cstring>
#include <cstdint>
#include <vector>

namespace iso {

// ─── File layout ──────────────────────────────
//
//  [FileHeader]           fixed 80 bytes
//  [ChunkEntry] × N       index: which chunks exist and where
//  [ChunkTileData] × N    tile data for each chunk
//  [EntityRecord] × M     entity data
//
// All multi-byte values are little-endian (native on x86/x64)
// ──────────────────────────────────────────────

// ─── Packed structures for file I/O ───────────

#pragma pack(push, 1)

struct FileHeader {
    char     magic[4]       = {'I','S','M','P'};  // "ISMP" = iso map
    uint32_t version        = 2;
    uint16_t chunk_size     = CHUNK_SIZE;          // tiles per chunk side (16)
    int16_t  min_floor      = -1;
    int16_t  max_floor      = 4;
    uint16_t tile_width     = 252;
    uint16_t tile_height    = 128;
    float    height_pixel_offset = 30.f;
    float    floor_height   = 1.f;
    float    tile_padding   = 1.f;
    uint8_t  pixel_snap     = 1;                   // bool
    uint8_t  reserved_flags = 0;
    uint32_t chunk_count    = 0;
    uint32_t entity_count   = 0;
    uint8_t  reserved[32]   = {};
};

static_assert(sizeof(FileHeader) == 72);

struct ChunkEntry {
    int32_t  cx = 0;                               // chunk coordinate
    int32_t  cy = 0;
    uint32_t data_offset = 0;                      // byte offset from start of tile data section
    uint32_t data_size   = 0;                      // byte size of this chunk's tile data
};

static_assert(sizeof(ChunkEntry) == 16);

struct TileDataPacked {
    uint16_t tile_id;
    uint32_t flags;
    uint8_t  height;
    uint8_t  variant;
    uint16_t user_data;
};

static_assert(sizeof(TileDataPacked) == 10);

struct EntityRecord {
    uint32_t id;
    uint16_t tile_id;
    float    x;
    float    y;
    int32_t  floor;
    int32_t  z_bias;
    float    rotation;
    uint8_t  flags;       // bit 0 = solid
    uint16_t user_data;
    uint8_t  reserved[3]; // pad to 32 bytes
};

static_assert(sizeof(EntityRecord) == 32);

#pragma pack(pop)

// ─── Save ─────────────────────────────────────

inline bool save_isomap(const ChunkMap& map, const std::string& filepath) 
{

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    const auto& cfg = map.config();
    const auto& ents = map.entities().all();

    // Build chunk list (skip empty)
    struct ChunkInfo {
        ChunkCoord cc;
        const Chunk* chunk;
    };
    std::vector<ChunkInfo> chunks;
    map.for_each_chunk([&](ChunkCoord cc, const Chunk& chunk) {
        if (!chunk.is_empty())
            chunks.push_back({cc, &chunk});
    });

    // ── Header ────────────────────────────────

    FileHeader header;
    header.chunk_size          = CHUNK_SIZE;
    header.min_floor           = (int16_t)cfg.min_floor;
    header.max_floor           = (int16_t)cfg.max_floor;
    header.tile_width          = (uint16_t)cfg.tile_width;
    header.tile_height         = (uint16_t)cfg.tile_height;
    header.height_pixel_offset = cfg.height_pixel_offset;
    header.floor_height        = cfg.floor_height;
    header.tile_padding        = cfg.tile_padding;
    header.pixel_snap          = cfg.pixel_snap ? 1 : 0;
    header.chunk_count         = (uint32_t)chunks.size();
    header.entity_count        = (uint32_t)ents.size();

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // ── Chunk index ───────────────────────────

    int floor_count = cfg.max_floor - cfg.min_floor + 1;
    uint32_t tiles_per_chunk = CHUNK_TILE_COUNT * LAYER_COUNT * floor_count;
    uint32_t chunk_data_size = tiles_per_chunk * sizeof(TileDataPacked);

    std::vector<ChunkEntry> index(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) {
        index[i].cx          = chunks[i].cc.cx;
        index[i].cy          = chunks[i].cc.cy;
        index[i].data_offset = (uint32_t)(i * chunk_data_size);
        index[i].data_size   = chunk_data_size;
    }
    file.write(reinterpret_cast<const char*>(index.data()),
               index.size() * sizeof(ChunkEntry));

    // ── Chunk tile data ───────────────────────

    for (const auto& ci : chunks) {
        const Chunk& chunk = *ci.chunk;
        for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
            for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                auto lt = static_cast<LayerType>(layer);
                for (int lr = 0; lr < CHUNK_SIZE; ++lr) {
                    for (int lc = 0; lc < CHUNK_SIZE; ++lc) {
                        TileData td = chunk.tile_safe(lt, lc, lr, f);
                        TileDataPacked packed;
                        packed.tile_id   = td.tile_id;
                        packed.flags     = static_cast<uint32_t>(td.flags);
                        packed.height    = td.height;
                        packed.variant   = td.variant;
                        packed.user_data = td.user_data;
                        file.write(reinterpret_cast<const char*>(&packed),
                                   sizeof(packed));
                    }
                }
            }
        }
    }

    // ── Entity data ───────────────────────────

    for (const auto& e : ents) {
        EntityRecord rec;
        rec.id        = e.id;
        rec.tile_id   = e.tile_id;
        rec.x         = e.x;
        rec.y         = e.y;
        rec.floor     = e.floor;
        rec.z_bias    = e.z_bias;
        rec.rotation  = e.rotation;
        rec.flags     = e.flags;
        rec.user_data = e.user_data;
        std::memset(rec.reserved, 0, sizeof(rec.reserved));
        file.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }

    return file.good();
}

// ─── Load ─────────────────────────────────────

inline bool load_isomap(ChunkMap& map, const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    // ── Header ────────────────────────────────

    FileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (std::memcmp(header.magic, "ISMP", 4) != 0) return false;
    if (header.version != 2) return false;

    IsoConfig cfg;
    cfg.min_floor           = header.min_floor;
    cfg.max_floor           = header.max_floor;
    cfg.tile_width          = header.tile_width;
    cfg.tile_height         = header.tile_height;
    cfg.height_pixel_offset = header.height_pixel_offset;
    cfg.floor_height        = header.floor_height;
    cfg.tile_padding        = header.tile_padding;
    cfg.pixel_snap          = (header.pixel_snap != 0);

    map.clear();
    map.config() = cfg;

    // ── Chunk index ───────────────────────────

    std::vector<ChunkEntry> index(header.chunk_count);
    file.read(reinterpret_cast<char*>(index.data()),
              header.chunk_count * sizeof(ChunkEntry));

    // ── Chunk tile data ───────────────────────

    int floor_count = cfg.max_floor - cfg.min_floor + 1;

    for (const auto& ci : index) {
        ChunkCoord cc{ci.cx, ci.cy};
        Chunk& chunk = map.get_or_create(cc);

        for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
            for (int layer = 0; layer < LAYER_COUNT; ++layer) {
                auto lt = static_cast<LayerType>(layer);
                for (int lr = 0; lr < CHUNK_SIZE; ++lr) {
                    for (int lc = 0; lc < CHUNK_SIZE; ++lc) {
                        TileDataPacked packed;
                        file.read(reinterpret_cast<char*>(&packed),
                                  sizeof(packed));
                        TileData& td = chunk.tile(lt, lc, lr, f);
                        td.tile_id   = packed.tile_id;
                        td.flags     = static_cast<TileFlags>(packed.flags);
                        td.height    = packed.height;
                        td.variant   = packed.variant;
                        td.user_data = packed.user_data;
                    }
                }
            }
        }
    }

    // ── Entity data ───────────────────────────

    EntityLayer& entities = map.entities();
    for (uint32_t i = 0; i < header.entity_count; ++i) {
        EntityRecord rec;
        file.read(reinterpret_cast<char*>(&rec), sizeof(rec));

        MapEntity e;
        e.id        = rec.id;
        e.tile_id   = rec.tile_id;
        e.x         = rec.x;
        e.y         = rec.y;
        e.floor     = rec.floor;
        e.z_bias    = rec.z_bias;
        e.rotation  = rec.rotation;
        e.flags     = rec.flags;
        e.user_data = rec.user_data;
        entities.add_raw(e);
    }

    map.prune_empty();
    return file.good();
}

// ─── Migration helpers ────────────────────────

/// Convert old .isom + .ent files to new .isomap
inline bool convert_legacy(const std::string& isom_path,
                           const std::string& ent_path,
                           const std::string& output_path)
{
    // Load legacy IsoMap
    std::ifstream isom_file(isom_path, std::ios::binary);
    if (!isom_file.is_open()) return false;

    // Read old header (64 bytes)
    struct LegacyHeader {
        char     magic[4];
        uint32_t version;
        int32_t  cols, rows;
        int32_t  min_floor, max_floor;
        int32_t  tile_width, tile_height;
        uint8_t  reserved[32];
    };

    LegacyHeader lh;
    isom_file.read(reinterpret_cast<char*>(&lh), sizeof(lh));
    if (std::memcmp(lh.magic, "ISOM", 4) != 0) return false;

    IsoConfig cfg;
    cfg.default_map_cols = lh.cols;
    cfg.default_map_rows = lh.rows;
    cfg.min_floor        = lh.min_floor;
    cfg.max_floor        = lh.max_floor;
    cfg.tile_width       = lh.tile_width;
    cfg.tile_height      = lh.tile_height;

    // Build IsoMap from legacy file
    IsoMap old_map(cfg);
    for (int f = cfg.min_floor; f <= cfg.max_floor; ++f) {
        Floor& floor = old_map.floor(f);
        for (int layer = 0; layer < LAYER_COUNT; ++layer) {
            auto data = floor.layer_data(static_cast<LayerType>(layer));
            for (auto& td : data) {
                TileDataPacked packed;
                isom_file.read(reinterpret_cast<char*>(&packed), sizeof(packed));
                td.tile_id   = packed.tile_id;
                td.flags     = static_cast<TileFlags>(packed.flags);
                td.height    = packed.height;
                td.variant   = packed.variant;
                td.user_data = packed.user_data;
            }
        }
    }
    isom_file.close();

    // Convert to ChunkMap
    ChunkMap chunk_map(cfg);
    chunk_map.import_from(old_map);

    // Load legacy entities (.ent file) if exists
    std::ifstream ent_file(ent_path, std::ios::binary);
    if (ent_file.is_open()) {
        char ent_magic[4];
        ent_file.read(ent_magic, 4);
        if (std::memcmp(ent_magic, "ISOE", 4) == 0) {
            uint32_t ent_version, ent_count;
            ent_file.read(reinterpret_cast<char*>(&ent_version), 4);
            ent_file.read(reinterpret_cast<char*>(&ent_count), 4);

            for (uint32_t i = 0; i < ent_count; ++i) {
                MapEntity e;
                ent_file.read(reinterpret_cast<char*>(&e.id), 4);
                ent_file.read(reinterpret_cast<char*>(&e.tile_id), 2);
                ent_file.read(reinterpret_cast<char*>(&e.x), 4);
                ent_file.read(reinterpret_cast<char*>(&e.y), 4);
                ent_file.read(reinterpret_cast<char*>(&e.floor), 4);
                ent_file.read(reinterpret_cast<char*>(&e.rotation), 4);
                uint8_t flags;
                ent_file.read(reinterpret_cast<char*>(&flags), 1);
                e.flags = flags;
                ent_file.read(reinterpret_cast<char*>(&e.user_data), 2);
                ent_file.read(reinterpret_cast<char*>(&e.z_bias), 4);
                chunk_map.entities().add_raw(e);
            }
        }
        ent_file.close();
    }

    return save_isomap(chunk_map, output_path);
}

} // namespace iso
