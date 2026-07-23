# docs/.fleet/ — fleet control signals (overseer-owned)

Machine-checkable markers that let idle sessions **auto-resume** when their dependencies land,
with no human in the loop. A Claude session goes idle after its turn; the only way it wakes is
a background task it armed before going idle. So each blocked session arms a watcher that polls
one deterministic signal here and exits when the signal appears — which re-invokes that session.

## The auto-resume relay (S2 example)

S2 (Prompting UX) is blocked on two upstream gates: **A** = S3 lands the PluginEditor split;
**B** = S1 lands Faust-source retention. We unblock it in two stages:

**Stage 1 — overseer watcher (armed in the S6 session).** Polls the concrete artifacts:
- Gate A: `PromptPanel.h`, `CodeEditorPanel.h`, `ParamGridPanel.h` all exist in `host/Source/`.
- Gate B: `getStateInformation` in `PluginProcessor.h` is no longer the empty `{}` stub.

When both hold, the overseer wakes, **validates** (build/split sane, retention real), then creates
the signal file `docs/.fleet/S2_UNBLOCKED`. The overseer is the authority on "actually ready,"
not mere file presence.

**Stage 2 — S2 watcher (armed in the S2 session).** Polls only for `docs/.fleet/S2_UNBLOCKED`.
When it appears, S2 wakes and resumes Wave 1 (PromptPanel → CodeEditorPanel).

```
Stage 1:  until [ -f host/Source/PromptPanel.h ] && [ -f host/Source/CodeEditorPanel.h ] \
                 && [ -f host/Source/ParamGridPanel.h ] \
                 && ! grep -qE 'getStateInformation\([^)]*\)[[:space:]]*override[[:space:]]*\{[[:space:]]*\}' \
                       host/Source/PluginProcessor.h; do sleep 120; done
          # overseer wakes -> validate -> touch docs/.fleet/S2_UNBLOCKED

Stage 2:  until [ -f docs/.fleet/S2_UNBLOCKED ]; do sleep 120; done
          # S2 wakes -> resume Wave 1
```

## Caveats (name them honestly)
- **Session liveness required.** A background watcher only wakes the session that armed it, and
  only while that session's process is alive. If the S2 terminal is closed, its watcher dies —
  that is the one thing that still needs a human (relaunch S2). Nothing else does.
- **Overseer liveness for Stage 1.** If the overseer session is down when the gates land, the
  `S2_UNBLOCKED` marker won't be created. Fallback: S2 can instead watch the Gate-A/Gate-B
  artifacts *directly* (Stage-1 condition), decoupling it from the overseer at the cost of
  resuming on file-presence rather than overseer-validated readiness. Direct-watch = more robust;
  relay = validated. Default is the relay.
- **Signal is one-shot.** Delete `S2_UNBLOCKED` to re-arm. Markers are transient control state,
  not project history.

## Signal registry
| Signal file | Set by | Watched by | Means |
|---|---|---|---|
| `S2_UNBLOCKED` | overseer (Stage 1) | S2 | Gate A + Gate B validated; S2 may start Wave 1 |
