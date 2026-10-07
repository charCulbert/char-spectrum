#define NOMINMAX // keep std::min usable
#include <windows.h>
#include "native.h"

// A child HWND in the host's window. Dawn draws into it with D3D12; a Win32
// timer on it drives frames, and its window procedure forwards the mouse and keys.
namespace gpu::platform {

const char *const windowApi = CLAP_WINDOW_API_WIN32;
const bool needsTimer = false;

struct Window
{
    HWND hwnd = nullptr;
    Callbacks callbacks;
    bool locked = false;
    POINT anchor = {};       // screen position the cursor is held at while locked
    float x = 0, y = 0;      // pointer position reported to the view (pixels)
};

static HINSTANCE thisModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &module);
    return module;
}

static unsigned modifiers()
{
    auto down = [](int key) { return GetKeyState(key) < 0; };
    return (down(VK_SHIFT) ? View::shift : 0u) | (down(VK_CONTROL) ? View::control : 0u) | (down(VK_MENU) ? View::alt : 0u);
}

static bool keyFor(WPARAM virtualKey, View::Key &key)
{
    switch (virtualKey)
    {
    case VK_LEFT: key = View::Key::left; return true;
    case VK_RIGHT: key = View::Key::right; return true;
    case VK_UP: key = View::Key::up; return true;
    case VK_DOWN: key = View::Key::down; return true;
    case VK_PRIOR: key = View::Key::pageUp; return true;
    case VK_NEXT: key = View::Key::pageDown; return true;
    case VK_HOME: key = View::Key::home; return true;
    case VK_END: key = View::Key::end; return true;
    case VK_TAB: key = View::Key::tab; return true;
    case VK_RETURN: key = View::Key::enter; return true;
    case VK_ESCAPE: key = View::Key::escape; return true;
    case VK_BACK: key = View::Key::backspace; return true;
    case VK_DELETE: key = View::Key::deleteForward; return true;
    }
    return false;
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *window = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!window) return DefWindowProcW(hwnd, message, wParam, lParam);
    auto pointer = [&](View::Pointer type, float wheel = 0) {
        window->callbacks.pointer({type, window->x, window->y, modifiers(), wheel});
    };
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
    {
        if (!window->locked)
            window->x = float(short(LOWORD(lParam))), window->y = float(short(HIWORD(lParam)));
        else if (message == WM_MOUSEMOVE)
        {
            // Move by how far the cursor got from the anchor, then put it back
            // (which sends one more WM_MOUSEMOVE, with no movement).
            POINT cursor;
            GetCursorPos(&cursor);
            if (cursor.x == window->anchor.x && cursor.y == window->anchor.y) return 0;
            window->x += float(cursor.x - window->anchor.x), window->y += float(cursor.y - window->anchor.y);
            SetCursorPos(window->anchor.x, window->anchor.y);
        }
    }
    switch (message)
    {
    case WM_TIMER: window->callbacks.frame(); return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd); // for keys
        SetCapture(hwnd);
        pointer(View::Pointer::down);
        return 0;
    case WM_MOUSEMOVE: if (GetCapture() == hwnd) pointer(View::Pointer::move); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); pointer(View::Pointer::up); return 0;
    case WM_MOUSEWHEEL:
    {
        POINT at = {short(LOWORD(lParam)), short(HIWORD(lParam))}; // screen coordinates
        ScreenToClient(hwnd, &at);
        window->callbacks.pointer({View::Pointer::wheel, float(at.x), float(at.y), modifiers(),
                                   float(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA});
        return 0;
    }
    case WM_KEYDOWN:
    {
        View::Key key;
        if (keyFor(wParam, key) && window->callbacks.key(key, modifiers())) return 0;
        // Not ours: let the host's window have it (its shortcuts, transport).
        PostMessageW(GetParent(hwnd), message, wParam, lParam);
        return 0;
    }
    case WM_CHAR:
    {
        // Typed characters (TranslateMessage makes these from WM_KEYDOWN).
        // UTF-16 in, UTF-8 out; controls and surrogate halves are skipped.
        const auto c = unsigned(wParam);
        char utf8[4] = {};
        if (c < 0x20 || c == 0x7F || (c >= 0xD800 && c < 0xE000)) return 0;
        if (c < 0x80) utf8[0] = char(c);
        else if (c < 0x800) utf8[0] = char(0xC0 | c >> 6), utf8[1] = char(0x80 | (c & 0x3F));
        else utf8[0] = char(0xE0 | c >> 12), utf8[1] = char(0x80 | (c >> 6 & 0x3F)), utf8[2] = char(0x80 | (c & 0x3F));
        if (!window->callbacks.text(utf8)) PostMessageW(GetParent(hwnd), message, wParam, lParam);
        return 0;
    }
    case WM_KEYUP: PostMessageW(GetParent(hwnd), message, wParam, lParam); return 0;
    case WM_ERASEBKGND: return 1; // Dawn paints every pixel
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

Window *create(const clap_window_t *parent, uint32_t width, uint32_t height, Callbacks callbacks)
{
    HWND host = static_cast<HWND>(parent->win32);
    if (!IsWindow(host)) return nullptr;
    static const wchar_t *className = L"ClapGpuView"; // per module: other plugins' classes don't clash
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = thisModule();
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    RegisterClassExW(&windowClass); // fails harmlessly when already registered

    auto *window = new Window{nullptr, std::move(callbacks)};
    window->hwnd = CreateWindowExW(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                   int(width), int(height), host, nullptr, thisModule(), nullptr);
    if (!window->hwnd)
    {
        delete window;
        return nullptr;
    }
    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    SetTimer(window->hwnd, 1, 16, nullptr);
    return window;
}

wgpu::Surface createSurface(const wgpu::Instance &instance, Window *window)
{
    wgpu::SurfaceSourceWindowsHWND source;
    source.hinstance = thisModule();
    source.hwnd = window->hwnd;
    wgpu::SurfaceDescriptor descriptor;
    descriptor.nextInChain = &source;
    return instance.CreateSurface(&descriptor);
}

void lockPointer(Window *window, bool locked)
{
    if (window->locked == locked) return;
    window->locked = locked;
    if (locked) GetCursorPos(&window->anchor);
    ShowCursor(locked ? FALSE : TRUE);
}

void destroy(Window *window)
{
    lockPointer(window, false);
    KillTimer(window->hwnd, 1);
    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, 0);
    DestroyWindow(window->hwnd);
    delete window;
}

// clap.gui sizes on Windows are already physical pixels.
void setSize(Window *window, uint32_t width, uint32_t height)
{
    SetWindowPos(window->hwnd, nullptr, 0, 0, int(width), int(height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void pixelSize(Window *window, uint32_t &width, uint32_t &height)
{
    RECT area = {};
    GetClientRect(window->hwnd, &area);
    width = uint32_t(area.right), height = uint32_t(area.bottom);
}

// Before the view has a window, the system's scale is the best guess.
float scale(Window *window) { return float(window ? GetDpiForWindow(window->hwnd) : GetDpiForSystem()) / 96.0f; }

void setVisible(Window *window, bool visible) { ShowWindow(window->hwnd, visible ? SW_SHOW : SW_HIDE); }

void pumpEvents(Window *) {}

}
