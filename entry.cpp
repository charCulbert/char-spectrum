// The CLAP entry point: what a host looks up first when it loads the plugin
// file. It offers one factory, which makes the one plugin in plugin.cpp.
// (clap-wrapper compiles this file into each format separately, so it stays
// apart from plugin.cpp.)
#include <clap/clap.h>
#include <string.h>
#include "core/resources.h"

// Defined in plugin.cpp.
const clap_plugin_descriptor_t *getPluginDescriptor();
const clap_plugin_t *createPlugin(const clap_host_t *host);

static const clap_plugin_factory_t pluginFactory = {
    .get_plugin_count = [](const clap_plugin_factory *factory) -> uint32_t { return 1; },

    .get_plugin_descriptor = [](const clap_plugin_factory *factory, uint32_t index) -> const clap_plugin_descriptor_t *
    { return index == 0 ? getPluginDescriptor() : nullptr; },

    .create_plugin = [](const clap_plugin_factory *factory, const clap_host_t *host, const char *pluginID) -> const clap_plugin_t *
    {
        if (!clap_version_is_compatible(host->clap_version) || strcmp(pluginID, getPluginDescriptor()->id))
            return nullptr;
        return createPlugin(host);
    },
};

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,

    .init = [](const char *path) -> bool
    {
        core::initResources(path); // finds the files it ships (see core/resources.h)
        return true;
    },

    .deinit = []() {},

    .get_factory = [](const char *factoryID) -> const void *
    { return strcmp(factoryID, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &pluginFactory; },
};
