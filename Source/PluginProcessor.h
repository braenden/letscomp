#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "assistant/AssistantController.h"
#include "dsp/CompressorEngine.h"
#include "visualization/ScopeData.h"

class LetsCompAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    LetsCompAudioProcessor();
    ~LetsCompAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    // Any thread. The host asks for a clean start (transport jump, stop). The
    // audio thread clears the compressor at the start of its next block.
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override;
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    letscomp::visualization::ScopeFifo<8192>& getScopeFifo() noexcept { return scopeFifo; }
    letscomp::visualization::MeterValues& getMeterValues() noexcept { return meters; }
    letscomp::assistant::AssistantController& getAssistant() noexcept { return assistant; }

    // Message thread: brings the reported latency and the program delay in line
    // with the Lookahead parameter. A timer calls it once the parameter has held
    // still for a moment, so host automation is picked up even while the editor
    // is closed, and dragging the knob makes one latency change, not many.
    void synchroniseLatency();

private:
    void timerCallback() override;
    letscomp::dsp::CompressorSettings readSettings() const noexcept;
    int lookaheadSamplesFromParameter() const noexcept;

    letscomp::dsp::CompressorEngine compressor;
    letscomp::visualization::ScopeFifo<8192> scopeFifo;
    letscomp::visualization::MeterValues meters;
    std::atomic<double> preparedSampleRate{44100.0};
    std::atomic<int> activeLookaheadSamples{0};
    std::atomic<bool> resetRequested{false};
    // Set while the host may restart the plug-in to pick up a new latency.
    std::atomic<bool> latencyRestartPending{false};
    int preparedBlockSize = 0, preparedChannels = 0;
    int lastSeenLookaheadSamples = -1, lookaheadStableTicks = 0, restartPendingTicks = 0;
    letscomp::assistant::AssistantController assistant{parameters};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LetsCompAudioProcessor)
};
