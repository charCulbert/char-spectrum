#pragma once
#include <webgpu/webgpu_cpp.h>
#include "../core/messages.h"
#include <functional>
#include <memory>

// A plugin's GPU-drawn interface. The same View runs natively (gpu/gui.cpp,
// Dawn) and in a WCLAP host's page (gpu/web/main.cpp, the browser's WebGPU).
// Presenters own the device and surface; a View records draw calls, handles
// the pointer, and exchanges the plugin's messages.
namespace gpu {

class View
{
public:
    enum class Pointer { down, move, up, wheel };
    enum Modifier : unsigned { shift = 1, control = 2, alt = 4, command = 8 };
    struct PointerEvent
    {
        Pointer type;
        float x, y;             // pixels
        unsigned modifiers = 0; // Modifier bits
        float wheel = 0;        // for Pointer::wheel: notches, positive away from the user
    };
    // The keys controls and text fields use; other keys go back to the host.
    enum class Key { left, right, up, down, pageUp, pageDown, home, end, tab, enter, escape, backspace, deleteForward };

    virtual ~View() = default;
    virtual void prepare(const wgpu::Device &device, wgpu::TextureFormat format) = 0;
    // Sizes are in pixels; scale is pixels per logical point (for line widths).
    virtual void draw(const wgpu::RenderPassEncoder &pass, float width, float height, float scale) = 0;
    virtual void pointer(const PointerEvent &event) = 0;
    // Returns true if the key was used; presenters pass unused keys to the host.
    virtual bool key(Key, unsigned /*modifiers*/) { return false; }
    // Typed text (UTF-8); returns true if used, e.g. by a focused text field.
    virtual bool text(const char * /*utf8*/) { return false; }
    virtual void receive(const core::Value &message) = 0;
    // Presenters draw only after input, a message or a resize, plus every
    // frame while this is true (e.g. during an animation).
    virtual bool animating() const { return false; }

    // Set by the presenter: delivers a message to the plugin.
    std::function<void(const core::Value &)> send;
    // Set by the presenter: while locked, the cursor is hidden and stays put,
    // and pointer() positions keep moving with the mouse past the view's and
    // the screen's edges. Where the platform refuses, drags work as before.
    std::function<void(bool locked)> lockPointer;
    // Set by the presenter where the view can ask for a new size (natively;
    // not in WCLAP pages): pixels, as draw() gets them. The plugin's
    // clap.gui.adjust_size limits it, and the host resizes the window.
    std::function<void(float width, float height)> resizeTo;
};

// Implemented by the plugin's interface code (e.g. ui/AnalyzerView.cpp).
std::unique_ptr<View> createView();

// Reads a file the plugin ships (its resources/ folder), e.g. "/fonts/Inter.ttf".
// Natively it comes from the bundle's resources; in WCLAP it is built into the
// wasm. Returns empty when missing. Implemented by each presenter.
std::string loadAsset(const char *path);

}
