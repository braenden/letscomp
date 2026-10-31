#pragma once

#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace letscomp::dsp
{
class LookaheadDelay
{
public:
    static constexpr double crossfadeSeconds = 0.010;

    void prepare(double sampleRate, int maximumBlockSize, int channels)
    {
        maxDelaySamples = static_cast<int>(std::ceil(sampleRate * 0.020)) + 2;
        bufferSize = maxDelaySamples + std::max(1, maximumBlockSize) + 2;
        activeChannels = std::clamp(channels, 1, 2);
        crossfadeLength = std::max(1, static_cast<int>(std::round(sampleRate * crossfadeSeconds)));
        for (auto& channel : buffers)
            channel.assign(static_cast<size_t>(bufferSize), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : buffers)
            std::fill(channel.begin(), channel.end(), 0.0f);
        writePosition = 0;
        currentDelaySamples = targetDelaySamples = previousDelaySamples = 0;
        pendingDelaySamples = 0;
        crossfadePosition = crossfadeLength;
        hasPendingDelay = false;
        hasProcessedSamples = false;
    }

    void setDelaySamples(int samples) noexcept
    {
        const auto clamped = std::clamp(samples, 0, std::max(0, maxDelaySamples - 2));
        if (!hasProcessedSamples)
        {
            currentDelaySamples = previousDelaySamples = targetDelaySamples = clamped;
            pendingDelaySamples = clamped;
            hasPendingDelay = false;
            crossfadePosition = crossfadeLength;
            return;
        }
        if (crossfadePosition < crossfadeLength)
        {
            pendingDelaySamples = clamped;
            hasPendingDelay = clamped != targetDelaySamples;
            return;
        }
        if (clamped == targetDelaySamples) return;
        startCrossfade(clamped);
    }
    int getDelaySamples() const noexcept
    {
        return hasPendingDelay ? pendingDelaySamples : targetDelaySamples;
    }

    float processSample(int channel, float input) noexcept
    {
        assert(channel >= 0 && channel < activeChannels);
        if (channel < 0 || channel >= activeChannels)
            return 0.0f;
        auto& data = buffers[static_cast<size_t>(channel)];
        data[static_cast<size_t>(writePosition)] = std::isfinite(input) ? input : 0.0f;
        const auto read = [this, &data](int delayInSamples)
        {
            auto position = writePosition - delayInSamples;
            if (position < 0) position += bufferSize;
            return data[static_cast<size_t>(position)];
        };
        if (crossfadePosition >= crossfadeLength)
            return read(targetDelaySamples);
        // An S-shaped fade has no corners at either end, which a linear one
        // would leave as a faint tick on low notes.
        const auto position = static_cast<float>(crossfadePosition) / static_cast<float>(crossfadeLength);
        const auto mix = position * position * (3.0f - 2.0f * position);
        return read(previousDelaySamples) + mix * (read(targetDelaySamples) - read(previousDelaySamples));
    }

    void advance() noexcept
    {
        hasProcessedSamples = true;
        if (crossfadePosition < crossfadeLength && ++crossfadePosition >= crossfadeLength)
        {
            currentDelaySamples = targetDelaySamples;
            if (hasPendingDelay)
            {
                const auto next = pendingDelaySamples;
                hasPendingDelay = false;
                if (next != currentDelaySamples)
                    startCrossfade(next);
            }
        }
        if (++writePosition >= bufferSize)
            writePosition = 0;
    }

private:
    void startCrossfade(int delaySamples) noexcept
    {
        if (delaySamples == currentDelaySamples)
        {
            previousDelaySamples = targetDelaySamples = currentDelaySamples;
            crossfadePosition = crossfadeLength;
            return;
        }
        previousDelaySamples = currentDelaySamples;
        targetDelaySamples = delaySamples;
        crossfadePosition = 0;
    }

    std::array<std::vector<float>, 2> buffers;
    int bufferSize = 1, maxDelaySamples = 0, writePosition = 0, activeChannels = 2;
    int currentDelaySamples = 0, previousDelaySamples = 0, targetDelaySamples = 0;
    int pendingDelaySamples = 0;
    int crossfadePosition = 1, crossfadeLength = 1;
    bool hasPendingDelay = false, hasProcessedSamples = false;
};
}
