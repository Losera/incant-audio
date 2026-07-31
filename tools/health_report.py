#!/usr/bin/env python3
"""health_report.py — the three-prong health check, as numbers.

PluginForge is three things wearing one coat: a real-time DSP audio path, an LLM
generation pipeline, and a plugin UI. Each already had measurement capacity. None
of it was comparable, most of it was prose, and some of it ran nowhere at all.

This aggregates all three into one dated artifact:

    artifacts/health/health_<date>.json   machine
    artifacts/health/health_<date>.md     human

WHAT THIS IS NOT. It does not judge. It counts, and it says what it could not
count. Three things in this project have no instrument and the report says so
rather than omitting them:

  * whether a generated plugin SOUNDS like what was asked (COLLABORATION.md §1)
  * whether the grid LOOKS right, as opposed to reporting right strings
  * semantic fidelity (PF-013) — blocked on a judge model independent of the
    generator, and no free one is installed

THE ABSENT/ZERO DISTINCTION IS THE WHOLE POINT. A harness that did not run is
recorded as `"status": "absent"`, never as zero checks. This project's signature
defect is a control believed to run that does not — five hooks that never fired,
a benchmark measuring a deleted prompt, a CI gate reporting to nobody. A silent
zero here would be the same defect wearing a report's clothes. `--strict` turns
any absence into a non-zero exit.

LANES AND WHY THEY ARE SEPARATE. `--lane dsp|ui|ai|all`:

  dsp   $0, no network, no display, no lock. Parallel-safe.
  ui    $0, no network, NEEDS a display (xvfb-run is absent on the dev box).
        Parallel-safe with dsp.
  ai    SPENDS PROVIDER QUOTA and holds the PF-025 lock at bench/results/.run.lock.
        STRICTLY ONE AT A TIME, ever. Requires --i-authorize-spend.

The build is deliberately NOT a lane: all three read binaries out of host/build,
and p6_capture shells out to the OfflineRenderTest binary, so a rebuild racing a
harness corrupts whichever is mid-link. Build first, serially, then fan out.

⚠️ ONE MORE SERIALISATION, FOUND THE HARD WAY (2026-07-30). The `dsp` lane's
oracle forks a full faust2sndfile C++ compile *and link* per patch, and the `ai`
lane forks a generation subprocess per prompt. Running both at once put enough
fork/exec pressure on the box that unrelated `python` invocations died with

    Fatal Python error: init_fs_encoding: failed to get the Python codec
    LookupError: no codec search functions registered

— a resource failure that looks nothing like one. Neither lane holds a lock the
other respects, because neither is contending for quota or files; they are
contending for process slots. If you are running the oracle corpus and the AI
lane on one machine, run them sequentially. `ui` is cheap and safe alongside
either.

Usage:
    python tools/health_report.py --lane dsp
    python tools/health_report.py --lane ui
    python tools/health_report.py --lane ai --i-authorize-spend --runs 2
    python tools/health_report.py --collect          # merge lanes -> report
    python tools/health_report.py --collect --strict # absence is an error
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "host" / "build"
OUTDIR = ROOT / "artifacts" / "health"
LANEDIR = OUTDIR / "lanes"

# ── Harness inventory ───────────────────────────────────────────────────────
# `jits` matters for PF-036: those binaries emit code through libfaust's LLVM and
# must run under the CPU shim on any host whose CPUID over-promises. `display`
# matters because xvfb-run is not installed here, so those two need the real
# session and are skipped (loudly) without one.
HARNESSES = {
    "dsp": [
        # name,                    artefact dir,                   jits, display
        ("OfflineRenderTest",      "OfflineRenderTest",            True,  False),
        ("JitTargetTest",          "JitTargetTest",                True,  False),
        ("OutputGuardTest",        "OutputGuardTest",              False, False),
        ("ParamMapTest",           "ParamMapTest",                 False, False),
        ("StatePersistenceTest",   "StatePersistenceTest",         True,  False),
    ],
    "ui": [
        ("EditorSessionTest",        "EditorSessionTest",        True, True),
        ("PromptPanelThreadingTest", "PromptPanelThreadingTest", True, True),
    ],
}

SUMMARY_RE = re.compile(
    r"PF_SUMMARY\s+harness=(\S+)\s+checks=(\d+)\s+failures=(\d+)"
)
SNAPSHOT_RE = re.compile(r"snapshot -> (\S+\.png) \((\d+)x(\d+)\)")
KNOBORDER_RE = re.compile(r"knob order on screen:\s*(.+)")
CENSUS_RE = re.compile(r"^\s+(\w+)\s+evex=(\d+)\s+kmask=(\d+)\s+vex=(\d+)")
CAPTURE_RE = re.compile(
    r"CAPTURE_OK wav=(\S+) params=(\d+) rms=([\d.eE+-]+) peak=([\d.eE+-]+) "
    r"dc=([\d.eE+-]+) nan=(\d) inf=(\d) muted=(\d)"
)


def _now() -> str:
    return _dt.datetime.now().isoformat(timespec="seconds")


def _find(name: str) -> Path | None:
    hits = sorted(BUILD.rglob(name))
    return next((h for h in hits if h.is_file() and os.access(h, os.X_OK)), None)


def _shim() -> Path | None:
    return next(iter(sorted(BUILD.rglob("libpf_cpu_shim.so"))), None)


def _have_display() -> bool:
    return bool(os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"))


def _asan() -> str | None:
    """Path to libasan, which must LEAD LD_PRELOAD or an ASan binary aborts."""
    try:
        out = subprocess.run(
            ["gcc", "-print-file-name=libasan.so"],
            capture_output=True, text=True, timeout=30,
        ).stdout.strip()
        return out if out and Path(out).exists() else None
    except Exception:
        return None


# ── Lane runners ────────────────────────────────────────────────────────────
def run_harness(name: str, artefact: str, jits: bool, needs_display: bool) -> dict:
    """Run one C++ harness and parse its PF_SUMMARY plus any extra signals."""
    binary = _find(artefact)
    if binary is None:
        return {"status": "absent", "reason": f"binary {artefact} not built"}
    if needs_display and not _have_display():
        return {"status": "absent",
                "reason": "needs a display; xvfb-run is not installed on this host"}

    env = dict(os.environ)
    # PF-036: a JIT-ing harness on a CPU that names an ISA it cannot execute
    # SIGILLs inside computemydsp. The shim is CI's fix; using it here too keeps
    # the local numbers comparable with CI's.
    if jits:
        shim, asan = _shim(), _asan()
        if shim and asan:
            env["LD_PRELOAD"] = f"{asan}:{shim}"
            env["PLUGINFORGE_FAUST_CPU"] = "x86-64"

    try:
        p = subprocess.run([str(binary)], capture_output=True, text=True,
                           timeout=600, env=env, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "reason": "exceeded 600s"}

    out = p.stdout + p.stderr
    m = SUMMARY_RE.search(out)
    rec: dict = {
        "status": "ran",
        "exit_code": p.returncode,
        "binary": str(binary.relative_to(ROOT)),
    }
    if m:
        rec["checks"] = int(m.group(2))
        rec["failures"] = int(m.group(3))
    else:
        # Ran but said nothing countable. NOT the same as absent, and not the
        # same as zero -- record it as its own state so it cannot read as health.
        rec["status"] = "ran_unparseable"
        rec["reason"] = "no PF_SUMMARY line in output"

    # Extra per-harness signals that already existed as prose.
    snaps = [{"file": f, "w": int(w), "h": int(h)}
             for f, w, h in SNAPSHOT_RE.findall(out)]
    if snaps:
        rec["snapshots"] = snaps
    order = KNOBORDER_RE.search(out)
    if order:
        # PF-038 is printed as a fact and deliberately not asserted. Recording it
        # gives the lexicographic ordering a number instead of a log line.
        labels = [s.strip() for s in order.group(1).split(",")]
        rec["knob_order"] = labels
        rec["knob_order_is_lexicographic"] = labels == sorted(labels)
    census = CENSUS_RE.findall(out)
    if census:
        rec["isa_census"] = [
            {"patch": n, "evex": int(e), "kmask": int(k), "vex": int(v)}
            for n, e, k, v in census
        ]
    return rec


def lane_dsp(oracle_results: str | None = None) -> dict:
    res = {"lane": "dsp", "started": _now(), "harnesses": {}}
    for name, art, jits, disp in HARNESSES["dsp"]:
        res["harnesses"][name] = run_harness(name, art, jits, disp)

    # ParamPoolTsanTest asserts nothing; its verdict is "TSan stayed quiet".
    tsan = _find("ParamPoolTsanTest")
    if tsan is None:
        res["tsan"] = {"status": "absent", "reason": "binary not built"}
    else:
        env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1")
        try:
            p = subprocess.run([str(tsan)], capture_output=True, text=True,
                               timeout=600, env=env, cwd=ROOT)
            res["tsan"] = {"status": "ran", "exit_code": p.returncode,
                           "clean": p.returncode == 0}
        except subprocess.TimeoutExpired:
            res["tsan"] = {"status": "timeout"}

    # Point the oracle at a NAMED archive, not at results.json. Every benchmark
    # run copies over results.json, so an oracle result attributed to it is
    # ambiguous the moment a run lands -- and this pass runs two. The archives
    # are what PF-025 exists to preserve; measure those.
    res["oracle"] = run_oracle_corpus(
        (ROOT / oracle_results) if oracle_results else None)
    res["finished"] = _now()
    return res


def run_oracle_corpus(results_path: Path | None = None) -> dict:
    """Render every compiling patch in a benchmark corpus and measure it.

    The corpus driver lived only as a 24-line heredoc inside tools/check.sh, so
    the numbers were computed and thrown away. `render_oracle.analyse()` already
    returns a JSON-serialisable dict; it simply had no writer.

    Run metadata is recorded deliberately: bench/results/oracle_20260725.json
    carries no timestamp, provider or source hash, so nothing can say what
    produced it.
    """
    sys.path.insert(0, str(ROOT / "bench"))
    try:
        import render_oracle as ro  # type: ignore
    except Exception as exc:
        return {"status": "absent", "reason": f"cannot import render_oracle: {exc}"}

    # Accept either an absolute path or one relative to the repo root; callers
    # naturally write the latter, and relative_to() below would explode on it.
    results_path = Path(results_path) if results_path else (
        ROOT / "bench" / "results" / "results.json")
    if not results_path.is_absolute():
        results_path = ROOT / results_path
    if not results_path.exists():
        return {"status": "absent", "reason": f"{results_path} missing"}

    records = json.loads(results_path.read_text())
    compiling = [r for r in records if r.get("first_try_compiles")]

    out = {
        "status": "ran",
        "source_results": str(results_path.relative_to(ROOT)),
        "source_records": len(records),
        "compiling_records": len(compiling),
        "started": _now(),
        "patches": [],
        "passed": 0, "failed": 0, "unsupported": 0, "errored": 0,
        "expectation_unmet": 0,
    }
    for r in compiling:
        try:
            # analyse_record, not analyse: it applies the record's own tail
            # expectation from render_oracle.TAIL_EXPECT_MS. The expectation table
            # lives there so this file and tools/check.sh cannot drift apart on it.
            a = ro.analyse_record(r)
        except Exception as exc:
            out["errored"] += 1
            out["patches"].append({"prompt": r.get("prompt"),
                                   "category": r.get("category"),
                                   "analysis": {"rendered": False,
                                                "error": f"harness raised: {exc}"}})
            continue
        out["patches"].append({"prompt": r.get("prompt"),
                               "category": r.get("category"), "analysis": a})
        if not a.get("rendered"):
            out["unsupported" if a.get("unsupported") else "errored"] += 1
        elif a["measurement"]["ok"]:
            out["passed"] += 1
        else:
            out["failed"] += 1
        exp = a.get("expectation")
        if exp and not exp["met"]:
            out["expectation_unmet"] += 1
    out["finished"] = _now()
    return out


def lane_ui() -> dict:
    res = {"lane": "ui", "started": _now(), "harnesses": {},
           "display": os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")}
    for name, art, jits, disp in HARNESSES["ui"]:
        res["harnesses"][name] = run_harness(name, art, jits, disp)
    res["finished"] = _now()
    return res


def lane_ai(runs: int, provider: str, authorized: bool) -> dict:
    """Generation quality. SPENDS QUOTA. Holds the PF-025 lock, one at a time."""
    if not authorized:
        return {"lane": "ai", "status": "absent",
                "reason": "not authorized; pass --i-authorize-spend"}

    res = {"lane": "ai", "started": _now(), "provider": provider,
           "runs": [], "status": "ran"}
    env = dict(os.environ)
    # The bench harnesses pass budget=None, so llm/providers._pace() is a no-op
    # and the run is UNPACED. Unpaced runs walk into 429s that classify_failures
    # files as `transport` -- a corrupted measurement that reads as data. This is
    # the value already commented in .env.example.
    env.setdefault("PLUGINFORGE_MIN_INTERVAL", "13")

    for i in range(runs):
        p = subprocess.run(
            [sys.executable, "bench/run_benchmark.py", "--provider", provider],
            capture_output=True, text=True, timeout=7200, env=env, cwd=ROOT)
        entry = {"index": i + 1, "exit_code": p.returncode,
                 "tail": (p.stdout + p.stderr)[-2000:]}
        if p.returncode == 2:
            entry["reason"] = "PF-025 lock held by another run — refused"
        res["runs"].append(entry)
        if p.returncode != 0:
            res["status"] = "partial"
            break
        # run_benchmark copies to results.json AND writes a dated archive; grab
        # the archive so run 2 cannot overwrite run 1's evidence.
        archives = sorted((ROOT / "bench" / "results").glob("results_*_*.json"),
                          key=lambda q: q.stat().st_mtime)
        if archives:
            entry["archive"] = str(archives[-1].relative_to(ROOT))

    res["scored"] = score_runs([r.get("archive") for r in res["runs"]
                                if r.get("archive")])
    res["finished"] = _now()
    return res


def score_runs(archives: list[str]) -> dict:
    """Compile rate + 13-class failure profile per archive, and the deltas."""
    sys.path.insert(0, str(ROOT / "bench"))
    try:
        import classify_failures as cf  # type: ignore
    except Exception as exc:
        return {"status": "absent", "reason": f"cannot import: {exc}"}

    out: dict = {"status": "ran", "per_run": {}}
    for a in archives:
        try:
            recs = json.loads((ROOT / a).read_text())
            classified = cf.classify_records(recs)
            s = cf.summarise(classified)
            out["per_run"][a] = {
                "total": s["total"], "ok": s["ok"],
                "transport_excluded": s["transport_excluded"],
                "denominator": s["denominator"],
                "compile_rate": s["compile_rate"],
                "by_class": dict(s["by_class"]),
            }
        except Exception as exc:
            out["per_run"][a] = {"status": "error", "reason": str(exc)}

    # PF-031: the noise floor is the delta between two runs of the SAME prompt on
    # the SAME corpus. Until this exists, no delta on record is known to be
    # significant -- three archived runs sit at 80/88/88% with disjoint failure
    # sets, which is exactly what unmeasured noise looks like.
    rates = [v["compile_rate"] for v in out["per_run"].values()
             if isinstance(v.get("compile_rate"), (int, float))]
    if len(rates) >= 2:
        out["noise_floor"] = {
            "runs": len(rates),
            "rates": rates,
            "spread": max(rates) - min(rates),
            "note": ("Same prompt, same corpus, back to back. Any benchmark delta "
                     "smaller than this spread is not distinguishable from noise."),
        }

    # THE MORE IMPORTANT NUMBER. The rate spread understates the problem: two
    # runs can land on the same rate through completely different failures. What
    # decides whether a per-class claim means anything is how often the SAME
    # prompt fails with the SAME class. Measured 2026-07-30 over three runs: of
    # six prompts that failed at all, one was reproducible in both prompt and
    # class. A per-class delta read off a single run is, on that evidence,
    # mostly noise -- and reading one is exactly what this project has done.
    out["stability"] = _stability(list(archives))
    return out


def _stability(archives: list[str]) -> dict:
    """How often does the same prompt fail, and with the same class?"""
    sys.path.insert(0, str(ROOT / "bench"))
    try:
        import classify_failures as cf  # type: ignore
    except Exception as exc:
        return {"status": "absent", "reason": str(exc)}

    import collections
    fails: dict[str, list[str]] = collections.defaultdict(list)
    n = 0
    for a in archives:
        try:
            recs = json.loads((ROOT / a).read_text())
        except Exception:
            continue
        n += 1
        for r in cf.classify_records(recs):
            if r.klass != "ok":
                fails[r.prompt].append(r.label)
    if n < 2:
        return {"status": "absent", "reason": "need >=2 archives"}

    always = [p for p, v in fails.items() if len(v) == n]
    same_class = [p for p, v in fails.items() if len(v) > 1 and len(set(v)) == 1]
    return {
        "status": "ran",
        "runs": n,
        "prompts_that_failed_at_least_once": len(fails),
        "always_failed": always,
        "reproducible_prompt_and_class": same_class,
        "per_prompt": {p: v for p, v in sorted(fails.items(), key=lambda kv: -len(kv[1]))},
    }


# ── Collection ──────────────────────────────────────────────────────────────
def collect(strict: bool) -> int:
    LANEDIR.mkdir(parents=True, exist_ok=True)
    OUTDIR.mkdir(parents=True, exist_ok=True)
    date = _dt.date.today().strftime("%Y%m%d")

    report: dict = {
        "generated": _now(),
        "git_head": subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                                   capture_output=True, text=True,
                                   cwd=ROOT).stdout.strip(),
        "lanes": {},
        "no_instrument": [
            "Whether a generated plugin SOUNDS like its prompt (COLLABORATION.md §1).",
            "Whether the parameter grid LOOKS right, vs reporting right strings.",
            "The DAW's own automation lane, which still shows raw 0-1 slots.",
            "Semantic fidelity (PF-013): needs a judge model independent of the "
            "generator; ollama is not installed and gemini allows 20 req/day.",
        ],
    }
    for lane in ("dsp", "ui", "ai"):
        f = LANEDIR / f"{lane}.json"
        report["lanes"][lane] = (json.loads(f.read_text()) if f.exists()
                                 else {"lane": lane, "status": "absent",
                                       "reason": "lane was not run"})

    jpath = OUTDIR / f"health_{date}.json"
    jpath.write_text(json.dumps(report, indent=2))
    mpath = OUTDIR / f"health_{date}.md"
    mpath.write_text(render_markdown(report))

    absent = _absences(report)
    print(f"wrote {jpath.relative_to(ROOT)}")
    print(f"wrote {mpath.relative_to(ROOT)}")
    if absent:
        print(f"\n{len(absent)} thing(s) did not run:", file=sys.stderr)
        for a in absent:
            print(f"  ABSENT  {a}", file=sys.stderr)
    return 1 if (strict and absent) else 0


def _absences(report: dict) -> list[str]:
    out = []
    for lane, data in report["lanes"].items():
        if data.get("status") == "absent":
            out.append(f"lane {lane}: {data.get('reason')}")
            continue
        for name, h in (data.get("harnesses") or {}).items():
            if h.get("status") != "ran":
                out.append(f"{lane}/{name}: {h.get('status')} — {h.get('reason','')}")
        for key in ("oracle", "tsan"):
            k = data.get(key)
            if isinstance(k, dict) and k.get("status") not in (None, "ran"):
                out.append(f"{lane}/{key}: {k.get('status')} — {k.get('reason','')}")
    return out


def render_markdown(r: dict) -> str:
    L = [f"# PluginForge health — {r['generated']}  (`{r['git_head']}`)", ""]

    total_checks = total_fail = 0
    for lane in ("dsp", "ui"):
        for h in (r["lanes"][lane].get("harnesses") or {}).values():
            if h.get("status") == "ran":
                total_checks += h.get("checks", 0)
                total_fail += h.get("failures", 0)

    L += ["## Headline", "",
          f"- **{total_checks} assertions** across the DSP and UI harnesses, "
          f"**{total_fail} failing**."]

    orc = r["lanes"]["dsp"].get("oracle") or {}
    if orc.get("status") == "ran":
        L.append(f"- **Render oracle:** {orc['passed']} passed / {orc['failed']} failed "
                 f"/ {orc['unsupported']} unsupported / {orc['errored']} errored "
                 f"/ {orc.get('expectation_unmet', 0)} tail expectation unmet, over "
                 f"{orc['compiling_records']} compiling patches.")
    ai = r["lanes"]["ai"]
    sc = (ai.get("scored") or {}).get("per_run") or {}
    for name, v in sc.items():
        if "compile_rate" in v:
            L.append(f"- **Generation ({Path(name).name}):** "
                     f"{v['ok']}/{v['denominator']} = {v['compile_rate']:.0%} first-try compile.")
    nf = (ai.get("scored") or {}).get("noise_floor")
    if nf:
        L.append(f"- **Noise floor (PF-031):** spread of {nf['spread']:.1%} across "
                 f"{nf['runs']} identical runs. Smaller deltas are not signal.")
    L.append("")

    for lane, title in (("dsp", "DSP"), ("ui", "UI"), ("ai", "AI")):
        data = r["lanes"][lane]
        L += [f"## {title}", ""]
        if data.get("status") == "absent":
            L += [f"**ABSENT** — {data.get('reason')}", ""]
            continue
        hs = data.get("harnesses") or {}
        if hs:
            L += ["| harness | checks | failures | status |", "|---|---|---|---|"]
            for n, h in hs.items():
                if h.get("status") == "ran":
                    L.append(f"| `{n}` | {h.get('checks','?')} | {h.get('failures','?')} | ok |")
                else:
                    L.append(f"| `{n}` | — | — | **{h.get('status')}**: {h.get('reason','')} |")
            L.append("")
        if lane == "dsp":
            oracles = [o for o in (data.get("oracle"), data.get("oracle_secondary"))
                       if isinstance(o, dict) and o.get("status") == "ran"]
            for o in oracles:
                L += [f"### Render oracle — `{Path(o['source_results']).name}`", "",
                      f"{o['compiling_records']} of {o['source_records']} records compiled.", "",
                      "| verdict | n |", "|---|---|",
                      f"| passed | {o['passed']} |",
                      f"| failed | {o['failed']} |",
                      f"| unsupported (zero-input) | {o['unsupported']} |",
                      f"| errored | {o['errored']} |",
                      # .get(): re-rendering a pre-2026-07-31 archive has no such key.
                      f"| tail expectation unmet | {o.get('expectation_unmet', 0)} |", ""]
                bad = [p for p in o["patches"]
                       if p["analysis"].get("rendered")
                       and not p["analysis"]["measurement"]["ok"]]
                if bad:
                    L += ["**Failing patches:**", ""]
                    for p in bad:
                        m = p["analysis"]["measurement"]
                        L.append(f"- `{p['prompt']}` — {', '.join(m['reasons'])} "
                                 f"(rms {m['rms']:.3g}, peak {m['peak']:.3g}, "
                                 f"dc {m['dc_offset']:.3g})")
                    L.append("")
        for n, h in hs.items():
            if h.get("knob_order"):
                L += [f"**Knob order (PF-038)** from `{n}`: `{', '.join(h['knob_order'])}` — "
                      f"lexicographic: **{h['knob_order_is_lexicographic']}**", ""]
            if h.get("snapshots"):
                dims = sorted({"{}x{}".format(s["w"], s["h"]) for s in h["snapshots"]})
                L.append(f"**Snapshots** from `{n}`: {len(h['snapshots'])} PNGs "
                         f"({', '.join(dims)}).")
                L.append("")
        if lane == "ai":
            for name, v in sc.items():
                if "by_class" not in v:
                    continue
                L += [f"### `{Path(name).name}`", "",
                      f"{v['ok']}/{v['denominator']} = **{v['compile_rate']:.0%}** "
                      f"(transport excluded: {v['transport_excluded']})", "",
                      "| class | n |", "|---|---|"]
                for cls, n in sorted(v["by_class"].items(), key=lambda kv: -kv[1]):
                    L.append(f"| `{cls}` | {n} |")
                L.append("")
            if nf:
                L += ["### Noise floor (PF-031)", "",
                      f"Rates: {', '.join(f'{x:.1%}' for x in nf['rates'])} → "
                      f"spread **{nf['spread']:.1%}**.", "", nf["note"], ""]
            st = (ai.get("scored") or {}).get("stability") or {}
            if st.get("status") == "ran":
                L += ["### Failure stability — the number that matters more", "",
                      f"Over **{st['runs']} runs of the same prompt file**, "
                      f"{st['prompts_that_failed_at_least_once']} prompts failed at "
                      f"least once. Of those, **{len(st['always_failed'])}** failed every "
                      f"time, and only **{len(st['reproducible_prompt_and_class'])}** "
                      f"failed repeatedly with the *same* class.", "",
                      "| prompt | failed | classes seen |", "|---|---|---|"]
                for p, v in st["per_prompt"].items():
                    L.append(f"| {p[:60]} | {len(v)}/{st['runs']} | {', '.join(v)} |")
                L += ["", "A per-class delta read off a **single** run is therefore mostly "
                          "noise. Only a prompt that fails every run, with the same class, "
                          "is evidence of a defect rather than of sampling.", ""]

    L += ["## What this pass did NOT measure", ""]
    L += [f"- {x}" for x in r["no_instrument"]]
    L += ["", "*A harness that did not run is recorded as ABSENT, never as zero. "
              "A silent zero reads as health, and this project's recurring defect "
              "is a control believed to run that does not.*"]
    return "\n".join(L) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lane", choices=["dsp", "ui", "ai"])
    ap.add_argument("--collect", action="store_true")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero if anything was absent")
    ap.add_argument("--runs", type=int, default=2,
                    help="AI lane: how many identical benchmark runs (2 = noise floor)")
    ap.add_argument("--provider", default="groq")
    ap.add_argument("--oracle-results", metavar="PATH",
                    help="DSP lane: repo-relative benchmark archive for the oracle "
                         "to render. Defaults to bench/results/results.json, which "
                         "every benchmark run overwrites — name an archive when the "
                         "attribution has to survive the next run.")
    ap.add_argument("--i-authorize-spend", action="store_true", dest="authorized",
                    help="required for --lane ai; it spends provider quota")
    a = ap.parse_args()

    if not a.lane and not a.collect:
        ap.error("pass --lane {dsp,ui,ai} or --collect")

    if a.lane:
        LANEDIR.mkdir(parents=True, exist_ok=True)
        if a.lane == "dsp":
            data = lane_dsp(a.oracle_results)
        elif a.lane == "ui":
            data = lane_ui()
        else:
            data = lane_ai(a.runs, a.provider, a.authorized)
        (LANEDIR / f"{a.lane}.json").write_text(json.dumps(data, indent=2))
        print(f"lane {a.lane} -> {(LANEDIR / f'{a.lane}.json').relative_to(ROOT)}")
        for n, h in (data.get("harnesses") or {}).items():
            print(f"  {n:28s} {h.get('status'):18s} "
                  f"checks={h.get('checks','-')} failures={h.get('failures','-')}")

    return collect(a.strict) if a.collect else 0


if __name__ == "__main__":
    raise SystemExit(main())
