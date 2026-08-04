#!/usr/bin/env python3
"""
Presentation affordance checker — deterministic, per-plugin present/absent
scoring on eight structural affordances a well-designed plugin UI uses and a
flat bank of unlabeled sliders does not.

WHY THIS EXISTS. The project's only generation-quality metric today is
`first_try_compiles` — did Faust accept the text. A flat bank of twelve
unlabeled sliders with linear tapers scores 100% on that and is unusable.
The eight affordances below are drawn from a single pluginmaker.ai sample
(a stereo-delay + tape-flanger generation) cross-checked against
spec-evidence/structure-gaps.md (Brief A): every one is natively expressible
in Faust today (hgroup/vgroup nesting, [style:knob], checkbox/button,
[unit:...], [scale:log]/[scale:exp]) and reachable from `ParamInfo` with NO
representation change — this measures whether the GENERATOR uses affordances
that already exist, not whether the runtime needs new ones.

This is a QUALITY signal, not a CORRECTNESS gate. It never fails a build
(tools/check.sh calls it as reporting only); FaustEngine.cpp's
validatePatch() (Brief F) is what refuses a patch that cannot run.

METHOD: compile with `faust -json` (2.85.9, same as Brief A) and read the
compiler's OWN ui tree — never regex the .dsp source for structure. This
matters concretely for the nested-group check: Faust wraps every compiled
file in one outer vgroup named after the filename (verified against every
ladder_corpus.json entry), which is not real grouping and must not be
counted as one; only the compiler's own tree, walked by POSITION, can tell
that wrapper apart from an author's real vgroup()/hgroup() reliably — the
same reasoning FaustEngine.cpp's ParamCapture::currentGroup() already
applies ("Tracking depth rather than testing the name keeps that robust
when a patch happens to name a real group the same thing as the file").

ONE DELIBERATE EXCEPTION: affordance 8 (label distinct from variable name)
compares a widget's declared LABEL against the FAUST DSL IDENTIFIER that
declares it. `faust -json` has no field for this — its own "varname" is a
compiler-generated internal name (e.g. "fHslider0"), never the source
identifier, so there is no JSON fact to compare against. This one check
reads the .dsp source with a narrow regex scoped to exactly the
widget-assignment call shape (`ident = hslider("label...", ...)`), not a
general structural parse — see OPEN_QUESTIONS.md for the alternatives
considered and what this narrow form does not cover (aliased or
`with{}`-local widget declarations).

Usage:
    python bench/presentation_checker.py --write-baseline
        Score every bench/ladder_corpus.json entry and OVERWRITE
        bench/presentation_baseline.json. Deliberate and explicit — run
        once, by hand, when the reference snapshot should move.

    python bench/presentation_checker.py --report
        Score every bench/ladder_corpus.json entry FRESH and print a
        summary. Never writes the baseline file. This is what
        tools/check.sh calls; it always exits 0.

    python bench/presentation_checker.py --file some.dsp
        Score one arbitrary Faust source file and print its JSON result —
        for A/B runs against hand-written components later.

    python bench/presentation_checker.py --selftest
        Verifies the nested-group exclusion against a known-flat corpus
        entry (see verify_known_flat_entry()). Exit 1 on failure.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORPUS_FILE = ROOT / "bench" / "ladder_corpus.json"
BASELINE_FILE = ROOT / "bench" / "presentation_baseline.json"

# Order matches the brief's numbered list (1-8).
AFFORDANCES = [
    "any_hgroup",
    "any_nested_group",
    "any_non_hslider_widget",
    "any_checkbox_or_button",
    "any_style_knob",
    "any_unit_metadata",
    "any_log_or_exp_scale",
    "labels_distinct_from_varnames",
]

_GROUP_TYPES = {"vgroup", "hgroup", "tgroup"}


class CheckerError(Exception):
    """The source did not compile under `faust -json`."""


def compile_ui_tree(dsp_source: str, timeout: int = 60) -> dict:
    """The full `faust -json` metadata dict for one source string. Raises
    CheckerError (not a crash) when the source does not compile — the
    caller decides what a non-compiling patch scores."""
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "p.dsp").write_text(dsp_source, encoding="utf-8")
        r = subprocess.run(["faust", "-json", "p.dsp", "-o", "/dev/null"], cwd=d,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=timeout)
        meta_path = d / "p.dsp.json"
        if not meta_path.exists():
            raise CheckerError(f"faust -json failed: {r.stderr.strip()[:300]}")
        return json.loads(meta_path.read_text(encoding="utf-8"))


def _all_nodes(root_items: list) -> list:
    """Every node in the tree, wrapper included — depth-first, pre-order."""
    out = []

    def walk(node):
        out.append(node)
        if node.get("type") in _GROUP_TYPES:
            for child in node.get("items", []):
                walk(child)

    for root in root_items:
        walk(root)
    return out


def _any_nested_group(root_items: list) -> bool:
    """True iff a group has a group ANCESTOR, after stripping exactly the
    compiler-inserted outermost wrapper level of `root_items` — by
    POSITION, matching ParamCapture::currentGroup()'s "i = 1 skips the
    wrapper" convention (FaustEngine.cpp), not by testing the wrapper's name
    against the filename (a patch may legitimately name a real group the
    same thing as the file)."""

    def descends_into_group(node, ancestor_is_group: bool) -> bool:
        found = False
        if node.get("type") in _GROUP_TYPES:
            if ancestor_is_group:
                found = True
            for child in node.get("items", []):
                if descends_into_group(child, ancestor_is_group=True):
                    found = True
        return found

    for wrapper in root_items:
        for child in wrapper.get("items", []):
            if descends_into_group(child, ancestor_is_group=False):
                return True
    return False


def _meta_value(node: dict, key: str):
    for m in node.get("meta", []):
        if key in m:
            return m[key]
    return None


# Widget-declaring assignment: `ident = hslider("full quoted string", ...)`.
# Captures the FULL quoted string; bracketed metadata is stripped afterward
# by _strip_meta_brackets, since Faust metadata tags may appear anywhere in
# the string (prefix, suffix, or interspersed), not only at the end.
_ASSIGN_WIDGET_RE = re.compile(
    r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*'
    r'(?:hslider|vslider|nentry|button|checkbox|hbargraph|vbargraph)\s*'
    r'\(\s*"([^"]*)"',
    re.MULTILINE,
)

_META_BRACKET_RE = re.compile(r"\[[^\]]*\]")


def _strip_meta_brackets(raw_label: str) -> str:
    """Remove every `[...]` metadata tag, matching what `faust -json`'s own
    "label" field already does regardless of where in the string the tag
    sits — verified against a prefix-form probe (`"[unit:Hz]cutoff"` compiles
    to JSON label "cutoff"), not only the suffix form the corpus happens to
    use (`"Cutoff [unit:Hz]"`)."""
    return _META_BRACKET_RE.sub("", raw_label).strip()


def _labels_distinct_from_varnames(dsp_source: str) -> tuple[bool, int]:
    """(result, source_matches). See the module docstring's "ONE DELIBERATE
    EXCEPTION" for why this is the one check that reads source text. A patch
    declaring no widgets at all scores False — there is nothing for a label
    to be distinct FROM, and an empty UI is not an example of good label
    design."""
    matches = _ASSIGN_WIDGET_RE.findall(dsp_source)
    if not matches:
        return False, 0
    return (all(ident != _strip_meta_brackets(raw) for ident, raw in matches),
            len(matches))


def score_source(dsp_source: str) -> dict:
    """Score one Faust source string. `total` is 0-8, or None when the
    source does not compile (a non-compiling patch has no presentation to
    score, and is a DIFFERENT failure this checker does not speak to — see
    FaustEngine.cpp's validatePatch(), Brief F)."""
    try:
        meta = compile_ui_tree(dsp_source)
    except CheckerError as exc:
        return {"compiles": False, "error": str(exc), "total": None,
                **{a: None for a in AFFORDANCES}}

    root_items = meta.get("ui", [])
    all_nodes = _all_nodes(root_items)
    widgets = [n for n in all_nodes if n.get("type") not in _GROUP_TYPES]

    labels_ok, label_matches = _labels_distinct_from_varnames(dsp_source)

    scores = {
        "any_hgroup": any(n.get("type") == "hgroup" for n in all_nodes),
        "any_nested_group": _any_nested_group(root_items),
        "any_non_hslider_widget": any(w.get("type") != "hslider" for w in widgets),
        "any_checkbox_or_button": any(w.get("type") in ("checkbox", "button")
                                      for w in widgets),
        "any_style_knob": any(str(_meta_value(w, "style") or "").startswith("knob")
                              for w in widgets),
        "any_unit_metadata": any(_meta_value(w, "unit") for w in widgets),
        "any_log_or_exp_scale": any(_meta_value(w, "scale") in ("log", "exp")
                                    for w in widgets),
        "labels_distinct_from_varnames": labels_ok,
    }
    total = sum(1 for a in AFFORDANCES if scores[a])
    result = {"compiles": True, "error": None, "total": total, **scores}

    # Coverage diagnostic, not a score: if the narrow assignment regex found
    # fewer matches than the compiler reports widgets, check 8's verdict is
    # incomplete (an aliased or `with{}`-local declaration slipped past it).
    # Recorded rather than hidden — see the module docstring.
    if len(widgets) > label_matches:
        result["labels_check_coverage"] = f"{label_matches}/{len(widgets)} widgets matched by source pattern"

    return result


def verify_known_flat_entry() -> bool:
    """The acceptance check the brief requires by name: confirm the
    nested-group exclusion against a KNOWN-FLAT corpus entry. Entry 0
    ("a simple stereo gain control with a dB slider") compiles to exactly
    the compiler's own wrapper vgroup containing one hslider directly — no
    author-written group at all — so any_nested_group and any_hgroup must
    both read False. If this ever reads True, the exclusion is broken and
    every other score in the baseline is suspect.
    """
    corpus = json.loads(CORPUS_FILE.read_text())
    entry = corpus[0]
    result = score_source(entry["code"])
    ok = result["compiles"] and result["any_nested_group"] is False \
        and result["any_hgroup"] is False
    print(f"selftest: entry 0 ({entry['category']!r}) -- "
          f"any_nested_group={result['any_nested_group']}, "
          f"any_hgroup={result['any_hgroup']}: {'OK' if ok else 'FAIL'}")
    return ok


def score_corpus() -> list:
    corpus = json.loads(CORPUS_FILE.read_text())
    results = []
    for i, entry in enumerate(corpus):
        r = score_source(entry["code"])
        r["index"] = i
        r["category"] = entry["category"]
        r["prompt"] = entry["prompt"]
        results.append(r)
    return results


def _print_summary(results: list) -> None:
    scored = [r for r in results if r["compiles"]]
    n = len(results)
    print(f"presentation affordances -- {CORPUS_FILE.relative_to(ROOT)}, {n} entries "
          f"({len(scored)} compiled and scored):")
    if scored:
        totals = [r["total"] for r in scored]
        mean = sum(totals) / len(totals)
        best = max(scored, key=lambda r: r["total"])
        print(f"  mean {mean:.2f}/8, max {best['total']}/8 "
              f"(entry {best['index']}, {best['category']!r})")
        for a in AFFORDANCES:
            count = sum(1 for r in scored if r[a])
            print(f"  {count}/{len(scored)}  {a}")
        # The brief's own stated prediction, checked against what actually ran.
        over_2 = [r for r in scored if r["total"] > 2]
        if over_2:
            print(f"  FINDING: {len(over_2)} entr{'y' if len(over_2)==1 else 'ies'} "
                  f"scored above 2/8, contradicting the near-0/8 prediction:")
            for r in over_2:
                hits = [a for a in AFFORDANCES if r[a]]
                print(f"    entry {r['index']} ({r['category']!r}): {r['total']}/8 -- {hits}")
    not_compiled = [r for r in results if not r["compiles"]]
    if not_compiled:
        print(f"  {len(not_compiled)} entries did not compile: "
              f"{[r['index'] for r in not_compiled]}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--write-baseline", action="store_true",
                        help="Score the corpus and overwrite "
                             f"{BASELINE_FILE.relative_to(ROOT)}.")
    parser.add_argument("--report", action="store_true",
                        help="Score the corpus fresh and print a summary. "
                             "Never writes the baseline file. Always exits 0 "
                             "-- this is a quality report, not a gate.")
    parser.add_argument("--file", type=Path, metavar="PATH",
                        help="Score one arbitrary Faust source file and "
                             "print its JSON result.")
    parser.add_argument("--selftest", action="store_true",
                        help="Verify the nested-group exclusion against a "
                             "known-flat corpus entry. Exit 1 on failure.")
    args = parser.parse_args(argv)

    if args.selftest:
        return 0 if verify_known_flat_entry() else 1

    if args.file:
        result = score_source(args.file.read_text())
        print(json.dumps(result, indent=2))
        return 0

    if args.write_baseline:
        if not verify_known_flat_entry():
            print("Refusing to write a baseline while the selftest fails.",
                  file=sys.stderr)
            return 1
        results = score_corpus()
        BASELINE_FILE.write_text(json.dumps(results, indent=2) + "\n")
        _print_summary(results)
        print(f"\nWrote {BASELINE_FILE.relative_to(ROOT)}", file=sys.stderr)
        return 0

    # --report, or no args: read-only, always exits 0.
    _print_summary(score_corpus())
    return 0


if __name__ == "__main__":
    sys.exit(main())
