// The spectrum: sines read their level wherever they fall, the display has
// no needles between bands, and it reacts in tens of milliseconds.
#include "../chardsp_Spectrum.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

constexpr float sampleRate = 48000, minHz = 20, maxHz = 20000;
constexpr int bandCount = 256;
const double pi = 3.14159265358979;

static int bandOf(double hz)
{
    using S = chardsp::Spectrum;
    return int((S::scale(hz) - S::scale(minHz)) / (S::scale(maxHz) - S::scale(minHz)) * bandCount);
}

// Streams a signal in 512-sample blocks, analyzing 60 times a second; calls
// `each` with the time and bands after every analysis.
static std::vector<double> run(const std::function<float(int)> &signal, float seconds,
                               const std::function<void(double, const std::vector<double> &)> &each = {})
{
    auto spectrum = std::make_unique<chardsp::Spectrum>();
    std::vector<double> bands;
    float block[512];
    int sinceUpdate = 0;
    for (int n = 0; n < int(seconds * sampleRate); n += 512)
    {
        for (int i = 0; i < 512; ++i) block[i] = signal(n + i);
        spectrum->push(block, block, 512);
        if ((sinceUpdate += 512) >= sampleRate / 60)
        {
            sinceUpdate = 0;
            if (spectrum->analyze(sampleRate, minHz, maxHz, bandCount, bands) && each) each((n + 512) / sampleRate, bands);
        }
    }
    return bands;
}

static double peakNear(const std::vector<double> &bands, double hz)
{
    const int b = bandOf(hz);
    return std::max({bands[b - 1], bands[b], bands[std::min(b + 1, bandCount - 1)]});
}

int main()
{
    // Levels: 60 sine frequencies from 30 Hz to 18 kHz, amplitude 0.5 (-6.02 dB).
    double worst = 0;
    for (int i = 0; i < 60; ++i)
    {
        const double hz = 30 * std::pow(600.0, i / 59.0);
        const auto bands = run([&](int n) { return float(0.5 * std::sin(2 * pi * hz * n / sampleRate)); }, 0.5f);
        worst = std::max(worst, std::abs(peakNear(bands, hz) - -6.02));
    }
    std::printf("sines 30 Hz to 18 kHz: worst level error %.2f dB\n", worst);
    assert(worst < 2.5); // a sine between two bands' centres reads a little low

    // Leakage: an octave away from a 1 kHz sine, far below it.
    const auto sine1k = run([&](int n) { return float(0.5 * std::sin(2 * pi * 1000 * n / sampleRate)); }, 0.5f);
    std::printf("1 kHz sine: %.1f dB, an octave up %.1f dB\n", peakNear(sine1k, 1000), peakNear(sine1k, 2000));
    assert(peakNear(sine1k, 2000) < peakNear(sine1k, 1000) - 40);

    // Timing: silence, a sine from 0.3 s to 0.6 s, silence again. The highs
    // (short window) react within tens of milliseconds; the middle (medium
    // window, sharper peaks) takes a little longer.
    for (auto [hz, limit] : {std::pair{5000.0, 0.06}, {1000.0, 0.1}})
    {
        double reached = -1, faded = -1;
        run([&](int n) {
            const double t = n / sampleRate;
            return t >= 0.3 && t < 0.6 ? float(0.5 * std::sin(2 * pi * hz * n / sampleRate)) : 0.0f;
        }, 1.0f, [&](double t, const std::vector<double> &bands) {
            if (t > 0.3 && reached < 0 && peakNear(bands, hz) > -6.02 - 3) reached = t - 0.3;
            if (t > 0.6 && faded < 0 && peakNear(bands, hz) < -6.02 - 20) faded = t - 0.6;
        });
        std::printf("%5.0f Hz tone: within 3 dB %.0f ms after it starts, 20 dB down %.0f ms after it stops\n",
                    hz, reached * 1000, faded * 1000);
        assert(reached > 0 && reached < limit && faded > 0 && faded < limit + 0.04);
    }

    // Shape: a pure tone draws one smooth peak, falling steadily on both sides
    // for 20 dB, with no needles or plateaus on top.
    for (double hz : {150.0, 1000.0, 6000.0})
    {
        const auto bands = run([&](int n) { return float(0.5 * std::sin(2 * pi * hz * n / sampleRate)); }, 0.5f);
        int top = bandOf(hz) - 2;
        for (int b = top; b <= bandOf(hz) + 2; ++b) if (bands[b] > bands[top]) top = b;
        bool smooth = true;
        for (int side : {-1, 1})
            for (int b = top; bands[top] - bands[b + side] < 20; b += side)
                smooth = smooth && bands[b + side] <= bands[b] + 0.01;
        std::printf("%5.0f Hz tone: one smooth peak: %s\n", hz, smooth ? "yes" : "NO");
        assert(smooth);
    }
}
