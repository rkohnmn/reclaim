#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

// Built-in catalog of safe, transparent Windows, AI, game, and developer cleanup rules.
std::vector<CleanupRule> default_cleanup_rules();

// Parses a JSON rule pack string (schema_version: 1) into validated CleanupRule objects.
Result<std::vector<CleanupRule>> parse_rules_json(const std::string& json_text);

// Loads and validates a user-editable rules.json file from disk.
Result<std::vector<CleanupRule>> load_rules_from_json_file(const std::filesystem::path& path);

// Serializes a rule catalog to formatted JSON (schema_version: 1).
std::string export_rules_json(const std::vector<CleanupRule>& rules);

// Evaluates a single filesystem entry against the rule catalog.
std::optional<CleanupRule> match_cleanup_rule(const std::filesystem::path& path,
                                              bool is_directory,
                                              uint64_t size_bytes,
                                              const std::vector<CleanupRule>& rules);

}  // namespace reclaim
