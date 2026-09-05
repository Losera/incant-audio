"""UI-face producer contract tests (ADR-035 Step 5); every provider call mocked."""

from __future__ import annotations

import json
from unittest.mock import patch

import pytest

import generate
import ui_face


PARAMS = [
    {"label": "detune", "kind": "hslider", "group": "OSC",
     "min": 0.0, "max": 1.0, "default": 0.2, "unit": ""},
    {"label": "blend", "kind": "hslider", "group": "OSC",
     "min": 0.0, "max": 1.0, "default": 0.5, "unit": ""},
    {"label": "cutoff", "kind": "hslider", "group": "FILTER",
     "min": 20.0, "max": 20000.0, "default": 1200.0, "unit": "Hz"},
    {"label": "bypass", "kind": "checkbutton", "group": "FILTER",
     "min": 0.0, "max": 1.0, "default": 0.0, "unit": ""},
    {"label": "level", "kind": "meter", "group": "OUT",
     "min": 0.0, "max": 1.0, "default": 0.0, "unit": ""},
]


def face(*, archetype="synth-panel", tokens="velvet-drift", theme=None, sections=None):
    theme = theme if theme is not None else {
        "surface": "#0e0f13", "text": "#eef2ee",
        "accent": "#8fe3c1", "knob": "arc", "density": "roomy",
    }
    sections = sections if sections is not None else [
        {"id": "osc", "title": "OSC", "span": 1, "controls": [
            {"param": "detune", "style": "arc-knob", "size": "md"},
            {"param": "blend", "style": "arc-knob", "size": "md"},
        ]},
        {"id": "filter", "title": "FILTER", "span": 2, "controls": [
            {"param": "cutoff", "style": "arc-knob", "size": "lg"},
            {"param": "bypass", "style": "toggle", "size": ""},
        ]},
    ]
    return {"schema": 3, "archetype": archetype, "tokens": tokens,
            "theme": theme, "sections": sections}


def checked(raw=None, *, params=None, is_instrument=True):
    return ui_face.parse_and_validate_face(
        raw if raw is not None else face(),
        params if params is not None else PARAMS,
        is_instrument)


# ── parse_and_validate_face ─────────────────────────────────────────────────

def test_valid_face_normalises_and_keeps_structure():
    result = checked()
    assert result["schema"] == 3
    assert result["archetype"] == "synth-panel"
    assert result["tokens"] == "velvet-drift"
    assert [s["id"] for s in result["sections"]] == ["osc", "filter"]
    assert result["sections"][1]["span"] == 2
    placed = [c["param"] for s in result["sections"] for c in s["controls"]]
    assert placed == ["detune", "blend", "cutoff", "bypass"]


def test_fenced_json_is_accepted():
    result = checked("```json\n" + json.dumps(face()) + "\n```")
    assert result["archetype"] == "synth-panel"


def test_schema_zero_is_the_decline_hatch():
    assert checked({"schema": 0}) == {"schema": 0}
    assert checked({"schema": 0, "archetype": "nonsense"}) == {"schema": 0}


def test_unknown_param_label_is_dropped_not_fatal():
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [
            {"param": "detune"}, {"param": "ghost-knob"},
        ]},
        {"id": "filter", "title": "FILTER", "controls": [{"param": "cutoff"}]},
    ])
    result = checked(f)
    placed = [c["param"] for s in result["sections"] for c in s["controls"]]
    assert placed == ["detune", "cutoff"]


def test_a_meter_listed_as_a_control_is_fatal():
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [{"param": "detune"}]},
        {"id": "out", "title": "OUT", "controls": [{"param": "level"}]},
    ])
    with pytest.raises(ui_face.InvalidFace, match="meter"):
        checked(f)


def test_toggle_kind_never_keeps_a_continuous_style():
    # Two controls per section (not one) so this stays isolated from the
    # controls-per-section merge below -- that behaviour has its own tests.
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [{"param": "detune"}, {"param": "blend"}]},
        {"id": "filter", "title": "FILTER", "controls": [
            {"param": "cutoff"},
            {"param": "bypass", "style": "arc-knob", "size": "lg"},
        ]},
    ])
    result = checked(f)
    filter_controls = result["sections"][1]["controls"]
    bypass = next(c for c in filter_controls if c["param"] == "bypass")
    assert bypass["style"] == ""          # PF-005 stays structural


def test_duplicate_param_reference_is_deduped_to_first_placement():
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [
            {"param": "detune"}, {"param": "cutoff"},
        ]},
        {"id": "filter", "title": "FILTER", "controls": [
            {"param": "cutoff"}, {"param": "blend"},
        ]},
    ])
    result = checked(f)
    placed = [c["param"] for s in result["sections"] for c in s["controls"]]
    assert placed == ["detune", "cutoff", "blend"]


def test_empty_sections_are_pruned():
    # Two controls per surviving section so this stays isolated from the
    # controls-per-section merge below -- that behaviour has its own tests.
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [{"param": "detune"}, {"param": "blend"}]},
        {"id": "dead", "title": "DEAD", "controls": [{"param": "ghost"}]},
        {"id": "filter", "title": "FILTER", "controls": [{"param": "cutoff"}, {"param": "bypass"}]},
    ])
    result = checked(f)
    assert [s["id"] for s in result["sections"]] == ["osc", "filter"]


@pytest.mark.parametrize("sections, message", [
    ([{"id": "a", "title": "A", "controls": [{"param": "detune"}]}],
     "2-6 non-empty sections"),
    ([{"id": f"s{i}", "title": f"S{i}", "controls": [{"param": "detune"}]}
      for i in range(7)],
     "2-6 non-empty sections"),
    ("not-a-list", "sections must be a list"),
])
def test_section_count_bounds(sections, message):
    with pytest.raises(ui_face.InvalidFace, match=message):
        checked(face(sections=sections))


def test_many_thin_sections_merge_into_one():
    """The live defect this covers: a real 4-param "warm analog tape
    saturation effect with input drive, tone, output level, and a wet/dry
    mix" generation produced 4 sections, 1 knob each -- a heading per
    parameter, not one grouped panel. Below MIN_CONTROLS_PER_SECTION on
    average, merge rather than reject (every other degradation in this file
    keeps the face); order is preserved, nothing is dropped."""
    sections = [
        {"id": "a", "title": "A", "controls": [{"param": "detune"}]},
        {"id": "b", "title": "B", "controls": [{"param": "blend"}]},
        {"id": "c", "title": "C", "controls": [{"param": "cutoff"}]},
        {"id": "d", "title": "D", "controls": [{"param": "bypass"}]},
    ]
    result = checked(face(sections=sections))
    assert len(result["sections"]) == 1
    merged = result["sections"][0]
    assert merged["id"] == "controls"
    assert merged["title"] == "Controls"
    assert [c["param"] for c in merged["controls"]] == ["detune", "blend", "cutoff", "bypass"]


def test_well_grouped_sections_are_not_merged():
    """The merge threshold has a floor: two sections averaging exactly
    MIN_CONTROLS_PER_SECTION each is real grouping, not one param wrapped
    alone, and must stay separate -- otherwise the merge would swallow every
    grouped face, not just thin ones."""
    sections = [
        {"id": "osc", "title": "OSC", "controls": [{"param": "detune"}, {"param": "blend"}]},
        {"id": "filter", "title": "FILTER", "controls": [{"param": "cutoff"}, {"param": "bypass"}]},
    ]
    result = checked(face(sections=sections))
    assert len(result["sections"]) == 2
    assert [s["id"] for s in result["sections"]] == ["osc", "filter"]


@pytest.mark.parametrize("archetype", ["", "cool-panel", None, "SYNTH-PANEL"])
def test_unknown_archetype_is_fatal(archetype):
    with pytest.raises(ui_face.InvalidFace, match="archetype"):
        checked(face(archetype=archetype))


def test_schema_other_than_three_or_zero_is_fatal():
    with pytest.raises(ui_face.InvalidFace, match="schema must be 3"):
        checked(face() | {"schema": 2})


def test_theme_colours_pass_through_unknown_enums_are_dropped():
    result = checked(face(theme={
        "surface": "#0e0f13", "text": "rgba(238,242,238,0.9)",
        "accent": "#8fe3c1", "knob": "wobble", "density": "roomy",
        "bogusKey": "#fff",
    }))
    assert result["theme"]["surface"] == "#0e0f13"
    assert result["theme"]["text"] == "rgba(238,242,238,0.9)"
    assert "knob" not in result["theme"]          # unknown enum -> host defaults it
    assert result["theme"]["density"] == "roomy"
    assert "bogusKey" not in result["theme"]


def test_a_non_object_theme_is_fatal():
    with pytest.raises(ui_face.InvalidFace, match="theme must be an object"):
        checked(face(theme=["#fff"]))


def test_at_most_two_large_controls_the_rest_shrink():
    f = face(sections=[
        {"id": "osc", "title": "OSC", "controls": [
            {"param": "detune", "size": "lg"}, {"param": "blend", "size": "lg"},
        ]},
        {"id": "filter", "title": "FILTER", "controls": [
            {"param": "cutoff", "size": "lg"}, {"param": "bypass", "size": "sm"},
        ]},
    ])
    result = checked(f)
    sizes = {c["param"]: c["size"] for s in result["sections"] for c in s["controls"]}
    assert sizes == {"detune": "lg", "blend": "lg", "cutoff": "md", "bypass": "sm"}


def test_span_is_clamped():
    # Two controls per section (not one) so this stays isolated from the
    # controls-per-section merge below -- that behaviour has its own tests.
    f = face(sections=[
        {"id": "osc", "title": "OSC", "span": 9, "controls": [{"param": "detune"}, {"param": "blend"}]},
        {"id": "filter", "title": "FILTER", "span": 0, "controls": [{"param": "cutoff"}, {"param": "bypass"}]},
    ])
    result = checked(f)
    assert [s["span"] for s in result["sections"]] == [3, 1]


def test_invalid_json_is_fatal():
    with pytest.raises(ui_face.InvalidFace, match="invalid JSON"):
        checked("this is not json")


# ── the ui_face action through process_json_request ────────────────────────

def test_ui_face_action_returns_the_validated_face():
    with patch("ui_face.providers.make_generator",
               return_value=lambda _: json.dumps(face())):
        response = generate.process_json_request({
            "action": "ui_face", "prompt": "detuned saw pad",
            "is_instrument": True, "params": PARAMS,
            "provider": "ollama", "model": "face-model",
        })
    assert response["success"] is True
    assert response["action"] == "ui_face"
    assert response["provider"] == "ollama"
    assert response["model"] == "face-model"
    assert response["face"]["archetype"] == "synth-panel"


def test_ui_face_action_reports_a_typed_invalid_face():
    with patch("ui_face.providers.make_generator",
               return_value=lambda _: json.dumps(face(archetype="bogus"))):
        response = generate.process_json_request({
            "action": "ui_face", "prompt": "x", "params": PARAMS, "provider": "ollama",
        })
    assert response["success"] is False
    assert response["reason"] == "invalid_face"
    assert response["action"] == "ui_face"


def test_ui_face_action_rejects_an_empty_param_table_before_the_provider():
    with patch("ui_face.providers.make_generator") as make:
        response = generate.process_json_request({
            "action": "ui_face", "prompt": "x", "params": [], "provider": "ollama",
        })
    make.assert_not_called()
    assert response["success"] is False
    assert response["reason"] == "error"
    assert response["action"] == "ui_face"


def test_unknown_action_is_still_rejected():
    response = generate.process_json_request({"action": "sculpt", "prompt": "x"})
    assert response["success"] is False
    assert response["reason"] == "error"
    assert "unknown action" in response["error"]
