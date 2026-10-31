#include <algorithm>
#include "assistant/CaptureBuffer.h"
#include "assistant/Recommender.h"
#include "assistant/SignalAnalyzer.h"
#include "parameters/ParameterDomains.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>

namespace
{
using namespace letscomp::assistant;
constexpr double sampleRate = 48000.0;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        std::cerr << "FAIL: " << message << " (actual " << actual << ", expected " << expected
                  << ", tolerance " << tolerance << ")\n";
        ++failures;
    }
}

float gain(float decibels) { return std::pow(10.0f, decibels / 20.0f); }

// Harmonic tone shaped into syllables of 70 percent length, grouped in phrases
// of six with a pause between and a different level per phrase.
CaptureData vocalLike(double seconds, double syllableMs, float levelOffsetDb = 0.0f)
{
    CaptureData capture;
    capture.sampleRate = sampleRate;
    const auto frames = static_cast<std::size_t>(seconds * sampleRate);
    capture.channels.assign(2, std::vector<float>(frames, 0.0f));
    std::mt19937 random(7);
    std::uniform_real_distribution<float> phraseLevel(-20.0f, -6.0f);
    std::normal_distribution<float> noise(0.0f, 0.02f);
    const auto syllable = syllableMs * 0.001 * sampleRate;
    const auto pause = 0.3 * sampleRate;
    std::size_t position = 0;
    while (position < frames)
    {
        const auto level = gain(phraseLevel(random) + levelOffsetDb);
        for (int index = 0; index < 6 && position < frames; ++index)
        {
            const auto length = static_cast<std::size_t>(0.7 * syllable);
            for (std::size_t offset = 0; offset < length && position + offset < frames; ++offset)
            {
                const auto time = static_cast<double>(position + offset) / sampleRate;
                const auto shape = std::min(1.0, static_cast<double>(offset) / (0.015 * sampleRate))
                                 * std::min(1.0, static_cast<double>(length - offset) / (0.03 * sampleRate));
                double tone = 0.0;
                for (int harmonic = 1; harmonic <= 8; ++harmonic)
                    tone += std::sin(2.0 * std::numbers::pi * 180.0 * harmonic * time) / harmonic;
                const auto value = static_cast<float>(0.6 * tone * shape) * level + noise(random) * level;
                capture.channels[0][position + offset] = value;
                capture.channels[1][position + offset] = 0.9f * value;
            }
            position += static_cast<std::size_t>(syllable);
        }
        position += static_cast<std::size_t>(pause);
    }
    return capture;
}

// Kick and snare alternating every interval, with quiet hats in between.
CaptureData drumLike(double seconds, double intervalMs)
{
    CaptureData capture;
    capture.sampleRate = sampleRate;
    const auto frames = static_cast<std::size_t>(seconds * sampleRate);
    capture.channels.assign(1, std::vector<float>(frames, 0.0f));
    std::mt19937 random(11);
    std::normal_distribution<float> noise(0.0f, 0.3f);
    const auto interval = static_cast<std::size_t>(intervalMs * 0.001 * sampleRate);
    const auto addHit = [&](std::size_t start, float levelDb, double decayMs)
    {
        const auto length = static_cast<std::size_t>(0.25 * sampleRate);
        for (std::size_t offset = 0; offset < length && start + offset < frames; ++offset)
        {
            const auto envelope = std::exp(-static_cast<double>(offset) / (decayMs * 0.001 * sampleRate));
            capture.channels[0][start + offset] += static_cast<float>(envelope) * gain(levelDb) * noise(random);
        }
    };
    for (std::size_t hit = 0; hit * interval < frames; ++hit)
    {
        addHit(hit * interval, hit % 2 == 0 ? -4.0f : -10.0f, 60.0);
        addHit(hit * interval + interval / 2, -30.0f, 15.0);
    }
    return capture;
}

void testLoudness()
{
    std::vector<std::vector<float>> sine(1, std::vector<float>(static_cast<std::size_t>(10.0 * sampleRate)));
    for (std::size_t sample = 0; sample < sine[0].size(); ++sample)
        sine[0][sample] = 0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 997.0
                                                             * static_cast<double>(sample) / sampleRate));
    const auto loudness = integratedLoudness(sine, sampleRate);
    expect(loudness.has_value(), "a steady sine has integrated loudness");
    if (loudness.has_value())
        expectNear(*loudness, -23.01, 0.05, "BS.1770: a -20 dBFS 997 Hz sine on one channel reads -23.0 LUFS");

    const std::vector<std::vector<float>> silence(2, std::vector<float>(48000 * 5, 0.0f));
    expect(!integratedLoudness(silence, sampleRate).has_value(), "silence is gated away");
}

void testRejectsUnusableInput()
{
    CaptureData silence;
    silence.sampleRate = sampleRate;
    silence.channels.assign(2, std::vector<float>(static_cast<std::size_t>(10.0 * sampleRate), 0.0f));
    const auto silent = recommend(silence, analyseSignal(silence), SourceType::vocal, Style::control);
    expect(!silent.succeeded && !silent.message.empty(), "silence is rejected with an explanation");

    const auto shortTake = vocalLike(2.0, 220.0);
    const auto tooShort = recommend(shortTake, analyseSignal(shortTake), SourceType::vocal, Style::control);
    expect(!tooShort.succeeded && !tooShort.message.empty(), "a two-second take is rejected");
}

bool withinDomains(const letscomp::dsp::CompressorSettings& settings)
{
    using namespace letscomp::params::domain;
    return settings.thresholdDb >= thresholdMinimumDb && settings.thresholdDb <= thresholdMaximumDb
        && settings.ratio >= ratioMinimum && settings.ratio <= ratioMaximum
        && settings.kneeDb >= kneeMinimumDb && settings.kneeDb <= kneeMaximumDb
        && settings.attackMs >= attackMinimumMs && settings.attackMs <= attackMaximumMs
        && settings.releaseMs >= releaseMinimumMs && settings.releaseMs <= releaseMaximumMs
        && settings.makeupDb >= makeupMinimumDb && settings.makeupDb <= makeupMaximumDb
        && settings.lookaheadSamples == 0;
}

void testVocal()
{
    const auto capture = vocalLike(10.0, 220.0);
    const auto analysis = analyseSignal(capture);
    expect(analysis.medianOnsetIntervalMs.has_value(), "vocal syllable spacing is found");
    if (analysis.medianOnsetIntervalMs.has_value())
        expectNear(*analysis.medianOnsetIntervalMs, 220.0, 20.0, "vocal syllable spacing matches the take");
    expect(analysis.loudLevelDb > analysis.quietLevelDb + 6.0f, "phrase levels give a measurable spread");

    std::array<float, 4> achieved{};
    std::array<float, 4> attack{};
    for (const auto style : allStyles)
    {
        const auto name = "vocal " + std::string(identifier(style));
        const auto result = recommend(capture, analysis, SourceType::vocal, style);
        expect(result.succeeded && result.recommendation.has_value(), name + " succeeds");
        if (!result.recommendation.has_value())
            continue;
        const auto& recommendation = *result.recommendation;
        expect(recommendation.targetReached, name + " reaches its target");
        expectNear(recommendation.achievedGainReductionDb, recommendation.targetGainReductionDb, 0.4,
                   name + " lands on the target gain reduction");
        expect(withinDomains(recommendation.settings), name + " stays inside the parameter domains");
        expect(recommendation.notes.size() >= 5 && !recommendation.summary.empty(), name + " explains itself");
        const auto names = {"Threshold", "Ratio", "Attack", "Release", "Makeup", "Mix"};
        expect(std::all_of(names.begin(), names.end(), [&](const char* parameter)
               {
                   return std::any_of(recommendation.notes.begin(), recommendation.notes.end(),
                                      [&](const auto& note) { return note.parameter == parameter && !note.text.empty(); });
               }),
               name + " explains every main knob");

        // Makeup must bring the processed take back to the input loudness.
        const auto output = simulate(capture, recommendation.settings, true).output;
        const auto inputLoudness = integratedLoudness(capture.channels, sampleRate);
        const auto outputLoudness = integratedLoudness(output, sampleRate);
        expect(inputLoudness.has_value() && outputLoudness.has_value(), name + " loudness is measurable");
        if (inputLoudness.has_value() && outputLoudness.has_value())
            expectNear(*outputLoudness, *inputLoudness, 0.15, name + " makeup matches input loudness");

        achieved[static_cast<std::size_t>(style)] = recommendation.achievedGainReductionDb;
        attack[static_cast<std::size_t>(style)] = recommendation.settings.attackMs;
    }
    expect(achieved[static_cast<std::size_t>(Style::control)] > achieved[static_cast<std::size_t>(Style::gentle)] + 2.0f,
           "Control compresses clearly harder than Gentle");
    expect(attack[static_cast<std::size_t>(Style::punch)] < attack[static_cast<std::size_t>(Style::control)],
           "Upfront catches each syllable faster than Control");
    expect(achieved[static_cast<std::size_t>(Style::punch)] > achieved[static_cast<std::size_t>(Style::control)] + 1.0f,
           "Upfront compresses harder than Control");
    expect(displayName(SourceType::vocal, Style::punch) == "Upfront"
               && displayName(SourceType::drums, Style::punch) == "Punch",
           "the punch slot is called Upfront on a vocal only");

    const auto first = recommend(capture, analysis, SourceType::vocal, Style::control);
    const auto second = recommend(capture, analyseSignal(capture), SourceType::vocal, Style::control);
    expect(first.recommendation.has_value() && second.recommendation.has_value()
               && std::abs(first.recommendation->settings.thresholdDb - second.recommendation->settings.thresholdDb) < 1.0e-6f
               && std::abs(first.recommendation->settings.makeupDb - second.recommendation->settings.makeupDb) < 1.0e-6f,
           "the same take always gives the same recommendation");
}

void testAmounts()
{
    const auto capture = vocalLike(10.0, 220.0);
    const auto analysis = analyseSignal(capture);
    std::array<float, 3> achieved{};
    for (const auto amount : allAmounts)
    {
        const auto name = "vocal control " + std::string(identifier(amount));
        const auto result = recommend(capture, analysis, SourceType::vocal, Style::control, amount);
        expect(result.recommendation.has_value() && result.recommendation->targetReached, name + " reaches its target");
        if (!result.recommendation.has_value())
            continue;
        expectNear(result.recommendation->targetGainReductionDb,
                   styleTemplate(SourceType::vocal, Style::control).targetGainReductionDb * targetScale(amount), 1.0e-4,
                   name + " scales the style target");
        expectNear(result.recommendation->achievedGainReductionDb, result.recommendation->targetGainReductionDb, 0.4,
                   name + " lands on the scaled target");
        expect(result.recommendation->amount == amount, name + " records the amount");
        achieved[static_cast<std::size_t>(amount)] = result.recommendation->achievedGainReductionDb;
    }
    expect(achieved[0] + 1.5f < achieved[1] && achieved[1] + 1.5f < achieved[2], "Light, Medium and Heavy step up clearly");

    const auto upfront = recommend(capture, analysis, SourceType::vocal, Style::punch, Amount::heavy);
    expect(upfront.recommendation.has_value() && upfront.recommendation->targetReached
               && upfront.recommendation->achievedGainReductionDb > 10.0f,
           "Heavy Upfront gives more than 10 dB on the loud parts");
}

void testDrums()
{
    const auto capture = drumLike(10.0, 250.0);
    const auto analysis = analyseSignal(capture);
    expect(analysis.medianOnsetIntervalMs.has_value(), "drum hit spacing is found");
    if (analysis.medianOnsetIntervalMs.has_value())
        expectNear(*analysis.medianOnsetIntervalMs, 250.0, 15.0, "quiet hats do not count as hits");
    const auto punch = recommend(capture, analysis, SourceType::drums, Style::punch);
    expect(punch.succeeded && punch.recommendation.has_value(), "drum punch succeeds");
    if (punch.recommendation.has_value())
    {
        expectNear(punch.recommendation->settings.releaseMs, 150.0, 10.0,
                   "punch release recovers in 60 percent of the hit spacing");
        expectNear(punch.recommendation->achievedGainReductionDb, punch.recommendation->targetGainReductionDb, 0.4,
                   "drum punch lands on the target gain reduction");
        expect(punch.recommendation->settings.attackMs > styleTemplate(SourceType::drums, Style::control).attackMs,
               "drum punch lets more of each hit through than Control");
    }
}

void testQuietInput()
{
    const auto capture = vocalLike(10.0, 220.0, -44.0f);
    const auto result = recommend(capture, analyseSignal(capture), SourceType::vocal, Style::control);
    expect(result.succeeded && result.recommendation.has_value(), "a very quiet take still gets a starting point");
    if (result.recommendation.has_value())
    {
        expect(!result.recommendation->targetReached, "a very quiet take reports the target as out of reach");
        expectNear(result.recommendation->settings.thresholdDb, letscomp::params::domain::thresholdMinimumDb, 0.01,
                   "a very quiet take uses the lowest threshold");
    }
}

void testCaptureBuffer()
{
    CaptureBuffer buffer;
    buffer.prepare(1000.0, 2);
    buffer.arm();
    juce::AudioBuffer<float> block(2, 400);
    block.clear();
    buffer.push(block);
    expect(buffer.currentState() == CaptureBuffer::State::waiting, "silence does not start the capture");

    block.setSample(0, 100, 0.5f);
    block.setSample(1, 101, std::numeric_limits<float>::quiet_NaN());
    buffer.push(block);
    expect(buffer.currentState() == CaptureBuffer::State::capturing, "the first audible sample starts the capture");
    expectNear(buffer.capturedSeconds(), 0.3, 1.0e-9, "capture starts at the first audible sample");

    for (int pass = 0; pass < 40; ++pass)
        buffer.push(block);
    expect(buffer.currentState() == CaptureBuffer::State::full, "capture stops by itself after ten seconds");
    const auto data = buffer.copy();
    expect(data.frames() == 10000 && data.channels.size() == 2, "the copy holds exactly ten seconds");
    expect(std::abs(data.channels[0][0] - 0.5f) < 1.0e-9f && std::abs(data.channels[1][1]) < 1.0e-9f,
           "captured audio starts at the onset and non-finite samples are zeroed");

    buffer.arm();
    expect(buffer.currentState() == CaptureBuffer::State::waiting, "re-arming waits for the next onset");
    buffer.stop();
    buffer.push(block);
    expect(buffer.currentState() == CaptureBuffer::State::idle, "a stopped capture ignores input");
}
}

int main()
{
    testLoudness();
    testRejectsUnusableInput();
    testVocal();
    testAmounts();
    testDrums();
    testQuietInput();
    testCaptureBuffer();
    if (failures == 0)
        std::cout << "All LetsComp assistant tests passed\n";
    return failures == 0 ? 0 : 1;
}
