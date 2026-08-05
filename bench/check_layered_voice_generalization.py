#!/usr/bin/env python3
"""
Layered-voice control-exposure generalization study (docs/sessions/006-briefs/
p3-layered-voice-evidence.md).

STATUS.md's "Assumed, never checked" list carries: "the layered-voice
control-exposure prompt fix generalizes beyond the 2 prompts it was tested on."
The fix is commit bc061ff ("instrument_prompt.txt: a layered voice needs its own
control, always"), verified there on exactly 2 prompts. This script runs a NEW,
independently-sampled batch of layered-voice instrument prompts against a free
LLM provider and mechanically checks whether the fix's rule held.

THE MECHANICAL DEFINITION OF "EXPOSED", read from bc061ff's diff (not guessed
from the commit message):

    LAYERED VOICES -- every additional sound source needs its own control:
    If you add a second oscillator, an octave doubler, a sub-oscillator, or any
    other layer beyond the primary voice, give it its own hslider so the player
    can adjust or silence it -- an amount/mix control (0 to 1) or a detune
    amount, whichever fits. NEVER sum a second source into process
    unconditionally with no parameter governing its presence.

The added few-shot in that same commit shows the shape the rule expects:
    mainOsc = os.sawtooth(freq);
    octaveOsc = os.sawtooth(freq * 0.5);
    octaveMix = hslider("OctaveMix", 0.5, 0, 1, 0.01) : si.smoo;
    voice = mainOsc + (octaveOsc * octaveMix);

i.e. every oscillator/noise "layer" statement beyond the first must have a
declared hslider/vslider/nentry parameter that co-occurs with it (governing its
presence or its detune) and does NOT also co-occur with the primary layer (a
param shared across the whole voice, like `freq` or `gain`, does not count --
the rule is about the ADDED layer getting its OWN control).

MECHANICS (per llm/CONTRACT.md):
  - Free-only. Provider is pinned to "groq" by default and asserted to be one
    of the free-tier entries in llm/providers.py -- CONTRACT.md is explicit
    that generate.py's free-only guard lives ONLY in its __main__, not in
    generate_faust/generate_with_retry, so a library caller (this script) must
    enforce it itself. PLUGINFORGE_ALLOW_PAID is never read or set here.
  - Every attempt (including retries) pins kind=router.INSTRUMENT explicitly,
    per generate_faust's own docstring warning against re-routing mid-retry.
  - Takes bench/run_benchmark.py's quota lock for the duration of the run, the
    same lock score_efficacy.py and run_efficacy_study.py already share
    (PF-025/PF-030) -- one lock, every harness that spends provider quota.
  - Writes ONLY to a new bench/results/layered_voice_generalization_*.json.
    Never touches bench/results/.prompt_baseline.json.

Usage:
    python bench/check_layered_voice_generalization.py               # full run
    python bench/check_layered_voice_generalization.py --dry-run     # 1 prompt
    python bench/check_layered_voice_generalization.py --provider groq
"""
import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
RESULTS_DIR = BENCH_DIR / "results"

# Same lock every quota-spending harness in this tree shares (PF-025/PF-030).
# Imported lazily-but-at-module-scope, matching run_efficacy_study.py:56-60 and
# score_efficacy.py's own comment pointing at that precedent.
sys.path.insert(0, str(BENCH_DIR))
from run_benchmark import (  # noqa: E402
    BenchmarkLockHeld, acquire_lock, release_lock,
)

sys.path.insert(0, str(BENCH_DIR.parent / "llm"))
import providers  # noqa: E402
import router  # noqa: E402
import generate  # noqa: E402

# ── Free-only guard (ADR-012) ────────────────────────────────────────────────
# generate.py's __main__ enforces this; nothing does for a library import. This
# script is a library caller, so it enforces it itself, once, before any call.
FREE_PROVIDERS = {name for name, spec in providers.PROVIDERS.items() if spec.free}
DEFAULT_PROVIDER = "groq"  # matches bc061ff's own verification method


def assert_free(provider: str) -> None:
    if provider not in FREE_PROVIDERS:
        raise SystemExit(
            f"Refusing provider {provider!r}: not in the free-tier set "
            f"{sorted(FREE_PROVIDERS)} (ADR-012). This study is $0 by design; "
            f"PLUGINFORGE_ALLOW_PAID is never read here."
        )


# ── The 10 new prompts ───────────────────────────────────────────────────────
# Deliberately distinct from bc061ff's own verification prompts: that commit's
# few-shot is literally "a subtractive synth with an octave-down doubler" (now
# baked into instrument_prompt.txt as a worked example, so reusing it or a close
# paraphrase would test memorization of the example, not generalization of the
# rule). These vary both the primary voice (pad/bell/pluck/bass/organ/lead/
# brass/chiptune) and the added-layer type (detune/unison, sub-osc, noise
# texture, octave-up, drawbar harmonic, ring-mod, fifth harmony) on purpose, so
# a narrowed claim (if warranted) has real axes to narrow along.
PROMPTS = [
    "a pad synth with a detuned unison layer for a wider sound",
    "an FM bell with a sub-oscillator an octave below the carrier",
    "a plucked string patch layered with a soft noise breath layer",
    "a bass synth with a second square-wave layer one octave up for bite",
    "an organ voice with a drawbar-style second harmonic layer",
    "a lead synth with a detuned second saw layer for a supersaw effect",
    "a bell patch with a ring-modulated second layer for metallic overtones",
    "a brass synth with a sub-bass layer doubling the root note",
    "a chiptune lead with a pulse-wave harmony layer a fifth above",
    "a pad with an added noise texture layer for airiness",
]

# ── Mechanical "control exposed" check ───────────────────────────────────────
# Faust is one-statement-per-`;`, single-assignment. Split on `;` and work on
# whole statements rather than lines, since a definition can wrap lines.
CONTROL_RE = re.compile(r'^\s*(\w+)\s*=\s*(?:hslider|vslider|nentry)\s*\(')

# Oscillator/noise-generator primitives that plausibly build an audible "layer"
# per the rule's own wording ("a second oscillator, an octave doubler, a
# sub-oscillator, or any other layer"). Deliberately EXCLUDES os.lf_* (those are
# modulation-rate LFOs, not summed audio layers -- a vibrato LFO does not need
# its own mix knob under this rule) and en.*/fi.* (envelopes/filters, not
# sources). Two shapes, because they differ in Faust: most take an argument list
# (`os.sawtooth(freq)`), but `no.noise` is the one documented primitive
# (instrument_prompt.txt's stdlib block) with NO arguments at all -- it is
# referenced bare, e.g. `crack = no.noise : fi.resonlp(...)`.
LAYER_PRIMITIVE_CALL = (
    r"(?:os\.(?:osc|osci|sawtooth|saw2|square|triangle|pulsetrain|imptrain|"
    r"additivesaw|additivesquare|additivetriangle|impulse)"
    r"|pm\.\w+)"
)
_PRIMITIVE_ANCHOR_CALL = re.compile(rf'^\s*{LAYER_PRIMITIVE_CALL}\s*\(')
_PRIMITIVE_ANCHOR_NOARG = re.compile(r'^\s*no\.noise\b')

# Only these LHS names are eligible for the SINGLE-STATEMENT / INLINE detection
# path below (see analyze()'s docstring, shape (b)). Deliberately narrow: this
# path recognizes "os.<primitive>(...)" ANYWHERE inside an additive term, and
# without a name restriction that would also fire on a modulation expression
# that happens to use an audio-rate-shaped primitive for something that is NOT
# a summed voice layer, e.g. `cutoff = 1000 + os.osc(0.5)*200;` (an LFO driving
# a filter cutoff, not a second sound source). Every real "layers summed
# together" statement observed in this project's own few-shot (bc061ff) and in
# this study's own generations names its output one of these; a modulation
# target does not.
_SUM_OUTPUT_NAMES = {"process", "voice", "sig", "signal", "out", "output", "sum"}


def _extract_grouped_and_trailer(text: str) -> tuple[str, str]:
    """If `text` (stripped) is wrapped in one balanced leading '(' ... ')',
    return (content-between-those-parens, everything-after-the-close-paren).
    Otherwise return (text, "").

    This is what keeps a layer-specific control from being credited to a
    control that actually applies to the WHOLE summed group -- in
    `(a + b*mix) : *(gain)`, `gain` sits OUTSIDE the matching close paren and
    must not look like it belongs to `b` just because it is textually close.
    """
    s = text.strip()
    if not s.startswith("("):
        return s, ""
    depth = 0
    for i, ch in enumerate(s):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return s[1:i], s[i + 1:]
    return s, ""  # unbalanced -- treat conservatively as ungrouped


def _split_top_level_plus(expr: str) -> list[str]:
    """Split on '+' at paren-depth 0 only (a Faust sum of layers is written at
    the top level of one expression, e.g. `mainOsc + (octaveOsc * mix)`)."""
    parts, depth, current = [], 0, []
    for ch in expr:
        if ch == "(":
            depth += 1
            current.append(ch)
        elif ch == ")":
            depth -= 1
            current.append(ch)
        elif ch == "+" and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    parts.append("".join(current))
    return parts


def _term_is_primitive_anchored(term: str) -> bool:
    """Does this additive term's own content -- after peeling at most one
    layer of its OWN wrapping parens -- begin directly with a layer-generating
    primitive? Used for both statement shapes analyze() recognizes."""
    inner, _ = _extract_grouped_and_trailer(term)
    return bool(_PRIMITIVE_ANCHOR_CALL.match(inner) or _PRIMITIVE_ANCHOR_NOARG.match(inner))


def _bare_term_var(term: str) -> str | None:
    """If this additive term is nothing but a bare variable reference (after
    peeling at most one wrapping paren) -- e.g. the `unisonOsc` in `voice =
    mainOsc + unisonOsc;` -- return that name, else None."""
    inner, _ = _extract_grouped_and_trailer(term)
    inner = inner.strip()
    return inner if re.match(r"^\w+$", inner) else None


def _find_consumed_whole(statements: list[str]) -> set[str]:
    """Variable names that appear elsewhere as a BARE top-level additive term
    (not inside a function argument list, not as part of a larger
    expression). A variable consumed this way is being treated by the rest of
    the program as ONE atomic layer, however it is built internally -- e.g.
    `unisonOsc = (os.sawtooth(freq+detune) + os.sawtooth(freq-detune)) *
    unisonMix;` is itself a sum of two primitives, but `voice = mainOsc +
    unisonOsc;` treats the whole thing as a single term. Without this check,
    analyze() would wrongly try to split unisonOsc's OWN definition into two
    separate layer occurrences instead of recognizing it as one composite
    layer with one dedicated control (`unisonMix`)."""
    consumed = set()
    for stmt in statements:
        s = stmt.strip()
        m = re.match(r"^(\w+)\s*=\s*(.*)$", s, re.DOTALL)
        if not m:
            continue
        grouped, _trailer = _extract_grouped_and_trailer(m.group(2))
        for t in _split_top_level_plus(grouped):
            bare = _bare_term_var(t)
            if bare:
                consumed.add(bare)
    return consumed


def analyze(faust_code: str) -> dict:
    """Mechanically checks bc061ff's rule against one generated Faust program.

    Detects layers two ways, because real generations use both shapes:

    (a) MULTI-STATEMENT -- each layer gets its own named variable, bc061ff's
        own few-shot shape: `mainOsc = os.sawtooth(freq); octaveOsc =
        os.sawtooth(freq*0.5); voice = mainOsc + (octaveOsc*octaveMix);`.
        Control search follows the variable's token across every statement
        that references it, scoped to the specific top-level additive term it
        appears in there -- so a control shared with another layer in the SAME
        combining statement is not double-counted as "dedicated" to both.

    (a2) COMPOSITE NAMED LAYER -- a named variable whose OWN definition sums
        multiple primitives, but which is itself later referenced as a bare
        term elsewhere (`unisonOsc = (os.sawtooth(freq+detune) +
        os.sawtooth(freq-detune)) * unisonMix; voice = mainOsc + unisonOsc;`,
        observed live in this study's own groq output). Collapsed to ONE
        named occurrence rather than split -- see `_find_consumed_whole`.

    (b) SINGLE-STATEMENT / INLINE -- multiple layers appear in one expression
        with NO intermediate variable at all AND the LHS is never referenced
        elsewhere as a bare term, e.g. `process = (os.sawtooth(freq) +
        (os.sawtooth(freq*0.5)*subMix)) : fi.resonlp(...) : *(gain*env);`
        (also observed live in this study's own groq output). Each additive
        term that is independently primitive-anchored becomes its own layer
        occurrence, named `<var>#<index>`; control search is local to that
        term's own text, with any trailing chain OUTSIDE the term's matching
        close paren excluded (it applies to the whole sum, not to one layer).
        Restricted to `_SUM_OUTPUT_NAMES` -- see that constant's comment for
        why an unrestricted version over-fires on modulation expressions
        (e.g. `cutoff = 1000 + os.osc(0.5)*200;`, an LFO, not a voice layer).

    Returns a dict with `status` in:
      - "pass"                 -- >=1 added layer, each has a dedicated control
      - "fail_missing_control" -- >=1 added layer, at least one has none
      - "no_added_layer"       -- exactly one layer detected (nothing to check
                                   against the rule -- the model gave a single
                                   voice, not a layered one; recorded but not a
                                   pass, since the prompt asked for a layer)
      - "no_layer_detected"    -- the detector's primitive list found nothing
                                   (a real single-voice patch, or a layer built
                                   from a primitive outside the curated list --
                                   this detector's known blind spot, flagged
                                   rather than silently mis-scored)
    """
    statements = [s for s in faust_code.split(";")]

    control_vars: set[str] = set()
    for stmt in statements:
        m = CONTROL_RE.match(stmt.strip())
        if m:
            control_vars.add(m.group(1))

    consumed_whole = _find_consumed_whole(statements)

    # Ordered occurrences: {"name", "kind" ("named"|"inline"), "payload"}.
    #   named  -> payload is the variable name (searched across ALL statements)
    #   inline -> payload is this one term's own text (searched locally only)
    occurrences: list[dict] = []

    for stmt in statements:
        s = stmt.strip()
        m = re.match(r"^(\w+)\s*=\s*(.*)$", s, re.DOTALL)
        if not m:
            continue
        var_name, rhs = m.group(1), m.group(2)
        grouped, _trailer = _extract_grouped_and_trailer(rhs)
        terms = _split_top_level_plus(grouped)

        if len(terms) == 1:
            # `var = <primitive>(...)`, optionally chained -- shape (a).
            if _term_is_primitive_anchored(terms[0]) and "lfo" not in var_name.lower():
                occurrences.append({"name": var_name, "kind": "named", "payload": var_name})
        elif var_name in consumed_whole:
            # Multiple additive terms, but referenced elsewhere as ONE bare
            # term -- shape (a2): a composite layer, not several separate ones.
            if any(_term_is_primitive_anchored(t) for t in terms) and "lfo" not in var_name.lower():
                occurrences.append({"name": var_name, "kind": "named", "payload": var_name})
        elif var_name.lower() in _SUM_OUTPUT_NAMES:
            # Multiple additive terms, LHS is a recognized sum-output name,
            # and NOTHING references it as a single atomic term -- shape (b).
            for j, t in enumerate(terms):
                if _term_is_primitive_anchored(t):
                    label = var_name if j == 0 else f"{var_name}#{j}"
                    occurrences.append({"name": label, "kind": "inline", "payload": t})

    if not occurrences:
        return {"status": "no_layer_detected", "layer_vars": [], "control_vars": sorted(control_vars),
                "detail": []}

    if len(occurrences) == 1:
        return {"status": "no_added_layer", "layer_vars": [occurrences[0]["name"]],
                "control_vars": sorted(control_vars), "detail": []}

    def controls_in_text(text: str) -> set[str]:
        return {c for c in control_vars if re.search(rf"\b{re.escape(c)}\b", text)}

    def named_var_controls(var: str) -> set[str]:
        """KNOWN LIMITATION, found and left in place 2026-08-05: this only
        splits each RAW statement by ONE level of top-level '+' -- it does not
        recursively descend into a nested group to find an inner top-level
        '+' the way `_extract_grouped_and_trailer` + `_split_top_level_plus`
        do together for the inline-layer path above. So a statement shaped
        `process = (pad + air * noiseMix) * (gain * env);` -- a per-layer
        multiply (`air * noiseMix`) sharing an OUTER wrapping paren with a
        genuinely shared post-sum multiply (`* (gain * env)`) -- puts `pad`
        and `air` in the SAME undivided chunk, so `noiseMix` gets credited to
        BOTH and then cancelled out of `air`'s dedicated set by the primary-
        subtraction below, even though it should stay dedicated to `air`.
        Verified conservative, not dangerous: this can only make a genuinely
        dedicated control disappear (pushing a verdict toward
        fail_missing_control), never manufacture one that is not there
        (pushing toward a false pass). Confirmed live on this study's own
        record 10 ("a pad with an added noise texture layer") -- `noiseMix`
        was lost this way, but the record still correctly scored `pass`
        because `air`'s own filter params (`cutoff`, `q`) are untouched by
        `pad` and carried the verdict on their own. Left unfixed because
        fixing it is a straightforward but non-trivial rewrite of this
        function to reuse the grouped/trailer logic recursively, and this
        script found zero fail_missing_control records to be suspicious of in
        its first real run -- a bias that only creates false negatives has no
        way to have produced this run's 10/10.
        """
        found = set()
        var_pat = re.compile(rf"\b{re.escape(var)}\b")
        for stmt in statements:
            for chunk in _split_top_level_plus(stmt):
                if var_pat.search(chunk):
                    found |= controls_in_text(chunk)
        return found

    def occurrence_controls(occ: dict) -> set[str]:
        if occ["kind"] == "named":
            return named_var_controls(occ["payload"])
        return controls_in_text(occ["payload"])

    primary = occurrences[0]
    primary_controls = occurrence_controls(primary)

    detail = []
    all_dedicated = True
    for occ in occurrences[1:]:
        co = occurrence_controls(occ)
        dedicated = sorted(co - primary_controls)
        ok = len(dedicated) > 0
        all_dedicated = all_dedicated and ok
        detail.append({"layer_var": occ["name"], "dedicated_controls": dedicated, "has_own_control": ok})

    status = "pass" if all_dedicated else "fail_missing_control"
    return {"status": status, "layer_vars": [o["name"] for o in occurrences],
            "control_vars": sorted(control_vars), "detail": detail}


# ── Generation, product code path, kind pinned ───────────────────────────────

def generate_instrument_with_retry(user_prompt: str, provider: str, model: str,
                                   max_retries: int = 3) -> tuple[str | None, list[dict]]:
    """Mirrors generate.generate_with_retry, but pins kind=router.INSTRUMENT on
    every attempt (generate_faust's docstring: re-routing mid-retry on repair
    text would let a retry silently switch prompts). generate_with_retry itself
    takes no `kind` param, so this is a thin reimplementation rather than a
    wrapper.
    """
    error_ctx = ""
    truncated = False
    budget = generate.generation_budget()
    attempts_log: list[dict] = []
    for attempt in range(1, max_retries + 1):
        try:
            code = generate.generate_faust(
                user_prompt, error_ctx, provider=provider, model=model,
                budget=budget, truncated=truncated, kind=router.INSTRUMENT,
            )
        except providers.OutputTruncated as exc:
            attempts_log.append({"attempt": attempt, "outcome": "truncated", "detail": str(exc)[:300]})
            truncated, error_ctx = True, ""
            continue
        except (providers.RateLimited, providers.BudgetExhausted) as exc:
            attempts_log.append({"attempt": attempt, "outcome": "provider_error",
                                 "detail": f"{type(exc).__name__}: {exc}"[:300]})
            return None, attempts_log
        truncated = False
        valid, error = generate.validate_faust(code, budget)
        if valid:
            attempts_log.append({"attempt": attempt, "outcome": "compiled"})
            return code, attempts_log
        attempts_log.append({"attempt": attempt, "outcome": "compile_error", "detail": error[:500]})
        error_ctx = error
    return None, attempts_log


def run_study(prompts: list[str], provider: str, model: str) -> dict:
    records = []
    for i, prompt in enumerate(prompts, 1):
        print(f"[{i}/{len(prompts)}] {prompt!r} ...", file=sys.stderr)
        code, attempts_log = generate_instrument_with_retry(prompt, provider, model)
        if code is None:
            record = {
                "prompt": prompt, "compiled": False, "faust_code": None,
                "attempts": attempts_log, "analysis": None,
                "pass": False, "status": "fail_no_valid_generation",
            }
            print(f"    -> FAIL (no valid generation; {attempts_log[-1]['outcome'] if attempts_log else 'unknown'})",
                  file=sys.stderr)
        else:
            analysis = analyze(code)
            passed = analysis["status"] == "pass"
            record = {
                "prompt": prompt, "compiled": True, "faust_code": code,
                "attempts": attempts_log, "analysis": analysis,
                "pass": passed, "status": analysis["status"],
            }
            print(f"    -> {analysis['status'].upper()} "
                  f"(layers={analysis['layer_vars']})", file=sys.stderr)
        records.append(record)
    return {
        "brief": "docs/sessions/006-briefs/p3-layered-voice-evidence.md",
        "claim_under_test": (
            "the layered-voice control-exposure prompt fix (bc061ff) generalizes "
            "beyond the 2 prompts it was tested on"
        ),
        "provider": provider,
        "model": model,
        "kind_pinned": router.INSTRUMENT,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "n_prompts": len(prompts),
        "n_passed": sum(1 for r in records if r["pass"]),
        "status_counts": {
            s: sum(1 for r in records if r["status"] == s)
            for s in sorted({r["status"] for r in records})
        },
        "records": records,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--provider", default=DEFAULT_PROVIDER,
                    help=f"free-tier provider name (default: {DEFAULT_PROVIDER})")
    ap.add_argument("--model", default=None, help="override the provider's default model")
    ap.add_argument("--dry-run", action="store_true", help="run only the first prompt")
    ap.add_argument("--out", default=None, help="output path override")
    args = ap.parse_args()

    assert_free(args.provider)
    model = providers.resolve_model(args.provider, args.model)

    err = providers.check_credentials(args.provider)
    if err:
        raise SystemExit(f"Preflight failed: {err}")

    prompts = PROMPTS[:1] if args.dry_run else PROMPTS

    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        raise SystemExit(str(exc)) from None

    try:
        result = run_study(prompts, args.provider, model)
    finally:
        release_lock()

    if args.out:
        out_path = Path(args.out)
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        suffix = "_dryrun" if args.dry_run else ""
        out_path = RESULTS_DIR / f"layered_voice_generalization_{stamp}{suffix}.json"

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2))

    n, m = result["n_prompts"], result["n_passed"]
    print(f"\n{m}/{n} passed (control properly exposed on every added layer).", file=sys.stderr)
    print(f"Status breakdown: {result['status_counts']}", file=sys.stderr)
    print(f"Written to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
