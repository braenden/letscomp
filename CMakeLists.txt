cmake_minimum_required(VERSION 3.25)

project(LetsComp VERSION 0.5.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

option(LETSCOMP_UNIVERSAL "Build a universal arm64/x86_64 macOS binary" OFF)
option(LETSCOMP_COPY_AFTER_BUILD "Copy plug-ins into the user's plug-in folders" OFF)
option(LETSCOMP_BUILD_TESTS "Build DSP unit tests" ON)
set(LETSCOMP_SANITIZER "none" CACHE STRING "Test sanitizer: none, undefined, address, or thread")
set_property(CACHE LETSCOMP_SANITIZER PROPERTY STRINGS none undefined address thread)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS version")
if(APPLE AND LETSCOMP_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 91ad83ae34a81e0833b1a2b0866f54846370ae53
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG c1a5ad025f95d01e03267857fa8276ebeed16500
    GIT_SHALLOW FALSE
    GIT_SUBMODULES clap-libs/clap clap-libs/clap-helpers
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(clap_juce_extensions)

# Audio Units exist only on macOS.
set(LETSCOMP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LETSCOMP_FORMATS AU)
endif()

juce_add_plugin(LetsComp
    COMPANY_NAME "Kevin Brenden"
    COMPANY_COPYRIGHT "Copyright (C) 2026 Kevin Brenden"
    BUNDLE_ID io.github.braenden.letscomp
    PLUGIN_MANUFACTURER_CODE Brnd
    PLUGIN_CODE LtCp
    FORMATS ${LETSCOMP_FORMATS}
    PRODUCT_NAME "LetsComp"
    DESCRIPTION "Visual feed-forward compressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${LETSCOMP_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(LetsComp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/CompressorEngine.cpp
    Source/dsp/CompressorEngine.h
    Source/dsp/EnvelopeFollower.h
    Source/dsp/GainComputer.h
    Source/dsp/LookaheadDelay.h
    Source/dsp/PeakDetector.h
    Source/assistant/AssistantTypes.h
    Source/assistant/CaptureBuffer.h
    Source/assistant/SignalAnalyzer.cpp
    Source/assistant/SignalAnalyzer.h
    Source/assistant/Recommender.cpp
    Source/assistant/Recommender.h
    Source/assistant/AssistantController.cpp
    Source/assistant/AssistantController.h
    Source/parameters/Parameters.cpp
    Source/parameters/Parameters.h
    Source/parameters/ParameterDomains.h
    Source/visualization/ScopeData.h
    Source/visualization/HistoryBuffer.h
    Source/visualization/ScopeComponent.cpp
    Source/visualization/ScopeComponent.h
    Source/ui/Theme.h
    Source/ui/RotaryControl.cpp
    Source/ui/RotaryControl.h
    Source/ui/Widgets.cpp
    Source/ui/Widgets.h
    Source/ui/Header.cpp
    Source/ui/Header.h
    Source/ui/AssistPanel.cpp
    Source/ui/AssistPanel.h
    Source/ui/About.cpp
    Source/ui/About.h
    Source/ui/Preferences.h
    Source/ui/ControlsPanel.cpp
    Source/ui/ControlsPanel.h)

target_compile_definitions(LetsComp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LetsComp PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    target_compile_options(LetsComp PRIVATE -Werror)
endif()

clap_juce_extensions_plugin(TARGET LetsComp
    CLAP_ID "io.github.braenden.letscomp"
    CLAP_FEATURES audio-effect compressor stereo mono)

if(LETSCOMP_BUILD_TESTS)
    enable_testing()
    add_executable(LetsCompTests
        Tests/CompressorTests.cpp
        Source/dsp/CompressorEngine.cpp)
    target_include_directories(LetsCompTests PRIVATE Source)
    target_link_libraries(LetsCompTests PRIVATE
        juce::juce_audio_basics
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
        target_compile_options(LetsCompTests PRIVATE -Werror)
    endif()
    if(LETSCOMP_SANITIZER STREQUAL "undefined")
        target_compile_definitions(LetsCompTests PRIVATE LETSCOMP_SANITIZED_TESTS=1)
        target_compile_options(LetsCompTests PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(LetsCompTests PRIVATE -fsanitize=undefined)
    elseif(LETSCOMP_SANITIZER STREQUAL "address")
        target_compile_definitions(LetsCompTests PRIVATE LETSCOMP_SANITIZED_TESTS=1)
        target_compile_options(LetsCompTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(LetsCompTests PRIVATE -fsanitize=address,undefined)
    elseif(LETSCOMP_SANITIZER STREQUAL "thread")
        target_compile_definitions(LetsCompTests PRIVATE LETSCOMP_SANITIZED_TESTS=1)
        target_compile_options(LetsCompTests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(LetsCompTests PRIVATE -fsanitize=thread)
    elseif(NOT LETSCOMP_SANITIZER STREQUAL "none")
        message(FATAL_ERROR "Unsupported LETSCOMP_SANITIZER=${LETSCOMP_SANITIZER}")
    endif()
    add_test(NAME LetsCompDSP COMMAND LetsCompTests)
    set_tests_properties(LetsCompDSP PROPERTIES TIMEOUT 60)

    set(LETSCOMP_ASSISTANT_CORE
        Source/dsp/CompressorEngine.cpp
        Source/assistant/SignalAnalyzer.cpp
        Source/assistant/Recommender.cpp)

    add_executable(LetsCompAssistantTests Tests/AssistantTests.cpp ${LETSCOMP_ASSISTANT_CORE})
    target_include_directories(LetsCompAssistantTests PRIVATE Source)
    target_link_libraries(LetsCompAssistantTests PRIVATE
        juce::juce_audio_basics
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
        target_compile_options(LetsCompAssistantTests PRIVATE -Werror)
    endif()
    add_test(NAME LetsCompAssistant COMMAND LetsCompAssistantTests)
    set_tests_properties(LetsCompAssistant PROPERTIES TIMEOUT 120)

    # Offline runner for checking recommendations on real stems.
    add_executable(LetsCompAssist Tools/AssistantCli.cpp ${LETSCOMP_ASSISTANT_CORE})
    target_include_directories(LetsCompAssist PRIVATE Source)
    target_link_libraries(LetsCompAssist PRIVATE
        juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
        target_compile_options(LetsCompAssist PRIVATE -Werror)
    endif()

    add_executable(LetsCompProcessorTests Tests/ProcessorTests.cpp)
    target_include_directories(LetsCompProcessorTests PRIVATE Source)
    target_link_libraries(LetsCompProcessorTests PRIVATE
        LetsComp
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
        target_compile_options(LetsCompProcessorTests PRIVATE -Werror)
    endif()
    add_test(NAME LetsCompProcessorIntegration COMMAND LetsCompProcessorTests)
    set_tests_properties(LetsCompProcessorIntegration PROPERTIES TIMEOUT 30)
endif()
