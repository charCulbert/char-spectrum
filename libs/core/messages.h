#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Messages between plugin and web page: JSON-like values, sent as CBOR bytes.
// The page side is libs/webview/js/messages.js. Use on the main thread only.
namespace core {

struct Value
{
    using Array = std::vector<Value>;
    using Map = std::map<std::string, Value, std::less<>>;
    std::variant<std::monostate, bool, double, std::string, Array, Map> data;

    Value() = default;
    Value(bool v) : data(v) {}
    Value(double v) : data(v) {}
    Value(int v) : data(double(v)) {}
    Value(const char *v) : data(std::string(v)) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(Array v) : data(std::move(v)) {}
    Value(Map v) : data(std::move(v)) {}

    // Lenient readers: a missing or differently typed value gives the fallback.
    const Value &operator[](std::string_view key) const;
    double number(double fallback = 0) const;
    std::string_view text() const;
};

constexpr size_t maxMessageBytes = 1 << 20;

std::vector<unsigned char> encode(const Value &value);
// Returns nothing unless the bytes are exactly one well-formed value.
std::optional<Value> decode(const void *bytes, size_t size);

}
