#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Reads the files a plugin ships with it: its resources/ folder (a web page in
// page/, fonts, images, sounds...), which core_ship_resources copies into each
// format. Nothing here knows about any plugin.
//
// Where the folder lives:
//   macOS bundles, VST3 bundles     <bundle>/Contents/Resources/
//   loose binaries (Win/Linux .clap) <binary>.resources/
//   WCLAP                           <bundle>/resources/
namespace core {

// Call from clap_entry.init. Native builds find the binary that contains this
// code (wrappers pass different paths to init); WCLAP uses the bundle path.
void initResources(const char *entryPath);
void setResourceRoot(std::filesystem::path folder);

struct Resource
{
    std::string bytes;
    std::string mime;
};

// urlPath is an absolute URL path such as "/page/index.html" or
// "/fonts/font%20a.woff2?v=2". Returns nothing for anything outside the
// folder. File names must not contain '%', '?' or '#'.
std::optional<Resource> readResource(std::string_view urlPath);

}
