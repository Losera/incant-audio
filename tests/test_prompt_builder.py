"""
tests/test_prompt_builder.py — Unit tests for llm/prompt_builder.py
"""

import os
import re
import sys
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import prompt_builder
import providers

_BEGIN = "# BEGIN GENERATED STDLIB REFERENCE"
_END = "# END GENERATED STDLIB REFERENCE"
# Same pattern tools/gen_stdlib_block.py's verify_prompt_references() uses,
# reused rather than reinvented: a 2-letter namespace prefix + function name.
_NS_FUNC = re.compile(r"\b([a-z]{2})\.([A-Za-z_]\w*)")


def _split_prose_and_block(full_prompt: str):
    """(prose, raw_stdlib_block) -- prose is everything OUTSIDE the generated
    markers, since filtering only ever touches what's inside them."""
    start = full_prompt.index(_BEGIN) + len(_BEGIN)
    end = full_prompt.index(_END)
    return full_prompt[:start] + full_prompt[end:], full_prompt[start:end]


def test_dynamic_prompt_builder_shrinks_prompt_for_filter():
    user_prompt = "Design a 4-pole lowpass filter with resonance control."
    full_prompt = prompt_builder.SYSTEM_PROMPT_PATH.read_text()
    dynamic_prompt = prompt_builder.build_dynamic_prompt(user_prompt)
    
    assert len(dynamic_prompt) < len(full_prompt)
    assert "# BEGIN GENERATED STDLIB REFERENCE" in dynamic_prompt
    assert "# END GENERATED STDLIB REFERENCE" in dynamic_prompt
    assert "Never define the same variable" in dynamic_prompt
    assert "process" in dynamic_prompt


def test_dynamic_prompt_builder_preserves_headroom():
    user_prompt = "Create a distortion fuzz pedal with soft clipping."
    dynamic_prompt = prompt_builder.build_dynamic_prompt(user_prompt)
    
    slack = providers.headroom_tokens(dynamic_prompt, providers.MAX_OUTPUT_TOKENS)
    assert slack > 300, f"Expected >300 tokens slack, got {slack}"


def test_stdlib_filter_always_env_var_forces_filtering_despite_headroom(monkeypatch):
    """Queue item 6's A/B knob: PLUGINFORGE_STDLIB_FILTER_ALWAYS=1 must trim
    the stdlib block even when unfiltered headroom would otherwise pass the
    _MIN_UNFILTERED_HEADROOM gate. Benchmark-only -- see the flag's own
    comment in prompt_builder.py for why this must not become a permanent
    toggle if the A/B ships it.

    Uses the INSTRUMENT prompt deliberately, not the effects one: the
    effects prompt's own unfiltered headroom for almost any real user
    prompt is already below _MIN_UNFILTERED_HEADROOM (measured 135 tokens
    for "a warm compressor" against system_prompt.txt), so the baseline
    ALREADY always filters there and this flag would be observably a
    no-op if tested against it -- exactly the finding that sent item 6's
    benchmark to the instrument side instead. The instrument prompt's
    676-token headroom margin is what makes "pressure-only" and "always"
    actually differ.
    """
    instrument = (ROOT / "llm" / "prompts" / "instrument_prompt.txt").read_text()
    user_prompt = "a warm pad"
    baseline = prompt_builder.build_dynamic_prompt(user_prompt, base_system_prompt=instrument)

    monkeypatch.setenv("PLUGINFORGE_STDLIB_FILTER_ALWAYS", "1")
    forced = prompt_builder.build_dynamic_prompt(user_prompt, base_system_prompt=instrument)

    assert len(forced) < len(baseline), (
        "PLUGINFORGE_STDLIB_FILTER_ALWAYS=1 must shrink the instrument prompt "
        "relative to the default (unfiltered, since it has ample headroom) behavior"
    )
    assert "# BEGIN GENERATED STDLIB REFERENCE" in forced
    assert "# END GENERATED STDLIB REFERENCE" in forced


def test_stdlib_filter_always_off_by_default():
    assert os.environ.get("PLUGINFORGE_STDLIB_FILTER_ALWAYS", "0") == "0", (
        "must default to off outside a benchmark run -- see conftest.py if "
        "this ever fails in CI"
    )


def test_dynamic_prompt_filters_the_selected_base_instead_of_effects_default():
    instrument_path = ROOT / "llm" / "prompts" / "instrument_prompt.txt"
    instrument = instrument_path.read_text()
    dynamic_prompt = prompt_builder.build_dynamic_prompt(
        "a sawtooth synth", base_system_prompt=instrument
    )

    assert "An instrument MUST declare exactly these three controls" in dynamic_prompt
    assert "gate" in dynamic_prompt
    assert dynamic_prompt != prompt_builder.build_dynamic_prompt("a sawtooth synth")


@pytest.mark.parametrize("path", [
    prompt_builder.SYSTEM_PROMPT_PATH,
    ROOT / "llm" / "prompts" / "instrument_prompt.txt",
])
def test_core_prefix_functions_the_prose_references_survive_every_domain(path):
    """Every ns.func the PROSE (rules/units/few-shots, outside the generated
    stdlib markers) references, WHOSE NAMESPACE IS IN CORE_PREFIXES, must
    survive filter_stdlib_block() for every reachable domain combination.

    Scoped to CORE_PREFIXES deliberately, not every prose mention: a prose
    rule can cite a non-core function (de.delay, ef.gate_stereo) purely as
    an ILLUSTRATIVE EXAMPLE of a general-purpose rule ("the FIRST argument
    to de.delay, de.fdelay and ef.echo is the MAXIMUM delay") -- dropping
    that function's own reference-block ENTRY for a domain that doesn't
    need it is fine; the rule text itself is prose, never filtered, and
    stays regardless. What must NEVER happen, and is the actual 2026-08-13
    defect this guards against, is CORE_PREFIXES namespaces (the primitives
    a correct patch of almost any kind needs) disappearing because a prompt
    only matched one narrow domain -- e.g. instrument_prompt.txt teaches
    ve.moog_vcf in prose while a "subtractive synth" prompt's
    oscillator-only domain match used to strip fi.* and ve.* entirely, even
    though the prompt still said "Use ONLY functions from the ... reference
    below". Exhaustive over domain combinations rather than sampled
    prompts, so this makes the defect class impossible, not just patched
    for one prompt.
    """
    full = path.read_text()
    prose, raw_block = _split_prose_and_block(full)
    prose_refs = set(_NS_FUNC.findall(prose))

    # Scope to CORE_PREFIXES namespaces that the raw block actually
    # documents -- see the docstring for why non-core prose mentions are
    # excluded, and gen_stdlib_block.py --verify-prompt already covers
    # existence-against-the-installed-library for every reference here.
    core_refs = {(ns, fn) for ns, fn in prose_refs
                 if f"{ns}." in prompt_builder.CORE_PREFIXES and f"{ns}.{fn}" in raw_block}
    assert core_refs, (
        f"{path.name}: no CORE_PREFIXES ns.func reference found in the raw "
        "block -- this test isn't exercising anything, check _NS_FUNC/markers"
    )

    # Every domain combination extract_relevant_domains() can actually
    # produce: the full set (its own no-match fallback) or a single matched
    # domain. NOT the empty set -- that fallback guarantees callers never
    # see one, so filter_stdlib_block() is never reached with one either.
    all_domains = list(prompt_builder.DOMAIN_KEYWORDS.keys())
    domain_combos = [set(all_domains)] + [{d} for d in all_domains]
    for domains in domain_combos:
        filtered = prompt_builder.filter_stdlib_block(raw_block, domains)
        missing = sorted(f"{ns}.{fn}" for ns, fn in core_refs
                         if f"{ns}.{fn}" not in filtered)
        assert not missing, (
            f"{path.name}, domains={domains or '(none)'}: filtering dropped "
            f"core-prefix {missing}, which the prose still references"
        )
