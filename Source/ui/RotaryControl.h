#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace letscomp::ui
{
// A labelled knob with its value underneath. Large knobs are for the
// controls that matter most (threshold, ratio).
class RotaryControl final : public juce::Component
{
public:
    RotaryControl(juce::AudioProcessorValueTreeState&, const juce::String& parameterId,
                  const juce::String& displayName, bool large = false);
    void resized() override;
    // Adds the assistant's reason for this value to the tooltip. Once the
    // user has moved the knob away from the assistant's value the reason no
    // longer applies and is left out. An empty note clears it.
    void setAssistantNote(const juce::String& note, bool changedByUser);

private:
    juce::String baseTooltip;
    bool large;
    juce::Label label;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};
}

