// Runs the LetsComp assistant on audio files, exactly like the plug-in does
// after listening: the first ten seconds from the first sample above -60 dBFS.
//
//   LetsCompAssist [--source vocal|drums|bass|bus] [--style gentle|control|punch|upfront|glue]
//                   [--amount light|medium|heavy] file...
//
// Without --style all four styles are printed. Upfront is the vocal name for punch.

#include "assistant/CaptureBuffer.h"
#include "assistant/Recommender.h"
#include "assistant/SignalAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iomanip>
#include <iostream>

namespace
{
using namespace letscomp::assistant;

std::optional<CaptureData> load(const juce::File& file, juce::AudioFormatManager& formats)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return std::nullopt;
    const auto channels = static_cast<int>(std::min<unsigned int>(2, reader->numChannels));
    const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max()));
    juce::AudioBuffer<float> audio(channels, length);
    reader->read(&audio, 0, length, 0, true, channels > 1);

    CaptureBuffer capture;
    capture.prepare(reader->sampleRate, channels);
    capture.arm();
    constexpr int blockSize = 512;
    for (int position = 0; position < length; position += blockSize)
    {
        const auto count = std::min(blockSize, length - position);
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), channels, position, count);
        capture.push(block);
        if (capture.currentState() == CaptureBuffer::State::full)
            break;
    }
    return capture.copy();
}

void print(const SignalAnalysis& analysis)
{
    std::cout << std::fixed << std::setprecision(1)
              << "  heard " << analysis.durationSeconds << " s, playing " << analysis.activeSeconds << " s\n"
              << "  loud " << analysis.loudLevelDb << " dBFS, quiet " << analysis.quietLevelDb
              << " dBFS, peak " << analysis.peakLevelDb << " dBFS, crest " << analysis.crestDb << " dB\n"
              << "  onsets " << analysis.onsetsPerSecond << "/s, spacing "
              << (analysis.medianOnsetIntervalMs.has_value() ? std::to_string(static_cast<int>(*analysis.medianOnsetIntervalMs)) + " ms"
                                                              : std::string("none"))
              << ", below 150 Hz " << analysis.lowFrequencyShare * 100.0f << " %, loudness "
              << (analysis.loudnessLufs.has_value() ? std::to_string(*analysis.loudnessLufs).substr(0, 6) + " LUFS"
                                                     : std::string("n/a"))
              << "\n";
}
}

int main(int argc, char* argv[])
{
    std::optional<SourceType> source = SourceType::vocal;
    std::optional<Style> style;
    std::optional<Amount> amount = Amount::medium;
    std::vector<juce::File> files;
    auto valid = true;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--source" || argument == "--style" || argument == "--amount")
        {
            if (index + 1 >= argc)
            {
                valid = false;
                break;
            }
            const std::string value = argv[++index];
            if (argument == "--source")
                source = sourceFromIdentifier(value);
            else if (argument == "--amount")
                amount = amountFromIdentifier(value);
            else if (!(style = styleFromIdentifier(value == "upfront" ? "punch" : value)).has_value())
                valid = false;
        }
        else
        {
            files.emplace_back(juce::File::getCurrentWorkingDirectory().getChildFile(argument));
        }
    }
    if (!valid || !source.has_value() || !amount.has_value() || files.empty())
    {
        std::cerr << "usage: LetsCompAssist [--source vocal|drums|bass|bus] "
                     "[--style gentle|control|punch|upfront|glue] [--amount light|medium|heavy] file...\n";
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto status = 0;
    for (const auto& file : files)
    {
        std::cout << file.getFileName() << "\n";
        const auto capture = load(file, formats);
        if (!capture.has_value())
        {
            std::cout << "  cannot read this file\n";
            status = 1;
            continue;
        }
        const auto analysis = analyseSignal(*capture);
        print(analysis);
        for (const auto candidate : allStyles)
        {
            if (style.has_value() && *style != candidate)
                continue;
            const auto result = recommend(*capture, analysis, *source, candidate, *amount);
            if (!result.succeeded)
            {
                std::cout << "  " << result.message << "\n";
                status = 1;
                break;
            }
            const auto& settings = result.recommendation->settings;
            std::cout << std::setprecision(1) << "  " << displayName(*source, candidate) << ", "
                      << displayName(*amount) << ": " << result.recommendation->summary << ". Threshold "
                      << settings.thresholdDb << " dB, " << settings.ratio << ":1, attack " << settings.attackMs
                      << " ms, release " << settings.releaseMs << " ms, makeup " << std::showpos
                      << settings.makeupDb << std::noshowpos << " dB\n";
            if (style.has_value())
                for (const auto& note : result.recommendation->notes)
                    std::cout << "    " << note.parameter << ": " << note.text << "\n";
        }
    }
    return status;
}
