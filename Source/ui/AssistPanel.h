#pragma once

#include "Widgets.h"
#include "../assistant/AssistantController.h"

namespace letscomp::ui
{
// The assistant in one compact panel: what is being compressed, the style,
// how much, and LISTEN, which shows its own progress. It only puts text on
// screen when the user has to act or something went wrong; the rest is in
// the tooltips.
class AssistPanel final : public juce::Component
{
public:
    explicit AssistPanel(assistant::AssistantController&);

    // Message thread: re-reads the assistant.
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    assistant::AssistantController& controller;
    SegmentedControl sourceControl{{"Vocal", "Drums", "Bass", "Bus"}, false};
    SegmentedControl styleControl{{"Gentle", "Control", "Punch", "Glue"}, false};
    SegmentedControl amountControl{{"Light", "Medium", "Heavy"}, false};
    PillButton listenButton{icons::microphone()};
    juce::Rectangle<int> sourceLabel, styleLabel, amountLabel, captionArea;
    juce::String caption;
    bool captionIsError = false;
};
}
