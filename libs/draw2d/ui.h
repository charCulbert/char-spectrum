#pragma once
#include "draw2d.h"
#include "layout.h"
#include "../gpu/params.h"
#include "../gpu/view.h"
#include <chrono>

// Immediate-mode controls: call them from View::draw every frame, each with
// the pixel rectangle Layout gave it; each draws itself and edits its
// parameter (with begin/change/end). Forward View::pointer and View::key here.
//
// Knobs and sliders: drag (Shift for fine), scroll, double-click for the
// default. The last one clicked has keyboard focus: arrows step (Shift for
// fine), Page Up/Down step further, Home/End go to the ends, Tab moves on.
namespace gpu {

struct Theme
{
    Color text{0.90f, 0.93f, 0.95f}, dim{0.55f, 0.61f, 0.66f}, accent{0.37f, 0.71f, 0.64f};
    Color track{0.20f, 0.23f, 0.26f}, panel{0.11f, 0.13f, 0.15f, 0.92f}, edge{0.25f, 0.29f, 0.32f};
};

class Ui
{
public:
    Draw2D draw;
    Theme theme;
    std::function<void(bool)> lockPointer;      // View::lockPointer: knob and slider drags lock the pointer
    std::function<void(float, float)> resizeTo; // View::resizeTo: for resizeGrip

    void pointer(const View::PointerEvent &event);
    bool key(View::Key key, unsigned modifiers);
    void begin(float width, float height, float scale); // starts a frame of controls
    void end(const wgpu::RenderPassEncoder &pass);

    // A rotary control with its value and name below; drag up/down to change.
    // log: logarithmic mapping (min > 0).
    void knob(Params &params, Param *p, Rect r, const char *format, bool log = false);
    // A horizontal fader with its name and value above; drag left/right.
    void slider(Params &params, Param *p, Rect r, const char *format, bool log = false);

    // A custom region (pixels): reports press, drag and release on it, plus
    // double-clicks and the wheel over it.
    struct Area
    {
        bool pressed = false, held = false, released = false, doubleClicked = false;
        float x = 0, y = 0, wheel = 0;
        unsigned modifiers = 0;
    };
    Area area(const void *id, float x, float y, float w, float h);

    // A grip in the bottom-right corner of a view `width` x `height` pixels:
    // dragging it resizes the window (through resizeTo; drawn only if set).
    // Hosts without resizing of their own, such as AU hosts, need one.
    void resizeGrip(float width, float height);

    float s = 1; // pixels per point for this frame

private:
    // 0..1 position of a value, and back.
    static double toUnit(const Param &p, double v, bool log);
    static double fromUnit(const Param &p, double u, bool log);
    // Handles all input for the control with rectangle x,y,w,h. Returns true
    // while that control is being dragged.
    bool control(Params &params, Param &p, float x, float y, float w, float h, bool vertical, float travel, bool log);
    // A whole edit: begin, change and end at once (wheel, keys, double-click).
    void set(Params &params, Param &p, double value);
    bool over(float x, float y, float w, float h) const;
    bool focusShown(const void *id) const { return focus == id && focusVisible; }

    struct
    {
        float x = 0, y = 0, wheel = 0;
        unsigned modifiers = 0;
        bool down = false, pressed = false, released = false, doubleClicked = false;
    } mouse;
    std::chrono::steady_clock::time_point lastPress;
    float lastPressX = -1e9f, lastPressY = -1e9f;

    const void *active = nullptr; // the control being dragged
    float grabX = 0, grabY = 0;
    float gripOffsetX = 0, gripOffsetY = 0; // from the pointer to the view's corner
    double grabUnit = 0;
    bool grabFine = false;

    const void *focus = nullptr; // the control keys go to
    bool focusVisible = false;   // after a key, until the next click
    std::vector<std::pair<View::Key, unsigned>> keys; // this frame's
    std::vector<const void *> order;                  // controls in drawing order, for Tab
};

}
