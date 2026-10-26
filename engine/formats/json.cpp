#include "formats/json.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace openday::formats {

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : s_(text) {}

    JsonValue document() {
        JsonValue v = value(0);
        skipSpace();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw JsonError(std::string("JSON: ") + what + " at offset " + std::to_string(pos_));
    }

    void skipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    void expect(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) fail("unexpected token");
        pos_ += word.size();
    }

    // After an element of an array or object: true if another one follows.
    bool more(char close) {
        skipSpace();
        if (peek() == ',') {
            ++pos_;
            return true;
        }
        if (peek() != close) fail(close == ']' ? "expected ',' or ']'" : "expected ',' or '}'");
        ++pos_;
        return false;
    }

    JsonValue value(int depth) {
        if (depth > 64) fail("nesting too deep");
        skipSpace();
        JsonValue v;
        const char c = peek();
        if (c == '{') {
            v.type_ = JsonValue::Type::Object;
            ++pos_;
            skipSpace();
            if (peek() == '}') {
                ++pos_;
                return v;
            }
            do {
                skipSpace();
                if (peek() != '"') fail("expected a member name");
                std::string key = string();
                skipSpace();
                if (peek() != ':') fail("expected ':'");
                ++pos_;
                v.members_.insert_or_assign(std::move(key), value(depth + 1));
            } while (more('}'));
            return v;
        }
        if (c == '[') {
            v.type_ = JsonValue::Type::Array;
            ++pos_;
            skipSpace();
            if (peek() == ']') {
                ++pos_;
                return v;
            }
            do {
                v.items_.push_back(value(depth + 1));
            } while (more(']'));
            return v;
        }
        if (c == '"') {
            v.type_ = JsonValue::Type::String;
            v.string_ = string();
            return v;
        }
        if (c == 't' || c == 'f') {
            v.type_ = JsonValue::Type::Bool;
            v.bool_ = c == 't';
            expect(v.bool_ ? "true" : "false");
            return v;
        }
        if (c == 'n') {
            expect("null");
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos_;
            while (pos_ < s_.size() && std::string_view("+-.eE0123456789").find(s_[pos_]) != std::string_view::npos) ++pos_;
            v.type_ = JsonValue::Type::Number;
            // strtod, not from_chars: older macOS deployment targets lack the
            // floating-point from_chars. The program keeps the "C" locale.
            const std::string digits(s_.substr(start, pos_ - start));
            char* end = nullptr;
            v.number_ = std::strtod(digits.c_str(), &end);
            if (digits.empty() || end != digits.c_str() + digits.size()) fail("malformed number");
            return v;
        }
        fail("unexpected character");
    }

    std::uint32_t hex4() {
        if (pos_ + 4 > s_.size()) fail("truncated escape");
        std::uint32_t u = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = s_[pos_++];
            u <<= 4;
            if (h >= '0' && h <= '9') u |= static_cast<std::uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') u |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') u |= static_cast<std::uint32_t>(h - 'A' + 10);
            else fail("malformed escape");
        }
        return u;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string string() {
        ++pos_; // opening quote
        std::string out;
        for (;;) {
            if (pos_ >= s_.size()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) fail("unterminated string");
            switch (s_[pos_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && s_.substr(pos_, 2) == "\\u") {
                    pos_ += 2;
                    const std::uint32_t low = hex4();
                    cp = low >= 0xDC00 && low < 0xE000 ? 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00) : 0xFFFD;
                } else if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD; // lone surrogate
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("unknown escape");
            }
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

namespace {
const JsonValue kNull;
const std::string kEmpty;
const std::vector<JsonValue> kNoItems;
} // namespace

const std::string& JsonValue::string() const { return type_ == Type::String ? string_ : kEmpty; }

const std::vector<JsonValue>& JsonValue::items() const { return type_ == Type::Array ? items_ : kNoItems; }

const JsonValue& JsonValue::operator[](std::string_view key) const {
    if (type_ != Type::Object) return kNull;
    const auto it = members_.find(key);
    return it == members_.end() ? kNull : it->second;
}

JsonValue JsonValue::parse(std::string_view text) { return JsonParser(text).document(); }

namespace {

bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || i + static_cast<std::size_t>(extra) >= s.size()) return false;
        for (int k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

} // namespace

std::string jsonQuote(std::string_view s) {
    const bool utf8 = validUtf8(s);
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else if (c >= 0x80 && !utf8) {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                out += ch;
            }
        }
    }
    out += '"';
    return out;
}

void JsonWriter::separate() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (!first_.empty()) {
        if (!first_.back()) out_ += ',';
        first_.back() = false;
    }
}

JsonWriter& JsonWriter::open(char c) {
    separate();
    out_ += c;
    first_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::close(char c) {
    out_ += c;
    if (!first_.empty()) first_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
    separate();
    out_ += jsonQuote(name);
    out_ += ':';
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(std::string_view s) {
    separate();
    out_ += jsonQuote(s);
    return *this;
}

JsonWriter& JsonWriter::value(bool b) {
    separate();
    out_ += b ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::value(int n) {
    separate();
    out_ += std::to_string(n);
    return *this;
}

JsonWriter& JsonWriter::value(double n) {
    separate();
    if (!std::isfinite(n)) {
        out_ += "null";
    } else {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.17g", n);
        out_ += buf;
    }
    return *this;
}

JsonWriter& JsonWriter::null() {
    separate();
    out_ += "null";
    return *this;
}

JsonWriter& JsonWriter::raw(std::string_view json) {
    separate();
    out_ += json;
    return *this;
}

} // namespace openday::formats
