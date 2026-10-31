#include "Widgets.h"

namespace letscomp::ui
{
namespace icons
{
juce::Path microphone()
{
    juce::Path path;
    path.addRoundedRectangle(9.0f, 3.0f, 6.0f, 12.0f, 3.0f);
    path.addCentredArc(12.0f, 11.0f, 7.0f, 7.0f, 0.0f, juce::MathConstants<float>::halfPi,
                       juce::MathConstants<float>::pi * 1.5f, true);
    path.startNewSubPath(12.0f, 18.0f);
    path.lineTo(12.0f, 21.0f);
    return path;
}

juce::Path undo()
{
    juce::Path path;
    path.startNewSubPath(9.0f, 14.0f);
    path.lineTo(4.0f, 9.0f);
    path.lineTo(9.0f, 4.0f);
    path.startNewSubPath(4.0f, 9.0f);
    path.lineTo(15.0f, 9.0f);
    path.addCentredArc(15.0f, 14.0f, 5.0f, 5.0f, 0.0f, 0.0f, juce::MathConstants<float>::pi);
    path.lineTo(12.0f, 19.0f);
    return path;
}

juce::Path power()
{
    juce::Path path;
    path.startNewSubPath(12.0f, 3.0f);
    path.lineTo(12.0f, 11.0f);
    path.addCentredArc(12.0f, 13.0f, 8.0f, 8.0f, 0.0f, 0.24f * juce::MathConstants<float>::pi,
                       1.76f * juce::MathConstants<float>::pi, true);
    return path;
}

juce::Path eye()
{
    juce::Path path;
    path.startNewSubPath(2.0f, 12.0f);
    path.quadraticTo(12.0f, 2.0f, 22.0f, 12.0f);
    path.quadraticTo(12.0f, 22.0f, 2.0f, 12.0f);
    path.closeSubPath();
    path.addEllipse(9.0f, 9.0f, 6.0f, 6.0f);
    return path;
}

juce::Path pause()
{
    juce::Path path;
    path.startNewSubPath(9.0f, 5.0f);
    path.lineTo(9.0f, 19.0f);
    path.startNewSubPath(15.0f, 5.0f);
    path.lineTo(15.0f, 19.0f);
    return path;
}

juce::Path cross()
{
    juce::Path path;
    path.startNewSubPath(6.0f, 6.0f);
    path.lineTo(18.0f, 18.0f);
    path.startNewSubPath(18.0f, 6.0f);
    path.lineTo(6.0f, 18.0f);
    return path;
}

juce::Path chevronDown()
{
    juce::Path path;
    path.startNewSubPath(6.0f, 9.0f);
    path.lineTo(12.0f, 15.0f);
    path.lineTo(18.0f, 9.0f);
    return path;
}

juce::Path chevronUp()
{
    juce::Path path;
    path.startNewSubPath(6.0f, 15.0f);
    path.lineTo(12.0f, 9.0f);
    path.lineTo(18.0f, 15.0f);
    return path;
}

// A signal entering from the side into the detector.
juce::Path sidechain()
{
    juce::Path path;
    path.addRoundedRectangle(9.0f, 5.0f, 12.0f, 14.0f, 3.0f);
    path.startNewSubPath(2.0f, 12.0f);
    path.lineTo(13.0f, 12.0f);
    path.startNewSubPath(10.0f, 9.0f);
    path.lineTo(13.0f, 12.0f);
    path.lineTo(10.0f, 15.0f);
    return path;
}

juce::Path help()
{
    juce::Path path;
    path.addEllipse(3.0f, 3.0f, 18.0f, 18.0f);
    path.addCentredArc(12.0f, 9.5f, 2.8f, 2.8f, 0.0f, -1.4f, 1.9f, true);
    path.lineTo(12.0f, 13.5f);
    path.startNewSubPath(12.0f, 16.6f);
    path.lineTo(12.0f, 17.0f);
    return path;
}
}

void drawIcon(juce::Graphics& g, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour colour,
              float strokeWidth)
{
    const auto scale = std::min(area.getWidth(), area.getHeight()) / 24.0f;
    auto path = icon;
    path.applyTransform(juce::AffineTransform::scale(scale)
                            .translated(area.getCentreX() - 12.0f * scale, area.getCentreY() - 12.0f * scale));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

ChoiceButton::ChoiceButton() : juce::Button({})
{
    setClickingTogglesState(true);
}

void ChoiceButton::setText(const juce::String& newTitle, const juce::String& newSuffix,
                           const juce::String& newDescription)
{
    title = newTitle;
    suffix = newSuffix;
    description = newDescription;
    setButtonText(suffix.isEmpty() ? title : title + " " + suffix);
    setTitle(getButtonText());
    setDescription(description);
    repaint();
}

void ChoiceButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto selected = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(selected ? colours::accentFill : colours::control);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(selected ? colours::accent : highlighted || down ? colours::borderHover : colours::border);
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

    const auto titleColour = selected ? colours::accentText : colours::text;
    if (description.isEmpty())
    {
        g.setColour(selected ? colours::accentText : colours::textSoft);
        g.setFont(font(14.5f, selected ? Weight::bold : Weight::demiBold));
        g.drawText(title, bounds, juce::Justification::centred);
        return;
    }

    auto text = bounds.reduced(14.0f, 0.0f);
    auto titleRow = text.removeFromTop(text.getHeight() * 0.54f).withTrimmedTop(3.0f);
    const auto titleFont = font(14.5f, Weight::bold);
    g.setFont(titleFont);
    g.setColour(titleColour);
    g.drawText(title, titleRow, juce::Justification::bottomLeft);
    if (suffix.isNotEmpty())
    {
        const auto offset = juce::GlyphArrangement::getStringWidth(titleFont, title) + 5.0f;
        g.setFont(font(14.0f, Weight::medium));
        g.setColour(selected ? colours::accentSoft : colours::muted);
        g.drawText(suffix, titleRow.withTrimmedLeft(offset), juce::Justification::bottomLeft);
    }
    g.setFont(font(12.5f, Weight::medium));
    g.setColour(selected ? colours::accentSoft : colours::muted);
    g.drawText(description, text.withTrimmedTop(1.0f), juce::Justification::topLeft);
}

class SegmentedControl::Segment final : public juce::Button
{
public:
    explicit Segment(const juce::String& text) : juce::Button(text) {}

private:
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto selected = getToggleState();
        if (selected)
        {
            g.setColour(colours::selected);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
        }
        g.setColour(selected ? colours::text : highlighted ? colours::textSoft : colours::muted);
        g.setFont(font(13.5f, selected ? Weight::bold : Weight::demiBold));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

SegmentedControl::SegmentedControl(juce::StringArray options, bool isFramed) : framed(isFramed)
{
    static int nextGroup = 9000;
    const auto group = ++nextGroup;
    for (int index = 0; index < options.size(); ++index)
    {
        auto segment = std::make_unique<Segment>(options[index]);
        segment->setClickingTogglesState(true);
        segment->setRadioGroupId(group);
        segment->onClick = [this, index]
        {
            if (index == current) return;
            current = index;
            if (onChange) onChange(index);
        };
        addAndMakeVisible(*segment);
        segments.push_back(std::move(segment));
    }
    setSelected(0);
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::setSelected(int index)
{
    current = juce::jlimit(0, static_cast<int>(segments.size()) - 1, index);
    segments[static_cast<std::size_t>(current)]->setToggleState(true, juce::dontSendNotification);
}

void SegmentedControl::setTooltip(const juce::String& text)
{
    for (auto& segment : segments)
        segment->setTooltip(text);
}

void SegmentedControl::setLabel(int index, const juce::String& text)
{
    if (index >= 0 && index < static_cast<int>(segments.size()))
        segments[static_cast<std::size_t>(index)]->setButtonText(text);
}

void SegmentedControl::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(framed ? colours::control : colours::raised);
    g.fillRoundedRectangle(bounds, 10.0f);
    if (framed)
    {
        g.setColour(colours::border);
        g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
    }
}

void SegmentedControl::resized()
{
    auto area = getLocalBounds().reduced(3);
    const auto gap = 3;
    const auto count = static_cast<int>(segments.size());
    const auto width = (area.getWidth() - gap * (count - 1)) / count;
    for (auto& segment : segments)
    {
        segment->setBounds(area.removeFromLeft(width));
        area.removeFromLeft(gap);
    }
}

PillButton::PillButton(juce::Path iconToUse) : juce::Button({}), icon(std::move(iconToUse)) {}

void PillButton::setProgress(double newProgress)
{
    if (std::abs(newProgress - progress) < 1.0e-3) return;
    progress = newProgress;
    repaint();
}

void PillButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    auto fill = colours::text;
    if (highlighted) fill = juce::Colours::white;
    if (down) fill = fill.darker(0.08f);
    if (!isEnabled()) fill = fill.withMultipliedAlpha(0.4f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);

    const auto textFont = font(fontHeight, Weight::bold);
    const auto iconSize = fontHeight + 3.0f;
    const auto textWidth = juce::GlyphArrangement::getStringWidth(textFont, getButtonText());
    const auto total = (icon.isEmpty() ? 0.0f : iconSize + 8.0f) + textWidth;
    auto x = bounds.getCentreX() - total * 0.5f;
    if (!icon.isEmpty())
    {
        drawIcon(g, icon, {x, bounds.getCentreY() - iconSize * 0.5f, iconSize, iconSize}, colours::background, 2.2f);
        x += iconSize + 8.0f;
    }
    g.setColour(colours::background);
    g.setFont(textFont);
    g.drawText(getButtonText(), juce::Rectangle<float>(x, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
               juce::Justification::centredLeft);

    if (progress >= 0.0)
    {
        const auto bar = bounds.reduced(bounds.getHeight() * 0.5f, 0.0f).removeFromBottom(7.0f).withHeight(3.0f);
        g.setColour(colours::muted.withAlpha(0.35f));
        g.fillRoundedRectangle(bar, 1.5f);
        g.setColour(colours::accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, progress))), 1.5f);
    }
}

IconTextButton::IconTextButton(const juce::String& text, juce::Path iconToUse, bool isOutlined)
    : juce::Button(text), icon(std::move(iconToUse)), outlined(isOutlined)
{
}

void IconTextButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (on || outlined || highlighted)
    {
        g.setColour(on ? colours::accentFill : outlined ? colours::panel : colours::raised);
        g.fillRoundedRectangle(bounds, 9.0f);
    }
    if (on || outlined)
    {
        g.setColour(on ? colours::accent : highlighted || down ? colours::borderHover : colours::selected);
        g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
    }

    auto colour = on ? colours::accentText : outlined ? colours::textSoft : colours::muted;
    if (highlighted && !on) colour = colours::text;
    if (!isEnabled()) colour = colours::faint;
    const auto textFont = font(13.5f, Weight::demiBold);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(textFont, getButtonText());
    const auto total = 15.0f + 7.0f + textWidth;
    auto x = bounds.getCentreX() - total * 0.5f;
    drawIcon(g, icon, {x, bounds.getCentreY() - 7.5f, 15.0f, 15.0f}, colour, 1.8f);
    g.setColour(colour);
    g.setFont(textFont);
    g.drawText(getButtonText(), juce::Rectangle<float>(x + 22.0f, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
               juce::Justification::centredLeft);
}

IconButton::IconButton(const juce::String& name, juce::Path iconToUse) : juce::Button(name), icon(std::move(iconToUse))
{
    setTitle(name);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const auto on = getToggleState();
    if (on || highlighted)
    {
        g.setColour(on ? colours::raised : colours::control);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    }
    drawIcon(g, icon, getLocalBounds().toFloat().reduced(8.0f), on || highlighted ? colours::textSoft : colours::muted,
             1.9f);
}
}
