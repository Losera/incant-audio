"""Prompt-grounding tests — the regression guard for the 2026-07-21 incident.

Both system prompts were found to teach Faust functions that do not exist
(`ef.ping_pong`, `ef.chorus`, `ef.flanger`), and two of the four few-shot examples
in the production prompt did not compile. That directly produced the two failure
modes recorded as "persistent model failures" for two months — the flanger prompt
failing with `undefined symbol : flanger_mono` (the model had the right function
name and the wrong namespace, because the prompt taught a fictitious one).

These tests make that class of defect impossible to reintroduce:

  1. every curated entry in tools/gen_stdlib_block.py resolves in the installed lib
  2. the checked-in generated block matches what the generator produces now
  3. every `ns.func` token anywhere in the prompt resolves — including the prose
     rules and the few-shot examples, which is where the fabrications actually were
  4. every few-shot example COMPILES

Skipped, not failed, when Faust is absent: the CI Python job deliberately installs
no compiler ("no API key or faust compiler required"). The build-host job does have
Faust and runs this file explicitly — see .github/workflows/test.yml.

A failure here after a Faust upgrade is a true positive, not flakiness: it means the
installed library no longer provides something the prompt promises the model.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PROMPT_FILE = ROOT / "llm" / "prompts" / "system_prompt.txt"
FAUST_LIB_DIR = Path("/usr/share/faust")

sys.path.insert(0, str(ROOT / "tools"))

needs_faust_libs = pytest.mark.skipif(
    not (FAUST_LIB_DIR / "stdfaust.lib").exists(),
    reason="Faust standard library not installed (CI Python job has no compiler)",
)
needs_faust_binary = pytest.mark.skipif(
    shutil.which("faust") is None,
    reason="faust compiler not on PATH (CI Python job has no compiler)",
)


@pytest.fixture(scope="module")
def prompt_text() -> str:
    return PROMPT_FILE.read_text()


@needs_faust_libs
def test_every_curated_entry_resolves():
    """CURATED names a function that the installed stdfaust.lib does not export."""
    from gen_stdlib_block import resolve_all

    _, errors = resolve_all()
    assert not errors, (
        "Curated stdlib entries do not exist in the installed Faust library:\n  "
        + "\n  ".join(errors)
        + "\n\nEither a Faust upgrade renamed/removed them, or CURATED names "
          "something that never existed. Fix tools/gen_stdlib_block.py; do not "
          "weaken this check."
    )


@needs_faust_libs
def test_generated_block_is_current(prompt_text):
    """The checked-in block drifted from what the generator produces."""
    from gen_stdlib_block import BEGIN_MARKER, END_MARKER, render, resolve_all

    groups, errors = resolve_all()
    assert not errors, "resolve failed; see test_every_curated_entry_resolves"

    expected = render(groups)
    start = prompt_text.find(BEGIN_MARKER)
    end = prompt_text.find(END_MARKER)
    assert start != -1 and end != -1, (
        f"generated-block markers missing from {PROMPT_FILE.name}; "
        "the block must stay delimited so the generator can splice it"
    )
    actual = prompt_text[start:end + len(END_MARKER)]
    assert actual == expected, (
        "The generated stdlib block in the system prompt is stale or was "
        "hand-edited. Regenerate it:\n"
        "    python tools/gen_stdlib_block.py --write"
    )


@needs_faust_libs
def test_no_fabricated_stdlib_references(prompt_text):
    """A `ns.func` reference anywhere in the prompt names a nonexistent function."""
    from gen_stdlib_block import verify_prompt_references

    problems = verify_prompt_references(prompt_text)
    assert not problems, (
        "The system prompt references Faust functions that do not exist:\n  "
        + "\n  ".join(problems)
        + "\n\nThis is the exact defect found 2026-07-21 (ef.ping_pong, ef.chorus, "
          "ef.flanger). A few-shot example is the strongest signal in the prompt: "
          "teaching a fabricated name licenses the model to invent more."
    )


@needs_faust_libs
def test_prompt_has_few_shot_examples(prompt_text):
    """Guards the extractor itself — a silent parse failure would make the
    compile test below vacuously pass."""
    from gen_stdlib_block import iter_prompt_examples

    examples = iter_prompt_examples(prompt_text)
    assert len(examples) >= 4, (
        f"expected at least 4 few-shot examples, extracted {len(examples)}. "
        "If the prompt's USER:/FAUST: layout changed, update iter_prompt_examples()."
    )
    for idx, code in examples:
        assert 'import("stdfaust.lib")' in code, (
            f"example {idx} does not start with the required import — either the "
            "example is wrong or the extractor mis-sliced it"
        )


@needs_faust_binary
@needs_faust_libs
def test_every_few_shot_example_compiles(prompt_text):
    """A few-shot example that does not compile teaches the model to write
    code that does not compile. Two of four used to fail this."""
    from gen_stdlib_block import iter_prompt_examples

    failures = []
    for idx, code in iter_prompt_examples(prompt_text):
        with tempfile.NamedTemporaryFile("w", suffix=".dsp", delete=False) as fh:
            fh.write(code)
            tmp = fh.name
        try:
            result = subprocess.run(
                ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
                capture_output=True, text=True, timeout=30,
            )
            if result.returncode != 0:
                first_line = code.splitlines()[1] if len(code.splitlines()) > 1 else ""
                failures.append(
                    f"example {idx} ({first_line[:40]}...): {result.stderr.strip()[:200]}"
                )
        finally:
            Path(tmp).unlink(missing_ok=True)

    assert not failures, "Few-shot examples do not compile:\n  " + "\n  ".join(failures)
