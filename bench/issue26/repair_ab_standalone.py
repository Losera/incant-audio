#!/usr/bin/env python3
"""repair_ab_standalone.py — the issue-#26 repair-loop A/B, runnable outside PluginForge.

WHAT THIS IS
    A self-contained driver for the paired experiment behind
    https://github.com/Losera/incant-audio/issues/26 :

        "Does faust-rs's richer compile diagnostic shorten the
         generate -> compile -> correct loop?"

    For each distinct C++-rejected Faust program in the corpus, it runs the same
    corrective loop up to three ways, from the identical starting point, changing
    ONLY the error text the model is shown:

        arm A  raw C++ `faust` stderr                    (status quo)
        arm B  faust-rs --check --error-format json, rendered for a human
        arm C  faust-rs core only: stable code + one-line message + caret

    Output is one JSON record per (program, arm), in the exact schema
    bench/score_repair_ab.py consumes — so you score a standalone run with the
    same analysis code that produced the published numbers.

WHY IT EXISTS SEPARATELY FROM bench/run_repair_ab.py
    run_repair_ab.py is wired to PluginForge's provider stack, prompt loader and
    shared run-lock. This script needs none of that: it talks to any
    OpenAI-compatible chat endpoint (or a local ollama) over plain HTTP, and the
    corrective loop itself is imported from bench/repair_ab_core.py — the SAME
    module run_repair_ab.py uses — so a run here is not a reimplementation.

REQUIREMENTS
    * python 3.10+                (stdlib only; no pip install for this script)
    * faust        on PATH       — the C++ compiler, for arm A + the compile gate
    * faust-rs     on PATH       — or point PLUGINFORGE_FAUST_RS_BIN at it; needed
                                   for arms B/C (without it they fall back to A)
    * a chat model endpoint      — see --backend

    bench/frs_check.py, bench/repair_ab_core.py and llm/error_classes.py are
    imported from a PluginForge checkout (this script finds them relative to its
    own location, or honour PLUGINFORGE_ROOT).

EXAMPLES
    # local ollama, the model the published 3B run used
    python3 repair_ab_standalone.py \
        --corpus ../corpora/repair_corpus_20260830.json \
        --arms A,B,C --backend ollama --model qwen2.5-coder:3b \
        --out run_local.json
    python3 ../score_repair_ab.py run_local.json --screen ../corpora/repair_corpus_20260830.json

    # a hosted OpenAI-compatible endpoint (Groq shown; any works)
    GROQ_API_KEY=... python3 repair_ab_standalone.py \
        --corpus ../corpora/repair_corpus_20260830.json \
        --arms A,B --backend openai \
        --endpoint https://api.groq.com/openai/v1 \
        --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
        --samples 3 --out run_groq.json

    macOS: ollama binds 127.0.0.1 by default and a container / a different host
    can't reach it. Start it with  OLLAMA_HOST=0.0.0.0 ollama serve.

TRANSPORT FAILURES ARE NOT REPAIR FAILURES
    A 429 / 5xx / timeout / truncated response is retried (exp backoff,
    honouring Retry-After) and, if it still fails, recorded with
    `terminal_reason` set and EXCLUDED from the arm comparison — never scored as
    "faust-rs failed to repair this". --resume retries such cells; it does not
    treat them as done. If >=25% of a run aborts this way the script exits 3 and
    tells you the run reproduces nothing (the old behaviour was a clean-looking
    0/N table at exit 0).

DETERMINISM
    The published runs used temperature 0 and n=1 per (program, arm). Whether
    the local repair step is byte-stable run-to-run at temp 0 is NOT audited —
    the earlier claim that it "is deterministic when warm" had no artifact and
    is retracted; a proper audit is pre-registered as WP5 in
    bench/issue26/METHODOLOGY.md. A hosted model is definitely NOT bit
    deterministic at temp 0 — use --samples K (K>=5). --samples writes K records
    per (program, arm) tagged with `sample_index`. NOTE: score_repair_ab.py's
    load_pairs currently keeps only the LAST of the K per cell (WP1); until that
    lands, aggregate the K yourself before scoring — the end-of-run summary here
    uses a strict per-cell majority as a rough guide.

PROGRAM SCREEN
    By default the 10 corpus rows that are not Faust programs (English prose,
    one truncated fragment — bench/corpus_screen.py) are dropped, so a run here
    matches the published 192/115-program numbers. --no-screen keeps them for
    the raw 202-program view.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


# ── locate the shared PluginForge modules ────────────────────────────────────

def _find_repo_root() -> Path:
    env = os.environ.get("PLUGINFORGE_ROOT")
    if env:
        return Path(env).resolve()
    here = Path(__file__).resolve()
    for cand in (here.parent.parent.parent, *here.parents):
        if (cand / "bench" / "repair_ab_core.py").is_file():
            return cand
    # Docker / tarball layout: modules copied next to this script.
    return here.parent


_ROOT = _find_repo_root()
for p in (_ROOT / "bench", _ROOT / "llm", Path(__file__).resolve().parent):
    if p.is_dir() and str(p) not in sys.path:
        sys.path.insert(0, str(p))

try:
    import repair_ab_core  # noqa: E402
except ModuleNotFoundError as exc:  # pragma: no cover
    sys.exit(f"[!] cannot import {exc.name} — set PLUGINFORGE_ROOT to a checkout, "
             f"or run from bench/issue26/ inside one.")

# the system prompt the corpus was built with. Full repo: llm/prompts/. Repro
# package: the vendored MIT snapshot (byte-identical to c1e9370, sha a2d909565e3c2fd2).
_VENDORED_PROMPT = Path(__file__).resolve().parent / "system_prompt.txt"
_REPO_PROMPT = _ROOT / "llm" / "prompts" / "system_prompt.txt"
DEFAULT_SYSTEM_PROMPT = _REPO_PROMPT if _REPO_PROMPT.is_file() else _VENDORED_PROMPT
FAUST_VALIDATE_TIMEOUT_S = 30.0


# ── compile gate — behaviourally identical to bench/run_benchmark.py::validate_faust ───
# (same command, timeout and stderr truncation; pinned by
#  tests/test_repair_ab_core.py::test_validate_faust_parity)

def validate_faust(code: str) -> tuple[bool, str]:
    """(compiles, stderr[:500]). Mirrors bench/run_benchmark.py::validate_faust
    (which itself mirrors llm/generate.py) — same command, same timeout, same
    truncation, so an arm-A run here matches an arm-A run in the repo."""
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                     delete=False) as f:
        f.write(code)
        tmp = f.name
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=FAUST_VALIDATE_TIMEOUT_S,
            encoding="utf-8", errors="replace",
        )
        return result.returncode == 0, result.stderr.strip()[:500]
    except subprocess.TimeoutExpired:
        return False, ("faust compiler did not finish within 30s — "
                       "the generated patch is likely pathological "
                       "(unbounded delay, runaway recursion).")
    finally:
        os.unlink(tmp)


# ── output cleanup — vendored from llm/providers.py::strip_code_fences ────────
# (that module pulls in python-dotenv and the whole provider registry; this is
# the only piece of it the loop needs.) Kept behaviourally identical — see
# tests/test_repair_ab_core.py::test_strip_fences_parity, which diffs this
# against the real providers.strip_code_fences on a shared input set.

def strip_code_fences(text: str) -> str:
    stripped = text.strip()
    if "```" not in stripped:
        return stripped
    after_open = stripped.split("```", 1)[1]
    body = after_open.split("\n", 1)[1] if "\n" in after_open else ""
    body = body.split("```", 1)[0].strip()
    return body or stripped


# ── generation backends ─────────────────────────────────────────────────────

class RateLimited(Exception):
    """HTTP 429 / explicit rate-limit error from the endpoint."""


class EmptyResponse(Exception):
    """HTTP 200 but no usable completion (an {"error": ...} body, or no choices)."""


class OutputTruncated(Exception):
    """The model stopped because it hit the token cap (finish_reason == length).
    Matches llm/providers.py — the canonical harness aborts the attempt here, so
    the standalone must too or arm A scores differently for a non-faust-rs
    reason. Name is recognised by repair_ab_core._classify_terminal."""


def _http_post_json(url: str, payload: dict, headers: dict, timeout: float,
                    *, max_retries: int = 4) -> dict:
    """POST JSON, retrying on 429 / 5xx / connection errors with exponential
    backoff + jitter, honouring Retry-After. A 429 that outlasts the retries
    raises RateLimited (scored as a transport failure, NOT a repair failure —
    see METHODOLOGY.md L2 and tests/test_transport_error_exclusion.py)."""
    import random
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/json", **headers})
    last: Exception | None = None
    for attempt in range(max_retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            last = exc
            if exc.code == 429 or 500 <= exc.code < 600:
                retry_after = exc.headers.get("Retry-After") if exc.headers else None
                wait = (float(retry_after) if retry_after and retry_after.isdigit()
                        else min(2.0 ** attempt, 30.0) + random.uniform(0, 1.0))
                if attempt < max_retries:
                    print(f"    HTTP {exc.code}; retry {attempt + 1}/{max_retries} "
                          f"in {wait:.1f}s", file=sys.stderr)
                    time.sleep(wait)
                    continue
                if exc.code == 429:
                    raise RateLimited(f"429 after {max_retries} retries") from exc
            raise
        except (urllib.error.URLError, TimeoutError, ConnectionError) as exc:
            last = exc
            if attempt < max_retries:
                wait = min(2.0 ** attempt, 30.0) + random.uniform(0, 1.0)
                print(f"    {type(exc).__name__}; retry {attempt + 1}/{max_retries} "
                      f"in {wait:.1f}s", file=sys.stderr)
                time.sleep(wait)
                continue
            raise
    raise last  # unreachable, keeps type-checkers happy


def make_generator(backend: str, *, endpoint: str, model: str, system_prompt: str,
                   temperature: float, max_tokens: int, api_key: str | None,
                   http_timeout: float, max_retries: int = 4):
    """Returns callable(user_message) -> cleaned program text.

    Raises EmptyResponse if the endpoint returns 200 with no usable completion,
    OutputTruncated if the model stopped at the token cap, RateLimited on a
    persistent 429. All three are terminal transport/infra failures, not repair
    failures."""
    if backend == "ollama":
        base = endpoint.rstrip("/")
        url = f"{base}/api/chat"

        def gen(user_message: str) -> str:
            body = {
                "model": model,
                "messages": [{"role": "system", "content": system_prompt},
                             {"role": "user", "content": user_message}],
                "stream": False,
                "options": {"temperature": temperature, "num_predict": max_tokens},
            }
            out = _http_post_json(url, body, {}, http_timeout, max_retries=max_retries)
            if out.get("error"):
                raise EmptyResponse(str(out["error"])[:200])
            if out.get("done_reason") == "length":
                raise OutputTruncated(f"ollama hit num_predict={max_tokens}")
            content = (out.get("message") or {}).get("content", "") or ""
            if not content.strip():
                raise EmptyResponse("ollama returned an empty message")
            return strip_code_fences(content)

    elif backend == "openai":
        base = endpoint.rstrip("/")
        url = f"{base}/chat/completions"
        headers = {"Authorization": f"Bearer {api_key}"} if api_key else {}

        def gen(user_message: str) -> str:
            body = {
                "model": model,
                "messages": [{"role": "system", "content": system_prompt},
                             {"role": "user", "content": user_message}],
                "temperature": temperature,
                "max_tokens": max_tokens,
            }
            out = _http_post_json(url, body, headers, http_timeout, max_retries=max_retries)
            if out.get("error"):
                raise EmptyResponse(str(out["error"])[:200])
            choices = out.get("choices") or []
            if not choices:
                raise EmptyResponse("no choices in the completion")
            choice = choices[0]
            if choice.get("finish_reason") == "length":
                raise OutputTruncated(f"openai hit max_tokens={max_tokens}")
            content = (choice.get("message") or {}).get("content", "") or ""
            if not content.strip():
                raise EmptyResponse("empty message content")
            return strip_code_fences(content)
    else:  # pragma: no cover
        raise ValueError(f"unknown backend {backend!r}")

    return gen


# ── run ─────────────────────────────────────────────────────────────────────

def run(args: argparse.Namespace) -> int:
    system_prompt = Path(args.system_prompt).read_text()
    sp_sha = repair_ab_core.frs_check.sha(system_prompt)

    frs_bin = repair_ab_core.frs_check.faust_rs_bin()
    treat = {"B", "C"} & set(args.arms)
    if treat and frs_bin is None:
        print("[!] faust-rs not found — arms B/C would silently fall back to arm A. "
              "Put faust-rs on PATH or set PLUGINFORGE_FAUST_RS_BIN.", file=sys.stderr)
        return 2

    entries = repair_ab_core.load_corpus(Path(args.corpus))
    if not args.no_screen:
        sys.path.insert(0, str(_ROOT / "bench"))
        from corpus_screen import screen as _screen
        entries, dropped = _screen(json.loads(Path(args.corpus).read_text()))
        if dropped:
            print(f"screen: dropped {len(dropped)} non-program rows "
                  f"(--no-screen to keep them)", file=sys.stderr)
    if args.limit:
        entries = entries[:args.limit]
    elif args.stratify:
        entries = repair_ab_core.stratified_sample(entries, args.stratify)

    out_file = Path(args.out)
    records: list[dict] = []
    done: set[tuple] = set()
    if args.resume and out_file.exists():
        records = json.loads(out_file.read_text())
        # skip only records that actually produced a program — a transport /
        # truncation abort (terminal_reason set) is retried, never "done".
        done = {(r["code_sha"], r["arm"], r["repair_model"], r.get("sample_index", 0))
                for r in records if not r.get("terminal_reason")}
        stale = [r for r in records if r.get("terminal_reason")]
        records = [r for r in records if not r.get("terminal_reason")]
        if stale:
            print(f"resume: retrying {len(stale)} transport-aborted cells",
                  file=sys.stderr)

    meta = {
        "backend": args.backend, "endpoint": args.endpoint, "model": args.model,
        "temperature": args.temperature, "arms": list(args.arms),
        "system_prompt_sha": sp_sha, "system_prompt_path": str(args.system_prompt),
        "faust_rs": _bin_version(frs_bin, "--version"),
        "faust": _bin_version("faust", "--version"),
        "corpus": str(args.corpus), "samples": args.samples,
        "started": datetime.now(timezone.utc).isoformat(),
    }
    (out_file.parent).mkdir(parents=True, exist_ok=True)
    print(f"meta: {json.dumps(meta)}", file=sys.stderr)
    print(f"corpus: {len(entries)} distinct failing programs | arms {args.arms} "
          f"| model {args.model} | samples {args.samples}", file=sys.stderr)

    tasks = [(e, arm, s)
             for e in entries for arm in args.arms for s in range(args.samples)
             if (e["code_sha"], arm, args.model, s) not in done]
    print(f"{len(tasks)} (program, arm, sample) repairs pending", file=sys.stderr)

    for i, (entry, arm, s) in enumerate(tasks, 1):
        generate = make_generator(
            args.backend, endpoint=args.endpoint, model=args.model,
            system_prompt=system_prompt, temperature=args.temperature,
            max_tokens=args.max_tokens, api_key=_api_key(args),
            http_timeout=args.http_timeout, max_retries=args.max_retries)
        rec = repair_ab_core.repair_loop(entry, arm, generate, args.model, validate_faust)
        if args.samples > 1:
            rec["sample_index"] = s
        rec["run_meta"] = dict(meta)
        records.append(rec)
        out_file.write_text(json.dumps(records, indent=2))
        if rec.get("terminal_reason"):
            g = f"ABORT:{rec['terminal_reason']}"
        else:
            g = f"GREEN@{rec['attempts_to_green']}" if rec["repaired"] else "no-fix"
        tag = f"arm {arm}" + (f" s{s}" if args.samples > 1 else "")
        print(f"[{i:04d}/{len(tasks)}] {rec['prompt_id']:22s} {tag:9s} "
              f"{rec['first_error_class']:18s} -> {g}", file=sys.stderr)

    _summarise(records, args.model)

    aborted = sum(1 for r in records if r.get("terminal_reason"))
    if records and aborted / len(records) >= 0.25:
        print(f"\n[!] {aborted}/{len(records)} records ({aborted/len(records):.0%}) "
              f"ended on a transport/truncation abort — this run does NOT reproduce "
              f"anything. Check the endpoint (macOS ollama binds 127.0.0.1; try "
              f"OLLAMA_HOST=0.0.0.0 and --endpoint http://host.docker.internal:11434), "
              f"the API key, and rate limits. Re-run with --resume once fixed.",
              file=sys.stderr)
        return 3

    print(f"\nwrote {out_file}", file=sys.stderr)
    print(f"score it:  python3 {_ROOT / 'bench' / 'score_repair_ab.py'} {out_file}",
          file=sys.stderr)
    return 0


def _api_key(args: argparse.Namespace) -> str | None:
    if args.backend != "openai":
        return None
    key = os.environ.get(args.api_key_env or "")
    if not key and args.api_key_env:
        print(f"[!] ${args.api_key_env} is empty — sending no Authorization header.",
              file=sys.stderr)
    return key or None


def _bin_version(binary: str | None, flag: str) -> str | None:
    if not binary:
        return None
    try:
        out = subprocess.run([binary, flag], capture_output=True, text=True, timeout=10)
        return (out.stdout or out.stderr).strip().splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError):
        return None


def _summarise(records: list[dict], model: str) -> None:
    """A rough on-screen readout — real stats are score_repair_ab.py. Per cell
    with K>1 samples: 'repaired' = strict majority of the K, matching the
    aggregation README/METHODOLOGY prescribe (not any-of-K, which is optimistic)."""
    from collections import defaultdict
    recs = [r for r in records if r["repair_model"] == model]
    by_sha: dict[str, dict[str, list[dict]]] = defaultdict(lambda: defaultdict(list))
    for r in recs:
        by_sha[r["code_sha"]][r["arm"]].append(r)
    arms_present = sorted({r["arm"] for r in recs})
    print(f"\n-- {model}: arms {arms_present} (rough; real stats = score_repair_ab.py) --",
          file=sys.stderr)
    if "A" not in arms_present:
        return

    def majority_repaired(samples: list[dict]) -> bool:
        real = [s for s in samples if not s.get("terminal_reason")]
        if not real:
            return False
        return sum(1 for s in real if s["repaired"]) * 2 > len(real)

    for arm in [a for a in arms_present if a != "A"]:
        paired = [(v["A"], v[arm]) for v in by_sha.values() if "A" in v and arm in v]
        if not paired:
            continue
        a_g = sum(1 for a, _ in paired if majority_repaired(a))
        x_g = sum(1 for _, x in paired if majority_repaired(x))
        print(f"  A vs {arm} (n={len(paired)}): majority repaired  A {a_g}  {arm} {x_g}",
              file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", required=True,
                    help="repair_corpus_*.json (bench/corpora/ in the repo)")
    ap.add_argument("--out", default=f"repair_ab_standalone_{datetime.now():%Y%m%d}.json")
    ap.add_argument("--arms", default="A,B,C", help="comma list from A,B,C")
    ap.add_argument("--backend", choices=("ollama", "openai"), default="ollama")
    ap.add_argument("--endpoint", default="http://localhost:11434",
                    help="ollama: base host (default %(default)s). "
                         "openai: the /v1 base, e.g. https://api.groq.com/openai/v1")
    ap.add_argument("--model", default="qwen2.5-coder:3b")
    ap.add_argument("--api-key-env", default=None,
                    help="env var holding the bearer token (openai backend)")
    ap.add_argument("--temperature", type=float, default=0.0)
    ap.add_argument("--max-tokens", type=int, default=4096)
    ap.add_argument("--samples", type=int, default=1,
                    help="records per (program, arm); use >=5 for a non-deterministic model")
    ap.add_argument("--http-timeout", type=float, default=180.0)
    ap.add_argument("--max-retries", type=int, default=4,
                    help="429 / 5xx / connection retries with exp backoff (default 4)")
    ap.add_argument("--system-prompt", default=str(DEFAULT_SYSTEM_PROMPT))
    ap.add_argument("--limit", type=int, help="first N distinct programs (smoke test)")
    ap.add_argument("--stratify", type=int,
                    help="N distinct programs, first-error-class proportional")
    ap.add_argument("--no-screen", action="store_true",
                    help="keep the 10 non-program rows (bench/corpus_screen.py) — "
                         "reproduces the pre-screen 202-program numbers")
    ap.add_argument("--resume", action="store_true")
    args = ap.parse_args()

    args.arms = tuple(a.strip().upper() for a in args.arms.split(","))
    if any(a not in repair_ab_core.ARMS for a in args.arms):
        print(f"[!] --arms must be from {repair_ab_core.ARMS}", file=sys.stderr)
        return 2
    if args.limit and args.stratify:
        print("[!] --limit and --stratify are mutually exclusive", file=sys.stderr)
        return 2
    if not Path(args.system_prompt).is_file():
        print(f"[!] --system-prompt not found: {args.system_prompt}", file=sys.stderr)
        return 2
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
