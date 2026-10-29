#!/usr/bin/env python3
"""
Export a PluginForge session as a standalone plugin repository.

Generates a complete, buildable CMake + JUCE project with:
  - CMakeLists.txt (generated from Jinja2 template)
  - Source/Plugin.cpp (Faust JIT wrapper)
  - Source/PluginEditor.cpp (themed UI)
  - Patch.dsp (the generated Faust source)

Usage:
  python tools/export_repo.py <output_dir> [--source <faust_source_file>]

If --source is not provided, reads from stdin.

The output is a self-contained plugin project that builds independently of
PluginForge (requires JUCE + libfaust on the target machine).
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_DIR = REPO_ROOT / "tools" / "export"
JUCE_PATH = Path.home() / "JUCE"

# The header-only Faust architecture files an AOT-emitted `class mydsp : public
# dsp` needs to compile, with no libfaust at the recipient's build or runtime
# (ADR-023's 2026-08-13 amendment, docs/decisions.md). Confirmed by walking the
# transitive #include closure of dsp/dsp.h + gui/UI.h + gui/meta.h + gui/MapUI.h
# on this machine's Faust 2.85.9: exactly these six files, 1152 lines total,
# nothing else. dsp/dsp.h's own special exception permits redistributing them
# unmodified in a larger work -- vendored verbatim, never edited, per that
# exception's own condition.
_FAUST_VENDOR_HEADERS = (
    "dsp/dsp.h",
    "export.h",
    "gui/UI.h",
    "gui/meta.h",
    "gui/MapUI.h",
    "gui/PathBuilder.h",
)


def _faust_include_dir() -> Path:
    """The directory containing `faust/`, read from the installed toolchain
    itself rather than hardcoded -- CLAUDE.md's own machine-drift rule.
    `faust -includedir` (confirmed present on Faust 2.85.9) prints exactly
    this path."""
    result = subprocess.run(
        ["faust", "-includedir"], capture_output=True, text=True, check=True
    )
    return Path(result.stdout.strip())


def _vendor_faust_headers(output_dir: Path) -> None:
    src_root = _faust_include_dir() / "faust"
    dest_root = output_dir / "vendor" / "faust"
    for rel in _FAUST_VENDOR_HEADERS:
        dest = dest_root / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_root / rel, dest)


def _emit_faust_header(dsp_path: Path, output_header: Path, class_name: str = "mydsp") -> None:
    """Static-compile the frozen patch to a `class mydsp : public dsp` header
    via the real `faust` CLI -- the mechanism `.claude/skills/export/
    SKILL.md`'s landing requirement #1 names. No subprocess or libfaust needed
    at the recipient's end; this call happens once, at export time, on the
    PluginForge host machine. Raises on failure rather than emitting a broken
    header silently: generation already validates Faust before export sees it
    (CLAUDE.md "validated against the real faust CLI"), so a failure here
    means the source was hand-edited after generation.
    """
    result = subprocess.run(
        ["faust", "-lang", "cpp", "-cn", class_name, "-o", str(output_header), str(dsp_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"faust -lang cpp failed to emit an AOT header:\n{result.stderr}")


def _derive_plugin_code(source: str) -> str:
    """Deterministic 4-char plugin code from the patch's own Faust source.

    ADR-023's amendment, decision item 2: hosts key a plugin by this code, and
    every export before this fix hardcoded "Pfh1" -- identical to the shipping
    PluginForgeHost Fx target's own code (host/CMakeLists.txt:69), which that
    file's own comment warns against: two plugins sharing a code means one
    shadows the other. JUCE only requires exactly 4 characters (JUCEUtils.cmake's
    _juce_to_char_literal warns, does not reject, on other lengths); kept to
    the house convention (one uppercase letter, three lowercase) already used
    by the shipping targets' own codes and JUCE's own _juce_make_valid_4cc
    fallback generator.
    """
    digest = hashlib.sha1(source.encode("utf-8")).digest()
    upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    lower = "abcdefghijklmnopqrstuvwxyz"
    return upper[digest[0] % 26] + "".join(lower[b % 26] for b in digest[1:4])


def detect_plugin_type(source: str) -> dict:
    """Detect whether the Faust source is an instrument or effect."""
    has_gate = "gate" in source.lower()
    has_freq = any(w in source.lower() for w in ["freq", "key"])
    is_instrument = has_gate and has_freq

    return {
        # CMake-facing (JUCE's IS_SYNTH / NEEDS_MIDI_INPUT args in CMakeLists.txt.j2)
        "is_synth": "TRUE" if is_instrument else "FALSE",
        "needs_midi": "TRUE" if is_instrument else "FALSE",
        # C++-facing (Plugin.cpp bool return values) — real bool literals, not
        # strings, so they can be interpolated directly instead of compared
        # against a quoted "TRUE"/"FALSE" (PF-053: that comparison doesn't compile).
        "is_synth_cpp": "true" if is_instrument else "false",
        "needs_midi_cpp": "true" if is_instrument else "false",
        "vst3_category": "Instrument" if is_instrument else "Fx",
    }


def render_template(template: str, variables: dict) -> str:
    """Minimal Jinja2-like template renderer (no external deps)."""
    result = template
    for key, value in variables.items():
        result = result.replace("{{ " + key + " }}", str(value))
    return result


def export_plugin(output_dir: Path, source: str, plugin_name: str = "ExportedPlugin"):
    """Export a plugin to a standalone repository."""
    output_dir.mkdir(parents=True, exist_ok=True)
    source_dir = output_dir / "Source"
    source_dir.mkdir(exist_ok=True)

    # Detect plugin type from Faust source
    plugin_type = detect_plugin_type(source)
    plugin_code = _derive_plugin_code(source)
    variables = {"plugin_name": plugin_name, "plugin_code": plugin_code, **plugin_type}

    # Render CMakeLists.txt from template
    cmake_template = (TEMPLATE_DIR / "CMakeLists.txt.j2").read_text()
    cmake_content = render_template(cmake_template, variables)
    (output_dir / "CMakeLists.txt").write_text(cmake_content)

    # Write the Faust source (the editable source of record) and AOT-compile
    # it to a header, in-process on this machine -- the recipient's build
    # needs neither the faust CLI nor libfaust (ADR-023 amendment).
    (output_dir / "Patch.dsp").write_text(source)
    _emit_faust_header(output_dir / "Patch.dsp", source_dir / "Patch.h")
    _vendor_faust_headers(output_dir)

    # Generate Plugin.cpp — a frozen, AOT-compiled Faust DSP wrapper. This is
    # a stripped FaustEngine with the whole runtime-recompile swap protocol
    # deleted (docs/sessions/020-generated-faces-v2.md:219-234): a frozen
    # export never recompiles, so none of FaustEngine's audioBusy/compileMutex
    # machinery is needed. Only the effect path is wired here: MIDI-to-Faust
    # voice mapping (FaustEngine.cpp's noteOn/noteOff, :519-562) is a separate,
    # already-substantial subsystem and is NOT ported by this change -- an
    # exported instrument patch compiles and runs but will not yet make sound
    # from MIDI input.
    plugin_cpp = textwrap.dedent(f"""\
        // Plugin.cpp — generated by PluginForge export
        // Builds as a VST3/Standalone. The Faust patch is AOT-compiled to
        // Patch.h at export time (no libfaust at build or runtime here).

        #include <juce_audio_processors/juce_audio_processors.h>

        #include "faust/dsp/dsp.h"
        #include "faust/gui/UI.h"
        #include "faust/gui/meta.h"
        #include "faust/gui/MapUI.h"
        #include "Patch.h"

        class ExportedPlugin : public juce::AudioProcessor
        {{
        public:
            ExportedPlugin()
                : AudioProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Output", juce::AudioChannelSet::stereo(), true))
                , numIns(dsp.getNumInputs())
                , numOuts(dsp.getNumOutputs())
            {{
                dsp.buildUserInterface(&ui);
            }}

            void prepareToPlay(double sampleRate, int samplesPerBlock) override
            {{
                dsp.init(static_cast<int>(sampleRate));
                scratch.setSize(2, samplesPerBlock, false, false, true);
            }}

            void releaseResources() override {{}}

            void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
            {{
                juce::ScopedNoDenormals noDenormals;
                (void) midi;    // instrument MIDI-to-Faust mapping: not wired yet, see above

                const int n = buffer.getNumSamples();
                const int bufChans = buffer.getNumChannels();

                // Fast path: the patch's arity matches the host bus layout
                // exactly -- in-place, no copy. Mirrors FaustEngine::process's
                // own fast path (host/Source/FaustEngine.cpp:722-728); this
                // export is frozen to one patch, so numIns/numOuts are fixed
                // at construction rather than re-read per block.
                if (numIns == bufChans && numOuts == bufChans)
                {{
                    float* const* io = buffer.getArrayOfWritePointers();
                    dsp.compute(n, const_cast<float**>(io), const_cast<float**>(io));
                    return;
                }}

                // Mismatched arity (e.g. a mono effect on a stereo bus):
                // compute into `scratch`, then fan outputs back across the
                // host's channels, duplicating a mono output to both. Same
                // reasoning as FaustEngine.cpp:730-781.
                float* const* bufChannels = buffer.getArrayOfWritePointers();
                float* const* outChannels = scratch.getArrayOfWritePointers();

                float* ins[2]  = {{ nullptr, nullptr }};
                float* outs[2] = {{ nullptr, nullptr }};

                for (int ch = 0; ch < numIns; ++ch)
                    ins[ch] = bufChannels[ch < bufChans ? ch : bufChans - 1];
                for (int ch = 0; ch < numOuts; ++ch)
                    outs[ch] = outChannels[ch];

                dsp.compute(n, ins, outs);

                for (int ch = 0; ch < bufChans; ++ch)
                    juce::FloatVectorOperations::copy(bufChannels[ch],
                                                       outChannels[ch < numOuts ? ch : numOuts - 1],
                                                       n);
            }}

            juce::AudioProcessorEditor* createEditor() override {{ return new juce::GenericAudioProcessorEditor(*this); }}
            bool hasEditor() const override {{ return true; }}

            const juce::String getName() const override {{ return "{plugin_name}"; }}
            bool acceptsMidi() const override {{ return {plugin_type["needs_midi_cpp"]}; }}
            bool producesMidi() const override {{ return false; }}
            double getTailLengthSeconds() const override {{ return {plugin_type["is_synth_cpp"]} ? 2.0 : 0.0; }}

            int getNumPrograms() override {{ return 1; }}
            int getCurrentProgram() override {{ return 0; }}
            void setCurrentProgram(int) override {{}}
            const juce::String getProgramName(int) override {{ return {{}}; }}
            void changeProgramName(int, const juce::String&) override {{}}

            void getStateInformation(juce::MemoryBlock& destData) override {{}}
            void setStateInformation(const void* data, int sizeInBytes) override {{}}

        private:
            mydsp dsp;
            MapUI ui;
            const int numIns;
            const int numOuts;
            juce::AudioBuffer<float> scratch;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportedPlugin)
        }};

        juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
        {{
            return new ExportedPlugin();
        }}
    """)
    (source_dir / "Plugin.cpp").write_text(plugin_cpp)

    # Generate a simple editor stub
    editor_cpp = textwrap.dedent("""\
        // PluginEditor.cpp — generated by PluginForge export
        #include <juce_audio_processors/juce_audio_processors.h>

        class ExportedEditor : public juce::AudioProcessorEditor
        {
        public:
            ExportedEditor(juce::AudioProcessor& p) : AudioProcessorEditor(p)
            {
                setSize(400, 300);
            }

            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colours::darkgrey);
                g.setColour(juce::Colours::white);
                g.setFont(16.0f);
                g.drawFittedText("Exported from PluginForge\\n\\nEdit Patch.dsp and rebuild.",
                                 getLocalBounds(), juce::Justification::centred, 2);
            }

            void resized() override {}

        private:
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportedEditor)
        };
    """)
    (source_dir / "PluginEditor.cpp").write_text(editor_cpp)

    # Write README
    readme = textwrap.dedent(f"""\
        # {plugin_name}

        Generated by [PluginForge](https://github.com/pluginforge).

        ## Build

        ```bash
        # Requires: CMake 3.22+, JUCE. No Faust or libfaust needed -- the
        # patch was AOT-compiled to Source/Patch.h at export time.
        cmake -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build build
        ```

        The plugin will appear in `build/{plugin_name}_artefacts/`.

        ## Files

        - `Patch.dsp` — the Faust DSP source (editable, but Source/Patch.h
          below is what actually builds; re-export after editing)
        - `Source/Patch.h` — the AOT-compiled Faust DSP (`faust -lang cpp`)
        - `Source/Plugin.cpp` — JUCE plugin wrapping the compiled DSP
        - `Source/PluginEditor.cpp` — themed UI
        - `vendor/faust/` — unmodified Faust architecture headers Patch.h
          needs to compile (GRAME's LGPL + linking exception; see
          vendor/faust/dsp/dsp.h's own license header)

        ## Customising

        `Source/Patch.h` is AOT-compiled from `Patch.dsp` at export time, not
        at build time — editing `Patch.dsp` alone and rebuilding has no
        effect. To apply a change, edit `Patch.dsp` here, then from a
        PluginForge checkout re-run:

            python tools/export_repo.py <this-dir> --source <this-dir>/Patch.dsp --name {plugin_name}

        which re-compiles `Source/Patch.h`; then `cmake --build build` again.
    """)
    (output_dir / "README.md").write_text(readme)

    print(f"Exported plugin to {output_dir}/")
    print(f"  Plugin type: {'instrument' if plugin_type['is_synth'] == 'TRUE' else 'effect'}")
    print(f"  Build with: cd {output_dir} && cmake -B build && cmake --build build")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Export a PluginForge session as a standalone plugin repository"
    )
    parser.add_argument("output_dir", help="Output directory for the exported plugin")
    parser.add_argument("--source", help="Faust source file (reads from stdin if omitted)")
    parser.add_argument("--name", default="ExportedPlugin", help="Plugin name")
    args = parser.parse_args()

    if args.source:
        source = Path(args.source).read_text()
    else:
        source = sys.stdin.read()

    if not source.strip():
        print("Error: no Faust source provided", file=sys.stderr)
        return 1

    return export_plugin(Path(args.output_dir), source, args.name)


if __name__ == "__main__":
    sys.exit(main())
