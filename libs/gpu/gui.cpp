#include "gui.h"
#include <algorithm>
#include <cstring>

#if defined(__wasm__)

// WCLAP: the host shows the page (gpu_add_page) and sends its messages to
// clap.webview; this only accepts the host's "webview" window and sends ours.
namespace gpu {

Gui::Gui() = default;
Gui::~Gui() = default;

void Gui::init(const clap_plugin_t *plugin_, const clap_host_t *host_)
{
    plugin = plugin_;
    host = host_;
    pluginWebview = static_cast<const clap_plugin_webview_t *>(plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    hostWebview = static_cast<const clap_host_webview_t *>(host->get_extension(host, CLAP_EXT_WEBVIEW));
}

bool Gui::isApiSupported(const char *api, bool floating) const
{
    return api && !floating && pluginWebview && hostWebview && std::strcmp(api, CLAP_WINDOW_API_WEBVIEW) == 0;
}

float Gui::pixelsPerPoint() const { return 1.0f; } // clap.webview sizes are logical

bool Gui::getPreferredApi(const char **api, bool *floating) const
{
    *api = CLAP_WINDOW_API_WEBVIEW;
    *floating = false;
    return isApiSupported(*api, false);
}

bool Gui::create(const char *api, bool floating)
{
    if (created || !isApiSupported(api, floating)) return false;
    return created = true;
}

void Gui::destroy() { created = false; }
bool Gui::setParent(const clap_window_t *window)
{
    return created && window && window->api && std::strcmp(window->api, CLAP_WINDOW_API_WEBVIEW) == 0;
}
void Gui::setSize(uint32_t, uint32_t) {}
bool Gui::show() { return created; }
bool Gui::hide() { return created; }
void Gui::onTimer(clap_id) {}

bool Gui::send(const core::Value &message)
{
    const auto bytes = core::encode(message);
    return hostWebview && hostWebview->send(host, bytes.data(), uint32_t(bytes.size()));
}

}

#else

#include "context.h"
#include "native.h"
#include "../core/resources.h"

namespace gpu {

std::string loadAsset(const char *path)
{
    auto resource = core::readResource(path);
    return resource ? std::move(resource->bytes) : std::string();
}

Gui::Gui() = default;
Gui::~Gui() { destroy(); }

void Gui::init(const clap_plugin_t *plugin_, const clap_host_t *host_)
{
    plugin = plugin_;
    host = host_;
    pluginWebview = static_cast<const clap_plugin_webview_t *>(plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    hostTimers = static_cast<const clap_host_timer_support_t *>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    hostGui = static_cast<const clap_host_gui_t *>(host->get_extension(host, CLAP_EXT_GUI));
    pluginGui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
}

bool Gui::isApiSupported(const char *api, bool floating) const
{
    return api && !floating && pluginWebview && std::strcmp(api, platform::windowApi) == 0 &&
           (!platform::needsTimer || hostTimers);
}

float Gui::pixelsPerPoint() const
{
    return std::strcmp(platform::windowApi, CLAP_WINDOW_API_COCOA) == 0 ? 1.0f : platform::scale(window);
}

bool Gui::getPreferredApi(const char **api, bool *floating) const
{
    *api = platform::windowApi;
    *floating = false;
    return isApiSupported(*api, false);
}

bool Gui::create(const char *api, bool floating)
{
    if (context || !isApiSupported(api, floating)) return false;
    auto newContext = std::make_unique<Context>();
    if (!newContext->createDevice()) return false;
    if (platform::needsTimer && !hostTimers->register_timer(host, 16, &timer))
    {
        timer = CLAP_INVALID_ID;
        return false;
    }
    context = std::move(newContext);
    view = createView();
    // The View talks to the plugin exactly as the page does: through clap.webview.
    view->send = [this](const core::Value &message) {
        const auto bytes = core::encode(message);
        pluginWebview->receive(plugin, bytes.data(), uint32_t(bytes.size()));
    };
    view->lockPointer = [this](bool locked) { if (window) platform::lockPointer(window, locked); };
    if (hostGui && pluginGui)
        view->resizeTo = [this](float w, float h) {
            // The view measures in pixels; clap.gui in points on macOS.
            const float perUnit = window ? platform::scale(window) / pixelsPerPoint() : 1.0f;
            uint32_t newWidth = uint32_t(std::max(1.0f, w / perUnit)), newHeight = uint32_t(std::max(1.0f, h / perUnit));
            pluginGui->adjust_size(plugin, &newWidth, &newHeight);
            if (newWidth != width || newHeight != height)
                hostGui->request_resize(host, newWidth, newHeight); // the host then calls set_size
        };
    return true;
}

void Gui::destroy()
{
    if (timer != CLAP_INVALID_ID) hostTimers->unregister_timer(host, timer);
    timer = CLAP_INVALID_ID;
    // GPU objects first: the surface refers to our window (and on Linux to its
    // X display connection), which must outlive it.
    view.reset();
    context.reset();
    if (window) platform::destroy(window);
    window = nullptr;
}

bool Gui::setParent(const clap_window_t *parent)
{
    if (!context || window || !parent || !parent->api || std::strcmp(parent->api, platform::windowApi) != 0)
        return false;
    window = platform::create(parent, width, height, {
        [this] { frame(); },
        [this](const View::PointerEvent &event) { if (view) view->pointer(event), dirty = true; },
        [this](View::Key key, unsigned modifiers) { dirty = true; return view && view->key(key, modifiers); },
        [this](const char *utf8) { dirty = true; return view && view->text(utf8); },
    });
    if (!window) return false;
    context->surface = platform::createSurface(context->instance, window);
    uint32_t w, h;
    platform::pixelSize(window, w, h);
    context->configure(w, h);
    dirty = true;
    view->prepare(context->device, context->format); // sends "ready"
    return true;
}

void Gui::setSize(uint32_t newWidth, uint32_t newHeight)
{
    width = newWidth;
    height = newHeight;
    if (!window) return;
    platform::setSize(window, width, height);
    uint32_t w, h;
    platform::pixelSize(window, w, h);
    context->configure(w, h);
    dirty = true;
}

bool Gui::show()
{
    if (window) platform::setVisible(window, true);
    return context != nullptr;
}

bool Gui::hide()
{
    if (window) platform::setVisible(window, false);
    return context != nullptr;
}

void Gui::onTimer(clap_id id)
{
    if (id != timer || !window) return;
    platform::pumpEvents(window);
    frame();
}

void Gui::frame()
{
    if (!window || !view) return;
    // The view's size in pixels changes without clap.gui.set_size too: a host
    // may create it before it is in a window (AU), or move it to a screen of
    // another scale. Keep the surface matching it.
    uint32_t w, h;
    platform::pixelSize(window, w, h);
    if (w && h && (w != context->width || h != context->height))
    {
        context->configure(w, h);
        dirty = true;
    }
    if (!(dirty || view->animating())) return;
    dirty = false;
    context->frame(*view, platform::scale(window));
}

bool Gui::send(const core::Value &message)
{
    if (!view) return false;
    view->receive(message);
    dirty = true;
    return true;
}

}

#endif
