#pragma once

#include "AssistantTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace letscomp::assistant
{
// Records up to ten seconds of uncompressed input for the assistant.
//
// Threading: push() runs on the audio thread and is the only writer of the
// samples and the frame count. Everything else runs off the audio thread.
// Samples below the published frame count are never rewritten until the
// buffer is re-armed, so copy() can read them while capture continues.
class CaptureBuffer
{
public:
    enum class State { idle, waiting, capturing, full };

    static constexpr double captureSeconds = 10.0;
    // Capture starts at the first sample above this, so silence before the
    // performance does not use up the ten seconds.
    static constexpr float startLevelDb = -60.0f;

    // Allocates. Never concurrent with push() (host prepare contract).
    void prepare(double newSampleRate, int newChannels)
    {
        const std::scoped_lock lock(allocationMutex);
        state.store(State::idle, std::memory_order_release);
        sampleRate = newSampleRate;
        capacity = static_cast<std::size_t>(std::max(0.0, std::round(newSampleRate * captureSeconds)));
        storage.assign(static_cast<std::size_t>(std::clamp(newChannels, 1, 2)), std::vector<float>(capacity, 0.0f));
        frames.store(0, std::memory_order_release);
    }

    void arm() noexcept { state.store(State::waiting, std::memory_order_release); }
    // Stops recording. Whatever was captured so far stays readable.
    void stop() noexcept { state.store(State::idle, std::memory_order_release); }

    void push(const juce::AudioBuffer<float>& buffer) noexcept
    {
        auto current = state.load(std::memory_order_acquire);
        if (current != State::waiting && current != State::capturing)
            return;
        const auto available = buffer.getNumSamples();
        const auto sourceChannels = buffer.getNumChannels();
        if (available <= 0 || sourceChannels <= 0 || capacity == 0)
            return;

        auto start = 0;
        if (current == State::waiting)
        {
            const auto startLevel = juce::Decibels::decibelsToGain(startLevelDb);
            start = available;
            for (int channel = 0; channel < sourceChannels; ++channel)
            {
                const auto* data = buffer.getReadPointer(channel);
                for (int sample = 0; sample < start; ++sample)
                    if (std::isfinite(data[sample]) && std::abs(data[sample]) > startLevel)
                    {
                        start = sample;
                        break;
                    }
            }
            if (start == available)
                return;
            frames.store(0, std::memory_order_release);
            if (!state.compare_exchange_strong(current, State::capturing, std::memory_order_acq_rel))
                return;
        }

        const auto written = frames.load(std::memory_order_relaxed);
        const auto count = std::min(static_cast<std::size_t>(available - start), capacity - written);
        for (std::size_t channel = 0; channel < storage.size(); ++channel)
        {
            const auto* source = buffer.getReadPointer(std::min(static_cast<int>(channel), sourceChannels - 1));
            auto* destination = storage[channel].data() + written;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto value = source[static_cast<std::size_t>(start) + index];
                destination[index] = std::isfinite(value) ? value : 0.0f;
            }
        }
        frames.store(written + count, std::memory_order_release);
        if (written + count >= capacity)
        {
            auto capturing = State::capturing;
            state.compare_exchange_strong(capturing, State::full, std::memory_order_acq_rel);
        }
    }

    // Off the audio thread only. Copies the frames captured so far.
    CaptureData copy() const
    {
        const std::scoped_lock lock(allocationMutex);
        CaptureData data;
        data.sampleRate = sampleRate;
        const auto count = std::min(frames.load(std::memory_order_acquire), capacity);
        for (const auto& channel : storage)
            data.channels.emplace_back(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(count));
        return data;
    }

    State currentState() const noexcept { return state.load(std::memory_order_acquire); }
    double capturedSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(frames.load(std::memory_order_acquire)) / sampleRate : 0.0;
    }

private:
    mutable std::mutex allocationMutex;
    std::vector<std::vector<float>> storage;
    double sampleRate = 0.0;
    std::size_t capacity = 0;
    std::atomic<std::size_t> frames{0};
    std::atomic<State> state{State::idle};
};
}
