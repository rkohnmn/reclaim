#pragma once

#include <filesystem>
#include <string>

#include "reclaim/types.hpp"

namespace reclaim {

// Per-user Windows Explorer context-menu integration ("Analyze with Reclaim").
// Registers under HKCU\Software\Classes so no UAC elevation is required.
Result<void> install_explorer_context_menu(const std::filesystem::path& exe_path);
Result<void> uninstall_explorer_context_menu();
bool is_explorer_context_menu_installed();

}  // namespace reclaim
