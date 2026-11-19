#include "reclaim/shell_integration.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace reclaim {

#ifdef _WIN32
namespace {

Result<void> write_hkcu_string(const wchar_t* subkey, const wchar_t* value_name, const std::wstring& data) {
    HKEY key = nullptr;
    const LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        return Error{ErrorCode::PermissionDenied, "Failed to open HKCU registry key"};
    }
    const LONG wrc = RegSetValueExW(
        key, value_name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (wrc != ERROR_SUCCESS) {
        return Error{ErrorCode::IoFailure, "Failed to write HKCU registry value"};
    }
    return {};
}

constexpr const wchar_t* kDirKey = L"Software\\Classes\\Directory\\shell\\ReclaimAnalyze";
constexpr const wchar_t* kDirCmdKey = L"Software\\Classes\\Directory\\shell\\ReclaimAnalyze\\command";
constexpr const wchar_t* kBgKey = L"Software\\Classes\\Directory\\Background\\shell\\ReclaimAnalyze";
constexpr const wchar_t* kBgCmdKey = L"Software\\Classes\\Directory\\Background\\shell\\ReclaimAnalyze\\command";

}  // namespace
#endif

Result<void> install_explorer_context_menu(const std::filesystem::path& exe_path) {
#ifndef _WIN32
    (void)exe_path;
    return Error{ErrorCode::InvalidPath, "Explorer context menu integration is only available on Windows"};
#else
    if (exe_path.empty()) {
        return Error{ErrorCode::InvalidArgument, "Executable path cannot be empty"};
    }
    const std::wstring exe = exe_path.wstring();
    const std::wstring cmd = L"\"" + exe + L"\" --scan \"%V\"";

    auto r1 = write_hkcu_string(kDirKey, nullptr, L"Analyze with Reclaim");
    if (!r1) return r1;
    auto r2 = write_hkcu_string(kDirKey, L"Icon", exe);
    if (!r2) return r2;
    auto r3 = write_hkcu_string(kDirCmdKey, nullptr, cmd);
    if (!r3) return r3;

    auto r4 = write_hkcu_string(kBgKey, nullptr, L"Analyze with Reclaim");
    if (!r4) return r4;
    auto r5 = write_hkcu_string(kBgKey, L"Icon", exe);
    if (!r5) return r5;
    return write_hkcu_string(kBgCmdKey, nullptr, cmd);
#endif
}

Result<void> uninstall_explorer_context_menu() {
#ifndef _WIN32
    return Error{ErrorCode::InvalidPath, "Explorer context menu integration is only available on Windows"};
#else
    RegDeleteTreeW(HKEY_CURRENT_USER, kDirKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kBgKey);
    return {};
#endif
}

bool is_explorer_context_menu_installed() {
#ifndef _WIN32
    return false;
#else
    HKEY key = nullptr;
    const LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kDirKey, 0, KEY_READ, &key);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
#endif
}

}  // namespace reclaim
