#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "parameters/Parameters.h"
#include <cmath>

LetsCompAudioProcessor::LetsCompAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters(*this, nullptr, "LetsCompState", letscomp::params::createLayout())
{
    startTimerHz(10);
}

LetsCompAudioProcessor::~LetsCompAudioProcessor()
{
    stopTimer();
}

void LetsCompAudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto channels = getMainBusNumOutputChannels();
    // Hosts restart the plug-in to pick up a new latency, often while playing.
    // With an unchanged format the compressor keeps its state, so the new
    // lookahead crossfades in instead of the gain reduction dropping mid-note.
    const auto keepState = latencyRestartPending.exchange(false, std::memory_order_relaxed)
                        && std::abs(sampleRate - preparedSampleRate.load(std::memory_order_relaxed)) < 1.0e-6
                        && maximumExpectedSamplesPerBlock <= preparedBlockSize
                        && channels == preparedChannels;
    preparedSampleRate.store(sampleRate, std::memory_order_relaxed);
    activeLookaheadSamples.store(lookaheadSamplesFromParameter(), std::memory_order_relaxed);
    if (keepState)
    {
        compressor.setSettings(readSettings());
    }
    else
    {
        compressor.prepare(sampleRate, maximumExpectedSamplesPerBlock, channels);
        assistant.prepare(sampleRate, getMainBusNumInputChannels());
        compressor.setSettings(readSettings());
        compressor.reset();
        resetRequested.store(false, std::memory_order_relaxed);
        preparedBlockSize = maximumExpectedSamplesPerBlock;
        preparedChannels = channels;
    }
    // Latency is the lookahead only. With Lookahead at 0 ms the plug-in adds no delay.
    setLatencySamples(activeLookaheadSamples.load(std::memory_order_relaxed));
}

void LetsCompAudioProcessor::reset()
{
    // A latency restart is not a jump in the music, so it keeps the state.
    if (!latencyRestartPending.load(std::memory_order_relaxed))
        resetRequested.store(true, std::memory_order_relaxed);
}

int LetsCompAudioProcessor::lookaheadSamplesFromParameter() const noexcept
{
    const auto milliseconds = parameters.getRawParameterValue(letscomp::params::lookahead)
                                  ->load(std::memory_order_relaxed);
    return static_cast<int>(std::round(milliseconds * 0.001
                                       * preparedSampleRate.load(std::memory_order_relaxed)));
}

void LetsCompAudioProcessor::synchroniseLatency()
{
    const auto samples = lookaheadSamplesFromParameter();
    if (samples == activeLookaheadSamples.load(std::memory_order_relaxed))
        return;
    // The audio thread switches its program delay on the next block, and the
    // host is told about the new latency from this (message) thread. The host
    // may restart the plug-in from inside setLatencySamples, so mark it first.
    activeLookaheadSamples.store(samples, std::memory_order_relaxed);
    latencyRestartPending.store(true, std::memory_order_relaxed);
    restartPendingTicks = 0;
    setLatencySamples(samples);
}

void LetsCompAudioProcessor::timerCallback()
{
    const auto samples = lookaheadSamplesFromParameter();
    if (samples == activeLookaheadSamples.load(std::memory_order_relaxed))
        lookaheadStableTicks = 0;
    else if (samples != lastSeenLookaheadSamples)
        lookaheadStableTicks = 0;
    else if (++lookaheadStableTicks >= 3)
        synchroniseLatency();
    lastSeenLookaheadSamples = samples;

    // Hosts that take the new latency without a restart never call prepareToPlay,
    // so stop waiting for one after two seconds.
    if (latencyRestartPending.load(std::memory_order_relaxed) && ++restartPendingTicks >= 20)
        latencyRestartPending.store(false, std::memory_order_relaxed);
}

bool LetsCompAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    const auto mainSupported = (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
                            && output == layouts.getMainInputChannelSet();
    // The sidechain may be off, mono or stereo.
    const auto sidechain = layouts.inputBuses.size() > 1 ? layouts.getChannelSet(true, 1) : juce::AudioChannelSet{};
    return mainSupported
        && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono()
            || sidechain == juce::AudioChannelSet::stereo());
}

void LetsCompAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (resetRequested.exchange(false, std::memory_order_relaxed))
        compressor.reset();
    auto main = getBusBuffer(buffer, true, 0);
    for (int channel = getMainBusNumInputChannels(); channel < getMainBusNumOutputChannels(); ++channel)
        main.clear(channel, 0, main.getNumSamples());
    const auto hasSidechain = getBusCount(true) > 1 && getChannelCountOfBus(true, 1) > 0;
    const auto sidechain = hasSidechain ? getBusBuffer(buffer, true, 1) : juce::AudioBuffer<float>{};
    // The assistant listens to the main input before compression.
    assistant.pushInput(main);
    compressor.setSettings(readSettings());
    compressor.process(main, scopeFifo, meters, {}, hasSidechain ? &sidechain : nullptr);
}

letscomp::dsp::CompressorSettings LetsCompAudioProcessor::readSettings() const noexcept
{
    const auto value = [this](const char* parameterId)
    {
        return parameters.getRawParameterValue(parameterId)->load(std::memory_order_relaxed);
    };
    letscomp::dsp::CompressorSettings result;
    result.thresholdDb = value(letscomp::params::threshold);
    result.ratio = value(letscomp::params::ratio);
    result.kneeDb = value(letscomp::params::knee);
    result.attackMs = value(letscomp::params::attack);
    result.holdMs = value(letscomp::params::hold);
    result.releaseMs = value(letscomp::params::release);
    result.rangeDb = value(letscomp::params::range);
    result.makeupDb = value(letscomp::params::makeup);
    result.mix = value(letscomp::params::mix) * 0.01f;
    result.sidechainHighPassHz = value(letscomp::params::sidechainHighPass);
    result.externalSidechain = value(letscomp::params::externalSidechain) >= 0.5f;
    result.lookaheadSamples = activeLookaheadSamples.load(std::memory_order_relaxed);
    result.bypass = value(letscomp::params::bypass) >= 0.5f;
    return result;
}

juce::AudioProcessorEditor* LetsCompAudioProcessor::createEditor()
{
    return new LetsCompAudioProcessorEditor(*this);
}

juce::AudioProcessorParameter* LetsCompAudioProcessor::getBypassParameter() const
{
    return parameters.getParameter(letscomp::params::bypass);
}

void LetsCompAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void LetsCompAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml != nullptr)
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto restored = juce::ValueTree::fromXml(*xml);
            const juce::Identifier idProperty{"id"};
            // A damaged project can hold nan or inf, which the parameter ranges
            // pass through; those parameters start from their defaults instead.
            for (auto child : restored)
                if (auto* parameter = parameters.getParameter(child.getProperty(idProperty).toString());
                    parameter != nullptr && !std::isfinite(static_cast<double>(child.getProperty("value"))))
                    child.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
            parameters.replaceState(restored);
            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged(true));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LetsCompAudioProcessor();
}
