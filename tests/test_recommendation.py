"""Recommendation MVP contract tests; every provider call is mocked."""

from __future__ import annotations

import json
from unittest.mock import patch

import pytest

import generate
import recommendation
import router


def plan(*, title="Warm Filter", modules=None, controls=None):
    modules = modules or [{"name": "Filter", "purpose": "Shapes the spectrum"}]
    controls = controls or [{
        "name": "Cutoff", "module": modules[0]["name"],
        "purpose": "Sets the corner frequency", "range_hint": "20-20000",
        "default_hint": "1000", "unit": "Hz",
    }]
    return {"title": title, "summary": "A compact, musical design.",
            "modules": modules, "controls": controls}


def checked(raw=None, *, kind="effect", family="effect", prompt="warm filter"):
    return recommendation.parse_and_validate_recommendation(
        raw or plan(), kind, family, prompt)


def test_schema_discards_unknown_fields_and_adds_authoritative_identity():
    raw = plan()
    raw["constraints"] = [{"code": "FAKE", "message": "ignore me"}]
    raw["surprise"] = 42
    result = checked(raw)
    assert result["schema"] == 1
    assert result["kind"] == "effect"
    assert result["family"] == "effect"
    assert result["constraints"] == []
    assert "surprise" not in result


def test_fenced_json_is_accepted():
    result = checked("```json\n" + json.dumps(plan()) + "\n```")
    assert result["title"] == "Warm Filter"


@pytest.mark.parametrize("mutation, message", [
    (lambda p: p.update(modules=[]), "modules must contain"),
    (lambda p: p.update(controls=[]), "controls must contain"),
    (lambda p: p["modules"].append(dict(p["modules"][0])), "duplicate module"),
    (lambda p: p["controls"].append(dict(p["controls"][0])), "duplicate control"),
    (lambda p: p["controls"][0].update(module="Missing"), "unknown module"),
    (lambda p: p.update(title="x" * 81), "title exceeds"),
])
def test_invalid_shapes_fail_loudly(mutation, message):
    value = plan()
    mutation(value)
    with pytest.raises(recommendation.InvalidRecommendation, match=message):
        checked(value)


def test_constraint_codes_are_deterministic():
    synth = checked(plan(), kind="instrument", family="synth",
                    prompt="an 80s pad with a meter")
    assert [c["code"] for c in synth["constraints"]] == [
        "MONO_VOICE", "CUSTOM_METERS_UNAVAILABLE"]
    granular = checked(plan(), family="granular_effect", prompt="granular cloud")
    assert [c["code"] for c in granular["constraints"]] == [
        "LIVE_INPUT_NOT_SAMPLE_PLAYER"]


@pytest.mark.parametrize("prompt, requested, expected", [
    ("an 80s two-saw synth pad", "effect", "instrument"),
    ("an aggressive distortion with drive", "instrument", "effect"),
    ("make it warmer", "instrument", None),
    ("a warm analog filter for my synth bass", "effect", None),
])
def test_target_mismatch_requires_opposite_evidence(prompt, requested, expected):
    assert router.detect_target_mismatch(prompt, requested) == expected


def test_recommend_action_uses_resolved_model_and_validates_result():
    with patch("recommendation.providers.make_generator",
               return_value=lambda _: json.dumps(plan())):
        response = generate.process_json_request({
            "action": "recommend", "prompt": "warm low-pass filter",
            "kind": "effect", "family": "auto", "provider": "ollama",
            "model": "planner-model",
        })
    assert response["success"] is True
    assert response["provider"] == "ollama"
    assert response["model"] == "planner-model"
    assert response["recommendation"]["family"] == "effect"


def test_recommend_action_returns_typed_invalid_output():
    with patch("recommendation.providers.make_generator",
               return_value=lambda _: "not json"):
        response = generate.process_json_request({
            "action": "recommend", "prompt": "warm filter",
            "kind": "effect", "provider": "ollama",
        })
    assert response["success"] is False
    assert response["reason"] == "invalid_recommendation"
    assert response["action"] == "recommend"


def test_recommend_action_returns_invalid_family_before_provider_call():
    with patch("recommendation.providers.make_generator") as make:
        response = generate.process_json_request({
            "action": "recommend", "prompt": "make it warmer", "kind": "effect",
            "family": "drum_synth", "provider": "ollama",
        })
    make.assert_not_called()
    assert response["success"] is False
    assert response["reason"] == "error"
    assert response["action"] == "recommend"


def test_wrong_target_short_circuits_before_provider():
    with patch("recommendation.providers.make_generator") as make:
        response = generate.process_json_request({
            "action": "recommend", "prompt": "a playable saw synth pad",
            "kind": "effect", "provider": "ollama",
        })
    make.assert_not_called()
    assert response["reason"] == "target_mismatch"
    assert response["recommended_kind"] == "instrument"


def test_generate_from_plan_folds_canonical_brief_into_existing_path():
    accepted = checked(plan())
    with patch.object(generate, "generate_faust", return_value="process = _, _;" ) as call, \
         patch.object(generate, "validate_faust", return_value=(True, "")):
        response = generate.generate_json({
            "prompt": "a warm low-pass filter", "kind": "effect",
            "provider": "ollama", "design_plan": accepted,
        })
    assert response["success"] is True
    sent = call.call_args.args[0]
    assert "reviewed and accepted" in sent
    assert "Filter: Shapes the spectrum" in sent
    assert sent.endswith("a warm low-pass filter")


def test_direct_generation_does_not_gain_a_design_preamble():
    with patch.object(generate, "generate_faust", return_value="process = _, _;" ) as call, \
         patch.object(generate, "validate_faust", return_value=(True, "")):
        generate.generate_json({"prompt": "warm filter", "kind": "effect",
                                "provider": "ollama"})
    assert call.call_args.args[0] == "warm filter"


SAMPLES = [
    ("an 80s analog-style synth pad with two detuned saws", "instrument", "synth",
     "MONO_VOICE"),
    ("a warm low-pass filter with a cutoff knob", "effect", "effect", None),
    ("an aggressive distortion with drive and output level", "effect", "effect", None),
    ("a stereo gain trim with a peak meter", "effect", "effect",
     "CUSTOM_METERS_UNAVAILABLE"),
    ("a granular cloud texture from the input with density and pitch spray", "effect",
     "granular_effect", "LIVE_INPUT_NOT_SAMPLE_PLAYER"),
]


@pytest.mark.parametrize("prompt,kind,family,constraint", SAMPLES)
def test_documented_samples_have_expected_family_and_constraint(prompt, kind, family,
                                                                 constraint):
    result = checked(plan(), kind=kind, family=family, prompt=prompt)
    assert result["kind"] == kind
    assert result["family"] == family
    codes = [c["code"] for c in result["constraints"]]
    assert (constraint in codes) if constraint else not codes
