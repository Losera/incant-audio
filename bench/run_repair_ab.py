#!/usr/bin/env python3
"""bench/run_repair_ab.py — paired A/B: does faust-rs feedback shorten the repair loop?

THE QUESTION (Losera/incant-audio#26)
    "Does the better error improve the write-DSP / compile / correct loop — does
    the LLM better understand what the corrected DSP has to be, so fewer retry
    steps are needed?"

DESIGN
    For each distinct failing Faust program in the corpus
    (bench/build_repair_corpus.py), run the product-style corrective loop TWICE
    from the identical starting point — same program, same compiler verdict —
    differing in one thing only:

        arm A (status quo) : feedback = raw C++ `faust` stderr
        arm B (faust-rs)   : feedback = bench/frs_check.render(...) — the stable
                             FRS code, concrete arities, source line:col + caret,
                             and the `help` remedy, with the box-expr noise dropped

    The repair model is fixed (qwen2.5-coder:3b, temperature=0 — verified
    deterministic) regardless of which model first produced the program, so the
    only variable is the feedback text. A small local model is deliberate: it is
    where compiler-feedback quality should matter most, and it is what the #26
    question ("can the LLM better understand what the corrected DSP has to be")
    is really about. Pairing removes the sampling variance two independent grid
    runs would carry.

PRIMARY METRIC
    attempts_to_green — corrective attempts to the first compiling program
    (None if never). Paired across arms. Secondary: repaired-within-2 rate, and
    the second-error identity (same class again = feedback didn't land; new class
    = it landed but the model broke something else).

Output: bench/results/repair_ab/repair_ab_<date>.json — a flat list, one record
per (program, arm), incrementally written; --resume skips done pairs.
"""
import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
sys.path.insert(0, str(BENCH_DIR.parent / "llm"))

import providers  # noqa: E402
from run_benchmark import (  # noqa: E402
    BenchmarkLockHeld, acquire_lock, release_lock,
)
from run_efficacy_study import (  # noqa: E402
    SYSTEM_PROMPT, make_generation_budget, validate_faust,
)
import error_classes  # noqa: E402
import frs_check  # noqa: E402

DEFAULT_OUT_DIR = BENCH_DIR / "results" / "repair_ab"
DEFAULT_REPAIR_MODEL = "qwen2.5-coder:3b"
CORRECTIVE_ATTEMPTS = 2          # product loop is 3 total = 1 initial + 2 corrective

# arm A: byte-for-byte the product / efficacy-harness wording (llm/generate.py:300).
ARM_A_TEMPLATE = "\n\nYour previous output had this compiler error — fix it:\n{feedback}"
# arms B and C: frs_check.render*() already end with "Fix this and re-emit the
# complete program.", so they only need a lead-in.
ARM_FRS_TEMPLATE = "\n\n{feedback}"

# The three feedback regimes.
#   A  raw C++ `faust` stderr (status quo)
#   B  frs_check.render()          — faust-rs full: code, arities, caret, notes, help
#   C  frs_check.render_minimal()  — faust-rs core: code, one-line message, caret only
ARMS = ("A", "B", "C")


def load_corpus(path: Path) -> list[dict]:
    """Distinct failing programs, first occurrence of each code_sha kept."""
    seen: set[str] = set()
    out: list[dict] = []
    for r in json.loads(path.read_text()):
        if r["compiles"] or r["code_sha"] in seen:
            continue
        seen.add(r["code_sha"])
        out.append(r)
    return out


def feedback_for(arm: str, code: str, cpp_stderr: str) -> tuple[str, str | None]:
    """(feedback_text, frs_primary_code) for the given arm and current program."""
    if arm == "A":
        return cpp_stderr, None
    res = frs_check.check(code)
    if res is None or res.ok:
        # faust-rs unavailable or (shouldn't happen) accepts it — fall back to
        # C++ stderr so a faust-rs arm is never emptier than arm A.
        return cpp_stderr, None
    renderer = frs_check.render_minimal if arm == "C" else frs_check.render
    return renderer(res, code), (res.codes[0] if res.codes else None)


def repair_loop(entry: dict, arm: str, generate, repair_model: str) -> dict:
    """One arm's corrective loop from `entry`'s failing program."""
    prompt = entry["prompt"]
    code = entry["code"]
    cpp_stderr = entry["cpp_stderr"]
    first_class = error_classes.classify_error(cpp_stderr)

    attempt_log: list[dict] = []
    repaired = False
    attempts_to_green: int | None = None

    for n in range(1, CORRECTIVE_ATTEMPTS + 1):
        feedback_text, frs_code = feedback_for(arm, code, cpp_stderr)
        template = ARM_A_TEMPLATE if arm == "A" else ARM_FRS_TEMPLATE
        user_message = prompt + template.format(feedback=feedback_text)

        started = time.monotonic()
        try:
            new_code = generate(user_message)
        except Exception as exc:  # noqa: BLE001
            attempt_log.append({"n": n, "error": f"{type(exc).__name__}: {exc}"[:300]})
            break
        ok, err = validate_faust(new_code)
        attempt_log.append({
            "n": n,
            "feedback_arm": arm,
            "feedback_code": frs_code,
            "feedback_text": feedback_text[:1200],
            "code": new_code,
            "code_sha": frs_check.sha(new_code),
            "cpp_ok": ok,
            "cpp_stderr": "" if ok else err,
            "cpp_error_class": None if ok else error_classes.classify_error(err),
            "wall_s": round(time.monotonic() - started, 2),
        })
        if ok:
            repaired = True
            attempts_to_green = n
            break
        code, cpp_stderr = new_code, err

    second_class = attempt_log[0]["cpp_error_class"] if attempt_log and "cpp_error_class" in attempt_log[0] else None
    return {
        "prompt_id": entry["prompt_id"],
        "category": entry["category"],
        "tier": entry["tier"],
        "corpus_config": entry["config"],
        "code_sha": entry["code_sha"],
        "first_error_class": first_class,
        "arm": arm,
        "repair_model": repair_model,
        "repaired": repaired,
        "attempts_to_green": attempts_to_green,
        "attempts_used": len(attempt_log),
        "second_error_class": second_class,
        "second_error_same_as_first": (second_class == first_class) if second_class else None,
        "attempt_log": attempt_log,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }


def stratified_sample(entries: list[dict], n: int) -> list[dict]:
    """First `n` programs, but keeping the first-error-class proportions of the
    full corpus (deterministic — take round-robin within class, class order by
    frequency). Small classes keep at least one."""
    from collections import Counter, defaultdict
    if n >= len(entries):
        return entries
    by_class: dict[str, list[dict]] = defaultdict(list)
    for e in entries:
        by_class[error_classes.classify_error(e["cpp_stderr"])].append(e)
    order = [c for c, _ in Counter(
        error_classes.classify_error(e["cpp_stderr"]) for e in entries).most_common()]
    out: list[dict] = []
    idx = 0
    while len(out) < n:
        progressed = False
        for c in order:
            if idx < len(by_class[c]):
                out.append(by_class[c][idx]); progressed = True
                if len(out) == n:
                    break
        idx += 1
        if not progressed:
            break
    return out


def run(corpus_path: Path, out_file: Path, limit: int | None, sample: int | None,
        arms: tuple[str, ...], repair_model: str, resume: bool, dry_run: bool) -> None:
    entries = load_corpus(corpus_path)
    if sample:
        entries = stratified_sample(entries, sample)
    elif limit:
        entries = entries[:limit]
    print(f"corpus: {len(entries)} distinct failing programs | arms {arms} | "
          f"repair model {repair_model}", file=sys.stderr)

    records: list[dict] = []
    done: set[tuple[str, str, str]] = set()
    if resume and out_file.exists():
        records = json.loads(out_file.read_text())
        done = {(r["code_sha"], r["arm"], r["repair_model"]) for r in records}
        print(f"resume: {len(records)} records, {len(done)} (program,arm,model) done",
              file=sys.stderr)

    tasks = [(e, arm) for e in entries for arm in arms
             if (e["code_sha"], arm, repair_model) not in done]
    if dry_run:
        tasks = tasks[:len(arms) * 2]
    print(f"{len(tasks)} (program, arm) repairs pending", file=sys.stderr)

    for i, (entry, arm) in enumerate(tasks, 1):
        # A FRESH generator (hence a fresh Budget) per (program, arm): one arm's
        # corrective loop is one "product generation", and its budget must not be
        # shared with the next — reusing one Budget expires every repair after the
        # first ~140s of cumulative wall time. Same rule as
        # run_efficacy_study.run_study's per-cell generator.
        generate = providers.make_generator(
            "ollama", system_prompt=SYSTEM_PROMPT, model=repair_model,
            temperature=0.0, max_tokens=providers.MAX_OUTPUT_TOKENS,
            budget=make_generation_budget())
        rec = repair_loop(entry, arm, generate, repair_model)
        records.append(rec)
        write(out_file, records)
        g = "GREEN@%d" % rec["attempts_to_green"] if rec["repaired"] else "no-fix"
        print(f"[{i:04d}/{len(tasks)}] {rec['prompt_id']:22s} arm {arm} "
              f"{rec['first_error_class']:18s} → {g}", file=sys.stderr)

    summarise(records, repair_model)


def write(out_file: Path, records: list[dict]) -> None:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(records, indent=2))


def summarise(records: list[dict], repair_model: str) -> None:
    """Quick stderr readout — each faust-rs arm vs arm A, for this model.
    The real stats (McNemar/Wilcoxon/per-class) are bench/score_repair_ab.py."""
    from collections import defaultdict
    recs = [r for r in records if r["repair_model"] == repair_model]
    by_sha: dict[str, dict[str, dict]] = defaultdict(dict)
    for r in recs:
        by_sha[r["code_sha"]][r["arm"]] = r
    arms_present = sorted({r["arm"] for r in recs})
    print(f"\n── {repair_model}: arms {arms_present} ──", file=sys.stderr)
    if "A" not in arms_present:
        return
    for arm in [a for a in arms_present if a != "A"]:
        paired = [(v["A"], v[arm]) for v in by_sha.values() if "A" in v and arm in v]
        if not paired:
            continue
        a_g = sum(1 for a, _ in paired if a["repaired"])
        x_g = sum(1 for _, x in paired if x["repaired"])
        x_only = sum(1 for a, x in paired if x["repaired"] and not a["repaired"])
        a_only = sum(1 for a, x in paired if a["repaired"] and not x["repaired"])
        print(f"  A vs {arm}  (n={len(paired)}):  repaired  A {a_g}  {arm} {x_g}   "
              f"| discordant  {arm}-only {x_only}, A-only {a_only}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--out", type=Path,
                    default=DEFAULT_OUT_DIR / f"repair_ab_{datetime.now():%Y%m%d}.json")
    ap.add_argument("--arms", default="A,B",
                    help="comma list from A,B,C (default A,B)")
    ap.add_argument("--repair-model", default=DEFAULT_REPAIR_MODEL)
    ap.add_argument("--limit", type=int, help="first N distinct programs only")
    ap.add_argument("--sample", type=int,
                    help="N distinct programs, class-proportional (for the slow 7B run)")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="a few repairs only")
    args = ap.parse_args()

    arms = tuple(a.strip().upper() for a in args.arms.split(","))
    if any(a not in ARMS for a in arms):
        print(f"[!] --arms must be from {ARMS}", file=sys.stderr)
        return 2
    if {"B", "C"} & set(arms) and frs_check.faust_rs_bin() is None:
        print("[!] faust-rs not found — arms B/C would collapse to arm A. "
              "Set PLUGINFORGE_FAUST_RS_BIN or put faust-rs on PATH.", file=sys.stderr)
        return 2
    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        print(f"[!] {exc}", file=sys.stderr)
        return 2
    try:
        run(args.corpus, args.out, args.limit, args.sample, arms,
            args.repair_model, args.resume, args.dry_run)
    finally:
        release_lock()
    print(f"\nwrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
