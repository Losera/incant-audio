#!/usr/bin/env python3
"""bench/repair_ab_core.py — the issue-#26 repair-loop A/B, minus the plumbing.

Extracted from bench/run_repair_ab.py (2026-09-01) so the paired corrective
loop has ONE implementation, shared by:

  * bench/run_repair_ab.py            — the canonical in-repo harness (ollama via
                                        llm/providers.py, the shared bench lock)
  * bench/issue26/repair_ab_standalone.py — the external-reproduction harness
                                        (any OpenAI-compatible endpoint, no
                                        dependency on llm/providers.py or the
                                        rest of bench/)

Same argument bench/../llm/error_classes.py already makes: two copies of a rule
drift apart. The committed issue-#26 numbers were produced by THIS loop; a
reproduction that reimplements it is not reproducing it.

Only leaf imports here — `frs_check` and `error_classes`, both stdlib-only —
so this module is safe to import without pulling in the provider stack. The
compile step is injected (`validate_faust`) rather than imported, because the
two callers get it from different places.
"""
from __future__ import annotations

import sys
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

_BENCH_DIR = Path(__file__).resolve().parent
if str(_BENCH_DIR) not in sys.path:
    sys.path.insert(0, str(_BENCH_DIR))
if str(_BENCH_DIR.parent / "llm") not in sys.path:
    sys.path.insert(0, str(_BENCH_DIR.parent / "llm"))

import error_classes  # noqa: E402
import frs_check  # noqa: E402

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

# compile step: (code) -> (ok, stderr_or_empty). Injected by the caller.
ValidateFn = Callable[[str], "tuple[bool, str]"]
# generation step: (user_message) -> new_program_text. Injected by the caller.
GenerateFn = Callable[[str], str]


def load_corpus(path: Path) -> list[dict]:
    """Distinct failing programs, first occurrence of each code_sha kept."""
    import json
    seen: set[str] = set()
    out: list[dict] = []
    for r in json.loads(Path(path).read_text()):
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


def repair_loop(entry: dict, arm: str, generate: GenerateFn,
                repair_model: str, validate_faust: ValidateFn) -> dict:
    """One arm's corrective loop from `entry`'s failing program."""
    prompt = entry["prompt"]
    code = entry["code"]
    cpp_stderr = entry["cpp_stderr"]
    first_class = error_classes.classify_error(cpp_stderr)

    attempt_log: list[dict] = []
    repaired = False
    attempts_to_green: int | None = None
    # None once a program is produced; otherwise names why the loop aborted with
    # no repaired program — a transport/infra failure, NOT a repair failure.
    # Classified by exception class name so this module keeps its leaf-import
    # guarantee (no `import providers`). See METHODOLOGY.md L2.
    terminal_reason: str | None = None

    for n in range(1, CORRECTIVE_ATTEMPTS + 1):
        feedback_text, frs_code = feedback_for(arm, code, cpp_stderr)
        template = ARM_A_TEMPLATE if arm == "A" else ARM_FRS_TEMPLATE
        user_message = prompt + template.format(feedback=feedback_text)

        started = time.monotonic()
        try:
            new_code = generate(user_message)
        except Exception as exc:  # noqa: BLE001
            attempt_log.append({"n": n, "error": f"{type(exc).__name__}: {exc}"[:300]})
            terminal_reason = _classify_terminal(exc)
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

    second_class = (attempt_log[0]["cpp_error_class"]
                    if attempt_log and "cpp_error_class" in attempt_log[0] else None)
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
        "second_error_same_as_first": (
            (second_class == first_class) if second_class else None),
        "terminal_reason": terminal_reason,
        "attempt_log": attempt_log,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }


def _classify_terminal(exc: Exception) -> str:
    """Map a generator exception to a coarse terminal_reason, by class NAME so
    this leaf module never imports the provider stack."""
    name = type(exc).__name__
    if name in ("RateLimited", "BudgetExhausted", "TooManyRequests"):
        return "rate_limited"
    if name in ("OutputTruncated", "IncompleteRead"):
        return "truncated"
    if name in ("TimeoutExpired", "ReadTimeout", "Timeout"):
        return "timeout"
    if name in ("EmptyResponse", "MalformedResponse"):
        return "empty_response"
    return "transport_error"


def stratified_sample(entries: list[dict], n: int) -> list[dict]:
    """First `n` programs, but keeping the first-error-class proportions of the
    full corpus (deterministic — take round-robin within class, class order by
    frequency). Small classes keep at least one."""
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
                out.append(by_class[c][idx])
                progressed = True
                if len(out) == n:
                    break
        idx += 1
        if not progressed:
            break
    return out
