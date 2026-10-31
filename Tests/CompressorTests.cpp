#include "dsp/CompressorEngine.h"
#include "dsp/EnvelopeFollower.h"
#include "dsp/GainComputer.h"
#include "dsp/LookaheadDelay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int maximumLookahead = 960;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(float actual, float expected, float tolerance, const std::string& message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        std::cerr << "FAIL: " << message << " (actual " << actual
                  << ", expected " << expected << ", tolerance " << tolerance << ")\n";
        ++failures;
    }
}

void checkpoint(const char* name)
{
#if defined(LETSCOMP_SANITIZED_TESTS)
    std::cerr << "sanitizer checkpoint: " << name << std::endl;
#else
    juce::ignoreUnused(name);
#endif
}

std::vector<float> render(const std::vector<float>& input,
                          const letscomp::dsp::CompressorSettings& settings,
                          std::span<const int> blockPattern,
                          double renderSampleRate = sampleRate)
{
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    engine.prepare(renderSampleRate, 1024, 1);
    engine.setSettings(settings);
    engine.reset();

    std::vector<float> output(input.size(), 0.0f);
    std::size_t position = 0;
    std::size_t blockIndex = 0;
    while (position < input.size())
    {
        const auto requested = blockPattern.empty() ? 256 : blockPattern[blockIndex % blockPattern.size()];
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(requested),
                                                 input.size() - position);
        juce::AudioBuffer<float> block(1, static_cast<int>(count));
        std::copy_n(input.data() + position, count, block.getWritePointer(0));
        engine.process(block, fifo, meters);
        std::copy_n(block.getReadPointer(0), count, output.data() + position);
        position += count;
        ++blockIndex;
    }
    return output;
}

// Peak level above 2 kHz around a change, relative to the signal peak. The
// input is an 80 Hz sine, which has nothing up there, so anything found is a
// tick or click made by the change.
float clickLevelDb(const letscomp::dsp::CompressorSettings& before,
                   const letscomp::dsp::CompressorSettings& after, bool withSidechain = false)
{
    constexpr int blockSize = 512;
    constexpr int changeBlock = 94;
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    engine.prepare(sampleRate, blockSize, 1);
    engine.setSettings(before);
    engine.reset();
    std::vector<float> output;
    juce::AudioBuffer<float> block(1, blockSize), sidechain(1, blockSize);
    for (int blockIndex = 0; blockIndex < 2 * changeBlock; ++blockIndex)
    {
        engine.setSettings(blockIndex < changeBlock ? before : after);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto time = static_cast<double>(blockIndex * blockSize + sample) / sampleRate;
            block.setSample(0, sample, 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 80.0 * time)));
            sidechain.setSample(0, sample, 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 120.0 * time)));
        }
        engine.process(block, fifo, meters, {}, withSidechain ? &sidechain : nullptr);
        output.insert(output.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
        letscomp::visualization::ScopeFrame frame;
        while (fifo.pop(frame)) {}
    }

    auto highPassed = output;
    for (const auto q : {0.50980, 0.60134, 0.89998, 2.56292})
    {
        juce::IIRFilter filter;
        filter.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, 2000.0, q));
        filter.processSamples(highPassed.data(), static_cast<int>(highPassed.size()));
    }
    const auto change = static_cast<std::size_t>(changeBlock * blockSize);
    float signalPeak = 0.0f, clickPeak = 0.0f;
    for (std::size_t sample = change - 9600; sample < change + 12000; ++sample)
    {
        signalPeak = std::max(signalPeak, std::abs(output[sample]));
        clickPeak = std::max(clickPeak, std::abs(highPassed[sample]));
    }
    return juce::Decibels::gainToDecibels(clickPeak / signalPeak, -200.0f);
}

void testTransitionsDoNotClick()
{
    letscomp::dsp::CompressorSettings steady;
    steady.thresholdDb = -24.0f;
    steady.ratio = 4.0f;
    steady.kneeDb = 6.0f;
    steady.attackMs = 10.0f;
    steady.releaseMs = 100.0f;
    auto fast = steady;
    fast.ratio = 6.0f;
    fast.kneeDb = 3.0f;
    fast.attackMs = 0.1f;
    fast.releaseMs = 50.0f;

    auto bypassed = steady;
    bypassed.bypass = true;
    expect(clickLevelDb(steady, bypassed) < -90.0f, "switching bypass on does not tick");
    expect(clickLevelDb(bypassed, steady) < -90.0f, "switching bypass off does not tick");

    auto delayed = steady;
    delayed.lookaheadSamples = maximumLookahead;
    expect(clickLevelDb(steady, delayed) < -72.0f, "turning lookahead up does not tick");
    expect(clickLevelDb(delayed, steady) < -72.0f, "turning lookahead down does not tick");

    for (const auto& base : {steady, fast})
    {
        auto ranged = base;
        ranged.rangeDb = 3.0f;
        expect(clickLevelDb(base, ranged) < -58.0f, "switching Range on glides instead of snapping");
        expect(clickLevelDb(ranged, base) < -58.0f, "switching Range off glides instead of snapping");

        auto filtered = base;
        filtered.sidechainHighPassHz = 200.0f;
        expect(clickLevelDb(base, filtered) < -80.0f, "switching the sidechain filter on does not tick");
        expect(clickLevelDb(filtered, base) < -80.0f, "switching the sidechain filter off does not tick");
        auto moved = filtered;
        moved.sidechainHighPassHz = 60.0f;
        // A moving cutoff changes the level the detector hears, which with a
        // 0.1 ms attack behaves like any other quick level change.
        expect(clickLevelDb(filtered, moved) < -54.0f, "moving the sidechain filter does not tick");

        auto external = base;
        external.externalSidechain = true;
        expect(clickLevelDb(base, external, true) < -80.0f, "switching to the external sidechain does not tick");
        expect(clickLevelDb(external, base, true) < -80.0f, "switching back from the external sidechain does not tick");
    }
}

// Steady gain reduction (dB) on the last 100 ms of two seconds of `input`,
// optionally detected from `sidechainInput`.
float steadyReductionDb(const letscomp::dsp::CompressorSettings& settings, double inputHz, float inputLevel,
                        double sidechainHz = 0.0, float sidechainLevel = 0.0f, bool passSidechain = false)
{
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    constexpr int blockSize = 480;
    engine.prepare(sampleRate, blockSize, 1);
    engine.setSettings(settings);
    engine.reset();
    std::vector<float> trace(blockSize);
    juce::AudioBuffer<float> block(1, blockSize), sidechain(1, blockSize);
    float reduction = 0.0f;
    for (int blockIndex = 0; blockIndex < 200; ++blockIndex)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto time = static_cast<double>(blockIndex * blockSize + sample) / sampleRate;
            block.setSample(0, sample, inputLevel * static_cast<float>(std::sin(2.0 * std::numbers::pi * inputHz * time)));
            sidechain.setSample(0, sample, sidechainLevel * static_cast<float>(std::sin(2.0 * std::numbers::pi * sidechainHz * time)));
        }
        engine.process(block, fifo, meters, trace, passSidechain ? &sidechain : nullptr);
        letscomp::visualization::ScopeFrame frame;
        while (fifo.pop(frame)) {}
        if (blockIndex >= 190)
            for (const auto value : trace)
                reduction = std::max(reduction, value);
    }
    return reduction;
}

void testSidechain()
{
    letscomp::dsp::CompressorSettings settings;
    settings.thresholdDb = -24.0f;
    settings.ratio = 4.0f;
    settings.kneeDb = 0.0f;
    settings.attackMs = 5.0f;
    settings.releaseMs = 100.0f;

    // A 50 Hz tone 18 dB over the threshold gives 13.5 dB at 4:1.
    const auto unfiltered = steadyReductionDb(settings, 50.0, 0.5f);
    expectNear(unfiltered, 13.5f, 0.3f, "with the sidechain filter off, a low tone is compressed as before");
    auto filtered = settings;
    filtered.sidechainHighPassHz = 200.0f;
    expect(steadyReductionDb(filtered, 50.0, 0.5f) < 1.0f,
           "a 200 Hz sidechain filter keeps a 50 Hz tone from driving the compressor");
    expectNear(steadyReductionDb(filtered, 2000.0, 0.5f), 13.5f, 0.3f,
               "the sidechain filter leaves content well above its cutoff alone");

    // The filter only shapes detection: with ratio 1 the output is the input.
    auto transparent = filtered;
    transparent.ratio = 1.0f;
    std::vector<float> lowTone(9600);
    for (std::size_t sample = 0; sample < lowTone.size(); ++sample)
        lowTone[sample] = 0.5f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 50.0
                                                             * static_cast<double>(sample) / sampleRate));
    const std::array<int, 1> blocks{256};
    const auto passed = render(lowTone, transparent, blocks);
    float difference = 0.0f;
    for (std::size_t sample = 0; sample < lowTone.size(); ++sample)
        difference = std::max(difference, std::abs(passed[sample] - lowTone[sample]));
    expectNear(difference, 0.0f, 1.0e-6f, "the sidechain filter never filters the audio itself");

    // External: a quiet track ducked by a loud sidechain.
    auto external = settings;
    external.externalSidechain = true;
    expect(steadyReductionDb(settings, 1000.0, 0.05f, 1000.0, 0.5f, true) < 0.01f,
           "with the internal sidechain, a loud sidechain input does nothing");
    expectNear(steadyReductionDb(external, 1000.0, 0.05f, 1000.0, 0.5f, true), 13.5f, 0.3f,
               "with the external sidechain, the sidechain input's level sets the compression");
    expect(steadyReductionDb(external, 1000.0, 0.05f, 1000.0, 0.0f, true) < 0.01f,
           "a silent sidechain leaves the track alone");
    expectNear(steadyReductionDb(external, 1000.0, 0.5f), 13.5f, 0.3f,
               "without a sidechain input, external detection falls back to the track");
}

void testSupportedSampleRates()
{
    letscomp::dsp::CompressorSettings settings;
    settings.mix = 0.0f;
    const std::array<int, 5> blocks{1, 17, 64, 257, 1024};
    for (const auto rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    {
        const auto rateText = std::to_string(static_cast<int>(rate)) + " Hz";
        const auto lookahead = static_cast<int>(std::round(rate * 0.020));
        std::vector<float> impulse(static_cast<std::size_t>(lookahead + 2), 0.0f);
        impulse[0] = 1.0f;

        settings.lookaheadSamples = 0;
        const auto direct = render(impulse, settings, blocks, rate);
        expectNear(direct[0], 1.0f, 0.0f, "zero lookahead adds no latency at " + rateText);

        settings.lookaheadSamples = lookahead;
        const auto output = render(impulse, settings, blocks, rate);
        const auto prefixIsSilent = std::all_of(output.begin(), output.begin() + lookahead,
                                                [](float value) { return value == 0.0f; });
        expect(prefixIsSilent, "maximum lookahead is silent before the impulse at " + rateText);
        expectNear(output[static_cast<std::size_t>(lookahead)], 1.0f, 0.0f,
                   "maximum lookahead latency is exact at " + rateText);
    }
}

std::vector<float> sineWave(double frequency, float amplitude, std::size_t length)
{
    std::vector<float> wave(length);
    for (std::size_t sample = 0; sample < wave.size(); ++sample)
        wave[sample] = amplitude * std::sin(static_cast<float>(2.0 * std::numbers::pi * frequency
                                                              * static_cast<double>(sample) / sampleRate));
    return wave;
}

float rmsDb(const std::vector<float>& signal, std::size_t first, std::size_t last)
{
    double energy = 0.0;
    for (auto sample = first; sample < last; ++sample)
        energy += static_cast<double>(signal[sample]) * static_cast<double>(signal[sample]);
    return static_cast<float>(10.0 * std::log10(energy / static_cast<double>(last - first) + 1.0e-30));
}

// Steady tones must be compressed by exactly the static curve, whatever the
// attack, release and pitch. The old detector compressed a 4:1 sine by 6 to
// 10.5 dB depending on attack and release, and distorted bass.
void testStaticCurveIsTimingIndependent()
{
    const std::array<int, 1> blocks{256};
    for (const auto frequency : {41.0, 100.0, 1000.0})
        for (const auto attack : {0.05f, 10.0f, 50.0f})
            for (const auto release : {5.0f, 100.0f})
            {
                letscomp::dsp::CompressorSettings settings;
                settings.thresholdDb = -20.0f;
                settings.ratio = 4.0f;
                settings.kneeDb = 0.0f;
                settings.attackMs = attack;
                settings.releaseMs = release;
                const auto input = sineWave(frequency, 0.5f, 96000);
                const auto output = render(input, settings, blocks);
                const auto reduction = rmsDb(input, 48000, 96000) - rmsDb(output, 48000, 96000);
                const auto label = std::to_string(static_cast<int>(frequency)) + " Hz, attack "
                                 + std::to_string(attack) + " ms, release " + std::to_string(release) + " ms";
                expectNear(reduction, 10.5f, 0.3f, "static curve holds at " + label);

                double fundamentalCos = 0.0, fundamentalSin = 0.0, energy = 0.0;
                for (std::size_t sample = 48000; sample < 96000; ++sample)
                {
                    const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
                    fundamentalCos += output[sample] * std::cos(phase);
                    fundamentalSin += output[sample] * std::sin(phase);
                    energy += static_cast<double>(output[sample]) * static_cast<double>(output[sample]);
                }
                const auto fundamental = 2.0 * (fundamentalCos * fundamentalCos
                                                + fundamentalSin * fundamentalSin) / 48000.0;
                const auto distortion = std::sqrt(std::max(0.0, energy - fundamental) / fundamental);
                expect(distortion < 0.01, "steady compression adds under 1 percent THD at " + label);
            }
}

void testStereoLinkAndScope()
{
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    letscomp::dsp::CompressorSettings settings;
    settings.thresholdDb = -20.0f;
    settings.ratio = 4.0f;
    settings.kneeDb = 0.0f;
    settings.attackMs = 0.05f;
    settings.mix = 1.0f;
    engine.prepare(sampleRate, 6000, 2);
    engine.setSettings(settings);
    engine.reset();

    juce::AudioBuffer<float> stereo(2, 6000);
    for (int sample = 0; sample < stereo.getNumSamples(); ++sample)
    {
        stereo.setSample(0, sample, 1.0f);
        stereo.setSample(1, sample, 0.25f);
    }
    engine.process(stereo, fifo, meters);
    expectNear(stereo.getSample(1, 5999) / stereo.getSample(0, 5999), 0.25f, 1.0e-6f,
               "stereo linking applies identical gain to both channels");

    engine.reset();
    stereo.setSize(2, 1200, false, false, true);
    settings.mix = 0.0f;
    engine.setSettings(settings);
    engine.reset();
    for (int sample = 0; sample < stereo.getNumSamples(); ++sample)
    {
        stereo.setSample(0, sample, 0.5f);
        stereo.setSample(1, sample, -0.5f);
    }
    engine.process(stereo, fifo, meters);
    letscomp::visualization::ScopeFrame frame;
    auto sawPositive = false;
    auto sawNegative = false;
    while (fifo.pop(frame))
    {
        sawPositive = sawPositive || frame.inputMax > 0.49f;
        sawNegative = sawNegative || frame.inputMin < -0.49f;
    }
    expect(sawPositive && sawNegative,
           "stereo scope preserves opposite-polarity channels instead of cancelling them");
}

void testNonFiniteContainment()
{
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    letscomp::dsp::CompressorSettings settings;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    settings.thresholdDb = nan;
    settings.ratio = nan;
    settings.kneeDb = nan;
    settings.attackMs = nan;
    settings.holdMs = nan;
    settings.releaseMs = nan;
    settings.rangeDb = nan;
    settings.makeupDb = nan;
    settings.mix = nan;
    settings.lookaheadSamples = std::numeric_limits<int>::max();
    engine.prepare(sampleRate, 512, 2);
    engine.setSettings(settings);
    engine.reset();

    juce::AudioBuffer<float> block(2, 2048);
    block.clear();
    block.setSample(0, 0, nan);
    block.setSample(1, 1, std::numeric_limits<float>::infinity());
    block.setSample(0, 2, -std::numeric_limits<float>::infinity());
    block.setSample(1, 1000, 0.5f);
    engine.process(block, fifo, meters);
    auto finiteOutput = true;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            finiteOutput = finiteOutput && std::isfinite(block.getSample(channel, sample));
    expect(finiteOutput, "NaN and infinity are contained before they enter persistent DSP state");

    letscomp::visualization::ScopeFrame frame;
    auto finiteScope = true;
    while (fifo.pop(frame))
        finiteScope = finiteScope && std::isfinite(frame.inputMin) && std::isfinite(frame.inputMax)
                    && std::isfinite(frame.outputMin) && std::isfinite(frame.outputMax)
                    && std::isfinite(frame.inputRms) && std::isfinite(frame.outputRms)
                    && std::isfinite(frame.gainReductionDb);
    expect(finiteScope, "non-finite audio cannot poison live-scope frames");
}

void testScopeFifoStress()
{
    constexpr std::size_t frameCount = 100000;
    letscomp::visualization::ScopeFifo<257> fifo;
    std::atomic<bool> orderIsValid{true};
    std::thread consumer([&]
    {
        letscomp::visualization::ScopeFrame frame;
        std::size_t expected = 0;
        while (expected < frameCount)
        {
            if (!fifo.pop(frame))
            {
                std::this_thread::yield();
                continue;
            }
            if (std::abs(frame.gainReductionDb - static_cast<float>(expected)) > 0.0f)
                orderIsValid.store(false, std::memory_order_relaxed);
            ++expected;
        }
    });
    for (std::size_t index = 0; index < frameCount; ++index)
    {
        letscomp::visualization::ScopeFrame frame;
        frame.gainReductionDb = static_cast<float>(index);
        while (!fifo.push(frame))
            std::this_thread::yield();
    }
    consumer.join();
    expect(orderIsValid.load(std::memory_order_relaxed),
           "scope FIFO preserves every frame under producer/consumer contention");
}

#if !defined(LETSCOMP_SANITIZED_TESTS)
void benchmarkRealtimePath()
{
    constexpr double benchmarkRate = 192000.0;
    constexpr int blockSize = 512;
    constexpr double audioSeconds = 5.0;
    letscomp::dsp::CompressorEngine engine;
    letscomp::visualization::ScopeFifo<8192> fifo;
    letscomp::visualization::MeterValues meters;
    letscomp::dsp::CompressorSettings settings;
    settings.thresholdDb = -24.0f;
    settings.ratio = 8.0f;
    settings.kneeDb = 12.0f;
    settings.attackMs = 0.1f;
    settings.releaseMs = 250.0f;
    settings.lookaheadSamples = static_cast<int>(benchmarkRate * 0.020);
    engine.prepare(benchmarkRate, blockSize, 2);
    engine.setSettings(settings);
    engine.reset();
    juce::AudioBuffer<float> block(2, blockSize);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto value = 0.8f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 997.0
                                       * static_cast<double>(sample) / benchmarkRate));
        block.setSample(0, sample, value);
        block.setSample(1, sample, -0.7f * value);
    }
    const auto blockCount = static_cast<int>(std::ceil(audioSeconds * benchmarkRate / blockSize));
    const auto start = std::chrono::steady_clock::now();
    for (int blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        engine.process(block, fifo, meters);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto realtimeLoad = elapsed / audioSeconds;
    std::cout << "192 kHz stereo DSP benchmark: " << elapsed << " s for " << audioSeconds
              << " s audio (" << realtimeLoad * 100.0 << "% of one realtime audio thread)\n";
    expect(realtimeLoad < 1.0, "DSP stays faster than realtime at 192 kHz stereo");
}
#endif
}

int main()
{
    checkpoint("main entered");
    using letscomp::dsp::GainComputer;

    expectNear(GainComputer::gainReductionDb(-20.0f, -20.0f, 4.0f, 0.0f),
               0.0f, 1.0e-6f, "hard knee is idle at threshold");
    expectNear(GainComputer::gainReductionDb(0.0f, -20.0f, 4.0f, 0.0f),
               15.0f, 1.0e-6f, "4:1 ratio produces the expected gain reduction");
    expectNear(GainComputer::gainReductionDb(0.0f, -20.0f, 4.0f, 0.0f, 6.0f),
               6.0f, 1.0e-6f, "range caps maximum gain reduction");

    const auto belowKnee = GainComputer::gainReductionDb(-23.001f, -20.0f, 4.0f, 6.0f);
    const auto insideKnee = GainComputer::gainReductionDb(-22.999f, -20.0f, 4.0f, 6.0f);
    expect(std::abs(insideKnee - belowKnee) < 0.001f,
           "soft knee remains continuous at its lower boundary");

    letscomp::dsp::LookaheadDelay delay;
    delay.prepare(sampleRate, 256, 1);
    delay.setDelaySamples(480);
    for (int sample = 0; sample <= 480; ++sample)
    {
        const auto output = delay.processSample(0, sample == 0 ? 1.0f : 0.0f);
        delay.advance();
        expectNear(output, sample == 480 ? 1.0f : 0.0f, 0.0f,
                   "delay emits an impulse at the exact selected sample");
    }

    letscomp::dsp::LookaheadDelay automatedDelay;
    automatedDelay.prepare(1000.0, 64, 1);
    for (int sample = 0; sample < 40; ++sample)
    {
        automatedDelay.processSample(0, static_cast<float>(sample));
        automatedDelay.advance();
    }
    automatedDelay.setDelaySamples(20);
    automatedDelay.processSample(0, 40.0f);
    automatedDelay.advance();
    automatedDelay.setDelaySamples(0);
    automatedDelay.processSample(0, 41.0f);
    automatedDelay.advance();
    automatedDelay.setDelaySamples(7);
    for (int sample = 42; sample < 70; ++sample)
    {
        automatedDelay.processSample(0, static_cast<float>(sample));
        automatedDelay.advance();
    }
    expect(automatedDelay.getDelaySamples() == 7,
           "rapid delay automation completes at the most recently requested value");

    for (const auto envelopeRate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        letscomp::dsp::EnvelopeFollower follower;
        follower.prepare(envelopeRate);
        follower.setTimes(10.0f, 0.0f, 100.0f);
        const auto samplesInTimeConstant = static_cast<int>(std::round(envelopeRate * 0.010));
        auto reduction = 0.0f;
        for (int sample = 0; sample < samplesInTimeConstant; ++sample)
            reduction = follower.process(20.0f);
        expectNear(reduction, 20.0f * (1.0f - std::exp(-1.0f)), 0.02f,
                   "attack timing is sample-rate independent");
    }
    checkpoint("gain, delay, and envelope");

    letscomp::dsp::CompressorSettings drySettings;
    drySettings.mix = 0.0f;
    std::vector<float> impulse(static_cast<std::size_t>(maximumLookahead + 2), 0.0f);
    impulse[0] = 1.0f;
    const std::array<int, 4> irregularBlocks{17, 257, 3, 511};
    auto lookaheadDrySettings = drySettings;
    lookaheadDrySettings.lookaheadSamples = maximumLookahead;
    const auto delayedImpulse = render(impulse, lookaheadDrySettings, irregularBlocks);
    for (int sample = 0; sample < maximumLookahead; ++sample)
        expectNear(delayedImpulse[static_cast<std::size_t>(sample)], 0.0f, 0.0f,
                   "compressor stays silent before its 20 ms lookahead delay");
    expectNear(delayedImpulse[maximumLookahead], 1.0f, 0.0f,
               "compressor applies exactly the selected lookahead as latency");

    letscomp::dsp::CompressorEngine latencyEngine;
    latencyEngine.prepare(sampleRate, 64, 1);
    latencyEngine.setSettings(lookaheadDrySettings);
    latencyEngine.reset();
    expect(latencyEngine.latencySamples() == maximumLookahead,
           "engine latency equals the lookahead");
    latencyEngine.setSettings(drySettings);
    latencyEngine.reset();
    expect(latencyEngine.latencySamples() == 0, "engine latency is zero without lookahead");

    std::vector<float> sine(6000);
    for (std::size_t sample = 0; sample < sine.size(); ++sample)
        sine[sample] = 0.4f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 997.0
                                  * static_cast<double>(sample) / sampleRate));
    const std::array<int, 1> fixedBlocks{64};
    const auto fixedRender = render(sine, drySettings, fixedBlocks);
    const auto irregularRender = render(sine, drySettings, irregularBlocks);
    float maximumBlockDifference = 0.0f;
    for (std::size_t sample = 0; sample < sine.size(); ++sample)
        maximumBlockDifference = std::max(maximumBlockDifference,
                                           std::abs(fixedRender[sample] - irregularRender[sample]));
    expectNear(maximumBlockDifference, 0.0f, 0.0f,
               "output is independent of host block boundaries");
    for (std::size_t sample = 0; sample < sine.size(); ++sample)
        expectNear(fixedRender[sample], sine[sample], 0.0f,
                   "zero-percent mix without lookahead is an exact, undelayed dry path");
    checkpoint("dry path and block patterns");

    letscomp::dsp::CompressorSettings compression;
    compression.thresholdDb = -20.0f;
    compression.ratio = 4.0f;
    compression.kneeDb = 0.0f;
    compression.attackMs = 0.05f;
    compression.releaseMs = 100.0f;
    compression.mix = 1.0f;
    std::vector<float> constant(6000, 1.0f);
    const auto compressed = render(constant, compression, fixedBlocks);
    expectNear(compressed.back(), juce::Decibels::decibelsToGain(-15.0f), 0.002f,
               "steady-state compressor gain matches threshold and ratio math");

    const auto compressedFixed = render(sine, compression, fixedBlocks);
    const auto compressedIrregular = render(sine, compression, irregularBlocks);
    float maximumCompressedBlockDifference = 0.0f;
    for (std::size_t sample = 0; sample < sine.size(); ++sample)
        maximumCompressedBlockDifference = std::max(maximumCompressedBlockDifference,
            std::abs(compressedFixed[sample] - compressedIrregular[sample]));
    expectNear(maximumCompressedBlockDifference, 0.0f, 1.0e-7f,
               "compressed output is independent of host block boundaries");

    auto parallelSettings = compression;
    parallelSettings.mix = 0.5f;
    const auto halfWet = render(sine, parallelSettings, fixedBlocks);
    const auto fullyWet = render(sine, compression, fixedBlocks);
    const auto fullyDry = render(sine, drySettings, fixedBlocks);
    float maximumMixError = 0.0f;
    for (std::size_t sample = 0; sample < sine.size(); ++sample)
        maximumMixError = std::max(maximumMixError,
            std::abs(halfWet[sample] - 0.5f * (fullyDry[sample] + fullyWet[sample])));
    expectNear(maximumMixError, 0.0f, 2.0e-7f,
               "parallel mix is phase-aligned and mathematically exact");
    checkpoint("compression and parallel mix");

    letscomp::dsp::CompressorSettings zeroLookahead = compression;
    zeroLookahead.attackMs = 5.0f;
    zeroLookahead.releaseMs = 100.0f;
    std::vector<float> transient(3000, 0.0f);
    std::fill(transient.begin() + 200, transient.begin() + 300, 1.0f);
    auto lookaheadSettings = zeroLookahead;
    lookaheadSettings.lookaheadSamples = maximumLookahead;
    const auto withoutLookahead = render(transient, zeroLookahead, fixedBlocks);
    const auto withLookahead = render(transient, lookaheadSettings, fixedBlocks);
    expect(withLookahead[200 + maximumLookahead] < withoutLookahead[200] * 0.8f,
           "lookahead catches a transient before it reaches the program output");
    checkpoint("lookahead");

    letscomp::dsp::CompressorEngine smoothingEngine;
    letscomp::visualization::ScopeFifo<8192> smoothingFifo;
    letscomp::visualization::MeterValues smoothingMeters;
    letscomp::dsp::CompressorSettings smoothingSettings;
    smoothingSettings.ratio = 1.0f;
    smoothingSettings.mix = 1.0f;
    smoothingEngine.prepare(sampleRate, 2200, 1);
    smoothingEngine.setSettings(smoothingSettings);
    smoothingEngine.reset();
    juce::AudioBuffer<float> smoothingBlock(1, 2200);
    for (int sample = 0; sample < smoothingBlock.getNumSamples(); ++sample)
        smoothingBlock.setSample(0, sample, 0.25f);
    smoothingEngine.process(smoothingBlock, smoothingFifo, smoothingMeters);
    const auto beforeAutomation = smoothingBlock.getSample(0, smoothingBlock.getNumSamples() - 1);

    smoothingSettings.makeupDb = 24.0f;
    smoothingEngine.setSettings(smoothingSettings);
    smoothingBlock.setSize(1, 1200, false, false, true);
    for (int sample = 0; sample < smoothingBlock.getNumSamples(); ++sample)
        smoothingBlock.setSample(0, sample, 0.25f);
    smoothingEngine.process(smoothingBlock, smoothingFifo, smoothingMeters);
    const auto firstAutomatedSample = smoothingBlock.getSample(0, 0);
    expect(std::abs(firstAutomatedSample - beforeAutomation) < 0.02f,
           "makeup automation is smoothed instead of creating a click");
    expect(smoothingBlock.getSample(0, 1100) > 3.5f,
           "makeup smoothing reaches its requested target");

    letscomp::dsp::CompressorEngine bypassEngine;
    letscomp::visualization::ScopeFifo<8192> bypassFifo;
    letscomp::visualization::MeterValues bypassMeters;
    bypassEngine.prepare(sampleRate, 6000, 1);
    bypassEngine.setSettings(compression);
    bypassEngine.reset();
    juce::AudioBuffer<float> bypassBlock(1, 6000);
    for (int sample = 0; sample < bypassBlock.getNumSamples(); ++sample)
        bypassBlock.setSample(0, sample, 1.0f);
    bypassEngine.process(bypassBlock, bypassFifo, bypassMeters);
    const auto beforeBypass = bypassBlock.getSample(0, 5999);
    auto bypassSettings = compression;
    bypassSettings.bypass = true;
    bypassEngine.setSettings(bypassSettings);
    bypassBlock.setSize(1, 1000, false, false, true);
    for (int sample = 0; sample < bypassBlock.getNumSamples(); ++sample)
        bypassBlock.setSample(0, sample, 1.0f);
    bypassEngine.process(bypassBlock, bypassFifo, bypassMeters);
    expect(std::abs(bypassBlock.getSample(0, 0) - beforeBypass) < 0.02f,
           "bypass begins without an audio discontinuity");
    expectNear(bypassBlock.getSample(0, 999), 1.0f, 1.0e-6f,
               "bypass reaches an exact latency-preserving dry path");
    checkpoint("smoothing and bypass");

    letscomp::dsp::CompressorEngine automationEngine;
    letscomp::visualization::ScopeFifo<8192> automationFifo;
    letscomp::visualization::MeterValues automationMeters;
    auto automationSettings = compression;
    automationEngine.prepare(sampleRate, 64, 1);
    automationEngine.setSettings(automationSettings);
    automationEngine.reset();
    juce::AudioBuffer<float> automationBlock(1, 64);
    auto automationIsFinite = true;
    auto maximumAutomationOutput = 0.0f;
    for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
    {
        automationSettings.lookaheadSamples = blockIndex % 3 == 0 ? 0
                                            : blockIndex % 3 == 1 ? maximumLookahead : 137;
        automationSettings.thresholdDb = blockIndex % 2 == 0 ? -36.0f : -6.0f;
        automationSettings.rangeDb = blockIndex % 5 == 0 ? 0.0f : 3.0f;
        automationSettings.mix = blockIndex % 7 == 0 ? 0.0f : 1.0f;
        automationSettings.bypass = blockIndex % 11 == 0;
        automationEngine.setSettings(automationSettings);
        for (int sample = 0; sample < automationBlock.getNumSamples(); ++sample)
            automationBlock.setSample(0, sample, 0.7f * std::sin(static_cast<float>(
                2.0 * std::numbers::pi * 997.0 * (blockIndex * 64 + sample) / sampleRate)));
        automationEngine.process(automationBlock, automationFifo, automationMeters);
        for (int sample = 0; sample < automationBlock.getNumSamples(); ++sample)
        {
            const auto value = automationBlock.getSample(0, sample);
            automationIsFinite = automationIsFinite && std::isfinite(value);
            maximumAutomationOutput = std::max(maximumAutomationOutput, std::abs(value));
        }
    }
    expect(automationIsFinite && maximumAutomationOutput <= 0.701f,
           "extreme simultaneous automation remains finite and cannot create overshoot spikes");
    checkpoint("automation");

    testTransitionsDoNotClick();
    checkpoint("click-free transitions");
    testSidechain();
    checkpoint("sidechain");
    testSupportedSampleRates();
    checkpoint("sample rates");
    testStaticCurveIsTimingIndependent();
    checkpoint("static curve");
    testStereoLinkAndScope();
    checkpoint("stereo and scope");
    testNonFiniteContainment();
    checkpoint("non-finite containment");
    testScopeFifoStress();
    checkpoint("FIFO stress");
#if !defined(LETSCOMP_SANITIZED_TESTS)
    benchmarkRealtimePath();
#endif

    if (failures == 0)
        std::cout << "All LetsComp compressor tests passed\n";
    return failures == 0 ? 0 : 1;
}
