#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>
#include "chardsp_AnalysisBuffer.h"
#include "chardsp_Spectrum.h"

// A spectrum for display, after the method of Signalsmith Audio's analyser
// (github.com/Signalsmith-Audio/basics, analyser.h, MIT; Copyright 2022-2025
// Signalsmith Audio / Geraint Luff; full notice: licenses/SignalsmithBasics.txt),
// at three resolutions:
//
// - Blackman-Harris-windowed FFTs (chardsp::Spectrum, on Signalsmith's
//   real-input FFT) every few milliseconds: a long window (16384 samples) for the
//   lows, where notes are close together in Hz, a medium one (4096) for the
//   middle, and a short one (2048) for the highs, so they still react within
//   tens of milliseconds. Neighbouring resolutions are blended over an
//   octave: 250-500 Hz and 1-2 kHz.
// - Analysis bands 1/24 octave apart, but never narrower than the window
//   resolves nor wider than one window bin, so no frequency falls between them.
// - For each band, the FFT's complex value at the band's centre goes through
//   a two-pole complex resonator tuned to that frequency, twice as wide as the
//   band spacing. That smooths each band over time and makes it a smooth,
//   overlapping band-pass: narrow bands respond slower, wide ones faster.
// - Levels are in dB relative to a full-scale sine, which reads 0 dB.
//
// The audio thread only copies both channels into an AnalysisBuffer
// (chardsp_AnalysisBuffer.h). analyze() runs on another thread and catches up
// on every hop since its last call.
struct AnalyzerSpectrum
{
    static constexpr int hop = 240; // 5 ms at 48 kHz: the short window's step

    // While nothing pushes.
    AnalyzerSpectrum() { input.prepare (2, ringSize); }

    // Audio thread: never blocks or allocates.
    void push (const float* left, const float* right, uint32_t frames) noexcept
    {
        const float* channels[] = { left, right };
        input.push (channels, 2, int (frames));
    }

    // Other thread: `bandCount` display bands in dB from minHz to maxHz, evenly
    // spaced on a logarithmic frequency scale (see scale()).
    // Returns false until the short window's worth of samples has arrived; the
    // longer windows start from silence and fill in over their length.
    bool analyze (float sampleRate, float minHz, float maxHz, int bandCount, std::vector<double>& display)
    {
        const uint32_t end = input.getWritePosition();
        if (end < uint32_t (Short::windowSize) || sampleRate <= 0)
            return false;
        if (sampleRate != preparedRate)
        {
            preparedRate = sampleRate;
            low.prepare (sampleRate, 10, 500);
            mid.prepare (sampleRate, 250, 2000);
            high.prepare (sampleRate, 1000, sampleRate / 2);
            started = false;
        }

        // Every hop boundary since the last call. At the start, or after a
        // long gap (the page was closed), only the last 100 ms: resonators
        // forget older input. (Sample positions are unsigned and may wrap,
        // so compare distances.)
        if (! started || end - input.getPosition() > uint32_t (20 * Long::hop))
        {
            const uint32_t from = end > uint32_t (20 * Long::hop) ? end - 20 * Long::hop : uint32_t (hop);
            const uint32_t first = (from + Long::hop - 1) / Long::hop * Long::hop; // on every resolution's hop grid
            input.restart (hop, first - hop);
            started = true;
        }
        // Only up to `end`: a writer running faster than real time (an
        // offline render) would otherwise keep this loop going for ever.
        while (int32_t (end - input.getPosition()) >= hop && input.nextHop (Short::windowSize, ringSize))
        {
            const uint32_t next = input.getPosition();
            high.step (input, mixed);
            if (next % Medium::hop == 0)
                mid.step (input, mixed);
            if (next % Long::hop == 0)
                low.step (input, mixed);
        }

        // Each resolution drawn across the display, then blended by frequency.
        low.reduce (minHz, maxHz, bandCount, lowDisplay);
        mid.reduce (minHz, maxHz, bandCount, midDisplay);
        high.reduce (minHz, maxHz, bandCount, display);
        for (int d = 0; d < bandCount; ++d)
        {
            const double hzAtBand = scaleInverse (minHz, maxHz, (d + 0.5) / bandCount);
            const double toMid = std::clamp (std::log2 (hzAtBand / 250), 0.0, 1.0);   // 0 at 250 Hz, 1 at 500
            const double toHigh = std::clamp (std::log2 (hzAtBand / 1000), 0.0, 1.0); // 0 at 1 kHz, 1 at 2 kHz
            const double lowMid = lowDisplay[d] * (1 - toMid) + midDisplay[d] * toMid;
            display[d] = lowMid * (1 - toHigh) + display[d] * toHigh;
            if (hzAtBand > sampleRate / 2) // nothing above Nyquist: the curve drops to the floor
                display[d] = -140.0;
        }
        return true;
    }

    // The display's frequency scale: logarithmic, every octave the same width.
    static double scale (double hz) { return std::log (hz); }

private:
    static constexpr int ringSize = 65536;

    // The newest `count` samples up to the hop's end, the mix of both
    // channels; false if the writer overtook the read.
    static bool readMix (const chardsp::AnalysisBuffer<float>& input, int count, std::vector<float>& mix, std::vector<float>& right)
    {
        mix.resize (size_t (count));
        right.resize (size_t (count));
        const bool whole = input.read (0, count, mix.data()) && input.read (1, count, right.data());
        for (int i = 0; i < count; ++i)
        {
            // A NaN or infinity would stay in the resonators for good: read as silence.
            const float sample = 0.5f * (mix[size_t (i)] + right[size_t (i)]);
            mix[size_t (i)] = std::isfinite (sample) ? sample : 0.0f;
        }
        return whole;
    }
    static constexpr double pi = 3.14159265358979323846;

    static double scaleInverse (double minHz, double maxHz, double unit)
    {
        return std::exp (scale (minHz) + unit * (scale (maxHz) - scale (minHz)));
    }

    // One resolution: a window size, its zero-padded FFT, and how often it runs.
    template <int WindowSize, int FftSize, int Hop>
    struct Resolution
    {
        static constexpr int windowSize = WindowSize, fftSize = FftSize, hop = Hop;

        // Bands from fromHz to toHz. Resonators run once per hop: a pole at each
        // band's frequency (the FFT phase advances by that much per hop),
        // damped by twice the band's width, so neighbouring bands overlap and a
        // sine between two centres still reads nearly its full level.
        void prepare (float rate, double fromHz, double toHz)
        {
            sampleRate = rate;
            // Blackman-Harris (4 terms): sidelobes 92 dB down, so each tone draws
            // as a rounded bell straight down to the floor, with no skirts.
            // Periodic, zero-padded to twice its length, and rotated so the
            // window's centre is sample 0 ("zero phase"): then a sine's
            // neighbouring bins agree in phase and interpolate cleanly.
            spectrum.prepare ({ .size = windowSize, .fftSize = fftSize, .window = chardsp::SpectrumWindow::blackmanHarris,
                                .periodic = true });

            const double windowBinHz = double (rate) / windowSize, step = std::pow (2.0, 1.0 / 24);
            hz.clear();
            widthHz.clear();
            for (double f = fromHz; f < toHz && f < rate / 2;)
            {
                const double width = std::clamp (f * (step - 1), std::min (8.0, windowBinHz), windowBinHz);
                hz.push_back (f);
                widthHz.push_back (width);
                f += width;
            }
            const double hopRate = double (rate) / hop;
            twist.resize (hz.size());
            gain.resize (hz.size());
            for (size_t b = 0; b < hz.size(); ++b)
            {
                twist[b] = std::exp (std::complex<double> (-2 * pi * 2 * widthHz[b] / hopRate, 2 * pi * hz[b] / hopRate));
                gain[b] = 1 - std::abs (twist[b]);
            }
            state1.assign (hz.size(), 0.0);
            state2.assign (hz.size(), 0.0);
        }

        // One analysis frame ending at the buffer's current hop. A frame the
        // writer overtook while it was read is left out.
        void step (const chardsp::AnalysisBuffer<float>& input, std::vector<float>& block)
        {
            if (! readMix (input, windowSize, block, other))
                return;
            spectrum.process (block.data());
            const std::complex<float>* bins = spectrum.getBins();
            const double binHz = sampleRate / fftSize;
            for (size_t b = 0; b < hz.size(); ++b)
            {
                // The FFT's value at the band's centre, interpolated between bins.
                const double bin = std::min (hz[b] / binHz, fftSize / 2 - 1.001);
                const int k = int (bin);
                const std::complex<double> input = std::complex<double> (bins[k]) * (k + 1 - bin)
                                                 + std::complex<double> (bins[k + 1]) * (bin - k);
                state1[b] = state1[b] * twist[b] + input * gain[b];
                state2[b] = state2[b] * twist[b] + state1[b] * gain[b];
            }
        }

        // Display bands, each the loudest band within it; display bands with
        // no band centre (narrower than the band spacing) are filled with a
        // smooth curve (Catmull-Rom) through the ones that have one.
        void reduce (double minHz, double maxHz, int bandCount, std::vector<double>& display)
        {
            display.assign (bandCount, -140.0);
            for (size_t b = 0; b < hz.size(); ++b)
            {
                const int d = int (std::floor ((scale (hz[b]) - scale (minHz)) / (scale (maxHz) - scale (minHz)) * bandCount));
                if (d < 0 || d >= bandCount)
                    continue;
                const double amplitude = std::abs (state2[b]) * spectrum.getScale();
                display[d] = std::max (display[d], 20 * std::log10 (amplitude + 1e-7));
            }
            known.clear();
            for (int d = 0; d < bandCount; ++d)
                if (display[d] > -140.0)
                    known.push_back (d);
            for (size_t i = 0; i + 1 < known.size(); ++i)
            {
                const int d0 = known[i], d1 = known[i + 1];
                const int dPrev = i > 0 ? known[i - 1] : d0, dNext = i + 2 < known.size() ? known[i + 2] : d1;
                const double y0 = display[d0], y1 = display[d1];
                const double t0 = (y1 - display[dPrev]) / std::max (1, d1 - dPrev) * (d1 - d0);
                const double t1 = (display[dNext] - y0) / std::max (1, dNext - d0) * (d1 - d0);
                for (int g = d0 + 1; g < d1; ++g)
                {
                    const double t = double (g - d0) / (d1 - d0), t2 = t * t, t3 = t2 * t;
                    display[g] = (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * t0 + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * t1;
                }
            }
            // Beyond this resolution's first and last bands, hold their levels,
            // so the blend between resolutions has something to blend.
            if (!known.empty())
            {
                std::fill (display.begin(), display.begin() + known.front(), display[known.front()]);
                std::fill (display.begin() + known.back() + 1, display.end(), display[known.back()]);
            }
        }

        float sampleRate = 48000;
        chardsp::Spectrum<float> spectrum;
        std::vector<float> other; // the right channel while mixing
        std::vector<double> hz, widthHz, gain;
        std::vector<std::complex<double>> twist, state1, state2;
        std::vector<int> known;
    };

    using Long = Resolution<16384, 32768, 8 * hop>; // 340 ms window, every 40 ms
    using Medium = Resolution<4096, 8192, 2 * hop>; // 85 ms window, every 10 ms
    using Short = Resolution<2048, 4096, hop>;      // 43 ms window, every 5 ms

    chardsp::AnalysisBuffer<float> input;
    // The rest is used only by analyze().
    float preparedRate = 0;
    bool started = false;
    std::vector<float> mixed; // a frame of the mix
    Long low;
    Medium mid;
    Short high;
    std::vector<double> lowDisplay, midDisplay;
};
