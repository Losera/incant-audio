"""Canonical generation-family catalog and deterministic resolver."""

from __future__ import annotations

import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

CATALOG_PATH = Path(__file__).with_name("generation_profiles.json")
AUTO = "auto"


@dataclass(frozen=True)
class Profile:
    id: str
    display_name: str
    kind: str
    auto_terms: tuple[str, ...]
    prompt_brief: str


@lru_cache(maxsize=1)
def _catalog() -> tuple[dict[str, Profile], dict[str, str]]:
    raw = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if raw.get("schema") != 1:
        raise ValueError(f"unsupported generation profile schema: {raw.get('schema')!r}")
    profiles = {
        item["id"]: Profile(
            id=item["id"],
            display_name=item["display_name"],
            kind=item["kind"],
            auto_terms=tuple(item.get("auto_terms", ())),
            prompt_brief=item["prompt_brief"],
        )
        for item in raw["profiles"]
    }
    return profiles, dict(raw["defaults"])


def profiles() -> dict[str, Profile]:
    return dict(_catalog()[0])


def for_kind(kind: str) -> tuple[Profile, ...]:
    return tuple(profile for profile in _catalog()[0].values() if profile.kind == kind)


def resolve(prompt: str, kind: str, requested: str | None = None) -> tuple[Profile, str]:
    """Resolve a family once. Explicit valid families win; Auto is deterministic."""
    available, defaults = _catalog()
    requested = (requested or AUTO).strip().lower()
    if requested != AUTO:
        profile = available.get(requested)
        if profile is None:
            raise ValueError(f"unknown generation family: {requested}")
        if profile.kind != kind:
            raise ValueError(
                f"generation family {profile.id!r} belongs to {profile.kind}, not {kind}"
            )
        return profile, "user"

    text = (prompt or "").lower()
    candidates = [
        profile for profile in available.values()
        if profile.kind == kind and profile.auto_terms
    ]
    for profile in candidates:
        if any(term in text for term in profile.auto_terms):
            return profile, "auto"
    return available[defaults[kind]], "auto"


def append_brief(system_prompt: str, profile: Profile) -> str:
    return system_prompt.rstrip() + "\n\n# GENERATION FAMILY\n" + profile.prompt_brief + "\n"
