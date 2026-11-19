#ifdef _WIN32

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "main_window.hpp"
#include "resource.h"

#include "reclaim/format.hpp"
#include "reclaim/ntfs_mft.hpp"
#include "reclaim/reporter.hpp"
#include "reclaim/rules.hpp"
#include "reclaim/shell_integration.hpp"

#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace reclaim::ui {

namespace {

constexpr UINT WM_APP_SCAN_DONE = WM_APP + 1;
constexpr UINT WM_APP_DUPES_DONE = WM_APP + 2;
constexpr UINT WM_APP_CLEAN_DONE = WM_APP + 3;
constexpr UINT WM_APP_PROGRESS = WM_APP + 4;

constexpr int kCmdToggleItem = 2000;
constexpr int kCmdRestoreBatch = 3000;
constexpr int kCmdPurgeBatch = 3500;
constexpr int kCmdExpandBatch = 3600;
constexpr int kCmdRestoreEntry = 3700;
constexpr int kCmdPurgeExpired = 3800;
constexpr int kCmdCleanDupes = 4000;
constexpr int kCmdRunDupes = 4001;
constexpr int kCmdToggleMft = 4200;
constexpr int kCmdToggleContextMenu = 4201;
constexpr int kCmdExportRules = 4202;
constexpr int kCmdTreemapTile = 5000;

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::filesystem::path default_windows_scan_root() {
    if (const char* profile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(profile);
    }
    return std::filesystem::path("C:\\");
}

}  // namespace

MainWindow::MainWindow()
    : config_store_(ConfigStore::default_data_dir() / "reclaim.cfg"),
      vault_(ConfigStore::default_data_dir() / "quarantine"),
      active_rules_(default_cleanup_rules()) {
    auto loaded = config_store_.load();
    if (loaded) {
        state_.config = loaded.value();
        state_.dry_run = state_.config.dry_run_default;
    }
    state_.target_path = state_.config.last_scan_path.empty()
                             ? default_windows_scan_root()
                             : std::filesystem::path(state_.config.last_scan_path);

    // Load custom user rules.json if present
    const auto custom_rules = ConfigStore::default_data_dir() / "rules.json";
    std::error_code ec;
    if (std::filesystem::exists(custom_rules, ec)) {
        auto r = load_rules_from_json_file(custom_rules);
        if (r) active_rules_ = std::move(r.value());
    }

    // Automatically purge expired quarantine batches past retention policy
    vault_.purge_expired_batches(state_.config.quarantine_retention_days, false);
    refresh_quarantine_list();
}

MainWindow::~MainWindow() {
    cancel_flag_.store(true);
    join_worker();
}

void MainWindow::set_initial_scan_path(const std::filesystem::path& path) {
    state_.target_path = path;
    auto_scan_on_launch_ = true;
}

void MainWindow::join_worker() {
    if (worker_.joinable()) worker_.join();
}

bool MainWindow::create(HINSTANCE instance, int show_cmd) {
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::wnd_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_RECLAIM_ICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ReclaimMainWindowClass";

    if (!RegisterClassExW(&wc)) return false;

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Reclaim v1.1 — Storage Analyzer & Quarantine Cleaner",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1220, 780,
        nullptr, nullptr, instance, this);

    if (!hwnd_) return false;

    apply_immersive_dark_titlebar(hwnd_, state_.config.dark_theme);
    ShowWindow(hwnd_, show_cmd);
    UpdateWindow(hwnd_);

    if (auto_scan_on_launch_) {
        start_scan_async();
    }
    return true;
}

LRESULT CALLBACK MainWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle_message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: on_paint(); return 0;
        case WM_MOUSEMOVE: on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
        case WM_LBUTTONDOWN: on_lbutton_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
        case WM_MOUSEWHEEL: on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_KEYDOWN: on_key_down(wp); return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 920;
            mmi->ptMinTrackSize.y = 600;
            return 0;
        }
        case WM_APP_PROGRESS: {
            std::lock_guard<std::mutex> lock(result_mutex_);
            state_.status_banner = live_progress_text_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_SCAN_DONE: {
            join_worker();
            std::optional<Result<ScanReport>> res;
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                res = std::move(pending_scan_);
                pending_scan_.reset();
            }
            if (res && res->ok()) {
                state_.scan_report = std::move(res->value());
                state_.has_scan_report = true;
                state_.treemap_drill_path.clear();
                state_.status = AppStatus::Ready;
                const char* backend = (state_.scan_report.backend_used == ScanBackend::NtfsMft) ? "NTFS $MFT" : "Filesystem";
                state_.status_banner = "Found " + format_bytes(state_.scan_report.total_reclaimable_bytes) +
                                       " reclaimable across " + std::to_string(state_.scan_report.items.size()) +
                                       " items in " + format_duration_ms(state_.scan_report.duration_ms) +
                                       " (" + backend + ").";
            } else if (res) {
                state_.status = AppStatus::Error;
                state_.status_banner = "Scan stopped: " + res->error().message;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_DUPES_DONE: {
            join_worker();
            std::optional<Result<DuplicateReport>> res;
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                res = std::move(pending_dupes_);
                pending_dupes_.reset();
            }
            if (res && res->ok()) {
                state_.dup_report = std::move(res->value());
                state_.has_dup_report = true;
                state_.status = AppStatus::Ready;
                state_.status_banner = "Found " + std::to_string(state_.dup_report.groups.size()) +
                                       " duplicate groups (" + format_bytes(state_.dup_report.total_reclaimable_bytes) +
                                       " reclaimable).";
            } else if (res) {
                state_.status = AppStatus::Error;
                state_.status_banner = "Duplicate scan failed: " + res->error().message;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_CLEAN_DONE: {
            join_worker();
            std::optional<Result<CleanResult>> res;
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                res = std::move(pending_clean_);
                pending_clean_.reset();
            }
            if (res && res->ok()) {
                const auto& c = res->value();
                state_.status = AppStatus::Ready;
                state_.status_banner = (c.dry_run ? "[Dry Run] Would quarantine " : "Quarantined ") +
                                       std::to_string(c.items_processed) + " items (" +
                                       format_bytes(c.bytes_reclaimed) + "). Undo available in Quarantine Vault.";
                refresh_quarantine_list();
            } else if (res) {
                state_.status = AppStatus::Error;
                state_.status_banner = "Cleanup failed: " + res->error().message;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            cancel_flag_.store(true);
            join_worker();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void MainWindow::on_key_down(WPARAM vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (vk == VK_F5) { on_command(IDM_SCAN_NOW); return; }
    if (vk == VK_ESCAPE) { on_command(IDM_CANCEL_OP); return; }
    if (vk == VK_BACK && state_.active_view == NavView::Treemap) { on_command(IDM_TREEMAP_UP); return; }
    if (ctrl && vk == 'T') { on_command(IDM_TOGGLE_THEME); return; }
    if (ctrl && vk == VK_RETURN) { on_command(IDM_CLEAN_SELECTED); return; }
    if (ctrl && vk == 'Z') { on_command(IDM_RESTORE_LATEST); return; }
    if (ctrl && vk == '1') { on_command(IDM_NAV_OVERVIEW); return; }
    if (ctrl && vk == '2') { on_command(IDM_NAV_TREEMAP); return; }
    if (ctrl && vk == '3') { on_command(IDM_NAV_DUPLICATES); return; }
    if (ctrl && vk == '4') { on_command(IDM_NAV_QUARANTINE); return; }
    if (ctrl && vk == '5') { on_command(IDM_NAV_SETTINGS); return; }
}

void MainWindow::on_mouse_wheel(int delta) {
    state_.scroll_offset = std::max(0, state_.scroll_offset - (delta / 3));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::on_mouse_move(int x, int y) {
    if (state_.active_view == NavView::Treemap) {
        int new_hover = -1;
        for (size_t i = 0; i < state_.treemap_tiles.size(); ++i) {
            const auto& r = state_.treemap_tiles[i].rect;
            if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
                new_hover = static_cast<int>(i);
                break;
            }
        }
        if (new_hover != state_.hovered_tile_index) {
            state_.hovered_tile_index = new_hover;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void MainWindow::on_lbutton_down(int x, int y) {
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
        if (it->rect.Contains(static_cast<float>(x), static_cast<float>(y))) {
            on_command(it->command_id, it->payload_index, it->sub_index);
            return;
        }
    }
}

void MainWindow::on_command(int cmd_id, int payload_idx, int sub_idx) {
    switch (cmd_id) {
        case IDM_NAV_OVERVIEW: state_.active_view = NavView::Overview; state_.scroll_offset = 0; break;
        case IDM_NAV_TREEMAP: state_.active_view = NavView::Treemap; state_.scroll_offset = 0; break;
        case IDM_NAV_DUPLICATES: state_.active_view = NavView::Duplicates; state_.scroll_offset = 0; break;
        case IDM_NAV_QUARANTINE: state_.active_view = NavView::Quarantine; refresh_quarantine_list(); state_.scroll_offset = 0; break;
        case IDM_NAV_SETTINGS: state_.active_view = NavView::Settings; state_.scroll_offset = 0; break;
        case IDM_SCAN_NOW: start_scan_async(); return;
        case IDM_CANCEL_OP:
            cancel_flag_.store(true);
            state_.status_banner = "Cancelling active operation...";
            break;
        case IDM_CLEAN_SELECTED: start_clean_async(); return;
        case IDM_TOGGLE_THEME:
            state_.config.dark_theme = !state_.config.dark_theme;
            apply_immersive_dark_titlebar(hwnd_, state_.config.dark_theme);
            config_store_.save(state_.config);
            break;
        case IDM_TOGGLE_DRYRUN:
            state_.dry_run = !state_.dry_run;
            state_.config.dry_run_default = state_.dry_run;
            config_store_.save(state_.config);
            break;
        case IDM_BROWSE_FOLDER: browse_target_folder(); return;
        case IDM_EXPORT_HTML: export_html_report(); return;
        case IDM_RESTORE_LATEST:
            if (!state_.quarantine_batches.empty() && !state_.quarantine_batches[0].restored) {
                restore_batch_by_index(0);
            }
            return;
        case IDM_TREEMAP_UP:
            if (!state_.treemap_drill_path.empty()) state_.treemap_drill_path.pop_back();
            break;
        case kCmdRunDupes: start_duplicate_scan_async(); return;
        case kCmdCleanDupes: {
            if (!state_.has_dup_report) return;
            std::vector<std::filesystem::path> dupes;
            for (const auto& g : state_.dup_report.groups) {
                for (const auto& f : g.files) {
                    if (!f.keep) dupes.push_back(f.path);
                }
            }
            auto res = vault_.quarantine_items(dupes, "Duplicate file cleanup", state_.dry_run);
            if (res) {
                state_.status_banner = (res.value().dry_run ? "[Dry Run] Would quarantine " : "Quarantined ") +
                                       std::to_string(res.value().items_processed) + " duplicate copies (" +
                                       format_bytes(res.value().bytes_reclaimed) + "). Originals preserved.";
                refresh_quarantine_list();
            }
            break;
        }
        case kCmdToggleItem:
            if (payload_idx >= 0 && static_cast<size_t>(payload_idx) < state_.scan_report.items.size()) {
                auto& item = state_.scan_report.items[static_cast<size_t>(payload_idx)];
                item.selected = !item.selected;
            }
            break;
        case kCmdExpandBatch:
            expanded_batch_index_ = (expanded_batch_index_ == payload_idx) ? -1 : payload_idx;
            break;
        case kCmdRestoreBatch: restore_batch_by_index(payload_idx); return;
        case kCmdRestoreEntry: restore_entry_by_index(payload_idx, sub_idx); return;
        case kCmdPurgeBatch: purge_batch_by_index(payload_idx); return;
        case kCmdPurgeExpired: purge_expired_batches(); return;
        case kCmdToggleMft:
            prefer_ntfs_mft_ = !prefer_ntfs_mft_;
            state_.status_banner = prefer_ntfs_mft_
                ? "NTFS $MFT fast-scan enabled (uses raw MFT when elevated on NTFS, otherwise falls back safely)."
                : "NTFS $MFT fast-scan disabled (using standard non-elevated filesystem scan).";
            break;
        case kCmdToggleContextMenu: toggle_context_menu(); return;
        case kCmdExportRules: export_rules_pack(); return;
        case kCmdTreemapTile:
            if (payload_idx >= 0 && state_.current_treemap_node()) {
                const auto* node = state_.current_treemap_node();
                if (static_cast<size_t>(payload_idx) < node->children.size() &&
                    node->children[static_cast<size_t>(payload_idx)].is_directory) {
                    state_.treemap_drill_path.push_back(static_cast<size_t>(payload_idx));
                }
            }
            break;
        default:
            break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::start_scan_async() {
    if (state_.status == AppStatus::Scanning || state_.status == AppStatus::Cleaning) return;
    join_worker();
    cancel_flag_.store(false);
    state_.status = AppStatus::Scanning;
    state_.status_banner = "Scanning " + state_.target_path.string() + "...";
    state_.config.last_scan_path = state_.target_path.string();
    config_store_.save(state_.config);
    InvalidateRect(hwnd_, nullptr, FALSE);

    ScanOptions opts;
    opts.root_path = state_.target_path;
    opts.rules = active_rules_;
    opts.exclusions = state_.config.exclusions;
    opts.prefer_ntfs_mft = prefer_ntfs_mft_;
    opts.cancel_flag = &cancel_flag_;
    opts.progress_cb = [this](const ScanProgress& p) {
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            live_progress_text_ = "Scanning: " + std::to_string(p.files_visited) +
                                  " files | Reclaimable so far: " + format_bytes(p.reclaimable_bytes);
        }
        PostMessageW(hwnd_, WM_APP_PROGRESS, 0, 0);
    };

    worker_ = std::thread([this, opts]() {
        StorageScanner scanner;
        auto res = scanner.scan(opts);
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_scan_ = std::move(res);
        }
        PostMessageW(hwnd_, WM_APP_SCAN_DONE, 0, 0);
    });
}

void MainWindow::start_duplicate_scan_async() {
    if (state_.status == AppStatus::Scanning || state_.status == AppStatus::Cleaning) return;
    join_worker();
    cancel_flag_.store(false);
    state_.status = AppStatus::Scanning;
    state_.status_banner = "Running 3-stage SHA-256 duplicate scan on " + state_.target_path.string() + "...";
    InvalidateRect(hwnd_, nullptr, FALSE);

    DuplicateOptions opts;
    opts.root_path = state_.target_path;
    opts.min_file_size_bytes = state_.config.duplicate_min_bytes;
    opts.exclusions = state_.config.exclusions;
    opts.cancel_flag = &cancel_flag_;

    worker_ = std::thread([this, opts]() {
        DuplicateFinder finder;
        auto res = finder.find(opts);
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_dupes_ = std::move(res);
        }
        PostMessageW(hwnd_, WM_APP_DUPES_DONE, 0, 0);
    });
}

void MainWindow::start_clean_async() {
    if (!state_.has_scan_report || state_.status == AppStatus::Scanning || state_.status == AppStatus::Cleaning) return;
    std::vector<std::filesystem::path> selected;
    for (const auto& item : state_.scan_report.items) {
        if (item.selected) selected.push_back(item.path);
    }
    if (selected.empty()) {
        state_.status_banner = "No items selected to reclaim.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    join_worker();
    state_.status = AppStatus::Cleaning;
    state_.status_banner = state_.dry_run ? "Simulating dry-run cleanup..." : "Moving selected items to Quarantine Vault...";
    InvalidateRect(hwnd_, nullptr, FALSE);

    const bool dry = state_.dry_run;
    const std::string reason = "GUI cleanup of " + state_.target_path.string();

    worker_ = std::thread([this, selected = std::move(selected), reason, dry]() {
        auto res = vault_.quarantine_items(selected, reason, dry);
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_clean_ = std::move(res);
        }
        PostMessageW(hwnd_, WM_APP_CLEAN_DONE, 0, 0);
    });
}

void MainWindow::restore_batch_by_index(int index) {
    if (index < 0 || static_cast<size_t>(index) >= state_.quarantine_batches.size()) return;
    const auto& batch = state_.quarantine_batches[static_cast<size_t>(index)];
    auto res = vault_.restore_batch(batch.batch_id);
    if (res) {
        state_.status_banner = "Restored " + std::to_string(res.value().items_restored) +
                               " items (" + format_bytes(res.value().bytes_restored) + ") from " + batch.batch_id + ".";
    } else {
        state_.status_banner = "Restore failed: " + res.error().message;
    }
    refresh_quarantine_list();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::restore_entry_by_index(int batch_index, int entry_index) {
    if (batch_index < 0 || static_cast<size_t>(batch_index) >= state_.quarantine_batches.size()) return;
    const auto& batch = state_.quarantine_batches[static_cast<size_t>(batch_index)];
    auto res = vault_.restore_entry(batch.batch_id, static_cast<size_t>(entry_index));
    if (res) {
        state_.status_banner = "Restored single item (" + format_bytes(res.value().bytes_restored) + ") from " + batch.batch_id + ".";
    } else {
        state_.status_banner = "Single-item restore failed: " + res.error().message;
    }
    refresh_quarantine_list();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::purge_batch_by_index(int index) {
    if (index < 0 || static_cast<size_t>(index) >= state_.quarantine_batches.size()) return;
    const auto& batch = state_.quarantine_batches[static_cast<size_t>(index)];
    auto res = vault_.purge_batch(batch.batch_id);
    if (res) {
        state_.status_banner = "Permanently purged " + batch.batch_id + " (" + format_bytes(res.value()) + " freed).";
    } else {
        state_.status_banner = "Purge failed: " + res.error().message;
    }
    expanded_batch_index_ = -1;
    refresh_quarantine_list();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::purge_expired_batches() {
    auto res = vault_.purge_expired_batches(state_.config.quarantine_retention_days, state_.dry_run);
    if (res) {
        state_.status_banner = (res.value().dry_run ? "[Dry Run] Would purge " : "Purged ") +
                               std::to_string(res.value().batches_purged) + " expired batches (" +
                               format_bytes(res.value().bytes_freed) + ").";
    } else {
        state_.status_banner = "Auto-expiry failed: " + res.error().message;
    }
    refresh_quarantine_list();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::toggle_context_menu() {
    if (is_explorer_context_menu_installed()) {
        auto r = uninstall_explorer_context_menu();
        state_.status_banner = r ? "Removed 'Analyze with Reclaim' from Windows Explorer context menu." : r.error().message;
    } else {
        wchar_t exe_buf[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
        auto r = install_explorer_context_menu(std::filesystem::path(exe_buf));
        state_.status_banner = r ? "Added 'Analyze with Reclaim' to Windows Explorer context menu (per-user)." : r.error().message;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::export_rules_pack() {
    const auto out_path = ConfigStore::default_data_dir() / "rules.json";
    auto w = write_text_file_atomic(out_path, export_rules_json(active_rules_));
    state_.status_banner = w ? ("Exported editable rules.json to " + out_path.string()) : ("Export failed: " + w.error().message);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::export_html_report() {
    if (!state_.has_scan_report) {
        state_.status_banner = "Run a scan first before exporting a report.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto out_path = ConfigStore::default_data_dir() / "reclaim_audit_report.html";
    auto w = write_text_file_atomic(out_path, export_scan_html(state_.scan_report));
    state_.status_banner = w ? ("Exported audit report to " + out_path.string()) : ("Export failed: " + w.error().message);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::browse_target_folder() {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"Select folder or drive to analyze with Reclaim";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t buf[MAX_PATH]{};
        if (SHGetPathFromIDListW(pidl, buf)) {
            state_.target_path = std::filesystem::path(wide_to_utf8(buf));
            state_.status_banner = "Target set to " + state_.target_path.string() + ". Press Scan (F5).";
        }
        CoTaskMemFree(pidl);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::refresh_quarantine_list() {
    auto batches = vault_.list_batches();
    if (batches) state_.quarantine_batches = std::move(batches.value());
}

void MainWindow::on_paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = std::max(1, static_cast<int>(rc.right - rc.left));
    const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));

    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    {
        Gdiplus::Graphics g(mem_dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        const ThemePalette p = make_palette(state_.config.dark_theme);
        hit_regions_.clear();

        Gdiplus::SolidBrush bg(p.canvas);
        g.FillRectangle(&bg, 0, 0, w, h);

        draw_nav_rail(g, p, static_cast<float>(h));
        draw_top_bar(g, p, static_cast<float>(w));

        const Gdiplus::RectF content(220.0f, 64.0f, static_cast<float>(w) - 220.0f, static_cast<float>(h) - 64.0f);
        switch (state_.active_view) {
            case NavView::Overview: draw_overview(g, p, content); break;
            case NavView::Treemap: draw_treemap(g, p, content); break;
            case NavView::Duplicates: draw_duplicates(g, p, content); break;
            case NavView::Quarantine: draw_quarantine(g, p, content); break;
            case NavView::Settings: draw_settings(g, p, content); break;
        }
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    EndPaint(hwnd_, &ps);
}

void MainWindow::draw_nav_rail(Gdiplus::Graphics& g, const ThemePalette& p, float h) {
    Gdiplus::SolidBrush rail_bg(p.rail);
    g.FillRectangle(&rail_bg, 0.0f, 0.0f, 220.0f, h);

    Gdiplus::Pen border_pen(p.border, 1.0f);
    g.DrawLine(&border_pen, 220.0f, 0.0f, 220.0f, h);

    Gdiplus::SolidBrush amber(p.accent);
    fill_rounded_rect(g, amber, nullptr, Gdiplus::RectF(20.0f, 18.0f, 12.0f, 24.0f), 3.0f);
    Gdiplus::SolidBrush cobalt(p.treemap_colors[1]);
    fill_rounded_rect(g, cobalt, nullptr, Gdiplus::RectF(35.0f, 18.0f, 10.0f, 11.0f), 2.5f);
    Gdiplus::SolidBrush teal(p.treemap_colors[2]);
    fill_rounded_rect(g, teal, nullptr, Gdiplus::RectF(35.0f, 32.0f, 10.0f, 10.0f), 2.5f);

    Gdiplus::Font brand_font(L"Segoe UI", 14.0f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush txt(p.text_primary);
    g.DrawString(L"Reclaim", -1, &brand_font, Gdiplus::PointF(54.0f, 18.0f), &txt);

    struct NavItem { const wchar_t* label; NavView view; int cmd; };
    const NavItem items[] = {
        {L"Overview & Scan", NavView::Overview, IDM_NAV_OVERVIEW},
        {L"Treemap Explorer", NavView::Treemap, IDM_NAV_TREEMAP},
        {L"Duplicate Finder", NavView::Duplicates, IDM_NAV_DUPLICATES},
        {L"Quarantine Vault", NavView::Quarantine, IDM_NAV_QUARANTINE},
        {L"Settings & Rules", NavView::Settings, IDM_NAV_SETTINGS}
    };

    Gdiplus::Font nav_font(L"Segoe UI", 10.0f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush muted(p.text_muted);

    float y = 76.0f;
    for (const auto& item : items) {
        const bool active = (state_.active_view == item.view);
        Gdiplus::RectF r(12.0f, y, 196.0f, 38.0f);
        if (active) {
            Gdiplus::SolidBrush active_bg(p.surface);
            fill_rounded_rect(g, active_bg, &border_pen, r, 6.0f);
            fill_rounded_rect(g, amber, nullptr, Gdiplus::RectF(12.0f, y + 8.0f, 3.0f, 22.0f), 1.5f);
        }
        g.DrawString(item.label, -1, &nav_font, Gdiplus::PointF(26.0f, y + 10.0f), active ? &txt : &muted);
        hit_regions_.push_back(HitRegion{r, item.cmd, -1, -1});
        y += 44.0f;
    }

    ULARGE_INTEGER free_avail{}, total_bytes{}, total_free{};
    if (GetDiskFreeSpaceExW(L"C:\\", &free_avail, &total_bytes, &total_free) && total_bytes.QuadPart > 0) {
        const double used_frac = 1.0 - (static_cast<double>(free_avail.QuadPart) / static_cast<double>(total_bytes.QuadPart));
        Gdiplus::RectF card(16.0f, h - 96.0f, 188.0f, 76.0f);
        Gdiplus::SolidBrush card_bg(p.surface);
        fill_rounded_rect(g, card_bg, &border_pen, card, 8.0f);

        Gdiplus::Font small_f(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
        g.DrawString(L"SYSTEM DRIVE (C:)", -1, &small_f, Gdiplus::PointF(28.0f, h - 86.0f), &muted);

        Gdiplus::SolidBrush track(p.border);
        fill_rounded_rect(g, track, nullptr, Gdiplus::RectF(28.0f, h - 62.0f, 164.0f, 8.0f), 4.0f);
        fill_rounded_rect(g, amber, nullptr, Gdiplus::RectF(28.0f, h - 62.0f, static_cast<float>(164.0 * used_frac), 8.0f), 4.0f);

        const std::wstring free_lbl = utf8_to_wide(format_bytes(free_avail.QuadPart) + " free of " + format_bytes(total_bytes.QuadPart));
        Gdiplus::Font mono_f(L"Consolas", 8.5f, Gdiplus::FontStyleRegular);
        g.DrawString(free_lbl.c_str(), -1, &mono_f, Gdiplus::PointF(28.0f, h - 46.0f), &txt);
    }
}

void MainWindow::draw_top_bar(Gdiplus::Graphics& g, const ThemePalette& p, float w) {
    Gdiplus::SolidBrush bar_bg(p.surface);
    g.FillRectangle(&bar_bg, 221.0f, 0.0f, w - 221.0f, 64.0f);
    Gdiplus::Pen border_pen(p.border, 1.0f);
    g.DrawLine(&border_pen, 221.0f, 64.0f, w, 64.0f);

    Gdiplus::Font path_font(L"Consolas", 9.5f, Gdiplus::FontStyleRegular);
    Gdiplus::Font btn_font(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush txt(p.text_primary);

    const float path_w = std::max(180.0f, w - 640.0f);
    Gdiplus::RectF path_box(240.0f, 15.0f, path_w, 34.0f);
    Gdiplus::SolidBrush canvas_bg(p.canvas);
    fill_rounded_rect(g, canvas_bg, &border_pen, path_box, 6.0f);
    const std::wstring path_wstr = utf8_to_wide(state_.target_path.string());
    g.DrawString(path_wstr.c_str(), -1, &path_font, Gdiplus::PointF(252.0f, 24.0f), &txt);
    hit_regions_.push_back(HitRegion{path_box, IDM_BROWSE_FOLDER, -1, -1});

    Gdiplus::RectF dry_btn(w - 384.0f, 15.0f, 108.0f, 34.0f);
    Gdiplus::SolidBrush dry_bg(state_.dry_run ? p.review_bg : p.canvas);
    fill_rounded_rect(g, dry_bg, &border_pen, dry_btn, 6.0f);
    Gdiplus::SolidBrush dry_txt(state_.dry_run ? p.review_fg : p.text_muted);
    g.DrawString(state_.dry_run ? L"Dry Run: ON" : L"Dry Run: OFF", -1, &btn_font, Gdiplus::PointF(w - 372.0f, 23.0f), &dry_txt);
    hit_regions_.push_back(HitRegion{dry_btn, IDM_TOGGLE_DRYRUN, -1, -1});

    const bool busy = (state_.status == AppStatus::Scanning || state_.status == AppStatus::Cleaning);
    Gdiplus::RectF scan_btn(w - 266.0f, 15.0f, 106.0f, 34.0f);
    Gdiplus::SolidBrush scan_bg(busy ? p.caution_bg : p.surface_hover);
    fill_rounded_rect(g, scan_bg, &border_pen, scan_btn, 6.0f);
    g.DrawString(busy ? L"Cancel (Esc)" : L"Scan (F5)", -1, &btn_font, Gdiplus::PointF(w - 246.0f, 23.0f), &txt);
    hit_regions_.push_back(HitRegion{scan_btn, busy ? IDM_CANCEL_OP : IDM_SCAN_NOW, -1, -1});

    Gdiplus::RectF clean_btn(w - 150.0f, 15.0f, 132.0f, 34.0f);
    Gdiplus::SolidBrush accent_bg(p.accent);
    Gdiplus::SolidBrush accent_fg(p.accent_text);
    fill_rounded_rect(g, accent_bg, nullptr, clean_btn, 6.0f);
    g.DrawString(L"Reclaim Now", -1, &btn_font, Gdiplus::PointF(w - 124.0f, 23.0f), &accent_fg);
    hit_regions_.push_back(HitRegion{clean_btn, IDM_CLEAN_SELECTED, -1, -1});
}

void MainWindow::draw_overview(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area) {
    Gdiplus::Pen border_pen(p.border, 1.0f);
    Gdiplus::SolidBrush card_bg(p.surface);
    Gdiplus::SolidBrush txt(p.text_primary);
    Gdiplus::SolidBrush muted(p.text_muted);

    Gdiplus::Font label_f(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
    Gdiplus::Font val_f(L"Consolas", 16.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font body_f(L"Segoe UI", 9.5f, Gdiplus::FontStyleRegular);

    Gdiplus::RectF banner(area.X + 20.0f, area.Y + 16.0f, area.Width - 40.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, banner, 6.0f);
    const std::wstring banner_w = utf8_to_wide(state_.status_banner);
    g.DrawString(banner_w.c_str(), -1, &body_f, Gdiplus::PointF(banner.X + 14.0f, banner.Y + 9.0f), &txt);

    const float kpi_y = area.Y + 66.0f;
    const float kpi_w = (area.Width - 40.0f - 36.0f) / 4.0f;
    struct Kpi { const wchar_t* title; std::string value; };
    const Kpi kpis[4] = {
        {L"RECLAIMABLE SELECTED", state_.has_scan_report ? format_bytes(state_.selected_reclaim_bytes()) : "0 B"},
        {L"DETECTED ITEMS", state_.has_scan_report ? std::to_string(state_.scan_report.items.size()) : "0"},
        {L"SCAN DURATION", state_.has_scan_report ? format_duration_ms(state_.scan_report.duration_ms) : "--"},
        {L"QUARANTINE VAULT", format_bytes(vault_.total_quarantined_bytes())}
    };
    for (int i = 0; i < 4; ++i) {
        Gdiplus::RectF r(area.X + 20.0f + i * (kpi_w + 12.0f), kpi_y, kpi_w, 78.0f);
        fill_rounded_rect(g, card_bg, &border_pen, r, 8.0f);
        g.DrawString(kpis[i].title, -1, &label_f, Gdiplus::PointF(r.X + 14.0f, r.Y + 12.0f), &muted);
        const std::wstring vw = utf8_to_wide(kpis[i].value);
        g.DrawString(vw.c_str(), -1, &val_f, Gdiplus::PointF(r.X + 14.0f, r.Y + 34.0f), &txt);
    }

    const float list_y = kpi_y + 94.0f;
    Gdiplus::RectF table(area.X + 20.0f, list_y, area.Width - 40.0f, area.Height - (list_y - area.Y) - 20.0f);
    fill_rounded_rect(g, card_bg, &border_pen, table, 8.0f);

    if (!state_.has_scan_report) {
        g.DrawString(L"No scan results yet. Click 'Scan (F5)' to inspect caches, AI models, build artifacts, and temp files.",
                     -1, &body_f, Gdiplus::PointF(table.X + 24.0f, table.Y + 40.0f), &muted);
        return;
    }

    if (state_.scan_report.items.empty()) {
        g.DrawString(L"Clean bill of health: no reclaimable cache or bloat items found in this folder.",
                     -1, &body_f, Gdiplus::PointF(table.X + 24.0f, table.Y + 40.0f), &txt);
        return;
    }

    float row_y = table.Y + 14.0f - static_cast<float>(state_.scroll_offset);
    for (size_t i = 0; i < state_.scan_report.items.size(); ++i) {
        if (row_y + 34.0f < table.Y + 8.0f) { row_y += 38.0f; continue; }
        if (row_y > table.Y + table.Height - 36.0f) break;

        const auto& item = state_.scan_report.items[i];
        Gdiplus::RectF row_r(table.X + 10.0f, row_y, table.Width - 20.0f, 34.0f);
        hit_regions_.push_back(HitRegion{row_r, kCmdToggleItem, static_cast<int>(i), -1});

        Gdiplus::RectF chk(row_r.X + 8.0f, row_r.Y + 9.0f, 16.0f, 16.0f);
        Gdiplus::SolidBrush chk_bg(item.selected ? p.accent : p.canvas);
        fill_rounded_rect(g, chk_bg, &border_pen, chk, 3.0f);

        Gdiplus::Color pill_bg = (item.safety == SafetyLevel::Safe) ? p.safe_bg : (item.safety == SafetyLevel::Review ? p.review_bg : p.caution_bg);
        Gdiplus::Color pill_fg = (item.safety == SafetyLevel::Safe) ? p.safe_fg : (item.safety == SafetyLevel::Review ? p.review_fg : p.caution_fg);
        Gdiplus::SolidBrush pb(pill_bg), pf(pill_fg);
        fill_rounded_rect(g, pb, nullptr, Gdiplus::RectF(row_r.X + 34.0f, row_r.Y + 7.0f, 62.0f, 20.0f), 4.0f);
        const std::wstring sw = utf8_to_wide(safety_to_string(item.safety));
        g.DrawString(sw.c_str(), -1, &label_f, Gdiplus::PointF(row_r.X + 44.0f, row_r.Y + 10.0f), &pf);

        const std::wstring pw = utf8_to_wide(item.path.string());
        g.DrawString(pw.c_str(), -1, &body_f, Gdiplus::PointF(row_r.X + 108.0f, row_r.Y + 8.0f), &txt);

        const std::wstring szw = utf8_to_wide(format_bytes(item.size_bytes));
        Gdiplus::Font mono_f(L"Consolas", 9.5f, Gdiplus::FontStyleBold);
        g.DrawString(szw.c_str(), -1, &mono_f, Gdiplus::PointF(row_r.X + row_r.Width - 104.0f, row_r.Y + 8.0f), &txt);

        row_y += 38.0f;
    }
}

void MainWindow::draw_treemap(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area) {
    Gdiplus::Pen border_pen(p.border, 1.0f);
    Gdiplus::SolidBrush txt(p.text_primary);
    Gdiplus::SolidBrush muted(p.text_muted);
    Gdiplus::Font body_f(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold);

    const FileNode* node = state_.current_treemap_node();
    if (!node) {
        g.DrawString(L"Run a scan (F5) to generate an interactive squarified storage treemap.",
                     -1, &body_f, Gdiplus::PointF(area.X + 24.0f, area.Y + 32.0f), &muted);
        return;
    }

    Gdiplus::RectF back_btn(area.X + 20.0f, area.Y + 14.0f, 96.0f, 30.0f);
    Gdiplus::SolidBrush card_bg(p.surface);
    fill_rounded_rect(g, card_bg, &border_pen, back_btn, 6.0f);
    g.DrawString(L"Up Level", -1, &body_f, Gdiplus::PointF(back_btn.X + 18.0f, back_btn.Y + 6.0f), &txt);
    hit_regions_.push_back(HitRegion{back_btn, IDM_TREEMAP_UP, -1, -1});

    const std::wstring crumb = utf8_to_wide(node->path.string() + " (" + format_bytes(node->size_bytes) + ")");
    g.DrawString(crumb.c_str(), -1, &body_f, Gdiplus::PointF(area.X + 130.0f, area.Y + 20.0f), &txt);

    const RectF map_bounds{area.X + 20.0, area.Y + 56.0, area.Width - 40.0, area.Height - 104.0};
    state_.treemap_tiles = compute_squarified_treemap(*node, map_bounds, 48);

    Gdiplus::Font tile_f(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush dark_ink(Gdiplus::Color(255, 13, 17, 23));

    for (size_t i = 0; i < state_.treemap_tiles.size(); ++i) {
        const auto& t = state_.treemap_tiles[i];
        Gdiplus::RectF r(static_cast<float>(t.rect.x) + 1.5f, static_cast<float>(t.rect.y) + 1.5f,
                         std::max(1.0f, static_cast<float>(t.rect.width) - 3.0f),
                         std::max(1.0f, static_cast<float>(t.rect.height) - 3.0f));
        Gdiplus::Color c = p.treemap_colors[static_cast<size_t>(t.color_index % 6)];
        Gdiplus::SolidBrush fill(c);
        Gdiplus::Pen tile_pen(static_cast<int>(i) == state_.hovered_tile_index ? p.text_primary : p.canvas,
                              static_cast<int>(i) == state_.hovered_tile_index ? 2.0f : 1.0f);
        g.FillRectangle(&fill, r);
        g.DrawRectangle(&tile_pen, r);

        if (r.Width > 76.0f && r.Height > 32.0f) {
            const std::wstring lbl = utf8_to_wide(t.name + "\n" + format_bytes(t.size_bytes));
            g.DrawString(lbl.c_str(), -1, &tile_f, r, nullptr, &dark_ink);
        }
        hit_regions_.push_back(HitRegion{r, kCmdTreemapTile, static_cast<int>(i), -1});
    }

    if (state_.hovered_tile_index >= 0 &&
        static_cast<size_t>(state_.hovered_tile_index) < state_.treemap_tiles.size()) {
        const auto& ht = state_.treemap_tiles[static_cast<size_t>(state_.hovered_tile_index)];
        std::ostringstream oss;
        oss << ht.path.string() << "  |  " << format_bytes(ht.size_bytes)
            << " (" << std::fixed << std::setprecision(1) << (ht.fraction_of_parent * 100.0) << "%)";
        const std::wstring hw = utf8_to_wide(oss.str());
        g.DrawString(hw.c_str(), -1, &body_f, Gdiplus::PointF(area.X + 20.0f, area.Y + area.Height - 36.0f), &txt);
    }
}

void MainWindow::draw_duplicates(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area) {
    Gdiplus::Pen border_pen(p.border, 1.0f);
    Gdiplus::SolidBrush card_bg(p.surface);
    Gdiplus::SolidBrush txt(p.text_primary);
    Gdiplus::SolidBrush muted(p.text_muted);
    Gdiplus::SolidBrush accent_bg(p.accent);
    Gdiplus::SolidBrush accent_fg(p.accent_text);
    Gdiplus::Font btn_f(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold);
    Gdiplus::Font body_f(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);

    Gdiplus::RectF run_btn(area.X + 20.0f, area.Y + 16.0f, 180.0f, 34.0f);
    fill_rounded_rect(g, card_bg, &border_pen, run_btn, 6.0f);
    g.DrawString(L"Find Duplicates (SHA-256)", -1, &btn_f, Gdiplus::PointF(run_btn.X + 14.0f, run_btn.Y + 8.0f), &txt);
    hit_regions_.push_back(HitRegion{run_btn, kCmdRunDupes, -1, -1});

    Gdiplus::RectF clean_btn(area.X + 214.0f, area.Y + 16.0f, 196.0f, 34.0f);
    fill_rounded_rect(g, accent_bg, nullptr, clean_btn, 6.0f);
    g.DrawString(L"Quarantine Extra Copies", -1, &btn_f, Gdiplus::PointF(clean_btn.X + 18.0f, clean_btn.Y + 8.0f), &accent_fg);
    hit_regions_.push_back(HitRegion{clean_btn, kCmdCleanDupes, -1, -1});

    if (!state_.has_dup_report) {
        g.DrawString(L"3-stage verification (size -> 4KB partial hash -> full SHA-256). Always protects at least one original copy.",
                     -1, &body_f, Gdiplus::PointF(area.X + 20.0f, area.Y + 72.0f), &muted);
        return;
    }

    float y = area.Y + 66.0f - static_cast<float>(state_.scroll_offset);
    for (const auto& grp : state_.dup_report.groups) {
        const float card_h = 34.0f + static_cast<float>(grp.files.size()) * 22.0f;
        if (y > area.Y + area.Height - 20.0f) break;
        if (y + card_h > area.Y + 56.0f) {
            Gdiplus::RectF cr(area.X + 20.0f, y, area.Width - 40.0f, card_h);
            fill_rounded_rect(g, card_bg, &border_pen, cr, 8.0f);
            const std::wstring hdr = utf8_to_wide("SHA-256: " + grp.sha256_hex.substr(0, 16) +
                                                  "  |  " + format_bytes(grp.file_size_bytes) +
                                                  " each  |  Reclaimable: " + format_bytes(grp.reclaimable_bytes()));
            g.DrawString(hdr.c_str(), -1, &btn_f, Gdiplus::PointF(cr.X + 14.0f, cr.Y + 8.0f), &txt);

            float fy = cr.Y + 30.0f;
            for (const auto& f : grp.files) {
                const std::wstring line = utf8_to_wide(std::string(f.keep ? "[KEEP] " : "[DUPE] ") + f.path.string());
                Gdiplus::SolidBrush line_b(f.keep ? p.safe_fg : p.text_muted);
                g.DrawString(line.c_str(), -1, &body_f, Gdiplus::PointF(cr.X + 18.0f, fy), &line_b);
                fy += 22.0f;
            }
        }
        y += card_h + 10.0f;
    }
}

void MainWindow::draw_quarantine(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area) {
    Gdiplus::Pen border_pen(p.border, 1.0f);
    Gdiplus::SolidBrush card_bg(p.surface);
    Gdiplus::SolidBrush txt(p.text_primary);
    Gdiplus::SolidBrush muted(p.text_muted);
    Gdiplus::Font btn_f(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font body_f(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);

    // Top action strip: Purge Expired Batches
    Gdiplus::RectF exp_btn(area.X + 20.0f, area.Y + 14.0f, 230.0f, 32.0f);
    fill_rounded_rect(g, card_bg, &border_pen, exp_btn, 6.0f);
    const std::wstring exp_lbl = utf8_to_wide("Purge Batches Older Than " + std::to_string(state_.config.quarantine_retention_days) + "d");
    g.DrawString(exp_lbl.c_str(), -1, &btn_f, Gdiplus::PointF(exp_btn.X + 14.0f, exp_btn.Y + 8.0f), &txt);
    hit_regions_.push_back(HitRegion{exp_btn, kCmdPurgeExpired, -1, -1});

    if (state_.quarantine_batches.empty()) {
        g.DrawString(L"Quarantine Vault is empty. Cleaned items are stored here safely for one-click or per-file undo.",
                     -1, &body_f, Gdiplus::PointF(area.X + 24.0f, area.Y + 66.0f), &muted);
        return;
    }

    float y = area.Y + 60.0f - static_cast<float>(state_.scroll_offset);
    for (size_t i = 0; i < state_.quarantine_batches.size(); ++i) {
        const auto& b = state_.quarantine_batches[i];
        const bool expanded = (expanded_batch_index_ == static_cast<int>(i));
        const float card_h = expanded ? (70.0f + static_cast<float>(std::min<size_t>(b.entries.size(), 10)) * 28.0f) : 64.0f;

        Gdiplus::RectF cr(area.X + 20.0f, y, area.Width - 40.0f, card_h);
        fill_rounded_rect(g, card_bg, &border_pen, cr, 8.0f);

        Gdiplus::RectF header_hit(cr.X, cr.Y, cr.Width - 210.0f, 64.0f);
        hit_regions_.push_back(HitRegion{header_hit, kCmdExpandBatch, static_cast<int>(i), -1});

        const std::wstring title = utf8_to_wide(std::string(expanded ? "[-] " : "[+] ") + b.batch_id +
                                                "  (" + format_bytes(b.total_bytes) + ", " +
                                                std::to_string(b.entries.size()) + " items)");
        g.DrawString(title.c_str(), -1, &btn_f, Gdiplus::PointF(cr.X + 14.0f, cr.Y + 10.0f), &txt);

        const std::wstring sub = utf8_to_wide(b.created_iso8601 + "  |  " + b.reason +
                                              (b.restored ? "  [RESTORED]" : "  [ACTIVE]"));
        g.DrawString(sub.c_str(), -1, &body_f, Gdiplus::PointF(cr.X + 14.0f, cr.Y + 34.0f), &muted);

        if (!b.restored) {
            Gdiplus::RectF restore_btn(cr.X + cr.Width - 196.0f, cr.Y + 16.0f, 88.0f, 32.0f);
            Gdiplus::SolidBrush safe_b(p.safe_bg), safe_f(p.safe_fg);
            fill_rounded_rect(g, safe_b, &border_pen, restore_btn, 6.0f);
            g.DrawString(L"Restore All", -1, &btn_f, Gdiplus::PointF(restore_btn.X + 12.0f, restore_btn.Y + 8.0f), &safe_f);
            hit_regions_.push_back(HitRegion{restore_btn, kCmdRestoreBatch, static_cast<int>(i), -1});

            Gdiplus::RectF purge_btn(cr.X + cr.Width - 100.0f, cr.Y + 16.0f, 84.0f, 32.0f);
            Gdiplus::SolidBrush caut_b(p.caution_bg), caut_f(p.caution_fg);
            fill_rounded_rect(g, caut_b, &border_pen, purge_btn, 6.0f);
            g.DrawString(L"Purge", -1, &btn_f, Gdiplus::PointF(purge_btn.X + 22.0f, purge_btn.Y + 8.0f), &caut_f);
            hit_regions_.push_back(HitRegion{purge_btn, kCmdPurgeBatch, static_cast<int>(i), -1});
        }

        if (expanded) {
            float ey = cr.Y + 68.0f;
            const size_t lim = std::min<size_t>(b.entries.size(), 10);
            for (size_t j = 0; j < lim; ++j) {
                const auto& e = b.entries[j];
                const std::wstring eline = utf8_to_wide(format_bytes(e.size_bytes) + "  " + e.original_path);
                g.DrawString(eline.c_str(), -1, &body_f, Gdiplus::PointF(cr.X + 24.0f, ey + 4.0f), e.restored ? &muted : &txt);
                if (!e.restored) {
                    Gdiplus::RectF one_btn(cr.X + cr.Width - 116.0f, ey, 100.0f, 24.0f);
                    Gdiplus::SolidBrush safe_b(p.safe_bg), safe_f(p.safe_fg);
                    fill_rounded_rect(g, safe_b, &border_pen, one_btn, 4.0f);
                    g.DrawString(L"Restore File", -1, &btn_f, Gdiplus::PointF(one_btn.X + 14.0f, one_btn.Y + 4.0f), &safe_f);
                    hit_regions_.push_back(HitRegion{one_btn, kCmdRestoreEntry, static_cast<int>(i), static_cast<int>(j)});
                }
                ey += 28.0f;
            }
        }
        y += card_h + 10.0f;
    }
}

void MainWindow::draw_settings(Gdiplus::Graphics& g, const ThemePalette& p, Gdiplus::RectF area) {
    Gdiplus::Pen border_pen(p.border, 1.0f);
    Gdiplus::SolidBrush card_bg(p.surface);
    Gdiplus::SolidBrush txt(p.text_primary);
    Gdiplus::SolidBrush muted(p.text_muted);
    Gdiplus::Font hdr_f(L"Segoe UI", 11.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font body_f(L"Segoe UI", 9.5f, Gdiplus::FontStyleRegular);

    Gdiplus::RectF card(area.X + 20.0f, area.Y + 18.0f, area.Width - 40.0f, 340.0f);
    fill_rounded_rect(g, card_bg, &border_pen, card, 8.0f);

    g.DrawString(L"Preferences, Rule Packs & Windows Integration (v1.1)", -1, &hdr_f, Gdiplus::PointF(card.X + 20.0f, card.Y + 18.0f), &txt);

    // Row 1 buttons
    Gdiplus::RectF theme_btn(card.X + 20.0f, card.Y + 56.0f, 220.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, theme_btn, 6.0f);
    g.DrawString(state_.config.dark_theme ? L"Theme: Carbon Dark (Ctrl+T)" : L"Theme: Editorial Light (Ctrl+T)",
                 -1, &body_f, Gdiplus::PointF(theme_btn.X + 14.0f, theme_btn.Y + 9.0f), &txt);
    hit_regions_.push_back(HitRegion{theme_btn, IDM_TOGGLE_THEME, -1, -1});

    Gdiplus::RectF mft_btn(card.X + 254.0f, card.Y + 56.0f, 250.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, mft_btn, 6.0f);
    g.DrawString(prefer_ntfs_mft_ ? L"NTFS $MFT Fast Scan: ON (Admin)" : L"NTFS $MFT Fast Scan: OFF",
                 -1, &body_f, Gdiplus::PointF(mft_btn.X + 14.0f, mft_btn.Y + 9.0f), &txt);
    hit_regions_.push_back(HitRegion{mft_btn, kCmdToggleMft, -1, -1});

    // Row 2 buttons
    Gdiplus::RectF ctx_btn(card.X + 20.0f, card.Y + 104.0f, 260.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, ctx_btn, 6.0f);
    g.DrawString(is_explorer_context_menu_installed() ? L"Explorer Context Menu: Installed" : L"Add Explorer Context Menu",
                 -1, &body_f, Gdiplus::PointF(ctx_btn.X + 14.0f, ctx_btn.Y + 9.0f), &txt);
    hit_regions_.push_back(HitRegion{ctx_btn, kCmdToggleContextMenu, -1, -1});

    Gdiplus::RectF rules_btn(card.X + 294.0f, card.Y + 104.0f, 210.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, rules_btn, 6.0f);
    g.DrawString(L"Export Editable rules.json", -1, &body_f, Gdiplus::PointF(rules_btn.X + 16.0f, rules_btn.Y + 9.0f), &txt);
    hit_regions_.push_back(HitRegion{rules_btn, kCmdExportRules, -1, -1});

    Gdiplus::RectF export_btn(card.X + 518.0f, card.Y + 104.0f, 210.0f, 36.0f);
    fill_rounded_rect(g, card_bg, &border_pen, export_btn, 6.0f);
    g.DrawString(L"Export HTML Audit Report", -1, &body_f, Gdiplus::PointF(export_btn.X + 16.0f, export_btn.Y + 9.0f), &txt);
    hit_regions_.push_back(HitRegion{export_btn, IDM_EXPORT_HTML, -1, -1});

    const wchar_t* invariants =
        L"Hard Safety Invariants (Always Enforced):\n"
        L"  1. Zero Telemetry & Zero Network Calls: Reclaim never opens a network socket.\n"
        L"  2. No Registry Cleaners: Only transparent filesystem caches, AI/dev artifacts, and verified duplicates.\n"
        L"  3. Protected OS Paths: C:\\Windows\\System32, WinSxS, Boot, EFI, and drive roots are hard-blocked.\n"
        L"  4. Granular Quarantine Vault: Restore an entire batch or a single file, with retention auto-expiry.\n"
        L"  5. Duplicate Protection: Every SHA-256 duplicate group preserves at least one original file.";
    g.DrawString(invariants, -1, &body_f, Gdiplus::PointF(card.X + 20.0f, card.Y + 164.0f), &muted);
}

}  // namespace reclaim::ui

#endif  // _WIN32
