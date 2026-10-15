cmake_minimum_required(VERSION 3.22)
project(QChop VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headless Linux: JUCE's X11 extras need dev headers we don't have.
# Disable them globally so both juceaide and the plugin build clean.
add_compile_definitions(
    JUCE_USE_XRANDR=0
    JUCE_USE_XINERAMA=0
    JUCE_USE_XCURSOR=0
)

# Pull JUCE automatically — first build downloads it (~150MB), then it's cached.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(QChop
    COMPANY_NAME "Headroom Audio"
    PLUGIN_NAME "QChop"
    DESCRIPTION "Sample slicer: auto-chop loops by transient, trigger 16 pads"
    PLUGIN_MANUFACTURER_CODE Hdrm
    PLUGIN_CODE Qchp
    BUNDLE_ID com.headroomaudio.qchop
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "QChop"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH FALSE
)

juce_add_binary_data(QChopAssets SOURCES Source/Assets/headroom-logo.png)

target_sources(QChop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(QChop PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(QChop PRIVATE
    QChopAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
