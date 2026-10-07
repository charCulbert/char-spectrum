#pragma once
#include <webgpu/webgpu_cpp.h>
#include <string>
#include <string_view>
#include <vector>

// Small 2D drawing on WebGPU: rounded rectangles, circles and text. Every shape
// is one instanced quad; a signed-distance shader gives antialiased edges at
// any scale. Text uses a distance-field atlas built once from a TrueType font
// with stb_truetype (printable ASCII only).
namespace gpu {

struct Color { float r, g, b, a = 1; };

class Draw2D
{
public:
    enum class Align { left, center, right };

    // fontData: the bytes of a .ttf file. Returns false if the font is unusable.
    bool prepare(const wgpu::Device &device, wgpu::TextureFormat format, const std::string &fontData);

    // Coordinates are pixels from the top left, as in View::draw.
    void begin(float width, float height);
    void rect(float x, float y, float w, float h, float radius, Color fill,
              float border = 0, Color borderColor = {0, 0, 0, 0});
    void circle(float cx, float cy, float radius, Color fill, float border = 0, Color borderColor = {0, 0, 0, 0});
    // Strokes with round ends and joins. All lines draw in one pass on top of
    // the other shapes; only their points go to the GPU (16 bytes each).
    void line(float x0, float y0, float x1, float y1, float width, Color color);
    void polyline(const float *xy, size_t points, float width, Color color);
    // A ring sector: angles in radians, 0 pointing up, increasing clockwise.
    void arc(float cx, float cy, float radius, float width, float from, float to, Color color);
    // y is the text's baseline; returns the text's width. Distance-field text
    // without hinting: keep it at 11 px or more on 1x screens.
    float text(float x, float y, float size, std::string_view text, Color color, Align align = Align::left);
    float measure(float size, std::string_view text) const;
    void flush(const wgpu::RenderPassEncoder &pass);

private:
    struct Instance
    {
        float rect[4];   // x, y, width, height (pixels)
        float uv[4];     // atlas rectangle, for glyphs
        float fill[4];
        float stroke[4]; // border color
        float shape[4];  // kind (0 box, 1 glyph, 2 line, 3 arc), then per kind: see draw2d.cpp
    };
    struct Glyph
    {
        float u0, v0, u1, v1; // atlas rectangle, 0..1
        float x, y, w, h;     // quad relative to the pen, at bakeSize
        float advance;        // at bakeSize
    };
    static constexpr float bakeSize = 48; // pixels the atlas is rendered at; distance fields scale up well
    static constexpr int atlasSize = 1024;

    struct LineStyle
    {
        float color[4];
        float halfWidth, pad[3];
    };
    struct LinePoint
    {
        float x, y, style, start; // start = 1 on each polyline's first point
    };

    wgpu::Device device;
    wgpu::RenderPipeline pipeline, linePipeline;
    wgpu::Buffer uniforms, instances, lineStyles, linePoints;
    wgpu::BindGroup bindGroup, lineBindGroup;
    size_t capacity = 0, styleCapacity = 0, pointCapacity = 0;
    std::vector<LineStyle> styles;
    std::vector<LinePoint> points;
    float size[2] = {1, 1};
    Glyph glyphs[96] = {}; // ' ' .. '~'
    std::vector<Instance> batch;
};

}
