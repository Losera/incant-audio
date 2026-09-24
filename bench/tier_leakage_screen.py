#!/usr/bin/env python3
"""bench/tier_leakage_screen.py — outcome-blind check for tier-rule leakage.

WHY THIS EXISTS (2026-09-23 adversarial review, finding A8)

docs/prompt_efficacy_study.md:42-44 names its own internal-validity threat and
leaves it to a human spot-check: "tier-rule leakage (an L1 prompt accidentally
naming a parameter)." That threat has never actually been audited — every
published tier delta (the L1 metaphor-only trough at 48%/50% first-try in both
the groq and ollama grids) implicitly assumes L1 prompts never leak the
category-identifying or parameter-identifying words the tier rule forbids. If
they do, part of what looks like "the model does worse with vague prompts" is
really "some L1 prompts are not actually vague."

MECHANICAL, NOT TUNED — checked before any outcome was read
    Every candidate leak term below is derived from either (a) the tier
    definitions' own worked examples in docs/prompt_efficacy_study.md:28-34
    ("cutoff", "Q", "feedback" — L2's named jargon examples) plus a standard,
    effect-agnostic DSP-parameter vocabulary, or (b) the dataset's own
    `category` and `target` fields in tiered_prompts.json — written before any
    tier prompt text and before any generation ever ran, so using them as the
    per-effect "what would count as naming the category" list cannot have been
    tuned to make any tier-delta result look cleaner. This module was written
    and its self-test frozen before results.json/tiered_prompts.json's L1
    prompts were read for this purpose — see tests/test_tier_leakage_screen.py.

TIER RULES ENFORCED (docs/prompt_efficacy_study.md:28-34)
    L2 — "NO parameter jargon (no 'cutoff', 'Q', 'feedback')" — category words
         ARE allowed at L2 ("colloquial category words OK ('echo')").
    L1 — "must NOT name any effect category or parameter" — both banned.
    L0 — "must NOT describe the sound mechanically" — NOT the same rule as L1;
         a gear/artist reference legitimately names concrete nouns ("the fader
         from an SSL mixing console") without being a mechanical description.
         A lexicon match here is evidence, not a verdict — flagged separately
         and never rolled into the L1/L2 pass/fail count.

WHAT THIS CANNOT SEE, stated so it isn't rediscovered later
    - Synonyms not in PARAM_JARGON / the effect's own category-word set
      ("squash" for compression, "shimmer" for reverb-adjacent modulation).
      This is a floor on the leakage rate, not a ceiling.
    - Whether a category word appears as PART of the sensory metaphor itself
      rather than a literal violation (e.g. a metaphor that happens to contain
      "gate" as an ordinary English word, not the effect). False positives are
      possible; every flagged effect_id should be spot-checked, same as
      docs/prompt_efficacy_study.md already asked a human to do.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
DEFAULT_PROMPTS_FILE = BENCH_DIR / "prompts" / "tiered_prompts.json"

# L2's own two named examples ("cutoff", "Q", "feedback" — docs/prompt_efficacy_
# study.md:32) plus a standard, effect-agnostic DSP-parameter vocabulary. Every
# entry names a PARAMETER or a unit, never an effect category (those come from
# each effect's own `target`/`category`, not from a global list, so this
# section carries no risk of encoding one effect's leak into another's check).
PARAM_JARGON = [
    "cutoff", r"\bq\b", "feedback", "resonance", "resonant",
    "threshold", "ratio", "attack", "release", "decay time", "sustain",
    r"\bdb\b", "dbfs", r"\bhz\b", r"\bkhz\b", r"\bms\b", "millisecond",
    "wet/dry", "wet-dry", "dry/wet",
    "envelope", r"\blfo\b", "modulation depth", "sidechain",
    "slope", r"\bdb/oct\b", "octave", "semitone", "cents",
]
# Deliberately NOT jargon, tried and reverted 2026-09-23: "knob", "slider",
# "parameter", bare "mix". The rule's own worked examples (cutoff/Q/feedback,
# docs/prompt_efficacy_study.md:32) are all technical audio-parameter terms;
# "knob"/"slider" are the ordinary words a genuine L2 "casual musician" prompt
# is written to use ("a simple volume knob"), and flagging them fired on 9/25
# effects, unanimously on prompts that are exactly what the tier is supposed to
# sound like -- an over-broad lexicon, not a real leak. Bare "mix" is ambiguous
# between "my mix" (the song) and a wet/dry control; only the unambiguous
# wet/dry spelling stays.

# Effect-agnostic filler that would otherwise fire on every `target` word
# ("with", "for", "a", the connective tissue of an English noun phrase).
STOPWORDS = {
    "a", "an", "the", "with", "for", "and", "or", "of", "to", "in", "on",
    "control", "adjustable", "stereo", "mono", "simple",
}


def _category_words(effect: dict) -> set[str]:
    """Content words derived from an effect's OWN category + target — the
    only per-effect source, so one effect's leak list can never be tuned by
    another effect's tier text."""
    text = f"{effect['category']} {effect['target']}"
    words = re.findall(r"[a-zA-Z][a-zA-Z\-]+", text.lower())
    return {w for w in words if w not in STOPWORDS and len(w) > 2}


def _find_matches(text: str, terms: set[str] | list[str]) -> list[str]:
    lower = text.lower()
    hits = []
    for term in terms:
        pattern = term if term.startswith(r"\b") or "/" in term else re.escape(term)
        if re.search(pattern, lower):
            hits.append(term)
    return hits


def screen_effect(effect: dict) -> dict:
    """Check one effect's L1/L2/L0 prompts against its own tier rules."""
    cat_words = _category_words(effect)
    tiers = effect["tiers"]

    l1_category_hits = _find_matches(tiers["L1"], cat_words)
    l1_jargon_hits = _find_matches(tiers["L1"], PARAM_JARGON)
    l1_numeric = bool(re.search(r"\d", tiers["L1"]))

    l2_jargon_hits = _find_matches(tiers["L2"], PARAM_JARGON)

    l0_category_hits = _find_matches(tiers["L0"], cat_words)

    return {
        "effect_id": effect["effect_id"],
        "l1_category_hits": l1_category_hits,
        "l1_jargon_hits": l1_jargon_hits,
        "l1_has_digit": l1_numeric,
        "l1_violation": bool(l1_category_hits or l1_jargon_hits or l1_numeric),
        "l2_jargon_hits": l2_jargon_hits,
        "l2_violation": bool(l2_jargon_hits),
        # Evidence only -- see module docstring. Never folded into *_violation.
        "l0_category_hits_evidence_only": l0_category_hits,
    }


def screen_dataset(dataset: dict) -> list[dict]:
    return [screen_effect(e) for e in dataset["effects"]]


def print_report(results: list[dict]) -> int:
    l1_violations = [r for r in results if r["l1_violation"]]
    l2_violations = [r for r in results if r["l2_violation"]]
    l0_evidence = [r for r in results if r["l0_category_hits_evidence_only"]]

    print(f"Screened {len(results)} effects against their own tier rules.\n")
    print(f"L1 (must name NO category/parameter/number): "
          f"{len(l1_violations)}/{len(results)} violate")
    for r in l1_violations:
        why = ", ".join(filter(None, [
            f"category:{r['l1_category_hits']}" if r["l1_category_hits"] else "",
            f"jargon:{r['l1_jargon_hits']}" if r["l1_jargon_hits"] else "",
            "digit" if r["l1_has_digit"] else "",
        ]))
        print(f"  {r['effect_id']}: {why}")

    print(f"\nL2 (must carry NO parameter jargon): "
          f"{len(l2_violations)}/{len(results)} violate")
    for r in l2_violations:
        print(f"  {r['effect_id']}: {r['l2_jargon_hits']}")

    print(f"\nL0 category-word matches (EVIDENCE ONLY, not a violation count -- "
          f"L0's rule is 'no mechanical description', not 'no category word'; "
          f"a gear reference can legitimately name the thing): "
          f"{len(l0_evidence)}/{len(results)}")
    for r in l0_evidence:
        print(f"  {r['effect_id']}: {r['l0_category_hits_evidence_only']}")

    return 1 if (l1_violations or l2_violations) else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompts", type=Path, default=DEFAULT_PROMPTS_FILE)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    dataset = json.loads(a.prompts.read_text(encoding="utf-8"))
    results = screen_dataset(dataset)
    if a.json:
        print(json.dumps(results, indent=2))
        return 1 if any(r["l1_violation"] or r["l2_violation"] for r in results) else 0
    return print_report(results)


if __name__ == "__main__":
    sys.exit(main())
