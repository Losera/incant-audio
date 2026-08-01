#!/usr/bin/env python3
"""PreToolUse hook (Write/Edit/MultiEdit): guards llm/prompts/system_prompt.txt.

REPLACES check_adr009_prompt_sync.py, which is now retired. That hook kept the
ADR-009 rule text in sync across two prompt files. As of 2026-07-21 there is only
one prompt file — bench/prompts/system_faust.txt was deleted and the benchmark now
loads the production prompt — so a *sync* check has nothing to compare against and
would fail closed on every edit.

What replaced it is a stronger check on the one remaining file. The sync hook's
real-world failure is worth recording: it verified one sentence and one regex, and
the team believed it guaranteed the two files could not drift. They had in fact
drifted substantially, and — worse — BOTH files taught Faust functions that do not
exist (`ef.ping_pong`, `ef.chorus`, `ef.flanger`). Two of the four few-shot examples
in the production prompt did not compile. A check on a proxy created confidence
proportional to the invariant rather than to the proxy.

WHAT THIS ENFORCES (all mechanically decidable):
  1. The ADR-009 duplicate-symbol sentence is still present.
  2. A "process ... exactly once" clause is still present.
  3. The generated-stdlib-block markers are intact, so tools/gen_stdlib_block.py
     can still splice, and the block cannot be silently hand-edited away.
  4. Every `ns.func` token in the file resolves against the INSTALLED Faust
     library — the anti-fabrication check, and the one that would have caught the
     2026-07-21 defect on the day it was introduced.

WHAT THIS DOES NOT CATCH (stated per COLLABORATION.md §7):
  - Whether a few-shot example COMPILES. That needs the faust binary and is too
    slow for a PreToolUse hook; tests/test_prompt_stdlib.py covers it in CI.
  - Whether a rule or description is *correct*, only that names exist. A function
    can exist and still be described wrongly or used with the wrong arity.
  - Prompt QUALITY. Nothing mechanical can tell you a prompt generates worse Faust;
    that is what bench/check_prompt_regression.py and the efficacy study are for.

Skips silently (exit 0) when the Faust library is not installed — the check would
be meaningless, and a machine without Faust must still be able to edit docs.
Fails closed (exit 2) on any unexpected error.
"""
import json
import re
import sys
from pathlib import Path

# Every prompt in llm/prompts/, not just system_prompt.txt.
#
# ⚠️ THIS WAS A SINGLE HARD-CODED PATH, and that made the guard a trap rather
# than a control the moment a second prompt was added. `instrument_prompt.txt`
# would have matched nothing here: no ADR-009 duplicate-`process` rule check, no
# stdlib resolution, no foreign-construct scan -- while every gate stayed green
# and the file looked guarded because its sibling was. That is exactly the class
# check.sh's header names, "believing a control runs when it does not."
#
# Generalised BEFORE the second prompt exists, deliberately, so the new file is
# born covered rather than retrofitted into coverage later.
PROMPT_RE = re.compile(r"(^|/)llm/prompts/[A-Za-z0-9_-]+\.txt$")
PROMPT_DIR_REL = "llm/prompts"

FAUST_STDLIB = Path("/usr/share/faust/stdfaust.lib")

SHARED_SENTENCE_RE = re.compile(r"Never define the same variable or function name[^\n]*")
PROCESS_ONCE_RE = re.compile(r"process[^\n]*exactly once")
BEGIN_MARKER = "# BEGIN GENERATED STDLIB REFERENCE"
END_MARKER = "# END GENERATED STDLIB REFERENCE"

# Constructs from other languages that Faust does not have. On 2026-07-27 the prompt
# was found instructing "use let bindings or with { } blocks" — Faust has `with`,
# `letrec` and `environment`, and no `let`. That single word produced the
# `syntax error, unexpected IDENT` recorded for P6 #9: the prompt was teaching the
# failure it was blamed for, for weeks.
#
# --verify-prompt could not see it, and neither could a compile check, because the
# claim was PROSE. It contained no code span to extract. Hence a rule that reads
# English rather than Faust.
#
# Deliberately excluded: if, for, while, class, then, else, function, case, int,
# float, import, select. Either Faust has them or they are ordinary English that
# appears throughout the prompt ("If the effect you need...", "For stereo effects...").
# A check that fires on the sentence explaining the rule is worse than no check.
FOREIGN_CONSTRUCTS = {
    "let": "Faust has `with { }`, `letrec` and `environment`. There is no `let`.",
    "lambda": r"Faust writes anonymous functions as \(x).(expr), not `lambda`.",
    "def": "Faust defines with `name = expr;`. There is no `def`.",
    "elif": "Faust has no conditional statement at all; use `select2`/`select3`.",
    "var": "Faust has no mutable variables. State goes through `~` or `rwtable`.",
    "return": "A Faust definition IS its value. There is no `return`.",
    "null": "Faust has no null. An absent signal is silence, `0`.",
    "foreach": "Faust iterates with `par`, `seq`, `sum`, `prod`.",
}
# A sentence that DENIES the construct is the fix, not the defect:
# "Faust has NO let bindings" must pass while "use let bindings" must not.
NEGATION_RE = re.compile(
    r"\b(?:no|not|never|non|without|lacks?|lacking|avoid|instead\s+of|"
    r"unsupported|does\s+not|doesn't|isn't|is\s+not|has\s+no)\b",
    re.I,
)
SENTENCE_SPLIT_RE = re.compile(r"(?<=[.!?])\s+")

WATCHED_TOOLS = {"Write", "Edit", "MultiEdit"}


def resolved_posix_path(file_path: str, cwd: str) -> str:
    p = Path(file_path)
    if not p.is_absolute():
        p = Path(cwd) / p
    return p.resolve(strict=False).as_posix()


def get_effective_content(tool_name: str, tool_input: dict, abs_path: Path) -> str:
    """Post-edit content. For Edit/MultiEdit this is reconstructed from disk;
    an old_string that is not found fails closed rather than guessing."""
    if tool_name == "Write":
        return tool_input.get("content", "")

    if not abs_path.exists():
        raise RuntimeError(f"cannot verify prompt invariants: {abs_path} not on disk")
    content = abs_path.read_text()

    if tool_name == "Edit":
        edits = [{
            "old_string": tool_input.get("old_string", ""),
            "new_string": tool_input.get("new_string", ""),
            "replace_all": tool_input.get("replace_all", False),
        }]
    else:
        edits = tool_input.get("edits", [])

    for edit in edits:
        old = edit.get("old_string", "")
        new = edit.get("new_string", "")
        if old not in content:
            raise RuntimeError(
                "cannot verify prompt invariants: old_string not found in current "
                "file content (stale read or concurrent modification) -- refusing "
                "to guess, failing closed"
            )
        count = -1 if edit.get("replace_all") else 1
        content = content.replace(old, new, count) if count > 0 else content.replace(old, new)

    return content


def foreign_construct_problems(content: str) -> list[str]:
    """Sentences that RECOMMEND a construct Faust does not have.

    Scans everything except the generated stdlib block, which is machine-written
    from the installed library and cannot contain prose. Line wrapping is collapsed
    first, because the rules are bullets that wrap mid-sentence and a per-line scan
    would split "Faust has NO let / bindings" across the negation.

    NOT COVERED, and it matters: this recognises a fixed list of constructs from
    other languages. A construct Faust lacks that is NOT on the list, or a real
    Faust function described with the wrong arity, passes clean. The list is the
    proxy; the invariant is "the prompt teaches only real Faust", and the gap
    between them is why tests/test_prompt_stdlib.py compiles the claims as well.
    """
    prose = content
    if BEGIN_MARKER in prose and END_MARKER in prose:
        head, _, rest = prose.partition(BEGIN_MARKER)
        prose = head + rest.partition(END_MARKER)[2]

    flat = re.sub(r"\s+", " ", prose)
    problems = []
    for sentence in SENTENCE_SPLIT_RE.split(flat):
        if NEGATION_RE.search(sentence):
            continue
        for kw, why in FOREIGN_CONSTRUCTS.items():
            if re.search(rf"\b{re.escape(kw)}\b", sentence):
                problems.append(f"`{kw}` — {why}\n      in: \"{sentence.strip()[:110]}\"")
    return problems


def block(message: str) -> int:
    print(
        "BLOCKED (prompt invariants): " + message + "\n\n"
        f"Files in {PROMPT_DIR_REL}/ are loaded by the product AND the benchmark, "
        "so an error here changes generated audio AND invalidates "
        "every measurement. If a stdlib name was rejected, check it against the "
        "installed library rather than memory:\n"
        "    python tools/gen_stdlib_block.py --verify-prompt\n"
        "    python tools/gen_stdlib_block.py --write   # regenerate the block\n"
        "See .claude/hooks/check_prompt_invariants.py for what this does NOT catch.",
        file=sys.stderr,
    )
    return 2


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("tool_name") not in WATCHED_TOOLS:
        return 0

    tool_input = payload.get("tool_input", {})
    file_path = tool_input.get("file_path", "")
    if not file_path:
        return 0

    path = resolved_posix_path(file_path, payload.get("cwd", "."))
    match = PROMPT_RE.search(path)
    if not match:
        return 0

    content = get_effective_content(payload["tool_name"], tool_input, Path(path))

    if not SHARED_SENTENCE_RE.search(content):
        return block(
            "this edit would remove the ADR-009 duplicate-symbol sentence "
            '(anchor: "Never define the same variable or function name..."). '
            "That rule exists because duplicate-`process` regressions were a "
            "measured failure class."
        )

    if not PROCESS_ONCE_RE.search(content):
        return block(
            'this edit would remove the "process ... exactly once" clause, '
            "dropping half of the ADR-009 rule text."
        )

    if BEGIN_MARKER not in content or END_MARKER not in content:
        return block(
            "this edit would remove the generated-stdlib-block markers "
            f"({BEGIN_MARKER!r} / {END_MARKER!r}). The block is generated from the "
            "installed Faust library by tools/gen_stdlib_block.py; without the "
            "markers it cannot be regenerated and will silently rot."
        )

    foreign = foreign_construct_problems(content)
    if foreign:
        return block(
            "this edit would teach a construct that Faust DOES NOT HAVE:\n  "
            + "\n  ".join(foreign)
            + "\n\nThis is the 2026-07-27 defect (PF-024). The prompt said \"use let "
              "bindings or with { } blocks\"; Faust has no `let`, and that produced the "
              "`syntax error, unexpected IDENT` the battery recorded for P6 #9. "
              "If you are DENYING the construct rather than recommending it, say so in "
              "the same sentence — \"Faust has NO let bindings\" passes this check."
        )

    # Anti-fabrication check. Requires the installed library; skip if absent.
    if not FAUST_STDLIB.exists():
        return 0

    root = path[: match.start()]
    tools_dir = Path((root + "/" if root else "") + "tools")
    sys.path.insert(0, str(tools_dir))
    try:
        from gen_stdlib_block import verify_prompt_references
    except ImportError as exc:
        raise RuntimeError(
            f"cannot verify stdlib references: {tools_dir}/gen_stdlib_block.py "
            f"not importable ({exc})"
        ) from exc

    problems = verify_prompt_references(content)
    if problems:
        return block(
            "this edit would introduce Faust functions that DO NOT EXIST in the "
            "installed standard library:\n  " + "\n  ".join(problems)
            + "\n\nThis is the exact defect found 2026-07-21. A few-shot example is "
              "the strongest signal in a prompt: teaching a fabricated name licenses "
              "the model to invent more."
        )

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
