// From github.com/charCulbert/chardsp at 87bfc1e (MIT).
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace chardsp
{

// Hands a live multichannel signal from the audio thread to an analysis on
// another thread, a hop at a time. It knows nothing of the analysis: the
// reader asks for the next hop, then copies the newest samples up to its end
// from any channel, as many as its block needs.
//
// Positions count samples pushed, from prepare()'s firstPosition (a host's
// sample clock, say), wrapping after 2^32. A hop ends a whole number of hops
// after where the reader last restarted, so hops keep their grid when the
// reader skips some.
//
// prepare() allocates, while nothing pushes or reads. push() runs on the audio
// thread and only copies samples into the ring; restart(), nextHop() and
// read() run on the reader's thread. Neither waits for the other: a reader
// that falls behind skips hops instead, and a read the writer overtook while
// it copied says so, for the reader to drop that hop.
template <typename SampleType>
class AnalysisBuffer
{
public:
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "AnalysisBuffer supports float or double");
    // Some targets (32-bit Arm Cortex-M, say) have no lock-free 64-bit
    // atomics, so double there would make push() call a lock.
    static_assert (std::atomic<SampleType>::is_always_lock_free,
                   "AnalysisBuffer needs lock-free atomics of its sample type "
                   "on this target: use float");
    static_assert (std::atomic<uint32_t>::is_always_lock_free,
                   "AnalysisBuffer needs lock-free 32-bit atomics");

    // Room for at least `capacity` samples of each channel (rounded up to a
    // power of two), all silent. The reader then restart()s.
    bool prepare (int newNumChannels, int capacity,
                  uint32_t firstPosition = 0)
    {
        if (newNumChannels < 1 || capacity < 1 || capacity > (1 << 30))
            return false;

        int size = 8;

        while (size < capacity)
            size *= 2;

        numChannels = newNumChannels;
        ringSize = static_cast<uint32_t> (size);
        ring.reset (new std::atomic<SampleType>[
            static_cast<size_t> (numChannels) * ringSize]());
        writeIndex.store (firstPosition, std::memory_order_release);
        writing.store (firstPosition, std::memory_order_relaxed);
        filled.store (0, std::memory_order_relaxed);
        position = firstPosition;
        advanced = 0;
        skipped = hop = 0;
        return true;
    }

    int getNumChannels() const noexcept { return numChannels; }
    int getCapacity() const noexcept { return static_cast<int> (ringSize); }

    // Audio thread: never blocks or allocates. A prepared channel past
    // numChannels gets silence, so every channel stays in step.
    void push (const SampleType* const* channels, int numChannelsGiven,
               int numSamples) noexcept
    {
        if (ringSize == 0 || numSamples <= 0)
            return;

        const uint32_t start = writeIndex.load (std::memory_order_relaxed);
        // Say how far this push will write before writing it, so a read the
        // writes overtake can tell.
        writing.store (start + static_cast<uint32_t> (numSamples),
                       std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);

        for (int c = 0; c < numChannels; ++c)
        {
            auto* channel = ring.get() + static_cast<size_t> (c) * ringSize;
            const SampleType* input = c < numChannelsGiven ? channels[c]
                                                           : nullptr;
            uint32_t index = start;

            for (int i = 0; i < numSamples; ++i, ++index)
                channel[index & (ringSize - 1)].store (
                    input != nullptr ? input[i] : SampleType {},
                    std::memory_order_relaxed);
        }

        const uint32_t before = filled.load (std::memory_order_relaxed);

        if (before < ringSize)
            filled.store (static_cast<uint32_t> (std::min<uint64_t> (
                              ringSize, uint64_t { before } + numSamples)),
                          std::memory_order_relaxed);

        writeIndex.store (start + static_cast<uint32_t> (numSamples),
                          std::memory_order_release);
    }

    // The samples pushed so far: the position just past the newest.
    uint32_t getWritePosition() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire);
    }

    // Hops of `newHop` samples from `from`, so the first ends at from + hop;
    // by default from the newest sample.
    void restart (int newHop, uint32_t from) noexcept
    {
        hop = std::max (1, newHop);
        position = from;
        advanced = 0;
        skipped = 0;
    }

    void restart (int newHop) noexcept { restart (newHop, getWritePosition()); }

    int getHop() const noexcept { return hop; }

    // Moves on a hop, if one has arrived, for a reader whose blocks are
    // `blockSize` samples: nothing until that many have been pushed, and
    // never a hop whose block the ring may have overwritten. With more than
    // `keep` hops waiting, it skips to the newest `keep`. False, staying put,
    // when no hop is waiting or a block and a hop don't fit in the ring.
    bool nextHop (int blockSize, int keep = 8) noexcept
    {
        if (hop == 0 || blockSize < 1
            || static_cast<int64_t> (blockSize) + hop > ringSize)
            return false;

        const auto step = static_cast<uint32_t> (hop);
        const uint32_t end = getWritePosition();

        if (filled.load (std::memory_order_relaxed)
            < static_cast<uint32_t> (blockSize))
            return false;

        const uint32_t waiting = (end - position) / step;

        if (waiting == 0)
            return false;

        const uint32_t room = (ringSize - static_cast<uint32_t> (blockSize))
                            / step;
        const uint32_t limit = std::max (uint32_t { 1 }, std::min (
            static_cast<uint32_t> (std::max (1, keep)), room));
        skipped = waiting > limit ? static_cast<int> (waiting - limit) : 0;
        advanced = (static_cast<uint32_t> (skipped) + 1) * step;
        position += advanced;
        return true;
    }

    // Where the current hop ends, the samples since the last one ended
    // (counting any skipped), and how many hops were skipped to reach it.
    uint32_t getPosition() const noexcept { return position; }
    uint32_t getAdvanced() const noexcept { return advanced; }
    int getSkipped() const noexcept { return skipped; }

    // The `numSamples` (at most getCapacity()) of a channel up to the current
    // hop's end. Any from before the first push read as silence. False when
    // the writer overwrote the oldest of them before the copy finished: the
    // copy is then torn, and the hop is to be dropped. A read that succeeds
    // means every earlier read of the hop that was no longer did too, so
    // reading every channel the same length (or the longest read last), the
    // last read's answer is the hop's.
    bool read (int channel, int numSamples,
               SampleType* destination) const noexcept
    {
        const auto* samples = ring.get() + static_cast<size_t> (channel)
                                               * ringSize;
        uint32_t index = position - static_cast<uint32_t> (numSamples);

        for (int i = 0; i < numSamples; ++i, ++index)
            destination[i] = samples[index & (ringSize - 1)].load (
                std::memory_order_relaxed);

        // Any sample overwritten above makes the push that did it visible here.
        std::atomic_thread_fence (std::memory_order_acquire);
        return writing.load (std::memory_order_relaxed)
                   - (position - static_cast<uint32_t> (numSamples))
               <= ringSize;
    }

private:
    int numChannels = 0;
    uint32_t ringSize = 0;
    std::unique_ptr<std::atomic<SampleType>[]> ring; // channel after channel
    std::atomic<uint32_t> writeIndex { 0 };
    std::atomic<uint32_t> writing { 0 }; // the end of the push under way
    std::atomic<uint32_t> filled { 0 }; // samples pushed, up to ringSize
    uint32_t position = 0, advanced = 0; // the reader's
    int skipped = 0, hop = 0;
};

} // namespace chardsp
