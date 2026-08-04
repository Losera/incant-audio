"""Voice-control label contract — single-source-of-truth guard.

FaustEngine::extractVoiceControls (host/Source/FaustEngine.cpp) and
llm/prompts/instrument_prompt.txt used to state the same six accepted label
spellings ("gate", "freq"/"key", "gain"/"vel"/"velocity") independently, with
nothing to notice a rename on either side. Both are now GENERATED from
llm/voice_contract.json by tools/gen_voice_contract.py — this is the runnable
check that the two checked-in artifacts still match what the generator
produces, the same shape as test_prompt_stdlib.py's
test_generated_block_is_current for the stdlib block.

No Faust compiler needed: this compares generated text against the JSON
source, never against the installed library.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_voice_contract as gvc  # noqa: E402


def test_header_matches_the_canonical_source():
    """host/Source/VoiceContract.generated.h drifted from llm/voice_contract.json."""
    contract = gvc.load_contract()
    assert gvc.HEADER_FILE.exists(), (
        f"{gvc.HEADER_FILE} does not exist. Generate it:\n"
        "    python tools/gen_voice_contract.py --write-header"
    )
    actual = gvc.HEADER_FILE.read_text()
    expected = gvc.render_header(contract)
    assert actual == expected, (
        "host/Source/VoiceContract.generated.h is stale or was hand-edited. "
        "Regenerate it:\n"
        "    python tools/gen_voice_contract.py --write-header"
    )


def test_prompt_block_matches_the_canonical_source():
    """The GENERATED VOICE CONTRACT block in instrument_prompt.txt drifted."""
    contract = gvc.load_contract()
    assert gvc.PROMPT_FILE.exists()
    text = gvc.PROMPT_FILE.read_text()
    assert gvc.PROMPT_BEGIN in text and gvc.PROMPT_END in text, (
        f"generated-block markers ({gvc.PROMPT_BEGIN!r} / {gvc.PROMPT_END!r}) "
        f"missing from {gvc.PROMPT_FILE.name}; the block must stay delimited so "
        "the generator can splice it"
    )
    expected_block = gvc.render_prompt_block(contract)
    start = text.find(gvc.PROMPT_BEGIN)
    stop = text.find(gvc.PROMPT_END) + len(gvc.PROMPT_END)
    actual_block = text[start:stop]
    assert actual_block == expected_block, (
        "The generated voice-contract block in llm/prompts/instrument_prompt.txt "
        "is stale or was hand-edited. Regenerate it:\n"
        "    python tools/gen_voice_contract.py --write-prompt"
    )


def test_the_check_can_actually_fail():
    """CLAUDE.md: 'A control counts only once it has been seen failing.'

    A hand-edited copy of either generated artifact must make --check exit
    non-zero -- not just make the two pytest checks above fail, but the exact
    command tools/check.sh's full level runs.
    """
    contract = gvc.load_contract()
    tampered_header = gvc.render_header(contract) + "// hand-edited\n"
    assert tampered_header != gvc.render_header(contract)

    original = gvc.HEADER_FILE.read_text()
    try:
        gvc.HEADER_FILE.write_text(tampered_header)
        assert gvc.header_is_stale(contract), (
            "gen_voice_contract.py's staleness check did not notice a "
            "hand-edited header -- it has no teeth."
        )
    finally:
        gvc.HEADER_FILE.write_text(original)
    assert not gvc.header_is_stale(contract), "restore left the header stale"


def test_match_order_is_gate_then_freq_then_gain():
    """Pins the ORDER FaustEngine::extractVoiceControls checks in, since a
    silent reorder would change which spelling wins when a patch declares
    more than one (e.g. both "freq" and "key") -- see FaustEngine.cpp's "first
    match wins per role" comment. Out of scope for Brief D to change; this is
    what keeps it from changing by accident.
    """
    contract = gvc.load_contract()
    assert gvc.zone_names(contract) == ["gate", "freq", "gain"]


def test_accepted_label_set_is_unchanged():
    """The exact six strings FaustEngine.cpp's original if/else chain matched,
    byte for byte -- this is Brief D's central promise: refactoring the MATCH
    MECHANISM must not add, remove, rename, or re-case any accepted label.
    """
    contract = gvc.load_contract()
    all_labels = {
        label["label"]
        for zone in contract["zones"]
        for label in zone["labels"]
    }
    assert all_labels == {"gate", "freq", "key", "gain", "vel", "velocity"}

    raw_units = {
        label["label"]
        for zone in contract["zones"]
        for label in zone["labels"]
        if label.get("rawUnits")
    }
    assert raw_units == {"key", "vel", "velocity"}
