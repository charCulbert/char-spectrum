#include "resources.h"
#include <algorithm>
#include <fstream>
#include <iterator>

#if defined(_WIN32)
#define NOMINMAX // keep std::min usable
#include <windows.h>
#elif !defined(__wasi__)
#include <dlfcn.h>
#endif

namespace core {

// A function-local static, built on first use: clap-wrapper's AU calls
// clap_entry.init from a static constructor, which may run before a plain
// global's constructor would, and that constructor would then empty it.
static std::filesystem::path &root()
{
    static std::filesystem::path folder;
    return folder;
}

void setResourceRoot(std::filesystem::path folder) { root() = std::move(folder); }

#if !defined(__wasi__)
static std::filesystem::path thisBinary()
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    wchar_t path[32768] = {};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&thisBinary), &module) ||
        !GetModuleFileNameW(module, path, 32768))
        return {};
    return path;
#else
    Dl_info info = {};
    if (!dladdr(reinterpret_cast<const void *>(&thisBinary), &info) || !info.dli_fname)
        return {};
    return info.dli_fname;
#endif
}
#endif

void initResources(const char *entryPath)
{
#if defined(__wasi__)
    // The host mounts the WCLAP bundle at entryPath. WASI opens paths
    // relative to its preopened root, so drop the leading '/'.
    root() = std::filesystem::path(entryPath ? entryPath : "").relative_path() / "resources";
#else
    (void)entryPath;
    const auto binary = thisBinary();
    const auto folder = binary.parent_path();
    if (folder.parent_path().filename() == "Contents") // <bundle>/Contents/<MacOS|x86_64-win|...>/
        root() = folder.parent_path() / "Resources";
    else
        root() = folder / (binary.filename() += ".resources");
#endif
}

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string mimeFor(const std::filesystem::path &file)
{
    std::string extension = file.extension().string();
    for (auto &c : extension)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    static constexpr struct { const char *extension, *mime; } types[] = {
        {".html", "text/html"}, {".js", "text/javascript"}, {".mjs", "text/javascript"},
        {".css", "text/css"}, {".json", "application/json"}, {".wasm", "application/wasm"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".webp", "image/webp"}, {".gif", "image/gif"},
        {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".ttf", "font/ttf"}, {".otf", "font/otf"}, {".txt", "text/plain"},
        {".wav", "audio/wav"}, {".mp3", "audio/mpeg"},
    };
    for (const auto &type : types)
        if (extension == type.extension) return type.mime;
    return "application/octet-stream";
}

std::optional<Resource> readResource(std::string_view urlPath)
{
    urlPath = urlPath.substr(0, urlPath.find_first_of("?#"));
    if (root().empty() || urlPath.empty() || urlPath[0] != '/') return {};

    // Percent-decode, then accept only plain relative segments.
    std::string decoded;
    for (size_t i = 1; i < urlPath.size(); ++i)
    {
        char c = urlPath[i];
        if (c == '%')
        {
            if (i + 2 >= urlPath.size()) return {};
            const int high = hexDigit(urlPath[i + 1]), low = hexDigit(urlPath[i + 2]);
            if (high < 0 || low < 0) return {};
            c = char(high * 16 + low);
            i += 2;
        }
        if (c == '\0' || c == '\\' || c == ':') return {};
        decoded += c;
    }
    if (decoded.empty() || decoded.back() == '/') decoded += "index.html";

    std::filesystem::path file = root();
    size_t start = 0;
    while (start <= decoded.size())
    {
        const size_t end = std::min(decoded.find('/', start), decoded.size());
        const auto part = std::string_view(decoded).substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return {};
        file /= std::filesystem::path(std::string(part));
        start = end + 1;
    }

#if !defined(__wasi__)
    // Opening a directory can succeed and read as empty, so check first.
    // Not in WASI: the WCLAP hosts' WASI (wclap-host-js) reports file and
    // directory types swapped, so this check would reject every file.
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return {};
#endif
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    Resource resource{std::string(std::istreambuf_iterator<char>(input), {}), mimeFor(file)};
    if (input.bad()) return {};
    return resource;
}

}
