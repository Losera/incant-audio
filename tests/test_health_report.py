"""Tests for tools/health_report.py — previously zero coverage anywhere in the repo.

Fixture discipline follows tests/test_classify_failures.py: regex fixtures are the
*actual* printf format strings the C++ harnesses emit, not invented approximations.
Sources, re-verified while writing this file:

    PF_SUMMARY   host/tests/EditorSessionTest.cpp:2119
                 `std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\\n", ...)`
    snapshot ->  host/tests/EditorSessionTest.cpp:145
                 `std::printf("      snapshot -> %s (%dx%d)\\n", ...)`
    knob order   host/tests/EditorSessionTest.cpp:406
                 `std::printf("      knob order on screen: %s\\n", ...)`
    evex/kmask/vex  host/tests/JitTargetTest.cpp:223
                 `printf("  %-10s evex=%-3d kmask=%-3d vex=%-3d\\n", ...)`
    CAPTURE_OK   host/tests/OfflineRenderTest.cpp:830-831
                 `std::printf("CAPTURE_OK wav=%s params=%d rms=%.6f peak=%.6f "
                 "dc=%.6f nan=%d inf=%d muted=%d instrument=%d note=%d vel=%d "
                 "sr=%d held_end=%d held_rms=%.6f tail_rms=%.6f\\n", ...)`

No C++ build is required: every test either exercises the regexes directly with
strings shaped exactly like those printf calls, or drives health_report's Python
logic (`collect`, `run_oracle_corpus`) against hand-written fixture files under
tmp_path, with tools/health_report.OUTDIR/LANEDIR monkeypatched so nothing touches
the real artifacts/health/ tree.
"""
from __future__ import annotations

import json
import os
import stat
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import health_report as hr  # noqa: E402


# ── Regex fixtures: verbatim-shaped harness output ─────────────────────────────


class TestSummaryRe:
    """PF_SUMMARY harness=%s checks=%d failures=%d — every harness's own tally line."""

    def test_matches_editor_session_test_line(self):
        line = "PF_SUMMARY harness=EditorSessionTest checks=143 failures=0\n"
        m = hr.SUMMARY_RE.search(line)
        assert m is not None
        assert m.group(1) == "EditorSessionTest"
        assert m.group(2) == "143"
        assert m.group(3) == "0"

    def test_matches_with_nonzero_failures(self):
        line = "PF_SUMMARY harness=OutputGuardTest checks=61 failures=3\n"
        m = hr.SUMMARY_RE.search(line)
        assert m is not None
        assert (m.group(1), m.group(2), m.group(3)) == ("OutputGuardTest", "61", "3")

    def test_finds_the_line_inside_a_larger_blob(self):
        # run_harness searches the whole stdout+stderr blob, not a single line.
        blob = (
            "some banner\n"
            "  more prose\n"
            "PF_SUMMARY harness=ParamMapTest checks=12 failures=0\n"
            "trailing junk\n"
        )
        m = hr.SUMMARY_RE.search(blob)
        assert m is not None
        assert m.group(1) == "ParamMapTest"


class TestSnapshotRe:
    """snapshot -> %s (%dx%d) — EditorSessionTest.cpp:145."""

    def test_matches_a_snapshot_line(self):
        line = "      snapshot -> fixture_003_afterGenerate.png (960x640)\n"
        m = hr.SNAPSHOT_RE.search(line)
        assert m is not None
        assert m.group(1) == "fixture_003_afterGenerate.png"
        assert m.group(2) == "960"
        assert m.group(3) == "640"

    def test_findall_collects_multiple_snapshots(self):
        blob = (
            "      snapshot -> a.png (100x200)\n"
            "      snapshot -> b.png (300x400)\n"
        )
        found = hr.SNAPSHOT_RE.findall(blob)
        assert found == [("a.png", "100", "200"), ("b.png", "300", "400")]


class TestKnoborderRe:
    """knob order on screen: %s — EditorSessionTest.cpp:406, comma-joined labels."""

    def test_matches_and_captures_the_full_label_list(self):
        line = "      knob order on screen: Cutoff, Resonance, Drive, Mix\n"
        m = hr.KNOBORDER_RE.search(line)
        assert m is not None
        assert m.group(1) == "Cutoff, Resonance, Drive, Mix"

    def test_run_harness_style_split_matches_labels(self):
        # Mirrors run_harness's own post-processing (health_report.py:194).
        line = "      knob order on screen: Cutoff, Resonance, Drive\n"
        m = hr.KNOBORDER_RE.search(line)
        labels = [s.strip() for s in m.group(1).split(",")]
        assert labels == ["Cutoff", "Resonance", "Drive"]


class TestCensusRe:
    """evex=/kmask=/vex= — JitTargetTest.cpp:223.

    printf("  %-10s evex=%-3d kmask=%-3d vex=%-3d\\n", name, evex, kmask, vex).
    %-Ns left-justifies within a field of width N (padding with trailing spaces,
    not leading), which is what the fixture strings below reproduce exactly by
    using the same %-10s/%-3d format specifiers Python and C share.
    """

    def _census_line(self, name: str, evex: int, kmask: int, vex: int) -> str:
        return "  %-10s evex=%-3d kmask=%-3d vex=%-3d\n" % (name, evex, kmask, vex)

    def test_matches_a_single_isolated_line(self):
        line = self._census_line("lowpass", 0, 1, 1)
        m = hr.CENSUS_RE.match(line)
        assert m is not None
        assert m.group(1) == "lowpass"
        assert (m.group(2), m.group(3), m.group(4)) == ("0", "1", "1")

    def test_findall_does_not_extract_from_realistic_multiline_output(self):
        """Documents a real gap, not a desired behaviour.

        CENSUS_RE is `r"^\\s+(\\w+)\\s+evex=..."` compiled WITHOUT re.MULTILINE
        (health_report.py, module scope near SUMMARY_RE). `^` without that flag is
        a zero-width assertion true only at index 0 of the whole string, not at the
        start of each line. JitTargetTest.cpp always prints a banner line
        ("\\nAVX-512 CPU (znver4) -- the CI runner's misdetected identity\\n",
        JitTargetTest.cpp:216) before any census line, so in the real
        `out = p.stdout + p.stderr` blob no census line ever sits at index 0, and
        `CENSUS_RE.findall(out)` returns `[]` on every real run -- run_harness's
        `isa_census` key (health_report.py:197-202) is therefore never populated in
        production, silently, which is exactly the "absent vs. zero" confusion this
        module's own docstring says it exists to prevent (health_report.py:22-27).

        This test is in scope for tests/-only additions and does not fix the
        regex (that would be a tools/ edit); it exists so the gap is asserted
        rather than merely noticed while reading the source.
        """
        realistic_out = (
            "\nconservative CPU (x86-64) -- the setting CI uses\n"
            "\nAVX-512 CPU (znver4) -- the CI runner's misdetected identity\n"
            + self._census_line("lowpass", 0, 1, 1)
            + self._census_line("hipass", 2, 0, 0)
            + "\nPASSED -- 0 failure(s)\n"
        )
        assert hr.CENSUS_RE.findall(realistic_out) == []

        # The isolated-line form (no preceding banner) is exactly what DOES match,
        # which is why the single-line test above passes while this one fails to
        # extract anything -- the difference is entirely about what precedes the
        # line in the buffer, not about the census line's own shape.
        isolated = self._census_line("lowpass", 0, 1, 1)
        assert hr.CENSUS_RE.findall(isolated) != []


class TestCaptureRe:
    """CAPTURE_OK ... — OfflineRenderTest.cpp:830. Defined but currently unused by
    run_harness (grep confirms no call site reads CAPTURE_RE); tested directly
    against the regex object since that is the only contract that exists."""

    def test_matches_an_effect_capture_line(self):
        line = (
            "CAPTURE_OK wav=/tmp/tmpxyz.wav params=12 rms=0.123456 peak=0.987654 "
            "dc=0.000012 nan=0 inf=0 muted=0 instrument=0 note=-1 vel=0 sr=48000 "
            "held_end=0 held_rms=0.000000 tail_rms=0.000000\n"
        )
        m = hr.CAPTURE_RE.search(line)
        assert m is not None
        wav, params, rms, peak, dc, nan, inf_, muted = m.groups()
        assert wav == "/tmp/tmpxyz.wav"
        assert params == "12"
        assert rms == "0.123456"
        assert peak == "0.987654"
        assert dc == "0.000012"
        assert (nan, inf_, muted) == ("0", "0", "0")

    def test_matches_an_instrument_capture_line_with_nonzero_flags(self):
        line = (
            "CAPTURE_OK wav=/tmp/note60.wav params=4 rms=0.500000 peak=0.900000 "
            "dc=0.010000 nan=1 inf=0 muted=1 instrument=1 note=60 vel=100 sr=48000 "
            "held_end=1024 held_rms=0.400000 tail_rms=0.050000\n"
        )
        m = hr.CAPTURE_RE.search(line)
        assert m is not None
        assert m.group(6) == "1"  # nan
        assert m.group(8) == "1"  # muted


# ── run_harness: end-to-end against a fake executable, no C++ build needed ─────


def _make_fake_binary(build_dir: Path, name: str, stdout_text: str) -> Path:
    """A tiny shell script standing in for a built C++ harness under BUILD."""
    subdir = build_dir / "tests"
    subdir.mkdir(parents=True, exist_ok=True)
    script = subdir / name
    script.write_text(f"#!/usr/bin/env bash\ncat <<'PF_EOF'\n{stdout_text}\nPF_EOF\n")
    script.chmod(script.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return script


class TestRunHarness:
    def test_absent_binary_is_reported_not_raised(self, tmp_path, monkeypatch):
        monkeypatch.setattr(hr, "BUILD", tmp_path / "nonexistent")
        rec = hr.run_harness("Nope", "NopeBinary", jits=False, needs_display=False)
        assert rec == {"status": "absent", "reason": "binary NopeBinary not built"}

    def test_needs_display_without_one_is_absent(self, tmp_path, monkeypatch):
        monkeypatch.setattr(hr, "BUILD", tmp_path)
        _make_fake_binary(tmp_path, "NeedsDisplay", "PF_SUMMARY harness=X checks=1 failures=0")
        monkeypatch.setattr(hr, "_have_display", lambda: False)
        rec = hr.run_harness("X", "NeedsDisplay", jits=False, needs_display=True)
        assert rec["status"] == "absent"
        assert "display" in rec["reason"]

    def test_clean_run_is_parsed(self, tmp_path, monkeypatch):
        monkeypatch.setattr(hr, "BUILD", tmp_path)
        _make_fake_binary(
            tmp_path, "CleanHarness",
            "PF_SUMMARY harness=CleanHarness checks=10 failures=0",
        )
        rec = hr.run_harness("CleanHarness", "CleanHarness", jits=False, needs_display=False)
        assert rec["status"] == "ran"
        assert rec["exit_code"] == 0
        assert rec["checks"] == 10
        assert rec["failures"] == 0

    def test_output_missing_pf_summary_is_handled_as_ran_unparseable(self, tmp_path, monkeypatch):
        """Malformed-input case: a harness that ran but printed no PF_SUMMARY line
        at all (e.g. crashed before its own tally, or the format string changed).
        run_harness must not raise -- it records a distinct, honest status."""
        monkeypatch.setattr(hr, "BUILD", tmp_path)
        _make_fake_binary(tmp_path, "NoSummary", "just some prose, no PF_SUMMARY here")
        rec = hr.run_harness("NoSummary", "NoSummary", jits=False, needs_display=False)
        assert rec["status"] == "ran_unparseable"
        assert rec["reason"] == "no PF_SUMMARY line in output"
        # "checks"/"failures" must NOT be silently defaulted to 0 -- that would be
        # exactly the absent/zero conflation health_report.py's docstring warns
        # against (health_report.py:22-27).
        assert "checks" not in rec
        assert "failures" not in rec

    def test_summary_line_missing_failures_field_is_also_unparseable(self, tmp_path, monkeypatch):
        """A line that ALMOST matches SUMMARY_RE -- present, but missing the
        `failures=` field entirely -- must not raise or be silently coerced."""
        monkeypatch.setattr(hr, "BUILD", tmp_path)
        _make_fake_binary(tmp_path, "PartialSummary", "PF_SUMMARY harness=PartialSummary checks=5")
        rec = hr.run_harness("PartialSummary", "PartialSummary", jits=False, needs_display=False)
        assert rec["status"] == "ran_unparseable"

    def test_snapshots_and_knob_order_are_captured_alongside_summary(self, tmp_path, monkeypatch):
        stdout_text = (
            "PF_SUMMARY harness=EditorSessionTest checks=2 failures=0\n"
            "      snapshot -> a.png (100x200)\n"
            "      knob order on screen: Cutoff, Drive\n"
        )
        monkeypatch.setattr(hr, "BUILD", tmp_path)
        _make_fake_binary(tmp_path, "EditorSessionTest", stdout_text)
        rec = hr.run_harness("EditorSessionTest", "EditorSessionTest", jits=False, needs_display=False)
        assert rec["status"] == "ran"
        assert rec["snapshots"] == [{"file": "a.png", "w": 100, "h": 200}]
        assert rec["knob_order"] == ["Cutoff", "Drive"]
        assert rec["knob_order_is_lexicographic"] is True


# ── collect(): the exit-code / strictness contract ──────────────────────────────


def _write_lane(lane_dir: Path, lane: str, data: dict) -> None:
    lane_dir.mkdir(parents=True, exist_ok=True)
    (lane_dir / f"{lane}.json").write_text(json.dumps(data))


class TestCollectContract:
    """collect(strict) merges tools/health_report.py's lane files into a dated
    report and returns 0, or 1 under --strict if anything was absent."""

    def _patch_dirs(self, tmp_path, monkeypatch):
        outdir = tmp_path / "artifacts" / "health"
        lanedir = outdir / "lanes"
        monkeypatch.setattr(hr, "OUTDIR", outdir)
        monkeypatch.setattr(hr, "LANEDIR", lanedir)
        return outdir, lanedir

    def test_all_lanes_clean_strict_succeeds(self, tmp_path, monkeypatch):
        outdir, lanedir = self._patch_dirs(tmp_path, monkeypatch)
        _write_lane(lanedir, "dsp", {
            "lane": "dsp",
            "harnesses": {"OutputGuardTest": {"status": "ran", "checks": 5, "failures": 0}},
            "tsan": {"status": "ran", "exit_code": 0, "clean": True},
            "oracle": {"status": "ran", "source_results": "bench/results/results.json",
                       "source_records": 1, "compiling_records": 1, "patches": [],
                       "passed": 1, "failed": 0, "unsupported": 0, "errored": 0,
                       "expectation_unmet": 0},
        })
        _write_lane(lanedir, "ui", {
            "lane": "ui",
            "harnesses": {"EditorSessionTest": {"status": "ran", "checks": 3, "failures": 0}},
        })
        _write_lane(lanedir, "ai", {"lane": "ai", "status": "ran", "runs": [], "scored": {}})

        rc = hr.collect(strict=True)

        assert rc == 0
        # And the artifact was actually written, not just a claimed return code.
        assert (outdir / f"health_{__import__('datetime').date.today():%Y%m%d}.json").exists()

    def test_missing_lane_is_absent_and_strict_fails(self, tmp_path, monkeypatch):
        outdir, lanedir = self._patch_dirs(tmp_path, monkeypatch)
        _write_lane(lanedir, "dsp", {
            "lane": "dsp", "harnesses": {}, "oracle": {"status": "absent",
                                                        "reason": "no results.json"},
        })
        _write_lane(lanedir, "ui", {"lane": "ui", "harnesses": {}})
        # ai.json deliberately not written -> collect() defaults it to absent
        # ("lane was not run", health_report.py:458-460).

        rc_strict = hr.collect(strict=True)
        assert rc_strict == 1

        # Non-strict must not raise and must still return 0, even with the same
        # absence on record -- strict only changes the exit code, never whether
        # collect() runs to completion.
        rc_lenient = hr.collect(strict=False)
        assert rc_lenient == 0

    def test_harness_with_failures_is_not_treated_as_absent(self, tmp_path, monkeypatch):
        """By this module's own design (docstring, health_report.py:13-15: "it does
        not judge. It counts."), a harness that RAN and reported nonzero failures is
        not an absence -- strict is about instrumentation coverage, not pass/fail.
        Confirmed by reading _absences() (health_report.py:477-490): it only checks
        `status != "ran"`, never `failures`."""
        outdir, lanedir = self._patch_dirs(tmp_path, monkeypatch)
        _write_lane(lanedir, "dsp", {
            "lane": "dsp",
            "harnesses": {"OutputGuardTest": {"status": "ran", "checks": 5, "failures": 5}},
        })
        _write_lane(lanedir, "ui", {"lane": "ui", "harnesses": {}})
        _write_lane(lanedir, "ai", {"lane": "ai", "status": "ran"})

        rc = hr.collect(strict=True)
        assert rc == 0

    def test_absences_names_the_specific_missing_thing(self, tmp_path, monkeypatch):
        self._patch_dirs(tmp_path, monkeypatch)
        report = {
            "lanes": {
                "dsp": {"status": "absent", "reason": "lane was not run"},
                "ui": {"harnesses": {"EditorSessionTest": {"status": "timeout"}}},
                "ai": {"status": "ran"},
            }
        }
        absences = hr._absences(report)
        assert absences == [
            "lane dsp: lane was not run",
            "ui/EditorSessionTest: timeout — ",
        ]


# ── Malformed input reaching a json.loads site ───────────────────────────────────


class TestMalformedJsonInputs:
    def test_collect_raises_on_a_corrupt_lane_file(self, tmp_path, monkeypatch):
        """collect() loads each lanes/<name>.json with a bare `json.loads` and no
        try/except (health_report.py:456-460) -- confirmed by reading the source,
        not assumed. A corrupt lane file is therefore NOT gracefully handled: it
        propagates a real json.JSONDecodeError out of collect(). This test records
        that as the actual current contract rather than guessing "it's probably
        fine" -- per the task's instruction not to assume what "handled" means
        here. It is in scope for a tests/-only change to assert this, not to
        soften it (that would be a tools/health_report.py edit)."""
        outdir = tmp_path / "artifacts" / "health"
        lanedir = outdir / "lanes"
        monkeypatch.setattr(hr, "OUTDIR", outdir)
        monkeypatch.setattr(hr, "LANEDIR", lanedir)
        lanedir.mkdir(parents=True)
        (lanedir / "dsp.json").write_text("{not actually json")
        (lanedir / "ui.json").write_text(json.dumps({"lane": "ui", "harnesses": {}}))
        (lanedir / "ai.json").write_text(json.dumps({"lane": "ai", "status": "ran"}))

        with pytest.raises(json.JSONDecodeError):
            hr.collect(strict=False)

    def test_run_oracle_corpus_raises_on_a_corrupt_results_file(self, tmp_path):
        """Same shape, different site: run_oracle_corpus's `json.loads(
        results_path.read_text())` (health_report.py:261) is likewise unguarded.
        A corrupt bench/results archive crashes the dsp lane rather than degrading
        to an 'absent'/'errored' oracle record. Documented, not fixed (tests-only
        scope)."""
        bad = tmp_path / "corrupt_results.json"
        bad.write_text("{not valid json")
        with pytest.raises(json.JSONDecodeError):
            hr.run_oracle_corpus(bad)

    def test_run_oracle_corpus_missing_file_is_handled_gracefully(self, tmp_path):
        """Contrast case: a MISSING results file (as opposed to a malformed one) IS
        handled, via an explicit existence check (health_report.py:258-259) before
        any json.loads call -- so this one does not raise."""
        missing = tmp_path / "does_not_exist.json"
        out = hr.run_oracle_corpus(missing)
        assert out == {"status": "absent", "reason": f"{missing} missing"}

    def test_run_oracle_corpus_per_record_errors_are_caught(self, tmp_path):
        """Once past the top-level json.loads, per-record failures inside the loop
        ARE caught (health_report.py:274-286: try/except around ro.analyse_record)
        and turned into an "errored" tally entry rather than aborting the whole
        corpus -- a record shaped so render_oracle can't analyse it (no 'prompt'
        source, or a type analyse_record chokes on) must not crash the run."""
        results = tmp_path / "results.json"
        # A record marked as compiling but missing everything analyse_record
        # actually needs (e.g. the Faust source) -- shaped to make analyse_record
        # raise internally rather than to satisfy it.
        results.write_text(json.dumps([
            {"first_try_compiles": True, "prompt": "malformed record", "category": "test"},
        ]))
        out = hr.run_oracle_corpus(results)
        assert out["status"] == "ran"
        assert out["compiling_records"] == 1
        assert out["errored"] + out["unsupported"] + out["passed"] + out["failed"] == 1


# ── Product lane: document-derived (docs/BUGS.md, docs/decisions.md, STATUS.md) ──


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


class TestScanBugsRegistry:
    """_scan_bugs_registry() reuses tests/test_bugs_registry_integrity.py's own
    parse_registry()/_REGISTRY_ROW_RE rather than a second parser (see
    docs/HEALTH_SCHEMA.md's "reuse, don't reimplement"), so these fixtures are
    shaped exactly like that module's own: 8-column registry rows."""

    def _bugs_md(self, root: Path, rows: str) -> None:
        _write(root / "docs" / "BUGS.md", "# Bug Registry\n\n## Registry\n\n"
               "| ID | Title | Sev | Status | Lane | File:line | Discovered | Closed |\n"
               "|---|---|---|---|---|---|---|---|\n" + rows)

    def test_counts_and_open_rows(self, tmp_path):
        self._bugs_md(tmp_path, (
            "| PF-001 | A fixed defect | high | fixed | S1 | `a.cpp:1` | 2026-01-01 | 2026-01-02 |\n"
            "| PF-002 | An open high-severity defect | high | open | S1 | `b.cpp:2` | 2026-01-01 | — |\n"
            "| PF-003 | An in-progress low defect | low | in-progress | S2 | `c.cpp:3` | 2026-01-01 | — |\n"
        ))
        out = hr._scan_bugs_registry(tmp_path)
        assert out["status"] == "ran"
        assert out["counts"] == {"fixed": 1, "open": 1, "in-progress": 1}
        ids = {r["id"] for r in out["open"]}
        assert ids == {"PF-002", "PF-003"}
        # Severity-ranked: high before low.
        assert [r["id"] for r in out["open"]] == ["PF-002", "PF-003"]

    def test_fixed_rows_are_not_in_the_open_list(self, tmp_path):
        self._bugs_md(tmp_path, (
            "| PF-010 | Closed long ago | medium | wontfix | S1 | `x.cpp:1` | 2026-01-01 | 2026-01-02 |\n"
        ))
        out = hr._scan_bugs_registry(tmp_path)
        assert out["open"] == []
        assert out["counts"] == {"wontfix": 1}

    def test_missing_file_is_absent(self, tmp_path):
        out = hr._scan_bugs_registry(tmp_path)
        assert out == {"status": "absent", "reason": "docs/BUGS.md missing"}


class TestScanAdrs:
    def _decisions_md(self, root: Path, body: str) -> None:
        _write(root / "docs" / "decisions.md", body)

    def test_finds_proposed_and_accepted(self, tmp_path):
        self._decisions_md(tmp_path, (
            "## ADR-001 — An accepted decision\n\n"
            "| | |\n|---|---|\n"
            "| **Status** | Accepted — 2026-01-01 |\n"
            "| **Date** | 2026-01-01 |\n\n"
            "## ADR-002 — A proposed decision\n\n"
            "| | |\n|---|---|\n"
            "| **Status** | Proposed (2026-02-01) |\n"
            "| **Date** | 2026-02-01 |\n"
        ))
        out = hr._scan_adrs(tmp_path)
        assert out["status"] == "ran"
        assert out["total"] == 2
        assert [a["id"] for a in out["proposed"]] == ["ADR-002"]
        assert out["proposed"][0]["title"] == "A proposed decision"

    def test_amended_proposed_status_still_counts_as_proposed(self, tmp_path):
        """ADR-036's real shape: 'Proposed (date). Amended ...: §1/§2
        superseded.' -- the scan reports the leading word, not the nuance
        (docs/HEALTH_SCHEMA.md explains why: the amendment prose is what a
        reader should go read, not what a health report should compress)."""
        self._decisions_md(tmp_path, (
            "## ADR-036 — Something amended\n\n"
            "| | |\n|---|---|\n"
            "| **Status** | Proposed (2026-09-03). "
            "**Amended 2026-09-04: direction picked; §1/§2 superseded.** |\n"
        ))
        out = hr._scan_adrs(tmp_path)
        assert [a["id"] for a in out["proposed"]] == ["ADR-036"]

    def test_no_adr_headers_is_ran_with_zero(self, tmp_path):
        self._decisions_md(tmp_path, "# Not an ADR file\n\nJust prose.\n")
        out = hr._scan_adrs(tmp_path)
        assert out == {"status": "ran", "total": 0, "proposed": []}

    def test_missing_file_is_absent(self, tmp_path):
        out = hr._scan_adrs(tmp_path)
        assert out == {"status": "absent", "reason": "docs/decisions.md missing"}


class TestScanStatusNextThree:
    def test_extracts_numbered_items_and_stops_at_next_heading(self, tmp_path):
        _write(tmp_path / "STATUS.md", (
            "## Broken — ranked\n\nsome other section\n\n"
            "## Next three things\n\n"
            "1. First thing.\n"
            "2. Second thing.\n"
            "3. Third thing.\n\n"
            "## Waiting on you\n\n"
            "1. Not part of Next three -- must not be captured.\n"
        ))
        out = hr._scan_status_next_three(tmp_path)
        assert out["status"] == "ran"
        assert out["count"] == 3
        assert out["items"][0] == "First thing."
        assert "Not part of" not in " ".join(out["items"])

    def test_section_absent_when_heading_missing(self, tmp_path):
        _write(tmp_path / "STATUS.md", "## Something else\n\nno next-three here\n")
        out = hr._scan_status_next_three(tmp_path)
        assert out["status"] == "absent"

    def test_missing_file_is_absent(self, tmp_path):
        out = hr._scan_status_next_three(tmp_path)
        assert out == {"status": "absent", "reason": "STATUS.md missing"}


class TestCheckAssumedDrift:
    """The known, previously-real drift: tools/check.sh's level_assumed() only
    counts '- **bolded**' LIST bullets; a section rewritten to lead with a
    bolded SENTENCE in plain prose undercounts against what a human reading
    it would call a named claim."""

    def test_bulleted_claims_agree_with_the_counter(self, tmp_path):
        _write(tmp_path / "STATUS.md", (
            "## Assumed, never checked\n\n"
            "- **A bulleted claim.** Some detail.\n"
            "- **Another bulleted claim.** More detail.\n\n"
            "## Next three things\n\nnothing here\n"
        ))
        out = hr._check_assumed_drift(tmp_path)
        assert out["status"] == "ran"
        assert out["counter_value"] == 2
        assert out["bold_claims_in_prose"] == 2
        assert out["drifted"] is False

    def test_a_prose_only_bold_claim_drifts(self, tmp_path):
        """This is the real historical shape (PF-079's claim): the section
        opens with a bolded sentence that is NOT a '- ' bullet."""
        _write(tmp_path / "STATUS.md", (
            "## Assumed, never checked\n\n"
            "**A claim written as plain prose, not a bullet.** Supporting detail "
            "follows in the same paragraph.\n\n"
            "## Next three things\n\nnothing here\n"
        ))
        out = hr._check_assumed_drift(tmp_path)
        assert out["status"] == "ran"
        assert out["counter_value"] == 0
        assert out["bold_claims_in_prose"] == 1
        assert out["drifted"] is True
        assert out["note"] is not None

    def test_no_claims_at_all_is_not_a_drift(self, tmp_path):
        _write(tmp_path / "STATUS.md", (
            "## Assumed, never checked\n\n*(none.)*\n\n## Next three things\n\nx\n"
        ))
        out = hr._check_assumed_drift(tmp_path)
        assert out["counter_value"] == 0
        assert out["bold_claims_in_prose"] == 0
        assert out["drifted"] is False

    def test_missing_file_is_absent(self, tmp_path):
        out = hr._check_assumed_drift(tmp_path)
        assert out == {"status": "absent", "reason": "STATUS.md missing"}


class TestLaneProduct:
    """lane_product() wires the four scans together; this just confirms the
    wiring, not the scans' own logic (covered above)."""

    def test_all_four_fields_present_and_ran(self, tmp_path):
        _write(tmp_path / "docs" / "BUGS.md", "# Bugs\n\n## Registry\n\n"
               "| ID | Title | Sev | Status | Lane | File:line | Discovered | Closed |\n"
               "|---|---|---|---|---|---|---|---|\n"
               "| PF-001 | x | high | open | S1 | `a:1` | 2026-01-01 | — |\n")
        _write(tmp_path / "docs" / "decisions.md",
               "## ADR-001 — x\n\n| | |\n|---|---|\n| **Status** | Proposed |\n")
        _write(tmp_path / "STATUS.md", (
            "## Assumed, never checked\n\n*(none.)*\n\n"
            "## Next three things\n\n1. Do a thing.\n\n## Waiting on you\n\nx\n"
        ))

        out = hr.lane_product(tmp_path)
        assert out["lane"] == "product"
        assert out["bugs"]["status"] == "ran"
        assert out["adrs"]["status"] == "ran"
        assert out["status_next_three"]["status"] == "ran"
        assert out["assumed_drift"]["status"] == "ran"
        assert "started" in out and "finished" in out

    def test_missing_docs_report_absent_per_field_not_a_crash(self, tmp_path):
        out = hr.lane_product(tmp_path)
        assert out["bugs"]["status"] == "absent"
        assert out["adrs"]["status"] == "absent"
        assert out["status_next_three"]["status"] == "absent"
        assert out["assumed_drift"]["status"] == "absent"


class TestCollectIncludesProductLane:
    """collect() runs the product lane INLINE when no lanes/product.json
    exists, rather than defaulting it to absent the way dsp/ui/ai are
    defaulted -- it is $0 and document-only, so there is no reason to make a
    caller run `--lane product` separately first (docs/HEALTH_SCHEMA.md)."""

    def test_collect_computes_product_lane_without_a_separate_run(
        self, tmp_path, monkeypatch
    ):
        outdir = tmp_path / "artifacts" / "health"
        lanedir = outdir / "lanes"
        monkeypatch.setattr(hr, "OUTDIR", outdir)
        monkeypatch.setattr(hr, "LANEDIR", lanedir)
        lanedir.mkdir(parents=True)
        (lanedir / "dsp.json").write_text(json.dumps({"lane": "dsp", "harnesses": {}}))
        (lanedir / "ui.json").write_text(json.dumps({"lane": "ui", "harnesses": {}}))
        (lanedir / "ai.json").write_text(json.dumps({"lane": "ai", "status": "ran"}))
        # Deliberately no lanes/product.json written.

        hr.collect(strict=False)

        date = __import__("datetime").date.today().strftime("%Y%m%d")
        report = json.loads((outdir / f"health_{date}.json").read_text())
        assert "product" in report["lanes"]
        assert report["lanes"]["product"]["lane"] == "product"

    def test_an_explicit_product_lane_file_is_honoured_over_the_inline_fallback(
        self, tmp_path, monkeypatch
    ):
        outdir = tmp_path / "artifacts" / "health"
        lanedir = outdir / "lanes"
        monkeypatch.setattr(hr, "OUTDIR", outdir)
        monkeypatch.setattr(hr, "LANEDIR", lanedir)
        lanedir.mkdir(parents=True)
        for name in ("dsp", "ui", "ai"):
            (lanedir / f"{name}.json").write_text(json.dumps({"lane": name}))
        sentinel = {"lane": "product", "marker": "explicit-run-not-overwritten"}
        (lanedir / "product.json").write_text(json.dumps(sentinel))

        hr.collect(strict=False)

        date = __import__("datetime").date.today().strftime("%Y%m%d")
        report = json.loads((outdir / f"health_{date}.json").read_text())
        assert report["lanes"]["product"] == sentinel
