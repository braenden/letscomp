#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace letscomp::dsp
{
// Second-order Butterworth high-pass for the detector, as a topology-preserving
// state-variable filter so the cutoff can glide without clicks. Four channels:
// left and right of the main input, then of the external sidechain.
class SidechainFilter
{
public:
    static constexpr int maximumChannels = 4;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::isfinite(newSampleRate) ? std::clamp(newSampleRate, 1.0, 768000.0) : 44100.0;
        reset();
        setCutoff(cutoffHz);
    }

    void reset() noexcept
    {
        integrator1.fill(0.0f);
        integrator2.fill(0.0f);
    }

    void setCutoff(float hz) noexcept
    {
        cutoffHz = std::isfinite(hz) ? hz : 20.0f;
        const auto limited = std::clamp(static_cast<double>(cutoffHz), 1.0, 0.45 * sampleRate);
        const auto g = std::tan(std::numbers::pi * limited / sampleRate);
        a1 = static_cast<float>(1.0 / (1.0 + g * (g + damping)));
        a2 = static_cast<float>(g) * a1;
        a3 = static_cast<float>(g) * a2;
    }

    float process(int channel, float input) noexcept
    {
        const auto index = static_cast<std::size_t>(std::clamp(channel, 0, maximumChannels - 1));
        auto& s1 = integrator1[index];
        auto& s2 = integrator2[index];
        const auto v3 = input - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        const auto highPass = input - static_cast<float>(damping) * v1 - v2;
        if (!std::isfinite(highPass))
        {
            s1 = s2 = 0.0f;
            return 0.0f;
        }
        return highPass;
    }

private:
    static constexpr double damping = std::numbers::sqrt2;
    double sampleRate = 44100.0;
    float cutoffHz = 20.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    std::array<float, maximumChannels> integrator1{}, integrator2{};
};
}
