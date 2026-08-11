#!/usr/bin/env python3
"""
llm/prompt_builder.py — Dynamic stdlib reference prompt builder.

Optimizes LLM context headroom by dynamically tailoring stdlib references based on
keywords in the user prompt (e.g. filter, delay, reverb, oscillator, dynamics),
preventing Groq HTTP 413 "Request too large" token overflows while preserving prompt invariants.
"""

import re
from pathlib import Path
from typing import Dict, List, Set, Optional

PROMPT_DIR = Path(__file__).resolve().parent / "prompts"
SYSTEM_PROMPT_PATH = PROMPT_DIR / "system_prompt.txt"

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
    """Filter lines of the stdlib reference block to retain relevant functions."""
    lines = stdlib_block.splitlines()
    filtered_lines: List[str] = []
    
    # Prefix mapping for namespaces
    ns_map = {
        "filter": ["fi.", "pf."],
        "oscillator": ["os."],
        "time": ["de.", "re.", "ef."],
        "dynamics": ["co.", "ma.", "ef."],
        "envelope": ["en.", "ba."]
    }
    
    allowed_prefixes = set()
    for d in domains:
        for p in ns_map.get(d, []):
            allowed_prefixes.add(p)
    # Always include basic math/unit/signal helpers
    allowed_prefixes.update(["ba.", "si.", "ma."])

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
    domains = extract_relevant_domains(user_prompt)
    trimmed_stdlib_block = filter_stdlib_block(raw_stdlib_block, domains)
    
    return full_prompt[:start_idx] + "\n" + trimmed_stdlib_block.strip() + "\n" + full_prompt[end_idx:]


if __name__ == "__main__":
    test_user_prompt = "Create a lowpass filter plugin with resonant Q control."
    dynamic_prompt = build_dynamic_prompt(test_user_prompt)
    print(f"Original system prompt size: {len(SYSTEM_PROMPT_PATH.read_text())} chars")
    print(f"Dynamic system prompt size:  {len(dynamic_prompt)} chars")
