#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/config.hpp"
#include "reclaim/duplicates.hpp"
#include "reclaim/quarantine.hpp"
#include "reclaim/scanner.hpp"
#include "reclaim/treemap.hpp"
#include "reclaim/types.hpp"

namespace reclaim::ui {

enum class NavView {
    Overview = 0,
    Treemap = 1,
    Duplicates = 2,
    Quarantine = 3,
    Settings = 4
};

enum class AppStatus {
    Idle = 0,
    Scanning,
    Ready,
    Cleaning,
    Error
};

struct AppState {
    NavView active_view{NavView::Overview};
    AppStatus status{AppStatus::Idle};
    AppConfig config;

    std::filesystem::path target_path;
    std::string status_banner{"Ready — select a drive or folder and press Scan (F5)."};
    std::string error_detail;

    ScanReport scan_report;
    bool has_scan_report{false};

    DuplicateReport dup_report;
    bool has_dup_report{false};

    std::vector<QuarantineBatch> quarantine_batches;

    // Treemap navigation stack (indices of children drilled into)
    std::vector<size_t> treemap_drill_path;
    std::vector<TreemapTile> treemap_tiles;
    int hovered_tile_index{-1};

    int scroll_offset{0};
    bool dry_run{false};

    const FileNode* current_treemap_node() const {
        if (!has_scan_report) return nullptr;
        const FileNode* cur = &scan_report.root_tree;
        for (size_t idx : treemap_drill_path) {
            if (idx < cur->children.size()) {
                cur = &cur->children[idx];
            } else {
                break;
            }
        }
        return cur;
    }

    uint64_t selected_reclaim_bytes() const {
        uint64_t total = 0;
        for (const auto& item : scan_report.items) {
            if (item.selected) total += item.size_bytes;
        }
        return total;
    }

    size_t selected_reclaim_count() const {
        size_t count = 0;
        for (const auto& item : scan_report.items) {
            if (item.selected) ++count;
        }
        return count;
    }
};

}  // namespace reclaim::ui
