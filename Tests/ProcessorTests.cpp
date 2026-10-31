#include "PluginProcessor.h"
#include "parameters/Parameters.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

namespace
{
float rawValue(LetsCompAudioProcessor& processor, const char* id)
{
    return processor.parameters.getRawParameterValue(id)->load();
}

// A ten-second take with syllables every 200 ms at three phrase levels.
letscomp::assistant::CaptureData syllableTake()
{
    letscomp::assistant::CaptureData take;
    take.sampleRate = 48000.0;
    take.channels.assign(1, std::vector<float>(480000, 0.0f));
    for (std::size_t sample = 0; sample < take.channels[0].size(); ++sample)
    {
        const auto syllable = sample / 9600;
        const auto inside = sample % 9600 < 7000;
        const auto level = std::array{0.5f, 0.2f, 0.08f}[(syllable / 5) % 3];
        take.channels[0][sample] = inside ? level * std::sin(static_cast<float>(sample) * 0.05f) : 0.0f;
    }
    return take;
}

// Plays blocks of `signal` (sample index to value) through the processor and
// appends the left channel to `output`.
template <typename Signal>
void play(LetsCompAudioProcessor& processor, Signal signal, int blocks, std::size_t& position,
          std::vector<float>& output)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = signal(position + static_cast<std::size_t>(sample));
            block.setSample(0, sample, value);
            block.setSample(1, sample, value);
        }
        processor.processBlock(block, midi);
        output.insert(output.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
        position += blockSize;
    }
}

// Peak level above 2 kHz in [first, last) relative to the whole output's peak.
float clickLevelDb(std::vector<float> output, std::size_t first, std::size_t last)
{
    float signalPeak = 0.0f, clickPeak = 0.0f;
    for (const auto value : output)
        signalPeak = std::max(signalPeak, std::abs(value));
    for (const auto q : {0.50980, 0.60134, 0.89998, 2.56292})
    {
        juce::IIRFilter filter;
        filter.setCoefficients(juce::IIRCoefficients::makeHighPass(48000.0, 2000.0, q));
        filter.processSamples(output.data(), static_cast<int>(output.size()));
    }
    for (auto sample = first; sample < std::min(last, output.size()); ++sample)
        clickPeak = std::max(clickPeak, std::abs(output[sample]));
    return juce::Decibels::gainToDecibels(clickPeak / signalPeak, -200.0f);
}

float sine(double frequency, std::size_t sample)
{
    return 0.5f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * frequency
                                              * static_cast<double>(sample) / 48000.0));
}

void setLookahead(LetsCompAudioProcessor& processor, float milliseconds)
{
    auto* lookahead = processor.parameters.getParameter(letscomp::params::lookahead);
    lookahead->setValueNotifyingHost(lookahead->convertTo0to1(milliseconds));
}

// A transport jump makes the host call reset(). With lookahead on, the delay
// line must not play the end of the previous position at the new one.
void testHostResetClearsLookahead()
{
    LetsCompAudioProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    setLookahead(processor, 5.0f);
    processor.prepareToPlay(48000.0, 512);
    std::size_t position = 0;
    std::vector<float> before, after;
    play(processor, [](std::size_t n) { return sine(80.0, n); }, 94, position, before);
    processor.reset();
    std::size_t resumed = 0;
    play(processor, [](std::size_t n)
    {
        const auto fade = std::min(1.0f, static_cast<float>(n) / 240.0f);
        return sine(200.0, n) * fade * fade;
    }, 47, resumed, after);
    expect(clickLevelDb(after, 0, 1440) < -60.0f,
           "after a host reset, playback starts clean instead of with the old position's audio");
}

// Hosts restart the plug-in (reset, then prepareToPlay) to take a new latency,
// often while playing. The compressor keeps its state through that restart.
void testLatencyRestartKeepsPlaying()
{
    LetsCompAudioProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    std::size_t position = 0;
    std::vector<float> output;
    const auto tone = [](std::size_t n) { return sine(80.0, n); };
    play(processor, tone, 94, position, output);
    const auto change = output.size();
    setLookahead(processor, 5.0f);
    processor.synchroniseLatency();
    processor.reset();
    processor.releaseResources();
    processor.prepareToPlay(48000.0, 512);
    play(processor, tone, 94, position, output);
    expect(processor.getLatencySamples() == 240, "the restart reports the new lookahead as latency");
    expect(clickLevelDb(output, change - 240, change + 12000) < -72.0f,
           "a latency restart crossfades to the new lookahead instead of clicking");

    std::vector<float> later;
    processor.reset();
    std::size_t resumed = 0;
    play(processor, [](std::size_t n)
    {
        const auto fade = std::min(1.0f, static_cast<float>(n) / 240.0f);
        return sine(200.0, n) * fade * fade;
    }, 47, resumed, later);
    expect(clickLevelDb(later, 0, 1440) < -60.0f, "a host reset after the restart still clears the delay");
}

void setParameter(LetsCompAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Hosts route another track into the sidechain input for ducking.
void testSidechainBus()
{
    LetsCompAudioProcessor processor;
    auto layout = processor.getBusesLayout();
    expect(layout.inputBuses.size() == 2 && layout.getChannelSet(true, 1).isDisabled(),
           "the sidechain input exists and is off until the host enables it");
    auto monoSidechain = layout;
    monoSidechain.inputBuses.getReference(1) = juce::AudioChannelSet::mono();
    auto surroundSidechain = layout;
    surroundSidechain.inputBuses.getReference(1) = juce::AudioChannelSet::create5point1();
    expect(processor.checkBusesLayoutSupported(monoSidechain), "a mono sidechain is supported");
    expect(!processor.checkBusesLayoutSupported(surroundSidechain), "a surround sidechain is refused");
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    expect(processor.setBusesLayout(layout), "a stereo sidechain can be enabled");

    setParameter(processor, letscomp::params::threshold, -24.0f);
    setParameter(processor, letscomp::params::ratio, 4.0f);
    setParameter(processor, letscomp::params::knee, 0.0f);
    setParameter(processor, letscomp::params::attack, 5.0f);
    setParameter(processor, letscomp::params::externalSidechain, 1.0f);
    processor.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    juce::MidiBuffer midi;
    float outputPeak = 0.0f;
    for (int blockIndex = 0; blockIndex < 200; ++blockIndex)
    {
        for (int sample = 0; sample < 512; ++sample)
        {
            const auto tone = static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0
                                                          * (blockIndex * 512 + sample) / 48000.0));
            buffer.setSample(0, sample, 0.05f * tone);
            buffer.setSample(1, sample, 0.05f * tone);
            buffer.setSample(2, sample, 0.5f * tone);
            buffer.setSample(3, sample, 0.5f * tone);
        }
        processor.processBlock(buffer, midi);
        if (blockIndex >= 190)
            for (int sample = 0; sample < 512; ++sample)
                outputPeak = std::max(outputPeak, std::abs(buffer.getSample(0, sample)));
    }
    const auto reductionDb = juce::Decibels::gainToDecibels(0.05f / outputPeak);
    expect(std::abs(reductionDb - 13.5f) < 0.5f,
           "a loud sidechain ducks a quiet track by the sidechain's level");
}

void testAssistant()
{
    using letscomp::assistant::AssistantController;
    LetsCompAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    auto& assistant = processor.getAssistant();
    setParameter(processor, letscomp::params::sidechainHighPass, 150.0f);
    setParameter(processor, letscomp::params::externalSidechain, 1.0f);
    const auto thresholdBefore = rawValue(processor, letscomp::params::threshold);
    const auto mixBefore = rawValue(processor, letscomp::params::mix);
    const auto filterBefore = rawValue(processor, letscomp::params::sidechainHighPass);

    assistant.setStyle(letscomp::assistant::Style::punch);
    assistant.analyseAndApplyNow(syllableTake());
    expect(rawValue(processor, letscomp::params::sidechainHighPass) <= 20.5f
               && rawValue(processor, letscomp::params::externalSidechain) < 0.5f,
           "the assistant's result listens to the part itself, unfiltered");
    expect(assistant.phase() == AssistantController::Phase::ready && assistant.recommendation().has_value(),
           "the assistant produces a recommendation from a take");
    if (const auto& recommendation = assistant.recommendation(); recommendation.has_value())
    {
        expect(std::abs(rawValue(processor, letscomp::params::threshold) - recommendation->settings.thresholdDb) < 0.02f,
               "the recommended threshold reaches the host parameter");
        expect(std::abs(rawValue(processor, letscomp::params::attack) - recommendation->settings.attackMs) < 0.05f,
               "the recommended attack reaches the host parameter");
        expect(std::abs(rawValue(processor, letscomp::params::makeup) - recommendation->settings.makeupDb) < 0.02f,
               "the recommended makeup reaches the host parameter");
    }
    expect(assistant.canRevert(), "an applied recommendation can be reverted");

    assistant.setStyle(letscomp::assistant::Style::gentle);
    assistant.setAmount(letscomp::assistant::Amount::heavy);
    assistant.analyseAndApplyNow(syllableTake());
    assistant.revert();
    expect(std::abs(rawValue(processor, letscomp::params::threshold) - thresholdBefore) < 0.02f
               && std::abs(rawValue(processor, letscomp::params::mix) - mixBefore) < 0.02f,
           "revert restores the settings from before the assistant");
    expect(std::abs(rawValue(processor, letscomp::params::sidechainHighPass) - filterBefore) < 0.5f
               && rawValue(processor, letscomp::params::externalSidechain) > 0.5f,
           "revert restores the sidechain settings too");
    expect(!assistant.canRevert() && !assistant.recommendation().has_value(), "revert clears the result");

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    LetsCompAudioProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expect(restored.getAssistant().style() == letscomp::assistant::Style::gentle
               && restored.getAssistant().amount() == letscomp::assistant::Amount::heavy,
           "the chosen assistant style and amount are saved with the project");
}
}

int main()
{
    // The processor owns message-thread timers (latency sync, assistant).
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    LetsCompAudioProcessor processor;
    expect(processor.getBypassParameter()
               == processor.parameters.getParameter(letscomp::params::bypass),
           "the host bypass endpoint uses LetsComp's smoothed bypass parameter");

    processor.prepareToPlay(48000.0, 64);
    expect(processor.getLatencySamples() == 0,
           "processor reports no latency while lookahead is off");

    auto* lookahead = processor.parameters.getParameter(letscomp::params::lookahead);
    expect(lookahead != nullptr, "lookahead parameter exists");
    if (lookahead != nullptr)
    {
        lookahead->setValueNotifyingHost(lookahead->convertTo0to1(5.0f));
        processor.synchroniseLatency();
        expect(processor.getLatencySamples() == 240,
               "reported latency follows the lookahead parameter");
        lookahead->setValueNotifyingHost(lookahead->convertTo0to1(0.0f));
        processor.synchroniseLatency();
        expect(processor.getLatencySamples() == 0,
               "reported latency returns to zero with lookahead");
    }

    auto* threshold = processor.parameters.getParameter(letscomp::params::threshold);
    expect(threshold != nullptr
               && std::abs(threshold->convertFrom0to1(threshold->getDefaultValue()) + 18.0f) < 1.0e-3f,
           "threshold defaults to -18 dB so new instances compress typical material");

    auto* mix = processor.parameters.getParameter(letscomp::params::mix);
    expect(mix != nullptr, "mix parameter exists");
    if (mix != nullptr)
        mix->setValueNotifyingHost(mix->convertTo0to1(25.0f));

    const juce::Identifier idProperty{"id"};
    juce::MemoryBlock currentState;
    processor.getStateInformation(currentState);
    expect(!currentState.isEmpty(), "current processor state serializes");
    LetsCompAudioProcessor reopened;
    reopened.setStateInformation(currentState.getData(), static_cast<int>(currentState.getSize()));
    expect(std::abs(rawValue(reopened, letscomp::params::mix) - 25.0f) < 1.0e-4f,
           "saved settings come back when the project is opened again");

    const std::array<std::byte, 8> corruptState{};
    processor.setStateInformation(corruptState.data(), static_cast<int>(corruptState.size()));
    expect(std::abs(rawValue(processor, letscomp::params::mix) - 25.0f) < 1.0e-4f,
           "malformed state is rejected without altering live parameters");

    auto damagedState = processor.parameters.copyState();
    damagedState.getChildWithProperty(idProperty, letscomp::params::threshold).setProperty("value", "nan", nullptr);
    damagedState.getChildWithProperty(idProperty, letscomp::params::makeup).setProperty("value", "-inf", nullptr);
    juce::MemoryBlock damagedBinary;
    processor.copyXmlToBinary(*damagedState.createXml(), damagedBinary);
    processor.setStateInformation(damagedBinary.getData(), static_cast<int>(damagedBinary.getSize()));
    for (auto* parameter : processor.getParameters())
        expect(std::isfinite(parameter->getValue()),
               "non-finite saved value for " + parameter->getName(64).toStdString() + " is replaced");
    expect(std::abs(rawValue(processor, letscomp::params::threshold) - (-18.0f)) < 1.0e-4f,
           "a non-finite threshold falls back to its default");

    testAssistant();
    testHostResetClearsLookahead();
    testSidechainBus();
    testLatencyRestartKeepsPlaying();

    if (failures == 0)
        std::cout << "All LetsComp processor integration tests passed\n";
    return failures == 0 ? 0 : 1;
}
