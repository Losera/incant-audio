# host/Source/ Contract — the 64-slot macro model

Least obvious mechanism in the repo — read before touching `ParamPool`,
`ParamIdentity.h`, or `ParamMap.h`.

## Two layers, never conflated
1. **Host-facing**: exactly 64 params, `macro_0..macro_63`
   (`ParamPool::slotId`), created ONCE in
   `PluginProcessor::createParameterLayout()` (:37-71) — `ParamPool` must
   never create one, only look it up (jassert trips otherwise). DAW
   automation lanes bind to these names FOREVER (ADR-004).
2. **Per-patch identity**: discovered at compile time into `ParamInfo`
   (FaustEngine.h:66-140), plus a derived `id`
   (`ParamIdentity::base()+disambiguate()`, ParamIdentity.h:125-186:
   `slug(group)/slug(label)`, "#2" on collision).

## The join: `ParamPool::remap()` (ParamPool.cpp:38-160)
Two passes, order load-bearing: Pass 1 (:79) reclaims — a param whose `id`
held a slot last time gets it back. Pass 2 (:100) packs newcomers lowest-
free-slot-first. `Kind::Meter` is `!eligible` (:77, `isWritable()`), never
takes a slot — an output, not a knob. Overflow past 64 lands in
`RemapResult::overflowed` (:117) — LOGGED, not refused; DSP stays live.

## Value conversion: `ParamMap.h`, the ONLY place
Slot (0..1) <-> zone (real units): `mapSlotToZone`/`mapZoneToSlot`
(:83,135), curve from `[scale:]` or inferred (`Hz` defaults log, :59-66).
`pushToFaust` (audio thread) and the editor's default seeding both call
these; neither may reimplement it (PF-001/PF-037 were this bug, twice).

## Persistence
Slot -> `id` map is a state-blob field (schemaVersion 2). `idScheme`
(`ParamIdentity::kSchemeVersion`) is a ONE-WAY DOOR: changing `slug()`'s
output for an already-accepted input orphans every saved project using
it. A v1 blob (no map) seeds nothing, packs positionally — matching v1.

## Violations
- `disambiguate()`'s "#2" tie-break (ParamIdentity.h:159-166) is positional
  for same-named controls in one group — UNTESTED across a rename of one.
- Nothing asserts `ParamPool::slots.size() == POOL_SIZE` at construction —
  a future `createParameterLayout()` edit could silently desync the two.
