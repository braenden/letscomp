#include "RotaryControl.h"
#include "Theme.h"

namespace letscomp::ui
{
RotaryControl::RotaryControl(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                             const juce::String& displayName, bool isLarge)
    : large(isLarge), attachment(state, parameterId, slider)
{
    label.setText(displayName, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, colours::muted);
    label.setFont(font(large ? 14.0f : 13.0f, Weight::demiBold));
    label.setInterceptsMouseClicks(false, false);
    slider.getProperties().set("large", large);
    slider.getProperties().set("bipolar", parameterId == "makeup");
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 110, large ? 28 : 24);
    slider.setName(displayName);
    slider.setTitle(displayName);
    const auto tooltip
        = parameterId == "threshold" ? "Level where compression starts. Lower it to compress more of the part."
        : parameterId == "ratio"     ? "How firmly levels above the threshold are turned down. "
                                       "4:1 is firm; 10:1 and up is close to limiting."
        : parameterId == "attack"    ? "How fast compression grabs. Short tames peaks and consonants; "
                                       "longer lets the punch through."
        : parameterId == "release"   ? "How fast compression lets go. Very short can distort; long can sound squashed."
        : parameterId == "knee"      ? "How gradually compression starts around the threshold. Higher is smoother."
        : parameterId == "hold"      ? "Keeps compression in place this long before it starts to let go."
        : parameterId == "lookahead" ? "Lets the compressor react just before peaks. Adds the same amount of latency."
        : parameterId == "range"     ? "The most compression that can ever be applied. Off means no limit."
        : parameterId == "makeup"    ? "Turns the compressed signal back up. Match the bypass loudness to compare fairly."
        : parameterId == "mix"       ? "Blends in the uncompressed signal for parallel compression."
        : parameterId == "sidechainHighPass"
            ? "Keeps low end out of what the compressor listens to, so a kick or bass does not pump "
              "the whole part. The sound itself is not filtered."
                                     : "";
    baseTooltip = tooltip;
    slider.setTooltip(tooltip);
    slider.setDescription(tooltip);
    if (parameterId == "attack" || parameterId == "release")
        slider.setMouseDragSensitivity(180);
    if (auto* parameter = state.getParameter(parameterId))
        slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    addAndMakeVisible(label);
    addAndMakeVisible(slider);
}

void RotaryControl::setAssistantNote(const juce::String& note, bool changedByUser)
{
    slider.setTooltip(note.isEmpty() || changedByUser ? baseTooltip : baseTooltip + "\n\nAssistant: " + note);
}

void RotaryControl::resized()
{
    // Knob and value sit on a shared baseline so a row of mixed
    // sizes lines up; the label rides just above its own knob.
    auto area = getLocalBounds();
    const auto sliderHeight = std::min(area.getHeight() - 24, large ? 136 : 108);
    slider.setBounds(area.removeFromBottom(sliderHeight));
    area.removeFromBottom(4);
    label.setBounds(area.removeFromBottom(20));
}
}
