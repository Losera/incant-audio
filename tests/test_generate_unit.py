"""Unit tests for llm/generate.py — no Anthropic API key or faust compiler required."""
import json
import os
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch, MagicMock, call
import pytest

# conftest.py sets ANTHROPIC_API_KEY before this import.
import generate  # noqa: E402  (llm/ is on sys.path via conftest)

VALID_FAUST = '''\
import("stdfaust.lib");
gain = hslider("Gain [unit:dB]", 0, -60, 12, 0.1) : ba.db2linear;
process = _ * gain, _ * gain;\
'''


def _api_response(text: str) -> MagicMock:
    block = MagicMock()
    block.text = text
    resp = MagicMock()
    resp.content = [block]
    return resp


# ---------------------------------------------------------------------------
# SystemPrompt
# ---------------------------------------------------------------------------

class TestSystemPrompt:
    def test_loaded_at_import(self):
        assert generate.SYSTEM_PROMPT
        assert len(generate.SYSTEM_PROMPT) > 100

    def test_contains_stdfaust_import(self):
        assert 'import("stdfaust.lib")' in generate.SYSTEM_PROMPT

    def test_contains_process_keyword(self):
        assert "process" in generate.SYSTEM_PROMPT

    def test_contains_hslider(self):
        assert "hslider" in generate.SYSTEM_PROMPT

    def test_has_multiple_examples(self):
        assert generate.SYSTEM_PROMPT.count("process =") >= 2


# ---------------------------------------------------------------------------
# generate_faust()
# ---------------------------------------------------------------------------

class TestGenerateFaust:
    def test_returns_stripped_response_text(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response("  " + VALID_FAUST + "\n\n")):
            result = generate.generate_faust("a gain plugin")
        assert result == VALID_FAUST

    def test_passes_user_prompt_as_message(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("stereo reverb")
        msgs = mock_create.call_args[1]["messages"]
        assert any("stereo reverb" in m["content"] for m in msgs)

    def test_injects_system_prompt(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("a filter")
        assert mock_create.call_args[1]["system"] == generate.SYSTEM_PROMPT

    def test_error_context_appended_on_retry(self):
        error_msg = "undefined symbol 'foo'"
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("a filter", error_context=error_msg)
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert error_msg in combined

    def test_no_error_prefix_when_error_context_empty(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("a filter", error_context="")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert "compiler error" not in combined


# ---------------------------------------------------------------------------
# generate_faust() — prior_source folding (A3)
# ---------------------------------------------------------------------------

class TestGenerateFaustPriorSource:
    def test_folds_prior_source_ahead_of_the_user_prompt(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make the resonance stronger",
                                    prior_source="process = _ * 0.5;")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert "process = _ * 0.5;" in combined
        assert "MODIFY the following existing Faust program" in combined
        assert "make the resonance stronger" in combined
        # The fold is a preamble ahead of the request, not appended after it.
        assert combined.index("process = _ * 0.5;") < combined.index(
            "make the resonance stronger")

    def test_no_prior_source_leaves_message_byte_identical_to_today(self):
        """The negative half: an absent prior_source must change nothing."""
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make the resonance stronger")
        msgs = mock_create.call_args[1]["messages"]
        user_msgs = [m["content"] for m in msgs if m["role"] == "user"]
        assert user_msgs == ["make the resonance stronger"]

    def test_empty_string_prior_source_treated_same_as_absent(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make it louder", prior_source="")
        msgs = mock_create.call_args[1]["messages"]
        user_msgs = [m["content"] for m in msgs if m["role"] == "user"]
        assert user_msgs == ["make it louder"]


# ---------------------------------------------------------------------------
# generate_faust() — refine_mode preamble selection (PromptPanel's 3-mode
# ComboBox: New/Add/Redo)
# ---------------------------------------------------------------------------

class TestGenerateFaustRefineMode:
    def test_surgical_uses_surgical_preamble(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("add a low-pass filter",
                                    prior_source="process = _ * 0.5;",
                                    refine_mode="surgical")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert "MINIMAL, SURGICAL change" in combined
        assert "process = _ * 0.5;" in combined
        # NOT the legacy or context framing.
        assert "MODIFY the following existing Faust program, not write" not in combined
        assert "free to REWRITE it from scratch" not in combined

    def test_context_uses_context_preamble(self):
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make it sound completely different",
                                    prior_source="process = _ * 0.5;",
                                    refine_mode="context")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert "free to REWRITE it from scratch" in combined
        assert "process = _ * 0.5;" in combined
        assert "MINIMAL, SURGICAL change" not in combined

    def test_absent_refine_mode_keeps_legacy_preamble(self):
        """refine_mode=None (the default) must reproduce today's exact framing —
        an older host, or a request that never sets the field, gets the
        pre-refine_mode behavior byte for byte."""
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make the resonance stronger",
                                    prior_source="process = _ * 0.5;")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert generate._REFINE_PREAMBLE.format(prior="process = _ * 0.5;") in combined
        assert "MINIMAL, SURGICAL change" not in combined
        assert "free to REWRITE it from scratch" not in combined

    def test_unknown_refine_mode_falls_back_to_legacy_preamble(self):
        """Degrade, don't fail: a future/typo'd refine_mode value must not crash
        or silently drop the prior source — same 'degrade, don't fail' rule
        select_prompt() uses for an unrecognised `kind`."""
        with patch.object(generate.client.messages, "create",
                          return_value=_api_response(VALID_FAUST)) as mock_create:
            generate.generate_faust("make it louder",
                                    prior_source="process = _ * 0.5;",
                                    refine_mode="bogus")
        msgs = mock_create.call_args[1]["messages"]
        combined = " ".join(m["content"] for m in msgs)
        assert generate._REFINE_PREAMBLE.format(prior="process = _ * 0.5;") in combined


# ---------------------------------------------------------------------------
# validate_faust()
# ---------------------------------------------------------------------------

class TestValidateFaust:
    def _mock_run(self, returncode: int, stderr: str = "") -> MagicMock:
        m = MagicMock()
        m.returncode = returncode
        m.stderr = stderr
        return m

    def test_valid_code_returns_true(self):
        with patch("subprocess.run", return_value=self._mock_run(0)):
            ok, err = generate.validate_faust(VALID_FAUST)
        assert ok is True
        assert err == ""

    def test_invalid_code_returns_false_with_message(self):
        with patch("subprocess.run", return_value=self._mock_run(1, "ERROR : bad symbol")):
            ok, err = generate.validate_faust("bad faust")
        assert ok is False
        assert "ERROR" in err

    def test_calls_faust_compiler(self):
        with patch("subprocess.run", return_value=self._mock_run(0)) as mock_run:
            generate.validate_faust(VALID_FAUST)
        cmd = mock_run.call_args[0][0]
        assert cmd[0] == "faust"

    def test_temp_file_removed_after_success(self, tmp_path, monkeypatch):
        removed = []
        original_unlink = os.unlink
        monkeypatch.setattr(os, "unlink", lambda p: removed.append(p) or original_unlink(p))
        with patch("subprocess.run", return_value=self._mock_run(0)):
            generate.validate_faust(VALID_FAUST)
        assert len(removed) == 1
        assert not Path(removed[0]).exists()

    def test_temp_file_removed_after_failure(self, monkeypatch):
        removed = []
        original_unlink = os.unlink
        monkeypatch.setattr(os, "unlink", lambda p: removed.append(p) or original_unlink(p))
        with patch("subprocess.run", return_value=self._mock_run(1, "error")):
            generate.validate_faust("bad code")
        assert len(removed) == 1
        assert not Path(removed[0]).exists()

    def test_stderr_is_stripped(self):
        with patch("subprocess.run", return_value=self._mock_run(1, "  ERROR  \n")):
            _, err = generate.validate_faust("bad")
        assert not err.startswith(" ")
        assert not err.endswith("\n")


# ---------------------------------------------------------------------------
# generate_with_retry()
# ---------------------------------------------------------------------------

class TestGenerateWithRetry:
    def test_returns_code_on_first_valid_attempt(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            result = generate.generate_with_retry("a gain plugin")
        assert "process" in result
        assert "import" in result

    def test_retries_and_succeeds_on_second_attempt(self):
        validate_calls = {"n": 0}

        def flaky_validate(_code, *_):
            validate_calls["n"] += 1
            return (False, "compile error") if validate_calls["n"] == 1 else (True, "")

        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", side_effect=flaky_validate):
            result = generate.generate_with_retry("a plugin", max_retries=3)

        assert validate_calls["n"] == 2
        assert result == VALID_FAUST

    def test_passes_error_to_second_generate_call(self):
        compile_error = "ERROR : undefined 'bad_fn'"
        generate_calls = []

        def tracking_generate(prompt, error_context="", **_):
            generate_calls.append(error_context)
            return VALID_FAUST

        def validate_after_first(code, *_):
            return (False, compile_error) if len(generate_calls) == 1 else (True, "")

        with patch.object(generate, "generate_faust", side_effect=tracking_generate), \
             patch.object(generate, "validate_faust", side_effect=validate_after_first):
            generate.generate_with_retry("a plugin", max_retries=3)

        assert generate_calls[0] == ""
        assert compile_error in generate_calls[1]

    def test_raises_after_exhausting_retries(self):
        with patch.object(generate, "generate_faust", return_value="bad"), \
             patch.object(generate, "validate_faust", return_value=(False, "always broken")):
            with pytest.raises(RuntimeError, match="Failed to generate"):
                generate.generate_with_retry("a plugin", max_retries=3)

    def test_exactly_max_retries_attempts_made(self):
        attempt_count = {"n": 0}

        def count_generate(prompt, error_context="", **_):
            attempt_count["n"] += 1
            return "bad"

        with patch.object(generate, "generate_faust", side_effect=count_generate), \
             patch.object(generate, "validate_faust", return_value=(False, "error")):
            with pytest.raises(RuntimeError):
                generate.generate_with_retry("plugin", max_retries=3)

        assert attempt_count["n"] == 3

    def test_respects_custom_max_retries(self):
        attempt_count = {"n": 0}

        def count_generate(prompt, error_context="", **_):
            attempt_count["n"] += 1
            return "bad"

        with patch.object(generate, "generate_faust", side_effect=count_generate), \
             patch.object(generate, "validate_faust", return_value=(False, "error")):
            with pytest.raises(RuntimeError):
                generate.generate_with_retry("plugin", max_retries=5)

        assert attempt_count["n"] == 5


# ---------------------------------------------------------------------------
# generate_json() — JSON wire mode (ADR-011 contract)
# ---------------------------------------------------------------------------

class TestGenerateJson:
    BASE_REQUEST = {
        "prompt": "a gain plugin",
        "provider": "anthropic",
        "model": "claude-opus-4-6",
        "max_retries": 3,
    }

    def test_success_response_shape(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            result = generate.generate_json(self.BASE_REQUEST)
        assert result["success"] is True
        assert result["faust_code"] == VALID_FAUST
        assert result["attempts"] == 1
        assert result["error"] is None

    def test_failure_response_shape(self):
        with patch.object(generate, "generate_faust", return_value="bad"), \
             patch.object(generate, "validate_faust", return_value=(False, "bad symbol")):
            result = generate.generate_json({**self.BASE_REQUEST, "max_retries": 2})
        assert result["success"] is False
        assert result["faust_code"] is None
        assert result["attempts"] == 2
        assert "bad symbol" in result["error"]

    def test_attempt_count_reflects_actual_tries(self):
        calls = {"n": 0}

        def flaky(code, *_):
            calls["n"] += 1
            return (True, "") if calls["n"] == 2 else (False, "err")

        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", side_effect=flaky):
            result = generate.generate_json(self.BASE_REQUEST)
        assert result["attempts"] == 2

    def test_defaults_to_claude_when_provider_omitted(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            generate.generate_json({"prompt": "a filter", "max_retries": 1})
        _, kwargs = mock_gen.call_args
        assert kwargs.get("provider", generate.DEFAULT_PROVIDER) == "anthropic"

    def test_unknown_provider_raises(self):
        with pytest.raises(ValueError, match="Unknown provider"):
            generate.generate_json({**self.BASE_REQUEST, "provider": "openai", "max_retries": 1})


# ---------------------------------------------------------------------------
# generate_json() — prior_source preflight (A6)
# ---------------------------------------------------------------------------

class TestGenerateJsonPriorSourcePreflight:
    BASE_REQUEST = {"prompt": "make it louder", "provider": "anthropic",
                    "model": "claude-opus-4-6", "max_retries": 1}

    def test_no_prior_source_never_calls_preflight(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source") as mock_preflight:
            generate.generate_json(self.BASE_REQUEST)
        mock_preflight.assert_not_called()
        assert mock_gen.call_args.kwargs["prior_source"] is None

    def test_fitting_prior_source_is_sent_whole(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=True):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "process = _;"})
        assert mock_gen.call_args.kwargs["prior_source"] == "process = _;"
        assert "prior_source_dropped" not in result

    def test_oversized_prior_source_is_dropped_not_truncated(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=False):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "y" * 5000})
        # Dropped ENTIRELY, never truncated -- generate_faust must see None, not
        # a shortened string.
        assert mock_gen.call_args.kwargs["prior_source"] is None
        assert result["prior_source_dropped"] is True

    def test_dropped_flag_absent_when_generation_fails(self):
        """Additive on the success path only, same treatment as `kind` (PF-019)."""
        with patch.object(generate, "generate_faust", return_value="bad"), \
             patch.object(generate, "validate_faust", return_value=(False, "bad symbol")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=False):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "y" * 5000})
        assert result["success"] is False
        assert "prior_source_dropped" not in result


# ---------------------------------------------------------------------------
# generate_json() — refine_mode preflight branching (surgical hard-fail vs
# context/legacy soft-drop)
# ---------------------------------------------------------------------------

class TestGenerateJsonRefineModePreflight:
    BASE_REQUEST = {"prompt": "make it louder", "provider": "anthropic",
                    "model": "claude-opus-4-6", "max_retries": 1}

    def test_surgical_fitting_is_sent_whole_with_refine_mode(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=True):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "process = _;",
                 "refine_mode": "surgical"})
        assert mock_gen.call_args.kwargs["prior_source"] == "process = _;"
        assert mock_gen.call_args.kwargs["refine_mode"] == "surgical"
        assert result["success"] is True
        assert "prior_source_refused" not in result
        assert "prior_source_dropped" not in result

    def test_surgical_oversized_is_refused_not_dropped(self):
        """The behavior this whole change exists for: surgical mode must HARD-FAIL
        rather than silently regenerate from scratch, which would violate the
        'minimal, surgical change' contract Add promises."""
        with patch.object(generate, "generate_faust") as mock_gen, \
             patch.object(generate.providers, "preflight_prior_source", return_value=False):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "y" * 5000,
                 "refine_mode": "surgical"})
        mock_gen.assert_not_called()          # short-circuit: no generation attempted
        assert result["success"] is False
        assert result["attempts"] == 0
        assert result["prior_source_refused"] is True
        assert "prior_source_dropped" not in result
        assert "Redo" in result["error"]      # tells the user the actual way out

    def test_context_oversized_is_dropped_not_refused(self):
        """Redo keeps today's soft-drop behavior -- only Add hard-fails."""
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=False):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "y" * 5000,
                 "refine_mode": "context"})
        assert mock_gen.call_args.kwargs["prior_source"] is None
        assert result["prior_source_dropped"] is True
        assert "prior_source_refused" not in result

    def test_context_fitting_uses_context_preamble_for_the_preflight_measurement(self):
        """The preflight must measure the SAME preamble generate_faust will
        actually send -- measuring the wrong (shorter/longer) one would admit a
        request generate_faust then can't fit, or reject one it could."""
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source",
                          return_value=True) as mock_preflight:
            generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "process = _;",
                 "refine_mode": "context"})
        candidate = mock_preflight.call_args[0][1]
        assert "free to REWRITE it from scratch" in candidate

    def test_refine_mode_absent_reproduces_legacy_drop_behavior(self):
        """No refine_mode field at all (an older host, or New/fresh mode which
        never sends prior_source anyway) must behave exactly as before this
        change: drop, never refuse."""
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")), \
             patch.object(generate.providers, "preflight_prior_source", return_value=False):
            result = generate.generate_json(
                {**self.BASE_REQUEST, "prior_source": "y" * 5000})
        assert mock_gen.call_args.kwargs["refine_mode"] is None
        assert result["prior_source_dropped"] is True
        assert "prior_source_refused" not in result


class TestGenerateJsonProviderAwarePreflight:
    """PF-060 end-to-end: generate_json() now passes `provider` into
    preflight_prior_source() instead of leaving it implicit-groq (it took no
    provider argument at all). These two tests deliberately do NOT mock
    preflight_prior_source — unlike every other test in this file, it is the
    function under test here — only generate_faust/validate_faust are mocked,
    same as the rest of this file's pattern. See providers.py's
    TestProviderAwarePreflight for the unit-level version and the live
    2026-08-13 reproduction this defect is based on.
    """
    BASE_REQUEST = {"prompt": "make it louder", "provider": "anthropic",
                    "model": "claude-opus-4-6", "max_retries": 1}

    # Same size, same reasoning as tests/test_providers_unit.py's
    # TestProviderAwarePreflight.TRIAL_PRIOR_SOURCE: the 2026-08-13 trial's
    # actual refused payload size (logs/prompts.jsonl ts:2026-08-13T23:06:23Z),
    # not an arbitrary large number.
    TRIAL_PRIOR_SOURCE = "y" * 2043

    def test_groq_refuses_the_trial_sized_payload_end_to_end(self):
        with patch.object(generate, "generate_faust") as mock_gen:
            result = generate.generate_json(
                {**self.BASE_REQUEST, "provider": "groq",
                 "prior_source": self.TRIAL_PRIOR_SOURCE,
                 "refine_mode": "surgical"})
        mock_gen.assert_not_called()          # short-circuit: no generation attempted
        assert result["prior_source_refused"] is True

    def test_large_context_provider_admits_the_same_payload_end_to_end(self):
        """The dossier's own §7 acceptance criterion, run through the real
        production entry point rather than mocked: the trial's actual refused
        payload, byte-for-byte unchanged, is admitted once the SELECTED
        provider (anthropic, 1M-token context — see its ProviderSpec) is the
        one the admission math actually checks, instead of an unconditional
        groq figure neither this request nor its provider ever asked for."""
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            result = generate.generate_json(
                {**self.BASE_REQUEST,
                 "prior_source": self.TRIAL_PRIOR_SOURCE,
                 "refine_mode": "surgical"})
        assert mock_gen.call_args.kwargs["prior_source"] == self.TRIAL_PRIOR_SOURCE
        assert mock_gen.call_args.kwargs["refine_mode"] == "surgical"
        assert result["success"] is True
        assert "prior_source_refused" not in result


# ---------------------------------------------------------------------------
# --request-file mode (A2)
# ---------------------------------------------------------------------------

class TestRequestFileMode:
    def test_read_request_file_parses_json(self, tmp_path):
        path = tmp_path / "request.json"
        path.write_text(json.dumps({"prompt": "a filter", "prior_source": "x"}),
                        encoding="utf-8")
        assert generate._read_request_file(str(path)) == {
            "prompt": "a filter", "prior_source": "x"}

    def test_missing_file_yields_adr011_failure_not_a_traceback(self, monkeypatch, capsys, tmp_path):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        missing = str(tmp_path / "does-not-exist.json")
        generate._run_subprocess_mode(lambda: generate._read_request_file(missing))
        captured = capsys.readouterr()
        payload = json.loads(captured.out.strip())
        assert payload["success"] is False
        assert "Traceback" not in captured.out
        assert "Traceback" in captured.err

    def test_request_file_content_reaches_generate_json(self, monkeypatch, capsys, tmp_path):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        path = tmp_path / "request.json"
        path.write_text(json.dumps({"prompt": "stereo reverb", "prior_source": "process = _;"}),
                        encoding="utf-8")
        captured_request = {}

        def fake_generate_json(request):
            captured_request.update(request)
            return {"success": True, "faust_code": "x", "attempts": 1, "error": None}

        with patch.object(generate, "generate_json", side_effect=fake_generate_json):
            generate._run_subprocess_mode(lambda: generate._read_request_file(str(path)))
        capsys.readouterr()
        assert captured_request == {"prompt": "stereo reverb", "prior_source": "process = _;"}

    def test_request_file_flag_counts_as_subprocess_mode(self, tmp_path):
        """The A2 trap, exercised for real: a bare `python generate.py
        --request-file <path>` under the paid-provider guard (no
        PLUGINFORGE_ALLOW_PAID) must print the ADR-011 JSON like --json/--prompt
        do. If --request-file were missing from `:469`'s _subprocess_mode check,
        this would instead print a bare `[!] ...` to stderr and exit 1."""
        req = tmp_path / "request.json"
        req.write_text(json.dumps({"prompt": "x"}), encoding="utf-8")
        env = os.environ.copy()
        env["PLUGINFORGE_PROVIDER"] = "anthropic"
        env.pop("PLUGINFORGE_ALLOW_PAID", None)
        llm_dir = Path(__file__).resolve().parent.parent / "llm"
        result = subprocess.run(
            [sys.executable, "generate.py", "--request-file", str(req)],
            cwd=str(llm_dir), env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0
        payload = json.loads(result.stdout.strip())
        assert payload["success"] is False


# ---------------------------------------------------------------------------
# Provider abstraction — generate_faust() passes provider/model through
# ---------------------------------------------------------------------------

class TestProviderAbstraction:
    def test_generate_faust_passes_provider_to_call_api(self):
        with patch.object(generate, "_call_api", return_value=VALID_FAUST) as mock_api:
            generate.generate_faust("a filter", provider="anthropic", model="claude-opus-4-6")
        _, kwargs = mock_api.call_args
        assert kwargs.get("provider", mock_api.call_args[0][1]) in ("anthropic",)

    def test_generate_with_retry_forwards_provider_args(self):
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST) as mock_gen, \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            generate.generate_with_retry("a filter", provider="anthropic", model="claude-opus-4-6")
        all_args = mock_gen.call_args.args + tuple(mock_gen.call_args.kwargs.values())
        assert "anthropic" in all_args
        assert "claude-opus-4-6" in all_args


# ---------------------------------------------------------------------------
# _run_subprocess_mode() — ADR-011 --prompt/--json hardening (point F)
# ---------------------------------------------------------------------------

class TestSubprocessModeMissingApiKey:
    def test_missing_key_prints_exact_failure_json(self, monkeypatch, capsys):
        monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
        generate._run_subprocess_mode(lambda: {"prompt": "x"})
        captured = capsys.readouterr()
        assert captured.err == ""
        payload = json.loads(captured.out.strip())
        assert payload == {
            "success": False,
            "faust_code": None,
            "attempts": 0,
            "error": "ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or "
                     "the plugin's environment.",
            # `reason` added with PF-019 (2026-07-25). This exact-equality
            # assertion is deliberate and is the ADR-011 wire contract test: it
            # fails whenever the response shape changes, which is the point. The
            # `error` string above is still asserted character-for-character.
            "reason": "no_credentials",
            "provider": "anthropic",
            "model": generate.DEFAULT_MODEL,
            "provider_attempts": [{
                "provider": "anthropic", "model": generate.DEFAULT_MODEL,
                "reason": "no_credentials",
                "error": "ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or "
                         "the plugin's environment.",
            }],
            "fallback_used": False,
        }

    def test_empty_key_treated_as_missing(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "")
        generate._run_subprocess_mode(lambda: {"prompt": "x"})
        payload = json.loads(capsys.readouterr().out.strip())
        assert payload["success"] is False
        assert "ANTHROPIC_API_KEY" in payload["error"]

    def test_missing_key_stdout_is_single_json_line(self, monkeypatch, capsys):
        monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
        generate._run_subprocess_mode(lambda: {"prompt": "x"})
        lines = capsys.readouterr().out.strip().splitlines()
        assert len(lines) == 1
        assert lines[0].startswith("{")

    def test_request_is_read_before_provider_aware_key_check(self, monkeypatch, capsys):
        monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
        calls = []
        generate._run_subprocess_mode(lambda: calls.append(1) or {"prompt": "x"})
        capsys.readouterr()
        assert calls == [1]


class TestProviderFallback:
    def test_fallback_is_off_by_default(self):
        failure = generate._failure(1, "rate_limited", "daily quota", retry_after=3000)
        with patch.object(generate, "_generate_json_one", return_value=failure) as call, \
             patch.object(generate.providers, "check_credentials", return_value=None):
            result = generate.generate_json({"prompt": "x", "provider": "groq"})
        assert call.call_count == 1
        assert result["reason"] == "rate_limited"
        assert "provider_attempts" not in result

    def test_approved_fallback_uses_next_provider_and_records_provenance(self):
        failure = generate._failure(1, "rate_limited", "daily quota", retry_after=3000)
        failure["quota_scope"] = "daily"
        success = {"success": True, "faust_code": VALID_FAUST, "attempts": 1,
                   "error": None, "reason": "ok", "kind": "effect",
                   "family": "effect", "family_source": "auto"}
        budget = generate.providers.Budget(total=140)
        with patch.object(generate, "_generate_json_one",
                          side_effect=[failure, success]) as call, \
             patch.object(generate.providers, "check_credentials", return_value=None), \
             patch.object(generate.providers, "assert_free"):
            result = generate.generate_json({
                "prompt": "x", "provider": "groq", "allow_fallback": True,
                "fallback_providers": ["ollama"], "budget": budget})
        assert call.call_count == 2
        assert call.call_args_list[0].args[0]["budget"] is budget
        assert call.call_args_list[1].args[0]["budget"] is budget
        assert result["success"] is True
        assert result["provider"] == "ollama"
        assert result["fallback_used"] is True
        assert [a["provider"] for a in result["provider_attempts"]] == ["groq", "ollama"]
        assert result["provider_attempts"][0]["quota_scope"] == "daily"

    def test_model_failure_does_not_switch_provider(self):
        failure = generate._failure(3, "invalid_faust", "bad symbol")
        with patch.object(generate, "_generate_json_one", return_value=failure) as call, \
             patch.object(generate.providers, "check_credentials", return_value=None):
            result = generate.generate_json({
                "prompt": "x", "provider": "groq", "allow_fallback": True,
                "fallback_providers": ["ollama"]})
        assert call.call_count == 1
        assert result["reason"] == "invalid_faust"
        assert result["fallback_used"] is False

    def test_fallback_provider_requires_its_own_credentials(self):
        failure = generate._failure(1, "rate_limited", "daily quota")
        with patch.object(generate, "_generate_json_one", return_value=failure) as call, \
             patch.object(generate.providers, "check_credentials",
                          side_effect=[None, "GOOGLE_API_KEY is not set"]):
            result = generate.generate_json({
                "prompt": "x", "provider": "groq", "allow_fallback": True,
                "fallback_providers": ["gemini"]})
        assert call.call_count == 1
        assert result["reason"] == "no_credentials"
        assert result["provider"] == "gemini"


class TestSubprocessModeUnexpectedException:
    def test_exception_converted_to_failure_json_not_traceback(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")

        def boom():
            raise RuntimeError("boom: something broke")

        generate._run_subprocess_mode(boom)
        captured = capsys.readouterr()

        payload = json.loads(captured.out.strip())
        assert payload == {
            "success": False,
            "faust_code": None,
            "attempts": 0,
            "error": "boom: something broke",
            # An unclassified exception is reason="error" — the generic bucket.
            # PF-019's typed reasons are for the two cases the user can act on.
            "reason": "error",
        }
        assert "Traceback" not in captured.out
        assert "Traceback" in captured.err

    def test_exception_from_generate_json_itself(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        with patch.object(generate, "generate_json", side_effect=ValueError("bad schema")):
            generate._run_subprocess_mode(lambda: {"prompt": "x"})
        captured = capsys.readouterr()

        out_lines = captured.out.strip().splitlines()
        assert len(out_lines) == 1
        payload = json.loads(out_lines[0])
        assert payload["success"] is False
        assert payload["error"] == "bad schema"
        assert "Traceback" in captured.err

    def test_malformed_json_stdin_does_not_crash(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")

        def bad_build_request():
            raise json.JSONDecodeError("Expecting value", "not json", 0)

        generate._run_subprocess_mode(bad_build_request)
        captured = capsys.readouterr()
        payload = json.loads(captured.out.strip())
        assert payload["success"] is False
        assert "Traceback" not in captured.out


class TestSubprocessModeNormalPath:
    def test_success_path_unaffected_by_key_precheck(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        with patch.object(generate, "generate_faust", return_value=VALID_FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            generate._run_subprocess_mode(lambda: {"prompt": "a gain plugin"})
        captured = capsys.readouterr()

        payload = json.loads(captured.out.strip())
        assert payload["success"] is True
        assert payload["faust_code"] == VALID_FAUST
        assert captured.err == ""

    def test_failure_path_unaffected_by_key_precheck(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        with patch.object(generate, "generate_faust", return_value="bad"), \
             patch.object(generate, "validate_faust", return_value=(False, "bad symbol")):
            generate._run_subprocess_mode(lambda: {"prompt": "x", "max_retries": 2})
        payload = json.loads(capsys.readouterr().out.strip())
        assert payload["success"] is False
        assert "bad symbol" in payload["error"]

    def test_build_request_result_passed_through_to_generate_json(self, monkeypatch, capsys):
        monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-test-key")
        captured_request = {}

        def fake_generate_json(request):
            captured_request.update(request)
            return {"success": True, "faust_code": "x", "attempts": 1, "error": None}

        with patch.object(generate, "generate_json", side_effect=fake_generate_json):
            generate._run_subprocess_mode(lambda: {"prompt": "stereo reverb"})
        capsys.readouterr()
        assert captured_request == {"prompt": "stereo reverb"}


# ---------------------------------------------------------------------------
# Integration marker — skipped unless faust + real API key present
# ---------------------------------------------------------------------------

@pytest.mark.integration
class TestGenerateIntegration:
    """Requires ANTHROPIC_API_KEY and faust compiler."""

    @pytest.mark.parametrize("prompt", [
        "a stereo low-pass filter with cutoff",
        "a simple gain control in decibels",
    ])
    def test_generates_valid_faust(self, prompt):
        result = generate.generate_with_retry(prompt, max_retries=3)
        assert "process" in result
        assert 'import("stdfaust.lib")' in result
