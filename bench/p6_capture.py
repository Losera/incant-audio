#!/usr/bin/env python3
"""Turn the 14-prompt P6 battery into a folder of WAVs and a scorecard to tick.

WHY THIS EXISTS
---------------
There is exactly one judgement in this project that no instrument can make:
whether a generated plugin SOUNDS like what was asked for. The render oracle
proves a patch is not broken -- no NaN, no silence, no DC, no runaway gain -- and
cannot tell you the filter is musical or that the fuzz is the fuzz the prompt
described. COLLABORATION.md §1 is explicit that this is the human's, and it stays
the human's.

What it does NOT have to stay is a 40-minute session driving an app. The
2026-07-24 battery had a person launching a Standalone, typing 14 prompts,
waiting on generations, routing audio, and listening -- and most of that is
machine work wrapped around a few minutes of listening. This script does all of
the wrapping. What comes out the far end is a folder of WAVs and a table with one
empty column, and filling that column in is the part that needed ears all along.

WHAT IT DOES NOT MEASURE
------------------------
Anything about fidelity. The `verdict` column is the objective gate only: did it
compile, did it render, is the output finite/non-silent/bounded. A patch can be
green on every one of those and be completely wrong, and that failure mode --
compiles clean, sounds wrong -- is the one that decides whether this project is
any good. That is what the empty column is for.

RENDERED THROUGH THE SHIPPING PATH
----------------------------------
WAVs come from `OfflineRenderTest --capture`, i.e. through
PluginForgeProcessor::processBlock: the libfaust JIT, the FaustEngine swap
protocol, ParamPool's denormalisation and OutputGuard all run as they do in a
DAW. Not through faust2sndfile, which is a different compiler and a different
code path. If someone is spending their ears, they should hear the real thing.

Parameters sit at each patch's declared defaults -- the position a user hears the
instant after clicking Generate, which is the moment being reproduced.

QUOTA
-----
A real run spends free-tier provider requests: 14 generations, up to 3 attempts
each. It holds the PF-025 lock for its whole duration, so it cannot race
run_benchmark.py into a shared rate limit (which is PF-030's hazard, closed here
for this harness). Sequential by construction, never parallel.

  python bench/p6_capture.py --dry-run     # no LLM, no quota, canned patches
  python bench/p6_capture.py --provider groq --i-authorize-spend
"""
import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))
sys.path.insert(0, str(ROOT / "llm"))

from run_benchmark import acquire_lock, release_lock  # noqa: E402  (PF-025/PF-030)
import render_oracle  # noqa: E402  (pitch_of_wav -- report only, never a gate here)

BATTERY = ROOT / "bench" / "prompts" / "p6_battery.json"
GENERATE = ROOT / "llm" / "generate.py"

# Canned patches for --dry-run: enough variety that the scorecard, the WAVs and
# the failure rows all get exercised without a network. Deliberately includes a
# non-compiling one and a silent one -- a dry run whose every row is green tells
# you nothing about the rows that matter.
DRY_RUN_PATCHES = [
    'import("stdfaust.lib");\nc = hslider("Cutoff", 800, 20, 20000, 1);\n'
    'process = fi.resonlp(c, 0.707, 1), fi.resonlp(c, 0.707, 1);\n',
    'import("stdfaust.lib");\ng = hslider("Gain", 0.5, 0, 1, 0.01);\n'
    'process = _ * g, _ * g;\n',
    'import("stdfaust.lib");\nprocess = this is not faust;\n',      # compile failure
    'import("stdfaust.lib");\ng = checkbox("Gate");\n'
    'process = _ * g, _ * g;\n',                                    # silent at defaults
    # An instrument: the full voice contract, so --capture plays note 45 and
    # the dry run exercises the instrument=1 branch of verdict_for below.
    'import("stdfaust.lib");\n'
    'freq = hslider("freq",440,20,2000,0.01);\n'
    'gain = hslider("gain",0.5,0,1,0.01);\n'
    'gate = button("gate");\n'
    'process = os.osc(freq) * gain * en.adsr(0.01,0.1,0.7,0.2,gate) <: _,_;\n',
]


def find_capture_binary() -> Path | None:
    """The OfflineRenderTest binary, which carries --capture."""
    for base in (ROOT / "host" / "build", ROOT / "host" / "build-ci"):
        hits = sorted(base.glob("OfflineRenderTest_artefacts/**/OfflineRenderTest"))
        if hits:
            return hits[0]
    return None


def generate_one(prompt: str, provider: str, timeout_s: int) -> dict:
    """One real generation through the SAME entry point the C++ host uses.

    `--prompt` rather than importing generate_faust: the product path is the one
    worth measuring, it emits one ADR-011 JSON line, and it is what
    log_user_prompt records -- so a capture run also accumulates real prompt-log
    data instead of bypassing it.
    """
    env_note = f"PLUGINFORGE_PROVIDER={provider}" if provider else "(provider from .env)"
    try:
        proc = subprocess.run(
            [sys.executable, str(GENERATE), "--prompt", prompt],
            capture_output=True, text=True, timeout=timeout_s,
            env={**__import__("os").environ,
                 **({"PLUGINFORGE_PROVIDER": provider} if provider else {})},
        )
    except subprocess.TimeoutExpired:
        return {"success": False, "error": f"generate.py exceeded {timeout_s}s", "reason": "timeout"}

    line = ""
    for raw in proc.stdout.splitlines():
        if raw.strip().startswith("{"):
            line = raw.strip()
    if not line:
        return {"success": False,
                "error": f"no JSON from generate.py {env_note}\n{proc.stderr[-2000:]}",
                "reason": "error"}
    try:
        return json.loads(line)
    except json.JSONDecodeError as exc:
        return {"success": False, "error": f"unparseable JSON: {exc}", "reason": "error"}


def capture_wav(binary: Path, dsp: Path, wav: Path, timeout_s: int = 180) -> dict:
    """Render one patch to WAV through the shipping audio path."""
    try:
        proc = subprocess.run([str(binary), "--capture", str(dsp), str(wav)],
                              capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        return {"ok": False, "why": f"render exceeded {timeout_s}s"}

    for raw in proc.stdout.splitlines():
        if raw.startswith("CAPTURE_OK"):
            fields = dict(tok.split("=", 1) for tok in raw.split()[1:])
            return {"ok": True, **fields}
    tail = (proc.stdout + proc.stderr).strip().splitlines()
    return {"ok": False, "why": tail[-1] if tail else f"exit {proc.returncode}"}


def verdict_for(cap: dict) -> tuple[str, str]:
    """The OBJECTIVE gate only. Says nothing about whether it sounds right."""
    if not cap.get("ok"):
        return "RENDER FAIL", cap.get("why", "")
    rms = float(cap.get("rms", 0.0))
    peak = float(cap.get("peak", 0.0))
    if cap.get("nan") == "1" or cap.get("inf") == "1":
        return "NaN/Inf", "non-finite samples reached the output"
    if cap.get("muted") == "1":
        return "MUTED", "OutputGuard latched -- runaway or non-finite"
    if cap.get("instrument") == "1" and rms < 1e-5:
        # No longer ambiguous the way a bare-effect silence is. --capture PLAYS
        # note 45 at an instrument, so a patch that declares the full voice
        # contract and still renders silent did not receive its note or did not
        # sound it. PF-032's "silent by design vs broken" question is answered
        # by the note, not by a better threshold -- this is that answer.
        return "NO NOTE", "instrument rendered silent with note 45 held"
    if rms < 1e-5:
        # Not necessarily a defect: a patch gated off at its declared defaults is
        # silent BY DESIGN, and PF-032 is open on telling the two apart. Flagged,
        # not failed, because the person listening is the one who can tell.
        return "SILENT", f"rms {rms:.2e} at declared defaults -- may be gated off by design"
    if peak > 4.0:
        return "HOT", f"peak {peak:.2f}"
    return "ok", f"rms {rms:.4f}, peak {peak:.2f}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true",
                    help="canned patches, no LLM, no quota. Proves the pipeline.")
    ap.add_argument("--provider", default="groq",
                    help="provider for a real run (default: groq, the free one with headroom)")
    ap.add_argument("--i-authorize-spend", action="store_true",
                    help="required for a real run; 14 generations of free-tier quota")
    ap.add_argument("--out", default=None, help="output directory (default: artifacts/p6_<date>)")
    ap.add_argument("--timeout", type=int, default=180, help="per-generation seconds")
    ap.add_argument("--only", type=int, default=None, help="run a single prompt id (1-14)")
    args = ap.parse_args()

    if not args.dry_run and not args.i_authorize_spend:
        print(__doc__)
        print("\nA real run spends free-tier quota: 14 generations, up to 3 attempts each.")
        print("Re-run with --i-authorize-spend, or use --dry-run to exercise the pipeline "
              "for free.\n")
        return 2

    binary = find_capture_binary()
    if binary is None:
        print("OfflineRenderTest not built. Run:\n"
              "  cmake --build host/build --target OfflineRenderTest", file=sys.stderr)
        return 1

    battery = json.loads(BATTERY.read_text())
    prompts = battery["prompts"]
    if args.only is not None:
        prompts = [p for p in prompts if p["id"] == args.only]
        if not prompts:
            print(f"no prompt with id {args.only}", file=sys.stderr)
            return 1

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d")
    outdir = Path(args.out) if args.out else ROOT / "artifacts" / f"p6_{stamp}"
    outdir.mkdir(parents=True, exist_ok=True)

    mode = "DRY RUN (no LLM, no quota)" if args.dry_run else f"LIVE on {args.provider}"
    print(f"P6 capture -- {mode}")
    print(f"  {len(prompts)} prompts -> {outdir}")
    print(f"  rendering through {binary.name} --capture (the shipping audio path)\n")

    # PF-025 lock for the WHOLE run, not per generation: the hazard is two
    # harnesses sharing one rate limit, and that is a property of the run.
    # Dry runs take it too -- they write into the same tree, and a guard you skip
    # when it is inconvenient is not a guard.
    acquire_lock()
    rows = []
    try:
        for i, item in enumerate(prompts):
            pid, tier, prompt = item["id"], item["tier"], item["prompt"]
            print(f"  [{pid:2d}/{len(prompts)}] {tier:<10} {prompt[:58]}")

            if args.dry_run:
                patch = DRY_RUN_PATCHES[i % len(DRY_RUN_PATCHES)]
                resp = {"success": True, "faust_code": patch, "attempts": 1, "reason": "ok"}
            else:
                resp = generate_one(prompt, args.provider, args.timeout)

            row = {**item, "attempts": resp.get("attempts", 0),
                   "reason": resp.get("reason", "error"), "wav": None}

            if not resp.get("success") or not resp.get("faust_code"):
                row["verdict"] = "GEN FAIL"
                row["detail"] = (resp.get("error") or "no error text")[:200].replace("\n", " ")
                print(f"       GEN FAIL -- {row['detail'][:80]}")
                rows.append(row)
                continue

            dsp = outdir / f"{pid:02d}_patch.dsp"
            dsp.write_text(resp["faust_code"])
            wav = outdir / f"{pid:02d}_{tier}.wav"

            cap = capture_wav(binary, dsp, wav, args.timeout)
            row["verdict"], row["detail"] = verdict_for(cap)
            row["params"] = cap.get("params", "-")
            row["wav"] = wav.name if cap.get("ok") else None

            # REPORT the pitch, never gate on it here -- this harness runs on
            # nondeterministic model output (corpus_main makes the same call for
            # the tail check), so it must not go red for reasons unrelated to
            # whatever change is being evaluated. The gate's teeth live in
            # tests/test_pitch_gate.py's deterministic fixtures.
            if cap.get("ok") and cap.get("instrument") == "1" and row["wav"]:
                held_end = int(cap.get("held_end", 0)) or None
                try:
                    p = render_oracle.pitch_of_wav(wav, midi_note=45, held_end=held_end)
                except Exception as exc:  # pragma: no cover -- diagnostics only
                    row["detail"] += f" [pitch: error ({exc})]"
                else:
                    if p.evaluated:
                        row["detail"] += (f" [pitch: {p.f0_hz:.1f} Hz, "
                                          f"{p.cents_error:+.0f}c from A2/110 Hz]")
                    else:
                        row["detail"] += f" [pitch: not evaluated ({p.why})]"

            print(f"       {row['verdict']:<11} {row['detail']}")
            rows.append(row)
    finally:
        release_lock()

    # ── The scorecard ───────────────────────────────────────────────────────
    ok = sum(1 for r in rows if r["verdict"] == "ok")
    lines = [
        f"# P6 listening scorecard -- {stamp}",
        "",
        f"**{mode}.** {ok} of {len(rows)} passed the objective gate. "
        "That number says nothing about whether any of them sound right.",
        "",
        "Play each WAV against the dry signal and fill in the last column. The "
        "input is a 440 Hz sine plus broadband noise, ~2.1 s, identical for every "
        "patch -- so differences you hear are the patch, not the material.",
        "",
        "`verdict` is the objective gate only: compiled, rendered, finite, "
        "non-silent, bounded. **A row marked `ok` can still be completely wrong**, "
        "and that failure -- compiles clean, sounds wrong -- is the one nothing "
        "here can see.",
        "",
        "| # | tier | prompt | expected direction | params | verdict | detail | does it sound right? |",
        "|---|------|--------|--------------------|--------|---------|--------|----------------------|",
    ]
    for r in rows:
        wav = f"[{r['wav']}]({r['wav']})" if r.get("wav") else "--"
        lines.append(
            f"| {r['id']} | {r['tier']} | {r['prompt'][:70]} | {r.get('expect','')[:50]} | "
            f"{r.get('params','-')} | {r['verdict']} | {wav} {r.get('detail','')[:60]} |  |"
        )
    lines += [
        "",
        "## What to listen for",
        "",
        "* **#1 and #2 are controls.** They are the specified ones; if they fail, "
        "the run is broken rather than the tier being hard.",
        "* **#3, #4 and #6 are a set** -- warm / underwater / cold-and-distant. All "
        "three plausibly resolve to \"some kind of filtering\", but they are "
        "*different sounds* to a human. Whether the system distinguishes them is "
        "the single most interesting thing here, and it is invisible to every "
        "metric in this repo.",
        "* **#14 must fail gracefully.** A clean error or a valid-if-odd DSP both "
        "pass. A hang, a crash or a garbled status does not.",
        "",
        "Source prompts: `docs/p6_test_battery.md`. Do not reword one to make it "
        "work -- a reworded prompt is a different data point, so note it as one.",
        "",
    ]
    card = outdir / "scorecard.md"
    card.write_text("\n".join(lines))

    print(f"\n  {ok}/{len(rows)} passed the objective gate")
    print(f"  scorecard -> {card}")
    print(f"  wavs      -> {outdir}")
    if not args.dry_run:
        print("\n  The listening pass is the part that needed you. "
              "The rest of it is done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
