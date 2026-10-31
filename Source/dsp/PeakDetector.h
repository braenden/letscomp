#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace letscomp::dsp
{
// Sliding-window peak level. The window covers most of a half period of the
// lowest bass note (E1, 41 Hz, ripple below 0.5 dB), so a steady tone produces
// a steady level and the gain computer sees a constant target instead of one
// that swings to zero at every zero crossing. Without this, attack and release would decide
// how much a steady tone is compressed, and low notes would be distorted.
class PeakDetector
{
public:
    static constexpr double windowSeconds = 0.010;

    // Allocates. Call only off the audio thread.
    void prepare(double sampleRate)
    {
        windowSamples = std::max(1, static_cast<int>(std::round(sampleRate * windowSeconds)));
        capacity = windowSamples + 1;
        values.assign(static_cast<size_t>(capacity), 0.0f);
        times.assign(static_cast<size_t>(capacity), 0);
        reset();
    }

    void reset() noexcept
    {
        head = count = 0;
        time = 0;
    }

    // Monotonic deque: amortised O(1) per sample, no allocation.
    float process(float level) noexcept
    {
        while (count > 0 && values[static_cast<size_t>(backIndex())] <= level)
            --count;
        const auto slot = static_cast<size_t>((head + count) % capacity);
        values[slot] = level;
        times[slot] = time;
        ++count;
        while (times[static_cast<size_t>(head)] <= time - windowSamples)
        {
            head = (head + 1) % capacity;
            --count;
        }
        ++time;
        return values[static_cast<size_t>(head)];
    }

    int windowLength() const noexcept { return windowSamples; }

private:
    int backIndex() const noexcept { return (head + count - 1) % capacity; }

    std::vector<float> values;
    std::vector<std::int64_t> times;
    int windowSamples = 1, capacity = 2, head = 0, count = 0;
    std::int64_t time = 0;
};
}
