// A small JSON reader (RFC 8259) for the GitHub release information the
// updater downloads. Values form a tree; numbers are kept as double.
#pragma once

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openday::formats {

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }

    // The value, or `fallback` when it has another type.
    bool boolean(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double number(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
    const std::string& string() const; // empty unless a string

    // Array elements (empty unless an array).
    const std::vector<JsonValue>& items() const;
    // Object member, or a null value when missing (or not an object).
    const JsonValue& operator[](std::string_view key) const;

    static JsonValue parse(std::string_view text); // throws JsonError

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<JsonValue> items_;
    std::map<std::string, JsonValue, std::less<>> members_;
};

} // namespace openday::formats
