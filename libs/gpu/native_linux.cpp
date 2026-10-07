#include "native.h"
#include <X11/Xlib.h> // after webgpu_cpp.h: Xlib defines macros such as Status and None
#include <X11/keysym.h>
#include <X11/Xutil.h> // XLookupString

// A child X11 window in the host's window. Dawn draws into it with Vulkan.
// Linux hosts have no shared event loop, so the host's timer (needsTimer)
// drives both our X events and frames.
namespace gpu::platform {

const char *const windowApi = CLAP_WINDOW_API_X11;
const bool needsTimer = true;

struct Window
{
    Display *display = nullptr; // our own connection, so we see only our events
    ::Window window = 0;
    Callbacks callbacks;
    bool locked = false;
    int anchorX = 0, anchorY = 0; // where the cursor is held while locked
    float x = 0, y = 0;           // pointer position reported to the view (pixels)
    Cursor blank = 0;             // an invisible cursor, made on first lock
};

Window *create(const clap_window_t *parent, uint32_t width, uint32_t height, Callbacks callbacks)
{
    Display *display = XOpenDisplay(nullptr);
    if (!display || !parent->x11)
    {
        if (display) XCloseDisplay(display);
        return nullptr;
    }
    auto *window = new Window{display, 0, std::move(callbacks)};
    window->window = XCreateSimpleWindow(display, ::Window(parent->x11), 0, 0, width, height, 0, 0, 0);
    XSelectInput(display, window->window, ButtonPressMask | ButtonReleaseMask | Button1MotionMask | KeyPressMask);
    XMapWindow(display, window->window);
    XFlush(display);
    return window;
}

wgpu::Surface createSurface(const wgpu::Instance &instance, Window *window)
{
    wgpu::SurfaceSourceXlibWindow source;
    source.display = window->display;
    source.window = window->window;
    wgpu::SurfaceDescriptor descriptor;
    descriptor.nextInChain = &source;
    return instance.CreateSurface(&descriptor);
}

void lockPointer(Window *window, bool locked)
{
    if (window->locked == locked) return;
    window->locked = locked;
    if (!locked)
    {
        XUngrabPointer(window->display, CurrentTime);
        XFlush(window->display);
        return;
    }
    if (!window->blank)
    {
        const char bits[1] = {0};
        const Pixmap empty = XCreateBitmapFromData(window->display, window->window, bits, 1, 1);
        XColor black = {};
        window->blank = XCreatePixmapCursor(window->display, empty, empty, &black, &black, 0, 0);
        XFreePixmap(window->display, empty);
    }
    window->anchorX = int(window->x), window->anchorY = int(window->y);
    XGrabPointer(window->display, window->window, False, ButtonReleaseMask | Button1MotionMask,
                 GrabModeAsync, GrabModeAsync, 0, window->blank, CurrentTime);
    XFlush(window->display);
}

void destroy(Window *window)
{
    lockPointer(window, false);
    if (window->blank) XFreeCursor(window->display, window->blank);
    XDestroyWindow(window->display, window->window);
    XCloseDisplay(window->display);
    delete window;
}

void setSize(Window *window, uint32_t width, uint32_t height)
{
    XResizeWindow(window->display, window->window, width, height);
    XFlush(window->display);
}

void pixelSize(Window *window, uint32_t &width, uint32_t &height)
{
    XWindowAttributes attributes = {};
    XGetWindowAttributes(window->display, window->window, &attributes);
    width = uint32_t(attributes.width), height = uint32_t(attributes.height);
}

float scale(Window *) { return 1.0f; }

void setVisible(Window *window, bool visible)
{
    if (visible) XMapWindow(window->display, window->window);
    else XUnmapWindow(window->display, window->window);
    XFlush(window->display);
}

static unsigned modifiers(unsigned state)
{
    return (state & ShiftMask ? View::shift : 0u) | (state & ControlMask ? View::control : 0u) | (state & Mod1Mask ? View::alt : 0u);
}

void pumpEvents(Window *window)
{
    while (XPending(window->display))
    {
        XEvent event;
        XNextEvent(window->display, &event);
        if (event.type == MotionNotify)
        {
            const int x = event.xmotion.x, y = event.xmotion.y;
            if (!window->locked)
                window->x = float(x), window->y = float(y);
            else if (x == window->anchorX && y == window->anchorY)
                continue; // our own warp back to the anchor
            else
            {
                // Move by how far the cursor got from the anchor, then put it back.
                window->x += float(x - window->anchorX), window->y += float(y - window->anchorY);
                XWarpPointer(window->display, 0, window->window, 0, 0, 0, 0, window->anchorX, window->anchorY);
            }
            window->callbacks.pointer({View::Pointer::move, window->x, window->y, modifiers(event.xmotion.state)});
        }
        else if (event.type == ButtonPress && (event.xbutton.button == Button4 || event.xbutton.button == Button5))
            window->callbacks.pointer({View::Pointer::wheel, float(event.xbutton.x), float(event.xbutton.y),
                                       modifiers(event.xbutton.state), event.xbutton.button == Button4 ? 1.0f : -1.0f});
        else if ((event.type == ButtonPress || event.type == ButtonRelease) && event.xbutton.button == Button1)
        {
            if (!window->locked) window->x = float(event.xbutton.x), window->y = float(event.xbutton.y);
            if (event.type == ButtonPress) XSetInputFocus(window->display, window->window, RevertToParent, CurrentTime);
            window->callbacks.pointer({event.type == ButtonPress ? View::Pointer::down : View::Pointer::up,
                                       window->x, window->y, modifiers(event.xbutton.state)});
        }
        else if (event.type == KeyPress)
        {
            View::Key key;
            switch (XLookupKeysym(&event.xkey, 0))
            {
            case XK_Left: key = View::Key::left; break;
            case XK_Right: key = View::Key::right; break;
            case XK_Up: key = View::Key::up; break;
            case XK_Down: key = View::Key::down; break;
            case XK_Page_Up: key = View::Key::pageUp; break;
            case XK_Page_Down: key = View::Key::pageDown; break;
            case XK_Home: key = View::Key::home; break;
            case XK_End: key = View::Key::end; break;
            case XK_Tab: case XK_ISO_Left_Tab: key = View::Key::tab; break;
            case XK_Return: case XK_KP_Enter: key = View::Key::enter; break;
            case XK_Escape: key = View::Key::escape; break;
            case XK_BackSpace: key = View::Key::backspace; break;
            case XK_Delete: key = View::Key::deleteForward; break;
            default:
            {
                // Typed characters (Latin-1 without an input method, which is
                // enough for numbers). X11 has no simple way to hand unused
                // keys back to the host.
                char typed[8] = {};
                if (XLookupString(&event.xkey, typed, sizeof(typed) - 1, nullptr, nullptr) == 1
                    && static_cast<unsigned char>(typed[0]) >= 0x20 && typed[0] != 0x7F && !(event.xkey.state & ControlMask))
                {
                    const auto c = static_cast<unsigned char>(typed[0]);
                    char utf8[3] = {char(c)};
                    if (c >= 0x80) utf8[0] = char(0xC0 | c >> 6), utf8[1] = char(0x80 | (c & 0x3F));
                    window->callbacks.text(utf8);
                }
                continue;
            }
            }
            window->callbacks.key(key, modifiers(event.xkey.state));
        }
    }
}

}
