#pragma once

#include "AssistantTypes.h"

#include <atomic>

namespace letscomp::assistant
{
// The assistant needs a few seconds of playing to see the part's dynamics.
inline constexpr double minimumCaptureSeconds = 3.0;
inline constexpr double minimumActiveSeconds = 1.0;

const StyleTemplate& styleTemplate(SourceType, Style) noexcept;

struct SimulationResult
{
    std::vector<float> gainReductionDb;        // per sample
    std::vector<std::vector<float>> output;    // empty unless requested
};

// Runs the real-time CompressorEngine offline over the capture. Allocates.
SimulationResult simulate(const CaptureData&, const dsp::CompressorSettings&, bool keepOutput);

// Picks ratio, knee and attack from the template, release from the part's
// onset spacing, then solves the threshold by bisection against the real
// engine so the loud frames get the template's gain reduction (scaled by the
// amount), and finally
// matches loudness with makeup. Background thread only. Returns early with
// succeeded == false when cancel becomes true.
AssistantResult recommend(const CaptureData&, const SignalAnalysis&, SourceType, Style,
                          Amount = Amount::medium, const std::atomic<bool>* cancel = nullptr);
}
