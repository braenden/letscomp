#include "Header.h"
#include "../parameters/Parameters.h"

namespace letscomp::ui
{
using assistant::AssistantController;

juce::String listenButtonText(const AssistantController& controller)
{
    switch (controller.phase())
    {
        case AssistantController::Phase::waiting:   return "Cancel";
        case AssistantController::Phase::listening: return "Stop";
        case AssistantController::Phase::analysing:
        case AssistantController::Phase::ready:     return controller.recommendation().has_value() ? "Listen again" : "Listen";
        case AssistantController::Phase::idle:
        case AssistantController::Phase::failed:    return "Listen";
    }
    return "Listen";
}

Header::Header(AssistantController& controllerToUse, juce::AudioProcessorValueTreeState& state)
    : controller(controllerToUse), bypassAttachment(state, params::bypass, bypassButton)
{
    undoButton.setTooltip("Restore your settings from before the assistant");
    undoButton.onClick = [this] { controller.revert(); };
    bypassButton.setClickingTogglesState(true);
    bypassButton.setTooltip("Hear the track without compression, to compare.");
    tipsButton.onClick = [this]
    {
        setTipsShown(!tipsShown);
        if (onTipsChange) onTipsChange(tipsShown);
    };
    addAndMakeVisible(undoButton);
    addAndMakeVisible(tipsButton);
    addAndMakeVisible(bypassButton);
    refresh();
}

void Header::refresh()
{
    undoButton.setEnabled(controller.canRevert());
}

void Header::setTipsShown(bool shown)
{
    tipsShown = shown;
    tipsButton.setButtonText(shown ? "Tips on" : "Tips off");
    tipsButton.setTooltip("Turn the help that appears when you hover a control on or off");
}

void Header::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(logoArea.contains(event.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                          : juce::MouseCursor::NormalCursor);
}

void Header::mouseUp(const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && logoArea.contains(event.getPosition()) && onAbout)
        onAbout();
}

void Header::paint(juce::Graphics& g)
{
    auto logo = logoArea.toFloat();
    const auto mark = logo.removeFromLeft(26.0f).withSizeKeepingCentre(26.0f, 18.0f);
    g.setColour(colours::output);
    g.drawEllipse(mark.getX() + 1.0f, mark.getY() + 1.5f, 15.0f, 15.0f, 2.0f);
    g.setColour(colours::accent);
    g.drawEllipse(mark.getX() + 9.0f, mark.getY() + 1.5f, 15.0f, 15.0f, 2.0f);
    logo.removeFromLeft(10.0f);
    const auto nameFont = font(18.0f, Weight::heavy);
    g.setColour(colours::text);
    g.setFont(nameFont);
    g.drawText("LetsComp", logo, juce::Justification::centredLeft);
    logo.removeFromLeft(juce::GlyphArrangement::getStringWidth(nameFont, "LetsComp") + 8.0f);
    g.setColour(colours::faint);
    g.setFont(font(12.5f, Weight::demiBold));
    g.drawText("v" JucePlugin_VersionString, logo.translated(0.0f, 1.5f), juce::Justification::centredLeft);

    g.setColour(colours::line);
    g.fillRect(getLocalBounds().removeFromBottom(1));
}

void Header::resized()
{
    auto area = getLocalBounds().reduced(24, 0).withTrimmedBottom(1);
    logoArea = area.removeFromLeft(210).withSizeKeepingCentre(210, 32);
    bypassButton.setBounds(area.removeFromRight(108).withSizeKeepingCentre(108, 36));
    area.removeFromRight(8);
    tipsButton.setBounds(area.removeFromRight(104).withSizeKeepingCentre(104, 36));
    area.removeFromRight(4);
    undoButton.setBounds(area.removeFromRight(92).withSizeKeepingCentre(92, 36));
}
}
