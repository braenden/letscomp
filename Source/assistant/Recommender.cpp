#include "Recommender.h"
#include "SignalAnalyzer.h"
#include "../parameters/ParameterDomains.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>

namespace letscomp::assistant
{
namespace
{
constexpr int bisectionSteps = 18;
constexpr float bisectionToleranceDb = 0.05f;

// Rows: vocal, drums, bass, bus. Columns: gentle, control, punch, glue.
// {target GR, ratio, knee, attack, release share of onset spacing, release min, max, default}
constexpr StyleTemplate templates[4][4]{
    {{3.0f, 2.0f, 9.0f, 15.0f, 0.6f, 80.0f, 250.0f, 150.0f},
     {6.0f, 4.0f, 6.0f, 4.0f, 0.5f, 50.0f, 180.0f, 100.0f},
     {7.5f, 6.0f, 3.0f, 0.1f, 0.5f, 50.0f, 150.0f, 100.0f},   // Upfront
     {2.5f, 1.8f, 12.0f, 25.0f, 1.0f, 150.0f, 400.0f, 250.0f}},
    {{3.0f, 2.0f, 6.0f, 20.0f, 0.5f, 60.0f, 200.0f, 120.0f},
     {6.0f, 4.0f, 3.0f, 2.0f, 0.4f, 40.0f, 150.0f, 80.0f},
     {6.0f, 4.0f, 2.0f, 25.0f, 0.6f, 50.0f, 250.0f, 120.0f},
     {3.0f, 2.0f, 6.0f, 30.0f, 1.0f, 100.0f, 600.0f, 300.0f}},
    {{3.0f, 2.0f, 8.0f, 20.0f, 0.8f, 100.0f, 300.0f, 180.0f},
     {6.0f, 4.0f, 6.0f, 6.0f, 0.6f, 80.0f, 250.0f, 140.0f},
     {5.0f, 4.0f, 3.0f, 25.0f, 0.5f, 70.0f, 200.0f, 120.0f},
     {3.0f, 2.5f, 9.0f, 30.0f, 1.0f, 150.0f, 400.0f, 250.0f}},
    {{1.5f, 1.5f, 12.0f, 30.0f, 1.0f, 150.0f, 600.0f, 300.0f},
     {3.0f, 2.5f, 8.0f, 10.0f, 0.5f, 80.0f, 300.0f, 150.0f},
     {3.0f, 3.0f, 4.0f, 30.0f, 0.5f, 80.0f, 300.0f, 150.0f},
     {2.0f, 2.0f, 6.0f, 30.0f, 0.75f, 100.0f, 400.0f, 200.0f}},
};

[[gnu::format(printf, 1, 2)]] std::string format(const char* pattern, ...)
{
    char buffer[320];
    va_list arguments;
    va_start(arguments, pattern);
    std::vsnprintf(buffer, sizeof(buffer), pattern, arguments);
    va_end(arguments);
    return buffer;
}

std::string text(std::string_view view) { return std::string(view); }

const char* eventName(SourceType source) noexcept
{
    switch (source)
    {
        case SourceType::vocal: return "syllables";
        case SourceType::drums: return "hits";
        case SourceType::bass:  return "notes";
        case SourceType::bus:   return "beats";
    }
    return "notes";
}

const char* singularEventName(SourceType source) noexcept
{
    switch (source)
    {
        case SourceType::vocal: return "syllable";
        case SourceType::drums: return "hit";
        case SourceType::bass:  return "note";
        case SourceType::bus:   return "beat";
    }
    return "note";
}

std::string attackReason(SourceType source, Style style)
{
    switch (style)
    {
        case Style::gentle:
            return format("leaves the start of each %s alone and only eases the body", singularEventName(source));
        case Style::control:
            return "catches level jumps quickly so the part sits at a steady level";
        case Style::punch:
            if (source == SourceType::vocal)
                return "clamps each syllable the moment it starts, so no consonant pokes out and the vocal "
                       "sits flat and upfront";
            return format("lets the front of each %s through before the compressor clamps, which adds snap",
                          singularEventName(source));
        case Style::glue:
            return "keeps transients while the reduction moves with the whole part";
    }
    return {};
}

std::string formatMilliseconds(float milliseconds)
{
    return milliseconds < 10.0f ? format("%.1f", static_cast<double>(milliseconds))
                                : format("%.0f", static_cast<double>(milliseconds));
}

float roundTo(float value, float step) noexcept { return std::round(value / step) * step; }

float averageLoudReduction(const SignalAnalysis& analysis, const std::vector<float>& gainReduction,
                           std::size_t frameLength)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t frame = 0; frame < analysis.loudFrames.size(); ++frame)
    {
        if (!analysis.loudFrames[frame])
            continue;
        const auto first = frame * frameLength;
        const auto last = std::min(first + frameLength, gainReduction.size());
        for (auto sample = first; sample < last; ++sample)
            sum += gainReduction[sample];
        count += last > first ? last - first : 0;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

float activeRmsDb(const SignalAnalysis& analysis, const std::vector<std::vector<float>>& channels,
                  std::size_t frameLength)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& channel : channels)
        for (std::size_t frame = 0; frame < analysis.activeFrames.size(); ++frame)
        {
            if (!analysis.activeFrames[frame])
                continue;
            for (auto sample = frame * frameLength; sample < std::min((frame + 1) * frameLength, channel.size()); ++sample)
            {
                sum += static_cast<double>(channel[sample]) * static_cast<double>(channel[sample]);
                ++count;
            }
        }
    return count > 0 && sum > 0.0 ? static_cast<float>(10.0 * std::log10(sum / static_cast<double>(count))) : -160.0f;
}
}

const StyleTemplate& styleTemplate(SourceType source, Style style) noexcept
{
    return templates[static_cast<int>(source)][static_cast<int>(style)];
}

SimulationResult simulate(const CaptureData& capture, const dsp::CompressorSettings& settings, bool keepOutput)
{
    constexpr int blockSize = 1024;
    SimulationResult result;
    const auto frames = capture.frames();
    const auto channels = static_cast<int>(std::min<std::size_t>(2, capture.channels.size()));
    if (frames == 0 || channels == 0)
        return result;

    dsp::CompressorEngine engine;
    auto fifo = std::make_unique<visualization::ScopeFifo<8192>>();
    visualization::MeterValues meters;
    engine.prepare(capture.sampleRate, blockSize, channels);
    engine.setSettings(settings);
    engine.reset();

    result.gainReductionDb.assign(frames, 0.0f);
    if (keepOutput)
        result.output.assign(static_cast<std::size_t>(channels), std::vector<float>(frames, 0.0f));
    juce::AudioBuffer<float> block(channels, blockSize);
    visualization::ScopeFrame discarded;
    for (std::size_t position = 0; position < frames; position += blockSize)
    {
        const auto count = static_cast<int>(std::min<std::size_t>(blockSize, frames - position));
        block.setSize(channels, count, false, false, true);
        for (int channel = 0; channel < channels; ++channel)
            std::copy_n(capture.channels[static_cast<std::size_t>(channel)].data() + position, count,
                        block.getWritePointer(channel));
        engine.process(block, *fifo, meters,
                       std::span<float>(result.gainReductionDb.data() + position, static_cast<std::size_t>(count)));
        if (keepOutput)
            for (int channel = 0; channel < channels; ++channel)
                std::copy_n(block.getReadPointer(channel), count,
                            result.output[static_cast<std::size_t>(channel)].data() + position);
        while (fifo->pop(discarded)) {}
    }
    return result;
}

AssistantResult recommend(const CaptureData& capture, const SignalAnalysis& analysis,
                          SourceType source, Style style, Amount amount, const std::atomic<bool>* cancel)
{
    AssistantResult result;
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    if (capture.seconds() < minimumCaptureSeconds)
    {
        result.message = format("Too short to judge. Play at least %.0f seconds.", minimumCaptureSeconds);
        return result;
    }
    if (analysis.activeSeconds < minimumActiveSeconds)
    {
        result.message = "Not enough signal. Play the part louder than -60 dBFS for at least a second.";
        return result;
    }

    const auto& preset = styleTemplate(source, style);
    dsp::CompressorSettings settings;
    settings.ratio = preset.ratio;
    settings.kneeDb = preset.kneeDb;
    settings.attackMs = preset.attackMs;
    settings.holdMs = 0.0f;
    settings.rangeDb = 0.0f;
    settings.makeupDb = 0.0f;
    settings.mix = 1.0f;
    settings.lookaheadSamples = 0;
    settings.bypass = false;

    auto release = preset.releaseDefaultMs;
    if (analysis.medianOnsetIntervalMs.has_value())
        release = std::clamp(preset.releasePerOnset * *analysis.medianOnsetIntervalMs,
                             preset.releaseMinimumMs, preset.releaseMaximumMs);
    settings.releaseMs = roundTo(release, release < 100.0f ? 1.0f : 5.0f);

    const auto frameLength = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::round(capture.sampleRate * SignalAnalysis::frameSeconds)));
    const auto loudReduction = [&](float thresholdDb)
    {
        auto trial = settings;
        trial.thresholdDb = thresholdDb;
        return averageLoudReduction(analysis, simulate(capture, trial, false).gainReductionDb, frameLength);
    };

    const auto target = preset.targetGainReductionDb * targetScale(amount);
    auto low = params::domain::thresholdMinimumDb;
    auto high = params::domain::thresholdMaximumDb;
    auto threshold = low;
    auto reached = true;
    if (loudReduction(low) < target)
    {
        reached = false;
    }
    else
    {
        for (int step = 0; step < bisectionSteps; ++step)
        {
            if (cancelled())
                return result;
            threshold = 0.5f * (low + high);
            const auto reduction = loudReduction(threshold);
            if (std::abs(reduction - target) < bisectionToleranceDb)
                break;
            if (reduction > target) low = threshold;
            else high = threshold;
        }
    }
    settings.thresholdDb = std::clamp(roundTo(threshold, 0.1f), params::domain::thresholdMinimumDb,
                                      params::domain::thresholdMaximumDb);
    if (cancelled())
        return result;

    const auto final = simulate(capture, settings, true);
    const auto achieved = averageLoudReduction(analysis, final.gainReductionDb, frameLength);
    const auto outputLoudness = integratedLoudness(final.output, capture.sampleRate);
    const auto loudnessMatched = analysis.loudnessLufs.has_value() && outputLoudness.has_value();
    const auto makeup = loudnessMatched
                      ? static_cast<float>(*analysis.loudnessLufs - *outputLoudness)
                      : activeRmsDb(analysis, capture.channels, frameLength)
                            - activeRmsDb(analysis, final.output, frameLength);
    settings.makeupDb = std::clamp(roundTo(makeup, 0.1f), params::domain::makeupMinimumDb,
                                   params::domain::makeupMaximumDb);

    Recommendation recommendation;
    recommendation.source = source;
    recommendation.style = style;
    recommendation.amount = amount;
    recommendation.settings = settings;
    recommendation.targetGainReductionDb = target;
    recommendation.achievedGainReductionDb = achieved;
    recommendation.targetReached = reached;

    const auto sourceName = text(displayName(source));
    const auto styleName = text(displayName(source, style));
    const auto amountName = text(displayName(amount));
    const auto attackText = formatMilliseconds(settings.attackMs);
    auto& list = recommendation.notes;
    list.push_back({"Threshold", reached
        ? format("%.1f dB gives about %.1f dB of reduction on the loud parts (the %s target), "
                 "which peak around %.1f dBFS.",
                 static_cast<double>(settings.thresholdDb), static_cast<double>(achieved), amountName.c_str(),
                 static_cast<double>(analysis.loudLevelDb))
        : format("%.1f dB is as low as it goes. The part is quiet, so it only reaches %.1f dB of reduction; "
                 "turn the input up for more.",
                 static_cast<double>(settings.thresholdDb), static_cast<double>(achieved))});
    list.push_back({"Ratio", format("%.1f:1 with a %.0f dB knee, the %s %s starting point.",
        static_cast<double>(settings.ratio), static_cast<double>(settings.kneeDb),
        sourceName.c_str(), styleName.c_str())});
    list.push_back({"Attack", format("%s ms %s.", attackText.c_str(), attackReason(source, style).c_str())});
    list.push_back({"Release", analysis.medianOnsetIntervalMs.has_value()
        ? format("%.0f ms recovers between %s, which come about every %.0f ms.",
                 static_cast<double>(settings.releaseMs), eventName(source),
                 static_cast<double>(*analysis.medianOnsetIntervalMs))
        : format("%.0f ms is the %s %s default, because no steady rhythm was found.",
                 static_cast<double>(settings.releaseMs), sourceName.c_str(), styleName.c_str())});
    list.push_back({"Makeup", loudnessMatched
        ? format("%+.1f dB brings the loudness back to the input (%.1f LUFS), so comparing with bypass is fair.",
                 static_cast<double>(settings.makeupDb), *analysis.loudnessLufs)
        : format("%+.1f dB brings the level back to the input, so comparing with bypass is fair.",
                 static_cast<double>(settings.makeupDb))});
    list.push_back({"Mix", "100 percent, so you hear the plain compressor. Lower it to blend in the dry "
                           "signal for parallel compression."});
    list.push_back({"Other", "Hold, lookahead, range and the sidechain filter are off, the compressor "
                             "listens to the part itself, and mix is 100 percent, so you hear the plain "
                             "compressor."});
    // The knobs already show every value, so the summary only says what was achieved.
    recommendation.summary = reached
        ? format("About %.1f dB on the loud parts", static_cast<double>(achieved))
        : format("Only %.1f dB on the loud parts. The part is too quiet for more", static_cast<double>(achieved));

    result.succeeded = true;
    result.recommendation = std::move(recommendation);
    return result;
}
}
