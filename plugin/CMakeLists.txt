cmake_minimum_required(VERSION 3.22)
project(Tx81zEmulator VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Reuse the JUCE checkout already fetched for the D-110 project if present -
# avoids re-downloading the whole framework; falls back to a fresh fetch
# otherwise so this project stays buildable on its own.
set(_cached_juce "C:/Users/bd260/Projects/D110-VST/plugin/build/_deps/juce-src")
if(EXISTS "${_cached_juce}/CMakeLists.txt")
    FetchContent_Declare(JUCE SOURCE_DIR "${_cached_juce}")
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
    )
endif()
FetchContent_MakeAvailable(JUCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../engine" "${CMAKE_CURRENT_BINARY_DIR}/engine_build")

juce_add_plugin(Tx81zEmulator
    COMPANY_NAME "DIY"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "C:/Program Files/Common Files/VST3"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Tx8z
    FORMATS VST3
    PRODUCT_NAME "TX81Z Emulator"
)

target_sources(Tx81zEmulator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(Tx81zEmulator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Tx81zEmulator PRIVATE
    tx81z_system
    juce::juce_audio_utils
    juce::juce_audio_processors
)

target_link_libraries(Tx81zEmulator PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
