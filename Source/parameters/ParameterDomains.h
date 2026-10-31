#pragma once

namespace letscomp::params::domain
{
inline constexpr float thresholdMinimumDb = -60.0f, thresholdMaximumDb = 0.0f;
inline constexpr float ratioMinimum = 1.0f, ratioMaximum = 20.0f;
inline constexpr float kneeMinimumDb = 0.0f, kneeMaximumDb = 24.0f;
inline constexpr float attackMinimumMs = 0.05f, attackMaximumMs = 250.0f;
inline constexpr float holdMinimumMs = 0.0f, holdMaximumMs = 500.0f;
inline constexpr float releaseMinimumMs = 1.0f, releaseMaximumMs = 3000.0f;
inline constexpr float lookaheadMinimumMs = 0.0f, lookaheadMaximumMs = 20.0f;
inline constexpr float rangeMinimumDb = 0.0f, rangeMaximumDb = 60.0f;
inline constexpr float makeupMinimumDb = -24.0f, makeupMaximumDb = 24.0f;
inline constexpr float mixMinimumPercent = 0.0f, mixMaximumPercent = 100.0f;
// The lowest value is Off.
inline constexpr float sidechainHighPassOffHz = 20.0f, sidechainHighPassMaximumHz = 500.0f;
}
