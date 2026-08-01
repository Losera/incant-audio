"""The instrument/effect router.

The router picks which system prompt a request gets. Getting it wrong is not
catastrophic in either direction -- llm/router.py's docstring argues why -- but it
is user-visible, so the cases that matter are pinned here rather than trusted.

The two corpora below are the real test. `bench/prompts/prompts.json`'s 25 entries
are what the benchmark measures, and its five `generative` records are exactly the
prompts that have been scored green for months while producing patches nobody
could play. If the router cannot separate those five from the other twenty, it
has not earned its place in the pipeline.
"""
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import router  # noqa: E402


# The five `generative` prompts from bench/prompts/prompts.json, verbatim.
GENERATIVE = [
    "a pure sine wave oscillator with frequency and amplitude",
    "a sawtooth synth with an ADSR envelope",
    "a 2-operator FM synth with ratio and modulation index",
    "a white noise generator with a tone control",
    "a Karplus-Strong plucked string synthesizer",
]

# A spread of the effect categories: trivial, filters, time-based, dynamics.
EFFECTS = [
    "a stereo gain control in dB",
    "a warm analog-style low-pass filter with cutoff and resonance",
    "a stereo delay with time, feedback, and dry/wet",
    "a ping-pong delay that bounces between left and right",
    "a tape-style flanger",
    "a stereo chorus with rate and depth",
    "a compressor with threshold, ratio, attack and release",
    "a noise gate that closes below a threshold",
    "a high-shelf EQ for adding air to vocals",
    "a notch filter to remove 60 Hz hum",
    "a plate reverb with decay and damping",
    "a bitcrusher with sample-rate reduction",
]


@pytest.mark.parametrize("prompt", GENERATIVE)
def test_generative_prompts_route_to_instrument(prompt):
    assert router.classify(prompt) == router.INSTRUMENT, router.explain(prompt)


@pytest.mark.parametrize("prompt", EFFECTS)
def test_effect_prompts_route_to_effect(prompt):
    assert router.classify(prompt) == router.EFFECT, router.explain(prompt)


def test_the_whole_benchmark_corpus_splits_five_to_twenty():
    """End-to-end against the real file, not a copy of it.

    Pinning the SPLIT rather than each prompt means a corpus edit that changes the
    balance shows up here, instead of being absorbed silently.
    """
    corpus = json.loads((ROOT / "bench" / "prompts" / "prompts.json").read_text())
    # {category: [prompt, ...]} -- five categories of five.
    prompts = [p for entries in corpus.values() for p in entries]
    assert len(prompts) == 25, f"corpus is {len(prompts)} prompts, expected 25"
    assert set(corpus["generative"]) == set(GENERATIVE), (
        "bench/prompts/prompts.json's generative category no longer matches the "
        "list pinned in this file; update GENERATIVE deliberately."
    )

    kinds = [router.classify(p) for p in prompts]
    n_inst = kinds.count(router.INSTRUMENT)
    assert n_inst == 5, (
        f"router calls {n_inst} of 25 prompts instruments; the corpus has exactly "
        "five generative entries. Misrouted:\n  "
        + "\n  ".join(
            f"{k:10} {p}" for k, p in zip(kinds, prompts)
            if (k == router.INSTRUMENT) != (p in GENERATIVE)
        )
    )


class TestTheAmbiguousCases:
    """The phrasings that make a naive keyword match wrong.

    Each of these was a real failure of an earlier draft of the term lists, not a
    hypothetical -- which is why they are pinned individually.
    """

    def test_an_lfo_is_not_an_instrument(self):
        # "oscillator" is the strongest instrument word there is, and a tremolo is
        # built from one. Without the modulator exclusion every modulation effect
        # routes to the instrument prompt and comes out silent.
        assert router.classify(
            "a tremolo using an LFO oscillator to modulate the amplitude"
        ) == router.EFFECT

    def test_a_filter_for_a_synth_is_still_a_filter(self):
        # Names the thing being PROCESSED, not the thing being built. A tie must
        # not resolve to instrument, which is why the comparison is strict.
        assert router.classify(
            "a warm analog filter for my synth bass"
        ) == router.EFFECT

    def test_bass_boost_is_not_a_bass_patch(self):
        assert router.classify("a bass boost shelf for kick drums") == router.EFFECT

    def test_an_explicitly_playable_request_is_an_instrument(self):
        assert router.classify(
            "a playable pad I can hold chords on"
        ) == router.INSTRUMENT

    def test_empty_and_junk_default_to_effect(self):
        # The default is the status quo: an unrecognised prompt must behave
        # exactly as it did before the router existed.
        for junk in ("", "   ", "asdfgh", "make it better"):
            assert router.classify(junk) == router.EFFECT

    def test_none_does_not_raise(self):
        assert router.classify(None) == router.EFFECT


def test_explain_reports_the_evidence():
    """A misroute must be debuggable without a debugger."""
    ev = router.explain("a sawtooth synth with an ADSR envelope")
    assert ev["kind"] == router.INSTRUMENT
    assert ev["instrument_hits"], "no evidence recorded for an instrument verdict"

    ev = router.explain("a tremolo using an LFO oscillator")
    assert ev["kind"] == router.EFFECT
    assert ev["sources_read_as_modulators"] is True, (
        "the LFO exclusion fired but explain() does not say so, so a future "
        "misroute here would look unexplained"
    )
