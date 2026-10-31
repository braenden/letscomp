#include "PluginEditor.h"
#include "parameters/Parameters.h"
#include "ui/Preferences.h"

namespace
{
constexpr int margin = 24;
constexpr int headerHeight = 56;
constexpr int topGap = 20;
constexpr int rowGap = 16;
constexpr int controlsHeight = 172;
constexpr int bottomMargin = 16;
constexpr int assistWidth = 300;
}

LetsCompAudioProcessorEditor::LetsCompAudioProcessorEditor(LetsCompAudioProcessor& processorToUse)
    : AudioProcessorEditor(&processorToUse),
      audioProcessor(processorToUse),
      header(processorToUse.getAssistant(), processorToUse.parameters),
      assistPanel(processorToUse.getAssistant()),
      scope(processorToUse.getScopeFifo(),
            processorToUse.parameters.getRawParameterValue(letscomp::params::threshold),
            processorToUse.parameters.getParameter(letscomp::params::threshold),
            processorToUse.getMeterValues()),
      controls(processorToUse.parameters)
{
    setLookAndFeel(&theme);
    for (auto* child : std::initializer_list<juce::Component*>{&header, &assistPanel, &scope, &controls})
        addAndMakeVisible(child);

    header.onTipsChange = [this](bool shown)
    {
        letscomp::ui::setTipsEnabled(shown);
        showTips(shown);
    };
    header.onAbout = [this] { showAbout(); };
    showTips(letscomp::ui::tipsEnabled());

    audioProcessor.getAssistant().onChange = [this] { assistantChanged(); };
    assistantChanged();

    setResizable(true, true);
    setResizeLimits(900, 600, 1600, 1040);
    setSize(1200, 740);
}

LetsCompAudioProcessorEditor::~LetsCompAudioProcessorEditor()
{
    audioProcessor.getAssistant().onChange = nullptr;
    setLookAndFeel(nullptr);
}

void LetsCompAudioProcessorEditor::showTips(bool shown)
{
    header.setTipsShown(shown);
    if (!shown)
        tooltipWindow.reset();
    else if (tooltipWindow == nullptr)
        tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 600);
}

void LetsCompAudioProcessorEditor::showAbout()
{
    about = std::make_unique<letscomp::ui::AboutOverlay>();
    about->onClose = [this]
    {
        // Deleting the overlay from inside its own callback is unsafe, so wait a moment.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer(this)]
        {
            if (safe != nullptr) safe->about.reset();
        });
    };
    addAndMakeVisible(*about);
    about->setBounds(getLocalBounds());
    about->grabKeyboardFocus();
}

void LetsCompAudioProcessorEditor::assistantChanged()
{
    using Phase = letscomp::assistant::AssistantController::Phase;
    auto& assistant = audioProcessor.getAssistant();
    header.refresh();
    assistPanel.refresh();

    const auto& recommendation = assistant.recommendation();
    const auto showResult = recommendation.has_value() && assistant.phase() == Phase::ready;
    scope.setGainReductionTarget(showResult ? std::optional<float>(recommendation->achievedGainReductionDb)
                                            : std::nullopt);
    controls.setRecommendation(showResult ? &*recommendation : nullptr);
}

void LetsCompAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(letscomp::ui::colours::background);
}

void LetsCompAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds(area.removeFromTop(headerHeight));
    area = area.withTrimmedTop(topGap).withTrimmedBottom(bottomMargin).reduced(margin, 0);
    controls.setBounds(area.removeFromBottom(controlsHeight));
    area.removeFromBottom(rowGap);
    assistPanel.setBounds(area.removeFromLeft(assistWidth));
    area.removeFromLeft(20);
    scope.setBounds(area);
    if (about != nullptr)
        about->setBounds(getLocalBounds());
}
