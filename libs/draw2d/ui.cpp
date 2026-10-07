#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gpu {

void Ui::pointer(const View::PointerEvent &event)
{
    mouse.x = event.x, mouse.y = event.y, mouse.modifiers = event.modifiers;
    switch (event.type)
    {
    case View::Pointer::down:
    {
        // A second press soon after and near the first is a double-click.
        const auto now = std::chrono::steady_clock::now();
        const bool second = now - lastPress < std::chrono::milliseconds(400)
                         && std::abs(event.x - lastPressX) < 4 * s && std::abs(event.y - lastPressY) < 4 * s;
        mouse.doubleClicked |= second; // both presses may arrive before the next frame
        lastPress = second ? std::chrono::steady_clock::time_point{} : now;
        lastPressX = event.x, lastPressY = event.y;
        mouse.down = mouse.pressed = true;
        focusVisible = false;
        break;
    }
    case View::Pointer::up: mouse.down = false, mouse.released = true; break;
    case View::Pointer::wheel: mouse.wheel += event.wheel; break;
    case View::Pointer::move: break;
    }
}

bool Ui::key(View::Key key, unsigned modifiers)
{
    // Tab moves between controls; the rest need a control to act on.
    if (!focus && key != View::Key::tab) return false;
    keys.push_back({key, modifiers});
    focusVisible = true;
    return true;
}

void Ui::begin(float width, float height, float scale)
{
    s = scale;
    draw.begin(width, height);
    order.clear();
}

void Ui::end(const wgpu::RenderPassEncoder &pass)
{
    for (auto [key, modifiers] : keys)
    {
        if (key != View::Key::tab || order.empty()) continue;
        const auto at = std::find(order.begin(), order.end(), focus);
        const bool back = modifiers & View::shift;
        if (at == order.end()) focus = back ? order.back() : order.front();
        else if (back) focus = at == order.begin() ? order.back() : *(at - 1);
        else focus = at + 1 == order.end() ? order.front() : *(at + 1);
    }
    draw.flush(pass);
    // Each press, release, wheel turn and key is seen by one frame.
    mouse.pressed = mouse.released = mouse.doubleClicked = false;
    mouse.wheel = 0;
    keys.clear();
}

double Ui::toUnit(const Param &p, double v, bool log)
{
    if (p.max <= p.min) return 0;
    if (log && p.min > 0) return std::log(v / p.min) / std::log(p.max / p.min);
    return (v - p.min) / (p.max - p.min);
}

double Ui::fromUnit(const Param &p, double u, bool log)
{
    u = std::clamp(u, 0.0, 1.0);
    if (log && p.min > 0) return p.min * std::pow(p.max / p.min, u);
    return p.min + u * (p.max - p.min);
}

bool Ui::over(float x, float y, float w, float h) const
{
    return mouse.x >= x && mouse.x <= x + w && mouse.y >= y && mouse.y <= y + h;
}

void Ui::set(Params &params, Param &p, double value)
{
    params.begin(p);
    params.change(p, value);
    params.end(p);
}

bool Ui::control(Params &params, Param &p, float x, float y, float w, float h, bool vertical, float travel, bool log)
{
    order.push_back(&p);
    const bool fine = mouse.modifiers & View::shift;
    const double unit = toUnit(p, p.value, log);

    if (!active && over(x, y, w, h))
    {
        if (mouse.pressed) focus = &p;
        if (mouse.pressed && mouse.doubleClicked)
            set(params, p, p.defaultValue);
        else if (mouse.pressed)
        {
            active = &p;
            grabX = mouse.x, grabY = mouse.y, grabUnit = unit, grabFine = fine;
            params.begin(p);
            if (lockPointer) lockPointer(true);
        }
        else if (mouse.wheel != 0)
            set(params, p, fromUnit(p, unit + mouse.wheel * (fine ? 0.002 : 0.02), log));
    }

    if (focus == &p)
        for (auto [key, modifiers] : keys)
        {
            const double step = modifiers & View::shift ? 0.001 : 0.01, u = toUnit(p, p.value, log);
            switch (key)
            {
            case View::Key::up: case View::Key::right: set(params, p, fromUnit(p, u + step, log)); break;
            case View::Key::down: case View::Key::left: set(params, p, fromUnit(p, u - step, log)); break;
            case View::Key::pageUp: set(params, p, fromUnit(p, u + 0.1, log)); break;
            case View::Key::pageDown: set(params, p, fromUnit(p, u - 0.1, log)); break;
            case View::Key::home: set(params, p, p.min); break;
            case View::Key::end: set(params, p, p.max); break;
            default: break; // Tab: in end()
            }
        }

    if (active != &p) return false;
    // Switching to or from fine mid-drag continues from where the value is.
    if (fine != grabFine) grabX = mouse.x, grabY = mouse.y, grabUnit = unit, grabFine = fine;
    const double moved = (vertical ? grabY - mouse.y : mouse.x - grabX) / (travel * (fine ? 10 : 1));
    const double value = fromUnit(p, grabUnit + moved, log);
    if (value != p.value) params.change(p, value);
    if (!mouse.down)
    {
        params.end(p);
        active = nullptr;
        if (lockPointer) lockPointer(false);
    }
    return true;
}

void Ui::knob(Params &params, Param *p, Rect box, const char *format, bool log)
{
    if (!p) return;
    // The dial fills the width, leaving room below for two lines of text.
    const float r = std::min(box.w, box.h - 34 * s) / 2, cx = box.x + box.w / 2, cy = box.y + r;
    const bool dragging = control(params, *p, cx - r, cy - r, 2 * r, 2 * r, true, 200 * s, log);
    // 270 degrees of travel, from 7:30 to 4:30.
    const float from = 3.927f, sweep = 4.712f, u = float(toUnit(*p, p->value, log));
    const float width = std::max(3.0f * s, r * 0.14f);
    if (focusShown(p)) draw.circle(cx, cy, r + 2 * s, {0, 0, 0, 0}, 1.5f * s, theme.accent);
    draw.arc(cx, cy, r - width, width, from, from + sweep, theme.track);
    if (u > 0) draw.arc(cx, cy, r - width, width, from, from + sweep * u, theme.accent);
    const float angle = from + sweep * u, inner = r - 2.6f * width;
    draw.circle(cx, cy, inner, theme.panel, 1 * s, dragging ? theme.accent : theme.edge);
    draw.line(cx + std::sin(angle) * inner * 0.35f, cy - std::cos(angle) * inner * 0.35f,
              cx + std::sin(angle) * inner * 0.85f, cy - std::cos(angle) * inner * 0.85f, 2 * s, theme.text);
    char value[32];
    std::snprintf(value, sizeof(value), format, p->value);
    draw.text(cx, cy + r + 13 * s, 11 * s, value, theme.text, Draw2D::Align::center);
    draw.text(cx, cy + r + 28 * s, 11 * s, p->name, theme.dim, Draw2D::Align::center);
}

void Ui::slider(Params &params, Param *p, Rect box, const char *format, bool log)
{
    if (!p) return;
    const float px = box.x, py = box.y, w = box.w, h = box.h;
    const bool dragging = control(params, *p, px, py, w, h, false, w, log);
    const float u = float(toUnit(*p, p->value, log)), track = py + 16 * s;
    if (focusShown(p)) draw.rect(px - 3 * s, py - 3 * s, w + 6 * s, h + 6 * s, 4 * s, {0, 0, 0, 0}, 1.5f * s, theme.accent);
    draw.text(px, py + 8 * s, 11 * s, p->name, theme.dim);
    char value[32];
    std::snprintf(value, sizeof(value), format, p->value);
    draw.text(px + w, py + 8 * s, 10 * s, value, theme.text, Draw2D::Align::right);
    draw.rect(px, track - 2 * s, w, 4 * s, 2 * s, theme.track);
    draw.rect(px, track - 2 * s, w * u, 4 * s, 2 * s, theme.accent);
    draw.circle(px + w * u, track, (dragging ? 7.0f : 6.0f) * s, theme.text);
}

void Ui::resizeGrip(float width, float height)
{
    if (!resizeTo) return;
    const float size = 14 * s;
    const auto grip = area(&gripOffsetX, width - size, height - size, size, size);
    if (grip.pressed) gripOffsetX = width - grip.x, gripOffsetY = height - grip.y;
    if (grip.held) resizeTo(grip.x + gripOffsetX, grip.y + gripOffsetY);
    // Three short diagonal lines, as windows' resize corners have.
    const Color color = grip.held ? theme.accent : theme.dim;
    for (float d : {4.0f, 8.0f, 12.0f})
        draw.line(width - 3 * s - d * s, height - 3 * s, width - 3 * s, height - 3 * s - d * s, 1.2f * s, color);
}

Ui::Area Ui::area(const void *id, float x, float y, float w, float h)
{
    Area result;
    result.modifiers = mouse.modifiers;
    if (!active && over(x, y, w, h))
    {
        if (mouse.pressed && mouse.doubleClicked) result.doubleClicked = true;
        else if (mouse.pressed) active = id, result.pressed = true;
        result.wheel = mouse.wheel;
    }
    if (active != id) return result;
    result.held = true, result.x = mouse.x, result.y = mouse.y;
    if (!mouse.down) result.released = true, active = nullptr;
    return result;
}

}
