#pragma once
// iso_engine editor — tile catalog
// Loads tile images from a directory, creates thumbnails
// ─────────────────────────────────────────────────────

#include "raylib.h"
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace iso_ed {

namespace fs = std::filesystem;

// ─── Tile entry ───────────────────────────────

struct TileEntry {
    int          id = 0;             // unique tile ID (index in catalog)
    std::string  name;               // filename without extension
    std::string  path;               // full file path
    Texture2D    texture{};          // full-size texture
    Texture2D    thumbnail{};        // scaled-down for palette display
    int          src_width  = 0;     // original image width
    int          src_height = 0;     // original image height

    // Category (auto-detected from size or subfolder)
    enum class Category : uint8_t {
        Ground,       // 512x256 ground/floor tiles
        Wall,         // 512x256 or 160x128 wall tiles
        BuildingCube, // 128x128 stackable blocks
        Object,       // furniture, props
        Roof,         // free-form roof pieces
        Tree,         // free-form vegetation
        Other         // everything else
    } category = Category::Other;

    bool loaded = false;
};

// ─── Tile catalog ─────────────────────────────

class TileCatalog {
public:
    static constexpr int THUMB_SIZE = 64; // thumbnail max dimension

    TileCatalog() = default;
    ~TileCatalog() { unload_all(); }

    // Non-copyable (owns GPU textures)
    TileCatalog(const TileCatalog&) = delete;
    TileCatalog& operator=(const TileCatalog&) = delete;

    /// Load all supported images from a directory (recursive)
    /// Returns number of tiles loaded
    int load_directory(const std::string& dir_path) {
        if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
            TraceLog(LOG_WARNING, "TileCatalog: directory not found: %s", dir_path.c_str());
            return 0;
        }

        root_path_ = dir_path;
        int count = 0;

        // Collect all image files
        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".bmp" || ext == ".tga") {
                files.push_back(entry.path());
            }
        }

        // Sort alphabetically for consistent IDs
        std::sort(files.begin(), files.end());

        // Load each file
        for (const auto& file : files) {
            TileEntry entry;
            entry.id   = next_id_++;
            entry.name = file.stem().string();
            entry.path = file.string();

            // Load image
            Image img = LoadImage(entry.path.c_str());
            if (img.data == nullptr) {
                TraceLog(LOG_WARNING, "TileCatalog: failed to load: %s", entry.path.c_str());
                continue;
            }

            entry.src_width  = img.width;
            entry.src_height = img.height;
            entry.category   = detect_category(img.width, img.height, file);

            // Create full texture
            entry.texture = LoadTextureFromImage(img);
            SetTextureFilter(entry.texture, TEXTURE_FILTER_BILINEAR);

            // Create thumbnail
            Image thumb = ImageCopy(img);
            float scale = std::min(
                (float)THUMB_SIZE / img.width,
                (float)THUMB_SIZE / img.height
            );
            int tw = (int)(img.width  * scale);
            int th = (int)(img.height * scale);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            ImageResize(&thumb, tw, th);
            entry.thumbnail = LoadTextureFromImage(thumb);
            UnloadImage(thumb);
            UnloadImage(img);

            entry.loaded = true;
            tiles_.push_back(std::move(entry));
            count++;
        }

        TraceLog(LOG_INFO, "TileCatalog: loaded %d tiles from %s", count, dir_path.c_str());
        return count;
    }

    /// Add a single tile from an image file
    int add_tile(const std::string& file_path) {
        if (!fs::exists(file_path)) return -1;

        TileEntry entry;
        entry.id   = next_id_++;
        entry.name = fs::path(file_path).stem().string();
        entry.path = file_path;

        Image img = LoadImage(file_path.c_str());
        if (img.data == nullptr) return -1;

        entry.src_width  = img.width;
        entry.src_height = img.height;
        entry.category   = detect_category(img.width, img.height, fs::path(file_path));
        entry.texture    = LoadTextureFromImage(img);
        SetTextureFilter(entry.texture, TEXTURE_FILTER_BILINEAR);

        Image thumb = ImageCopy(img);
        float scale = std::min((float)THUMB_SIZE / img.width, (float)THUMB_SIZE / img.height);
        ImageResize(&thumb, std::max(1, (int)(img.width * scale)),
                            std::max(1, (int)(img.height * scale)));
        entry.thumbnail = LoadTextureFromImage(thumb);
        UnloadImage(thumb);
        UnloadImage(img);

        entry.loaded = true;
        int id = entry.id;
        tiles_.push_back(std::move(entry));
        return id;
    }

    /// Unload all textures
    void unload_all() {
        for (auto& t : tiles_) {
            if (t.loaded) {
                UnloadTexture(t.texture);
                UnloadTexture(t.thumbnail);
                t.loaded = false;
            }
        }
        tiles_.clear();
        next_id_ = 1; // 0 = empty/eraser
    }

    // ─── Access ───────────────────────────────

    [[nodiscard]] const TileEntry* get(int id) const {
        for (const auto& t : tiles_)
            if (t.id == id) return &t;
        return nullptr;
    }

    [[nodiscard]] const std::vector<TileEntry>& tiles() const { return tiles_; }
    [[nodiscard]] int count() const { return (int)tiles_.size(); }

    /// Get tiles filtered by category
    [[nodiscard]] std::vector<const TileEntry*> by_category(TileEntry::Category cat) const {
        std::vector<const TileEntry*> result;
        for (const auto& t : tiles_)
            if (t.category == cat) result.push_back(&t);
        return result;
    }

    [[nodiscard]] const std::string& root_path() const { return root_path_; }

    // ─── Category names ───────────────────────

    static const char* category_name(TileEntry::Category cat) {
        switch (cat) {
            case TileEntry::Category::Ground:       return "Ground";
            case TileEntry::Category::Wall:         return "Wall";
            case TileEntry::Category::BuildingCube: return "Cube";
            case TileEntry::Category::Object:       return "Object";
            case TileEntry::Category::Roof:         return "Roof";
            case TileEntry::Category::Tree:         return "Tree";
            case TileEntry::Category::Other:        return "Other";
        }
        return "?";
    }

private:
    std::vector<TileEntry> tiles_;
    std::string root_path_;
    int next_id_ = 1;  // 0 is reserved for "empty/no tile"

    /// Guess category from image dimensions and path
    static TileEntry::Category detect_category(int w, int h, const fs::path& path) {
        // Check subfolder name first
        std::string parent = path.parent_path().filename().string();
        std::transform(parent.begin(), parent.end(), parent.begin(), ::tolower);

        if (parent.find("ground") != std::string::npos || parent.find("floor") != std::string::npos)
            return TileEntry::Category::Ground;
        if (parent.find("wall") != std::string::npos)
            return TileEntry::Category::Wall;
        if (parent.find("roof") != std::string::npos)
            return TileEntry::Category::Roof;
        if (parent.find("tree") != std::string::npos || parent.find("veget") != std::string::npos)
            return TileEntry::Category::Tree;
        if (parent.find("object") != std::string::npos || parent.find("prop") != std::string::npos ||
            parent.find("furn") != std::string::npos)
            return TileEntry::Category::Object;
        if (parent.find("cube") != std::string::npos || parent.find("block") != std::string::npos)
            return TileEntry::Category::BuildingCube;

        // Check filename
        std::string name = path.stem().string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (name.find("ground") != std::string::npos || name.find("grass") != std::string::npos ||
            name.find("dirt") != std::string::npos || name.find("floor") != std::string::npos)
            return TileEntry::Category::Ground;
        if (name.find("wall") != std::string::npos)
            return TileEntry::Category::Wall;
        if (name.find("roof") != std::string::npos)
            return TileEntry::Category::Roof;
        if (name.find("tree") != std::string::npos)
            return TileEntry::Category::Tree;

        // Guess from dimensions
        if (w == 128 && h == 128)
            return TileEntry::Category::BuildingCube;
        if (w == 512 && h == 256)
            return TileEntry::Category::Ground; // or wall, but ground is more common
        if (w == 160 && h == 128)
            return TileEntry::Category::Wall;

        return TileEntry::Category::Other;
    }
};

} // namespace iso_ed
