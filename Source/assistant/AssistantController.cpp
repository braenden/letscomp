#include "AssistantController.h"
#include "Recommender.h"
#include "SignalAnalyzer.h"
#include "../parameters/Parameters.h"

namespace letscomp::assistant
{
namespace
{
const juce::Identifier sourceProperty{"assistSource"};
const juce::Identifier styleProperty{"assistStyle"};
const juce::Identifier amountProperty{"assistAmount"};

// Parameters the assistant writes. Bypass is left to the user.
constexpr const char* managedParameters[]{
    params::threshold, params::ratio, params::knee, params::attack, params::hold, params::release,
    params::lookahead, params::range, params::makeup, params::mix, params::sidechainHighPass,
    params::externalSidechain};
}

AssistantController::AssistantController(juce::AudioProcessorValueTreeState& state)
    : parameters(state)
{
    startTimerHz(20);
}

AssistantController::~AssistantController()
{
    stopTimer();
    cancelJob();
    pool.removeAllJobs(true, 5000);
}

void AssistantController::prepare(double sampleRate, int channels)
{
    capture.prepare(sampleRate, channels);
    if (currentPhase == Phase::waiting || currentPhase == Phase::listening)
        setPhase(Phase::idle);
}

SourceType AssistantController::source() const
{
    return sourceFromIdentifier(parameters.state.getProperty(sourceProperty).toString().toStdString())
        .value_or(SourceType::vocal);
}

Style AssistantController::style() const
{
    return styleFromIdentifier(parameters.state.getProperty(styleProperty).toString().toStdString())
        .value_or(Style::control);
}

void AssistantController::setSource(SourceType newSource)
{
    if (newSource == source()) return;
    parameters.state.setProperty(sourceProperty, juce::String(std::string(identifier(newSource))), nullptr);
    // A new choice re-runs the recommendation on what was already heard, so
    // the user can compare starting points without playing the part again.
    if (heard != nullptr) launchJob();
    notify();
}

void AssistantController::setStyle(Style newStyle)
{
    if (newStyle == style()) return;
    parameters.state.setProperty(styleProperty, juce::String(std::string(identifier(newStyle))), nullptr);
    if (heard != nullptr) launchJob();
    notify();
}

Amount AssistantController::amount() const
{
    return amountFromIdentifier(parameters.state.getProperty(amountProperty).toString().toStdString())
        .value_or(Amount::medium);
}

void AssistantController::setAmount(Amount newAmount)
{
    if (newAmount == amount()) return;
    parameters.state.setProperty(amountProperty, juce::String(std::string(identifier(newAmount))), nullptr);
    if (heard != nullptr) launchJob();
    notify();
}

void AssistantController::toggleListening()
{
    switch (currentPhase)
    {
        case Phase::waiting:
            capture.stop();
            setPhase(heard != nullptr && currentRecommendation.has_value() ? Phase::ready : Phase::idle);
            return;
        case Phase::listening:
            finishListening();
            return;
        case Phase::idle:
        case Phase::analysing:
        case Phase::ready:
        case Phase::failed:
            cancelJob();
            failure.clear();
            capture.arm();
            setPhase(Phase::waiting);
            return;
    }
}

void AssistantController::revert()
{
    if (!revertValues.has_value()) return;
    cancelJob();
    for (const auto& [id, value] : *revertValues)
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    revertValues.reset();
    currentRecommendation.reset();
    heard.reset();
    setPhase(Phase::idle);
}

void AssistantController::timerCallback()
{
    const auto captureState = capture.currentState();
    if (currentPhase == Phase::waiting && captureState == CaptureBuffer::State::capturing)
        setPhase(Phase::listening);
    if ((currentPhase == Phase::waiting || currentPhase == Phase::listening)
        && captureState == CaptureBuffer::State::full)
        finishListening();

    if (currentPhase == Phase::listening && std::abs(listenedSeconds() - lastNotifiedSeconds) >= 0.1)
    {
        lastNotifiedSeconds = listenedSeconds();
        notify();
    }

    if (job != nullptr && job->finished.load(std::memory_order_acquire))
    {
        auto finished = std::move(job);
        if (!finished->cancel.load(std::memory_order_relaxed))
            handleResult(std::move(finished->result));
    }
}

void AssistantController::finishListening()
{
    capture.stop();
    auto data = std::make_shared<CaptureData>(capture.copy());
    if (data->seconds() < minimumCaptureSeconds)
    {
        failure = "Too short to judge. Play at least " + juce::String(minimumCaptureSeconds, 0) + " seconds.";
        setPhase(Phase::failed);
        return;
    }
    heard = std::move(data);
    launchJob();
}

void AssistantController::launchJob()
{
    cancelJob();
    auto next = std::make_shared<Job>();
    job = next;
    pool.addJob([next, data = heard, chosenSource = source(), chosenStyle = style(), chosenAmount = amount()]
    {
        const auto analysis = analyseSignal(*data);
        next->result = recommend(*data, analysis, chosenSource, chosenStyle, chosenAmount, &next->cancel);
        next->finished.store(true, std::memory_order_release);
    });
    setPhase(Phase::analysing);
}

void AssistantController::cancelJob()
{
    if (job != nullptr)
        job->cancel.store(true, std::memory_order_relaxed);
    job.reset();
}

void AssistantController::analyseAndApplyNow(const CaptureData& data)
{
    cancelJob();
    heard = std::make_shared<CaptureData>(data);
    handleResult(recommend(*heard, analyseSignal(*heard), source(), style(), amount()));
}

void AssistantController::handleResult(AssistantResult result)
{
    if (!result.succeeded || !result.recommendation.has_value())
    {
        failure = juce::String(result.message);
        heard.reset();
        setPhase(Phase::failed);
        return;
    }
    apply(*result.recommendation);
    currentRecommendation = std::move(result.recommendation);
    setPhase(Phase::ready);
}

void AssistantController::apply(const Recommendation& recommendation)
{
    // The revert point is the user's own settings from before the first
    // result. Other styles and new takes keep it until the user reverts.
    if (!revertValues.has_value())
    {
        std::map<juce::String, float> values;
        for (const auto* id : managedParameters)
            if (auto* parameter = parameters.getParameter(id))
                values[id] = parameter->getValue();
        revertValues = std::move(values);
    }

    const auto& settings = recommendation.settings;
    const auto set = [this](const char* id, float value)
    {
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    set(params::threshold, settings.thresholdDb);
    set(params::ratio, settings.ratio);
    set(params::knee, settings.kneeDb);
    set(params::attack, settings.attackMs);
    set(params::hold, settings.holdMs);
    set(params::release, settings.releaseMs);
    set(params::lookahead, 0.0f);
    set(params::range, settings.rangeDb);
    set(params::makeup, settings.makeupDb);
    set(params::mix, settings.mix * 100.0f);
    // The recommendation is worked out on the part's own, unfiltered level.
    set(params::sidechainHighPass, params::domain::sidechainHighPassOffHz);
    set(params::externalSidechain, 0.0f);
}

void AssistantController::setPhase(Phase next)
{
    currentPhase = next;
    lastNotifiedSeconds = -1.0;
    notify();
}

void AssistantController::notify()
{
    if (onChange) onChange();
}
}
