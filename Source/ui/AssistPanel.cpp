#include "AssistPanel.h"
#include "Header.h"

namespace letscomp::ui
{
namespace
{
using assistant::AssistantController;
}

AssistPanel::AssistPanel(AssistantController& controllerToUse) : controller(controllerToUse)
{
    sourceControl.onChange = [this](int index) { controller.setSource(assistant::allSources[static_cast<std::size_t>(index)]); };
    styleControl.onChange = [this](int index) { controller.setStyle(assistant::allStyles[static_cast<std::size_t>(index)]); };
    amountControl.onChange = [this](int index) { controller.setAmount(assistant::allAmounts[static_cast<std::size_t>(index)]); };
    sourceControl.setTooltip("What you are compressing. Bus is a group or the whole mix.");
    amountControl.setTooltip("How much to compress. Medium is the style's own amount; "
                             "Light is about half as much, Heavy half as much again.");
    listenButton.setTooltip("Listens to up to 10 seconds of the uncompressed input, then sets a starting point");
    listenButton.onClick = [this] { controller.toggleListening(); };
    for (auto* child : std::initializer_list<juce::Component*>{&sourceControl, &styleControl, &amountControl, &listenButton})
        addAndMakeVisible(child);
    refresh();
}

void AssistPanel::refresh()
{
    const auto source = controller.source();
    const auto style = controller.style();
    sourceControl.setSelected(static_cast<int>(source));
    for (std::size_t index = 0; index < assistant::allStyles.size(); ++index)
        styleControl.setLabel(static_cast<int>(index),
                              juce::String(std::string(assistant::displayName(source, assistant::allStyles[index]))));
    styleControl.setSelected(static_cast<int>(style));
    styleControl.setTooltip(source == assistant::SourceType::vocal
        ? "Gentle: light levelling. Control: steady level. Upfront: tight and in front, "
          "as in modern rap and pop. Glue: smooth and even."
        : "Gentle: light levelling. Control: steady level. Punch: lets each hit through for snap. "
          "Glue: holds the parts together.");
    amountControl.setSelected(static_cast<int>(controller.amount()));

    const auto phase = controller.phase();
    listenButton.setButtonText(listenButtonText(controller));
    const auto listening = phase == AssistantController::Phase::waiting || phase == AssistantController::Phase::listening;
    listenButton.setProgress(listening ? controller.listenedSeconds() / assistant::CaptureBuffer::captureSeconds : -1.0);

    // Only say something when the user has to act or something went wrong.
    captionIsError = phase == AssistantController::Phase::failed;
    caption = phase == AssistantController::Phase::waiting ? juce::String("Play the part")
            : captionIsError ? controller.failureMessage()
                             : juce::String();
    repaint();
}

void AssistPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(bounds, 16.0f);
    g.setColour(colours::line);
    g.drawRoundedRectangle(bounds, 16.0f, 1.0f);

    g.setColour(colours::muted);
    g.setFont(font(13.0f, Weight::demiBold));
    g.drawText("What are you compressing?", sourceLabel, juce::Justification::centredLeft);
    g.drawText("Style", styleLabel, juce::Justification::centredLeft);
    g.drawText("How much", amountLabel, juce::Justification::centredLeft);

    g.setColour(captionIsError ? juce::Colour(0xffe08a6c) : colours::muted);
    g.setFont(font(12.5f, Weight::medium));
    g.drawFittedText(caption, captionArea, juce::Justification::centred, 2, 1.0f);
}

void AssistPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    const auto row = [&area](int height) { return area.removeFromTop(height); };
    sourceLabel = row(16);
    row(6);
    sourceControl.setBounds(row(38));
    row(12);
    styleLabel = row(16);
    row(6);
    styleControl.setBounds(row(38));
    row(12);
    amountLabel = row(16);
    row(6);
    amountControl.setBounds(row(38));

    captionArea = area.removeFromBottom(32);
    area.removeFromBottom(4);
    listenButton.setBounds(area.removeFromBottom(46));
}
}
