#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct RectF {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

struct TreemapTile {
    std::string name;
    std::filesystem::path path;
    uint64_t size_bytes{0};
    double fraction_of_parent{0.0};
    bool is_directory{false};
    int color_index{0};
    RectF rect;
};

// Squarified Treemap layout (Bruls, Huizing & van Wijk, 2000).
// Produces low-aspect-ratio rectangles whose areas are strictly proportional to size_bytes
// (Lie Factor = 1.00).
std::vector<TreemapTile> compute_squarified_treemap(const FileNode& node,
                                                    RectF bounds,
                                                    size_t max_tiles = 60);

}  // namespace reclaim
