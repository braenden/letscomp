#pragma once

#include "Widgets.h"

namespace letscomp::ui
{
// Who made LetsComp, its licence and the libraries it is built with, as a
// card over the dimmed editor. Clicking outside the card or Escape closes it.
class AboutOverlay final : public juce::Component
{
public:
    AboutOverlay();
    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    juce::Rectangle<int> card() const;

    IconButton closeButton{"Close", icons::cross()};
    juce::HyperlinkButton licenceLink{"Read the licence", juce::URL("https://www.gnu.org/licenses/agpl-3.0.html")};
    juce::HyperlinkButton sourceLink{"Source code", juce::URL("https://github.com/braenden/LetsComp")};
};
}
