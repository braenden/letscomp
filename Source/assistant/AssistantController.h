#pragma once

#include "CaptureBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace letscomp::assistant
{
// Owns the assistant workflow: listen to the uncompressed input, analyse it on
// a background thread and apply the result to the host parameters. Lives in
// the processor so a result still lands while the editor is closed.
//
// All public members except pushInput() are message-thread only.
class AssistantController final : private juce::Timer
{
public:
    enum class Phase { idle, waiting, listening, analysing, ready, failed };

    explicit AssistantController(juce::AudioProcessorValueTreeState&);
    ~AssistantController() override;

    void prepare(double sampleRate, int channels);
    // Audio thread.
    void pushInput(const juce::AudioBuffer<float>& buffer) noexcept { capture.push(buffer); }

    // Starts listening, or, while listening, analyses what has been heard so far.
    void toggleListening();
    void setSource(SourceType);
    void setStyle(Style);
    void setAmount(Amount);
    SourceType source() const;
    Style style() const;
    Amount amount() const;
    // Restores the settings from before the assistant applied its result.
    void revert();
    bool canRevert() const noexcept { return revertValues.has_value(); }

    Phase phase() const noexcept { return currentPhase; }
    double listenedSeconds() const noexcept { return capture.capturedSeconds(); }
    const std::optional<Recommendation>& recommendation() const noexcept { return currentRecommendation; }
    const juce::String& failureMessage() const noexcept { return failure; }

    // Called on the message thread whenever anything above changes.
    std::function<void()> onChange;

    // Synchronous variant for tests: analyses a capture and applies the result.
    void analyseAndApplyNow(const CaptureData&);

private:
    struct Job
    {
        std::atomic<bool> cancel{false}, finished{false};
        AssistantResult result;
    };

    void timerCallback() override;
    void finishListening();
    void launchJob();
    void cancelJob();
    void handleResult(AssistantResult);
    void apply(const Recommendation&);
    void setPhase(Phase);
    void notify();

    juce::AudioProcessorValueTreeState& parameters;
    CaptureBuffer capture;
    juce::ThreadPool pool{juce::ThreadPoolOptions{}.withThreadName("LetsComp Assistant").withNumberOfThreads(1)};
    std::shared_ptr<Job> job;
    std::shared_ptr<const CaptureData> heard;
    std::optional<std::map<juce::String, float>> revertValues;
    std::optional<Recommendation> currentRecommendation;
    juce::String failure;
    Phase currentPhase = Phase::idle;
    double lastNotifiedSeconds = -1.0;
};
}
