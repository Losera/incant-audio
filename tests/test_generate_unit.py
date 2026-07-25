"""Unit tests for llm/generate.py — no Anthropic API key or faust compiler required."""
import json
import os
import subprocess
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

    def test_missing_key_never_calls_build_request(self, monkeypatch, capsys):
        monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
        calls = []
        generate._run_subprocess_mode(lambda: calls.append(1))
        capsys.readouterr()
        assert calls == []


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
