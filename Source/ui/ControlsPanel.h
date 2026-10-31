#pragma once

#include "RotaryControl.h"
#include "Widgets.h"
#include "../assistant/AssistantTypes.h"

#include <optional>

namespace letscomp::ui
{
// One row of controls: threshold and ratio large, attack, release, makeup
// and mix smaller. Each knob's tooltip carries the assistant's reason for
// its value until the user moves it. MORE opens knee, hold, lookahead,
// range and the sidechain in a small window and, while closed, names the
// ones that are on.
class ControlsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ControlsPanel(juce::AudioProcessorValueTreeState&);
    ~ControlsPanel() override;

    // The assistant's current result; nullptr clears the reasons.
    void setRecommendation(const assistant::Recommendation*);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class MoreButton;
    class MoreWindow;

    void timerCallback() override;
    void updateReasons();
    void showMore();

    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<RotaryControl>> controls;
    std::vector<juce::String> ids;
    std::unique_ptr<MoreButton> moreButton;
    juce::Component::SafePointer<juce::CallOutBox> moreWindow;
    std::optional<assistant::Recommendation> recommendation;
    std::vector<int> dividers;
};
}
