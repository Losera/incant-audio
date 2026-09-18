"""Unit tests for bench/capture.py — the interactive-capture harness (PF-072..075).

Split like tests/test_render_oracle.py: the pure-Python pieces (slug/id/enabled
gating) run everywhere; anything that shells out to `faust`/`faust2sndfile`
self-skips where those aren't installed (the Python-only CI job), same
skipif as test_render_oracle.py.
"""
import json
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import capture  # noqa: E402


LOWPASS = ('import("stdfaust.lib");\n'
          'freq = hslider("Cutoff [unit:Hz]", 1000, 20, 20000, 1);\n'
          'process = fi.resonlp(freq, 0.707, 1.0), fi.resonlp(freq, 0.707, 1.0);')
INSTRUMENT = 'import("stdfaust.lib");\nprocess = os.osc(440) * hslider("Gain", 0.5, 0, 1, 0.01);'


class TestEnabled:
    def test_off_by_default(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_CAPTURE", raising=False)
        assert capture.enabled() is False

    @pytest.mark.parametrize("val", ["1", "true", "TRUE", "on", "yes"])
    def test_on_values(self, monkeypatch, val):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", val)
        assert capture.enabled() is True

    @pytest.mark.parametrize("val", ["0", "false", "", "off", "no"])
    def test_off_values(self, monkeypatch, val):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", val)
        assert capture.enabled() is False


class TestSlug:
    def test_lowercases_and_dashes(self):
        assert capture._slug("A Warm Analog Reverb!") == "a-warm-analog-reverb"

    def test_truncates(self):
        assert len(capture._slug("x" * 100, maxlen=10)) <= 10

    def test_empty_falls_back(self):
        assert capture._slug("!!!") == "capture"


class TestNextId:
    def test_empty_dir_starts_at_one(self, tmp_path):
        assert capture._next_id(tmp_path) == "001"

    def test_increments_past_existing(self, tmp_path):
        (tmp_path / "001-foo.json").write_text("{}")
        (tmp_path / "004-bar.json").write_text("{}")
        assert capture._next_id(tmp_path) == "005"

    def test_ignores_non_numeric_prefix(self, tmp_path):
        (tmp_path / "xxx-junk.txt").write_text("")
        assert capture._next_id(tmp_path) == "001"


class TestCaptureGenerationGating:
    def test_returns_none_when_disabled(self, monkeypatch, tmp_path):
        monkeypatch.delenv("PLUGINFORGE_CAPTURE", raising=False)
        monkeypatch.setattr(capture, "CAPTURE_ROOT", tmp_path)
        result = capture.capture_generation(
            {"prompt": "x"}, {"success": True, "faust_code": LOWPASS})
        assert result is None
        assert list(tmp_path.rglob("*.json")) == []

    def test_never_raises_on_a_malformed_response(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", "1")
        monkeypatch.setattr(capture, "CAPTURE_ROOT", tmp_path)
        # response is not a dict -- capture_generation must fail open, not raise.
        result = capture.capture_generation({"prompt": "x"}, None)
        assert result is None


pytestmark_faust = pytest.mark.skipif(
    shutil.which("faust") is None,
    reason="faust not installed (expected in the Python-only CI job)",
)
pytestmark_faust2sndfile = pytest.mark.skipif(
    shutil.which("faust2sndfile") is None,
    reason="faust2sndfile not installed (expected in the Python-only CI job)",
)


@pytestmark_faust
class TestExtractParams:
    def test_reads_declared_sliders(self):
        params = capture.extract_params(LOWPASS)
        assert params is not None
        labels = {p["label"] for p in params}
        assert "Cutoff" in labels

    def test_none_on_bad_source(self):
        assert capture.extract_params("this is not faust") is None


@pytestmark_faust2sndfile
class TestCaptureGenerationEndToEnd:
    def test_writes_json_and_wav_for_an_effect(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", "1")
        monkeypatch.setattr(capture, "CAPTURE_ROOT", tmp_path)
        request = {"prompt": "a warm lowpass", "refine_mode": None,
                  "provider": "groq", "model": "gpt-oss-120b"}
        response = {"success": True, "faust_code": LOWPASS, "attempts": 1,
                   "reason": "ok", "kind": "effect"}
        cap_id = capture.capture_generation(request, response)
        assert cap_id == "001"

        day_dirs = list(tmp_path.iterdir())
        assert len(day_dirs) == 1
        files = sorted(p.name for p in day_dirs[0].iterdir())
        json_files = [f for f in files if f.endswith(".json")]
        assert len(json_files) == 1
        record = json.loads((day_dirs[0] / json_files[0]).read_text())
        assert record["prompt"] == "a warm lowpass"
        assert record["success"] is True
        assert record["param_count"] is not None and record["param_count"] >= 1
        assert record["audio_tail"]["skipped"] is False
        assert record["audio_tail"]["path"].endswith(".wav")
        assert (day_dirs[0] / record["audio_tail"]["path"]).exists()

    def test_skips_audio_for_zero_input_instrument(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", "1")
        monkeypatch.setattr(capture, "CAPTURE_ROOT", tmp_path)
        request = {"prompt": "a simple sine synth"}
        response = {"success": True, "faust_code": INSTRUMENT, "attempts": 1,
                   "reason": "ok", "kind": "instrument"}
        capture.capture_generation(request, response)

        day_dir = next(tmp_path.iterdir())
        json_file = next(p for p in day_dir.iterdir() if p.suffix == ".json")
        record = json.loads(json_file.read_text())
        assert record["audio_tail"]["skipped"] is True
        assert not list(day_dir.glob("*.wav"))

    def test_failed_generation_records_no_source_or_audio(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_CAPTURE", "1")
        monkeypatch.setattr(capture, "CAPTURE_ROOT", tmp_path)
        request = {"prompt": "an impossible patch"}
        response = {"success": False, "faust_code": None, "attempts": 3,
                   "reason": "invalid_faust", "error": "syntax error"}
        capture.capture_generation(request, response)

        day_dir = next(tmp_path.iterdir())
        json_file = next(p for p in day_dir.iterdir() if p.suffix == ".json")
        record = json.loads(json_file.read_text())
        assert record["success"] is False
        assert record["faust_source"] is None
        assert record["audio_tail"] is None
