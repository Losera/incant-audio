#!/usr/bin/env python3
"""bench/frs_build_compare.py — does faust-rs accept/reject or diagnostic
content move across a build of it, on the full repair corpus?

WHY THIS EXISTS (2026-09-23 faust-rs delta audit,
docs/records/faust-rs-delta-2026-09-23.md)

`bench/frs_rederive.py` already re-derives the diagnostic-quality figures
(source location / stable code / help remedy) over the 15 never-compiled
efficacy cells. This module answers the companion question over the FULL
repair corpus (192 screened programs, `bench/corpus_screen.py`): does
accept/reject agreement between C++ `faust` and a given `faust-rs` binary hold
across a build of faust-rs, or does upstream drift move it on some subset this
smaller 15-cell check wouldn't see?

Pass one or more `label=/path/to/faust-rs` pairs to compare multiple builds in
one run — this is how the 2026-09-23 audit checked the true `0.8.0` tag, the
production binary this project actually measured PF-076 with, and current
`main`, side by side, and found all three agree 191/191 (the 192nd program
times out the C++ compiler itself, at >20s, independent of faust-rs — excluded
identically from every column so the comparison stays apples-to-apples).

Requires: the C++ `faust` compiler on PATH, and at least one faust-rs binary
(path passed explicitly — this compares SPECIFIC builds, so it does not fall
back to PLUGINFORGE_FAUST_RS_BIN / PATH the way bench/frs_check.py does for its
single-binary callers).

Usage:
    python bench/frs_build_compare.py tag_0.8.0=/path/to/tag/faust-rs \\
        main=/path/to/main/faust-rs
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
import corpus_screen  # noqa: E402
import frs_check  # noqa: E402

CORPUS = BENCH_DIR / "corpora" / "repair_corpus_20260830.json"
CPP_TIMEOUT_S = 20.0


class CompilerTimeout(Exception):
    """The C++ compiler itself hung on this program — not a faust-rs question."""


def cpp_ok(src: str) -> bool:
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", delete=False) as fh:
        fh.write(src)
        path = fh.name
    try:
        proc = subprocess.run(["faust", "-lang", "cpp", path, "-o", "/dev/null"],
                              capture_output=True, text=True, timeout=CPP_TIMEOUT_S,
                              errors="replace")
        return proc.returncode == 0
    except subprocess.TimeoutExpired:
        raise CompilerTimeout()
    finally:
        os.unlink(path)


def compare(corpus_path: Path, binaries: dict[str, str]) -> dict:
    """(label -> {n, agree, codes_present, disagreements}), plus n_timeouts."""
    recs = json.loads(corpus_path.read_text(encoding="utf-8"))
    included, _ = corpus_screen.screen(recs)

    cpp_verdicts: dict[str, bool] = {}
    cpp_timeouts: set[str] = set()
    for r in included:
        sha = frs_check.sha(r["code"])
        if sha in cpp_verdicts or sha in cpp_timeouts:
            continue
        try:
            cpp_verdicts[sha] = cpp_ok(r["code"])
        except CompilerTimeout:
            cpp_timeouts.add(sha)

    out = {"n_raw": len(recs), "n_screened": len(included),
          "n_cpp_timeouts": len(cpp_timeouts), "by_binary": {}}

    for label, bin_path in binaries.items():
        agree = codes_present = 0
        disagreements = []
        seen: set[str] = set()
        for r in included:
            sha = frs_check.sha(r["code"])
            if sha in seen or sha in cpp_timeouts:
                continue
            seen.add(sha)
            res = frs_check.check(r["code"], bin_path=bin_path)
            if res is None:
                continue
            c_ok, f_ok = cpp_verdicts[sha], res.ok
            if c_ok == f_ok:
                agree += 1
            else:
                disagreements.append(
                    {"effect_id": r.get("effect_id", "?"), "cpp_ok": c_ok,
                    "frs_ok": f_ok, "codes": res.codes})
            codes_present += bool(res.codes)
        out["by_binary"][label] = {"n": len(seen), "agree": agree,
                                    "codes_present": codes_present,
                                    "disagreements": disagreements}
    return out


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    binaries = dict(a.split("=", 1) for a in args)

    result = compare(CORPUS, binaries)
    print(f"n = {result['n_screened']} screened programs "
          f"(raw corpus: {result['n_raw']})\n")
    if result["n_cpp_timeouts"]:
        print(f"NOTE: {result['n_cpp_timeouts']} program(s) timed out the C++ "
              f"compiler (>{CPP_TIMEOUT_S:.0f}s) and are excluded from every "
              f"comparison below.\n")

    any_disagree = False
    for label, v in result["by_binary"].items():
        print(f"[{label}]  agreement {v['agree']}/{v['n']}   "
              f"codes-present {v['codes_present']}/{v['n']}")
        for d in v["disagreements"]:
            any_disagree = True
            print(f"    DISAGREE {d}")
    return 1 if any_disagree else 0


if __name__ == "__main__":
    sys.exit(main())
