#!/usr/bin/env python3
"""bench/score_efficacy_render.py — render-level rescoring of an efficacy archive.

WHY THIS EXISTS (2026-09-23 adversarial review of the prompt-efficacy study,
docs/records/efficacy-adversarial-review-2026-09-23.md, finding B1/F2)

`bench/score_efficacy.py` reports "first-try compile rate" and "retry-corrected
rate" — both are `faust -lang cpp -> /dev/null` accept/reject, the front end's
opinion only. That is not efficacy. `faust` happily accepts a Karplus-Strong
patch that renders +79.6 dB runaway, DC offset 2.35, peak 4541
(docs/BUGS.md:828-832) — a real regression already on file, just never rolled
into a tier table. This module reuses the render oracle
(bench/render_oracle.py) — the repo's one instrument that measures whether
compiled output is actually safe audio — over an efficacy archive, broken out
by tier and category, and reports COMPILED vs COMPILED-AND-RENDERS-SAFELY side
by side.

SELECTION: first-try compiles OR retry-successes, not just first-try
    `render_oracle.corpus_main()` filters on `first_try_compiles` only — correct
    for its purpose (bench/ladder_corpus.json is a first-try-only frozen fixture)
    but WRONG for an efficacy archive, where `retry_success` records are exactly
    the ones the product ships to a user (the product's real success gate is
    retry-corrected, not first-try — CLAUDE.md's error-correction loop). Checked
    2026-09-23: on efficacy_groq_20260831.json, 21 of 125 records have
    `terminal_reason=="compiled"` and `first_try_compiles=False` — genuine
    retry rescues that `corpus_main`'s filter silently never renders. This
    module selects `first_try_compiles or retry_success`, so no compiled patch
    the product would actually run past its own retry loop escapes the safety
    gate. It calls `render_oracle.analyse_corpus()` — the ONE corpus driver
    (see that function's own docstring) — rather than re-deriving gate logic,
    which is exactly the duplication its docstring warns against.

CENSORING (adversarial review finding A7)
    A record whose LAST attempt was cut off by a rate limit
    (`terminal_reason == "rate_limited"`) never produced compiling code and is
    excluded from both the compile and the render denominator, not counted as
    a failure. `estimated_output_tokens` near the provider's cap is flagged
    (`--flag-truncation-frac`, default 0.97) as a soft warning, not an
    exclusion — this project has no hard truncation detector on estimated
    (not measured) token counts; see tests/test_truncation_detection.py for
    the one that exists on a different signal.

WHAT THIS DOES NOT DO
    - No LLM call, ever. Pure re-analysis of already-committed `code` fields.
    - Does not touch bench/ladder_corpus.json or check.sh audio's frozen
      corpus/exit-code contract.
    - Does not replace bench/score_efficacy.py's compile-rate tables — this is
      an additional, stricter view, meant to run alongside them.
    - Tail *expectations* are reported, never gated here either (matches
      render_oracle.corpus_main's EXIT POLICY comment) — this module's
      exit code reflects only the render safety gate (measurement.ok).

Usage:
    python bench/score_efficacy_render.py --results bench/results/efficacy/efficacy_groq_20260831.json
    python bench/score_efficacy_render.py --results FILE --prompts bench/prompts/tiered_prompts.json --pairwise
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))

import render_oracle  # noqa: E402
from frs_check import sha  # noqa: E402 — reuse the project's one content-hash convention
from score_repair_ab import mcnemar_exact  # noqa: E402 — reuse, don't reimplement

TIER_ORDER = ["L4", "L3", "L2", "L1", "L0"]

# Above this fraction of the provider's max_tokens, an estimated (not measured)
# output length is flagged as possibly truncated. Soft warning only — see
# module docstring's CENSORING note. 0.97 leaves headroom for legitimate
# generations that land close to the cap without tripping on every one.
DEFAULT_FLAG_TRUNCATION_FRAC = 0.97


def load_records(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def is_censored(rec: dict) -> bool:
    """A record with no chance to compile at all — exclude, don't fail (A7)."""
    return rec.get("terminal_reason") == "rate_limited" or not rec.get("code")


def is_compiled(rec: dict) -> bool:
    """Would the product have shipped this? First-try OR retry-rescued."""
    return bool(rec.get("first_try_compiles") or rec.get("retry_success"))


def possibly_truncated(rec: dict, max_tokens: int | None,
                       frac: float = DEFAULT_FLAG_TRUNCATION_FRAC) -> bool:
    if not max_tokens:
        return False
    est = rec.get("estimated_output_tokens")
    return bool(est and est >= frac * max_tokens)


def render_verdict(rec: dict) -> tuple[bool, str]:
    """(renders_safely, reason). Only called on a compiled record.

    Uncached — compiles and renders every call. Callers that visit the same
    record more than once (this module's own pairwise_tier_tests does, and so
    would score() + pairwise together) MUST go through cached_verdict() /
    a shared cache instead, or a 125-cell archive costs a render every time a
    tier pair shares an effect. See build_verdict_cache().
    """
    r = render_oracle.analyse_record(rec)
    if not r["rendered"]:
        if r.get("unsupported"):
            return None, "unsupported (0-input generator)"  # type: ignore[return-value]
        return False, r["error"]
    m = r["measurement"]
    return bool(m["ok"]), ("ok" if m["ok"] else "; ".join(m["reasons"]))


def build_verdict_cache(records: list[dict]) -> dict[str, tuple]:
    """Render every distinct compiled program ONCE, keyed by content hash.

    Without this, score() renders each compiled record once and
    pairwise_tier_tests() renders it again for every tier pair it appears in
    (up to 4 more times for a 5-tier grid) — a 125-cell archive went from "a
    few minutes" to "times out at 10 minutes" the first time this was run for
    real (2026-09-23), which is why this cache exists rather than an
    optimization left for later. Keyed on `code` text via bench/frs_check.sha
    — the project's one content-hash convention — not on id(rec), so two
    records that happen to carry byte-identical code (e.g. a retry that
    reproduced the first attempt) render only once.
    """
    cache: dict[str, tuple] = {}
    for rec in records:
        if is_censored(rec) or not is_compiled(rec):
            continue
        key = sha(rec["code"])
        if key not in cache:
            cache[key] = render_verdict(rec)
    return cache


def cached_verdict(rec: dict, cache: dict[str, tuple]) -> tuple[bool, str]:
    return cache[sha(rec["code"])]


def score(records: list[dict], *, max_tokens: int | None = None,
         cache: dict[str, tuple] | None = None) -> dict:
    """Per-tier and per-category: n, censored, compiled, rendered_safe, unsupported.

    `unsupported` (zero-input generators) is counted separately and excluded
    from the rendered_safe denominator — render_oracle structurally cannot
    judge them (UnsupportedPatch); counting them as failures would be a false
    claim in the other direction. See B4 in the adversarial review.

    `cache`: a build_verdict_cache() result. Built fresh from `records` if not
    given — but a caller also running pairwise_tier_tests() over the SAME
    records should build one cache and pass it to both, or every record renders
    twice.
    """
    cache = cache if cache is not None else build_verdict_cache(records)

    def _bucket() -> dict:
        # Zeroed up front, not defaultdict(int): a key an aggregation never
        # touches (e.g. "compiled" for a tier that was entirely rate-limited)
        # must still read as 0, not raise or silently vanish from a dict()
        # snapshot -- the same "absence must not read as a pass" discipline
        # render_oracle.py applies to its own gate fields.
        return {"n": 0, "censored": 0, "compiled": 0, "possibly_truncated": 0,
                "unsupported": 0, "rendered_safe": 0}

    by_tier: dict[str, dict] = {t: _bucket() for t in TIER_ORDER}
    by_category: dict[str, dict] = defaultdict(_bucket)
    details = []

    for rec in records:
        tier, cat = rec.get("tier", "?"), rec.get("category", "?")
        for bucket in (by_tier.setdefault(tier, _bucket()),
                       by_category[cat]):
            bucket["n"] += 1
        if is_censored(rec):
            for bucket in (by_tier[tier], by_category[cat]):
                bucket["censored"] += 1
            continue
        compiled = is_compiled(rec)
        trunc = possibly_truncated(rec, max_tokens)
        for bucket in (by_tier[tier], by_category[cat]):
            bucket["compiled"] += int(compiled)
            bucket["possibly_truncated"] += int(trunc)
        if not compiled:
            continue
        safe, reason = cached_verdict(rec, cache)
        if safe is None:
            for bucket in (by_tier[tier], by_category[cat]):
                bucket["unsupported"] += 1
            continue
        for bucket in (by_tier[tier], by_category[cat]):
            bucket["rendered_safe"] += int(safe)
        if not safe:
            details.append({"tier": tier, "category": cat,
                            "effect_id": rec.get("effect_id"),
                            "prompt": (rec.get("prompt") or "")[:70],
                            "reason": reason})

    return {"by_tier": {t: dict(v) for t, v in by_tier.items()},
            "by_category": {c: dict(v) for c, v in by_category.items()},
            "failures": details}


def pairwise_tier_tests(records: list[dict],
                        cache: dict[str, tuple] | None = None) -> list[dict]:
    """McNemar exact test on RENDERED-SAFE, paired by effect_id, for every tier
    pair. Reuses bench/score_repair_ab.mcnemar_exact rather than a second
    implementation (adversarial review A2: this is a matched-set design and
    was being read as independent binomials).

    `cache`: see score()'s docstring — pass the same build_verdict_cache()
    result if also calling score() on these records, or every compiled
    program renders again here.
    """
    cache = cache if cache is not None else build_verdict_cache(records)
    by_effect_tier: dict[tuple[str, str], dict] = {}
    for rec in records:
        if is_censored(rec):
            continue
        key = (rec.get("effect_id"), rec.get("tier"))
        by_effect_tier[key] = rec

    effects = sorted({eid for eid, _ in by_effect_tier})
    out = []
    for i, t1 in enumerate(TIER_ORDER):
        for t2 in TIER_ORDER[i + 1:]:
            pairs = []
            for eid in effects:
                r1, r2 = by_effect_tier.get((eid, t1)), by_effect_tier.get((eid, t2))
                if r1 is None or r2 is None:
                    continue
                c1, c2 = is_compiled(r1), is_compiled(r2)
                s1 = cached_verdict(r1, cache)[0] if c1 else False
                s2 = cached_verdict(r2, cache)[0] if c2 else False
                # unsupported (None) can't be judged either way; drop the pair
                if s1 is None or s2 is None:
                    continue
                pairs.append((bool(s1), bool(s2)))
            n = len(pairs)
            t1_only = sum(1 for a, b in pairs if a and not b)
            t2_only = sum(1 for a, b in pairs if b and not a)
            out.append({
                "tier_a": t1, "tier_b": t2, "n_pairs": n,
                "a_only": t1_only, "b_only": t2_only,
                "mcnemar_p": mcnemar_exact(t2_only, t1_only) if n else 1.0,
            })
    return out


def print_report(scores: dict, pairwise: list[dict] | None) -> None:
    print("Render-level rescoring (compiled vs compiled-AND-renders-safely)\n")
    print(f"{'tier':6} {'n':>4} {'censored':>9} {'compiled':>9} "
          f"{'unsupported':>11} {'render_safe':>11} {'trunc?':>7}")
    for t in TIER_ORDER:
        v = scores["by_tier"].get(t, {})
        if not v:
            continue
        n = v.get("n", 0)
        print(f"{t:6} {n:>4} {v.get('censored', 0):>9} {v.get('compiled', 0):>9} "
              f"{v.get('unsupported', 0):>11} {v.get('rendered_safe', 0):>11} "
              f"{v.get('possibly_truncated', 0):>7}")

    print("\nby category:")
    for c, v in sorted(scores["by_category"].items()):
        print(f"  {c:12} n={v.get('n',0):3} compiled={v.get('compiled',0):3} "
              f"render_safe={v.get('rendered_safe',0):3} "
              f"unsupported={v.get('unsupported',0):3}")

    if scores["failures"]:
        print(f"\n{len(scores['failures'])} compiled-but-unsafe patches:")
        for f in scores["failures"]:
            print(f"  [{f['tier']}/{f['category']}] {f['prompt']} -- {f['reason']}")

    if pairwise:
        print("\nPaired tier-vs-tier McNemar (render-safe, matched by effect_id):")
        for p in pairwise:
            flag = " *" if p["mcnemar_p"] < 0.05 and p["n_pairs"] else ""
            print(f"  {p['tier_a']} vs {p['tier_b']}: n={p['n_pairs']:3} "
                  f"{p['tier_a']}_only={p['a_only']:2} {p['tier_b']}_only={p['b_only']:2} "
                  f"p={p['mcnemar_p']:.4f}{flag}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", required=True, type=Path)
    ap.add_argument("--pairwise", action="store_true",
                    help="also run paired McNemar tests between every tier pair")
    ap.add_argument("--max-tokens", type=int, default=None,
                    help="provider's max_tokens, to flag likely-truncated records")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    records = load_records(a.results)
    cache = build_verdict_cache(records)   # one render per distinct program, ever
    print(f"rendering {len(cache)} distinct compiled programs...", file=sys.stderr)
    scores = score(records, max_tokens=a.max_tokens, cache=cache)
    pairwise = pairwise_tier_tests(records, cache=cache) if a.pairwise else None

    if a.json:
        print(json.dumps({"scores": scores, "pairwise": pairwise}, indent=2))
    else:
        print_report(scores, pairwise)

    any_unsafe = any(v.get("compiled", 0) - v.get("rendered_safe", 0) - v.get("unsupported", 0) > 0
                     for v in scores["by_tier"].values())
    return 1 if any_unsafe else 0


if __name__ == "__main__":
    sys.exit(main())
