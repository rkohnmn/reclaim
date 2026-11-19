#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app_state.hpp"
#include "theme.hpp"

namespace reclaim::ui {

struct HitRegion {
    Gdiplus::RectF rect;
    int command_id{0};
    int payload_index{-1};
    int sub_index{-1};
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void set_initial_scan_path(const std::filesystem::path& path);
    bool create(HINSTANCE instance, int show_cmd);
    HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void on_paint();
    void on_mouse_move(int x, int y);
    void on_lbutton_down(int x, int y);
    void on_mouse_wheel(int delta);
    void on_key_down(WPARAM vk);
    void on_command(int cmd_id, int payload_idx = -1, int sub_idx = -1);

    void start_scan_async();
    void start_duplicate_scan_async();
    void start_clean_async();
    void restore_batch_by_index(int index);
    void restore_entry_by_index(int batch_index, int entry_index);
    void purge_batch_by_index(int index);
    void purge_expired_batches();
    void toggle_context_menu();
    void export_rules_pack();
    void export_html_report();
    void browse_target_folder();
    void refresh_quarantine_list();
    void join_worker();

    void draw_nav_rail(Gdiplus::Graphics& g, const ThemePalette& p, float h);
    void draw_top_bar(Gdiplus::Graphics& g, const ThemePalette& p, float w);
    void draw_overview(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area);
    void draw_treemap(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area);
    void draw_duplicates(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area);
    void draw_quarantine(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area);
    void draw_settings(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area);

    HWND hwnd_{nullptr};
    HINSTANCE instance_{nullptr};
    UINT dpi_{96};
    bool auto_scan_on_launch_{false};
    bool prefer_ntfs_mft_{false};
    int expanded_batch_index_{-1};

    AppState state_;
    ConfigStore config_store_;
    QuarantineVault vault_;
    std::vector<CleanupRule> active_rules_;

    std::vector<HitRegion> hit_regions_;
    std::atomic<bool> cancel_flag_{false};
    std::thread worker_;

    std::mutex result_mutex_;
    std::optional<Result<ScanReport>> pending_scan_;
    std::optional<Result<DuplicateReport>> pending_dupes_;
    std::optional<Result<CleanResult>> pending_clean_;
    std::string live_progress_text_;
};

}  // namespace reclaim::ui

#endif  // _WIN32
