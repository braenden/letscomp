#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace letscomp::ui
{
// One dark surface with few frames. Amber always means compression;
// blue-violet is the output and the value of a control.
namespace colours
{
inline const juce::Colour background{0xff0d0f13};
inline const juce::Colour scope{0xff0a0c10};
inline const juce::Colour panel{0xff12151b};
inline const juce::Colour control{0xff14171d};
inline const juce::Colour raised{0xff161a21};
inline const juce::Colour selected{0xff262b36};
inline const juce::Colour line{0xff1c2028};
inline const juce::Colour border{0xff222731};
inline const juce::Colour borderHover{0xff323947};
inline const juce::Colour text{0xffe8ebf0};
inline const juce::Colour textSoft{0xffc9cfd9};
inline const juce::Colour muted{0xff8a93a3};
inline const juce::Colour faint{0xff5e6676};
inline const juce::Colour accent{0xfff5a524};
inline const juce::Colour accentText{0xffffd08a};
inline const juce::Colour accentNumber{0xffffc266};
inline const juce::Colour accentFill{0xff2a2216};
inline const juce::Colour accentSoft{0xffc9b48f};
inline const juce::Colour output{0xff7c8cff};
inline const juce::Colour input{0xff262c37};
}

enum class Weight { regular, medium, demiBold, bold, heavy };

// Avenir Next ships with every Mac and Segoe UI with every Windows PC; other
// systems use their default sans serif.
inline juce::Font font(float height, Weight weight = Weight::medium)
{
#if JUCE_MAC
    const char* style = "Medium";
    switch (weight)
    {
        case Weight::regular:  style = "Regular"; break;
        case Weight::medium:   style = "Medium"; break;
        case Weight::demiBold: style = "Demi Bold"; break;
        case Weight::bold:     style = "Bold"; break;
        case Weight::heavy:    style = "Heavy"; break;
    }
    return juce::Font(juce::FontOptions("Avenir Next", height, juce::Font::plain).withStyle(style));
#elif JUCE_WINDOWS
    const char* style = "Regular";
    switch (weight)
    {
        case Weight::regular:
        case Weight::medium:   style = "Regular"; break;
        case Weight::demiBold: style = "Semibold"; break;
        case Weight::bold:     style = "Bold"; break;
        case Weight::heavy:    style = "Black"; break;
    }
    return juce::Font(juce::FontOptions("Segoe UI", height, juce::Font::plain).withStyle(style));
#else
    const auto bold = weight == Weight::demiBold || weight == Weight::bold || weight == Weight::heavy;
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
#endif
}

class Theme final : public juce::LookAndFeel_V4
{
public:
    Theme()
    {
        setColour(juce::Slider::rotarySliderFillColourId, colours::output);
        setColour(juce::Slider::rotarySliderOutlineColourId, colours::line);
        setColour(juce::Slider::textBoxTextColourId, colours::text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, colours::output.withAlpha(0.4f));
        setColour(juce::Label::textWhenEditingColourId, colours::text);
        setColour(juce::Label::outlineWhenEditingColourId, colours::border);
        setColour(juce::Label::backgroundWhenEditingColourId, colours::control);
        setColour(juce::TextEditor::highlightColourId, colours::output.withAlpha(0.4f));
        setColour(juce::CaretComponent::caretColourId, colours::text);
        setColour(juce::TooltipWindow::backgroundColourId, colours::raised);
        setColour(juce::TooltipWindow::textColourId, colours::text);
        setColour(juce::TooltipWindow::outlineColourId, colours::border);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        const auto large = slider.getProperties()["large"];
        label->setFont(font(large ? 18.0f : 15.0f, large ? Weight::bold : Weight::demiBold));
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour(juce::Label::textColourId, colours::text);
        return label;
    }

    // A thick arc on a dark disc with a dot for the pointer. Bipolar controls
    // (makeup) draw the arc from the centre so 0 dB shows as no arc.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosition, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto large = static_cast<bool>(slider.getProperties()["large"]);
        const auto bipolar = static_cast<bool>(slider.getProperties()["bipolar"]);
        const auto size = static_cast<float>(std::min(width, height));
        const auto centre = juce::Rectangle<int>(x, y, width, height).toFloat().getCentre();
        const auto stroke = large ? 8.0f : 6.0f;
        const auto radius = size * 0.5f - stroke * 0.5f - 1.0f;
        const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(colours::control);
        g.fillEllipse(juce::Rectangle<float>(radius * 1.58f, radius * 1.58f).withCentre(centre));

        const auto arc = [&](float from, float to)
        {
            juce::Path path;
            path.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, from, to, true);
            return path;
        };
        const juce::PathStrokeType strokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(colours::line);
        g.strokePath(arc(rotaryStartAngle, rotaryEndAngle), strokeType);

        const auto origin = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;
        if (std::abs(angle - origin) > 0.01f)
        {
            g.setColour(slider.isEnabled() ? colours::output : colours::faint);
            g.strokePath(arc(std::min(origin, angle), std::max(origin, angle)), strokeType);
        }

        const auto dotRadius = radius * 0.55f;
        const auto dot = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * dotRadius;
        g.setColour(colours::text);
        g.fillEllipse(juce::Rectangle<float>(large ? 8.0f : 6.0f, large ? 8.0f : 6.0f).withCentre(dot));
    }

    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        g.setColour(colours::raised);
        g.fillPath(path);
        g.setColour(colours::border);
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }

    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 16; }
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return 12.0f; }

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        const auto bounds = juce::Rectangle<int>(width, height).toFloat();
        g.setColour(colours::raised);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(colours::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
        g.setColour(colours::text);
        g.setFont(font(13.0f, Weight::medium));
        g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(12, 8),
                         juce::Justification::centredLeft, 12, 1.0f);
    }

    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> position,
                                          juce::Rectangle<int> parentArea) override
    {
        juce::AttributedString attributed;
        attributed.append(text, font(13.0f, Weight::medium));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths(attributed, 360.0f);
        const auto w = static_cast<int>(layout.getWidth()) + 26;
        const auto h = static_cast<int>(layout.getHeight()) + 18;
        return juce::Rectangle<int>(position.x > parentArea.getCentreX() ? position.x - (w + 12) : position.x + 24,
                                    position.y > parentArea.getCentreY() ? position.y - (h + 6) : position.y + 6,
                                    w, h)
            .constrainedWithin(parentArea);
    }
};
}
