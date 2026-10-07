// char-spectrum's interface: the live spectrum, drawn by one fragment shader,
// with its scales and the Smoothing readout drawn on top by draw2d, in the
// manner of Slide: hairlines and ticks. The spectrum arrives from the
// plugin as "spectrum" messages; Smoothing edits the plugin's setting (see
// plugin.cpp).
#include "draw2d/ui.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <string_view>

namespace {

// One full-screen triangle; everything is computed per pixel.
constexpr const char *shader = R"(
struct Uniforms {
    size: vec4f,       // width, height (pixels), scale, unused
    color: vec4f,      // the spectrum's colour
    background: vec4f,
    faint: vec4f,      // the line's colour at the floor
    ranges: vec4f,     // lowest Hz, highest Hz, unused, unused
    plot: vec4f,       // where 0 dB, the floor and the frequency range fall: left, top, right, bottom (pixels)
};
@group(0) @binding(0) var<uniform> u: Uniforms;
// The live spectrum from the plugin: bands in dB, log-spaced over the plot.
@group(0) @binding(1) var<storage, read> spectrum: array<f32>;
fn bandAt(x: f32) -> f32 { // fractional band index at a pixel x; band d covers [d, d+1)/n
    let n = f32(arrayLength(&spectrum));
    return clamp(clamp((x - u.plot.x) / (u.plot.z - u.plot.x), 0.0, 1.0) * n - 0.5, 0.0, n - 1.0);
}
fn band(i: i32) -> f32 {
    return spectrum[u32(clamp(i, 0, i32(arrayLength(&spectrum)) - 1))];
}
// The tangent at a band for a monotone curve: flat at peaks and dips, so
// the curve never overshoots the bands (Fritsch-Carlson).
fn tangent(before: f32, after: f32) -> f32 {
    if (before * after <= 0.0) { return 0.0; }
    return 2.0 / (1.0 / before + 1.0 / after);
}
fn spectrumDb(x: f32) -> f32 {
    // A smooth curve through the bands around this pixel.
    let t = bandAt(x);
    let i = i32(floor(t));
    let f = t - floor(t);
    let p0 = band(i - 1);
    let p1 = band(i);
    let p2 = band(i + 1);
    let p3 = band(i + 2);
    let m1 = tangent(p1 - p0, p2 - p1);
    let m2 = tangent(p2 - p1, p3 - p2);
    let f2 = f * f;
    let f3 = f2 * f;
    return (2.0 * f3 - 3.0 * f2 + 1.0) * p1 + (f3 - 2.0 * f2 + f) * m1 + (-2.0 * f3 + 3.0 * f2) * p2 + (f3 - f2) * m2;
}
// Distance from a point to the segment a-b.
fn segmentDistance(p: vec2f, a: vec2f, b: vec2f) -> f32 {
    let ab = b - a;
    let t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - (a + t * ab));
}

@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

fn xOf(hz: f32) -> f32 { // logarithmic: every octave the same width
    let r = u.plot;
    return r.x + (r.z - r.x) * log(hz / u.ranges.x) / log(u.ranges.y / u.ranges.x);
}
// The level scale: linear in dB, 0 dBFS at the top, the floor at the bottom
// edge. Quieter levels fall below it, out of sight.
const floorDb = -100.0;
fn levelY(dbfs: f32) -> f32 {
    let r = u.plot;
    return r.y + (r.w - r.y) * max(dbfs, 2.0 * floorDb) / floorDb;
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let px = pos.xy;
    let s = u.size.z;
    let r = u.plot;
    let across = step(r.x, px.x) * step(px.x, r.z);
    let inside = across * step(r.y, px.y) * step(px.y, r.w);
    var color = u.background.rgb;

    // Faint guides at the labelled frequencies and every 10 dB.
    var grid = 0.0;
    let gridHz = array<f32, 10>(20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0);
    for (var i = 0; i < 10; i++) {
        grid = max(grid, 1.0 - smoothstep(0.0, s, abs(px.x - xOf(gridHz[i]))));
    }
    for (var db = 0.0; db >= floorDb; db -= 10.0) {
        grid = max(grid, 1.0 - smoothstep(0.0, s, abs(px.y - levelY(db))));
    }
    color += vec3f(0.035) * grid * inside;

    // The spectrum: a faint fill below it and the line itself, which greys
    // out towards the floor and falls through it, as in Pro-Q, past the
    // frequency labels below.
    let db = spectrumDb(px.x);
    let ys = levelY(db);
    // The true distance to the curve, drawn as segments, looking a few
    // pixels each side, so steep sides stay solid.
    var distance = 1e9;
    var previous = vec2f(px.x - 4.0, levelY(spectrumDb(px.x - 4.0)));
    for (var k = -3.0; k <= 4.0; k += 1.0) {
        let next = vec2f(px.x + k, levelY(spectrumDb(px.x + k)));
        distance = min(distance, segmentDistance(px, previous, next));
        previous = next;
    }
    let loud = smoothstep(floorDb, floorDb + 30.0, db);
    let below = smoothstep(ys - 0.5, ys + 0.5, px.y) * across;
    // The fill's brightness follows height on the plot, not distance from the
    // line, so it is the same in every column and doesn't streak.
    let height = 1.0 - clamp((px.y - r.y) / (r.w - r.y), 0.0, 1.0);
    color += u.color.rgb * below * (0.03 + 0.12 * height * height);
    let lineColor = mix(u.faint.rgb, u.color.rgb, loud);
    color = mix(color, lineColor, (1.0 - smoothstep(0.5 * s, 1.3 * s, distance)) * across);

    return vec4f(color, 1.0);
}
)";

struct Uniforms
{
    float size[4];
    float color[4];
    float background[4];
    float faint[4];
    float ranges[4];
    float plot[4];
};

constexpr float minHz = 10, maxHz = 30000; // past the audible range each side, as Pro-Q
constexpr float floorDb = -100;

float hzToUnit(float hz) { return std::log(hz / minHz) / std::log(maxHz / minHz); }

// Slide's dark theme.
constexpr gpu::Color rgb(unsigned hex)
{
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f};
}
constexpr gpu::Color background = rgb(0x2b2b2b), ink = rgb(0xdedede), ink2 = rgb(0xbdbdbd), dim = rgb(0x909090),
                     tick = rgb(0x767676), amber = rgb(0xddc07e);

class AnalyzerView : public gpu::View
{
public:
    void prepare(const wgpu::Device &newDevice, wgpu::TextureFormat format) override
    {
        device = newDevice;
        createPipeline(format);
        ui.draw.prepare(device, format, gpu::loadAsset("/fonts/IBMPlexMono-Medium.ttf"));
        params.send = send;
        ui.resizeTo = resizeTo;
        send(core::Value::Map{{"type", "ready"}});
    }

    void receive(const core::Value &message) override
    {
        if (params.receive(message) || message["type"].text() != "spectrum") return;
        auto *db = std::get_if<core::Value::Array>(&message["db"].data);
        const gpu::Param *smoothing = params.find("Smoothing");
        if (!db || !smoothing) return;
        // Levels fall back with a time constant Smoothing sets, from 10 ms
        // to 5 s, and rise eight times faster than they fall
        // (messages arrive about 60 times a second).
        const float fallSeconds = 0.01f * std::pow(500.0f, float(smoothing->value) / 100);
        const float fall = 1 - std::exp(-1 / (60 * fallSeconds));
        const float rise = 1 - std::exp(-8 / (60 * fallSeconds));
        for (size_t i = 0; i < spectrum.size() && i < db->size(); ++i)
        {
            const float level = float((*db)[i].number(-140));
            spectrum[i] += (level - spectrum[i]) * (level > spectrum[i] ? rise : fall);
        }
        // What's drawn: each band averaged a little with its neighbours
        // (in power), which calms the noise without blunting peaks much.
        auto power = [](float db) { return std::pow(10.0f, db / 10); };
        for (size_t i = 0; i < spectrum.size(); ++i)
        {
            const float left = spectrum[i > 0 ? i - 1 : i], right = spectrum[std::min(i + 1, spectrum.size() - 1)];
            shown[i] = 10 * std::log10(0.25f * power(left) + 0.5f * power(spectrum[i]) + 0.25f * power(right) + 1e-14f);
            // Tilt 4.5 dB per octave, pivoting at 1 kHz, as Pro-Q does by default:
            // music tends to fall off about this fast, so it looks roughly level.
            const float hz = minHz * std::pow(maxHz / minHz, (i + 0.5f) / shown.size());
            shown[i] += 4.5f * std::log2(hz / 1000);
        }
    }
    void pointer(const PointerEvent &event) override { ui.pointer(event); }
    bool key(Key key, unsigned modifiers) override { return ui.key(key, modifiers); }

    void draw(const wgpu::RenderPassEncoder &pass, float width, float height, float scale) override
    {
        gpu::Param *smoothing = params.find("Smoothing");
        if (!smoothing) return; // until the plugin has sent its settings
        ui.begin(width, height, scale);
        const float s = ui.s;
        const float plot[4] = {0, 12 * s, width, height - 26 * s};
        drawSpectrum(pass, width, height, plot);
        drawScales(plot, height);
        drawSmoothing(*smoothing, width - 12 * s, 24 * s);
        ui.resizeGrip(width, height);
        ui.end(pass);
    }

private:
    // Text on whole pixels, which keeps it sharp. size is in points.
    float text(float x, float y, float size, std::string_view label, gpu::Color color,
               gpu::Draw2D::Align align = gpu::Draw2D::Align::left)
    {
        return ui.draw.text(std::round(x), std::round(y), size * ui.s, label, color, align);
    }

    // On the plot: frequencies along the bottom edge, levels in dBFS up the left.
    void drawScales(const float plot[4], float height)
    {
        const float s = ui.s;
        auto xOf = [&](float hz) { return plot[0] + (plot[2] - plot[0]) * hzToUnit(hz); };
        auto levelY = [&](float dbfs) { return plot[1] + (plot[3] - plot[1]) * dbfs / floorDb; };

        for (float decade = 10; decade < maxHz; decade *= 10)
            for (int k = 1; k < 10 && decade * k <= maxHz; ++k)
                ui.draw.line(xOf(decade * k), height, xOf(decade * k), height - 3 * s, s, tick);
        float lastRight = 30 * s; // clear of the level labels
        for (auto [hz, label] : {std::pair{20.0f, "20"}, {50.0f, "50"}, {100.0f, "100"}, {200.0f, "200"}, {500.0f, "500"},
                                 {1000.0f, "1k"}, {2000.0f, "2k"}, {5000.0f, "5k"}, {10000.0f, "10k"}, {20000.0f, "20k"}})
        {
            const float x = xOf(hz), half = ui.draw.measure(12 * s, label) / 2;
            ui.draw.line(x, height, x, height - 6 * s, s, ink2);
            if (x - half < lastRight + 8 * s) continue; // too close to the last label
            text(x, height - 10 * s, 12, label, ink2, gpu::Draw2D::Align::center);
            lastRight = x + half;
        }

        float lastY = -1e9f;
        for (float dbfs = 0; dbfs >= floorDb; dbfs -= 5)
        {
            const float y = levelY(dbfs);
            const bool major = std::fmod(dbfs, 10.0f) == 0;
            ui.draw.line(0, y, (major ? 6 : 3) * s, y, s, major ? ink2 : tick);
            if (!major || y - lastY < 16 * s) continue;
            char label[8];
            std::snprintf(label, sizeof(label), "%.0f", dbfs);
            text(9 * s, y + 4.5f * s, 12, label, ink2);
            lastY = y;
        }
    }

    // Smoothing as a readout, right-aligned at `right`: drag it up or down,
    // scroll over it, or double-click to reset.
    void drawSmoothing(gpu::Param &p, float right, float baseline)
    {
        const float s = ui.s;
        char value[16];
        std::snprintf(value, sizeof(value), "%3.0f", p.value);
        const float valueWidth = ui.draw.measure(15 * s, "100"), nameWidth = ui.draw.measure(15 * s, p.name) + 8 * s;
        const float left = right - valueWidth - nameWidth;
        const gpu::Ui::Area area = ui.area(&p, left - 4 * s, baseline - 16 * s, right - left + 8 * s, 22 * s);
        if (area.pressed) params.begin(p), dragFromY = area.y, dragFromValue = p.value;
        if (area.held) params.change(p, dragFromValue + (dragFromY - area.y) / (200 * s) * (p.max - p.min));
        if (area.released) params.end(p);
        if (area.doubleClicked || area.wheel)
        {
            params.begin(p);
            params.change(p, area.doubleClicked ? p.defaultValue : p.value + area.wheel * 2);
            params.end(p);
        }
        text(left, baseline, 15, p.name, ink2);
        text(right, baseline, 15, value, area.held ? amber : ink, gpu::Draw2D::Align::right);
    }

    // The spectrum: one full-screen triangle through the shader above.
    void createPipeline(wgpu::TextureFormat format)
    {
        wgpu::ShaderSourceWGSL source;
        source.code = shader;
        wgpu::ShaderModuleDescriptor moduleDescriptor;
        moduleDescriptor.nextInChain = &source;
        wgpu::ShaderModule module = device.CreateShaderModule(&moduleDescriptor);
        wgpu::ColorTargetState target;
        target.format = format;
        wgpu::FragmentState fragment;
        fragment.module = module;
        fragment.targetCount = 1;
        fragment.targets = &target;
        wgpu::RenderPipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.vertex.module = module;
        pipelineDescriptor.fragment = &fragment;
        pipeline = device.CreateRenderPipeline(&pipelineDescriptor);

        wgpu::BufferDescriptor bufferDescriptor;
        bufferDescriptor.size = sizeof(Uniforms);
        bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms = device.CreateBuffer(&bufferDescriptor);
        bufferDescriptor.size = sizeof(float) * spectrum.size();
        bufferDescriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        spectrumBuffer = device.CreateBuffer(&bufferDescriptor);

        wgpu::BindGroupEntry entries[2];
        entries[0].binding = 0;
        entries[0].buffer = uniforms;
        entries[1].binding = 1;
        entries[1].buffer = spectrumBuffer;
        wgpu::BindGroupDescriptor groupDescriptor;
        groupDescriptor.layout = pipeline.GetBindGroupLayout(0);
        groupDescriptor.entryCount = 2;
        groupDescriptor.entries = entries;
        bindGroup = device.CreateBindGroup(&groupDescriptor);
    }

    void drawSpectrum(const wgpu::RenderPassEncoder &pass, float width, float height, const float plot[4])
    {
        const Uniforms data{{width, height, ui.s, 0},
                            {amber.r, amber.g, amber.b, 1},
                            {background.r, background.g, background.b, 1},
                            {dim.r, dim.g, dim.b, 1},
                            {minHz, maxHz, 0, 0},
                            {plot[0], plot[1], plot[2], plot[3]}};
        device.GetQueue().WriteBuffer(uniforms, 0, &data, sizeof(data));
        device.GetQueue().WriteBuffer(spectrumBuffer, 0, shown.data(), sizeof(float) * shown.size());
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroup);
        pass.Draw(3);
    }

    wgpu::Device device;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer uniforms;
    wgpu::BindGroup bindGroup;
    wgpu::Buffer spectrumBuffer;
    std::vector<float> spectrum = std::vector<float>(512, -140.0f); // dB per band, smoothed over time
    std::vector<float> shown = std::vector<float>(512, -140.0f);    // and across neighbours: what's drawn
    gpu::Params params; // the plugin's settings (not CLAP parameters; see plugin.cpp)
    gpu::Ui ui;
    float dragFromY = 0;
    double dragFromValue = 0;
};

}

std::unique_ptr<gpu::View> gpu::createView() { return std::make_unique<AnalyzerView>(); }
