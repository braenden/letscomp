#pragma once

#include "Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace letscomp::ui
{
// Line icons drawn in a 24 by 24 box and stroked at their final size.
namespace icons
{
juce::Path microphone();
juce::Path undo();
juce::Path power();
juce::Path eye();
juce::Path pause();
juce::Path cross();
juce::Path chevronDown();
juce::Path chevronUp();
juce::Path help();
juce::Path sidechain();
}

void drawIcon(juce::Graphics&, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour,
              float strokeWidth = 2.0f);

// A selectable card: a title, an optional dimmer suffix and an optional
// description line. Selected cards turn amber.
class ChoiceButton final : public juce::Button
{
public:
    ChoiceButton();
    void setText(const juce::String& title, const juce::String& suffix = {}, const juce::String& description = {});

private:
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    juce::String title, suffix, description;
};

// A row of equal options with the chosen one raised.
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl(juce::StringArray options, bool framed);
    ~SegmentedControl() override;
    void setSelected(int index);
    int selected() const noexcept { return current; }
    void setTooltip(const juce::String&);
    void setLabel(int index, const juce::String&);
    std::function<void(int)> onChange;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Segment;
    std::vector<std::unique_ptr<Segment>> segments;
    int current = 0;
    bool framed;
};

// The light, rounded call to action (LISTEN).
class PillButton final : public juce::Button
{
public:
    explicit PillButton(juce::Path icon);
    void setFontHeight(float height) { fontHeight = height; repaint(); }
    // 0 to 1 draws a progress line inside the pill; below 0 hides it.
    void setProgress(double newProgress);

private:
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    juce::Path icon;
    float fontHeight = 15.0f;
    double progress = -1.0;
};

// Icon plus text, either quiet (Undo) or outlined (Bypass). When toggled on
// it turns amber, so an active bypass is hard to miss.
class IconTextButton final : public juce::Button
{
public:
    IconTextButton(const juce::String& text, juce::Path icon, bool outlined);

private:
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    juce::Path icon;
    bool outlined;
};

// A small square icon button for the scope's view tools.
class IconButton final : public juce::Button
{
public:
    IconButton(const juce::String& name, juce::Path icon);

private:
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    juce::Path icon;
};
}
