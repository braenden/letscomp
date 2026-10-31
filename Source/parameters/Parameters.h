#pragma once

#include "ParameterDomains.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace letscomp::params
{
inline constexpr auto threshold = "threshold";
inline constexpr auto ratio = "ratio";
inline constexpr auto knee = "knee";
inline constexpr auto attack = "attack";
inline constexpr auto hold = "hold";
inline constexpr auto release = "release";
inline constexpr auto lookahead = "lookahead";
inline constexpr auto range = "range";
inline constexpr auto makeup = "makeup";
inline constexpr auto mix = "mix";
inline constexpr auto sidechainHighPass = "sidechainHighPass";
inline constexpr auto externalSidechain = "externalSidechain";
inline constexpr auto bypass = "bypass";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
