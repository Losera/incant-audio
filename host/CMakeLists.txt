cmake_minimum_required(VERSION 3.22)
project(PluginForge VERSION 0.1.0)

# Set via: cmake -DJUCE_PATH=/path/to/JUCE
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE root")
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Set -DJUCE_PATH=...")
endif()
add_subdirectory(${JUCE_PATH} JUCE)

juce_add_plugin(PluginForgeHost
    PLUGIN_MANUFACTURER_CODE Pfge
    PLUGIN_CODE Pfh1
    FORMATS VST3 Standalone
    PRODUCT_NAME "PluginForge Host"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(PluginForgeHost PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(PluginForgeHost PRIVATE cxx_std_17)

# NEEDS_WEB_BROWSER/NEEDS_CURL FALSE above only control linking (see
# JUCEUtils.cmake's _juce_link_optional_libraries) -- juce_gui_extra.cpp's
# #include of <gtk/gtk.h>/<webkit2/webkit2.h>, and juce_core's curl symbol
# usage, are gated on the JUCE_WEB_BROWSER / JUCE_USE_CURL macros, which both
# default to 1 regardless of the NEEDS_* plugin options. This plugin has no
# WebView UI and talks to llm/generate.py via subprocess argv, not JUCE HTTP,
# so both are disabled here. (Arch also only ships webkit2gtk-4.1, not the
# webkit2gtk-4.0 JUCE's CMake looks for, so JUCE_WEB_BROWSER's default of 1
# would fail the build even with gtk3 installed.)
# JUCE_VST3_CAN_REPLACE_VST2 also defaults to 1 and pulls in a legacy VST2 SDK
# header (pluginterfaces/vst2.x/vstfxstore.h) that modern JUCE checkouts don't
# bundle (Steinberg no longer allows redistributing the VST2 SDK). We only
# build the VST3 format (see FORMATS above), so this is irrelevant here.
target_compile_definitions(PluginForgeHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# libfaust ships as a shared library on Arch Linux at /usr/lib/libfaust.so.
# find_library resolves the .so symlink to the versioned library at configure time.
find_library(LIBFAUST_LIB faust REQUIRED
    HINTS /usr/lib /usr/local/lib)
message(STATUS "Found libfaust: ${LIBFAUST_LIB}")

target_link_libraries(PluginForgeHost PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${LIBFAUST_LIB}
)

# ParamPoolTsanTest -- ThreadSanitizer stress harness for ParamPool's
# double-buffer scheme (host/tests/ParamPoolConcurrencyTest.cpp). A separate
# console-app target (not a plugin format) so -fsanitize=thread only applies
# here, not to the shipping plugin build above. PAIR draft -- see
# docs/collaboration_log.md 2026-07-18 entry.
juce_add_console_app(ParamPoolTsanTest
    PRODUCT_NAME "ParamPool TSan Test"
)

target_sources(ParamPoolTsanTest PRIVATE
    tests/ParamPoolConcurrencyTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(ParamPoolTsanTest PRIVATE cxx_std_17)

target_compile_definitions(ParamPoolTsanTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_options(ParamPoolTsanTest PRIVATE -fsanitize=thread -g -O1)
target_link_options(ParamPoolTsanTest PRIVATE -fsanitize=thread)

target_link_libraries(ParamPoolTsanTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# OutputGuardTest -- behavioural tests for the audio-output safety net
# (host/tests/OutputGuardTest.cpp). Separate console app so ASan/UBSan apply
# only here. Deliberately links NOTHING but juce_audio_basics and
# Source/OutputGuard.cpp: the guard must stay free of plugin/JIT dependencies so
# it can be reasoned about (and fuzzed) in isolation. ASan is worth the cost
# here specifically -- the guard indexes fixed-size per-channel state arrays
# against a host-supplied channel count.
juce_add_console_app(OutputGuardTest
    PRODUCT_NAME "OutputGuard Test"
)

target_sources(OutputGuardTest PRIVATE
    tests/OutputGuardTest.cpp
    Source/OutputGuard.cpp
)

target_compile_features(OutputGuardTest PRIVATE cxx_std_17)

target_compile_definitions(OutputGuardTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_options(OutputGuardTest PRIVATE -fsanitize=address,undefined -g -O1)
target_link_options(OutputGuardTest PRIVATE -fsanitize=address,undefined)

target_link_libraries(OutputGuardTest PRIVATE
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
)

# ParamMapTest -- slot<->zone conversion (host/Source/ParamMap.h). Header-only
# under test, so this compiles just the test TU; it links libfaust only because
# FaustEngine.h (where ParamInfo lives) includes the libfaust headers, not
# because any libfaust function is called.
juce_add_console_app(ParamMapTest
    PRODUCT_NAME "ParamMap Test"
)

target_sources(ParamMapTest PRIVATE
    tests/ParamMapTest.cpp
)

target_compile_features(ParamMapTest PRIVATE cxx_std_17)

target_compile_definitions(ParamMapTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_options(ParamMapTest PRIVATE -fsanitize=address,undefined -g -O1)
target_link_options(ParamMapTest PRIVATE -fsanitize=address,undefined)

target_link_libraries(ParamMapTest PRIVATE
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)
