#pragma once

#include "AssistantTypes.h"

namespace letscomp::assistant
{
// Background thread only: allocates.
SignalAnalysis analyseSignal(const CaptureData& capture);

// ITU-R BS.1770 integrated loudness (K-weighting, 400 ms blocks, absolute and
// relative gates). nullopt when nothing passes the gates.
std::optional<double> integratedLoudness(const std::vector<std::vector<float>>& channels,
                                         double sampleRate);

// Linear interpolated percentile of an unsorted copy. Returns fallback when empty.
float percentile(std::vector<float> values, double fraction, float fallback = -160.0f);
}
