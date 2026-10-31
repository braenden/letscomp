#pragma once

#include "../dsp/CompressorEngine.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace letscomp::assistant
{
enum class SourceType { vocal, drums, bass, bus };
enum class Style { gentle, control, punch, glue };
// How hard to compress, applied on top of the style's target gain reduction.
enum class Amount { light, medium, heavy };

inline constexpr std::array allSources{SourceType::vocal, SourceType::drums, SourceType::bass, SourceType::bus};
inline constexpr std::array allStyles{Style::gentle, Style::control, Style::punch, Style::glue};
inline constexpr std::array allAmounts{Amount::light, Amount::medium, Amount::heavy};

// Stable identifiers, used in saved plug-in state.
constexpr std::string_view identifier(SourceType source) noexcept
{
    switch (source)
    {
        case SourceType::vocal: return "vocal";
        case SourceType::drums: return "drums";
        case SourceType::bass:  return "bass";
        case SourceType::bus:   return "bus";
    }
    return "vocal";
}

constexpr std::string_view identifier(Style style) noexcept
{
    switch (style)
    {
        case Style::gentle:  return "gentle";
        case Style::control: return "control";
        case Style::punch:   return "punch";
        case Style::glue:    return "glue";
    }
    return "control";
}

constexpr std::string_view identifier(Amount amount) noexcept
{
    switch (amount)
    {
        case Amount::light:  return "light";
        case Amount::medium: return "medium";
        case Amount::heavy:  return "heavy";
    }
    return "medium";
}

constexpr std::string_view displayName(SourceType source) noexcept
{
    switch (source)
    {
        case SourceType::vocal: return "Vocal";
        case SourceType::drums: return "Drums";
        case SourceType::bass:  return "Bass";
        case SourceType::bus:   return "Bus / Mix";
    }
    return "Vocal";
}

// On a vocal the punch slot is the tight, upfront sound of modern rap and pop
// rather than letting transients through, so it carries its own name there.
constexpr std::string_view displayName(SourceType source, Style style) noexcept
{
    switch (style)
    {
        case Style::gentle:  return "Gentle";
        case Style::control: return "Control";
        case Style::punch:   return source == SourceType::vocal ? "Upfront" : "Punch";
        case Style::glue:    return "Glue";
    }
    return "Control";
}

// Menu text, with a hint of where the style is typically heard.
constexpr std::string_view menuName(SourceType source, Style style) noexcept
{
    return source == SourceType::vocal && style == Style::punch ? "Upfront (rap, pop)"
                                                                : displayName(source, style);
}

constexpr std::string_view displayName(Amount amount) noexcept
{
    switch (amount)
    {
        case Amount::light:  return "Light";
        case Amount::medium: return "Medium";
        case Amount::heavy:  return "Heavy";
    }
    return "Medium";
}

// Scales the style's target gain reduction.
constexpr float targetScale(Amount amount) noexcept
{
    switch (amount)
    {
        case Amount::light:  return 0.6f;
        case Amount::medium: return 1.0f;
        case Amount::heavy:  return 1.5f;
    }
    return 1.0f;
}

inline std::optional<SourceType> sourceFromIdentifier(std::string_view text) noexcept
{
    for (const auto source : allSources)
        if (identifier(source) == text) return source;
    return std::nullopt;
}

inline std::optional<Style> styleFromIdentifier(std::string_view text) noexcept
{
    for (const auto style : allStyles)
        if (identifier(style) == text) return style;
    return std::nullopt;
}

inline std::optional<Amount> amountFromIdentifier(std::string_view text) noexcept
{
    for (const auto amount : allAmounts)
        if (identifier(amount) == text) return amount;
    return std::nullopt;
}

// Uncompressed input captured for analysis, one vector per channel.
struct CaptureData
{
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;

    std::size_t frames() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
    double seconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(frames()) / sampleRate : 0.0;
    }
};

// Everything the recommender needs to know about the captured part. All levels
// are measured before compression, on 10 ms frames that match the detector.
struct SignalAnalysis
{
    static constexpr double frameSeconds = 0.010;

    double durationSeconds = 0.0;
    double activeSeconds = 0.0;
    float loudLevelDb = -160.0f;      // 90th percentile frame peak while playing
    float quietLevelDb = -160.0f;     // 20th percentile frame peak while playing
    float peakLevelDb = -160.0f;      // highest frame peak
    float crestDb = 0.0f;             // median peak over 100 ms RMS while playing
    float onsetsPerSecond = 0.0f;
    std::optional<float> medianOnsetIntervalMs;
    float lowFrequencyShare = 0.0f;   // energy share below 150 Hz
    std::optional<double> loudnessLufs;
    std::vector<float> framePeakDb;
    std::vector<bool> activeFrames;
    std::vector<bool> loudFrames;     // loudest quarter of the active frames
};

// One starting point per source and style. Every value is a visible product
// choice; only the threshold, release and makeup are derived from the signal.
struct StyleTemplate
{
    float targetGainReductionDb;   // average reduction on the loud frames at Medium
    float ratio;
    float kneeDb;
    float attackMs;
    float releasePerOnset;         // release as a share of the onset spacing
    float releaseMinimumMs, releaseMaximumMs, releaseDefaultMs;
};

struct ParameterNote
{
    std::string parameter;
    std::string text;
};

struct Recommendation
{
    SourceType source = SourceType::vocal;
    Style style = Style::control;
    Amount amount = Amount::medium;
    dsp::CompressorSettings settings;   // lookaheadSamples is always 0
    float targetGainReductionDb = 0.0f;
    float achievedGainReductionDb = 0.0f;
    bool targetReached = true;
    std::string summary;
    std::vector<ParameterNote> notes;
};

struct AssistantResult
{
    bool succeeded = false;
    std::string message;
    std::optional<Recommendation> recommendation;
};
}
