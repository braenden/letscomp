#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace letscomp::ui
{
// Per-user settings that belong to the person, not to one project, kept in
// ~/Library/Application Support/LetsComp/LetsComp.settings on macOS.
inline juce::PropertiesFile::Options preferenceOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "LetsComp";
    options.filenameSuffix = ".settings";
    options.folderName = "LetsComp";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

inline bool tipsEnabled()
{
    juce::PropertiesFile file(preferenceOptions());
    return file.getBoolValue("tips", true);
}

inline void setTipsEnabled(bool enabled)
{
    juce::PropertiesFile file(preferenceOptions());
    file.setValue("tips", enabled);
    file.saveIfNeeded();
}
}
