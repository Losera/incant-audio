#!/usr/bin/env python3
"""
bench/ab_stdlib_filter_instrument.py — queue item 6's A/B pilot.

Tests whether making llm/prompt_builder.py's stdlib trim UNCONDITIONAL (not
just a low-headroom rescue) holds generation quality, on the INSTRUMENT
prompt specifically -- the only prompt where this axis currently has any
effect (the effects prompt's own unfiltered headroom for a real prompt is
already below _MIN_UNFILTERED_HEADROOM, confirmed 2026-08-25, so "pressure-
only" and "always" are already identical there today).

Runs entirely against local ollama: $0, no daily quota, no network
dependency. Two arms per prompt:
  baseline      - PLUGINFORGE_STDLIB_FILTER_ALWAYS unset (today's behavior)
  always_filter - PLUGINFORGE_STDLIB_FILTER_ALWAYS=1 (the proposed default)

Corpus: bench/prompts/tiered_instrument_prompts_pilot.json (6 instrument
concepts x 2 tiers = 12 prompts) -- see that file's own description for why
it exists instead of reusing the effects-only 125-prompt tiered_prompts.json.

Usage:
    python bench/ab_stdlib_filter_instrument.py
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import providers  # noqa: E402
import prompt_builder  # noqa: E402
import voice_contract  # noqa: E402

CORPUS_PATH = ROOT / "bench" / "prompts" / "tiered_instrument_prompts_pilot.json"
INSTRUMENT_PROMPT_PATH = ROOT / "llm" / "prompts" / "instrument_prompt.txt"
RESULTS_DIR = ROOT / "bench" / "results"

PROVIDER = "ollama"


def validate_faust(code: str) -> tuple[bool, str]:
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                      delete=False) as f:
        f.write(code)
        tmp = f.name
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=30,
            encoding="utf-8", errors="replace",
        )
        return result.returncode == 0, result.stderr.strip()[:500]
    finally:
        os.unlink(tmp)


def matches_expected_primitives(code: str, expected_primitives: list) -> bool:
    if not expected_primitives:
        return True
    return any(p in (code or "") for p in expected_primitives)


_HSLIDER_LABEL = re.compile(r'\b(?:hslider|vslider|button|checkbox|nentry)\s*\(\s*"([^"]+)"')


def satisfies_voice_contract(code: str) -> bool:
    """Coarse heuristic, not FaustEngine's exact-case parser: does the code
    declare a UI control whose label is one of each zone's accepted labels,
    for all three zones (gate, freq-or-key, gain-or-vel)? Good enough for a
    quality signal in a 12-prompt pilot; not a substitute for the real
    voice-contract gate generate.py enforces."""
    labels = set(_HSLIDER_LABEL.findall(code or ""))
    zones = voice_contract.zone_labels()
    return all(bool(labels & accepted) for accepted in zones.values())


def strip_markdown_fence(text: str) -> str:
    text = text.strip()
    if text.startswith("```"):
        lines = text.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].strip() == "```":
            lines = lines[:-1]
        text = "\n".join(lines)
    return text.strip()


def run_one(gen, system_prompt: str, user_prompt: str) -> dict:
    t0 = time.time()
    try:
        raw = gen(system_prompt, user_prompt)
    except Exception as exc:  # noqa: BLE001 — recorded, not fatal to the run
        return {"elapsed_s": time.time() - t0, "error": repr(exc), "compiles": False,
                "matches_primitives": False, "voice_contract_ok": False, "code": ""}
    code = strip_markdown_fence(raw)
    compiles, stderr = validate_faust(code)
    return {
        "elapsed_s": round(time.time() - t0, 2),
        "error": None if compiles else stderr,
        "compiles": compiles,
        "code": code,
    }


def main() -> int:
    providers.assert_free(PROVIDER)
    corpus = json.loads(CORPUS_PATH.read_text(encoding="utf-8"))
    instrument_text = INSTRUMENT_PROMPT_PATH.read_text(encoding="utf-8")
    model = providers.PROVIDERS[PROVIDER].default_model

    records = []
    total = sum(len(i["tiers"]) for i in corpus["instruments"]) * 2
    done = 0

    for instrument in corpus["instruments"]:
        for tier, user_prompt in instrument["tiers"].items():
            for variant, env_value in (("baseline", "0"), ("always_filter", "1")):
                os.environ["PLUGINFORGE_STDLIB_FILTER_ALWAYS"] = env_value
                system_prompt = prompt_builder.build_dynamic_prompt(
                    user_prompt, base_system_prompt=instrument_text
                )
                gen = providers.make_generator(
                    PROVIDER, system_prompt=system_prompt, model=model,
                    temperature=0.0, max_tokens=providers.MAX_OUTPUT_TOKENS,
                )
                result = run_one(lambda sp, up: gen(up), system_prompt, user_prompt)
                result["matches_primitives"] = matches_expected_primitives(
                    result["code"], instrument.get("expected_primitives", [])
                )
                result["voice_contract_ok"] = satisfies_voice_contract(result["code"])
                result.update({
                    "instrument_id": instrument["instrument_id"],
                    "category": instrument["category"],
                    "tier": tier,
                    "variant": variant,
                    "prompt_chars": len(system_prompt),
                })
                records.append(result)
                done += 1
                status = "OK" if result["compiles"] else "FAIL"
                print(f"[{done}/{total}] {instrument['instrument_id']:12s} {tier:3s} "
                      f"{variant:14s} {status:4s} ({result['elapsed_s']}s, "
                      f"{result['prompt_chars']} chars)")

    os.environ.pop("PLUGINFORGE_STDLIB_FILTER_ALWAYS", None)

    # ── Aggregate, per variant ───────────────────────────────────────────────
    print("\n" + "=" * 70)
    for variant in ("baseline", "always_filter"):
        subset = [r for r in records if r["variant"] == variant]
        n = len(subset)
        compiles = sum(r["compiles"] for r in subset)
        primitives = sum(r["matches_primitives"] for r in subset)
        voice = sum(r["voice_contract_ok"] for r in subset)
        avg_chars = sum(r["prompt_chars"] for r in subset) / n if n else 0
        avg_time = sum(r["elapsed_s"] for r in subset) / n if n else 0
        print(f"{variant:14s}  compiles {compiles}/{n}  "
              f"primitives {primitives}/{n}  voice_contract {voice}/{n}  "
              f"avg_prompt_chars={avg_chars:.0f}  avg_time={avg_time:.1f}s")

    out_path = RESULTS_DIR / f"ab_stdlib_filter_instrument_pilot_{time.strftime('%Y%m%d_%H%M%S')}.json"
    out_path.write_text(json.dumps({
        "claim_under_test": "making prompt_builder.py's stdlib trim unconditional "
                             "(PLUGINFORGE_STDLIB_FILTER_ALWAYS=1) holds instrument "
                             "generation quality vs today's pressure-only default",
        "provider": PROVIDER,
        "model": model,
        "corpus": str(CORPUS_PATH.relative_to(ROOT)),
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "records": records,
    }, indent=2), encoding="utf-8")
    print(f"\nWrote {out_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
