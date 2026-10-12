// char-spectrum's interface: the live spectrum, drawn by chardraw's Spectrum
// view (libs/chardraw/views.h), with the Smoothing readout over its top right
// corner, laid out with Clay, in the manner of Slide: hairlines and ticks in
// its dark theme, amber, and IBM Plex Mono. The spectrum arrives from the
// plugin as "spectrum" messages; the readout edits the plugin's setting (see
// plugin.cpp).
#include "chardraw/ui.h"
#include "chardraw/views.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float minHz = 10, maxHz = 30000; // past the audible range each side, as Pro-Q
constexpr int bandCount = 512;             // as the plugin sends them
// Tilted 4.5 dB per octave around 1 kHz, as Pro-Q is by default: music falls
// off about this fast, so it looks roughly level.
constexpr float tilt = 4.5f;

// Slide's dark theme.
constexpr gpu::Color rgb(unsigned hex)
{
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f};
}
constexpr gpu::Color background = rgb(0x2b2b2b), ink = rgb(0xdedede), ink2 = rgb(0xbdbdbd), dim = rgb(0x909090),
                     tick = rgb(0x767676), grid = rgb(0x343434), amber = rgb(0xddc07e);

class AnalyzerView : public gpu::View
{
public:
    AnalyzerView()
    {
        // The bands' centres: evenly spaced on a log scale, as the plugin's analysis gives them.
        for (int i = 0; i < bandCount; ++i) hz[size_t(i)] = minHz * std::pow(maxHz / minHz, (i + 0.5f) / bandCount);
    }

    void prepare(const wgpu::Device &device, wgpu::TextureFormat format) override
    {
        ui.draw.prepare(device, format, gpu::loadAsset("/fonts/IBMPlexMono-Medium.ttf")); // font 0, for labels and values
        ui.theme.background = background, ui.theme.text = ink, ui.theme.dim = dim, ui.theme.edge = tick, ui.theme.accent = amber;
        ui.theme.textSize = 15;
        params.send = send;
        ui.lockPointer = lockPointer;
        ui.copyText = copyText;
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
        }
    }
    void pointer(const PointerEvent &event) override { ui.pointer(event); }
    bool key(Key key, unsigned modifiers) override { return ui.key(key, modifiers); }
    // Smoothing's value typed in (Enter or a digit with its readout focused).
    bool text(const char *utf8) override { return ui.text(utf8); }
    bool editingText() const override { return ui.editingText(); }
    Caret textCaret() const override { return ui.textCaret(); }
    // Screen readers: the readout describes itself, and the spectrum says what it shows.
    const gpu::Accessibility *accessibility() const override { return ui.accessibility(); }
    void accessAction(uint64_t id, gpu::Accessible::Action action, double value) override { ui.accessAction(id, action, value); }

    // The frame is built in record, so the presenter can skip one that looks
    // like the last; draw only flushes it.
    void draw(const wgpu::RenderPassEncoder &pass, float, float, float) override { ui.flush(pass); }
    bool record(float width, float height, float scale) override
    {
        gpu::Param *smoothing = params.find("Smoothing");
        if (!smoothing) return false; // until the plugin has sent its settings
        ui.begin(width, height, scale);

        // The layout, in points: the spectrum fills the window, the readout
        // floating over its top right corner. The scales' labels are lighter
        // than the curve's floor, and so is the readout's.
        const gpu::SpectrumView view{.minFrequency = minHz, .maxFrequency = maxHz, .tilt = tilt, .gridColour = grid, .labelColour = ink2};
        void *plot = ui.paint([this, view](gpu::Rect r) { gpu::spectrum(ui, "Spectrum", shown.data(), hz.data(), shown.size(), r, view); });
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .custom = {.customData = plot}})
        {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}},
                  .floating = {.offset = {-8, 7}, .attachPoints = {CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP},
                               .attachTo = CLAY_ATTACH_TO_PARENT}})
            {
                ui.theme.dim = ink2;
                ui.readout(params, smoothing, "Smoothing", "%3.0f");
                ui.theme.dim = dim;
            }
        }

        ui.resizeGrip(width, height);
        return ui.record();
    }

private:
    std::vector<float> hz = std::vector<float>(bandCount);                 // each band's centre
    std::vector<float> spectrum = std::vector<float>(bandCount, -140.0f); // dB per band, smoothed over time
    std::vector<float> shown = std::vector<float>(bandCount, -140.0f);    // and across neighbours: what's drawn
    gpu::Params params; // the plugin's settings (not CLAP parameters; see plugin.cpp)
    gpu::Ui ui;
};

}

std::unique_ptr<gpu::View> gpu::createView() { return std::make_unique<AnalyzerView>(); }
