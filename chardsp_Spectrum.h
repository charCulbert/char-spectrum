// From github.com/charCulbert/chardsp at 87bfc1e (MIT), its FFT include changed.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <type_traits>
#include <vector>

#include "dsp/fft.h" // chardsp has "signalsmith-dsp/fft.h"; this example includes Signalsmith DSP as dsp/

namespace chardsp
{

enum class SpectrumWindow
{
    rectangular,
    hann,
    hamming,
    blackman,
    blackmanHarris // four terms: sidelobes 92 dB down
};

// A spectrum of blocks of `size` samples, windowed and zero-padded to fftSize
// (a power of two, at least size; 0 is size itself). A symmetric window has
// both ends on the block's ends; a periodic one is one period of the window
// repeating, so blocks overlapping by a suitable hop sum evenly.
struct SpectrumDesign
{
    int size = 2048;
    int fftSize = 0;
    SpectrumWindow window = SpectrumWindow::hann;
    bool periodic = false;
};

inline bool operator== (const SpectrumDesign& a,
                        const SpectrumDesign& b) noexcept
{
    return a.size == b.size && a.fftSize == b.fftSize
        && a.window == b.window && a.periodic == b.periodic;
}

inline bool operator!= (const SpectrumDesign& a,
                        const SpectrumDesign& b) noexcept
{
    return ! (a == b);
}

// A windowed FFT (Signalsmith's real FFT) of a block: bin k is at
// k · sampleRate / fftSize Hz. Time is measured from the block's middle
// sample, which the FFT sees first ("zero phase"), so a steady sine's
// neighbouring bins agree in phase and interpolate cleanly.
//
// Magnitudes are scaled by the window's coherent gain (2 / Σw), so that a
// sine of amplitude A that fits the block a whole number of times, at least
// two cycles from DC and Nyquist, reads A at its bin: exactly with a periodic
// window, and within 0.04 dB with a symmetric one (least so in short blocks),
// as tested from 16 to 1024 samples. A sine that doesn't fit the block whole
// reads lower by the window's scalloping, and its image across DC or Nyquist
// adds to or takes from it by phase: most in short blocks, near the ends and
// with the rectangular window (a 17-sample block can read 6% either way).
// Nearer the ends than two cycles, Blackman and Blackman-Harris read up to
// 2 dB off by phase. A constant reads its value at DC.
//
// prepare() allocates; the rest does not.
template <typename SampleType>
class Spectrum
{
public:
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Spectrum supports float or double");

    // False, analysing nothing and with no bins, for a size under 4 or an
    // fftSize that isn't a power of two at least as long.
    bool prepare (const SpectrumDesign& newDesign)
    {
        design = newDesign;

        if (design.fftSize == 0)
            design.fftSize = design.size;

        const int n = design.size, length = design.fftSize;

        if (n < 4 || length < n || length > (1 << 30)
            || (length & (length - 1)) != 0)
        {
            window.clear();
            bins.clear();
            return false;
        }

        window.resize (static_cast<size_t> (n));
        double sum = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double x = twoPi * i / (design.periodic ? n : n - 1);
            double w = 1.0;

            switch (design.window)
            {
                case SpectrumWindow::rectangular: break;
                case SpectrumWindow::hann: w = 0.5 - 0.5 * std::cos (x); break;
                case SpectrumWindow::hamming: w = 0.54 - 0.46 * std::cos (x); break;
                case SpectrumWindow::blackman:
                    w = 0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2 * x);
                    break;
                case SpectrumWindow::blackmanHarris:
                    w = 0.35875 - 0.48829 * std::cos (x)
                      + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
                    break;
            }

            window[static_cast<size_t> (i)] = static_cast<SampleType> (w);
            sum += window[static_cast<size_t> (i)];
        }

        // A sine of amplitude A peaks at A · Σw / 2 in the FFT.
        scale = 2.0 / sum;
        transform.setSize (static_cast<size_t> (length));
        buffer.assign (static_cast<size_t> (length), SampleType {});
        bins.assign (static_cast<size_t> (length / 2 + 1), {});
        return true;
    }

    bool isValid() const noexcept { return ! window.empty(); }
    const SpectrumDesign& getDesign() const noexcept { return design; }
    int getNumBins() const noexcept { return static_cast<int> (bins.size()); }

    // Takes in a block of getDesign().size samples.
    void process (const SampleType* block) noexcept
    {
        if (! isValid())
            return;

        const int n = design.size, length = design.fftSize;
        std::fill (buffer.begin(), buffer.end(), SampleType {});

        for (int i = 0; i < n; ++i)
            buffer[static_cast<size_t> ((i - n / 2 + length) % length)] =
                block[i] * window[static_cast<size_t> (i)];

        transform.fft (buffer.data(), bins);
        // The FFT packs Nyquist into DC's imaginary part.
        bins[static_cast<size_t> (length / 2)] = bins[0].imag();
        bins[0].imag (SampleType {});
    }

    // The FFT's bins, DC to Nyquist, as it gives them: getScale() times a
    // bin's magnitude is its amplitude (half that at DC and Nyquist).
    const std::complex<SampleType>* getBins() const noexcept
    {
        return bins.data();
    }

    double getScale() const noexcept { return scale; }

    SampleType getMagnitude (int bin) const noexcept
    {
        const bool edge = bin == 0 || bin == design.fftSize / 2;
        return static_cast<SampleType> (
            std::abs (bins[static_cast<size_t> (bin)])
            * (edge ? 0.5 * scale : scale));
    }

    // getMagnitude() for every bin, into getNumBins() values.
    void getMagnitudes (SampleType* magnitudes) const noexcept
    {
        for (int k = 0; k < getNumBins(); ++k)
            magnitudes[k] = getMagnitude (k);
    }

private:
    static constexpr double twoPi = 6.283185307179586476925286766559;

    SpectrumDesign design;
    double scale = 1.0;
    std::vector<SampleType> window, buffer;
    std::vector<std::complex<SampleType>> bins;
    signalsmith::fft::RealFFT<SampleType> transform;
};

} // namespace chardsp
