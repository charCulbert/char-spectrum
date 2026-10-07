#include "messages.h"
#include <cstdint>
#include <cstring>

// CBOR (RFC 8949) subset: null, booleans, integers and floats (read as
// double), text strings, arrays, and maps with text keys. Definite lengths only.
namespace core {

const Value &Value::operator[](std::string_view key) const
{
    static const Value missing;
    if (auto map = std::get_if<Map>(&data))
        if (auto found = map->find(key); found != map->end()) return found->second;
    return missing;
}

double Value::number(double fallback) const
{
    auto v = std::get_if<double>(&data);
    return v ? *v : fallback;
}

std::string_view Value::text() const
{
    auto v = std::get_if<std::string>(&data);
    return v ? std::string_view(*v) : std::string_view();
}

// ---- Encoding

static void writeHead(std::vector<unsigned char> &out, unsigned major, uint64_t n)
{
    const unsigned char type = (unsigned char)(major << 5);
    int extra = n < 24 ? 0 : n <= 0xff ? 1 : n <= 0xffff ? 2 : n <= 0xffffffff ? 4 : 8;
    out.push_back(type | (unsigned char)(extra == 0 ? n : extra == 1 ? 24 : extra == 2 ? 25 : extra == 4 ? 26 : 27));
    for (int i = extra - 1; i >= 0; --i) out.push_back((unsigned char)(n >> (8 * i)));
}

static void write(std::vector<unsigned char> &out, const Value &value)
{
    if (std::holds_alternative<std::monostate>(value.data)) out.push_back(0xf6);
    else if (auto b = std::get_if<bool>(&value.data)) out.push_back(*b ? 0xf5 : 0xf4);
    else if (auto d = std::get_if<double>(&value.data))
    {
        uint64_t bits;
        std::memcpy(&bits, d, 8);
        out.push_back(0xfb); // always float64: exact, and simple to read back
        for (int i = 7; i >= 0; --i) out.push_back((unsigned char)(bits >> (8 * i)));
    }
    else if (auto s = std::get_if<std::string>(&value.data))
    {
        writeHead(out, 3, s->size());
        out.insert(out.end(), s->begin(), s->end());
    }
    else if (auto a = std::get_if<Value::Array>(&value.data))
    {
        writeHead(out, 4, a->size());
        for (const auto &item : *a) write(out, item);
    }
    else if (auto m = std::get_if<Value::Map>(&value.data))
    {
        writeHead(out, 5, m->size());
        for (const auto &[key, item] : *m)
        {
            writeHead(out, 3, key.size());
            out.insert(out.end(), key.begin(), key.end());
            write(out, item);
        }
    }
}

std::vector<unsigned char> encode(const Value &value)
{
    std::vector<unsigned char> out;
    write(out, value);
    return out;
}

// ---- Decoding: every length is checked against the bytes that remain.

namespace {
struct Reader
{
    const unsigned char *pos, *end;
    size_t remaining() const { return size_t(end - pos); }

    bool readUint(int count, uint64_t &n)
    {
        if (remaining() < size_t(count)) return false;
        n = 0;
        for (int i = 0; i < count; ++i) n = (n << 8) | *pos++;
        return true;
    }

    bool readText(uint64_t length, std::string &text)
    {
        if (length > remaining()) return false;
        text.assign((const char *)pos, size_t(length));
        pos += length;
        return true;
    }

    bool read(Value &value, int depth)
    {
        if (depth > 32 || pos == end) return false;
        const unsigned major = *pos >> 5, info = *pos & 31;
        ++pos;

        if (major == 7)
        {
            uint64_t bits;
            switch (info)
            {
            case 20: value = false; return true;
            case 21: value = true; return true;
            case 22: value = Value(); return true;
            case 26: {
                if (!readUint(4, bits)) return false;
                float f;
                uint32_t b32 = uint32_t(bits);
                std::memcpy(&f, &b32, 4);
                value = double(f);
                return true;
            }
            case 27: {
                if (!readUint(8, bits)) return false;
                double d;
                std::memcpy(&d, &bits, 8);
                value = d;
                return true;
            }
            default: return false; // undefined, half floats, other simple values
            }
        }

        uint64_t n = info;
        if (info >= 24 && (info > 27 || !readUint(1 << (info - 24), n))) return false;

        switch (major)
        {
        case 0: value = double(n); return true;
        case 1: value = -1.0 - double(n); return true;
        case 3: {
            std::string text;
            if (!readText(n, text)) return false;
            value = std::move(text);
            return true;
        }
        case 4: {
            if (n > remaining()) return false; // each item needs at least one byte
            Value::Array array;
            for (uint64_t i = 0; i < n; ++i)
                if (!read(array.emplace_back(), depth + 1)) return false;
            value = std::move(array);
            return true;
        }
        case 5: {
            if (n > remaining() / 2) return false;
            Value::Map map;
            for (uint64_t i = 0; i < n; ++i)
            {
                Value key, item;
                if (!read(key, depth + 1) || !read(item, depth + 1)) return false;
                auto text = std::get_if<std::string>(&key.data);
                if (!text || !map.emplace(std::move(*text), std::move(item)).second) return false;
            }
            value = std::move(map);
            return true;
        }
        default: return false; // byte strings and tags are not part of this protocol
        }
    }
};
}

std::optional<Value> decode(const void *bytes, size_t size)
{
    if (!bytes || size == 0 || size > maxMessageBytes) return {};
    Reader reader{(const unsigned char *)bytes, (const unsigned char *)bytes + size};
    Value value;
    if (!reader.read(value, 0) || reader.pos != reader.end) return {};
    return value;
}

}
