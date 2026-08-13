"""Generation-family routing and prompt selection are one deterministic contract."""

from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import generate
import generation_profiles


@pytest.mark.parametrize(
    ("prompt", "kind", "family"),
    [
        ("a warm stereo delay", "effect", "effect"),
        ("a live granular grain cloud", "effect", "granular_effect"),
        ("a warm polyphonic pad", "instrument", "synth"),
        ("a punchy synthesized kick drum", "instrument", "drum_synth"),
        ("a self-playing ambient drone", "instrument", "generator"),
    ],
)
def test_auto_resolution(prompt, kind, family):
    profile, source = generation_profiles.resolve(prompt, kind)
    assert profile.id == family
    assert source == "auto"


def test_explicit_family_is_authoritative():
    profile, source = generation_profiles.resolve(
        "a prompt containing drum and drone", "instrument", "synth"
    )
    assert (profile.id, source) == ("synth", "user")


def test_cross_host_family_is_rejected():
    with pytest.raises(ValueError, match="belongs to instrument, not effect"):
        generation_profiles.resolve("anything", "effect", "drum_synth")


def test_generate_json_uses_selected_instrument_prompt_and_family_brief():
    captured = {}

    def fake_call(content, provider, model, budget, system_prompt):
        captured["system_prompt"] = system_prompt
        return 'import("stdfaust.lib"); process = 0;'

    with patch.object(generate, "_call_api", side_effect=fake_call), \
         patch.object(generate, "validate_faust", return_value=(True, "")):
        result = generate.generate_json(
            {"prompt": "a procedural kick", "kind": "instrument", "family": "drum_synth"}
        )

    assert result["success"] is True
    assert result["kind"] == "instrument"
    assert result["family"] == "drum_synth"
    assert result["family_source"] == "user"
    assert "An instrument MUST declare exactly these three controls" in captured["system_prompt"]
    assert "procedural one-shot percussion voice" in captured["system_prompt"]


def test_invalid_family_fails_before_provider_call():
    with patch.object(generate, "_call_api") as call:
        result = generate.generate_json(
            {"prompt": "a kick", "kind": "effect", "family": "drum_synth"}
        )
    assert result["success"] is False
    assert result["attempts"] == 0
    assert result["reason"] == "error"
    call.assert_not_called()


def _profile(family):
    return generation_profiles.profiles()[family]


def _metadata(inputs, labels, outputs=1):
    return {
        "inputs": inputs,
        "outputs": outputs,
        "ui": [{"type": "vgroup", "label": "patch", "items": [
            {"type": "hslider", "label": label} for label in labels
        ]}],
    }


def test_profile_validator_accepts_complete_synth_contract():
    assert generate._validate_profile_metadata(
        _metadata(0, ["gate", "freq", "gain", "cutoff"]), _profile("synth")
    ) == (True, "")


def test_profile_validator_rejects_partial_drum_contract():
    valid, error = generate._validate_profile_metadata(
        _metadata(0, ["gate", "freq"]), _profile("drum_synth")
    )
    assert valid is False
    assert "complete MIDI voice contract" in error


def test_profile_validator_distinguishes_generator_from_effect():
    assert generate._validate_profile_metadata(
        _metadata(0, ["tone"]), _profile("generator")
    ) == (True, "")
    valid, error = generate._validate_profile_metadata(
        _metadata(0, ["tone"]), _profile("effect")
    )
    assert valid is False
    assert "must process" in error


def test_granular_profile_requires_affordance_controls():
    valid, error = generate._validate_profile_metadata(
        _metadata(2, ["Grain Size", "Density", "Delay", "Pitch", "Feedback", "Mix"]),
        _profile("granular_effect"),
    )
    assert (valid, error) == (True, "")

    valid, error = generate._validate_profile_metadata(
        _metadata(2, ["Grain Size", "Density", "Delay", "Mix"]),
        _profile("granular_effect"),
    )
    assert valid is False
    assert "pitch/spray" in error and "feedback" in error
