#include "reclaim/treemap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace reclaim {

namespace {

struct WeightedItem {
    const FileNode* node{nullptr};
    double area{0.0};
    int color_index{0};
};

double worst_aspect_ratio(const std::vector<WeightedItem>& row, double side_length) {
    if (row.empty() || side_length <= 0.0) return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    double min_a = std::numeric_limits<double>::infinity();
    double max_a = 0.0;
    for (const auto& item : row) {
        sum += item.area;
        min_a = std::min(min_a, item.area);
        max_a = std::max(max_a, item.area);
    }
    if (min_a <= 0.0 || sum <= 0.0) return std::numeric_limits<double>::infinity();
    const double s2 = side_length * side_length;
    const double sum2 = sum * sum;
    return std::max((s2 * max_a) / sum2, sum2 / (s2 * min_a));
}

void layout_row(const std::vector<WeightedItem>& row,
                RectF& remaining,
                uint64_t parent_total,
                std::vector<TreemapTile>& out) {
    if (row.empty() || remaining.width <= 0.0 || remaining.height <= 0.0) return;

    double row_area = 0.0;
    for (const auto& item : row) row_area += item.area;
    if (row_area <= 0.0) return;

    const bool horizontal_strip = remaining.width >= remaining.height;
    if (horizontal_strip) {
        // Place column along left edge of remaining rect
        const double strip_w = std::min(remaining.width, row_area / remaining.height);
        double cursor_y = remaining.y;
        for (const auto& item : row) {
            const double h = (strip_w > 0.0) ? (item.area / strip_w) : 0.0;
            out.push_back(TreemapTile{
                item.node->name,
                item.node->path,
                item.node->size_bytes,
                parent_total > 0 ? static_cast<double>(item.node->size_bytes) / static_cast<double>(parent_total) : 0.0,
                item.node->is_directory,
                item.color_index,
                RectF{remaining.x, cursor_y, strip_w, h}
            });
            cursor_y += h;
        }
        remaining.x += strip_w;
        remaining.width = std::max(0.0, remaining.width - strip_w);
    } else {
        // Place row along top edge of remaining rect
        const double strip_h = std::min(remaining.height, row_area / remaining.width);
        double cursor_x = remaining.x;
        for (const auto& item : row) {
            const double w = (strip_h > 0.0) ? (item.area / strip_h) : 0.0;
            out.push_back(TreemapTile{
                item.node->name,
                item.node->path,
                item.node->size_bytes,
                parent_total > 0 ? static_cast<double>(item.node->size_bytes) / static_cast<double>(parent_total) : 0.0,
                item.node->is_directory,
                item.color_index,
                RectF{cursor_x, remaining.y, w, strip_h}
            });
            cursor_x += w;
        }
        remaining.y += strip_h;
        remaining.height = std::max(0.0, remaining.height - strip_h);
    }
}

}  // namespace

std::vector<TreemapTile> compute_squarified_treemap(const FileNode& node,
                                                    RectF bounds,
                                                    size_t max_tiles) {
    std::vector<TreemapTile> tiles;
    if (bounds.width <= 0.0 || bounds.height <= 0.0 || node.children.empty()) {
        return tiles;
    }

    std::vector<const FileNode*> sorted;
    sorted.reserve(node.children.size());
    for (const auto& child : node.children) {
        if (child.size_bytes > 0) sorted.push_back(&child);
    }
    if (sorted.empty()) return tiles;

    std::sort(sorted.begin(), sorted.end(),
              [](const FileNode* a, const FileNode* b) { return a->size_bytes > b->size_bytes; });

    if (sorted.size() > max_tiles) {
        sorted.resize(max_tiles);
    }

    uint64_t displayed_total = 0;
    for (const auto* child : sorted) displayed_total += child->size_bytes;
    if (displayed_total == 0) return tiles;

    const double total_area = bounds.width * bounds.height;
    std::vector<WeightedItem> items;
    items.reserve(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i) {
        const double area = (static_cast<double>(sorted[i]->size_bytes) /
                             static_cast<double>(displayed_total)) * total_area;
        items.push_back(WeightedItem{sorted[i], area, static_cast<int>(i % 6)});
    }

    RectF remaining = bounds;
    std::vector<WeightedItem> current_row;

    for (const auto& item : items) {
        const double short_side = std::min(remaining.width, remaining.height);
        if (current_row.empty()) {
            current_row.push_back(item);
            continue;
        }

        std::vector<WeightedItem> candidate = current_row;
        candidate.push_back(item);

        if (worst_aspect_ratio(candidate, short_side) <= worst_aspect_ratio(current_row, short_side)) {
            current_row.push_back(item);
        } else {
            layout_row(current_row, remaining, node.size_bytes, tiles);
            current_row.clear();
            current_row.push_back(item);
        }
    }

    if (!current_row.empty()) {
        layout_row(current_row, remaining, node.size_bytes, tiles);
    }

    return tiles;
}

}  // namespace reclaim
