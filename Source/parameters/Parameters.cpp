#include "Parameters.h"

namespace letscomp::params
{
namespace
{
juce::NormalisableRange<float> logarithmicRange(float minimum, float maximum)
{
    return {minimum, maximum,
            [](float start, float end, float normalised)
            {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value)
            {
                return std::log(juce::jlimit(start, end, value) / start) / std::log(end / start);
            }};
}

juce::AudioParameterFloatAttributes numericAttributes(const juce::String& label, int decimals)
{
    return juce::AudioParameterFloatAttributes{}
        .withLabel(label)
        .withStringFromValueFunction([label, decimals](float value, int)
        {
            return juce::String(value, decimals) + (label.isEmpty() ? juce::String{} : " " + label);
        })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}

juce::AudioParameterFloatAttributes timeAttributes()
{
    return juce::AudioParameterFloatAttributes{}
        .withLabel("ms")
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(value, 1) + " ms";
        })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto id = [](const char* value) { return juce::ParameterID{value, 1}; };

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        id(threshold), "Threshold", juce::NormalisableRange<float>{domain::thresholdMinimumDb, domain::thresholdMaximumDb, 0.01f, 2.0f}, -18.0f,
        numericAttributes("dB", 1)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        id(ratio), "Ratio", juce::NormalisableRange<float>{domain::ratioMinimum, domain::ratioMaximum, 0.01f, 0.35f}, 2.0f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + ":1"; })
            .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(knee), "Knee", juce::NormalisableRange<float>{domain::kneeMinimumDb, domain::kneeMaximumDb}, 6.0f,
        numericAttributes("dB", 2)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(attack), "Attack", logarithmicRange(domain::attackMinimumMs, domain::attackMaximumMs), 10.0f,
        timeAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(hold), "Hold", juce::NormalisableRange<float>{domain::holdMinimumMs, domain::holdMaximumMs}, 0.0f,
        numericAttributes("ms", 2)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(release), "Release", logarithmicRange(domain::releaseMinimumMs, domain::releaseMaximumMs), 100.0f,
        timeAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(lookahead), "Lookahead", juce::NormalisableRange<float>{domain::lookaheadMinimumMs, domain::lookaheadMaximumMs}, 0.0f,
        numericAttributes("ms", 3)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(range), "Range", juce::NormalisableRange<float>{domain::rangeMinimumDb, domain::rangeMaximumDb}, 0.0f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) { return v < 0.05f ? juce::String("OFF") : juce::String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& s) { return s.containsIgnoreCase("off") ? 0.0f : s.getFloatValue(); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(makeup), "Makeup", juce::NormalisableRange<float>{domain::makeupMinimumDb, domain::makeupMaximumDb}, 0.0f,
        numericAttributes("dB", 2)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(mix), "Mix", juce::NormalisableRange<float>{domain::mixMinimumPercent, domain::mixMaximumPercent}, 100.0f,
        numericAttributes("%", 1)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(id(sidechainHighPass), "Sidechain Filter",
        logarithmicRange(domain::sidechainHighPassOffHz, domain::sidechainHighPassMaximumHz), domain::sidechainHighPassOffHz,
        juce::AudioParameterFloatAttributes{}
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int)
            {
                return v <= domain::sidechainHighPassOffHz + 0.5f ? juce::String("OFF") : juce::String(juce::roundToInt(v)) + " Hz";
            })
            .withValueFromStringFunction([](const juce::String& s)
            {
                return s.containsIgnoreCase("off") ? domain::sidechainHighPassOffHz : s.getFloatValue();
            })));
    layout.add(std::make_unique<juce::AudioParameterBool>(id(externalSidechain), "External Sidechain", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(id(bypass), "Bypass", false));
    return layout;
}
}
