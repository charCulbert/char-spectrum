// Runs a gpu::View in a WCLAP host's page: the browser's WebGPU draws into
// #canvas, and messages travel as clap.webview/3 describes (ArrayBuffers via
// window.parent.postMessage, and 'message' events back), encoded as libs/core/messages.h describes.
#include "../context.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

gpu::Context context;
std::unique_ptr<gpu::View> view;
bool ready = false;
bool dirty = true; // something changed since the last frame

EM_JS(void, postToPlugin, (const unsigned char *data, int size), {
    window.parent.postMessage(HEAPU8.slice(data, data + size).buffer, '*');
});

float pixelRatio() { return float(emscripten_get_device_pixel_ratio()); }

// Pointer lock needs a recent click (it comes from a knob press) and, inside
// an iframe, the host's sandbox must allow it; if refused, drags stay plain.
EM_JS(void, setPointerLock, (bool locked), {
    const canvas = document.getElementById('canvas');
    if (locked) try { Promise.resolve(canvas.requestPointerLock()).catch(() => {}); } catch {}
    else if (document.pointerLockElement === canvas) document.exitPointerLock();
});
EM_JS(bool, pointerLocked, (), {
    return document.pointerLockElement === document.getElementById('canvas');
});
float pointerX = 0, pointerY = 0; // pixels; keeps moving while the pointer is locked

bool frame(double, void *)
{
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const auto width = uint32_t(cssWidth * pixelRatio()), height = uint32_t(cssHeight * pixelRatio());
    if (width != context.width || height != context.height)
    {
        emscripten_set_canvas_element_size("#canvas", int(width), int(height));
        context.configure(width, height);
        dirty = true;
    }
    if (dirty || view->animating()) context.frame(*view, pixelRatio());
    dirty = false;
    return true; // keep the requestAnimationFrame loop going
}

template <class Event> unsigned modifiers(const Event &event)
{
    return (event.shiftKey ? gpu::View::shift : 0u) | (event.ctrlKey ? gpu::View::control : 0u)
         | (event.altKey ? gpu::View::alt : 0u) | (event.metaKey ? gpu::View::command : 0u);
}

bool wheel(int, const EmscriptenWheelEvent *event, void *)
{
    if (!ready) return false;
    // deltaY is positive towards the user, in pixels (about 100 per notch), lines or pages.
    const double perNotch = event->deltaMode == DOM_DELTA_PIXEL ? 100 : event->deltaMode == DOM_DELTA_LINE ? 3 : 1;
    view->pointer({gpu::View::Pointer::wheel, float(event->mouse.targetX) * pixelRatio(),
                   float(event->mouse.targetY) * pixelRatio(), modifiers(event->mouse), float(-event->deltaY / perNotch)});
    dirty = true;
    return true; // don't also scroll the page
}

bool key(int, const EmscriptenKeyboardEvent *event, void *)
{
    using Key = gpu::View::Key;
    static const std::pair<const char *, Key> keys[] = {
        {"ArrowLeft", Key::left}, {"ArrowRight", Key::right}, {"ArrowUp", Key::up}, {"ArrowDown", Key::down},
        {"PageUp", Key::pageUp}, {"PageDown", Key::pageDown}, {"Home", Key::home}, {"End", Key::end}, {"Tab", Key::tab},
        {"Enter", Key::enter}, {"Escape", Key::escape}, {"Backspace", Key::backspace}, {"Delete", Key::deleteForward}};
    if (!ready) return false;
    for (auto [name, k] : keys)
        if (std::strcmp(event->key, name) == 0)
            return view->key(k, modifiers(*event)) && (dirty = true); // handled: the browser does nothing else with it
    // A typed character: key is the character itself (named keys are longer ASCII words).
    const size_t length = std::strlen(event->key);
    const bool typed = length == 1 || (length > 1 && static_cast<unsigned char>(event->key[0]) >= 0x80);
    if (typed && !event->ctrlKey && !event->metaKey && view->text(event->key)) return dirty = true;
    return false;
}

bool mouse(int type, const EmscriptenMouseEvent *event, void *)
{
    if (!ready) return false;
    const auto pointer = type == EMSCRIPTEN_EVENT_MOUSEDOWN ? gpu::View::Pointer::down
                       : type == EMSCRIPTEN_EVENT_MOUSEUP   ? gpu::View::Pointer::up
                                                            : gpu::View::Pointer::move;
    if (!pointerLocked())
        pointerX = float(event->targetX) * pixelRatio(), pointerY = float(event->targetY) * pixelRatio();
    else if (pointer == gpu::View::Pointer::move)
        pointerX += float(event->movementX) * pixelRatio(), pointerY += float(event->movementY) * pixelRatio();
    // Handling the press stops the browser focusing this page, so do it here: keys come to the focused page.
    if (pointer == gpu::View::Pointer::down) EM_ASM(window.focus());
    view->pointer({pointer, pointerX, pointerY, modifiers(*event)});
    dirty = true;
    return true;
}

void start()
{
    view->prepare(context.device, context.format); // sends "ready"
    ready = true;
    emscripten_set_mousedown_callback("#canvas", nullptr, true, mouse);
    // Moves and releases anywhere in the page, so a drag can leave the canvas.
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, mouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, mouse);
    emscripten_set_wheel_callback("#canvas", nullptr, true, wheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, key);
    emscripten_request_animation_frame_loop(frame, nullptr);
}

}

// The ASSETS folder (gpu_add_page) is built into gui.wasm under /assets.
std::string gpu::loadAsset(const char *path)
{
    std::ifstream file(std::string("/assets") + path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

extern "C" EMSCRIPTEN_KEEPALIVE void gpuReceive(const unsigned char *data, int size)
{
    if (auto message = core::decode(data, size); message && view) view->receive(*message), dirty = true;
}

int main()
{
    view = gpu::createView();
    view->send = [](const core::Value &message) {
        const auto bytes = core::encode(message);
        postToPlugin(bytes.data(), int(bytes.size()));
    };
    view->lockPointer = [](bool locked) { setPointerLock(locked); };
    EM_ASM({
        window.addEventListener('message', event => {
            if (!(event.data instanceof ArrayBuffer)) return;
            const bytes = new Uint8Array(event.data);
            const pointer = _malloc(bytes.length);
            HEAPU8.set(bytes, pointer);
            _gpuReceive(pointer, bytes.length);
            _free(pointer);
        });
    });

    context.instance = wgpu::CreateInstance();
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    canvas.selector = "#canvas";
    wgpu::SurfaceDescriptor surfaceDescriptor;
    surfaceDescriptor.nextInChain = &canvas;
    context.surface = context.instance.CreateSurface(&surfaceDescriptor);

    // The browser answers asynchronously: adapter, then device, then start().
    wgpu::RequestAdapterOptions options;
    options.compatibleSurface = context.surface;
    context.instance.RequestAdapter(&options, wgpu::CallbackMode::AllowSpontaneous,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                emscripten_log(EM_LOG_ERROR, "No WebGPU adapter: %.*s", int(message.length), message.data);
                return;
            }
            context.adapter = std::move(adapter);
            context.adapter.RequestDevice(nullptr, wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                    if (status != wgpu::RequestDeviceStatus::Success)
                    {
                        emscripten_log(EM_LOG_ERROR, "No WebGPU device: %.*s", int(message.length), message.data);
                        return;
                    }
                    context.device = std::move(device);
                    context.configure(1, 1);
                    start();
                });
        });
}
