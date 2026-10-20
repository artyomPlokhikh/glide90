FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

set(GLIDE90_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GLIDE90_FORMATS AU)
elseif(UNIX)
    list(APPEND GLIDE90_FORMATS LV2)
endif()

juce_add_plugin(Glide90
    PRODUCT_NAME "GLIDE90"
    COMPANY_NAME "Glide90"
    BUNDLE_ID "io.github.glide90.Glide90"
    PLUGIN_MANUFACTURER_CODE Gl9d
    PLUGIN_CODE G90s
    DESCRIPTION "Reverse gate and vibrato-bar glide"
    FORMATS ${GLIDE90_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE   # pitch bend / mod wheel can drive the Sway bar
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    LV2URI "https://github.com/glide90/glide90"
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Fonts (SIL Open Font License, licences next to them): Barlow and Barlow Condensed for legends and
# values, Courier Prime for the print look.
juce_add_binary_data(glide90_fonts
    NAMESPACE GlideFonts
    HEADER_NAME GlideFonts.h
    SOURCES
        Resources/fonts/BarlowCondensed-Medium.ttf
        Resources/fonts/BarlowCondensed-SemiBold.ttf
        Resources/fonts/Barlow-Light.ttf
        Resources/fonts/Barlow-Regular.ttf
        Resources/fonts/Barlow-Medium.ttf
        Resources/fonts/CourierPrime-Regular.ttf
        Resources/fonts/CourierPrime-Bold.ttf)

set(GLIDE90_PLUGIN_SOURCES
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/PanelLookAndFeel.cpp
    Source/ui/Controls.cpp
    Source/ui/HeaderBar.cpp
    Source/ui/ModulePanel.cpp
    Source/ui/Dialog.cpp
    Source/ui/GatePanel.cpp
    Source/ui/ShapeDisplay.cpp
    Source/ui/GlidePanel.cpp
    Source/ui/HoldPanel.cpp)
set(GLIDE90_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(Glide90 PRIVATE ${GLIDE90_PLUGIN_SOURCES})
target_include_directories(Glide90 PRIVATE Source)
target_compile_definitions(Glide90 PUBLIC ${GLIDE90_JUCE_DEFINITIONS})

target_link_libraries(Glide90
    PRIVATE
        glide90_dsp
        glide90_fonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(GLIDE90_LTO)
    target_link_libraries(Glide90 PUBLIC juce::juce_recommended_lto_flags)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC's link-time step on all cores, not one.
        target_link_options(Glide90 PUBLIC $<$<CONFIG:Release>:-flto=auto>)
    endif()
endif()

if(GLIDE90_BUILD_TESTS)
    # Processor-level tests (memories, state, latency): the plugin's sources in a console app, since
    # the plugin's shared-code library does not export the JUCE module headers.
    juce_add_console_app(glide90_plugin_tests PRODUCT_NAME "glide90_plugin_tests")
    target_sources(glide90_plugin_tests PRIVATE ${GLIDE90_PLUGIN_SOURCES} ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin.cpp)
    target_include_directories(glide90_plugin_tests PRIVATE Source ${doctest_SOURCE_DIR})
    target_compile_definitions(glide90_plugin_tests PRIVATE ${GLIDE90_JUCE_DEFINITIONS}
        JucePlugin_Name="GLIDE90" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(glide90_plugin_tests PRIVATE glide90_dsp glide90_fonts juce::juce_audio_utils
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND glide90_plugin_tests)
endif()
