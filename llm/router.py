#!/usr/bin/env python3
"""Decide whether a user prompt asks for an INSTRUMENT or an EFFECT.

WHY THIS EXISTS. The two prompts cannot be one prompt. `system_prompt.txt` is
12,006 chars against groq's `prompt_tokens + max_tokens <= 8000` ceiling with
`max_tokens` floored at 4096, which leaves roughly 300 tokens of slack -- and the
voice contract, the instrument shape rules and a synth few-shot do not fit in 300
tokens. Splitting also *buys* budget rather than spending it: the ceiling is
PER REQUEST, so two prompts are two independent 8,000-token budgets, and the
instrument prompt drops the delay/reverb/modulation vocabulary it does not need.

WHY IT IS NOT AN LLM CALL. A classifier request would be a third round trip inside
a 100-second total budget (llm/generate.py:73) on a provider with a ~20-request
daily free tier. It would also fail exactly when the network is the problem.
This is keyword scoring: no network, no quota, microseconds, and deterministic --
which means it is testable, and a misroute is reproducible rather than a coin flip.

HOW A MISROUTE FAILS. Deliberately loudly, in both directions:
  - an EFFECT routed to the instrument prompt gets a `gate`, so the host detects
    the voice contract and the patch is silent until a note arrives. The user
    hears nothing and says so.
  - an INSTRUMENT routed to the effects prompt gets today's behaviour exactly --
    a patch that reads the input channel -- which is the status quo, not a
    regression.
Neither is silent-wrong, and the second is why `effect` is the default: an
unrecognised prompt behaves precisely as it did before this module existed.
"""
import re

INSTRUMENT = "instrument"
EFFECT = "effect"

# Words that only make sense when the user wants something PLAYABLE.
#
# Scored, not matched-once, because single keywords are ambiguous in both
# directions: "bass" appears in "bass boost" (an effect) as often as in "bass
# patch", and "note" appears in "note the resonance". Requiring the instrument
# side to out-score the effect side is what keeps those from flipping the route.
_INSTRUMENT_TERMS = (
    r"synth(esi[sz]er|esi[sz]ed)?", r"instrument", r"playable", r"polyphonic",
    r"monophonic", r"midi", r"keyboard", r"\bkeys?\b", r"\bnotes?\b", r"\bplay(ed|ing)?\b",
    r"\bpad\b", r"\blead\b", r"\bpluck(ed)?\b", r"\bbell\b", r"\borgan\b", r"\bpiano\b",
    r"\bvoice[sd]?\b", r"arpegg", r"chord", r"octave", r"subtractive", r"wavetable",
    r"additive", r"karplus", r"\bfm\b", r"\badsr\b", r"envelope", r"\bpatch\b",
    r"\bkick\b", r"\bsnare\b", r"\bdrum\b", r"\bpercussi", r"\bstab\b", r"\bdrone\b",
)

# Words that mean "process incoming audio". These are the existing corpus.
_EFFECT_TERMS = (
    r"\breverb\b", r"\bdelay\b", r"\becho\b", r"\bchorus\b", r"\bflanger\b",
    r"\bphaser\b", r"\bcompress", r"\blimiter\b", r"\bgate\b", r"\bexpander\b",
    r"\bdistort", r"\boverdrive\b", r"\bfuzz\b", r"\bsaturat", r"\bwavefold",
    r"\beq\b", r"\bequali[sz]er\b", r"\bshelf\b", r"\bnotch\b", r"\bband-?pass\b",
    r"\bhigh-?pass\b", r"\blow-?pass\b", r"\btremolo\b", r"\bvibrato\b",
    r"\bwidener?\b", r"\bstereo width\b", r"\bducker\b", r"\bde-?esser\b",
    r"\bpitch shift", r"\bbit ?crush", r"\bring ?mod", r"\bauto-?wah\b",
    r"\bincoming\b", r"\binput signal\b", r"\bdry/?wet\b", r"\bmix knob\b",
    # Generic processing nouns. `filter` earns its place: without it "a warm
    # analog filter for my synth bass" scored 1-0 for instrument on the word
    # "synth" alone -- the request names the thing being PROCESSED and the router
    # read it as the thing being BUILT. With it the score ties, and a tie is an
    # effect. A genuine synth request still wins, because "a subtractive synth
    # with a resonant filter" carries two instrument words against the one.
    r"\bfilter\b", r"\bboost\b", r"\bcut\b",
)

# Sources are the strongest instrument signal EXCEPT when they are modulators.
# "an oscillator" is a synth; "an LFO-modulated filter" is an effect whose LFO is
# an oscillator. The exclusion is what stops every tremolo from routing to the
# instrument prompt.
_SOURCE_TERMS = (r"\boscillator\b", r"\bsawtooth\b", r"\bsquare wave\b",
                 r"\bsine wave\b", r"\btriangle wave\b", r"\bwhite noise\b",
                 r"\bnoise generator\b", r"\bgenerator\b")
_MODULATOR_CONTEXT = (r"\blfo\b", r"\bmodulat", r"\bsweep", r"\bwobble\b")


def _score(text: str, terms) -> int:
    return sum(1 for t in terms if re.search(t, text))


def classify(prompt: str) -> str:
    """Return INSTRUMENT or EFFECT. Never raises; EFFECT is the safe default."""
    if not prompt:
        return EFFECT

    text = prompt.lower()

    inst = _score(text, _INSTRUMENT_TERMS)
    fx = _score(text, _EFFECT_TERMS)

    # A sound source counts for the instrument side only when nothing frames it
    # as modulation.
    if _score(text, _SOURCE_TERMS) and not _score(text, _MODULATOR_CONTEXT):
        inst += _score(text, _SOURCE_TERMS)

    # Strict majority. A tie is NOT an instrument: "a warm analog filter for my
    # synth" names the thing being processed, not the thing being built, and the
    # existing corpus is full of that phrasing.
    return INSTRUMENT if inst > fx else EFFECT


def explain(prompt: str) -> dict:
    """Same decision plus the evidence, for logs and for debugging a misroute."""
    text = (prompt or "").lower()
    matched_src = [t for t in _SOURCE_TERMS if re.search(t, text)]
    modulator = bool(_score(text, _MODULATOR_CONTEXT))
    return {
        "kind": classify(prompt),
        "instrument_hits": [t for t in _INSTRUMENT_TERMS if re.search(t, text)],
        "effect_hits": [t for t in _EFFECT_TERMS if re.search(t, text)],
        "source_hits": [] if modulator else matched_src,
        "sources_read_as_modulators": modulator and bool(matched_src),
    }


def detect_target_mismatch(prompt: str, requested_kind: str) -> str | None:
    """Return the opposite target only for an evidenced, strict-score mismatch.

    Empty/ambiguous prompts remain allowed. This is intentionally stricter than
    classify()'s effect default: a safe default must never become a false block in
    the Synth target merely because the user used vocabulary outside our lists.
    """
    if requested_kind not in {INSTRUMENT, EFFECT}:
        return None
    evidence = explain(prompt)
    inst = len(evidence["instrument_hits"]) + len(evidence["source_hits"])
    fx = len(evidence["effect_hits"])
    if requested_kind == EFFECT and inst > fx and inst > 0:
        return INSTRUMENT
    if requested_kind == INSTRUMENT and fx > inst and fx > 0:
        return EFFECT
    return None
