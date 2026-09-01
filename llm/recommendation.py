"""Bounded, typed pre-generation plugin design recommendations."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import providers


PROMPT_PATH = Path(__file__).with_name("prompts") / "recommendation_prompt.md"
SYSTEM_PROMPT = PROMPT_PATH.read_text(encoding="utf-8")

MAX_MODULES = 5
MAX_CONTROLS = 12
_LIMITS = {
    "title": 80,
    "summary": 300,
    "name": 40,
    "module": 40,
    "purpose": 160,
    "range_hint": 60,
    "default_hint": 60,
    "unit": 24,
}


class InvalidRecommendation(ValueError):
    """The planner returned data that cannot be shown or generated safely."""


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
        raise InvalidRecommendation(f"planner returned invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise InvalidRecommendation("planner response must be a JSON object")
    return value


def _text(obj: dict[str, Any], key: str, *, allow_empty: bool = False) -> str:
    value = obj.get(key)
    if not isinstance(value, str):
        raise InvalidRecommendation(f"{key} must be a string")
    value = value.strip()
    if not value and not allow_empty:
        raise InvalidRecommendation(f"{key} must not be empty")
    if len(value) > _LIMITS[key]:
        raise InvalidRecommendation(f"{key} exceeds {_LIMITS[key]} characters")
    return value


def constraints_for(prompt: str, kind: str, family: str) -> list[dict[str, str]]:
    """Deterministic product limitations; never trust the planner to disclose these."""
    result: list[dict[str, str]] = []
    text = (prompt or "").lower()
    if kind == "instrument" and family in {"synth", "drum_synth"}:
        result.append({
            "code": "MONO_VOICE",
            "message": "Incant Audio Synth currently plays one synthesized voice; chords/polyphony are unavailable.",
        })
    if any(term in text for term in ("meter", "bargraph", "gain reduction")):
        result.append({
            "code": "CUSTOM_METERS_UNAVAILABLE",
            "message": "Use the built-in post-DSP output meter; generated custom meters are not rendered in this version.",
        })
    if family == "granular_effect":
        result.append({
            "code": "LIVE_INPUT_NOT_SAMPLE_PLAYER",
            "message": "This design grains the live input through bounded delay lines; it does not load or play sample files.",
        })
    return result


def parse_and_validate_recommendation(raw: str | dict[str, Any],
                                      expected_kind: str,
                                      expected_family: str,
                                      prompt: str = "") -> dict[str, Any]:
    value = _json_object(raw)
    modules_raw = value.get("modules")
    controls_raw = value.get("controls")
    if not isinstance(modules_raw, list) or not 1 <= len(modules_raw) <= MAX_MODULES:
        raise InvalidRecommendation(f"modules must contain 1-{MAX_MODULES} entries")
    if not isinstance(controls_raw, list) or not 1 <= len(controls_raw) <= MAX_CONTROLS:
        raise InvalidRecommendation(f"controls must contain 1-{MAX_CONTROLS} entries")

    modules: list[dict[str, str]] = []
    names: set[str] = set()
    for item in modules_raw:
        if not isinstance(item, dict):
            raise InvalidRecommendation("every module must be an object")
        module = {"name": _text(item, "name"), "purpose": _text(item, "purpose")}
        folded = module["name"].casefold()
        if folded in names:
            raise InvalidRecommendation(f"duplicate module name: {module['name']}")
        names.add(folded)
        modules.append(module)

    canonical_names = {m["name"].casefold(): m["name"] for m in modules}
    controls: list[dict[str, str]] = []
    control_names: set[str] = set()
    for item in controls_raw:
        if not isinstance(item, dict):
            raise InvalidRecommendation("every control must be an object")
        module_name = _text(item, "module")
        canonical = canonical_names.get(module_name.casefold())
        if canonical is None:
            raise InvalidRecommendation(f"control references unknown module: {module_name}")
        control_name = _text(item, "name")
        folded_control = control_name.casefold()
        if folded_control in control_names:
            raise InvalidRecommendation(f"duplicate control name: {control_name}")
        control_names.add(folded_control)
        controls.append({
            "name": control_name,
            "module": canonical,
            "purpose": _text(item, "purpose"),
            "range_hint": _text(item, "range_hint", allow_empty=True),
            "default_hint": _text(item, "default_hint", allow_empty=True),
            "unit": _text(item, "unit", allow_empty=True),
        })

    return {
        "schema": 1,
        "title": _text(value, "title"),
        "summary": _text(value, "summary"),
        "kind": expected_kind,
        "family": expected_family,
        "modules": modules,
        "controls": controls,
        "constraints": constraints_for(prompt, expected_kind, expected_family),
    }


def recommend_plugin(request: dict[str, Any], budget=None) -> dict[str, Any]:
    provider = providers.resolve_provider(request.get("provider"))
    model = providers.resolve_model(provider, request.get("model"))
    kind = request["kind"]
    family = request["family"]
    user_message = json.dumps({
        "prompt": request["prompt"],
        "target": kind,
        "family": family,
    }, ensure_ascii=False)
    generate = providers.make_generator(
        provider,
        system_prompt=SYSTEM_PROMPT,
        model=model,
        max_tokens=1200,
        budget=budget,
    )
    raw = generate(user_message)
    plan = parse_and_validate_recommendation(raw, kind, family, request["prompt"])
    return {"success": True, "action": "recommend", "reason": "ok",
            "attempts": 1, "error": None, "provider": provider, "model": model,
            "recommendation": plan}


def format_design_brief(plan: dict[str, Any]) -> str:
    """Canonical bounded prose folded into generation after a user accepts the card."""
    checked = parse_and_validate_recommendation(
        plan, plan.get("kind", ""), plan.get("family", ""))
    module_lines = [f"{i + 1}. {m['name']}: {m['purpose']}"
                    for i, m in enumerate(checked["modules"])]
    control_lines = []
    for control in checked["controls"]:
        hints = ", ".join(x for x in (
            control["range_hint"],
            f"default {control['default_hint']}" if control["default_hint"] else "",
            control["unit"],
        ) if x)
        suffix = f" ({hints})" if hints else ""
        control_lines.append(
            f"- {control['name']} [{control['module']}]: {control['purpose']}{suffix}")
    return (
        "The user reviewed and accepted this compact plugin design. Implement it while "
        "obeying every system-level Faust and safety rule. Numeric hints are suggestions, "
        "not permission to violate bounded or valid ranges.\n\n"
        f"Title: {checked['title']}\nSummary: {checked['summary']}\n"
        "Ordered modules:\n" + "\n".join(module_lines) +
        "\nControls:\n" + "\n".join(control_lines) + "\n\nOriginal request: "
    )
