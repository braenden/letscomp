#pragma once

#include "PluginProcessor.h"
#include "ui/About.h"
#include "ui/AssistPanel.h"
#include "ui/ControlsPanel.h"
#include "ui/Header.h"
#include "ui/Theme.h"
#include "visualization/ScopeComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

// One page: the assistant panel and the scope side by side, and below
// them one row of knobs. Help lives in tooltips, which the user can turn
// off; the name in the header opens About.
class LetsCompAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LetsCompAudioProcessorEditor(LetsCompAudioProcessor&);
    ~LetsCompAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void assistantChanged();
    void showTips(bool);
    void showAbout();

    LetsCompAudioProcessor& audioProcessor;
    letscomp::ui::Theme theme;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<letscomp::ui::AboutOverlay> about;
    letscomp::ui::Header header;
    letscomp::ui::AssistPanel assistPanel;
    letscomp::visualization::ScopeComponent scope;
    letscomp::ui::ControlsPanel controls;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LetsCompAudioProcessorEditor)
};
