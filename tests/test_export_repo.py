"""
tests/test_export_repo.py — Unit + integration tests for tools/export_repo.py

PF-053 (docs/BUGS.md) has two independent defects:

  1. Compile-syntax bug: acceptsMidi()/getTailLengthSeconds() rendered
     `return TRUE == "TRUE";` — a bare, undefined C++ identifier compared to a
     string literal. Every exported project failed to compile. FIXED here.

  2. processBlock() is a passthrough stub that never loads/invokes the
     compiled Faust patch (Patch.dsp) — the exported plugin makes no sound by
     construction. NOT fixed here; deliberately deferred, see
     PLUGIN_HEALTH_PLAN.md P1.10 and .claude/skills/export/SKILL.md (the
     export command stays gated because of this half).

This file tests the generated-source fixes, guards against regressing back to
the bare TRUE/FALSE and doubled-brace bugs, and pins defect 2 as an honest
xfail so the deferral is visible in the suite rather than just in prose.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import export_repo  # noqa: E402

# CI's `test` job (.github/workflows/test.yml) is explicitly "no API key or
# faust compiler required" -- `build-host` is the job that apt-installs
# faust. export_plugin() now AOT-compiles via the real `faust` CLI
# unconditionally (ADR-038 E2), so every test that calls it needs this guard.
# Matches this file's own existing shutil.which("cmake") precedent below
# rather than inventing a new convention.
_requires_faust = pytest.mark.skipif(
    shutil.which("faust") is None, reason="faust CLI not installed"
)


# A minimal instrument patch (has both "gate" and "freq" -> detected as synth)
# and a minimal effect patch (neither) -> detected as Fx. Mirrors the
# has_gate/has_freq heuristic in export_repo.detect_plugin_type().
INSTRUMENT_SOURCE = (
    'import("stdfaust.lib");\n'
    'freq = hslider("freq", 440, 20, 2000, 1);\n'
    'gate = button("gate");\n'
    "process = os.osc(freq) * gate;\n"
)
EFFECT_SOURCE = 'import("stdfaust.lib");\nprocess = _ * 0.5;\n'


def _export(tmp_path, source, name="ExportedPlugin"):
    out = tmp_path / name
    rc = export_repo.export_plugin(out, source, name)
    assert rc == 0
    return out


def _plugin_cpp(exported_dir: Path) -> str:
    return (exported_dir / "Source" / "Plugin.cpp").read_text()


# ---------------------------------------------------------------------------
# Defect 1 (fixed): no more bare TRUE/FALSE C++ identifiers.
# ---------------------------------------------------------------------------


@_requires_faust
@pytest.mark.parametrize(
    "source,label",
    [(INSTRUMENT_SOURCE, "instrument"), (EFFECT_SOURCE, "effect")],
    ids=["instrument", "effect"],
)
def test_generated_cpp_has_no_bare_true_false_tokens(tmp_path, source, label):
    """Regression guard for PF-053 defect 1.

    The old code rendered `return TRUE == "TRUE";` — TRUE/FALSE with no
    quotes and no `bool`/macro definition anywhere in scope, which does not
    compile. Assert Plugin.cpp never contains a bare TRUE/FALSE token again.
    """
    exported = _export(tmp_path, source, f"Plugin_{label}")
    cpp = _plugin_cpp(exported)

    assert not re.search(r"\bTRUE\b", cpp), f"bare TRUE token regressed:\n{cpp}"
    assert not re.search(r"\bFALSE\b", cpp), f"bare FALSE token regressed:\n{cpp}"
    # And the old exact broken pattern specifically:
    assert '== "TRUE"' not in cpp
    assert '== "FALSE"' not in cpp


@_requires_faust
def test_instrument_renders_correct_bool_literals(tmp_path):
    exported = _export(tmp_path, INSTRUMENT_SOURCE, "InstrPlugin")
    cpp = _plugin_cpp(exported)

    assert "bool acceptsMidi() const override { return true; }" in cpp
    assert (
        "double getTailLengthSeconds() const override { return true ? 2.0 : 0.0; }"
        in cpp
    )


@_requires_faust
def test_effect_renders_correct_bool_literals(tmp_path):
    exported = _export(tmp_path, EFFECT_SOURCE, "FxPlugin")
    cpp = _plugin_cpp(exported)

    assert "bool acceptsMidi() const override { return false; }" in cpp
    assert (
        "double getTailLengthSeconds() const override { return false ? 2.0 : 0.0; }"
        in cpp
    )


@_requires_faust
def test_cmake_still_gets_upper_true_false(tmp_path):
    """CMakeLists.txt.j2's IS_SYNTH / NEEDS_MIDI_INPUT args are unrelated to
    the C++ bug and must keep receiving CMake-style TRUE/FALSE — the fix
    added separate *_cpp keys rather than changing this representation."""
    exported = _export(tmp_path, INSTRUMENT_SOURCE, "InstrCmake")
    cmake = (exported / "CMakeLists.txt").read_text()
    assert "IS_SYNTH TRUE" in cmake
    assert "NEEDS_MIDI_INPUT TRUE" in cmake

    exported_fx = _export(tmp_path, EFFECT_SOURCE, "FxCmake")
    cmake_fx = (exported_fx / "CMakeLists.txt").read_text()
    assert "IS_SYNTH FALSE" in cmake_fx
    assert "NEEDS_MIDI_INPUT FALSE" in cmake_fx


@_requires_faust
def test_print_summary_still_reports_correct_plugin_type(tmp_path, capsys):
    """The ~line 186 reader (plugin_type['is_synth'] == 'TRUE') was left
    untouched by the fix, since is_synth's representation didn't change."""
    _export(tmp_path, INSTRUMENT_SOURCE, "InstrPrint")
    out = capsys.readouterr().out
    assert "Plugin type: instrument" in out


# ---------------------------------------------------------------------------
# Defect 2 (FIXED, ADR-038 E2): processBlock now AOT-compiles Patch.dsp via
# the real `faust` CLI at export time and calls the emitted `mydsp::compute`.
# This was pinned as an honest xfail while deferred; the marker comes off
# here precisely because the behavior it asserted is no longer deferred.
# MIDI-to-Faust voice mapping for instrument patches is NOT part of this fix
# -- see export_repo.py's own comment at the Plugin.cpp template.
# ---------------------------------------------------------------------------


@_requires_faust
def test_process_block_invokes_the_compiled_faust_patch(tmp_path):
    exported = _export(tmp_path, EFFECT_SOURCE, "FxNotWired")
    cpp = _plugin_cpp(exported)

    # Isolate the processBlock() method body.
    match = re.search(
        r"processBlock\([^)]*\)\s*override\s*\{(.*?)\n\s{8}\}",
        cpp,
        re.DOTALL,
    )
    assert match, "could not locate processBlock() body in generated Plugin.cpp"
    body = match.group(1)

    # A real implementation must reference the compiled DSP object in some
    # form (Faust's generated ::compute call, a dsp instance, etc.) rather
    # than being an untouched passthrough.
    assert re.search(r"\bcompute\s*\(", body) or "mydsp" in body or "faustDsp" in body


# ---------------------------------------------------------------------------
# ADR-023 amendment: per-patch plugin identity, and no libfaust vendored in.
# ---------------------------------------------------------------------------


def test_plugin_code_is_deterministic_and_distinct_from_the_host():
    """Finding 1's collision ("Pfh1" hardcoded, identical to the shipping
    PluginForgeHost Fx target) must not recur: every export needs its own
    code, and re-exporting the same source must reproduce it."""
    code_a = export_repo._derive_plugin_code(EFFECT_SOURCE)
    code_b = export_repo._derive_plugin_code(EFFECT_SOURCE)
    code_c = export_repo._derive_plugin_code(INSTRUMENT_SOURCE)

    assert code_a == code_b, "same source must derive the same code"
    assert code_a != code_c, "different sources should derive different codes"
    assert re.fullmatch(r"[A-Z][a-z]{3}", code_a), f"not a valid 4-char code: {code_a}"
    assert code_a not in ("Pfh1", "Pfs1"), "collides with a shipping host target"


@_requires_faust
def test_cmake_receives_the_derived_plugin_code(tmp_path):
    exported = _export(tmp_path, EFFECT_SOURCE, "FxCode")
    cmake = (exported / "CMakeLists.txt").read_text()
    code = export_repo._derive_plugin_code(EFFECT_SOURCE)
    assert f"PLUGIN_CODE {code}" in cmake


@_requires_faust
def test_export_vendors_faust_headers_and_emits_patch_header(tmp_path):
    """No libfaust at build or runtime (ADR-023's 2026-08-13 amendment):
    the six header-only architecture files must be vendored unmodified, and
    Patch.h must exist and declare the AOT-compiled DSP class."""
    exported = _export(tmp_path, EFFECT_SOURCE, "FxVendor")

    for rel in export_repo._FAUST_VENDOR_HEADERS:
        assert (exported / "vendor" / "faust" / rel).is_file(), f"missing vendored {rel}"

    patch_h = (exported / "Source" / "Patch.h").read_text()
    assert "class mydsp" in patch_h
    assert "virtual void compute(" in patch_h
    assert "virtual void buildUserInterface(" in patch_h


@_requires_faust
def test_export_rejects_unparseable_faust_source(tmp_path):
    """A hand-edited Patch.dsp that no longer parses must fail loudly, not
    emit a broken header silently."""
    with pytest.raises(RuntimeError, match="faust -lang cpp"):
        export_repo.export_plugin(tmp_path / "Broken", "this is not faust", "Broken")


# ---------------------------------------------------------------------------
# Integration: both exported plugin types compile their generated sources.
# ---------------------------------------------------------------------------

_JUCE_PATH = Path.home() / "JUCE"


@pytest.mark.integration
@pytest.mark.skipif(shutil.which("cmake") is None, reason="cmake not installed")
@pytest.mark.skipif(
    not (_JUCE_PATH / "CMakeLists.txt").is_file(),
    reason=f"JUCE not found at {_JUCE_PATH} (export CMakeLists.txt.j2 default JUCE_PATH)",
)
@pytest.mark.parametrize(
    "source,name",
    [(EFFECT_SOURCE, "FxCompile"), (INSTRUMENT_SOURCE, "InstrumentCompile")],
    ids=["effect", "instrument"],
)
def test_exported_project_shared_code_compiles(tmp_path, source, name):
    """Configure the real export and compile its generated shared-code target.

    JUCE names the shared-code target exactly ``<name>``; the wrapper/package
    targets are ``<name>_VST3`` and ``<name>_Standalone``. Building only the
    unsuffixed target reaches Plugin.cpp and PluginEditor.cpp without invoking
    JUCE's COPY_PLUGIN_AFTER_BUILD installation step.
    """
    exported = _export(tmp_path, source, name)
    build_dir = exported / "build"

    configure = subprocess.run(
        [
            "cmake",
            "-S",
            str(exported),
            "-B",
            str(build_dir),
            f"-DJUCE_PATH={_JUCE_PATH}",
        ],
        capture_output=True,
        text=True,
        timeout=300,
    )

    assert configure.returncode == 0, (
        "cmake configure failed for exported project:\n"
        f"stdout:\n{configure.stdout}\nstderr:\n{configure.stderr}"
    )
    assert (build_dir / "CMakeCache.txt").is_file()

    build = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", name],
        capture_output=True,
        text=True,
        timeout=600,
    )

    assert build.returncode == 0, (
        f"generated shared-code target failed to compile for {name}:\n"
        f"stdout:\n{build.stdout}\nstderr:\n{build.stderr}"
    )
