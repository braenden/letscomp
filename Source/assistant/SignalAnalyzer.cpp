#include "SignalAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace letscomp::assistant
{
namespace
{
// Below this the input is treated as silence or noise floor.
constexpr float absoluteActivityFloorDb = -60.0f;
// Frames more than this far below the loud end are pauses, breaths or bleed.
constexpr float relativeActivityRangeDb = 30.0f;
// A frame starts a new event when it rises this much over the last 50 ms.
constexpr float onsetRiseDb = 6.0f;
constexpr int onsetLookbackFrames = 5;
constexpr int onsetRefractoryFrames = 8;
// Gaps longer than this are phrase breaks, not the rhythm of the part.
constexpr float longestOnsetIntervalMs = 1500.0f;
constexpr std::size_t minimumOnsetIntervals = 4;
constexpr double lowFrequencySplitHz = 150.0;

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double process(double input) noexcept
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

Biquad lowpass(double frequency, double sampleRate)
{
    constexpr double butterworthQ = 1.0 / std::numbers::sqrt2;
    const auto omega = 2.0 * std::numbers::pi * frequency / sampleRate;
    const auto alpha = std::sin(omega) / (2.0 * butterworthQ);
    const auto cosine = std::cos(omega);
    const auto a0 = 1.0 + alpha;
    Biquad filter;
    filter.b0 = (1.0 - cosine) * 0.5 / a0;
    filter.b1 = (1.0 - cosine) / a0;
    filter.b2 = filter.b0;
    filter.a1 = -2.0 * cosine / a0;
    filter.a2 = (1.0 - alpha) / a0;
    return filter;
}

// BS.1770 K-weighting for any sample rate (high shelf followed by high pass),
// using the analogue prototype constants published with libebur128.
std::array<Biquad, 2> kWeighting(double sampleRate)
{
    std::array<Biquad, 2> stages;
    {
        constexpr double frequency = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const auto k = std::tan(std::numbers::pi * frequency / sampleRate);
        const auto vh = std::pow(10.0, gainDb / 20.0);
        const auto vb = std::pow(vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        stages[0].b0 = (vh + vb * k / q + k * k) / a0;
        stages[0].b1 = 2.0 * (k * k - vh) / a0;
        stages[0].b2 = (vh - vb * k / q + k * k) / a0;
        stages[0].a1 = 2.0 * (k * k - 1.0) / a0;
        stages[0].a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        constexpr double frequency = 38.13547087602444, q = 0.5003270373238773;
        const auto k = std::tan(std::numbers::pi * frequency / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;
        stages[1].b0 = 1.0;
        stages[1].b1 = -2.0;
        stages[1].b2 = 1.0;
        stages[1].a1 = 2.0 * (k * k - 1.0) / a0;
        stages[1].a2 = (1.0 - k / q + k * k) / a0;
    }
    return stages;
}

float toDecibels(double gain) noexcept
{
    return gain > 1.0e-8 ? static_cast<float>(20.0 * std::log10(gain)) : -160.0f;
}

float powerToDecibels(double power) noexcept
{
    return power > 1.0e-16 ? static_cast<float>(10.0 * std::log10(power)) : -160.0f;
}

float finiteOrZero(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
}

float percentile(std::vector<float> values, double fraction, float fallback)
{
    if (values.empty())
        return fallback;
    std::sort(values.begin(), values.end());
    const auto position = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower + 1, values.size() - 1);
    const auto weight = static_cast<float>(position - static_cast<double>(lower));
    return values[lower] + weight * (values[upper] - values[lower]);
}

std::optional<double> integratedLoudness(const std::vector<std::vector<float>>& channels,
                                         double sampleRate)
{
    if (channels.empty() || sampleRate <= 0.0)
        return std::nullopt;
    const auto frames = channels.front().size();
    const auto blockLength = static_cast<std::size_t>(std::round(sampleRate * 0.4));
    const auto step = static_cast<std::size_t>(std::round(sampleRate * 0.1));
    if (blockLength == 0 || step == 0 || frames < blockLength)
        return std::nullopt;

    // Per-step K-weighted energy, summed over channels.
    const auto stepCount = frames / step;
    std::vector<double> stepEnergy(stepCount, 0.0);
    for (const auto& channel : channels)
    {
        auto filters = kWeighting(sampleRate);
        for (std::size_t index = 0; index < stepCount * step; ++index)
        {
            auto value = static_cast<double>(finiteOrZero(channel[index]));
            value = filters[1].process(filters[0].process(value));
            stepEnergy[index / step] += value * value;
        }
    }

    const auto stepsPerBlock = blockLength / step;
    std::vector<double> blockPower;
    for (std::size_t first = 0; first + stepsPerBlock <= stepCount; ++first)
    {
        const auto energy = std::accumulate(stepEnergy.begin() + static_cast<std::ptrdiff_t>(first),
                                            stepEnergy.begin() + static_cast<std::ptrdiff_t>(first + stepsPerBlock),
                                            0.0);
        blockPower.push_back(energy / static_cast<double>(stepsPerBlock * step));
    }

    const auto loudness = [](double power) { return -0.691 + 10.0 * std::log10(std::max(power, 1.0e-20)); };
    const auto gatedMean = [&](double gate) -> std::optional<double>
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (const auto power : blockPower)
            if (loudness(power) > gate)
            {
                sum += power;
                ++count;
            }
        if (count == 0) return std::nullopt;
        return sum / static_cast<double>(count);
    };

    const auto absoluteGated = gatedMean(-70.0);
    if (!absoluteGated.has_value())
        return std::nullopt;
    const auto relativeGated = gatedMean(loudness(*absoluteGated) - 10.0);
    if (!relativeGated.has_value())
        return std::nullopt;
    return loudness(*relativeGated);
}

SignalAnalysis analyseSignal(const CaptureData& capture)
{
    SignalAnalysis analysis;
    const auto sampleCount = capture.frames();
    if (sampleCount == 0 || capture.sampleRate <= 0.0)
        return analysis;
    analysis.durationSeconds = capture.seconds();

    const auto frameLength = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::round(capture.sampleRate * SignalAnalysis::frameSeconds)));
    const auto frameCount = sampleCount / frameLength;
    const auto channelCount = static_cast<double>(capture.channels.size());
    std::vector<float> peakDb(frameCount, -160.0f);
    std::vector<double> energy(frameCount, 0.0), lowEnergy(frameCount, 0.0);

    for (const auto& channel : capture.channels)
    {
        auto filter = lowpass(lowFrequencySplitHz, capture.sampleRate);
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            double peak = 0.0, sum = 0.0, lowSum = 0.0;
            for (std::size_t offset = 0; offset < frameLength; ++offset)
            {
                const auto value = static_cast<double>(finiteOrZero(channel[frame * frameLength + offset]));
                const auto low = filter.process(value);
                peak = std::max(peak, std::abs(value));
                sum += value * value;
                lowSum += low * low;
            }
            peakDb[frame] = std::max(peakDb[frame], toDecibels(peak));
            energy[frame] += sum / (static_cast<double>(frameLength) * channelCount);
            lowEnergy[frame] += lowSum / (static_cast<double>(frameLength) * channelCount);
        }
    }
    analysis.framePeakDb = peakDb;
    analysis.activeFrames.assign(frameCount, false);
    analysis.loudFrames.assign(frameCount, false);

    std::vector<float> audible;
    for (const auto level : peakDb)
        if (level > absoluteActivityFloorDb) audible.push_back(level);
    if (audible.empty())
        return analysis;

    const auto gate = std::max(absoluteActivityFloorDb,
                               percentile(audible, 0.95) - relativeActivityRangeDb);
    std::vector<float> activeLevels;
    for (std::size_t frame = 0; frame < frameCount; ++frame)
        if (peakDb[frame] > gate)
        {
            analysis.activeFrames[frame] = true;
            activeLevels.push_back(peakDb[frame]);
        }
    analysis.activeSeconds = static_cast<double>(activeLevels.size()) * SignalAnalysis::frameSeconds;
    analysis.loudLevelDb = percentile(activeLevels, 0.90);
    analysis.quietLevelDb = percentile(activeLevels, 0.20);
    analysis.peakLevelDb = *std::max_element(peakDb.begin(), peakDb.end());

    const auto loudGate = percentile(activeLevels, 0.75);
    std::vector<float> crest;
    double activeEnergy = 0.0, activeLowEnergy = 0.0;
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        if (!analysis.activeFrames[frame])
            continue;
        analysis.loudFrames[frame] = peakDb[frame] >= loudGate;
        const auto first = frame >= 9 ? frame - 9 : 0;
        const auto windowEnergy = std::accumulate(energy.begin() + static_cast<std::ptrdiff_t>(first),
                                                  energy.begin() + static_cast<std::ptrdiff_t>(frame + 1), 0.0)
                                / static_cast<double>(frame + 1 - first);
        crest.push_back(peakDb[frame] - powerToDecibels(windowEnergy));
        activeEnergy += energy[frame];
        activeLowEnergy += lowEnergy[frame];
    }
    analysis.crestDb = percentile(crest, 0.5, 0.0f);
    analysis.lowFrequencyShare = activeEnergy > 0.0
                               ? static_cast<float>(std::clamp(activeLowEnergy / activeEnergy, 0.0, 1.0)) : 0.0f;

    std::vector<std::size_t> onsets;
    for (std::size_t frame = onsetLookbackFrames; frame < frameCount; ++frame)
    {
        if (!analysis.activeFrames[frame])
            continue;
        const auto reference = *std::min_element(peakDb.begin() + static_cast<std::ptrdiff_t>(frame - onsetLookbackFrames),
                                                 peakDb.begin() + static_cast<std::ptrdiff_t>(frame));
        const auto refractoryOver = onsets.empty() || frame - onsets.back() >= onsetRefractoryFrames;
        if (peakDb[frame] - reference >= onsetRiseDb && refractoryOver)
            onsets.push_back(frame);
    }
    if (analysis.activeSeconds > 0.0)
        analysis.onsetsPerSecond = static_cast<float>(static_cast<double>(onsets.size()) / analysis.activeSeconds);

    std::vector<float> intervals;
    for (std::size_t index = 1; index < onsets.size(); ++index)
    {
        const auto milliseconds = static_cast<float>(onsets[index] - onsets[index - 1])
                                * static_cast<float>(SignalAnalysis::frameSeconds * 1000.0);
        if (milliseconds <= longestOnsetIntervalMs)
            intervals.push_back(milliseconds);
    }
    if (intervals.size() >= minimumOnsetIntervals)
        analysis.medianOnsetIntervalMs = percentile(intervals, 0.5);

    analysis.loudnessLufs = integratedLoudness(capture.channels, capture.sampleRate);
    return analysis;
}
}
