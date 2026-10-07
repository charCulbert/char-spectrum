#pragma once
#include "clap/clap.h"
#include "clap/ext/draft/webview.h"
#include "clap/ext/gui.h"
#include "clap/ext/timer-support.h"
#include "../core/messages.h"
#include <memory>

// clap.gui for a plugin whose interface is a gpu::View, drawn with Dawn straight
// into the host's window: no WebView. Same interface as webview::Gui, so
// plugin.cpp can use either. In WCLAP the host shows the View compiled to a web
// page itself (see gpu.cmake); this then only agrees to that and passes messages.
namespace gpu {

struct Context;
class View;
namespace platform { struct Window; }

class Gui
{
public:
    Gui();
    ~Gui();

    void init(const clap_plugin_t *plugin, const clap_host_t *host);

    bool isApiSupported(const char *api, bool floating) const;
    bool getPreferredApi(const char **api, bool *floating) const;
    bool create(const char *api, bool floating);
    void destroy();
    bool setParent(const clap_window_t *window);
    void setSize(uint32_t width, uint32_t height);
    bool show();
    bool hide();
    void onTimer(clap_id timer);
    // clap.gui sizes are points on macOS and pixels elsewhere: how many of
    // them make a point, for limits written in points.
    float pixelsPerPoint() const;

    // Delivers a message to the View. Fails if the interface is not open.
    bool send(const core::Value &message);

private:
    void frame();

    const clap_plugin_t *plugin = nullptr;
    const clap_host_t *host = nullptr;
    const clap_plugin_webview_t *pluginWebview = nullptr; // the View's messages go through it
#if defined(__wasm__)
    const clap_host_webview_t *hostWebview = nullptr; // shows the page
    bool created = false;
#else
    const clap_host_timer_support_t *hostTimers = nullptr;
    const clap_host_gui_t *hostGui = nullptr;      // request_resize, for the view's resize grip
    const clap_plugin_gui_t *pluginGui = nullptr;  // adjust_size: the plugin's own limits
    clap_id timer = CLAP_INVALID_ID;
    uint32_t width = 480, height = 300;
    std::unique_ptr<Context> context;
    std::unique_ptr<View> view;
    platform::Window *window = nullptr;
    bool dirty = true; // something changed since the last frame
#endif
};

}
