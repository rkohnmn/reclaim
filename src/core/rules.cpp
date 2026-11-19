#include "reclaim/rules.hpp"
#include "reclaim/format.hpp"
#include "reclaim/json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace reclaim {

namespace {

std::optional<BloatCategory> parse_category(const std::string& s) {
    if (s == "SystemTemp") return BloatCategory::SystemTemp;
    if (s == "BrowserCache") return BloatCategory::BrowserCache;
    if (s == "DeveloperArtifacts") return BloatCategory::DeveloperArtifacts;
    if (s == "ShaderAndGameCache") return BloatCategory::ShaderAndGameCache;
    if (s == "OrphanedAppData") return BloatCategory::OrphanedAppData;
    if (s == "InstallerLeftovers") return BloatCategory::InstallerLeftovers;
    if (s == "LogsAndCrashDumps") return BloatCategory::LogsAndCrashDumps;
    if (s == "DuplicateFiles") return BloatCategory::DuplicateFiles;
    return std::nullopt;
}

const char* category_key(BloatCategory cat) noexcept {
    switch (cat) {
        case BloatCategory::SystemTemp: return "SystemTemp";
        case BloatCategory::BrowserCache: return "BrowserCache";
        case BloatCategory::DeveloperArtifacts: return "DeveloperArtifacts";
        case BloatCategory::ShaderAndGameCache: return "ShaderAndGameCache";
        case BloatCategory::OrphanedAppData: return "OrphanedAppData";
        case BloatCategory::InstallerLeftovers: return "InstallerLeftovers";
        case BloatCategory::LogsAndCrashDumps: return "LogsAndCrashDumps";
        case BloatCategory::DuplicateFiles: return "DuplicateFiles";
    }
    return "SystemTemp";
}

std::optional<SafetyLevel> parse_safety(const std::string& s) {
    if (s == "Safe") return SafetyLevel::Safe;
    if (s == "Review") return SafetyLevel::Review;
    if (s == "Caution") return SafetyLevel::Caution;
    return std::nullopt;
}

std::vector<std::string> extract_lower_string_array(const JsonValue* val) {
    std::vector<std::string> out;
    if (!val || !val->is_array()) return out;
    for (const auto& elem : val->as_array()) {
        if (elem.is_string() && !elem.as_string().empty()) {
            out.push_back(to_lower_ascii(elem.as_string()));
        }
    }
    return out;
}

std::string serialize_string_array(const std::vector<std::string>& items) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < items.size(); ++i) {
        oss << "\"" << json_escape(items[i]) << "\"";
        if (i + 1 < items.size()) oss << ", ";
    }
    oss << "]";
    return oss.str();
}

}  // namespace

std::vector<CleanupRule> default_cleanup_rules() {
    return {
        CleanupRule{
            "browser_cache",
            "Browser & Electron App Caches",
            "HTTP cache, code cache, and GPU cache from Chromium, Edge, Firefox, Discord, Slack, and VS Code. Automatically rebuilt on launch.",
            BloatCategory::BrowserCache,
            SafetyLevel::Safe,
            {"cache_data", "code cache", "gpucache", "dawngraphitecache", "dawnd3dcache", "service worker", "cache2"},
            {"/google/chrome/user data/", "/microsoft/edge/user data/", "/mozilla/firefox/profiles/", "/discord/cache", "/slack/cache"},
            {},
            0,
            true
        },
        CleanupRule{
            "shader_cache",
            "GPU & Game Shader Caches",
            "Compiled DirectX, NVIDIA DXCache/GLCache, AMD DxCache, Steam, and Epic Games Launcher web/shader caches.",
            BloatCategory::ShaderAndGameCache,
            SafetyLevel::Safe,
            {"dxcache", "glcache", "shadercache", "d3dscache", "nv_cache", "webcache_4430"},
            {"/nvidia/dxcache", "/nvidia/glcache", "/amd/dxcache", "/d3dscache", "/steamapps/shadercache", "/epicgameslauncher/saved/webcache"},
            {},
            0,
            true
        },
        CleanupRule{
            "system_temp",
            "Temporary & Scratch Files",
            "OS and application temporary files (.tmp, .temp, .bak, Python __pycache__) left behind after installs or crashes.",
            BloatCategory::SystemTemp,
            SafetyLevel::Safe,
            {"temp", "tmp", "__pycache__"},
            {"/appdata/local/temp", "/windows/temp", "/inetcache"},
            {".tmp", ".temp", ".bak", ".old", ".chk"},
            0,
            true
        },
        CleanupRule{
            "logs_crash_dumps",
            "Application Logs & Crash Dumps",
            "Diagnostic .log files, Windows Error Reporting (.dmp, .hdmp, .mdmp) crash dumps, and rotated trace files.",
            BloatCategory::LogsAndCrashDumps,
            SafetyLevel::Safe,
            {"crashdumps", "crashpad", "minidump", "reportarchive"},
            {"/microsoft/windows/wer/", "/crashdumps"},
            {".dmp", ".mdmp", ".hdmp", ".log", ".etl"},
            1024,
            true
        },
        CleanupRule{
            "ai_model_caches",
            "Local AI & ML Model Caches",
            "Large cached model weights and temporary inference blobs from Hugging Face Hub, Ollama, LM Studio, and ComfyUI.",
            BloatCategory::DeveloperArtifacts,
            SafetyLevel::Review,
            {"huggingface", "torch_extensions", "triton"},
            {"/.cache/huggingface/hub", "/.ollama/models/blobs", "/.cache/lm-studio", "/comfyui/temp"},
            {".gguf.part", ".safetensors.tmp"},
            0,
            false
        },
        CleanupRule{
            "dev_artifacts",
            "Developer Caches & Build Artifacts",
            "Reproducible dependency caches and build directories (node_modules, .gradle, .pytest_cache, pip/npm/pnpm/cargo caches).",
            BloatCategory::DeveloperArtifacts,
            SafetyLevel::Review,
            {"node_modules", ".pytest_cache", ".mypy_cache", ".ruff_cache", ".parcel-cache", ".turbo", ".next", "npm-cache", "pip-cache", ".gradle"},
            {"/npm-cache", "/pnpm-store", "/yarn/cache", "/pip/cache", "/.cargo/registry/cache"},
            {".pyc", ".pyo", ".o", ".obj"},
            0,
            false
        },
        CleanupRule{
            "installer_leftovers",
            "Installer & Package Leftovers",
            "Cached setup packages (.msi, .msp, .msu, .cab) and extracted update bundles no longer needed after installation.",
            BloatCategory::InstallerLeftovers,
            SafetyLevel::Review,
            {"package cache", "downloaded installations", "softwaredistribution"},
            {"/softwaredistribution/download", "/package cache"},
            {".msi", ".msp", ".cab", ".msu"},
            65536,
            false
        },
        CleanupRule{
            "orphaned_appdata",
            "Stale Telemetry & Update Caches",
            "Accumulated updater staging folders, Squirrel/Electron updater payloads, and pending update queues.",
            BloatCategory::OrphanedAppData,
            SafetyLevel::Review,
            {"squirreltemp", "pendingupdates", "telemetry"},
            {"/appdata/local/squirreltemp"},
            {".nupkg"},
            0,
            false
        }
    };
}

Result<std::vector<CleanupRule>> parse_rules_json(const std::string& json_text) {
    auto parsed = parse_json(json_text);
    if (!parsed) return parsed.error();

    const JsonValue& root = parsed.value();
    if (!root.is_object()) {
        return Error{ErrorCode::InvalidArgument, "rules.json root must be a JSON object"};
    }

    const JsonValue* rules_arr = root.find("rules");
    if (!rules_arr || !rules_arr->is_array()) {
        return Error{ErrorCode::InvalidArgument, "rules.json must contain a 'rules' array"};
    }

    std::vector<CleanupRule> out;
    out.reserve(rules_arr->as_array().size());

    for (size_t i = 0; i < rules_arr->as_array().size(); ++i) {
        const JsonValue& item = rules_arr->as_array()[i];
        if (!item.is_object()) {
            return Error{ErrorCode::InvalidArgument, "Rule entry #" + std::to_string(i) + " must be an object"};
        }

        const JsonValue* id_v = item.find("id");
        const JsonValue* name_v = item.find("name");
        const JsonValue* cat_v = item.find("category");
        const JsonValue* safety_v = item.find("safety");

        if (!id_v || !id_v->is_string() || id_v->as_string().empty() ||
            !name_v || !name_v->is_string() || name_v->as_string().empty()) {
            return Error{ErrorCode::InvalidArgument, "Rule entry #" + std::to_string(i) + " missing required 'id' or 'name'"};
        }

        CleanupRule rule;
        rule.id = id_v->as_string();
        rule.name = name_v->as_string();
        if (const JsonValue* desc_v = item.find("description"); desc_v && desc_v->is_string()) {
            rule.description = desc_v->as_string();
        }
        if (cat_v && cat_v->is_string()) {
            auto cat = parse_category(cat_v->as_string());
            if (!cat) {
                return Error{ErrorCode::InvalidArgument, "Unknown category '" + cat_v->as_string() + "' in rule '" + rule.id + "'"};
            }
            rule.category = *cat;
        }
        if (safety_v && safety_v->is_string()) {
            auto s = parse_safety(safety_v->as_string());
            if (!s) {
                return Error{ErrorCode::InvalidArgument, "Unknown safety '" + safety_v->as_string() + "' in rule '" + rule.id + "'"};
            }
            rule.safety = *s;
        }

        rule.dir_name_matches = extract_lower_string_array(item.find("dir_name_matches"));
        rule.path_substrings = extract_lower_string_array(item.find("path_substrings"));
        rule.extensions = extract_lower_string_array(item.find("extensions"));

        if (const JsonValue* min_v = item.find("min_size_bytes")) {
            rule.min_size_bytes = min_v->as_uint64(0);
        }
        if (const JsonValue* def_v = item.find("default_selected")) {
            rule.default_selected = def_v->as_bool(false);
        }

        out.push_back(std::move(rule));
    }

    return out;
}

Result<std::vector<CleanupRule>> load_rules_from_json_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error{ErrorCode::NotFound, "Cannot open rule pack file: " + path.string()};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_rules_json(ss.str());
}

std::string export_rules_json(const std::vector<CleanupRule>& rules) {
    std::ostringstream oss;
    oss << "{\n  \"schema_version\": 1,\n  \"rules\": [\n";
    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& r = rules[i];
        oss << "    {\n"
            << "      \"id\": \"" << json_escape(r.id) << "\",\n"
            << "      \"name\": \"" << json_escape(r.name) << "\",\n"
            << "      \"description\": \"" << json_escape(r.description) << "\",\n"
            << "      \"category\": \"" << category_key(r.category) << "\",\n"
            << "      \"safety\": \"" << safety_to_string(r.safety) << "\",\n"
            << "      \"dir_name_matches\": " << serialize_string_array(r.dir_name_matches) << ",\n"
            << "      \"path_substrings\": " << serialize_string_array(r.path_substrings) << ",\n"
            << "      \"extensions\": " << serialize_string_array(r.extensions) << ",\n"
            << "      \"min_size_bytes\": " << r.min_size_bytes << ",\n"
            << "      \"default_selected\": " << (r.default_selected ? "true" : "false") << "\n"
            << "    }" << (i + 1 < rules.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}\n";
    return oss.str();
}

std::optional<CleanupRule> match_cleanup_rule(const std::filesystem::path& path,
                                              bool is_directory,
                                              uint64_t size_bytes,
                                              const std::vector<CleanupRule>& rules) {
    const std::string name_lower = to_lower_ascii(path.filename().string());
    std::string full_lower = to_lower_ascii(path.lexically_normal().generic_string());
    std::replace(full_lower.begin(), full_lower.end(), '\\', '/');
    const std::string ext_lower = is_directory ? std::string{} : to_lower_ascii(path.extension().string());

    for (const auto& rule : rules) {
        if (!is_directory && size_bytes < rule.min_size_bytes) {
            continue;
        }

        if (is_directory) {
            for (const auto& dir_match : rule.dir_name_matches) {
                if (name_lower == dir_match) return rule;
            }
            for (const auto& sub : rule.path_substrings) {
                if (full_lower.size() >= sub.size() &&
                    full_lower.compare(full_lower.size() - sub.size(), sub.size(), sub) == 0) {
                    return rule;
                }
            }
        } else {
            if (!ext_lower.empty()) {
                for (const auto& ext : rule.extensions) {
                    if (ext_lower == ext) return rule;
                }
            }
        }
    }

    return std::nullopt;
}

}  // namespace reclaim
