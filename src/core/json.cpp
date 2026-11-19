#include "reclaim/json.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace reclaim {

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : src_(src) {}

    Result<JsonValue> parse() {
        skip_ws();
        auto val = parse_value(0);
        if (!val) return val.error();
        skip_ws();
        if (pos_ != src_.size()) {
            return make_error("Unexpected trailing characters after JSON root");
        }
        return val;
    }

private:
    static constexpr int kMaxDepth = 64;

    Error make_error(const std::string& msg) const {
        std::ostringstream oss;
        oss << "JSON parse error at line " << line_ << ", col " << col_ << ": " << msg;
        return Error{ErrorCode::InvalidArgument, oss.str()};
    }

    char peek() const noexcept { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    char get() noexcept {
        if (pos_ >= src_.size()) return '\0';
        const char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        return c;
    }

    void skip_ws() noexcept {
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                get();
            } else {
                break;
            }
        }
    }

    bool consume_literal(const char* lit) noexcept {
        for (const char* p = lit; *p; ++p) {
            if (get() != *p) return false;
        }
        return true;
    }

    Result<JsonValue> parse_value(int depth) {
        if (depth > kMaxDepth) return make_error("Maximum JSON nesting depth exceeded");
        skip_ws();
        const char c = peek();
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"') return parse_string_val();
        if (c == 't') {
            if (!consume_literal("true")) return make_error("Expected literal 'true'");
            return JsonValue{true};
        }
        if (c == 'f') {
            if (!consume_literal("false")) return make_error("Expected literal 'false'");
            return JsonValue{false};
        }
        if (c == 'n') {
            if (!consume_literal("null")) return make_error("Expected literal 'null'");
            return JsonValue{nullptr};
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        return make_error("Unexpected token");
    }

    Result<std::string> parse_raw_string() {
        if (get() != '"') return make_error("Expected '\"'");
        std::string out;
        while (pos_ < src_.size()) {
            const char c = get();
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) {
                return make_error("Unescaped control character in string");
            }
            if (c == '\\') {
                const char esc = get();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return make_error("Incomplete \\u escape");
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = get();
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(10 + h - 'a');
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(10 + h - 'A');
                            else return make_error("Invalid hex digit in \\u escape");
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        return make_error("Invalid escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        return make_error("Unterminated string literal");
    }

    Result<JsonValue> parse_string_val() {
        auto s = parse_raw_string();
        if (!s) return s.error();
        return JsonValue{std::move(s.value())};
    }

    Result<JsonValue> parse_number() {
        const size_t start = pos_;
        if (peek() == '-') get();
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            return make_error("Invalid number");
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        if (peek() == '.') {
            get();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                return make_error("Invalid fractional number");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') get();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                return make_error("Invalid exponent in number");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        }
        const std::string token = src_.substr(start, pos_ - start);
        char* end_ptr = nullptr;
        const double num = std::strtod(token.c_str(), &end_ptr);
        if (!end_ptr || *end_ptr != '\0') return make_error("Number conversion failed");
        return JsonValue{num};
    }

    Result<JsonValue> parse_array(int depth) {
        get();
        JsonArray arr;
        skip_ws();
        if (peek() == ']') {
            get();
            return JsonValue{std::move(arr)};
        }
        while (true) {
            auto elem = parse_value(depth);
            if (!elem) return elem.error();
            arr.push_back(std::move(elem.value()));
            skip_ws();
            if (peek() == ',') {
                get();
                continue;
            }
            if (peek() == ']') {
                get();
                return JsonValue{std::move(arr)};
            }
            return make_error("Expected ',' or ']' in array");
        }
    }

    Result<JsonValue> parse_object(int depth) {
        get();
        JsonObject obj;
        skip_ws();
        if (peek() == '}') {
            get();
            return JsonValue{std::move(obj)};
        }
        while (true) {
            skip_ws();
            auto key = parse_raw_string();
            if (!key) return key.error();
            skip_ws();
            if (get() != ':') return make_error("Expected ':' after object key");
            auto val = parse_value(depth);
            if (!val) return val.error();
            obj[std::move(key.value())] = std::move(val.value());
            skip_ws();
            if (peek() == ',') {
                get();
                continue;
            }
            if (peek() == '}') {
                get();
                return JsonValue{std::move(obj)};
            }
            return make_error("Expected ',' or '}' in object");
        }
    }

    const std::string& src_;
    size_t pos_{0};
    size_t line_{1};
    size_t col_{1};
};

}  // namespace

Result<JsonValue> parse_json(const std::string& input) {
    JsonParser parser(input);
    return parser.parse();
}

}  // namespace reclaim
