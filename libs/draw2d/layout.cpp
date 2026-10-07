#define CLAY_IMPLEMENTATION
#include "layout.h"
#include <cstdio>

namespace gpu {

Layout::Layout()
{
    // Clay_MinMemorySize and Clay_Initialize read Clay's global current
    // context, which may belong to another instance: size from the defaults.
    Clay_SetCurrentContext(nullptr);
    memory.resize(Clay_MinMemorySize());
    const Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(memory.size(), memory.data());
    context = Clay_Initialize(arena, {1, 1}, {[](Clay_ErrorData error) {
        std::fprintf(stderr, "Clay: %.*s\n", int(error.errorText.length), error.errorText.chars);
    }, nullptr});
}

Layout::~Layout()
{
    // Don't leave Clay pointing at memory that is about to be freed.
    if (Clay_GetCurrentContext() == context)
        Clay_SetCurrentContext(nullptr);
}

void Layout::begin(Draw2D &newDraw, float width, float height, float newScale)
{
    draw = &newDraw;
    scale = newScale;
    strings.clear();
    customs.clear();
    Clay_SetCurrentContext(context); // each plugin instance has its own
    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig *config, void *self) -> Clay_Dimensions {
        const auto *layout = static_cast<Layout *>(self);
        return {layout->draw->measure(config->fontSize, {text.chars, size_t(text.length)}), config->fontSize * 1.25f};
    }, this);
    Clay_SetLayoutDimensions({width / scale, height / scale});
    Clay_BeginLayout();
}

Clay_String Layout::text(std::string characters)
{
    const auto &kept = strings.emplace_back(std::move(characters));
    return {false, int32_t(kept.size()), kept.data()};
}

void *Layout::custom(int kind, void *data)
{
    return &customs.emplace_back(Custom{kind, data});
}

void Layout::end(const std::function<void(int, void *, Rect)> &onCustom)
{
    const Clay_RenderCommandArray commands = Clay_EndLayout();
    const float s = scale;
    auto color = [](Clay_Color c) { return Color{c.r / 255, c.g / 255, c.b / 255, c.a / 255}; };
    for (int32_t i = 0; i < commands.length; ++i)
    {
        const Clay_RenderCommand &command = commands.internalArray[i];
        const Clay_BoundingBox &b = command.boundingBox;
        const Rect r{b.x * s, b.y * s, b.width * s, b.height * s};
        switch (command.commandType)
        {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const auto &data = command.renderData.rectangle;
            draw->rect(r.x, r.y, r.w, r.h, data.cornerRadius.topLeft * s, color(data.backgroundColor));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const auto &data = command.renderData.border;
            draw->rect(r.x, r.y, r.w, r.h, data.cornerRadius.topLeft * s, {0, 0, 0, 0}, data.width.left * s,
                       color(data.color));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const auto &data = command.renderData.text;
            // Clay gives the text's box; Draw2D wants its baseline.
            draw->text(r.x, r.y + data.fontSize * 0.95f * s, data.fontSize * s,
                       {data.stringContents.chars, size_t(data.stringContents.length)}, color(data.textColor));
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
            const auto *custom = static_cast<const Custom *>(command.renderData.custom.customData);
            onCustom(custom->kind, custom->data, r);
            break;
        }
        default: break; // images and clipping are not used
        }
    }
}

}
