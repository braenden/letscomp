#include "ScopeComponent.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>

namespace letscomp::visualization
{
namespace
{
namespace colours = ui::colours;
using ui::Weight;

// Waveforms, the threshold line and its handle share one vertical scale,
// otherwise the threshold appears above the level it actually acts on.
constexpr float waveformScale = 0.47f;
constexpr float readoutWidth = 150.0f;
constexpr float meterFloorDb = -60.0f;
constexpr float readoutFallDbPerFrame = 0.25f;   // about 15 dB per second at 60 Hz
constexpr float cornerRadius = 16.0f;

juce::String levelText(float db)
{
    return db <= meterFloorDb + 0.05f ? juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e") : juce::String(db, 1);
}
}

ScopeComponent::ScopeComponent(ScopeFifo<8192>& source, std::atomic<float>* thresholdValue,
                               juce::RangedAudioParameter* parameter, MeterValues& meterValues)
    : fifo(source), threshold(thresholdValue), thresholdParameter(parameter), meters(meterValues)
{
    inputButton.setClickingTogglesState(true);
    inputButton.setToggleState(true, juce::dontSendNotification);
    freezeButton.setClickingTogglesState(true);
    inputButton.setTooltip("Show the uncompressed input behind the output");
    freezeButton.setTooltip("Freeze the picture; audio keeps playing");
    clearButton.setTooltip("Clear the picture");
    inputButton.onClick = [this] { showComparison = inputButton.getToggleState(); repaint(); };
    freezeButton.onClick = [this] { frozen = freezeButton.getToggleState(); };
    clearButton.onClick = [this] { clearHistory(); };
    for (auto* button : {&inputButton, &freezeButton, &clearButton})
        addAndMakeVisible(*button);

    // The editor may have been closed for minutes while the audio thread kept
    // producing frames. Start with live data instead of replaying that backlog.
    ScopeFrame staleFrame;
    while (fifo.pop(staleFrame)) {}
}

void ScopeComponent::clearHistory() noexcept
{
    history.clear();
    repaint();
}

ScopeComponent::Layout ScopeComponent::layout() const noexcept
{
    Layout result;
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    result.readout = bounds.removeFromRight(readoutWidth);
    result.plot = bounds;
    const auto laneHeight = std::max(52.0f, std::round(bounds.getHeight() * 0.18f));
    bounds.removeFromTop(2.0f);
    result.gainReduction = bounds.removeFromTop(laneHeight);
    bounds.removeFromTop(8.0f);
    result.waveform = bounds.withTrimmedBottom(8.0f);
    return result;
}

void ScopeComponent::resized()
{
    const auto plot = layout().plot.toNearestInt();
    auto row = juce::Rectangle<int>(plot.getRight() - 12 - 3 * 32 - 2 * 4, plot.getBottom() - 12 - 32, 3 * 32 + 2 * 4, 32);
    inputButton.setBounds(row.removeFromLeft(32));
    row.removeFromLeft(4);
    freezeButton.setBounds(row.removeFromLeft(32));
    row.removeFromLeft(4);
    clearButton.setBounds(row);
}

void ScopeComponent::handleVBlank()
{
    ScopeFrame frame;
    while (fifo.pop(frame))
    {
        if (!frozen)
            history.push(frame);
    }

    const auto fall = [](float shown, float current)
    {
        return std::max(current, shown - readoutFallDbPerFrame);
    };
    shownReductionDb = fall(shownReductionDb, std::max(0.0f, meters.currentGainReductionDb.load(std::memory_order_relaxed)));
    shownInputDb = fall(shownInputDb, juce::Decibels::gainToDecibels(meters.inputPeak.load(std::memory_order_relaxed), meterFloorDb));
    shownOutputDb = fall(shownOutputDb, juce::Decibels::gainToDecibels(meters.outputPeak.load(std::memory_order_relaxed), meterFloorDb));
    repaint();
}

void ScopeComponent::paint(juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::scope);
    g.fillRoundedRectangle(frame, cornerRadius);

    const auto parts = layout();
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(frame, cornerRadius);
        g.reduceClipRegion(clip);

        g.setColour(colours::raised);
        g.fillRect(parts.plot.getX(), parts.gainReduction.getBottom() + 4.0f, parts.plot.getWidth(), 1.0f);
        g.fillRect(parts.plot.getX(), std::round(parts.waveform.getCentreY()), parts.plot.getWidth(), 1.0f);

        drawWaveforms(g, parts.waveform);
        drawGainReduction(g, parts.gainReduction);
        drawThreshold(g, parts);
        drawReadout(g, parts.readout);
    }
    g.setColour(colours::line);
    g.drawRoundedRectangle(frame, cornerRadius, 1.0f);
}

void ScopeComponent::drawWaveforms(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto available = history.size();
    if (available == 0) return;
    const auto width = std::max(1, static_cast<int>(area.getWidth()));
    const auto centre = area.getCentreY();
    const auto scale = area.getHeight() * waveformScale;
    const auto startPixel = static_cast<int>(static_cast<size_t>(width) * (history.capacity() - available)
                                              / history.capacity());
    const auto visibleWidth = std::max(1, width - startPixel);

    std::vector<juce::Point<float>> inTop, inBottom, outTop, outBottom;
    for (int pixel = startPixel; pixel < width; ++pixel)
    {
        const auto relativePixel = pixel - startPixel;
        const auto first = static_cast<size_t>(relativePixel) * available / static_cast<size_t>(visibleWidth);
        const auto last = std::max(first + 1, static_cast<size_t>(relativePixel + 1) * available
                                                  / static_cast<size_t>(visibleWidth));
        float inMin = 0.0f, inMax = 0.0f, outMin = 0.0f, outMax = 0.0f;
        for (auto index = first; index < std::min(last, available); ++index)
        {
            const auto& sample = history.at(index);
            inMin = std::min(inMin, sample.inputMin); inMax = std::max(inMax, sample.inputMax);
            outMin = std::min(outMin, sample.outputMin); outMax = std::max(outMax, sample.outputMax);
        }
        const auto x = area.getX() + static_cast<float>(pixel);
        const auto limit = [&](float value) { return juce::jlimit(-1.06f, 1.06f, value) * scale; };
        inTop.push_back({x, centre - limit(inMax)});
        inBottom.push_back({x, centre - limit(inMin)});
        outTop.push_back({x, centre - limit(outMax)});
        outBottom.push_back({x, centre - limit(outMin)});
    }
    const auto envelope = [](const std::vector<juce::Point<float>>& top, const std::vector<juce::Point<float>>& bottom)
    {
        juce::Path path;
        path.startNewSubPath(top.front());
        for (const auto& point : top) path.lineTo(point);
        for (auto point = bottom.rbegin(); point != bottom.rend(); ++point) path.lineTo(*point);
        path.closeSubPath();
        return path;
    };
    if (showComparison)
    {
        g.setColour(colours::input);
        g.fillPath(envelope(inTop, inBottom));
    }
    g.setColour(colours::output.withAlpha(0.92f));
    g.fillPath(envelope(outTop, outBottom));
}

float ScopeComponent::gainReductionScaleDb() const noexcept
{
    // Grow the lane in steps when heavy settings exceed the default 12 dB,
    // so deep reduction is never drawn flat against the bottom edge.
    auto maximum = gainReductionTarget.value_or(0.0f);
    for (size_t index = 0; index < history.size(); ++index)
        maximum = std::max(maximum, history.at(index).gainReductionDb);
    return maximum > 48.0f ? 72.0f : maximum > 24.0f ? 48.0f : maximum > 12.0f ? 24.0f : 12.0f;
}

void ScopeComponent::drawGainReduction(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto scaleDb = gainReductionScaleDb();
    const auto yFor = [&](float db) { return area.getY() + juce::jlimit(0.0f, 1.0f, db / scaleDb) * area.getHeight(); };

    if (gainReductionTarget.has_value())
    {
        const auto y = std::floor(yFor(*gainReductionTarget)) + 0.5f;
        const float dashes[]{2.0f, 5.0f};
        g.setColour(colours::accent.withAlpha(0.55f));
        g.drawDashedLine({area.getX(), y, area.getRight(), y}, dashes, 2, 1.0f);
        g.setColour(colours::accentText);
        g.setFont(ui::font(12.0f, Weight::bold));
        g.drawText("Target " + juce::String(*gainReductionTarget, 1),
                   juce::Rectangle<float>(area.getX() + 14.0f, y - 17.0f, 120.0f, 15.0f),
                   juce::Justification::centredLeft);
    }

    const auto available = history.size();
    if (available == 0) return;
    const auto width = std::max(1, static_cast<int>(area.getWidth()));
    const auto startPixel = static_cast<int>(static_cast<size_t>(width) * (history.capacity() - available)
                                              / history.capacity());
    const auto visibleWidth = std::max(1, width - startPixel);
    juce::Path line;
    for (int pixel = startPixel; pixel < width; ++pixel)
    {
        const auto relativePixel = pixel - startPixel;
        const auto first = static_cast<size_t>(relativePixel) * available / static_cast<size_t>(visibleWidth);
        const auto last = std::max(first + 1, static_cast<size_t>(relativePixel + 1) * available
                                                  / static_cast<size_t>(visibleWidth));
        float maximum = 0.0f;
        for (auto index = first; index < std::min(last, available); ++index)
            maximum = std::max(maximum, history.at(index).gainReductionDb);
        const auto x = area.getX() + static_cast<float>(pixel);
        if (pixel == startPixel) line.startNewSubPath(x, yFor(maximum)); else line.lineTo(x, yFor(maximum));
    }
    auto fill = line;
    fill.lineTo(area.getRight(), area.getY());
    fill.lineTo(area.getX() + static_cast<float>(startPixel), area.getY());
    fill.closeSubPath();
    g.setColour(colours::accent.withAlpha(0.16f));
    g.fillPath(fill);
    g.setColour(colours::accent);
    g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

float ScopeComponent::thresholdDb() const noexcept
{
    return threshold != nullptr ? threshold->load(std::memory_order_relaxed) : -18.0f;
}

float ScopeComponent::thresholdOffset(const Layout& parts) const noexcept
{
    return std::round(juce::Decibels::decibelsToGain(thresholdDb()) * parts.waveform.getHeight() * waveformScale);
}

juce::Rectangle<float> ScopeComponent::handleBounds(const Layout& parts) const
{
    const auto text = "Threshold " + juce::String(thresholdDb(), 1) + " dB";
    const auto width = juce::GlyphArrangement::getStringWidth(ui::font(12.5f, Weight::bold), text) + 34.0f;
    const auto y = parts.waveform.getCentreY() - thresholdOffset(parts);
    return {parts.plot.getX() + 14.0f, y - 15.0f, width, 30.0f};
}

void ScopeComponent::drawThreshold(juce::Graphics& g, const Layout& parts) const
{
    const auto centre = parts.waveform.getCentreY();
    const auto offset = thresholdOffset(parts);
    const float dashes[]{5.0f, 5.0f};
    g.setColour(colours::textSoft.withAlpha(0.75f));
    g.drawDashedLine({parts.plot.getX(), centre - offset, parts.plot.getRight(), centre - offset}, dashes, 2, 1.0f);
    g.setColour(colours::textSoft.withAlpha(0.3f));
    g.drawDashedLine({parts.plot.getX(), centre + offset, parts.plot.getRight(), centre + offset}, dashes, 2, 1.0f);

    g.setColour(colours::faint);
    g.setFont(ui::font(11.5f, Weight::demiBold));
    g.drawText("0 dB", juce::Rectangle<float>(parts.plot.getX() + 14.0f, centre - parts.waveform.getHeight() * waveformScale - 16.0f,
                                              60.0f, 14.0f), juce::Justification::centredLeft);

    auto handle = handleBounds(parts);
    g.setColour(dragging ? juce::Colours::white : colours::text);
    g.fillRoundedRectangle(handle, 15.0f);
    g.setColour(colours::background);
    g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre({handle.getX() + 14.0f, handle.getCentreY()}));
    g.setFont(ui::font(12.5f, Weight::bold));
    g.drawText("Threshold " + juce::String(thresholdDb(), 1) + " dB", handle.withTrimmedLeft(24.0f),
               juce::Justification::centredLeft);
}

void ScopeComponent::drawReadout(juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour(colours::raised);
    g.fillRect(area.removeFromLeft(1.0f));
    area.reduce(14.0f, 18.0f);

    // Compression right now as a ring with the number inside; the
    // assistant's target is a tick on the ring.
    g.setColour(colours::muted);
    g.setFont(ui::font(13.0f, Weight::demiBold));
    g.drawText("Compression", area.removeFromTop(18.0f), juce::Justification::centred);
    area.removeFromTop(6.0f);
    const auto size = std::min(area.getWidth(), 120.0f);
    const auto ring = area.removeFromTop(size).withSizeKeepingCentre(size, size);
    const auto centre = ring.getCentre();
    const auto stroke = 9.0f;
    const auto radius = size * 0.5f - stroke * 0.5f - 6.0f;
    const auto start = -0.75f * juce::MathConstants<float>::pi;
    const auto sweep = 1.5f * juce::MathConstants<float>::pi;
    const auto scaleDb = gainReductionTarget.value_or(0.0f) > 8.0f ? 18.0f : 12.0f;
    const auto arc = [&](float from, float to)
    {
        juce::Path path;
        path.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        return path;
    };
    const juce::PathStrokeType strokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(colours::line);
    g.strokePath(arc(start, start + sweep), strokeType);
    const auto reduction = shownReductionDb < 0.05f ? 0.0f : shownReductionDb;
    if (reduction > 0.0f)
    {
        g.setColour(colours::accent);
        g.strokePath(arc(start, start + sweep * juce::jlimit(0.004f, 1.0f, reduction / scaleDb)), strokeType);
    }
    if (gainReductionTarget.has_value())
    {
        const auto angle = start + sweep * juce::jlimit(0.0f, 1.0f, *gainReductionTarget / scaleDb);
        const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
        g.setColour(colours::text);
        g.drawLine({centre + direction * (radius - stroke), centre + direction * (radius + stroke)}, 2.0f);
    }
    g.setColour(reduction > 0.0f ? colours::accentNumber : colours::muted);
    g.setFont(ui::font(32.0f, Weight::bold));
    g.drawText(juce::String(reduction, 1), juce::Rectangle<float>(size, 36.0f).withCentre(centre.translated(0.0f, -4.0f)),
               juce::Justification::centred);
    g.setColour(colours::muted);
    g.setFont(ui::font(12.0f, Weight::demiBold));
    g.drawText("dB", juce::Rectangle<float>(size, 16.0f).withCentre(centre.translated(0.0f, 20.0f)),
               juce::Justification::centred);

    auto meterArea = area.withTrimmedTop(14.0f);
    if (meterArea.getHeight() < 90.0f)
        return;
    meterArea = meterArea.removeFromBottom(std::min(meterArea.getHeight(), 200.0f));
    const auto drawMeter = [&g](juce::Rectangle<float> column, const juce::String& label, float db, juce::Colour colour)
    {
        g.setColour(colours::muted);
        g.setFont(ui::font(12.0f, Weight::demiBold));
        g.drawText(label, column.removeFromBottom(16.0f), juce::Justification::centred);
        g.setColour(colours::text);
        g.setFont(ui::font(13.0f, Weight::bold));
        g.drawText(levelText(db), column.removeFromBottom(20.0f), juce::Justification::centred);
        column.removeFromBottom(6.0f);
        const auto bar = column.withSizeKeepingCentre(10.0f, column.getHeight());
        g.setColour(colours::raised);
        g.fillRoundedRectangle(bar, 5.0f);
        const auto fraction = juce::jlimit(0.0f, 1.0f, (db - meterFloorDb) / -meterFloorDb);
        g.setColour(colour);
        g.fillRoundedRectangle(bar.withTrimmedTop(bar.getHeight() * (1.0f - fraction)), 5.0f);
    };
    const auto half = meterArea.getWidth() * 0.5f;
    drawMeter(meterArea.removeFromLeft(half), "In", shownInputDb, juce::Colour(0xff3a4252));
    drawMeter(meterArea, "Out", shownOutputDb, colours::output);
}

bool ScopeComponent::overThreshold(juce::Point<float> position) const
{
    if (thresholdParameter == nullptr)
        return false;
    const auto parts = layout();
    if (!parts.plot.contains(position))
        return false;
    const auto centre = parts.waveform.getCentreY();
    const auto offset = thresholdOffset(parts);
    return handleBounds(parts).contains(position) || std::abs(position.y - (centre - offset)) < 6.0f
        || std::abs(position.y - (centre + offset)) < 6.0f;
}

void ScopeComponent::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(overThreshold(event.position) ? juce::MouseCursor::UpDownResizeCursor
                                                 : juce::MouseCursor::NormalCursor);
}

void ScopeComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!overThreshold(event.position))
        return;
    dragging = true;
    thresholdParameter->beginChangeGesture();
    mouseDrag(event);
}

void ScopeComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging)
        return;
    const auto parts = layout();
    const auto offset = std::abs(event.position.y - parts.waveform.getCentreY());
    const auto linear = offset / std::max(1.0f, parts.waveform.getHeight() * waveformScale);
    const auto range = thresholdParameter->getNormalisableRange();
    const auto db = juce::jlimit(range.start, range.end, juce::Decibels::gainToDecibels(linear, range.start));
    thresholdParameter->setValueNotifyingHost(thresholdParameter->convertTo0to1(db));
    repaint();
}

void ScopeComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging)
        return;
    dragging = false;
    thresholdParameter->endChangeGesture();
    repaint();
}
}
