"""Unit tests for bench/run_efficacy_study.py and bench/score_efficacy.py.

Mocked — NO network calls, NO faust compiler invocations. Mirrors the house
style of tests/test_generate_unit.py: mock subprocess.run for the compiler
and mock the anthropic client for generation. conftest.py sets
ANTHROPIC_API_KEY="" and puts llm/ on sys.path; this file additionally needs
bench/ on sys.path to import the two modules under test.
"""
import json
import os
import sys
from pathlib import Path
from unittest.mock import patch, MagicMock

import pytest

BENCH_DIR = Path(__file__).parent.parent / "bench"
sys.path.insert(0, str(BENCH_DIR))

import run_efficacy_study as efs  # noqa: E402
import score_efficacy as sco  # noqa: E402


VALID_FAUST = '''\
import("stdfaust.lib");
process = fi.resonlp(1000, 5, 0.5);\
'''


def _api_response(text: str) -> MagicMock:
    block = MagicMock()
    block.text = text
    resp = MagicMock()
    resp.content = [block]
    return resp


def _mock_run(returncode: int, stderr: str = "") -> MagicMock:
    m = MagicMock()
    m.returncode = returncode
    m.stderr = stderr
    return m


# ---------------------------------------------------------------------------
# Small 2-effect fixture dataset, matching the tiered_prompts.json schema.
# ---------------------------------------------------------------------------

FIXTURE_DATASET = {
    "version": 1,
    "tiers": ["L4", "L3", "L2", "L1", "L0"],
    "effects": [
        {
            "effect_id": "filters-01",
            "category": "filters",
            "target": "resonant low-pass filter",
            "expected_primitives": ["fi.resonlp", "fi.lowpass", "fi.moog_vcf", "fi.svf"],
            "tiers": {
                "L4": "a resonant low-pass filter using fi.resonlp with cutoff and Q controls",
                "L3": "a resonant low-pass filter with cutoff and resonance knobs",
                "L2": "a low-pass filter that can get squelchy",
                "L1": "a filter that cuts highs and can whistle a bit",
                "L0": "make it sound like Star Wars' AT-AT walker vocalizer, muffled",
            },
        },
        {
            "effect_id": "dynamics-01",
            "category": "dynamics",
            "target": "simple compressor",
            "expected_primitives": ["co.compressor_mono", "co.compressor_stereo", "an.amp_follower"],
            "tiers": {
                "L4": "a mono compressor using co.compressor_mono with threshold, ratio, attack, release",
                "L3": "a compressor with threshold and ratio controls",
                "L2": "something that evens out loud and quiet parts",
                "L1": "make quiet parts louder and loud parts quieter",
                "L0": "glue it together like a radio DJ voice",
            },
        },
    ],
}


@pytest.fixture()
def fixture_dataset():
    return json.loads(json.dumps(FIXTURE_DATASET))  # deep copy


# ---------------------------------------------------------------------------
# Dataset filtering — select_tasks()
# ---------------------------------------------------------------------------

class TestSelectTasks:
    def test_no_filters_returns_all_effect_tier_combinations(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset)
        assert len(tasks) == 2 * 5  # 2 effects x 5 tiers

    def test_tiers_filter(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset, tiers=["L4", "L0"])
        assert len(tasks) == 2 * 2
        assert all(tier in ("L4", "L0") for _, tier, _ in tasks)

    def test_categories_filter(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset, categories=["filters"])
        assert len(tasks) == 5
        assert all(effect["category"] == "filters" for effect, _, _ in tasks)

    def test_effects_filter(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset, effects=["dynamics-01"])
        assert len(tasks) == 5
        assert all(effect["effect_id"] == "dynamics-01" for effect, _, _ in tasks)

    def test_combined_filters(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset, tiers=["L4"], categories=["filters"])
        assert len(tasks) == 1
        effect, tier, prompt = tasks[0]
        assert effect["effect_id"] == "filters-01"
        assert tier == "L4"
        assert prompt == fixture_dataset["effects"][0]["tiers"]["L4"]

    def test_unknown_category_yields_empty(self, fixture_dataset):
        tasks = efs.select_tasks(fixture_dataset, categories=["nonexistent"])
        assert tasks == []


# ---------------------------------------------------------------------------
# Retry-loop behavior — run_effect_tier()
# ---------------------------------------------------------------------------

class TestRunEffectTier:
    EFFECT = FIXTURE_DATASET["effects"][0]
    TIER = "L4"
    PROMPT = FIXTURE_DATASET["effects"][0]["tiers"]["L4"]

    def test_first_try_success(self):
        generator = MagicMock(return_value=VALID_FAUST)
        with patch.object(efs, "validate_faust", return_value=(True, "")):
            record = efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=2)

        assert record["first_try_compiles"] is True
        assert record["retry_success"] is False
        assert record["attempts"] == 1
        assert record["errors"] == []
        assert record["code"] == VALID_FAUST
        generator.assert_called_once()

    def test_fail_then_error_context_appended_then_success(self):
        generator = MagicMock(return_value=VALID_FAUST)
        validate_calls = {"n": 0}

        def flaky_validate(_code):
            validate_calls["n"] += 1
            return (False, "ERROR : undefined symbol foo") if validate_calls["n"] == 1 else (True, "")

        with patch.object(efs, "validate_faust", side_effect=flaky_validate):
            record = efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=2)

        assert record["first_try_compiles"] is False
        assert record["retry_success"] is True
        assert record["attempts"] == 2
        assert record["errors"] == ["ERROR : undefined symbol foo"]

        # Second generator call's user message must contain the original
        # prompt AND the error-context suffix, exact wording from generate.py.
        second_call_msg = generator.call_args_list[1].args[0]
        assert self.PROMPT in second_call_msg
        assert "compiler error" in second_call_msg
        assert "ERROR : undefined symbol foo" in second_call_msg

    def test_exhausts_retries_and_records_all_errors(self):
        generator = MagicMock(return_value="bad code")
        with patch.object(efs, "validate_faust", return_value=(False, "always broken")):
            record = efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=2)

        assert record["first_try_compiles"] is False
        assert record["retry_success"] is False
        assert record["attempts"] == 3  # retries=2 -> up to 3 total attempts
        assert record["errors"] == ["always broken"] * 3
        assert generator.call_count == 3

    def test_zero_retries_means_single_attempt(self):
        generator = MagicMock(return_value="bad code")
        with patch.object(efs, "validate_faust", return_value=(False, "err")):
            record = efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=0)

        assert record["attempts"] == 1
        assert generator.call_count == 1

    def test_no_error_context_on_first_attempt(self):
        generator = MagicMock(return_value=VALID_FAUST)
        with patch.object(efs, "validate_faust", return_value=(True, "")):
            efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=2)

        first_call_msg = generator.call_args_list[0].args[0]
        assert first_call_msg == self.PROMPT
        assert "compiler error" not in first_call_msg

    def test_generator_exception_stops_cell_and_records_error(self):
        generator = MagicMock(side_effect=RuntimeError("API down"))
        record = efs.run_effect_tier(generator, self.EFFECT, self.TIER, self.PROMPT, retries=2)

        assert record["first_try_compiles"] is False
        assert record["retry_success"] is False
        assert any("API down" in e for e in record["errors"])
        generator.assert_called_once()


# ---------------------------------------------------------------------------
# Record schema completeness
# ---------------------------------------------------------------------------

class TestRecordSchema:
    REQUIRED_KEYS = {
        "provider", "effect_id", "category", "tier", "prompt", "dsl",
        "code", "first_try_compiles", "attempts", "retry_success",
        "errors", "timestamp",
    }

    def test_record_has_all_required_keys(self):
        effect = FIXTURE_DATASET["effects"][0]
        generator = MagicMock(return_value=VALID_FAUST)
        with patch.object(efs, "validate_faust", return_value=(True, "")):
            record = efs.run_effect_tier(generator, effect, "L4", effect["tiers"]["L4"], retries=2)
        assert self.REQUIRED_KEYS.issubset(record.keys())
        assert record["dsl"] == "faust"
        assert record["provider"] == "claude"
        assert record["effect_id"] == "filters-01"
        assert record["category"] == "filters"
        assert record["tier"] == "L4"


# ---------------------------------------------------------------------------
# Incremental write
# ---------------------------------------------------------------------------

class TestIncrementalWrite:
    def test_write_results_writes_after_each_record(self, tmp_path):
        out_file = tmp_path / "results.json"
        effect = FIXTURE_DATASET["effects"][0]
        tasks = [(effect, "L4", effect["tiers"]["L4"]),
                 (effect, "L3", effect["tiers"]["L3"])]

        generator = MagicMock(return_value=VALID_FAUST)
        written_snapshots = []

        original_write = efs.write_results

        def spy_write(path, records):
            original_write(path, records)
            written_snapshots.append(len(json.loads(path.read_text())))

        with patch.object(efs, "validate_faust", return_value=(True, "")), \
             patch.object(efs, "write_results", side_effect=spy_write):
            efs.run_study(tasks, retries=0, out_file=out_file, generator=generator)

        # After the 1st record, file has 1 entry; after the 2nd, 2 entries.
        assert written_snapshots == [1, 2]
        assert out_file.exists()
        final = json.loads(out_file.read_text())
        assert len(final) == 2

    def test_partial_results_survive_a_later_crash(self, tmp_path):
        out_file = tmp_path / "results.json"
        effect = FIXTURE_DATASET["effects"][0]
        tasks = [(effect, "L4", effect["tiers"]["L4"]),
                 (effect, "L3", effect["tiers"]["L3"])]

        call_count = {"n": 0}

        def flaky_generator(_msg):
            call_count["n"] += 1
            if call_count["n"] == 2:
                raise RuntimeError("simulated crash")
            return VALID_FAUST

        with patch.object(efs, "validate_faust", return_value=(True, "")):
            efs.run_study(tasks, retries=0, out_file=out_file, generator=flaky_generator)

        # run_effect_tier catches the exception internally and records it,
        # rather than propagating — so both cells complete and both are on disk.
        on_disk = json.loads(out_file.read_text())
        assert len(on_disk) == 2
        assert on_disk[1]["errors"]  # second cell recorded the simulated crash


# ---------------------------------------------------------------------------
# Error-class keyword tagging — classify_error()
# ---------------------------------------------------------------------------

class TestClassifyError:
    def test_syntax_class(self):
        assert sco.classify_error(VALID_FAUST, "ERROR : syntax error, unexpected token") == "SYNTAX"

    def test_hallucination_class_undefined_symbol(self):
        assert sco.classify_error(VALID_FAUST, "undefined symbol : flanger_mono") == "HALLUCINATION"

    def test_hallucination_class_unknown(self):
        assert sco.classify_error(VALID_FAUST, "unknown identifier 'foo'") == "HALLUCINATION"

    def test_semantic_class_evaluation_cycle(self):
        assert sco.classify_error(
            VALID_FAUST,
            "ERROR : after 400 evaluation steps, the compiler has detected an "
            "endless evaluation cycle of 12 steps",
        ) == "SEMANTIC"

    def test_semantic_class_type_mismatch(self):
        assert sco.classify_error(
            VALID_FAUST, "error: Cannot implicitly convert 'float64' to 'float32'"
        ) == "SEMANTIC"

    def test_incomplete_class_empty_code(self):
        assert sco.classify_error("", "some error") == "INCOMPLETE"

    def test_incomplete_class_truncated_code(self):
        assert sco.classify_error("proc", "some error") == "INCOMPLETE"

    def test_unclassified_class(self):
        assert sco.classify_error(VALID_FAUST, "something totally novel went wrong here") == "UNCLASSIFIED"


# ---------------------------------------------------------------------------
# expected_primitives matching
# ---------------------------------------------------------------------------

class TestExpectedPrimitivesMatching:
    def test_match_when_any_primitive_present(self):
        code = 'process = fi.resonlp(1000, 5, 0.5);'
        assert sco.matches_expected_primitives(code, ["fi.resonlp", "fi.lowpass"]) is True

    def test_no_match_when_none_present(self):
        code = 'process = _ * 0.5;'
        assert sco.matches_expected_primitives(code, ["fi.resonlp", "fi.lowpass"]) is False

    def test_no_match_with_empty_primitives_list(self):
        code = 'process = fi.resonlp(1000, 5, 0.5);'
        assert sco.matches_expected_primitives(code, []) is False


# ---------------------------------------------------------------------------
# compute_efficacy_scores() aggregation
# ---------------------------------------------------------------------------

class TestComputeEfficacyScores:
    def _record(self, effect_id, category, tier, first_try, retry_success, attempts,
                code=VALID_FAUST, errors=None):
        return {
            "provider": "claude",
            "effect_id": effect_id,
            "category": category,
            "tier": tier,
            "prompt": "irrelevant",
            "dsl": "faust",
            "code": code,
            "first_try_compiles": first_try,
            "attempts": attempts,
            "retry_success": retry_success,
            "errors": errors or [],
            "timestamp": "2026-07-19T00:00:00+00:00",
        }

    def test_first_try_aggregation(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L4", True, False, 1),
            self._record("filters-01", "filters", "L0", False, False, 3,
                         code="", errors=["undefined symbol : foo"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        assert scores["tier_agg"]["L4"]["first_try"] == [1, 1]
        assert scores["tier_agg"]["L0"]["first_try"] == [0, 1]
        assert scores["tier_agg"]["L0"]["retry"] == [0, 1]

    def test_retry_success_counts_toward_retry_not_first_try(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L2", False, True, 2,
                         errors=["ERROR : bad"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        assert scores["tier_agg"]["L2"]["first_try"] == [0, 1]
        assert scores["tier_agg"]["L2"]["retry"] == [1, 1]

    def test_mean_attempts(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L1", True, False, 1),
            self._record("dynamics-01", "dynamics", "L1", False, True, 3,
                         errors=["e1", "e2"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        agg = scores["tier_agg"]["L1"]
        assert agg["attempts_sum"] == 4
        assert agg["attempts_n"] == 2

    def test_error_class_tagging_only_considers_first_attempt_of_failures(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L0", False, False, 3,
                         code="", errors=["undefined symbol : flanger_mono", "later error"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        assert scores["error_classes"]["L0"]["HALLUCINATION"] == 1

    def test_semantic_pass_rate_only_over_compiled_successful(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L4", True, False, 1,
                         code='process = fi.resonlp(1000, 5, 0.5);'),
            self._record("filters-01", "filters", "L0", False, False, 3,
                         code="", errors=["bad"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        assert scores["semantic_pass"]["L4"] == [1, 1]
        # L0 failed to compile -> not counted in semantic_pass denominator at all
        assert scores["semantic_pass"]["L0"] == [0, 0]

    def test_unclassified_fragments_collected(self, fixture_dataset):
        records = [
            self._record("filters-01", "filters", "L0", False, False, 3,
                         code=VALID_FAUST, errors=["a totally novel failure mode"]),
        ]
        scores = sco.compute_efficacy_scores(records, fixture_dataset)
        assert scores["error_classes"]["L0"]["UNCLASSIFIED"] == 1
        assert len(scores["unclassified_fragments"]["L0"]) == 1
        assert "filters-01" in scores["unclassified_fragments"]["L0"][0]


# ---------------------------------------------------------------------------
# CLI-level dataset loading and preflight (mocked)
# ---------------------------------------------------------------------------

class TestPreflightCheck:
    def test_missing_api_key_and_missing_faust_both_reported(self, capsys):
        with patch.dict(os.environ, {"ANTHROPIC_API_KEY": ""}), \
             patch("subprocess.run", side_effect=FileNotFoundError):
            with pytest.raises(SystemExit):
                efs.preflight_check()
        captured = capsys.readouterr()
        assert "ANTHROPIC_API_KEY" in captured.err
        assert "faust" in captured.err

    def test_passes_when_key_set_and_faust_found(self, capsys):
        ok_run = MagicMock(returncode=0)
        with patch.dict(os.environ, {"ANTHROPIC_API_KEY": "sk-test"}), \
             patch("subprocess.run", return_value=ok_run):
            efs.preflight_check()  # should not raise
        captured = capsys.readouterr()
        assert "Preflight passed" in captured.err


class TestProviderProvenance:
    """A record must say which provider/model produced it — free-provider runs and
    the frozen claude baseline land in the same results directory."""

    EFFECT = FIXTURE_DATASET["effects"][0]

    def _record(self, **kwargs):
        generator = MagicMock(return_value=VALID_FAUST)
        with patch.object(efs, "validate_faust", return_value=(True, "")):
            return efs.run_effect_tier(generator, self.EFFECT, "L4",
                                       self.EFFECT["tiers"]["L4"], retries=2, **kwargs)

    def test_provider_defaults_to_claude_for_back_compat(self):
        assert self._record()["provider"] == "claude"

    def test_provider_is_recorded_when_given(self):
        assert self._record(provider="gemini")["provider"] == "gemini"

    def test_model_is_recorded(self):
        record = self._record(provider="gemini", model="gemini-3.6-flash")
        assert record["model"] == "gemini-3.6-flash"

    def test_model_falls_back_to_the_providers_pinned_default(self):
        record = self._record(provider="gemini")
        assert record["model"] == efs.run_benchmark.model_for("gemini")


class TestLoadDataset:
    def test_missing_required_key_raises(self, tmp_path):
        bad = tmp_path / "bad.json"
        bad.write_text(json.dumps({"version": 1}))
        with pytest.raises(ValueError):
            efs.load_dataset(bad)

    def test_valid_dataset_loads(self, tmp_path):
        good = tmp_path / "good.json"
        good.write_text(json.dumps(FIXTURE_DATASET))
        loaded = efs.load_dataset(good)
        assert loaded["tiers"] == FIXTURE_DATASET["tiers"]
        assert len(loaded["effects"]) == 2
