cmake_minimum_required(VERSION 3.22)

project(Speckle VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DSPECKLE_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(SPECKLE_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(SPECKLE_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(SPECKLE_BUILD_TESTS "Build the offline render test" ON)

if(SPECKLE_BUILD_PLUGIN)
    if(SPECKLE_JUCE_PATH)
        add_subdirectory(${SPECKLE_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(SPECKLE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SPECKLE_FORMATS AU)
    endif()

    juce_add_plugin(Speckle
        COMPANY_NAME "Kevin Kaye"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.speckle"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Spek
        FORMATS ${SPECKLE_FORMATS}
        PRODUCT_NAME "Speckle"
        DESCRIPTION "Granular looper: micro loops, clouds, glitches and delays"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Delay Reverb)

    juce_add_binary_data(SpeckleData SOURCES Resources/RobotoCondensed-Bold.ttf)

    target_sources(Speckle PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp
        Source/Keeper.cpp)

    target_include_directories(Speckle PRIVATE Source Source/engine)

    target_compile_definitions(Speckle PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Speckle
        PRIVATE
            SpeckleData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): renders test audio through every effect
# ---------------------------------------------------------------------------
if(SPECKLE_BUILD_TESTS)
    add_executable(speckle_render_test Tests/RenderTest.cpp)
    target_include_directories(speckle_render_test PRIVATE Source/engine)

    # Footswitch test: drives the real plugin with footswitch MIDI (needs JUCE)
    if(SPECKLE_BUILD_PLUGIN)
        juce_add_console_app(speckle_pedal_test PRODUCT_NAME "speckle_pedal_test")
        target_sources(speckle_pedal_test PRIVATE
            Tests/PedalTest.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/PedalControl.cpp
            Source/Keeper.cpp)
        target_include_directories(speckle_pedal_test PRIVATE Source Source/engine)
        target_compile_definitions(speckle_pedal_test PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            JucePlugin_VersionString="${PROJECT_VERSION}")
        target_link_libraries(speckle_pedal_test PRIVATE
            SpeckleData
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
    endif()
endif()
