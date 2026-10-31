#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace letscomp::visualization
{
struct ScopeFrame
{
    float inputMin = 0.0f, inputMax = 0.0f;
    float outputMin = 0.0f, outputMax = 0.0f;
    float inputRms = 0.0f, outputRms = 0.0f;
    float gainReductionDb = 0.0f;
};

template <size_t Capacity>
class ScopeFifo
{
public:
    static_assert(Capacity > 1, "ScopeFifo needs room for at least one frame");

    bool push(const ScopeFrame& frame) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == readIndex.load(std::memory_order_acquire))
            return false;
        frames[write] = frame;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(ScopeFrame& frame) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire))
            return false;
        frame = frames[read];
        readIndex.store(increment(read), std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t increment(size_t value) noexcept { return (value + 1) % Capacity; }
    std::array<ScopeFrame, Capacity> frames{};
    std::atomic<size_t> writeIndex{0}, readIndex{0};
};

struct MeterValues
{
    std::atomic<float> inputPeak{0.0f};
    std::atomic<float> outputPeak{0.0f};
    std::atomic<float> currentGainReductionDb{0.0f};
    std::atomic<float> recentMaxGainReductionDb{0.0f};
};
}
