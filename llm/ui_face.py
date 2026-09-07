"""Post-compile UI-face generation: a bounded, typed second LLM call.

ADR-035 Step 5 (docs/design/incant-ui/GENERATION_PLAN.md "Gap 5"). After a patch
compiles, this call receives the captured parameter table and emits a UiIr
schema-3 face (archetype + theme + grouped sections). It never touches DSP, so a
bad answer degrades to the host's deterministic `deriveLayoutFromGroups()` — the
floor, never bad audio.

Shape mirrors recommendation.py: one prompt file, a `parse_and_validate_*` that
raises a typed error, hard caps, and a `generate_*` that calls
`providers.make_generator(..., max_tokens=<small>)`.

The host re-validates everything host-side and is authoritative (contrast
validation in particular is C++ only — ThemeValidate.h — so a hand-written or
stale-cached IR is checked too). The checks here are a fast structural filter so
a malformed answer fails with a `reason` instead of reaching the renderer.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import providers


PROMPT_PATH = Path(__file__).with_name("prompts") / "ui_face_prompt.md"
SYSTEM_PROMPT = PROMPT_PATH.read_text(encoding="utf-8")

MIN_SECTIONS = 2
MAX_SECTIONS = 6
MAX_CONTROLS = 64          # the ParamPool slot budget; a face cannot exceed it
MAX_LG_CONTROLS = 2        # "lg is what tells the user which knob the plugin is about"

# A section with fewer than this many controls does not read as real grouping
# -- it is a heading over a lone knob, "its own tiny plugin". The live defect:
# a real "warm analog tape saturation effect with input drive, tone, output
# level, and a wet/dry mix" generation produced 4 sections, 1 knob each.
# Restructure, don't reject: every other degradation in this file keeps the
# face rather than discarding it for one bad judgment call, and the
# deterministic host fallback (ParamGridPanel::deriveLayoutFromGroups())
# folds the same shape the same way.
MIN_CONTROLS_PER_SECTION = 2

_LIMITS = {
    "tokens": 40,
    "id": 24,
    "title": 24,           # the prompt asks for <= 10; be lenient, the host truncates
}

ARCHETYPES = {
    "synth-panel", "channel-strip", "pedal", "tape-unit", "texture-field", "utility",
}
_THEME_ENUMS = {
    "display": {"condensed-sans", "geometric-sans", "grotesk", "slab", "engraved"},
    "readout": {"mono", "condensed-sans"},
    "knob": {"arc", "filled", "pointer", "chicken-head"},
    "density": {"roomy", "standard", "tight"},
}
_THEME_COLOURS = ("surface", "panel", "line", "text", "textDim", "accent", "accentAlt")

CONTROL_STYLES = {"arc-knob", "slider", "toggle", "inc-dec", ""}
CONTROL_SIZES = {"sm", "md", "lg", ""}
_CONTINUOUS_STYLES = {"arc-knob", "slider", "inc-dec"}
_TOGGLE_KINDS = {"button", "checkbutton"}
_NON_CONTROL_KINDS = {"meter"}


class InvalidFace(ValueError):
    """The producer returned a face that cannot be rendered safely."""


def _json_object(raw: str | dict[str, Any]) -> dict[str, Any]:
    if isinstance(raw, dict):
        return raw
    text = raw.strip()
    if "```" in text:
        after = text.split("```", 1)[1]
        text = after.split("\n", 1)[1] if "\n" in after else after
        text = text.split("```", 1)[0].strip()
    try:
        value = json.loads(text)
    except (TypeError, json.JSONDecodeError) as exc:
        raise InvalidFace(f"producer returned invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise InvalidFace("producer response must be a JSON object")
    return value


def _short_text(obj: dict[str, Any], key: str, *, fallback: str = "") -> str:
    value = obj.get(key, fallback)
    if not isinstance(value, str):
        raise InvalidFace(f"{key} must be a string")
    value = value.strip()
    if len(value) > _LIMITS[key]:
        raise InvalidFace(f"{key} exceeds {_LIMITS[key]} characters")
    return value


def _clean_theme(raw: Any) -> dict[str, str]:
    """Pass colours through as authored (the host runs WCAG contrast in C++);
    keep only recognised enum values so an unknown one falls to the host default
    rather than travelling as a live string."""
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise InvalidFace("theme must be an object")
    theme: dict[str, str] = {}
    for key in _THEME_COLOURS:
        val = raw.get(key)
        if isinstance(val, str) and val.strip():
            theme[key] = val.strip()
    for key, allowed in _THEME_ENUMS.items():
        val = raw.get(key)
        if isinstance(val, str) and val.strip() in allowed:
            theme[key] = val.strip()
    return theme


def parse_and_validate_face(raw: str | dict[str, Any],
                            params: list[dict[str, Any]],
                            is_instrument: bool) -> dict[str, Any]:
    """Return a normalised schema-3 face, or ``{"schema": 0}`` when the producer
    explicitly declined. Raises InvalidFace on anything unrenderable."""
    value = _json_object(raw)

    schema = value.get("schema")
    if schema == 0:
        return {"schema": 0}          # the prompt's "I am not sure" escape hatch
    if schema != 3:
        raise InvalidFace(f"schema must be 3 (or 0 to decline), got {schema!r}")

    archetype = value.get("archetype")
    if archetype not in ARCHETYPES:
        raise InvalidFace(f"archetype must be one of {sorted(ARCHETYPES)}, got {archetype!r}")

    kind_of = {p["label"]: str(p.get("kind", "")).lower()
               for p in params if isinstance(p, dict) and isinstance(p.get("label"), str)}
    writable = {label for label, kind in kind_of.items() if kind not in _NON_CONTROL_KINDS}

    sections_raw = value.get("sections")
    if not isinstance(sections_raw, list):
        raise InvalidFace("sections must be a list")

    sections: list[dict[str, Any]] = []
    seen_params: set[str] = set()
    lg_used = 0
    for item in sections_raw:
        if not isinstance(item, dict):
            raise InvalidFace("every section must be an object")
        title = _short_text(item, "title")
        if not title:
            raise InvalidFace("every section needs a title")
        section_id = _short_text(item, "id", fallback=title) or title
        span = item.get("span", 1)
        if not isinstance(span, int) or isinstance(span, bool):
            span = 1
        span = max(1, min(3, span))

        controls_raw = item.get("controls")
        if not isinstance(controls_raw, list):
            raise InvalidFace(f"section {section_id!r} has no controls list")

        controls: list[dict[str, str]] = []
        for ctrl in controls_raw:
            if not isinstance(ctrl, dict):
                raise InvalidFace("every control must be an object")
            label = ctrl.get("param")
            if not isinstance(label, str):
                raise InvalidFace("every control needs a string 'param'")
            if label in _NON_CONTROL_KINDS or kind_of.get(label) in _NON_CONTROL_KINDS:
                raise InvalidFace(f"{label!r} is a meter and cannot be a control")
            if label not in writable:
                continue                       # unknown label — dropped, not fatal
            if label in seen_params:
                continue                       # already placed — keep the first
            seen_params.add(label)

            style = ctrl.get("style", "")
            if style not in CONTROL_STYLES:
                style = ""
            if kind_of.get(label) in _TOGGLE_KINDS and style in _CONTINUOUS_STYLES:
                style = ""                     # PF-005 stays structural

            size = ctrl.get("size", "")
            if size not in CONTROL_SIZES:
                size = ""
            if size == "lg":
                if lg_used >= MAX_LG_CONTROLS:
                    size = "md"
                else:
                    lg_used += 1

            controls.append({"param": label, "style": style, "size": size})

        if controls:
            sections.append({"id": section_id, "title": title,
                             "span": span, "controls": controls})

    # Fold single-control sections rather than reject the face. A heading over
    # one knob reads as its own tiny plugin, not part of a grouped panel. Keyed
    # on whether ANY section is real (>= MIN_CONTROLS_PER_SECTION), not on the
    # average -- [1,1,4] and [3,1] both average >= 2 yet both leave a lone-knob
    # heading standing. Mirrors ParamGridPanel::deriveLayoutFromGroups() so the
    # host fallback and the LLM path degrade the same shape the same way:
    #   * no real section  -> one "Controls" section holding everything, order
    #     preserved (the "input drive / tone / output / wet-dry mix" case).
    #   * some real section -> fold each singleton forward into the preceding
    #     real section (a leading singleton goes into the first real one), so
    #     [2,2,1] stays two sections, not one flat grid.
    restructured = False
    if len(sections) > 1:
        real = [s for s in sections if len(s["controls"]) >= MIN_CONTROLS_PER_SECTION]
        if len(real) < len(sections):
            restructured = True
            if not real:
                merged = [c for s in sections for c in s["controls"]]
                sections = [{"id": "controls", "title": "Controls", "span": 1,
                             "controls": merged}]
            else:
                folded: list[dict[str, Any]] = []
                pending: list[dict[str, str]] = []
                for s in sections:
                    if len(s["controls"]) >= MIN_CONTROLS_PER_SECTION:
                        folded.append({**s, "controls": pending + list(s["controls"])})
                        pending = []
                    elif folded:
                        folded[-1]["controls"] = folded[-1]["controls"] + s["controls"]
                    else:
                        pending += s["controls"]
                sections = folded   # real >= 1, so pending is always drained

    min_sections = 1 if restructured else MIN_SECTIONS
    if not min_sections <= len(sections) <= MAX_SECTIONS:
        raise InvalidFace(
            f"a face needs {MIN_SECTIONS}-{MAX_SECTIONS} non-empty sections, got {len(sections)}")
    if len(seen_params) > MAX_CONTROLS:
        raise InvalidFace(f"a face cannot place more than {MAX_CONTROLS} controls")

    return {
        "schema": 3,
        "archetype": archetype,
        "tokens": _short_text(value, "tokens"),
        "theme": _clean_theme(value.get("theme")),
        "sections": sections,
    }


def generate_face(request: dict[str, Any], budget=None) -> dict[str, Any]:
    provider = providers.resolve_provider(request.get("provider"))
    model = providers.resolve_model(provider, request.get("model"))

    params = request.get("params")
    if not isinstance(params, list) or not params:
        raise ValueError("ui_face needs a non-empty 'params' list")
    is_instrument = bool(request.get("is_instrument"))
    prompt = request.get("prompt", "")

    user_message = json.dumps({
        "prompt": prompt,
        "is_instrument": is_instrument,
        "params": params,
    }, ensure_ascii=False)

    generate = providers.make_generator(
        provider,
        system_prompt=SYSTEM_PROMPT,
        model=model,
        max_tokens=1100,
        budget=budget,
    )
    raw = generate(user_message)
    face = parse_and_validate_face(raw, params, is_instrument)
    return {"success": True, "action": "ui_face", "reason": "ok",
            "attempts": 1, "error": None, "provider": provider, "model": model,
            "face": face}
