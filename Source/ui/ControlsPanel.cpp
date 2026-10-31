#include "ControlsPanel.h"
#include "../parameters/Parameters.h"

namespace letscomp::ui
{
namespace
{
constexpr int moreWidth = 132;
constexpr int dividerGap = 20;
constexpr float largeWeight = 1.08f;

// Each main knob, the assistant note that explains it, and how close the
// current value must be to the recommendation to still count as its value.
struct Reasoned
{
    const char* id;
    const char* name;
    const char* note;
    bool large;
    float tolerance;
};

constexpr Reasoned mainControls[]{
    {params::threshold, "Threshold", "Threshold", true, 0.05f},
    {params::ratio, "Ratio", "Ratio", true, 0.02f},
    {params::attack, "Attack", "Attack", false, 0.01f},
    {params::release, "Release", "Release", false, 0.5f},
    {params::makeup, "Makeup", "Makeup", false, 0.05f},
    {params::mix, "Mix", "Mix", false, 0.1f},
};

float recommendedValue(const assistant::Recommendation& recommendation, std::string_view id) noexcept
{
    const auto& settings = recommendation.settings;
    if (id == params::threshold) return settings.thresholdDb;
    if (id == params::ratio)     return settings.ratio;
    if (id == params::attack)    return settings.attackMs;
    if (id == params::release)   return settings.releaseMs;
    if (id == params::makeup)    return settings.makeupDb;
    return settings.mix * 100.0f;
}
}

class ControlsPanel::MoreButton final : public juce::Button
{
public:
    MoreButton() : juce::Button("More") {}

    // Names the hidden controls that are on, so lookahead's latency never hides.
    void setActive(const juce::String& newActive)
    {
        if (newActive == active) return;
        active = newActive;
        repaint();
    }

private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(highlighted || down ? colours::raised : colours::control);
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(active.isEmpty() ? colours::border : colours::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, 12.0f, 1.0f);

        auto area = bounds.reduced(12.0f, 0.0f);
        auto title = active.isEmpty() ? area : area.removeFromTop(area.getHeight() * 0.55f);
        const auto titleFont = font(14.0f, Weight::bold);
        const auto width = juce::GlyphArrangement::getStringWidth(titleFont, "More") + 4.0f + 16.0f;
        auto row = title.withSizeKeepingCentre(width, 20.0f);
        if (!active.isEmpty()) row.setY(title.getBottom() - 21.0f);
        g.setColour(colours::textSoft);
        g.setFont(titleFont);
        g.drawText("More", row, juce::Justification::centredLeft);
        drawIcon(g, icons::chevronDown(), row.removeFromRight(16.0f).withSizeKeepingCentre(16.0f, 16.0f),
                 colours::textSoft, 2.0f);
        if (active.isNotEmpty())
        {
            g.setColour(colours::accentText);
            g.setFont(font(12.0f, Weight::demiBold));
            g.drawFittedText(active, area.toNearestInt(), juce::Justification::centredTop, 2, 1.0f);
        }
    }

    juce::String active;
};

// Knee, hold, lookahead, range and the sidechain, shown in a call-out above MORE.
class ControlsPanel::MoreWindow final : public juce::Component
{
public:
    explicit MoreWindow(juce::AudioProcessorValueTreeState& parameters)
        : externalAttachment(parameters, params::externalSidechain, externalButton)
    {
        for (const auto& [id, name] : {std::pair<const char*, const char*>{params::knee, "Knee"},
                                       {params::hold, "Hold"}, {params::lookahead, "Lookahead"},
                                       {params::range, "Range"}, {params::sidechainHighPass, "SC Filter"}})
        {
            auto control = std::make_unique<RotaryControl>(parameters, id, name);
            addAndMakeVisible(*control);
            controls.push_back(std::move(control));
        }
        externalButton.setClickingTogglesState(true);
        externalButton.setTooltip("Compress by the level of the sidechain input instead of this track, "
                                  "for ducking. Route a track to LetsComp's sidechain in your DAW first. "
                                  "The assistant always listens to this track.");
        addAndMakeVisible(externalButton);
        setSize(5 * knobWidth + 2 * 12, 12 + 140 + 8 + 36 + 12);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        externalButton.setBounds(area.removeFromBottom(36).withSizeKeepingCentre(196, 36));
        area.removeFromBottom(8);
        for (auto& control : controls)
            control->setBounds(area.removeFromLeft(knobWidth));
    }

private:
    static constexpr int knobWidth = 104;
    std::vector<std::unique_ptr<RotaryControl>> controls;
    IconTextButton externalButton{"External sidechain", icons::sidechain(), true};
    juce::AudioProcessorValueTreeState::ButtonAttachment externalAttachment;
};

ControlsPanel::ControlsPanel(juce::AudioProcessorValueTreeState& stateToUse)
    : state(stateToUse), moreButton(std::make_unique<MoreButton>())
{
    for (const auto& control : mainControls)
    {
        controls.push_back(std::make_unique<RotaryControl>(state, control.id, control.name, control.large));
        addAndMakeVisible(*controls.back());
        ids.emplace_back(control.id);
    }
    moreButton->setTooltip("Knee, hold, lookahead, range and the sidechain. Most parts never need "
                           "them changed.");
    moreButton->onClick = [this] { showMore(); };
    addAndMakeVisible(*moreButton);
    timerCallback();
    startTimerHz(4);
}

ControlsPanel::~ControlsPanel()
{
    // JUCE owns the call-out and deletes it once it has closed.
    if (moreWindow != nullptr)
        moreWindow->dismiss();
}

void ControlsPanel::showMore()
{
    if (moreWindow != nullptr) return;
    auto* top = getTopLevelComponent();
    const auto target = top->getLocalArea(this, moreButton->getBounds());
    moreWindow = &juce::CallOutBox::launchAsynchronously(std::make_unique<MoreWindow>(state), target, top);
}

void ControlsPanel::setRecommendation(const assistant::Recommendation* newRecommendation)
{
    recommendation = newRecommendation != nullptr ? std::optional(*newRecommendation) : std::nullopt;
    updateReasons();
}

void ControlsPanel::updateReasons()
{
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const auto& definition = mainControls[index];
        juce::String note;
        auto changed = false;
        if (recommendation.has_value())
        {
            for (const auto& entry : recommendation->notes)
                if (entry.parameter == definition.note)
                    note = juce::String(entry.text);
            if (const auto* value = state.getRawParameterValue(definition.id))
            {
                const auto target = recommendedValue(*recommendation, definition.id);
                const auto tolerance = std::max(definition.tolerance, std::abs(target) * 0.005f);
                changed = std::abs(value->load(std::memory_order_relaxed) - target) > tolerance;
            }
        }
        controls[index]->setAssistantNote(note, changed);
    }
}

void ControlsPanel::timerCallback()
{
    updateReasons();

    juce::StringArray active;
    const auto isOn = [this](const char* id, float offValue)
    {
        const auto* value = state.getRawParameterValue(id);
        return value != nullptr && value->load() > offValue + 0.01f;
    };
    for (const auto& [id, name] : {std::pair<const char*, const char*>{params::hold, "Hold"},
                                   {params::lookahead, "Lookahead"}, {params::range, "Range"}})
        if (isOn(id, 0.0f))
            active.add(name);
    if (isOn(params::sidechainHighPass, params::domain::sidechainHighPassOffHz + 0.5f))
        active.add("SC filter");
    if (isOn(params::externalSidechain, 0.5f))
        active.add("External SC");
    moreButton->setActive(active.isEmpty() ? juce::String() : active.joinIntoString(", ") + " on");
}

void ControlsPanel::paint(juce::Graphics& g)
{
    g.setColour(colours::line);
    for (const auto x : dividers)
        g.fillRect(x, 24, 1, getHeight() - 48);
}

void ControlsPanel::resized()
{
    auto area = getLocalBounds();
    dividers.clear();
    auto more = area.removeFromRight(moreWidth);
    area.removeFromRight(dividerGap);
    dividers.push_back(area.getRight());
    area.removeFromRight(1 + dividerGap);
    moreButton->setBounds(more.withSizeKeepingCentre(moreWidth, 64));

    const auto available = static_cast<float>(area.getWidth() - (2 * dividerGap + 1));
    const auto unit = available / (2.0f * largeWeight + 4.0f);
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const auto width = static_cast<int>(std::round(unit * (mainControls[index].large ? largeWeight : 1.0f)));
        controls[index]->setBounds(area.removeFromLeft(width));
        if (index == 1)
        {
            area.removeFromLeft(dividerGap);
            dividers.push_back(area.getX());
            area.removeFromLeft(1 + dividerGap);
        }
    }
}
}
