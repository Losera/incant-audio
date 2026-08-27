#!/usr/bin/env python3
"""
llm/prompt_builder.py — Dynamic stdlib reference prompt builder.

Optimizes LLM context headroom by dynamically tailoring stdlib references based on
keywords in the user prompt (e.g. filter, delay, reverb, oscillator, dynamics),
preventing Groq HTTP 413 "Request too large" token overflows while preserving prompt invariants.
"""

import os
import re
from pathlib import Path
from typing import Dict, List, Set, Optional

import providers

PROMPT_DIR = Path(__file__).resolve().parent / "prompts"
SYSTEM_PROMPT_PATH = PROMPT_DIR / "system_prompt.txt"

# Prefixes retained regardless of matched domain: the primitives a correct
# patch of almost any kind needs (oscillators, filters, envelopes, noise,
# basic signal routing, math, and the voice/filter-model namespace).
#
# FIXED 2026-08-13. Domain filtering used to gate every one of these too,
# which silently violated the prompt's own "Use ONLY functions from the
# Faust standard library reference below" claim: a "subtractive synth"
# prompt matched only the `oscillator` domain (via router.py's "synth"/
# "subtractive" keywords) and lost `fi.*` entirely -- even though subtractive
# synthesis IS filtering an oscillator. `ve.` and `no.` were not in ns_map
# under ANY domain, so they were stripped from every filtered prompt,
# unconditionally, including instrument_prompt.txt's own prose teaching
# `ve.moog_vcf`/`ve.moogLadder` (instrument_prompt.txt:71-73). Confirmed live:
# a groq 429 for "an additive synth with reverb delay and chorus" showed
# `Requested` token counts varying 5177-7258 across retries of the SAME
# prompt -- that spread is this filtering, so it is a throughput lever, not
# just headroom hygiene, and has to stay correct under pressure, not merely
# smaller. Only the long tail below (delay/reverb/dynamics-specific
# namespaces) stays domain-gated.
CORE_PREFIXES = {"os.", "fi.", "en.", "no.", "si.", "ba.", "ma.", "ve."}

# Minimum unfiltered headroom (providers.headroom_tokens units) required to
# skip filtering entirely. NOT zero: measured against system_prompt.txt,
# several real prompts sit at 191-207 tokens of unfiltered headroom --
# technically positive (the request would be admitted), but filtering the
# SAME prompts brings that up to ~524, and 191-207 is not a safe margin to
# skip a free improvement for (one longer user prompt, or a later addition
# this function can't see -- generation_profiles.append_brief, a refine
# preamble -- erases it). Chosen to match the margin the pre-existing
# headroom test in tests/test_prompt_builder.py already asserted before this
# fix (it used a bare ">300" threshold on the FILTERED result).
_MIN_UNFILTERED_HEADROOM = 300

# Keywords mapped to Faust library domains
DOMAIN_KEYWORDS: Dict[str, Set[str]] = {
    "filter": {
        "filter", "lowpass", "highpass", "bandpass", "notch", "eq", "equalizer",
        "bq", "shelving", "cutoff", "resonance", "fi.", "pf."
    },
    "oscillator": {
        "oscillator", "synth", "wave", "sawtooth", "sine", "square", "triangle",
        "pulse", "fm", "lfo", "os.", "vco", "organ", "lead", "brass", "pluck"
    },
    "time": {
        "delay", "reverb", "echo", "chorus", "flanger", "phaser", "pitch",
        "comb", "allpass", "de.", "re.", "ef."
    },
    "dynamics": {
        "compressor", "limiter", "gate", "expander", "saturator", "fuzz",
        "distortion", "overdrive", "co.", "ma.", "sidechain", "threshold"
    },
    "envelope": {
        "envelope", "attack", "decay", "sustain", "release", "adsr", "en."
    }
}

def extract_relevant_domains(prompt_text: str) -> Set[str]:
    """Analyze prompt text and return matched domain keys."""
    text = prompt_text.lower()
    matched: Set[str] = set()
    
    for domain, keywords in DOMAIN_KEYWORDS.items():
        if any(kw in text for kw in keywords):
            matched.add(domain)
            
    # Default fallback: if no specific domain matched, include core synth/fx domains
    if not matched:
        matched = {"filter", "oscillator", "time", "dynamics", "envelope"}
        
    return matched


def filter_stdlib_block(stdlib_block: str, domains: Set[str]) -> str:
    """Filter lines of the stdlib reference block to retain relevant functions.

    Only the LONG TAIL below is domain-gated -- CORE_PREFIXES (module level)
    is always retained regardless of `domains`, so this can never strip a
    namespace the prompt's own prose depends on. Namespaces already in
    CORE_PREFIXES (fi., os., en., ba.) are intentionally absent from this map:
    matching their domain adds nothing new, since the floor already covers
    them.
    """
    lines = stdlib_block.splitlines()
    filtered_lines: List[str] = []

    # Prefix mapping for namespaces NOT in the always-included core floor.
    ns_map = {
        "filter": ["pf."],
        "time": ["de.", "re.", "ef."],
        "dynamics": ["co.", "ef."],
    }

    allowed_prefixes = set(CORE_PREFIXES)
    for d in domains:
        for p in ns_map.get(d, []):
            allowed_prefixes.add(p)

    for line in lines:
        # Keep headers, section dividers, and process notes
        if line.startswith("#") or line.startswith("//") or not line.strip() or line.startswith("STDLIB") or line.startswith("Namespace"):
            filtered_lines.append(line)
            continue
        
        # Check if line contains a function entry matching allowed namespace prefixes
        if any(p in line for p in allowed_prefixes):
            filtered_lines.append(line)
            
    return "\n".join(filtered_lines)


def build_dynamic_prompt(user_prompt: str, base_system_prompt_path: Optional[Path] = None,
                         base_system_prompt: Optional[str] = None) -> str:
    """Build a dynamic system prompt with headroom-optimized stdlib references."""
    if base_system_prompt is not None and base_system_prompt_path is not None:
        raise ValueError("pass base_system_prompt or base_system_prompt_path, not both")
    target_path = base_system_prompt_path or SYSTEM_PROMPT_PATH
    full_prompt = (base_system_prompt if base_system_prompt is not None
                   else target_path.read_text(encoding="utf-8"))
    
    begin_marker = "# BEGIN GENERATED STDLIB REFERENCE"
    end_marker = "# END GENERATED STDLIB REFERENCE"
    
    if begin_marker not in full_prompt or end_marker not in full_prompt:
        return full_prompt
        
    start_idx = full_prompt.index(begin_marker) + len(begin_marker)
    end_idx = full_prompt.index(end_marker)
    
    raw_stdlib_block = full_prompt[start_idx:end_idx]
    unfiltered = (full_prompt[:start_idx] + "\n" + raw_stdlib_block.strip()
                  + "\n" + full_prompt[end_idx:])

    # Filtering is a FALLBACK for token pressure, not an unconditional
    # transform -- if the unfiltered prompt fits with a REAL margin, keep
    # the whole reference rather than risk dropping something the prose
    # depends on. Bare positivity is not enough margin -- see
    # _MIN_UNFILTERED_HEADROOM's comment. `+ user_prompt` approximates the
    # full request; later additions (generation_profiles.append_brief, a
    # refine preamble) aren't visible here, so this is a conservative
    # estimate, not the final admission check -- providers.py's own TPM
    # preflight is still the authority.
    #
    # PLUGINFORGE_STDLIB_FILTER_ALWAYS is a benchmark-only knob (queue item 6's
    # A/B: does making the trim unconditional -- not just a low-headroom
    # rescue -- hold generation quality?), same pattern as
    # PLUGINFORGE_PROMPT_VARIANT. Not read anywhere outside bench/; if the A/B
    # says yes, promoting it means deleting the gate below, not keeping this
    # flag around as a permanent toggle.
    if (os.environ.get("PLUGINFORGE_STDLIB_FILTER_ALWAYS", "0") == "0"
            and providers.headroom_tokens(unfiltered + user_prompt) > _MIN_UNFILTERED_HEADROOM):
        return unfiltered

    domains = extract_relevant_domains(user_prompt)
    trimmed_stdlib_block = filter_stdlib_block(raw_stdlib_block, domains)

    return full_prompt[:start_idx] + "\n" + trimmed_stdlib_block.strip() + "\n" + full_prompt[end_idx:]


if __name__ == "__main__":
    test_user_prompt = "Create a lowpass filter plugin with resonant Q control."
    dynamic_prompt = build_dynamic_prompt(test_user_prompt)
    print(f"Original system prompt size: {len(SYSTEM_PROMPT_PATH.read_text())} chars")
    print(f"Dynamic system prompt size:  {len(dynamic_prompt)} chars")
