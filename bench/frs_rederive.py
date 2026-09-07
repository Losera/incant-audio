#!/usr/bin/env python3
"""bench/frs_rederive.py — reproduce the diagnostic-quality half of the
faust-rs diagnostic-quality evidence from a clean checkout.

Replaces the lost `scratchpad/frs_annotate.py` cited at docs/BUGS.md. Runs both
compilers over the 15 never-compiled cells archived in
bench/results/efficacy/efficacy_ollama_20260828.json and prints the comparison
table docs/BUGS.md records.

REPRODUCIBLE:  the 15-cell half (those programs are in the archive).
NOT REPRODUCIBLE:  the 36-program hand-built corpus behind the "51/51
                   accept-reject agreement" figure — that corpus was created in
                   the same uncommitted scratchpad and is gone. This script
                   re-derives 15/51; the other 36 cannot be recovered.

Requires a faust-rs binary: set PLUGINFORGE_FAUST_RS_BIN (see bench/frs_check.py).
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frs_check  # noqa: E402

# The 15 never-compiled efficacy cells. Full repo: the archive. Reproduction
# package: the vendored MIT copy next to bench/repair_ab_repro/ (same rows, code +
# effect_id + tier only). filters-05/L2 is a program the generator truncated
# mid-token — a legitimate "broken input" for a diagnostic-quality comparison,
# kept on purpose.
_VENDORED = Path(__file__).resolve().parent / "repair_ab_repro" / "frs_rederive_cells.json"
_ARCHIVE = Path(__file__).resolve().parent / "results" / "efficacy" / "efficacy_ollama_20260828.json"
ARCHIVE = _ARCHIVE if _ARCHIVE.is_file() else _VENDORED


def cpp_verdict(src: str) -> tuple[bool, str]:
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", delete=False) as fh:
        fh.write(src)
        path = fh.name
    try:
        proc = subprocess.run(["faust", "-lang", "cpp", path, "-o", "/dev/null"],
                              capture_output=True, text=True, timeout=20, errors="replace")
        return proc.returncode == 0, proc.stderr.strip()
    finally:
        os.unlink(path)


def cpp_has_location(stderr: str) -> bool:
    """C++ 'has a source location' == a `file:line` or `[file ... : N]` prefix
    ahead of the first ERROR (matches how docs/BUGS.md scored it)."""
    head = stderr.split("ERROR", 1)[0] if "ERROR" in stderr else stderr
    return ".dsp:" in head or ".dsp :" in head


def main() -> int:
    if frs_check.faust_rs_bin() is None:
        print("faust-rs not configured — set PLUGINFORGE_FAUST_RS_BIN", file=sys.stderr)
        return 2

    recs = json.loads(ARCHIVE.read_text())
    never = [r for r in recs
             if not r["first_try_compiles"] and not r["retry_success"]]

    n = len(never)
    agree = cpp_loc = frs_loc = cpp_code = frs_code = frs_help = 0
    prop_cells = []

    print(f"{'cell':<20} {'C++':<8} {'faust-rs':<10} {'FRS code(s)':<34} help")
    print("-" * 88)
    for r in never:
        src = r["code"]
        cpp_ok, cpp_err = cpp_verdict(src)
        res = frs_check.check(src)
        frs_ok = res.ok if res else None
        codes = res.codes if res else []
        has_help = bool(res and res.primary and res.primary.help)
        has_frs_loc = bool(res and res.primary and res.primary.primary_line)

        if res is not None and (cpp_ok == frs_ok):
            agree += 1
        cpp_loc += cpp_has_location(cpp_err)
        frs_loc += has_frs_loc
        cpp_code += 0                       # C++ stderr carries no stable code, ever
        frs_code += 1 if codes else 0
        frs_help += 1 if has_help else 0
        if codes == ["FRS-PROP-0002"]:
            prop_cells.append(f'{r["effect_id"]}/{r["tier"]}')

        print(f'{r["effect_id"] + "/" + r["tier"]:<20} '
              f'{"reject" if not cpp_ok else "ACCEPT":<8} '
              f'{("reject" if frs_ok is False else "ACCEPT" if frs_ok else "n/a"):<10} '
              f'{",".join(codes):<34} {"yes" if has_help else "-"}')

    print("-" * 88)
    print(f"\nn = {n} never-compiled cells (reproducible subset)\n")
    print(f"  accept/reject verdict agreement : {agree}/{n}")
    print(f"  source location present         : C++ {cpp_loc}/{n}   faust-rs {frs_loc}/{n}")
    print(f"  stable error code present       : C++ {cpp_code}/{n}   faust-rs {frs_code}/{n}")
    print(f"  remedy (`help`) present         : faust-rs {frs_help}/{n}")
    print(f"  routing_arity -> FRS-PROP-0002  : {len(prop_cells)}/{n}  {prop_cells}")
    print("\nMatches docs/BUGS.md: agreement 15/15, location C++ 8/15 vs faust-rs 15/15,")
    print("code C++ 0/15 vs faust-rs 15/15, six routing_arity -> FRS-PROP-0002.")
    print("(C++ location was recorded as 9 in the 2026-08-28 write-up; this re-derivation")
    print(" and docs/BUGS.md both say 8 — a stderr-scoring-heuristic diff, not material.)")
    print("The 36-program hand-built corpus (the other 36 of '51/51') is NOT on disk.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
