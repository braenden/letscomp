#include "CompressorEngine.h"

namespace letscomp::dsp
{
namespace
{
// An S-shaped fade has no corners at either end, which a linear one would
// leave as a faint tick on low notes.
float sCurve(float position) noexcept
{
    return position * position * (3.0f - 2.0f * position);
}
}

void CompressorEngine::prepare(double sampleRate, int maximumBlockSize, int newChannels)
{
    currentSampleRate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 1.0, 768000.0)
                                                  : 44100.0;
    channels = std::clamp(newChannels, 1, 2);
    samplesPerScopeFrame = std::max(1, static_cast<int>(std::round(currentSampleRate / 1000.0)));
    maxLookaheadSamples = static_cast<int>(std::round(currentSampleRate * 0.020));
    programDelay.prepare(currentSampleRate, maximumBlockSize, channels);
    for (auto& detector : detectors)
        detector.prepare(currentSampleRate);
    envelope.prepare(currentSampleRate);
    sidechainFilter.prepare(currentSampleRate);
    for (auto* smoother : {&thresholdSmoother, &ratioSmoother, &kneeSmoother, &makeupGainSmoother,
                           &mixSmoother, &effectBlendSmoother, &filterBlendSmoother, &sourceBlendSmoother})
        smoother->reset(currentSampleRate, 0.020);
    // Moving the cutoff changes the level the detector sees, so it glides slowly.
    cutoffSmoother.reset(currentSampleRate, 0.100);
    cutoffSmoother.setCurrentAndTargetValue(100.0f);
    sidechainFilter.setCutoff(100.0f);
    rangeSlewPerSample = static_cast<float>(rangeSlewDbPerSecond / currentSampleRate);
    reset();
}

void CompressorEngine::reset() noexcept
{
    programDelay.reset();
    programDelay.setDelaySamples(settings.lookaheadSamples);
    for (auto& detector : detectors)
        detector.reset();
    envelope.reset();
    sidechainFilter.reset();
    updateSmootherTargets(true);
    hasProcessedSamples = false;
    scopeSampleCount = 0;
    resetScopeBucket();
}

void CompressorEngine::setSettings(const CompressorSettings& newSettings) noexcept
{
    const auto finiteOr = [](float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    };
    auto sanitised = newSettings;
    sanitised.thresholdDb = std::clamp(finiteOr(sanitised.thresholdDb, -18.0f), -160.0f, 24.0f);
    sanitised.ratio = std::clamp(finiteOr(sanitised.ratio, 2.0f), 1.0f, 1000.0f);
    sanitised.kneeDb = std::clamp(finiteOr(sanitised.kneeDb, 6.0f), 0.0f, 160.0f);
    sanitised.attackMs = std::clamp(finiteOr(sanitised.attackMs, 10.0f), 0.001f, 10000.0f);
    sanitised.holdMs = std::clamp(finiteOr(sanitised.holdMs, 0.0f), 0.0f, 10000.0f);
    sanitised.releaseMs = std::clamp(finiteOr(sanitised.releaseMs, 100.0f), 0.001f, 10000.0f);
    sanitised.rangeDb = std::clamp(finiteOr(sanitised.rangeDb, 0.0f), 0.0f, 160.0f);
    sanitised.makeupDb = std::clamp(finiteOr(sanitised.makeupDb, 0.0f), -96.0f, 96.0f);
    sanitised.mix = std::clamp(finiteOr(sanitised.mix, 1.0f), 0.0f, 1.0f);
    sanitised.sidechainHighPassHz = std::clamp(finiteOr(sanitised.sidechainHighPassHz, 0.0f), 0.0f, 2000.0f);
    sanitised.lookaheadSamples = std::clamp(sanitised.lookaheadSamples, 0, maxLookaheadSamples);

    const auto changed = [](float a, float b) { return std::abs(a - b) > 1.0e-7f; };
    const auto timesChanged = changed(sanitised.attackMs, settings.attackMs)
                           || changed(sanitised.holdMs, settings.holdMs)
                           || changed(sanitised.releaseMs, settings.releaseMs);
    const auto lookaheadChanged = sanitised.lookaheadSamples != settings.lookaheadSamples;
    settings = sanitised;
    if (timesChanged)
        envelope.setTimes(settings.attackMs, settings.holdMs, settings.releaseMs);
    if (lookaheadChanged)
        programDelay.setDelaySamples(settings.lookaheadSamples);
    updateSmootherTargets(!hasProcessedSamples);
}

void CompressorEngine::updateSmootherTargets(bool snapToTarget) noexcept
{
    if (snapToTarget)
        rangeLimitDb = settings.rangeDb > 0.0f ? settings.rangeDb : noRangeLimitDb;
    const auto makeupGain = juce::Decibels::decibelsToGain(settings.makeupDb);
    const auto set = [snapToTarget](auto& smoother, float target)
    {
        if (snapToTarget) smoother.setCurrentAndTargetValue(target);
        else smoother.setTargetValue(target);
    };
    set(thresholdSmoother, settings.thresholdDb);
    set(ratioSmoother, settings.ratio);
    set(kneeSmoother, settings.kneeDb);
    set(makeupGainSmoother, makeupGain);
    set(mixSmoother, std::clamp(settings.mix, 0.0f, 1.0f));
    set(effectBlendSmoother, settings.bypass ? 0.0f : 1.0f);
    set(sourceBlendSmoother, settings.externalSidechain ? 1.0f : 0.0f);

    // Off fades the filter out and leaves the cutoff where it was, so turning
    // it back on glides from the old setting.
    const auto filterOn = settings.sidechainHighPassHz > sidechainHighPassOffHz;
    set(filterBlendSmoother, filterOn ? 1.0f : 0.0f);
    if (filterOn)
    {
        set(cutoffSmoother, settings.sidechainHighPassHz);
        if (snapToTarget)
            sidechainFilter.setCutoff(settings.sidechainHighPassHz);
    }
}

float CompressorEngine::nextRangeLimit(float unlimitedReductionDb) noexcept
{
    // Above the reduction in use the limit does nothing, so it may jump there at
    // once. Where it meets the reduction it moves at a bounded rate, so turning
    // Range on or off glides instead of snapping the gain.
    const auto target = settings.rangeDb > 0.0f ? settings.rangeDb : noRangeLimitDb;
    const auto inUse = std::max(unlimitedReductionDb, envelope.current());
    if (rangeLimitDb > target)
        rangeLimitDb = std::max(target, std::min(rangeLimitDb, inUse) - rangeSlewPerSample);
    else if (rangeLimitDb < target)
        rangeLimitDb = rangeLimitDb >= inUse ? target : std::min(target, rangeLimitDb + rangeSlewPerSample);
    return rangeLimitDb;
}

void CompressorEngine::resetScopeBucket() noexcept
{
    scopeBucket.inputMin = scopeBucket.inputMax = 0.0f;
    scopeBucket.outputMin = scopeBucket.outputMax = 0.0f;
    scopeBucket.inputRms = scopeBucket.outputRms = 0.0f;
    scopeBucket.gainReductionDb = 0.0f;
    scopeInputEnergy = scopeOutputEnergy = 0.0;
}

void CompressorEngine::updateMeter(std::atomic<float>& meter, float value, float decay) noexcept
{
    const auto previous = meter.load(std::memory_order_relaxed);
    meter.store(std::max(value, previous * decay), std::memory_order_relaxed);
}

void CompressorEngine::process(juce::AudioBuffer<float>& buffer, visualization::ScopeFifo<8192>& fifo,
                               visualization::MeterValues& meters,
                               std::span<float> gainReductionTrace,
                               const juce::AudioBuffer<float>* sidechain) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = std::min(channels, buffer.getNumChannels());
    if (numSamples <= 0 || numChannels <= 0)
        return;
    const auto sidechainChannels = sidechain != nullptr && sidechain->getNumSamples() >= numSamples
                                 ? std::min(2, sidechain->getNumChannels()) : 0;
    std::array<float*, 2> data{};
    for (int ch = 0; ch < numChannels; ++ch)
        data[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    float blockInputPeak = 0.0f, blockOutputPeak = 0.0f, blockMaxGr = 0.0f;
    float displayedGr = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (cutoffSmoother.isSmoothing())
            sidechainFilter.setCutoff(cutoffSmoother.getNextValue());
        const auto filterBlend = sCurve(filterBlendSmoother.getNextValue());
        const auto sourceBlend = sCurve(sourceBlendSmoother.getNextValue());
        float mainDirectPeak = 0.0f, mainFilteredPeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& input = data[static_cast<size_t>(ch)][sample];
            if (!std::isfinite(input))
                input = 0.0f;
            mainDirectPeak = std::max(mainDirectPeak, std::abs(input));
            mainFilteredPeak = std::max(mainFilteredPeak, std::abs(sidechainFilter.process(ch, input)));
        }
        // The detectors run on undelayed input while the program path is delayed
        // by the lookahead, so gain reduction can start before a transient.
        const auto mainDirect = detectors[DetectorPath::mainDirect].process(mainDirectPeak);
        const auto mainFiltered = detectors[DetectorPath::mainFiltered].process(mainFilteredPeak);
        const auto mainLevel = mainDirect + filterBlend * (mainFiltered - mainDirect);
        auto sidechainLevel = mainLevel;
        if (sidechainChannels > 0)
        {
            float directPeak = 0.0f, filteredPeak = 0.0f;
            for (int ch = 0; ch < sidechainChannels; ++ch)
            {
                auto input = sidechain->getSample(ch, sample);
                input = std::isfinite(input) ? input : 0.0f;
                directPeak = std::max(directPeak, std::abs(input));
                filteredPeak = std::max(filteredPeak, std::abs(sidechainFilter.process(2 + ch, input)));
            }
            const auto direct = detectors[DetectorPath::sidechainDirect].process(directPeak);
            const auto filtered = detectors[DetectorPath::sidechainFiltered].process(filteredPeak);
            sidechainLevel = direct + filterBlend * (filtered - direct);
        }
        const auto detectorPeak = mainLevel + sourceBlend * (sidechainLevel - mainLevel);
        const auto detectorDb = juce::Decibels::gainToDecibels(detectorPeak, -160.0f);
        const auto thresholdDb = thresholdSmoother.getNextValue();
        const auto ratio = ratioSmoother.getNextValue();
        const auto kneeDb = kneeSmoother.getNextValue();
        const auto makeupGain = makeupGainSmoother.getNextValue();
        const auto dryWetMix = mixSmoother.getNextValue();
        const auto effectBlend = sCurve(effectBlendSmoother.getNextValue());
        const auto unlimitedGr = GainComputer::gainReductionDb(detectorDb, thresholdDb, ratio, kneeDb);
        const auto rangeLimit = nextRangeLimit(unlimitedGr);
        const auto smoothedGr = envelope.process(std::min(unlimitedGr, rangeLimit));
        const auto appliedGr = std::min(smoothedGr, rangeLimit);
        displayedGr = appliedGr * effectBlend;
        if (static_cast<std::size_t>(sample) < gainReductionTrace.size())
            gainReductionTrace[static_cast<std::size_t>(sample)] = displayedGr;
        const auto compressionGain = juce::Decibels::decibelsToGain(-appliedGr);
        double frameInputEnergy = 0.0, frameOutputEnergy = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto delayedInput = programDelay.processSample(ch, data[static_cast<size_t>(ch)][sample]);
            const auto wet = delayedInput * compressionGain * makeupGain;
            const auto compressedMix = delayedInput + dryWetMix * (wet - delayedInput);
            const auto output = delayedInput + effectBlend * (compressedMix - delayedInput);
            data[static_cast<size_t>(ch)][sample] = std::isfinite(output) ? output : 0.0f;
            scopeBucket.inputMin = std::min(scopeBucket.inputMin, delayedInput);
            scopeBucket.inputMax = std::max(scopeBucket.inputMax, delayedInput);
            scopeBucket.outputMin = std::min(scopeBucket.outputMin, data[static_cast<size_t>(ch)][sample]);
            scopeBucket.outputMax = std::max(scopeBucket.outputMax, data[static_cast<size_t>(ch)][sample]);
            frameInputEnergy += static_cast<double>(delayedInput) * static_cast<double>(delayedInput);
            frameOutputEnergy += static_cast<double>(data[static_cast<size_t>(ch)][sample])
                               * static_cast<double>(data[static_cast<size_t>(ch)][sample]);
            blockInputPeak = std::max(blockInputPeak, std::abs(delayedInput));
            blockOutputPeak = std::max(blockOutputPeak, std::abs(data[static_cast<size_t>(ch)][sample]));
        }
        programDelay.advance();

        scopeInputEnergy += frameInputEnergy / static_cast<double>(numChannels);
        scopeOutputEnergy += frameOutputEnergy / static_cast<double>(numChannels);
        scopeBucket.gainReductionDb = std::max(scopeBucket.gainReductionDb, displayedGr);
        blockMaxGr = std::max(blockMaxGr, displayedGr);

        if (++scopeSampleCount >= samplesPerScopeFrame)
        {
            const auto denominator = static_cast<double>(scopeSampleCount);
            scopeBucket.inputRms = static_cast<float>(std::sqrt(scopeInputEnergy / denominator));
            scopeBucket.outputRms = static_cast<float>(std::sqrt(scopeOutputEnergy / denominator));
            fifo.push(scopeBucket);
            scopeSampleCount = 0;
            resetScopeBucket();
        }
    }

    const auto decay = static_cast<float>(std::exp(-static_cast<double>(numSamples) / (currentSampleRate * 0.35)));
    updateMeter(meters.inputPeak, blockInputPeak, decay);
    updateMeter(meters.outputPeak, blockOutputPeak, decay);
    meters.currentGainReductionDb.store(displayedGr, std::memory_order_relaxed);
    updateMeter(meters.recentMaxGainReductionDb, blockMaxGr, decay);
    hasProcessedSamples = true;
}
}
