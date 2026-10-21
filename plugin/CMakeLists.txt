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
    Source/ui/HoldPanel.cpp
    Source/ui/RoutingPanel.cpp)
set(GLIDE90_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# One source tree, two plugins (Source/Product.h): GLIDE90_PRODUCT picks which one a target is.
#   GLIDE90       GLIDE, REVERSE GATE and HOLD with the ROUTING (the plugin code GLIDE90 always had);
#   GLIDE90 LITE  GLIDE and REVERSE GATE, ordered by dragging, MIX on the gate.
function(glide90_add_product target product name code description categories)
    # GLIDE90 keeps its address, so LV2 projects still find it.
    set(uri "https://github.com/glide90/glide90")
    if(NOT target STREQUAL "Glide90")
        set(uri "${uri}#${target}")
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "Glide90"
        BUNDLE_ID "io.github.glide90.${target}"
        PLUGIN_MANUFACTURER_CODE Gl9d
        PLUGIN_CODE ${code}
        DESCRIPTION "${description}"
        FORMATS ${GLIDE90_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE   # pitch bend / mod wheel drive the bar; HOLD plays from MIDI notes
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${categories}
        AU_MAIN_TYPE kAudioUnitType_MusicEffect
        LV2URI "${uri}"
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${target} PRIVATE ${GLIDE90_PLUGIN_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC ${GLIDE90_JUCE_DEFINITIONS} GLIDE90_PRODUCT=${product})
    target_link_libraries(${target}
        PRIVATE
            glide90_dsp
            glide90_fonts
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    if(GLIDE90_LTO)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # GCC's link-time step on all cores, not one.
            target_link_options(${target} PUBLIC $<$<CONFIG:Release>:-flto=auto>)
        endif()
    endif()
endfunction()

# GLIDE90 keeps its target (and so its artefact folder).
set(GLIDE90_PRODUCTS "Full;Lite" CACHE STRING "Which plugins to build: Full (GLIDE90), Lite (GLIDE90 LITE)")
if("Full" IN_LIST GLIDE90_PRODUCTS)
    glide90_add_product(Glide90 0 "GLIDE90" G90s "Reverse gate, vibrato-bar glide and hold" "Fx;Reverb")
endif()
if("Lite" IN_LIST GLIDE90_PRODUCTS)
    glide90_add_product(Glide90Lite 1 "GLIDE90 LITE" G90l "Reverse gate and vibrato-bar glide" "Fx;Reverb")
endif()

if(GLIDE90_BUILD_TESTS)
    # Processor-level tests: the plugin's sources in a console app, since the plugin's shared-code
    # library does not export the JUCE module headers. GLIDE90 has the full suite, LITE its own.
    function(glide90_add_tests target product name test_source)
        juce_add_console_app(${target} PRODUCT_NAME "${target}")
        target_sources(${target} PRIVATE ${GLIDE90_PLUGIN_SOURCES} ${test_source})
        target_include_directories(${target} PRIVATE Source ${doctest_SOURCE_DIR})
        target_compile_definitions(${target} PRIVATE ${GLIDE90_JUCE_DEFINITIONS} GLIDE90_PRODUCT=${product}
            JucePlugin_Name="${name}" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${target} PRIVATE glide90_dsp glide90_fonts juce::juce_audio_utils
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endfunction()
    glide90_add_tests(glide90_plugin_tests 0 "GLIDE90" ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin.cpp)
    add_test(NAME plugin COMMAND glide90_plugin_tests)
    glide90_add_tests(glide90_lite_tests 1 "GLIDE90 LITE" ${CMAKE_SOURCE_DIR}/tests/plugin/test_lite.cpp)
    add_test(NAME lite COMMAND glide90_lite_tests)
endif()
