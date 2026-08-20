# Implementation Plan — BYO-LLM Mode ("spend your own inference")

**Frozen point-in-time record, 2026-08-06. Not maintained — not implemented, no ADR ratified it.**
Referenced by `llm/export_prompt.py` for provenance. The FLEET/S-lane roster named below was
retired 2026-07-25 (COLLABORATION.md history) — read the origin line as history, not process.

**Origin:** FLEET cross-lane request #6, **authorized by the human 2026-07-23.**
**Author:** S7 Competitive Research. **Owns delivery:** S1 (core) + S2/S3 (UI), phased below.
**Rationale:** `docs/competitive_landscape.md` — neutralizes our inference-cost/quota exposure
(Amorph spends $0 by using the user's own ChatGPT/Claude; we fight Gemini's ~20/day cap).

---

## What "without our intervention" means, precisely
A mode in which **PluginForge never calls an LLM**. We give the user a ready-to-paste prompt;
they run it in *their own* ChatGPT/Claude/Gemini (their subscription, their cost, their quota);
they paste the returned Faust back; we compile it. Zero API key, zero provider call, zero
network on our side. This is the Amorph **ASK → COMPILE → PLAY** loop, but on Faust and with our
existing safety net (`OutputGuard`) and — later — our error-feedback assist.

Two halves:
1. **Prompt export** — emit a self-contained payload the user pastes into any chat LLM.
2. **Code intake + compile** — user pastes Faust back; we compile via the existing path and
   surface compile errors so they can iterate manually.

---

## Invariants this feature must not break
- **One prompt, one measurement** (STATUS.md). The exported payload MUST load the *same*
  `llm/prompts/system_prompt.txt` that `generate.py` uses — do **not** fork the prompt.
  (`generate.py:41` is the reference load: `prompts/system_prompt.txt`.)
- **No key, no provider, no network in BYO.** `export_prompt.py` must not import/instantiate a
  provider, read an API key, or hit the network. That is the entire point; make it a test.
- **ADR-011 one-JSON-line contract** if the host shells out to the exporter (mirror
  `generate.py`'s stdout discipline).
- **Fence stripping.** Chat LLMs wrap code in ```faust … ``` fences. The intake path must strip
  them — reuse the existing helper in `llm/providers.py` (already does this for open models);
  do not reimplement.
- **Ownership by file area** (the S-lane scheme this plan was written under was retired
  2026-07-25, `c58a281`; COLLABORATION.md governs now). `llm/*` is the prompt layer,
  `PromptPanel`/`CodeEditorPanel` are the editor shell, `PluginEditor` is the composer. Keep
  changes within an area; coordinate cross-area work through the human.

---

## Existing anchors (already in the tree — reuse, don't rebuild)
- `PluginProcessor::loadFaustCode(const juce::String& faustCode, const juce::String& prompt={})`
  — `PluginProcessor.h:51`. **The paste→compile path already exists.** BYO code intake calls
  this directly; no new compile plumbing needed.
- `onFaustCompileSuccess` callback — `PluginProcessor.h:83`. Fires on JIT swap; drives the
  "Ready — N params mapped" status. **There is no failure counterpart** — see Phase 1, S1.
- State persistence (getState/setState) — just landed (S1/P11); BYO-compiled patches persist
  for free via the same source-retention blob (BYO sets prompt = the NL request; code = pasted).
- Unified prompt — `llm/prompts/system_prompt.txt`, loaded at `generate.py:41`.
- Fence-strip + provider registry — `llm/providers.py`.

---

## Phase 0 — buildable now, zero collisions  *(the spawned agent builds this)*
Delivers "spend your own inference" at the CLI/standalone level **today**, entirely inside S1's
`llm/*` lane, with no dependency on the in-flight PluginEditor split.

**New file: `llm/export_prompt.py`**
- Loads `prompts/system_prompt.txt` (same path constant as `generate.py`).
- `build_payload(user_request: str) -> str` — returns a self-contained, paste-ready block:
  the system prompt (grounding + stdlib + duplicate-symbol rule + few-shots), then a clearly
  delimited `USER REQUEST:` section, then a one-line instruction to reply with **only** a Faust
  `process = …;` patch in a single ```faust fenced block (so intake fence-stripping is
  deterministic).
- CLI: `python llm/export_prompt.py --prompt "warm tape delay"` → prints payload to stdout for
  copy-paste. `--json` → ADR-011-shaped one-line `{"ok":true,"payload":"…"}` for the host.
- **Must run with every provider env var unset and no API key.** No import of a provider client.

**New file: `llm/strip_fence.py`** *(or lift the existing helper out of `providers.py` if S1
prefers — agent proposes, S1 decides at merge).* A pure `strip_code_fence(text) -> str` used by
both the future host intake and a test. Reuse `providers.py`'s existing logic; do not fork it.

**Tests (`tests/test_export_prompt.py`):**
- payload contains the full system prompt content and the user request verbatim;
- payload is deterministic for a fixed request;
- building a payload performs **no** network/provider call and needs **no** env/API key
  (assert by running with `monkeypatch.delenv` on the provider vars);
- `strip_code_fence` removes ```faust fences and bare ``` fences, leaves unfenced code intact.

**Acceptance:** `python llm/export_prompt.py --prompt X` prints a payload a user can paste into
any chat LLM with no PluginForge network activity; new tests pass under `pytest -m "not
integration"`; the 239-test suite still passes.

**Collision control:** `llm/*` is the live S1 session's lane. The agent works in a **git
worktree** (isolated `main` copy) and hands its diff to S1/overseer to merge — it must **not**
push to shared `main` or edit any file S1 is holding. New files only; no edits to
`generate.py`/`providers.py` (propose those as a cross-lane note if the fence helper needs to
move).

---

## Phase 1 — cross-lane, after the PluginEditor split lands  *(not the agent; routed)*
These need files owned by other live lanes, so they are **cross-lane requests**, not this
agent's work:

- **S1 (processor):** add `std::function<void(const juce::String& error)> onFaustCompileFailure`
  symmetric to `onFaustCompileSuccess`, fired from the compile callback's error path, so the UI
  can show the compiler stderr the user pastes back to their own LLM (manual error-feedback loop,
  the BYO analogue of our auto-retry). Optionally expose the export payload to the host by
  shelling out to `export_prompt.py --json` (mirrors the ADR-011 `generate.py` path — no new C++
  prompt logic).
- **S2 (`PromptPanel`/`CodeEditorPanel`):** a **"Copy Prompt"** button (payload → clipboard) and
  a paste/**Compile** box that calls `loadFaustCode(pastedCode, requestText)`; render compile
  errors from `onFaustCompileFailure`. This is largely already in S2's Wave-1 CodeEditorPanel
  scope — BYO adds the Copy-Prompt button and wires the failure callback.
- **S3 (shell):** an **Integrated ⇄ BYO** mode toggle in the top-level layout; in BYO mode the
  generate button is hidden and the Copy-Prompt + paste flow is shown. No processor internals.

---

## Why this ordering
Phase 0 ships the actual value (a user can spend their own inference via the standalone/CLI now)
without waiting on the split, and without a lane collision. Phase 1 is pure UI sugar over paths
that already exist (`loadFaustCode`) — cheap, and it lands naturally as S2's CodeEditorPanel and
S3's shell come up in Wave 1.

## Open decisions
- Where should `strip_code_fence` live — stay in `providers.py` and be imported, or be lifted
  to its own module? (Agent will import from `providers.py` unless told otherwise.)
- Is BYO a mode toggle, or always-available-alongside (Copy-Prompt button always present,
  generate button present only when a provider is configured)? Recommend the latter — it
  degrades gracefully when there's no API key at all.
