#include "About.h"

namespace letscomp::ui
{
namespace
{
constexpr int cardWidth = 520;
constexpr int cardHeight = 400;
constexpr int padding = 28;
}

AboutOverlay::AboutOverlay()
{
    setWantsKeyboardFocus(true);
    closeButton.setTooltip("Close");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* link : {&licenceLink, &sourceLink})
    {
        link->setFont(font(13.0f, Weight::demiBold), false, juce::Justification::centredLeft);
        link->setColour(juce::HyperlinkButton::textColourId, colours::output);
        addAndMakeVisible(*link);
    }
    addAndMakeVisible(closeButton);
}

juce::Rectangle<int> AboutOverlay::card() const
{
    return getLocalBounds().withSizeKeepingCentre(cardWidth, cardHeight);
}

void AboutOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    const auto bounds = card().toFloat();
    g.setColour(colours::panel);
    g.fillRoundedRectangle(bounds, 16.0f);
    g.setColour(colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 16.0f, 1.0f);

    auto area = card().reduced(padding);
    const auto line = [&](const juce::String& text, float height, Weight weight, juce::Colour colour, int rows = 1)
    {
        g.setColour(colour);
        g.setFont(font(height, weight));
        g.drawFittedText(text, area.removeFromTop(static_cast<int>(std::ceil(height * 1.45f)) * rows),
                         juce::Justification::topLeft, rows, 1.0f);
    };

    line("LetsComp", 24.0f, Weight::heavy, colours::text);
    line("Version " JucePlugin_VersionString, 13.0f, Weight::medium, colours::muted);
    area.removeFromTop(12);
    line("Copyright (C) 2026 Kevin Brenden", 14.5f, Weight::bold, colours::text);
    line("A compressor with a live scope and an assistant that listens to your part and suggests a starting point.",
         13.0f, Weight::medium, colours::textSoft, 2);

    area.removeFromTop(14);
    g.setColour(colours::line);
    g.fillRect(area.removeFromTop(1));
    area.removeFromTop(14);

    line("Licence", 12.5f, Weight::demiBold, colours::muted);
    line("Free and open source under the GNU Affero General Public License, version 3. "
         "Provided as is, without any warranty.",
         13.0f, Weight::medium, colours::textSoft, 2);
    area.removeFromTop(26);   // the licence and source links

    line("Built with", 12.5f, Weight::demiBold, colours::muted);
    line("JUCE 8 (AGPLv3), VST3 SDK (MIT), CLAP and clap-juce-extensions (MIT), AudioUnitSDK (Apache 2.0).",
         13.0f, Weight::medium, colours::textSoft, 2);
    area.removeFromTop(8);
    line("VST is a registered trademark of Steinberg Media Technologies GmbH. "
         "Audio Unit is a trademark of Apple Inc.",
         12.0f, Weight::medium, colours::faint, 2);
}

void AboutOverlay::resized()
{
    const auto bounds = card();
    closeButton.setBounds(bounds.getRight() - 16 - 32, bounds.getY() + 16, 32, 32);
    // Sits under the licence text drawn in paint().
    licenceLink.setBounds(bounds.getX() + padding - 2, bounds.getY() + 242, 124, 22);
    sourceLink.setBounds(licenceLink.getRight() + 12, licenceLink.getY(), 110, 22);
}

void AboutOverlay::mouseUp(const juce::MouseEvent& event)
{
    if (!card().contains(event.getPosition()) && onClose)
        onClose();
}

bool AboutOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
}
