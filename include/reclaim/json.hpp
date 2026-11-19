#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind{Kind::Null};
    bool bool_val{false};
    double num_val{0.0};
    std::string str_val;
    JsonArray arr_val;
    JsonObject obj_val;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : kind(Kind::Null) {}
    explicit JsonValue(bool b) : kind(Kind::Bool), bool_val(b) {}
    explicit JsonValue(double n) : kind(Kind::Number), num_val(n) {}
    explicit JsonValue(std::string s) : kind(Kind::String), str_val(std::move(s)) {}
    explicit JsonValue(JsonArray a) : kind(Kind::Array), arr_val(std::move(a)) {}
    explicit JsonValue(JsonObject o) : kind(Kind::Object), obj_val(std::move(o)) {}

    bool is_null() const noexcept { return kind == Kind::Null; }
    bool is_bool() const noexcept { return kind == Kind::Bool; }
    bool is_number() const noexcept { return kind == Kind::Number; }
    bool is_string() const noexcept { return kind == Kind::String; }
    bool is_array() const noexcept { return kind == Kind::Array; }
    bool is_object() const noexcept { return kind == Kind::Object; }

    bool as_bool(bool fallback = false) const noexcept {
        return is_bool() ? bool_val : fallback;
    }
    double as_number(double fallback = 0.0) const noexcept {
        return is_number() ? num_val : fallback;
    }
    uint64_t as_uint64(uint64_t fallback = 0) const noexcept {
        return (is_number() && num_val >= 0.0) ? static_cast<uint64_t>(num_val) : fallback;
    }
    const std::string& as_string() const noexcept { return str_val; }
    const JsonArray& as_array() const noexcept { return arr_val; }
    const JsonObject& as_object() const noexcept { return obj_val; }

    const JsonValue* find(const std::string& key) const {
        if (!is_object()) return nullptr;
        auto it = obj_val.find(key);
        return it == obj_val.end() ? nullptr : &it->second;
    }
};

// Zero-dependency RFC 8259 JSON parser with explicit line/column error reporting.
Result<JsonValue> parse_json(const std::string& input);

}  // namespace reclaim
