"""Unit tests for bench/tier_leakage_screen.py — the mechanical tier-rule
leakage check (2026-09-23 adversarial review, finding A8).

Frozen oracle: the screen over the *committed* tiered_prompts.json must flag
exactly these effect_ids at L1, and exactly zero at L2. If this drifts --
narrowed to miss a real leak, or widened back to the "knob"/"slider" false
positives the module's own docstring records having tried and reverted -- this
breaks.

Also includes a hand-read verdict on each frozen L1 hit (module docstring:
"every flagged effect_id should be spot-checked"), so a reader does not have
to re-derive what's a real leak vs. an artifact of the lexicon.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import tier_leakage_screen as tls  # noqa: E402

DATASET = json.loads((ROOT / "bench" / "prompts" / "tiered_prompts.json").read_text())

# Derived outcome-blind (screen written and this test authored before the L1
# hits below were read for adjudication) and frozen on purpose.
#   REAL   -- the effect category is named outright, exactly the rule violation
#             the tier definition forbids.
#   LIKELY_FALSE_POSITIVE -- the matched word is used in its ordinary-English
#             sense as part of the intended sensory metaphor, not as a
#             disguised category name. Kept flagged rather than silently
#             excluded -- a mechanical screen that quietly special-cases its
#             own hits is exactly the kind of "tuned until it passed" control
#             this project's tests are written to refuse to be
#             (tests/test_prompt_headroom.py:207-215's defence, same idea).
L1_HITS = {
    "filters-05": "LIKELY_FALSE_POSITIVE",   # "fresh air and light" -- ordinary air, not the `air` EQ band
    "generative-02": "REAL",                 # "a bright sharp SYNTH note" -- names the category outright
    "generative-05": "LIKELY_FALSE_POSITIVE",  # "touching a real STRING" -- the metaphor IS a physical string
}


def test_l2_has_zero_jargon_violations():
    """L2 ('casual musician') must carry no PARAMETER jargon. Confirmed clean
    after removing "knob"/"slider"/"parameter" from PARAM_JARGON -- see that
    list's own comment for why they were tried and reverted."""
    results = tls.screen_dataset(DATASET)
    violators = {r["effect_id"] for r in results if r["l2_violation"]}
    assert violators == set()


def test_l1_flags_exactly_the_frozen_set():
    results = tls.screen_dataset(DATASET)
    violators = {r["effect_id"] for r in results if r["l1_violation"]}
    assert violators == set(L1_HITS)


def test_l1_real_leak_is_the_category_word_not_a_number_or_jargon_term():
    """generative-02's L1 prompt literally says 'synth note' -- the effect
    category named outright, which is exactly what the L1 rule forbids
    (docs/prompt_efficacy_study.md:33). Confirms the screen catches the class
    of leak it exists for, not just numbers or PARAM_JARGON matches."""
    results = tls.screen_dataset(DATASET)
    r = next(r for r in results if r["effect_id"] == "generative-02")
    assert "synth" in r["l1_category_hits"]


def test_l0_matches_are_never_counted_as_violations():
    """L0's rule ('no mechanical description') is not the L1 rule ('no
    category word') -- a gear/artist reference legitimately names concrete
    nouns. L0 hits must always be evidence-only."""
    results = tls.screen_dataset(DATASET)
    for r in results:
        assert "l0_violation" not in r  # there is no such field, on purpose


def test_knob_and_slider_are_not_treated_as_jargon():
    """Regression guard for the false-positive lexicon entries this module's
    PARAM_JARGON comment records having tried and reverted."""
    fake = {"effect_id": "x", "category": "trivial", "target": "gain control",
            "tiers": {"L1": "a simple volume knob and a slider for balance",
                     "L2": "a simple volume knob", "L0": "a fader"}}
    r = tls.screen_effect(fake)
    assert r["l2_jargon_hits"] == []
