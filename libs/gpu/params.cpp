#include "params.h"
#include <algorithm>

namespace gpu {

bool Params::receive(const core::Value &message)
{
    const auto type = message["type"].text();
    if (type == "params")
    {
        params.clear();
        if (auto list = std::get_if<core::Value::Array>(&message["params"].data))
            for (const auto &p : *list)
                params.push_back({int(p["id"].number(-1)), std::string(p["name"].text()), p["min"].number(),
                                  p["max"].number(1), p["default"].number(), p["default"].number()});
        return true;
    }
    if (type == "values")
    {
        if (auto list = std::get_if<core::Value::Array>(&message["values"].data))
            for (auto &p : params)
                if (!p.dragging && size_t(p.id) < list->size()) p.value = (*list)[size_t(p.id)].number(p.value);
        return true;
    }
    return false;
}

Param *Params::find(std::string_view name)
{
    for (auto &p : params)
        if (p.name == name) return &p;
    return nullptr;
}

void Params::begin(Param &p)
{
    p.dragging = true;
    send(core::Value::Map{{"type", "begin"}, {"id", p.id}});
}

void Params::change(Param &p, double value)
{
    p.value = std::clamp(value, p.min, p.max);
    send(core::Value::Map{{"type", "change"}, {"id", p.id}, {"value", p.value}});
}

void Params::end(Param &p)
{
    p.dragging = false;
    send(core::Value::Map{{"type", "end"}, {"id", p.id}});
}

}
