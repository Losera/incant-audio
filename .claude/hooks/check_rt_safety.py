#!/usr/bin/env python3
"""PreToolUse hook (Write/Edit/MultiEdit): blocks edits that introduce
non-real-time-safe constructs inside the two known audio-thread entry points --
FaustEngine::process() and PluginForgeProcessor::processBlock() -- per the RT-safety
rules in docs/audio_thread_example.md and docs/handoff_audiothread_task.md.

Scoping is anchored on the fully-qualified function signature, not a bare
"process(" token search, because host/Source/FaustEngine.cpp already contains an
unrelated stray `ParamPool::pushToFaust()` definition in the same file (a known,
separately-tracked bug). From the matched opening brace, a manual brace-depth
count extracts exactly that function's body -- this is what correctly EXCLUDES
FaustEngine::compile()'s body (same file, a few lines away), which legitimately
uses new/delete/std::mutex on its own detached background thread.

For Write, tool_input.content is the full new file -- scanned directly. For
Edit/MultiEdit, tool_input only carries old_string/new_string (or an edits list),
not the resulting file, so this hook reads the file currently on disk and
reconstructs the post-edit content itself before scanning it. If the expected
old_string isn't found in the on-disk content (stale read, race, mismatch), this
fails closed (exit 2) rather than silently allowing the edit through unscanned.

KNOWN LIMITATION (documented, not solved here): only these two named functions
are scoped. A helper function they call into is invisible to this hook -- real
RT-thread-reachability requires a call graph, which a regex/brace-counter cannot
provide. The `// SUBTLE:` comment convention plus human review remains the
fallback for that gap; see COLLABORATION.md §1 and the architecture-planning
skill's hookable-vs-not classification.
"""
import json
import re
import sys
from pathlib import Path

WATCHED_FILE_RE = re.compile(
    r"(^|/)host/Source/(FaustEngine\.(cpp|h)|PluginProcessor\.(cpp|h))$"
)

ANCHOR_RE = re.compile(
    r"(FaustEngine::process|\w+::processBlock)\s*\([^{]*?\)\s*\{"
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
                    "docs/audio_thread_example.md and the HUMAN-OWNED list in "
                    "COLLABORATION.md §1). RT code must never allocate/free heap "
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
