#pragma once
#include "draw2d.h"
#include <clay.h>
#include <deque>
#include <functional>
#include <string>
#include <vector>

// Flexbox-style layout with Clay (github.com/nicbarker/clay). Each frame,
// declare the interface as nested CLAY(...) boxes, sized in points. Layout
// draws the plain boxes, borders and text with Draw2D, and reports custom
// boxes (knobs, plots) back with their rectangle in pixels.
namespace gpu {

struct Rect
{
    float x, y, w, h;
};

class Layout
{
public:
    Layout();
    ~Layout();

    // Starts declaring a frame: the canvas in pixels, and pixels per point.
    void begin(Draw2D &draw, float width, float height, float scale);
    // Clay keeps pointers to text until end(); this keeps the characters alive.
    Clay_String text(std::string characters);
    // Marks a box as custom: pass the result as .custom = {.customData = ...}.
    void *custom(int kind, void *data = nullptr);
    // Computes the layout, draws it, and calls onCustom for each custom box.
    void end(const std::function<void(int kind, void *data, Rect pixels)> &onCustom);

private:
    struct Custom { int kind; void *data; };
    std::vector<char> memory;
    Clay_Context *context = nullptr;
    Draw2D *draw = nullptr;
    float scale = 1;
    std::deque<std::string> strings;
    std::deque<Custom> customs;
};

// Clay colors are 0-255.
inline Clay_Color clayColor(Color c) { return {c.r * 255, c.g * 255, c.b * 255, c.a * 255}; }

}
