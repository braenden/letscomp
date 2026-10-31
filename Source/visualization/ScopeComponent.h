#pragma once

#include "HistoryBuffer.h"
#include "../ui/Widgets.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace letscomp::visualization
{
// The live scope: input behind output as soft filled areas, gain reduction
// hanging from the top, and the threshold with a handle to drag. A readout
// column shows the compression right now as a ring with the assistant's
// target, plus input and output meters.
class ScopeComponent final : public juce::Component
{
public:
    ScopeComponent(ScopeFifo<8192>&, std::atomic<float>* thresholdValue,
                   juce::RangedAudioParameter* thresholdParameter, MeterValues&);

    void clearHistory() noexcept;
    // Gain reduction the assistant aimed for on the loud parts; drawn as a
    // reference line in the gain-reduction lane. nullopt hides it.
    void setGainReductionTarget(std::optional<float> targetDb)
    {
        gainReductionTarget = targetDb;
        repaint();
    }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    struct Layout
    {
        juce::Rectangle<float> plot, gainReduction, waveform, readout;
    };

    Layout layout() const noexcept;
    float thresholdDb() const noexcept;
    float thresholdOffset(const Layout&) const noexcept;
    juce::Rectangle<float> handleBounds(const Layout&) const;
    bool overThreshold(juce::Point<float>) const;
    void handleVBlank();
    void drawWaveforms(juce::Graphics&, juce::Rectangle<float>) const;
    void drawGainReduction(juce::Graphics&, juce::Rectangle<float>) const;
    void drawThreshold(juce::Graphics&, const Layout&) const;
    void drawReadout(juce::Graphics&, juce::Rectangle<float>) const;
    float gainReductionScaleDb() const noexcept;

    ScopeFifo<8192>& fifo;
    std::atomic<float>* threshold = nullptr;
    juce::RangedAudioParameter* thresholdParameter = nullptr;
    MeterValues& meters;
    HistoryBuffer history{4000};
    std::optional<float> gainReductionTarget;
    bool frozen = false, showComparison = true, dragging = false;
    // Readouts fall slowly so the numbers can be read while the audio moves.
    float shownReductionDb = 0.0f, shownInputDb = -80.0f, shownOutputDb = -80.0f;
    ui::IconButton inputButton{"Show input", ui::icons::eye()}, freezeButton{"Freeze", ui::icons::pause()},
                   clearButton{"Clear", ui::icons::cross()};
    juce::VBlankAttachment vBlankAttachment{this, [this] { handleVBlank(); }};
};
}
