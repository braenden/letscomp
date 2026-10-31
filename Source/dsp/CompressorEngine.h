#pragma once

#include "EnvelopeFollower.h"
#include "GainComputer.h"
#include "LookaheadDelay.h"
#include "PeakDetector.h"
#include "SidechainFilter.h"
#include "../visualization/ScopeData.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <span>

namespace letscomp::dsp
{
struct CompressorSettings
{
    float thresholdDb = -18.0f, ratio = 2.0f, kneeDb = 6.0f;
    float attackMs = 10.0f, holdMs = 0.0f, releaseMs = 100.0f;
    float rangeDb = 0.0f, makeupDb = 0.0f, mix = 1.0f;
    // High-pass on what the detector hears. At or below 20 Hz it is off.
    float sidechainHighPassHz = 0.0f;
    int lookaheadSamples = 0;
    // Detect from the sidechain input passed to process() instead of the main input.
    bool externalSidechain = false;
    bool bypass = false;
};

inline constexpr float sidechainHighPassOffHz = 20.0f;

class CompressorEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channels);
    void reset() noexcept;
    void setSettings(const CompressorSettings&) noexcept;
    // Without a sidechain buffer, external detection falls back to the main input.
    void process(juce::AudioBuffer<float>&, visualization::ScopeFifo<8192>&,
                 visualization::MeterValues&, std::span<float> gainReductionTrace = {},
                 const juce::AudioBuffer<float>* sidechain = nullptr) noexcept;
    float currentGainReductionDb() const noexcept { return envelope.current(); }
    // Program delay in samples. Equals the lookahead, so it is also the latency
    // the plug-in must report to the host.
    int latencySamples() const noexcept { return programDelay.getDelaySamples(); }
    int maximumLookaheadSamples() const noexcept { return maxLookaheadSamples; }

private:
    void resetScopeBucket() noexcept;
    void updateMeter(std::atomic<float>& meter, float value, float decay) noexcept;
    void updateSmootherTargets(bool snapToTarget) noexcept;
    float nextRangeLimit(float unlimitedReductionDb) noexcept;

    CompressorSettings settings;
    EnvelopeFollower envelope;
    // Peak levels of the main input and the sidechain, each unfiltered and
    // through the sidechain filter. Switching fades between these steady levels,
    // never between raw samples, so a fade cannot step the gain.
    enum DetectorPath { mainDirect, mainFiltered, sidechainDirect, sidechainFiltered };
    std::array<PeakDetector, 4> detectors;
    SidechainFilter sidechainFilter;
    LookaheadDelay programDelay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ratioSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> effectBlendSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> filterBlendSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sourceBlendSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmoother;
    // Range off is a limit far above any reduction. The limit moves at most this
    // fast where it meets the reduction in use.
    static constexpr float noRangeLimitDb = 160.0f;
    static constexpr float rangeSlewDbPerSecond = 500.0f;
    float rangeLimitDb = noRangeLimitDb, rangeSlewPerSample = 0.01f;
    double currentSampleRate = 44100.0;
    double scopeInputEnergy = 0.0, scopeOutputEnergy = 0.0;
    int channels = 2, samplesPerScopeFrame = 44, scopeSampleCount = 0;
    int maxLookaheadSamples = 0;
    bool hasProcessedSamples = false;
    visualization::ScopeFrame scopeBucket;
};
}
