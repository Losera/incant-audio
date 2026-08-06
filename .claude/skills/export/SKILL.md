# /export — Export plugin as a standalone repository

## STUB — DO NOT RUN

This skill is **gated** (2026-08-06). `tools/export_repo.py` emits a passthrough
plugin that never links Faust, never produces sound, and has a compile error
(`TRUE == "TRUE"` type mismatch). Session 008 marked it "manually verified" but the
artifact is structurally impossible — this is verification theater.

### What must land before /export can run

The export path (Track 4 of the alpha plan) requires:

1. **Static-compile Faust** via `faust -lang cpp` -> `class mydsp` header
   (mechanism verified, `faust -o t.h` produces 114-line `class mydsp`).
2. **IPlugFaust link** in the emitted CMakeLists — the exported plugin must call
   `buildUserInterface` + `compute` on the static DSP, not a passthrough stub.
3. **Exported plugin builds, loads, and makes sound** — the verification bar.

Until all three land, `/export` must refuse with this message:

```
/export is gated. tools/export_repo.py emits a silent passthrough stub (no Faust
link, no sound). See the skill file for the landing requirements.
```
