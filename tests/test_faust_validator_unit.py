"""Unit tests for llm/faust_validator.py — no faust compiler required."""
import subprocess
from pathlib import Path
from unittest.mock import patch, MagicMock
import pytest

from faust_validator import validate_file

VALID_FAUST = '''\
import("stdfaust.lib");
gain = hslider("Gain [unit:dB]", 0, -60, 12, 0.1) : ba.db2linear;
process = _ * gain, _ * gain;
'''

INVALID_FAUST = '''\
import("stdfaust.lib");
process = undefined_function_xyz();
'''


def _mock_run(returncode: int, stderr: str = "") -> MagicMock:
    m = MagicMock()
    m.returncode = returncode
    m.stderr = stderr
    return m


class TestValidateFile:
    def test_returns_true_for_valid_code(self, tmp_path):
        dsp = tmp_path / "ok.dsp"
        dsp.write_text(VALID_FAUST)
        with patch("subprocess.run", return_value=_mock_run(0)):
            ok, msg = validate_file(str(dsp))
        assert ok is True
        assert msg == ""

    def test_returns_false_for_invalid_code(self, tmp_path):
        dsp = tmp_path / "bad.dsp"
        dsp.write_text(INVALID_FAUST)
        err = "ERROR : undefined symbol 'undefined_function_xyz'"
        with patch("subprocess.run", return_value=_mock_run(1, err)):
            ok, msg = validate_file(str(dsp))
        assert ok is False
        assert "undefined" in msg

    def test_calls_faust_with_cpp_lang_flag(self, tmp_path):
        dsp = tmp_path / "ok.dsp"
        dsp.write_text(VALID_FAUST)
        with patch("subprocess.run", return_value=_mock_run(0)) as mock_run:
            validate_file(str(dsp))
        cmd = mock_run.call_args[0][0]
        assert cmd[0] == "faust"
        assert "-lang" in cmd
        assert "cpp" in cmd

    def test_passes_file_path_to_compiler(self, tmp_path):
        dsp = tmp_path / "myfile.dsp"
        dsp.write_text(VALID_FAUST)
        with patch("subprocess.run", return_value=_mock_run(0)) as mock_run:
            validate_file(str(dsp))
        cmd = mock_run.call_args[0][0]
        assert str(dsp) in cmd

    def test_uses_devnull_output(self, tmp_path):
        dsp = tmp_path / "ok.dsp"
        dsp.write_text(VALID_FAUST)
        with patch("subprocess.run", return_value=_mock_run(0)) as mock_run:
            validate_file(str(dsp))
        cmd = mock_run.call_args[0][0]
        assert "/dev/null" in cmd

    def test_propagates_timeout_exception(self, tmp_path):
        dsp = tmp_path / "ok.dsp"
        dsp.write_text(VALID_FAUST)
        with patch("subprocess.run", side_effect=subprocess.TimeoutExpired("faust", 15)):
            with pytest.raises(subprocess.TimeoutExpired):
                validate_file(str(dsp))

    def test_stderr_stripped_from_error_message(self, tmp_path):
        dsp = tmp_path / "bad.dsp"
        dsp.write_text(INVALID_FAUST)
        raw_stderr = "  ERROR : something bad  \n"
        with patch("subprocess.run", return_value=_mock_run(1, raw_stderr)):
            _, msg = validate_file(str(dsp))
        assert not msg.startswith(" ")
        assert not msg.endswith(" ")

    def test_nonzero_returncode_treated_as_invalid(self, tmp_path):
        dsp = tmp_path / "bad.dsp"
        dsp.write_text(INVALID_FAUST)
        for code in [1, 2, 127]:
            with patch("subprocess.run", return_value=_mock_run(code, "error")):
                ok, _ = validate_file(str(dsp))
            assert ok is False, f"returncode {code} should be invalid"


@pytest.mark.integration
class TestValidateFileIntegration:
    """Requires faust compiler installed on the system PATH."""

    def test_valid_example_compiles(self):
        examples = list((Path(__file__).parent.parent / "examples").glob("*.dsp"))
        assert examples, "No .dsp files found in examples/"
        ok, msg = validate_file(str(examples[0]))
        assert ok, f"Expected {examples[0].name} to compile:\n{msg}"


class TestNonUtf8CompilerOutput:
    """Regression: faust emits invalid UTF-8 on stderr when the source contains
    non-ASCII, because it echoes the offending line and cuts it mid-character.

    Found 2026-07-21 when a groq refusal reply ("I'm sorry, but I can't help with
    that.", with curly apostrophes) was passed to the validator. With the default
    errors="strict", subprocess.run(text=True) raises UnicodeDecodeError, so the
    validator never returns a compile failure and the ADR-005 retry loop never
    engages -- a recoverable attempt kills the whole generation instead.

    All three call sites must decode leniently: llm/generate.py::validate_faust,
    llm/faust_validator.py::validate_file, bench/run_benchmark.py::validate_faust.
    """

    NON_ASCII_SOURCE = "I’m sorry, but I can’t help with that."

    def test_validators_pass_lenient_decoding(self):
        """subprocess.run must be called with errors that tolerate bad bytes."""
        import inspect

        for func in (validate_file,):
            src = inspect.getsource(func)
            assert 'errors="replace"' in src or "errors='replace'" in src, (
                f"{func.__name__} decodes subprocess output strictly; a faust "
                "stderr containing invalid UTF-8 will raise instead of returning "
                "a compile error"
            )

    @pytest.mark.integration
    def test_non_utf8_compiler_output_does_not_raise(self, tmp_path):
        """End-to-end against the real compiler: must return, never raise."""
        dsp = tmp_path / "refusal.dsp"
        dsp.write_text(self.NON_ASCII_SOURCE, encoding="utf-8")
        ok, msg = validate_file(str(dsp))   # must not raise UnicodeDecodeError
        assert ok is False
        assert isinstance(msg, str)
