#!/usr/bin/env python3
"""
Regression check for llm/prompts/system_prompt.txt and bench/prompts/system_faust.txt.

Operationalizes a manual step that's already written down but never automated:
ADR-009's Consequences section and docs/next_steps.md B8 both say "re-run the
benchmark to confirm >=96% Faust compile rate" after touching these prompts.
Nobody wired that follow-through up until now.

Intended use: started manually during a PAIR session that edits either prompt
file, e.g. via `/loop 15m` pointed at this script. NOT meant to run unattended
on a schedule -- these files are HUMAN-OWNED (COLLABORATION.md SS1) and change
deliberately and rarely, so a background cron would burn API cost for near-zero
signal most of the time.

Each invocation:
  1. Hashes both prompt files and compares against bench/results/.prompt_baseline.json.
     Unchanged since the last check -> print "no change, skipping" and exit 0.
     (The stored hash updates after every completed check, pass or fail -- once a
     failure/regression has been reported once, re-spending API budget every loop
     tick to re-confirm the same unchanged failure is exactly the waste this
     hash gate exists to avoid. Fix the prompt and save; that's a new hash, which
     triggers a fresh check.)
  2. Runs `pytest -m "not integration"` first -- cheap, catches structural breakage.
  3. If that passes, runs the 9-prompt recovery subset (bench/prompts/recovery_prompts.json)
     through run_benchmark.py --provider claude -- a smoke signal, not the full
     25-prompt suite. NOTE: run_benchmark.py OVERWRITES bench/results/results.json;
     this replaces whatever a prior full run left there, same as running it by hand.
  4. Compares the resulting Faust first-try compile rate (via score_results.compute_scores)
     against the human-recorded baseline rate; flags a regression if it drops more
     than FLOAT_TOLERANCE below that recorded rate, or below FAUST_RATE_FLOOR outright.
  5. Never edits the prompt files itself (they're HUMAN-OWNED; protect_human_owned.py
     would block a direct edit anyway) -- report only.

The full, unfiltered 25-prompt run_benchmark.py run (matching ADR-009's actual
stated >=96% validation bar) stays a manual, human-triggered final check before
merging a prompt change -- this script deliberately does not run it every tick,
to control API cost.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = REPO_ROOT / "bench"
sys.path.insert(0, str(BENCH_DIR))
from score_results import compute_scores  # noqa: E402  (needs sys.path insert above)

SYSTEM_PROMPT = REPO_ROOT / "llm" / "prompts" / "system_prompt.txt"
SYSTEM_FAUST_PROMPT = BENCH_DIR / "prompts" / "system_faust.txt"
RECOVERY_PROMPTS = BENCH_DIR / "prompts" / "recovery_prompts.json"
RESULTS_FILE = BENCH_DIR / "results" / "results.json"
BASELINE_FILE = BENCH_DIR / "results" / ".prompt_baseline.json"

FAUST_RATE_FLOOR = 0.90  # absolute floor regardless of recorded baseline
FLOAT_TOLERANCE = 0.05  # 5 percentage points under the recorded baseline


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_baseline() -> dict:
    if not BASELINE_FILE.exists():
        print(
            f"ERROR: {BASELINE_FILE} not found. Seed it once with the current, "
            "human-approved Faust compile rate before using this check -- see "
            "bench/README.md's 'Regression-check loop' section.",
            file=sys.stderr,
        )
        sys.exit(1)
    return json.loads(BASELINE_FILE.read_text())


def save_baseline(baseline: dict) -> None:
    BASELINE_FILE.write_text(json.dumps(baseline, indent=2) + "\n")


def faust_compile_rate(records: list[dict], provider: str) -> tuple[float, int, int]:
    scores = compute_scores(records, provider)["faust"]
    passes = sum(p for p, _ in scores.values())
    total = sum(n for _, n in scores.values())
    rate = passes / total if total else 0.0
    return rate, passes, total


def run(argv: list[str]) -> int:
    baseline = load_baseline()

    current_hashes = {
        "system_prompt_hash": sha256_of(SYSTEM_PROMPT),
        "system_faust_prompt_hash": sha256_of(SYSTEM_FAUST_PROMPT),
    }
    if (
        current_hashes["system_prompt_hash"] == baseline.get("system_prompt_hash")
        and current_hashes["system_faust_prompt_hash"]
        == baseline.get("system_faust_prompt_hash")
    ):
        print("check_prompt_regression: no change since last check, skipping.")
        return 0

    print("check_prompt_regression: prompt file(s) changed, running checks...")

    pytest_result = subprocess.run(
        [sys.executable, "-m", "pytest", "-m", "not integration"],
        cwd=REPO_ROOT,
    )
    if pytest_result.returncode != 0:
        print(
            "check_prompt_regression: pytest FAILED -- not running the benchmark. "
            "Fix the structural break first.",
            file=sys.stderr,
        )
        # Deliberately do NOT update stored hashes here: a structural break is
        # cheap to keep re-flagging (pytest alone, no API calls), unlike step 3.
        return 1

    bench_result = subprocess.run(
        [
            sys.executable,
            "run_benchmark.py",
            "--provider",
            "claude",
            "--prompts",
            str(RECOVERY_PROMPTS),
        ],
        cwd=BENCH_DIR,
    )
    if bench_result.returncode != 0:
        print(
            "check_prompt_regression: run_benchmark.py exited non-zero -- see "
            "output above.",
            file=sys.stderr,
        )
        return 1

    records = json.loads(RESULTS_FILE.read_text())
    rate, passes, total = faust_compile_rate(records, "claude")
    recorded_rate = baseline["recorded_faust_compile_rate"]

    print(
        f"check_prompt_regression: Faust first-try compile rate = {rate:.0%} "
        f"({passes}/{total}), recorded baseline = {recorded_rate:.0%}"
    )

    regressed = rate < FAUST_RATE_FLOOR or rate < recorded_rate - FLOAT_TOLERANCE
    if regressed:
        print(
            f"check_prompt_regression: REGRESSION -- rate {rate:.0%} is below the "
            f"{FAUST_RATE_FLOOR:.0%} floor or more than {FLOAT_TOLERANCE:.0%} under "
            f"the recorded baseline of {recorded_rate:.0%}. Do not merge this prompt "
            "change without investigating.",
            file=sys.stderr,
        )
    else:
        print("check_prompt_regression: OK, no regression detected.")

    # Update stored hashes after any completed check (pass or fail) so repeated
    # /loop ticks on unchanged content don't re-spend API budget -- see module
    # docstring point 1 for why this doesn't apply to the pytest-failure path above.
    baseline.update(current_hashes)
    save_baseline(baseline)

    return 1 if regressed else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
