#!/usr/bin/env python3
"""PreToolUse hook (Write/Edit/MultiEdit): blocks edits that introduce
non-real-time-safe constructs inside any function reachable from the audio
thread, per the RT-safety rules in docs/fixplan_pushtofaust_swap.md.

SCOPED FUNCTIONS -- the transitive closure of PluginForgeProcessor::processBlock,
enumerated by hand because a regex/brace-counter cannot build a call graph:

    PluginForgeProcessor::processBlock   the audio-thread entry point
      -> FaustEngine::enterAudio/exitAudio  (header-inline, atomics only)
      -> ParamPool::pushToFaust             writes Faust zones every block
      -> FaustEngine::process               calls dsp->compute()
      -> OutputGuard::process               NaN/DC/limit before the speakers

PF-015: only the first and third of those used to be scoped. `pushToFaust` moved
ONTO the audio thread with the PF-004 fix (efbb5a5) and was never added, so for
several weeks the hook's coverage and the actual audio path disagreed -- and the
hook's own docstring said so while everything else described the path as
"hook-guarded". OutputGuard::process has been on the audio thread since it was
written (91a5a89) and was likewise never scoped.

Scoping is anchored on the fully-qualified function signature, not a bare
"process(" token search. From the matched opening brace a manual brace-depth
count extracts exactly that function's body -- this is what correctly EXCLUDES
neighbours in the same files that legitimately allocate or lock on background
threads: FaustEngine::compile/runCompile (new/delete/std::mutex on the compile
worker), ParamPool::remap (reserve/push_back on the compile thread), and
OutputGuard::prepare/reset.

For Write, tool_input.content is the full new file -- scanned directly. For
Edit/MultiEdit, tool_input only carries old_string/new_string (or an edits list),
not the resulting file, so this hook reads the file currently on disk and
reconstructs the post-edit content itself before scanning it. If the expected
old_string isn't found in the on-disk content (stale read, race, mismatch), this
fails closed (exit 2) rather than silently allowing the edit through unscanned.

WHAT THIS STILL DOES NOT CATCH -- say the quiet part, per COLLABORATION.md §7:

  * A NEW function called from an already-scoped one. The closure above is
    maintained by hand; adding `myHelper()` to processBlock does not add
    myHelper's body to this hook's scope. Real reachability needs a call graph.
    Whoever adds such a call must add the function here in the same commit.
  * Anything a scoped function reaches through a library (juce::, std::,
    libfaust). `dsp->compute()` is not scanned and cannot be.
  * Non-obvious allocation: a std::vector growing, a juce::String being copied,
    a std::function being assigned. The BANNED list is textual and catches the
    named constructs, not every way to touch the heap.
  * Priority inversion, unbounded loops, syscalls behind an inlined wrapper.

The `// SUBTLE:` convention plus review remains the fallback for that gap.
"""
import json
import re
import sys
from pathlib import Path

WATCHED_FILE_RE = re.compile(
    r"(^|/)host/Source/"
    r"(FaustEngine|PluginProcessor|ParamPool|OutputGuard)\.(cpp|h)$"
)

# One alternative per entry in the closure documented above. `FaustEngine::process`
# is spelled with a trailing \b so it cannot also match `processBlock`, and
# `\w+::processBlock` keeps working for whatever the processor class is called.
ANCHOR_RE = re.compile(
    r"("
    r"FaustEngine::process\b"
    r"|\w+::processBlock"
    r"|ParamPool::pushToFaust"
    r"|OutputGuard::process\b"
    r")\s*\([^{]*?\)\s*\{"
)

BANNED = [
    (re.compile(r"\bnew\b"), "new"),
    (re.compile(r"\bmalloc\s*\("), "malloc("),
    (re.compile(r"\bdelete\b"), "delete"),
    (re.compile(r"\bfree\s*\("), "free("),
    (re.compile(r"\bstd::mutex\b"), "std::mutex"),
    (re.compile(r"\bstd::lock_guard\b"), "std::lock_guard"),
    (re.compile(r"\bstd::unique_lock\b"), "std::unique_lock"),
    (re.compile(r"\.lock\s*\("), ".lock("),
    (re.compile(r"\bstd::cout\b"), "std::cout"),
    (re.compile(r"\bstd::cerr\b"), "std::cerr"),
    (re.compile(r"\bprintf\s*\("), "printf("),
    (re.compile(r"\bfprintf\s*\("), "fprintf("),
    (re.compile(r"\bjuce::Logger\b"), "juce::Logger"),
    (re.compile(r"\bfopen\s*\("), "fopen("),
    (re.compile(r"\bstd::ifstream\b"), "std::ifstream"),
    (re.compile(r"\bstd::ofstream\b"), "std::ofstream"),
    (re.compile(r"\bjuce::File\b"), "juce::File"),
]

WATCHED_TOOLS = {"Write", "Edit", "MultiEdit"}


def resolved_posix_path(file_path: str, cwd: str) -> Path:
    p = Path(file_path)
    if not p.is_absolute():
        p = Path(cwd) / p
    return p.resolve(strict=False)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def extract_function_bodies(text: str):
    bodies = []
    for m in ANCHOR_RE.finditer(text):
        name = m.group(1)
        brace_start = m.end() - 1
        depth = 0
        end_idx = None
        for i in range(brace_start, len(text)):
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end_idx = i
                    break
        # Unterminated brace (shouldn't happen in valid C++) -- fail safe by
        # treating the rest of the file as in-scope rather than under-scoping.
        if end_idx is None:
            end_idx = len(text)
        bodies.append((name, text[brace_start + 1 : end_idx]))
    return bodies


def get_effective_content(tool_name: str, tool_input: dict, abs_path: Path) -> str:
    if tool_name == "Write":
        return tool_input.get("content", "")

    if not abs_path.exists():
        raise RuntimeError(f"cannot verify RT-safety: {abs_path} not found on disk")
    content = abs_path.read_text()

    if tool_name == "Edit":
        edits = [
            {
                "old_string": tool_input.get("old_string", ""),
                "new_string": tool_input.get("new_string", ""),
                "replace_all": tool_input.get("replace_all", False),
            }
        ]
    else:  # MultiEdit
        edits = tool_input.get("edits", [])

    for edit in edits:
        old = edit.get("old_string", "")
        new = edit.get("new_string", "")
        if old not in content:
            raise RuntimeError(
                "cannot verify RT-safety: old_string not found in current file "
                "content (stale read or concurrent modification) -- refusing to "
                "guess, failing closed"
            )
        if edit.get("replace_all"):
            content = content.replace(old, new)
        else:
            content = content.replace(old, new, 1)

    return content


def main() -> int:
    payload = json.load(sys.stdin)
    tool_name = payload.get("tool_name")
    if tool_name not in WATCHED_TOOLS:
        return 0

    tool_input = payload.get("tool_input", {})
    file_path = tool_input.get("file_path", "")
    if not file_path:
        return 0

    cwd = payload.get("cwd", ".")
    abs_path = resolved_posix_path(file_path, cwd)
    if not WATCHED_FILE_RE.search(abs_path.as_posix()):
        return 0

    content = get_effective_content(tool_name, tool_input, abs_path)
    stripped = strip_comments(content)
    bodies = extract_function_bodies(stripped)

    for func_name, body in bodies:
        for pattern, token in BANNED:
            if pattern.search(body):
                print(
                    f"BLOCKED (RT-safety): this edit introduces `{token}` inside "
                    f"`{func_name}`, which runs on the real-time audio thread (see "
                    "docs/fixplan_pushtofaust_swap.md; audio-path changes are Tier 2 "
                    "under COLLABORATION.md §3). RT code must never allocate/free heap "
                    "memory, lock a mutex, or do I/O/logging. If you believe this "
                    "function was mis-scoped by this regex-based check (a known "
                    "limitation -- see .claude/hooks/check_rt_safety.py), stop and "
                    "ask the human rather than retrying the same edit.",
                    file=sys.stderr,
                )
                return 2

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
