cmake_minimum_required(VERSION 3.22)
project(PluginForge VERSION 0.1.0)

# Set via: cmake -DJUCE_PATH=/path/to/JUCE
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE root")
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Set -DJUCE_PATH=...")
endif()
add_subdirectory(${JUCE_PATH} JUCE)

# The shared source set behind every target in this file. Named once so the two
# plugin targets below cannot drift apart -- they are the SAME plugin compiled
# with different JUCE plugin traits (effect vs instrument), not two codebases.
# The test harnesses still list their sources explicitly; folding them in here
# too is a worthwhile cleanup, but it would touch ten targets at once and is not
# this change's job.
set(PF_CORE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

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

target_sources(PluginForgeHost PRIVATE ${PF_CORE_SOURCES})

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

# ── PluginForgeSynth — the same plugin, declared as an instrument ─────────────
# A generated synth is 0-in/N-out and needs note input, which an Fx-category
# plugin cannot get: the host has no reason to route MIDI to it, and this
# plugin's input bus is REQUIRED, so a DAW will not even instantiate it on an
# instrument track. Those are JUCE *plugin traits*, fixed at build time and
# baked into the VST3 metadata -- they cannot be switched at runtime, which is
# why this is a second target rather than a flag.
#
# Two targets rather than one instrument-that-also-takes-audio: making
# PluginForgeHost an instrument would move it in every DAW's browser and change
# what already-saved FX sessions load. The cost is build time -- JUCE recompiles
# its modules per target, and this tree already has twelve.
#
# Everything the two share lives in PF_CORE_SOURCES and in the source itself;
# the ONLY differences are the four traits below. PluginProcessor keys its bus
# layout and acceptsMidi() off JucePlugin_IsSynth, which JUCE defines from
# IS_SYNTH -- so there is no PluginForge-specific build flag to keep in sync.
#
# Standalone is built for the same reason the Fx target builds it: the VST3 is
# never installed to a scan path (COPY_PLUGIN_AFTER_BUILD FALSE), so the
# Standalone is the only way to play a generated instrument from a MIDI
# keyboard without a DAW -- which is the listening pass this whole effort is for.
juce_add_plugin(PluginForgeSynth
    PLUGIN_MANUFACTURER_CODE Pfge
    PLUGIN_CODE Pfs1               # MUST differ from the Fx target's Pfh1 --
                                   # hosts identify a plugin by this code, and a
                                   # collision makes one shadow the other.
    FORMATS VST3 Standalone
    PRODUCT_NAME "PluginForge Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(PluginForgeSynth PRIVATE ${PF_CORE_SOURCES})

target_compile_features(PluginForgeSynth PRIVATE cxx_std_17)

# Same rationale as the Fx target's block above.
target_compile_definitions(PluginForgeSynth PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PluginForgeSynth PRIVATE
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
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
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

# ---------------------------------------------------------------------------
# ParamIdentityTest -- the parameter-identity derivation rule
# (host/Source/ParamIdentity.h, host/tests/ParamIdentityTest.cpp).
#
# Deliberately the CHEAPEST target in this file: a plain executable with NO JUCE,
# NO libfaust and no plugin sources. ParamIdentity.h is pure string arithmetic
# with no dependency on either, and the rule it encodes is a one-way door -- once
# these ids are written into a saved user project, changing the derivation
# orphans every restored value. A rule that consequential belongs at the rung
# that costs two seconds to run, not behind a JIT and a message loop.
#
# ASan/UBSan for the same reason the other header-only tests carry them: the code
# under test indexes and concatenates strings from author-supplied labels.
# No -fno-sanitize=vptr here, unlike the harnesses that compile FaustEngine.cpp
# -- nothing in this TU touches libfaust's -fno-rtti types.
# ---------------------------------------------------------------------------
add_executable(ParamIdentityTest tests/ParamIdentityTest.cpp)
target_compile_features(ParamIdentityTest PRIVATE cxx_std_17)
target_compile_options(ParamIdentityTest PRIVATE -fsanitize=address,undefined -g -O1)
target_link_options(ParamIdentityTest PRIVATE -fsanitize=address,undefined)

# ---------------------------------------------------------------------------
# NoteRingTest / NoteRingTsanTest -- the message->audio note queue
# (host/Source/NoteRing.h, host/tests/NoteRingTest.cpp).
#
# TWO TARGETS FROM ONE SOURCE, deliberately. NoteRing.h is a release/acquire
# argument, and ASan and TSan cannot be combined in one binary, so a single
# target would have to pick which half of the header goes unchecked:
#
#   NoteRingTest      ASan/UBSan -- bounds and index arithmetic over the slot
#                     array, which is what the mask/wrap logic can get wrong.
#   NoteRingTsanTest  TSan -- the actual memory-ordering claim. The concurrency
#                     section runs a real producer thread against a real
#                     consumer thread; without this target that section proves
#                     only that the code does not crash, which is not the claim
#                     the header makes.
#
# No JUCE, no libfaust, no plugin sources: the queue is index arithmetic over a
# fixed array, and the same reasoning that factored out ParamGridLayout.h and
# ParamIdentity.h applies -- it must be exercisable without a compiled DSP, a
# message thread or a live editor.
# ---------------------------------------------------------------------------
add_executable(NoteRingTest tests/NoteRingTest.cpp)
target_compile_features(NoteRingTest PRIVATE cxx_std_17)
target_compile_options(NoteRingTest PRIVATE -fsanitize=address,undefined -g -O1)
target_link_options(NoteRingTest PRIVATE -fsanitize=address,undefined)
find_package(Threads REQUIRED)
target_link_libraries(NoteRingTest PRIVATE Threads::Threads)

add_executable(NoteRingTsanTest tests/NoteRingTest.cpp)
target_compile_features(NoteRingTsanTest PRIVATE cxx_std_17)
target_compile_options(NoteRingTsanTest PRIVATE -fsanitize=thread -g -O1)
target_link_options(NoteRingTsanTest PRIVATE -fsanitize=thread)
target_link_libraries(NoteRingTsanTest PRIVATE Threads::Threads)

# StatePersistenceTest -- round-trips the P11 persisted-state blob through a real
# PluginForgeProcessor (host/tests/StatePersistenceTest.cpp): getStateInformation ->
# MemoryBlock -> setStateInformation on a fresh processor, asserting macro values,
# Faust source, and prompt survive. Constructs the full processor, so it mirrors
# ParamPoolTsanTest's source/link set (incl. PluginEditor.cpp, pulled in by
# createEditor()); ASan/UBSan like the other behavioural tests. Links libfaust
# because loadFaustCode() queues a real compile (not awaited; the destructor joins).
juce_add_console_app(StatePersistenceTest
    PRODUCT_NAME "State Persistence Test"
)

target_sources(StatePersistenceTest PRIVATE
    tests/StatePersistenceTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(StatePersistenceTest PRIVATE cxx_std_17)

target_compile_definitions(StatePersistenceTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# -fno-sanitize=vptr: UBSan's vptr check needs RTTI typeinfo for the polymorphic
# llvm_dsp / llvm_dsp_factory types, but libfaust.so is built -fno-rtti and does not
# export it (link fails with "undefined reference to typeinfo for llvm_dsp"). The
# header-only ASan tests dodge this only because they never compile FaustEngine.cpp.
# Everything else in UBSan is kept.
target_compile_options(StatePersistenceTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(StatePersistenceTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(StatePersistenceTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# ValidationGateTest -- the post-compile validation gate (Brief F), which runs
# inside FaustEngine::runCompile (Source/FaustEngine.cpp's validatePatch())
# before the atomic DSP swap. Same source/link set and same reasoning as
# StatePersistenceTest immediately above: constructing PluginForgeProcessor
# reaches createEditor(), and loadFaustCode() queues a real JIT compile through
# the actual compile path -- there is no way to exercise the gate without it.
juce_add_console_app(ValidationGateTest
    PRODUCT_NAME "Validation Gate Test"
)

target_sources(ValidationGateTest PRIVATE
    tests/ValidationGateTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(ValidationGateTest PRIVATE cxx_std_17)

target_compile_definitions(ValidationGateTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# -fno-sanitize=vptr: see the StatePersistenceTest block above -- libfaust.so is
# built -fno-rtti and does not export typeinfo for llvm_dsp.
target_compile_options(ValidationGateTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(ValidationGateTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(ValidationGateTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# PromptPanelThreadingTest -- the PF-006 regression harness
# (host/tests/PromptPanelThreadingTest.cpp). Exercises the owned+joined generate
# worker: teardown with a subprocess in flight, rapid-fire supersede, and the
# tight submit-then-destroy race. Uses /bin/sh + a generated shell script as a
# fake generate.py (via PLUGINFORGE_PYTHON / PLUGINFORGE_LLM_SCRIPT), so it needs
# no Python, no API key and no network.
#
# Same source/link set as StatePersistenceTest: constructing PluginForgeProcessor
# reaches createEditor(), which pulls in the whole panel set. ASan/UBSan for the
# same reason this test exists -- a use-after-free is the thing being tested for,
# and it must fail loudly rather than pass by luck.
juce_add_console_app(PromptPanelThreadingTest
    PRODUCT_NAME "Prompt Panel Threading Test"
)

target_sources(PromptPanelThreadingTest PRIVATE
    tests/PromptPanelThreadingTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(PromptPanelThreadingTest PRIVATE cxx_std_17)

target_compile_definitions(PromptPanelThreadingTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Needed for MessageManager::runDispatchLoopUntil, which is declared inside
    # `#if JUCE_MODAL_LOOPS_PERMITTED` (juce_MessageManager.h:82-88). The test has
    # to pump the message loop, because the worker delivers every UI update via
    # MessageManager::callAsync and those lambdas are part of what is being
    # tested. Scoped to this target ONLY -- the shipping plugin must never enable
    # modal loops, which is why the plugin targets above do not.
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# -fno-sanitize=vptr: see the StatePersistenceTest block above -- libfaust.so is
# built -fno-rtti and does not export typeinfo for llvm_dsp.
target_compile_options(PromptPanelThreadingTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(PromptPanelThreadingTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(PromptPanelThreadingTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# OfflineRenderTest -- the OBJECTIVE half of the P6 listening battery
# (host/tests/OfflineRenderTest.cpp). Renders a corpus of hand-written Faust
# through the REAL plugin path (JIT, swap protocol, ParamPool denormalisation,
# OutputGuard) and asserts finite, non-silent, bounded, DC-free output plus the
# published parameter count. Includes pathological negative controls (NaN, DC,
# runaway gain) so the safety assertions can be shown to fail, and the PF-018
# sample-rate-change spec assertion.
#
# Same source/link set as StatePersistenceTest. ASan/UBSan: this test exists to
# catch bad audio, and memory errors under it must be loud.
juce_add_console_app(OfflineRenderTest
    PRODUCT_NAME "Offline Render Test"
)

target_sources(OfflineRenderTest PRIVATE
    tests/OfflineRenderTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(OfflineRenderTest PRIVATE cxx_std_17)

target_compile_definitions(OfflineRenderTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# -fno-sanitize=vptr: see the StatePersistenceTest block -- libfaust.so is built
# -fno-rtti and does not export typeinfo for llvm_dsp.
target_compile_options(OfflineRenderTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(OfflineRenderTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(OfflineRenderTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# OfflineSynthRenderTest -- the SAME harness, compiled as the instrument target.
#
# WHY THIS EXISTS. PF_IS_SYNTH is derived from JucePlugin_IsSynth, which JUCE
# defines only for plugin targets. Every console-app harness therefore compiles
# with PF_IS_SYNTH == 0, so until this target existed the instrument build's
# trait surface -- the optional input bus, acceptsMidi(), the release tail,
# isBusesLayoutSupported -- was exercised by NOTHING. PluginForgeSynth compiled
# and shipped and no assertion had ever run against it. That is the same defect
# check.sh's header names: believing a control runs when it does not.
#
# The #ifndef guard in PluginProcessor.h was written to be overridable from the
# command line for exactly this. -DPF_IS_SYNTH=1 is the whole difference.
#
# NO SANITIZERS HERE, deliberately. This target exists for the build-time traits;
# the audio path it shares with OfflineRenderTest is byte-identical source and is
# already covered under ASan/UBSan there. Building a second sanitised copy would
# roughly double the slowest harness in the tree to re-check code that is already
# checked. If this target ever grows assertions the Fx one does not have, revisit.
juce_add_console_app(OfflineSynthRenderTest
    PRODUCT_NAME "Offline Synth Render Test"
)

target_sources(OfflineSynthRenderTest PRIVATE
    tests/OfflineRenderTest.cpp
    ${PF_CORE_SOURCES}
)

target_compile_features(OfflineSynthRenderTest PRIVATE cxx_std_17)

target_compile_definitions(OfflineSynthRenderTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    PF_IS_SYNTH=1
)

target_link_libraries(OfflineSynthRenderTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# EditorSessionTest -- a simulated human session against the real editor
# (host/tests/EditorSessionTest.cpp). Constructs PluginForgeProcessor AND
# PluginForgeEditor and drives them through the entry points a person's clicks
# and keystrokes would reach, then asserts what the screen says: status text,
# control count, widget kind, window height, restored values. Ten scenarios.
#
# It fills a hole rather than adding depth: nothing had ever constructed the
# editor, so ParamGridPanel, the shell's dynamic window sizing, and the
# output-guard mute edge-detect had zero coverage between them, and PF-005's
# widget-kind promise had been "not confirmed by eye/runtime" since 2026-07-23.
#
# Needs a display, for the same reason PromptPanelThreadingTest does: it
# constructs juce::Components and pumps the message loop. Snapshots go through
# Component::createComponentSnapshot (software renderer, no peer), so the PNGs
# themselves do not need a compositor -- but the message loop does.
#
# Same source/link set and the same sanitizers as the two harnesses above.
juce_add_console_app(EditorSessionTest
    PRODUCT_NAME "Editor Session Test"
)

target_sources(EditorSessionTest PRIVATE
    tests/EditorSessionTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(EditorSessionTest PRIVATE cxx_std_17)

target_compile_definitions(EditorSessionTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # runDispatchLoopUntil lives inside `#if JUCE_MODAL_LOOPS_PERMITTED`
    # (juce_MessageManager.h:82-88). See PromptPanelThreadingTest above for why
    # this is scoped to test targets and never to the shipping plugin.
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# -fno-sanitize=vptr: see the StatePersistenceTest block -- libfaust.so is built
# -fno-rtti and does not export typeinfo for llvm_dsp.
target_compile_options(EditorSessionTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(EditorSessionTest PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(EditorSessionTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)

# ---------------------------------------------------------------------------
# pf_cpu_shim -- LD_PRELOAD interposer for llvm::sys::getHostCPUName().
#
# CI-ONLY, and never linked into anything: it is loaded via LD_PRELOAD by the
# workflow's JIT-running harness steps so libfaust's LLVM does not emit AVX-512
# on runners that name the ISA but cannot execute it (PF-036, docs/BUGS.md).
#
# Built as a bare shared library with no JUCE and no libfaust: it defines one
# mangled LLVM symbol and a presence marker, and must NOT pull libLLVM in itself
# or it would resolve the symbol it is trying to interpose.
#
# -fno-rtti/-fno-exceptions keep it minimal; it runs inside every process it is
# preloaded into, including ones under ASan.
# ---------------------------------------------------------------------------
add_library(pf_cpu_shim SHARED tools/pf_cpu_shim.cpp)
target_compile_features(pf_cpu_shim PRIVATE cxx_std_17)
target_compile_options(pf_cpu_shim PRIVATE -fno-rtti -fno-exceptions -O1)
set_target_properties(pf_cpu_shim PROPERTIES
    OUTPUT_NAME pf_cpu_shim
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# JitTargetTest -- the PF-036 regression (host/tests/JitTargetTest.cpp).
#
# Asserts that pf_cpu_shim actually changes the instruction set libfaust's JIT
# emits, by disassembling the JIT'd pages. Deliberately does NOT link JUCE: it
# never constructs a processor, and the property under test is entirely between
# libfaust, LLVM and the CPU.
#
# NO SANITIZERS, unlike the other harnesses. This test reads /proc/self/maps and
# copies raw bytes out of executable mappings it does not own; ASan's own
# allocator and shadow mappings change which regions appear and would make the
# "new executable mapping" diff report ASan's runtime rather than the JIT.
#
# Needs libdl for the dlsym() presence check on the preloaded shim.
# ---------------------------------------------------------------------------
add_executable(JitTargetTest tests/JitTargetTest.cpp)
target_compile_features(JitTargetTest PRIVATE cxx_std_17)
target_compile_options(JitTargetTest PRIVATE -fno-rtti -g -O1)
target_link_libraries(JitTargetTest PRIVATE ${LIBFAUST_LIB} ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------
# UiDesignGallery -- the design instrument for docs/ui_design_plan.md §3
# (host/tests/UiDesignGallery.cpp). Renders the real editor against the fixture
# matrix in tests/ui_fixtures/*.dsp and writes artifacts/ui_gallery/{*.png,
# manifest.json}. Driven by tools/ui_iterate.sh.
#
# NOT A TEST, and deliberately absent from tools/health_report.py's lanes: it
# makes no assertions, so a PF_SUMMARY line from it would report a lane of zero
# checks as healthy. See the header comment for why it exits 0 on layout drift.
#
# Same source set, display requirement and sanitizer flags as EditorSessionTest
# -- it constructs the same processor and editor, so it inherits the same
# constraints, including -fno-sanitize=vptr for libfaust's -fno-rtti build.
#
# Leak detection is ON, like every other harness here. A comment used to sit at
# this spot claiming "NO ASAN LEAK CHECK ... a 5-fixture loop trips
# LeakSanitizer on JUCE-internal statics" -- and the target below never
# contained such a setting. The only place it existed was an env var in
# tools/ui_iterate.sh, whose own comment said "See host/CMakeLists.txt", so the
# two pointed at each other and the property held only when the binary was
# launched through that one script. Measured 2026-07-31: five fixtures, leak
# detection on, exit 0, no LeakSanitizer report. Both the comment and the env
# var are gone. If LSan ever does fire here, the fix is a
# __lsan_default_options hook in UiDesignGallery.cpp -- it travels with the
# binary and survives direct invocation, which the env var did not -- shipped
# with the failing output beside it.
# ---------------------------------------------------------------------------
juce_add_console_app(UiDesignGallery
    PRODUCT_NAME "UI Design Gallery"
)

target_sources(UiDesignGallery PRIVATE
    tests/UiDesignGallery.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PromptPanel.cpp
    Source/CodeEditorPanel.cpp
    Source/ParamGridPanel.cpp
    Source/KeyboardPanel.cpp
    Source/FaustEngine.cpp
    Source/ParamPool.cpp
    Source/OutputGuard.cpp
)

target_compile_features(UiDesignGallery PRIVATE cxx_std_17)

target_compile_definitions(UiDesignGallery PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # runDispatchLoopUntil lives inside `#if JUCE_MODAL_LOOPS_PERMITTED`
    # (juce_MessageManager.h:82-88), same as the two harnesses above.
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_compile_options(UiDesignGallery PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -g -O1)
target_link_options(UiDesignGallery PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)

target_link_libraries(UiDesignGallery PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${LIBFAUST_LIB}
)
