"""PF-014 — real user prompts are recorded.

WHY THIS EXISTS
---------------
Until 2026-07-28 nothing in this project had ever recorded what a person actually typed
into it. All 25 benchmark prompts were invented by us, so the corpus measured our
imagination of the product rather than its use, and no failure class could be weighted by
how often it really occurs. PF-014 sat open from 2026-07-23 with a one-line description
and no code.

Two properties matter more than the logging itself, and both have red cases here:

  * ISOLATION — the log records the C++ host's subprocess path only. The bench harnesses
    call generate_faust/generate_with_retry directly and must never appear in it. A log
    contaminated by 25 synthetic prompts per benchmark run is worse than no log, because
    it looks like data.

  * FAIL-OPEN — unlike this project's hooks, a logging failure must cost a log line and
    never the user's generation. A hook exists to stop the work; this exists to observe
    it. The test for that drives a genuinely unwritable path.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

PROJECT_ROOT = Path(__file__).parent.parent
GENERATE = PROJECT_ROOT / "llm" / "generate.py"

sys.path.insert(0, str(PROJECT_ROOT / "llm"))
import generate  # noqa: E402


@pytest.fixture
def log_path(tmp_path, monkeypatch):
    p = tmp_path / "nested" / "prompts.jsonl"
    monkeypatch.setenv("PLUGINFORGE_PROMPT_LOG", str(p))
    return p


def _records(path: Path) -> list[dict]:
    return [json.loads(ln) for ln in path.read_text().splitlines() if ln.strip()]


# --------------------------------------------------------------------------- basics


class TestRecordShape:
    def test_a_successful_generation_is_recorded(self, log_path):
        generate.log_user_prompt(
            {"prompt": "a warm tape delay", "provider": "groq", "model": "m"},
            {"success": True, "faust_code": "process = _;", "attempts": 1,
             "error": None, "reason": "ok"},
        )
        (rec,) = _records(log_path)
        assert rec["prompt"] == "a warm tape delay"
        assert rec["provider"] == "groq"
        assert rec["success"] is True
        assert rec["reason"] == "ok"
        assert rec["faust_code"] == "process = _;"
        assert rec["ts"].endswith("Z")

    def test_a_failed_generation_is_recorded_with_its_reason(self, log_path):
        """The interesting half. A log of only the successes measures nothing."""
        generate.log_user_prompt(
            {"prompt": "ping-pong delay", "provider": "groq"},
            {"success": False, "faust_code": None, "attempts": 3,
             "error": "endless evaluation cycle", "reason": "invalid_faust"},
        )
        (rec,) = _records(log_path)
        assert rec["success"] is False
        assert rec["reason"] == "invalid_faust"
        assert rec["attempts"] == 3
        assert "endless evaluation cycle" in rec["error"]

    def test_records_append_rather_than_overwrite(self, log_path):
        for i in range(3):
            generate.log_user_prompt({"prompt": f"p{i}"}, {"success": True, "reason": "ok"})
        assert [r["prompt"] for r in _records(log_path)] == ["p0", "p1", "p2"]

    def test_non_ascii_prompts_survive_a_round_trip(self, log_path):
        """A person typing “°”, an em dash or a curly quote is not an edge case."""
        weird = "a 12 dB/oct filter — “warm”, 440 Hz ±2¢"
        generate.log_user_prompt({"prompt": weird}, {"success": True, "reason": "ok"})
        assert _records(log_path)[0]["prompt"] == weird

    def test_the_parent_directory_is_created(self, log_path):
        assert not log_path.parent.exists()
        generate.log_user_prompt({"prompt": "x"}, {"success": True, "reason": "ok"})
        assert log_path.exists()


# ----------------------------------------------------------------------- fail-open


class TestFailsOpen:
    def test_an_unwritable_path_does_not_raise(self, tmp_path, monkeypatch, capsys):
        """The red case. Logging must never be able to cost a user their generation."""
        blocked = tmp_path / "ro"
        blocked.mkdir()
        blocked.chmod(0o500)  # r-x: cannot create inside
        monkeypatch.setenv("PLUGINFORGE_PROMPT_LOG", str(blocked / "sub" / "prompts.jsonl"))
        try:
            generate.log_user_prompt({"prompt": "x"}, {"success": True, "reason": "ok"})
        finally:
            blocked.chmod(0o700)
        assert "not recorded" in capsys.readouterr().err, (
            "a logging failure must be reported on stderr, not swallowed in silence"
        )

    def test_an_unserialisable_response_does_not_raise(self, log_path, capsys):
        generate.log_user_prompt({"prompt": "x"}, {"success": True, "reason": object()})
        assert "not recorded" in capsys.readouterr().err

    def test_stderr_never_pollutes_stdout(self, log_path, capsys):
        """ADR-011: exactly one JSON line on stdout. A log warning must not join it."""
        generate.log_user_prompt({"prompt": "x"}, {"success": True, "reason": object()})
        assert capsys.readouterr().out == ""


# --------------------------------------------------------------------------- opt-out


class TestOptOut:
    @pytest.mark.parametrize("value", ["0", "off", "false", "no", "", "  OFF  "])
    def test_logging_can_be_disabled(self, tmp_path, monkeypatch, value):
        monkeypatch.setenv("PLUGINFORGE_PROMPT_LOG", value)
        assert generate._prompt_log_path() is None

    def test_the_default_path_is_inside_the_repo_logs_dir(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_PROMPT_LOG", raising=False)
        p = generate._prompt_log_path()
        assert p is not None
        assert p.parent.name == "logs" and p.name == "prompts.jsonl"

    def test_the_default_log_directory_is_gitignored(self):
        """A record of what a person typed is observation data, not repo content."""
        assert "/logs/" in (PROJECT_ROOT / ".gitignore").read_text(), (
            "logs/ is not gitignored — real user prompts would be committed"
        )


# ------------------------------------------------------------------------ isolation


class TestOnlyRealUserPromptsAreLogged:
    """The property that decides whether the log is worth anything."""

    def test_the_benchmark_harness_does_not_reach_the_logger(self):
        """run_benchmark.py must not call the subprocess path or the logger.

        Asserted against the source: proving it by running the benchmark would spend
        25 prompts of free-tier quota (PF-025's lesson about tests that cost money).
        """
        src = (PROJECT_ROOT / "bench" / "run_benchmark.py").read_text()
        for forbidden in ("generate_json", "log_user_prompt"):
            assert forbidden not in src, (
                f"bench/run_benchmark.py references {forbidden!r}. If the benchmark "
                "starts going through the host's subprocess path, every run injects 25 "
                "synthetic prompts into the real-user log and the corpus it feeds "
                "becomes self-referential (PF-014)."
            )
        # The other route in: shelling out to generate.py the way the C++ host does.
        # Match a QUOTED argv token, not a bare mention — the file cites
        # `llm/generate.py::validate_faust` in a comment, and prose is not a call.
        # Its only subprocess calls today are to `faust` itself (:157, :221).
        invocation = re.search(r"""['"][^'"]*\bgenerate\.py['"]""", src)
        assert invocation is None, (
            f"bench/run_benchmark.py invokes generate.py as a subprocess "
            f"({invocation.group(0) if invocation else ''}). That is the host's path, "
            "and it logs — benchmark prompts would enter the real-user record (PF-014)."
        )

    def test_the_logger_is_called_from_the_subprocess_entry_point(self):
        """The other direction: the one path the C++ host uses must still log."""
        src = GENERATE.read_text()
        body = src[src.index("def _run_subprocess_mode"):src.index('if __name__ == "__main__"')]
        assert body.count("log_user_prompt(") == 2, (
            "_run_subprocess_mode must log on BOTH the normal and the exception path — "
            "a prompt that blew up is the most interesting kind to have recorded."
        )

    def test_recommendation_pass_is_not_counted_as_a_generation(self):
        request = {"action": "recommend", "prompt": "a warm filter"}
        response = {"success": True, "action": "recommend", "reason": "ok"}
        with patch.object(generate.providers, "check_credentials", return_value=None), \
             patch.object(generate, "process_json_request", return_value=response), \
             patch.object(generate, "log_user_prompt") as log:
            generate._run_subprocess_mode(lambda: request)
        log.assert_not_called()


# ------------------------------------------------------------------- end-to-end wire


class TestSubprocessModeWritesTheLog:
    def test_prompt_mode_logs_a_failed_generation_end_to_end(self, tmp_path):
        """Drives the real wire with no network and no quota.

        Provider `ollama` is local and needs no credential (providers.py:339-340), so
        the precheck passes and execution reaches generate_json — which then fails to
        reach a server that is not running. That is the exception path, and it is the
        one that must log: argv parsing, _run_subprocess_mode, the logger, and the
        one-JSON-line-on-stdout contract, none of which a unit test covers.

        A missing *credential* deliberately does NOT log: the precheck returns before
        any generation is attempted, so there is no outcome to record.
        """
        log = tmp_path / "prompts.jsonl"
        env = {
            **os.environ,
            "PLUGINFORGE_PROMPT_LOG": str(log),
            "PLUGINFORGE_PROVIDER": "ollama",
            "PLUGINFORGE_MODEL": "",
            "PLUGINFORGE_GENERATION_BUDGET": "10",
        }
        proc = subprocess.run(
            [sys.executable, str(GENERATE), "--prompt", "a resonant lowpass"],
            capture_output=True, text=True, timeout=180, env=env, cwd=str(PROJECT_ROOT),
        )
        assert proc.returncode == 0, proc.stderr[-500:]
        lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
        assert len(lines) == 1, f"ADR-011 requires exactly one stdout line, got {lines}"
        payload = json.loads(lines[0])

        if payload.get("success"):
            pytest.skip("an ollama server is actually running; this case needs a failure")
        assert log.exists(), (
            "a generation ran and failed, and nothing was written to the prompt log. "
            "The failures are the half of PF-014 worth having."
        )
        (rec,) = _records(log)
        assert rec["prompt"] == "a resonant lowpass"
        assert rec["success"] is False
