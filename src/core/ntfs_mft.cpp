#include "reclaim/ntfs_mft.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace reclaim {

namespace {

FileNode build_subtree(uint64_t frn,
                       const std::filesystem::path& current_path,
                       size_t depth,
                       size_t max_depth,
                       const std::unordered_map<uint64_t, const MftRecord*>& by_frn,
                       const std::unordered_map<uint64_t, std::vector<uint64_t>>& children_map,
                       std::unordered_set<uint64_t>& visited) {
    FileNode node;
    node.path = current_path;
    node.is_directory = true;

    auto rec_it = by_frn.find(frn);
    if (rec_it != by_frn.end()) {
        node.name = rec_it->second->name;
        node.is_directory = rec_it->second->is_directory;
        node.size_bytes = rec_it->second->size_bytes;
    } else {
        node.name = current_path.filename().empty() ? current_path.string() : current_path.filename().string();
    }

    // Cycle protection against malformed or looped MFT parent links
    if (!visited.insert(frn).second) {
        return node;
    }

    auto ch_it = children_map.find(frn);
    if (ch_it != children_map.end()) {
        uint64_t total = node.size_bytes;
        for (uint64_t child_frn : ch_it->second) {
            auto c_rec = by_frn.find(child_frn);
            if (c_rec == by_frn.end()) continue;
            const auto child_path = current_path / c_rec->second->name;
            FileNode child_node = build_subtree(
                child_frn, child_path, depth + 1, max_depth, by_frn, children_map, visited);
            total += child_node.size_bytes;
            if (depth < max_depth && child_node.size_bytes > 0) {
                node.children.push_back(std::move(child_node));
            }
        }
        node.size_bytes = total;
        std::sort(node.children.begin(), node.children.end(),
                  [](const FileNode& a, const FileNode& b) { return a.size_bytes > b.size_bytes; });
    }

    return node;
}

#ifdef _WIN32
bool is_process_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &len) != 0;
    CloseHandle(token);
    return ok && elev.TokenIsElevated != 0;
}

std::string wide_to_utf8_len(const wchar_t* data, size_t wchar_count) {
    if (!data || wchar_count == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, data, static_cast<int>(wchar_count), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, data, static_cast<int>(wchar_count), out.data(), n, nullptr, nullptr);
    return out;
}
#endif

}  // namespace

FileNode reconstruct_mft_tree(const std::vector<MftRecord>& records,
                              uint64_t root_frn,
                              const std::filesystem::path& volume_root,
                              size_t max_tree_depth) {
    std::unordered_map<uint64_t, const MftRecord*> by_frn;
    by_frn.reserve(records.size());
    std::unordered_map<uint64_t, std::vector<uint64_t>> children_map;

    for (const auto& rec : records) {
        by_frn[rec.frn] = &rec;
        if (rec.frn != root_frn) {
            children_map[rec.parent_frn].push_back(rec.frn);
        }
    }

    std::unordered_set<uint64_t> visited;
    visited.reserve(records.size() + 1);
    return build_subtree(root_frn, volume_root, 0, max_tree_depth, by_frn, children_map, visited);
}

bool can_use_ntfs_mft(const std::filesystem::path& root_path) {
#ifdef _WIN32
    if (!is_process_elevated()) return false;
    const std::wstring s = root_path.wstring();
    if (s.size() < 2 || s[1] != L':') return false;
    const std::wstring drive_root = s.substr(0, 2) + L"\\";
    wchar_t fs_name[32]{};
    if (!GetVolumeInformationW(drive_root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs_name, 32)) {
        return false;
    }
    return _wcsicmp(fs_name, L"NTFS") == 0;
#else
    (void)root_path;
    return false;
#endif
}

Result<MftScanResult> scan_ntfs_mft(const std::filesystem::path& volume_root,
                                    const std::atomic<bool>* cancel_flag,
                                    size_t max_tree_depth) {
#ifndef _WIN32
    (void)volume_root;
    (void)cancel_flag;
    (void)max_tree_depth;
    return Error{ErrorCode::InvalidPath, "NTFS $MFT scanning is only available on Windows NTFS volumes"};
#else
    const auto t0 = std::chrono::steady_clock::now();
    if (!can_use_ntfs_mft(volume_root)) {
        return Error{ErrorCode::PermissionDenied, "NTFS $MFT requires Administrator privileges on a local NTFS volume"};
    }

    const std::wstring ws = volume_root.wstring();
    const std::wstring dev_path = L"\\\\.\\" + ws.substr(0, 2);

    HANDLE vol = CreateFileW(
        dev_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (vol == INVALID_HANDLE_VALUE) {
        return Error{ErrorCode::PermissionDenied, "Failed to open raw NTFS volume handle"};
    }

    struct HandleGuard {
        HANDLE h;
        ~HandleGuard() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    } guard{vol};

    USN_JOURNAL_DATA_V0 journal{};
    DWORD bytes_ret = 0;
    if (!DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal, sizeof(journal), &bytes_ret, nullptr)) {
        return Error{ErrorCode::IoFailure, "USN journal unavailable on volume; falling back to filesystem scan"};
    }

    MFT_ENUM_DATA_V0 med{};
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = journal.NextUsn;

    std::vector<uint8_t> buffer(65536);
    std::vector<MftRecord> records;
    records.reserve(32768);

    MftScanResult res;
    constexpr uint64_t kNtfsRootFrn = 5ULL;  // Standard NTFS root directory FRN

    while (DeviceIoControl(vol, FSCTL_ENUM_USN_DATA, &med, sizeof(med),
                           buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_ret, nullptr)) {
        if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) {
            return Error{ErrorCode::Cancelled, "NTFS $MFT scan cancelled"};
        }
        if (bytes_ret <= sizeof(USN)) break;

        DWORD offset = sizeof(USN);
        while (offset < bytes_ret) {
            const auto* rec = reinterpret_cast<const USN_RECORD_V2*>(buffer.data() + offset);
            if (rec->RecordLength == 0) break;

            const bool is_dir = (rec->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const uint64_t frn = rec->FileReferenceNumber & 0x0000FFFFFFFFFFFFULL;
            const uint64_t parent_frn = rec->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFULL;
            const std::string name = wide_to_utf8_len(rec->FileName, rec->FileNameLength / sizeof(wchar_t));

            records.push_back(MftRecord{frn, parent_frn, name, 0, is_dir});
            ++res.records_indexed;
            if (is_dir) ++res.dirs_indexed;
            else ++res.files_indexed;

            offset += rec->RecordLength;
        }
        med.StartFileReferenceNumber = *reinterpret_cast<const DWORDLONG*>(buffer.data());
    }

    res.root_tree = reconstruct_mft_tree(records, kNtfsRootFrn, volume_root, max_tree_depth);
    const auto t1 = std::chrono::steady_clock::now();
    res.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
#endif
}

}  // namespace reclaim
