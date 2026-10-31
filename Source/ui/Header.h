#pragma once

#include "Widgets.h"
#include "../assistant/AssistantController.h"

namespace letscomp::ui
{
// LISTEN's caption for the assistant's current phase.
juce::String listenButtonText(const assistant::AssistantController&);

// Top row: the name and version (click for About), Undo, Tips and Bypass.
class Header final : public juce::Component
{
public:
    Header(assistant::AssistantController&, juce::AudioProcessorValueTreeState&);

    // Message thread: re-reads the assistant.
    void refresh();
    void setTipsShown(bool);
    std::function<void(bool)> onTipsChange;
    std::function<void()> onAbout;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    assistant::AssistantController& controller;
    IconTextButton undoButton{"Undo", icons::undo(), false};
    IconTextButton tipsButton{"Tips on", icons::help(), false};
    IconTextButton bypassButton{"Bypass", icons::power(), true};
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;
    juce::Rectangle<int> logoArea;
    bool tipsShown = true;
};
}
