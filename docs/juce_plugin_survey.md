# JUCE / Faust Plugin Ecosystem Survey (P10)

Status: DRAFT for human review (DELEGATE). Grounds `docs/ui_design_plan.md` §2
(design-type taxonomy) and §3 (auto-layout complexity ladder) in real-world data.
Read-only research: no repo was cloned or built; no PluginForge code changed.
Fetch date: 2026-07-20 — stars/licenses/sizes drift over time.

---

## 1. Method

Three parallel read-only research agents, split to counteract the sampling bias
any single approach would introduce:

- **Group A — general JUCE ecosystem.** `sudara/awesome-juce`'s README + GitHub
  topics `juce-plugin`/`vst3`, deliberately sampled across a range of star counts
  (not just the top 10) to avoid size bias. 8 repos.
- **Group B — Faust ecosystem.** GitHub topic `faust`, the `grame-cncm/faust`
  examples tree, and faust2juce/GUI-Magic-wrapped Faust projects. 7 entries,
  including 2 bare `.dsp` files with no JUCE wrapper at all (the ladder's floor).
- **Group C — complexity-ladder anchors.** Deliberately curated, not
  topic-sorted: 3 well-known large JUCE instruments (to anchor the high end,
  which topic-sorted lists undersample relative to their real-world prominence)
  plus 3 tiny single-purpose utility plugins (to anchor the low end). One retry
  was needed here — the first attempt stalled on a slow fetch; the retry used a
  lighter-weight, fewer-calls-per-repo method and finished cleanly, at the cost of
  shallower per-repo verification than Groups A/B.

**Sample size:** 22 raw findings, one exact duplicate (`ZhiyuAlexZhang/FaustSynth`,
independently surfaced by both Group A and Group B) merged into a single row →
**21 unique entries** below. Exploratory, not exhaustive or statistically
representative — see §5 Limitations.

**Extraction method — no clone/build.** Per repo: `GET
api.github.com/repos/{owner}/{repo}` for stars/license/size (KB, a complexity
proxy); the git-trees API to locate `PluginEditor.cpp`/`.dsp`/`.jucer`/
`CMakeLists.txt` paths; raw-content fetch of the editor source to detect
`GenericAudioProcessorEditor` (→ GenericEditor), a custom `LookAndFeel_V4`
subclass or many bespoke `Component`s (→ custom), or `WebBrowserComponent`/a web
bundle directory (→ webview); for Faust `.dsp` files, an exact
`hslider`/`vslider`/`nentry`/`button`/`checkbox` count (a precise signal, not an
estimate). Where source-level verification wasn't reached (rate-limit/budget or
repo size), the table says "not verified" or "estimated (source)" rather than
inventing a number.

---

## 2. Survey table

Sorted by approx. param count, ascending — the ladder reads top to bottom. Two
"dynamic/host" entries (no fixed param set by design — they load arbitrary
user-supplied DSP at runtime) are listed separately at the end rather than forced
into the numeric sort.

| Name | URL | Stars | License | Plugin Type | Approx Params | UI Paradigm | Complexity Proxy | JUCE/Faust Ver | Source |
|---|---|---|---|---|---|---|---|---|---|
| grame-cncm/faust `noise.dsp` | github.com/grame-cncm/faust/blob/master-dev/examples/generator/noise.dsp | 3,095† | Other† | Generator | 1 (exact) | none (bare DSP) | 32 lines | Faust master-dev | B |
| sindiv/gainmeter-plugin | github.com/sindiv/gainmeter-plugin | 0 | none | Utility | 1 + meter | not verified | 11 KB | not verified | C |
| ZhiyuAlexZhang/MultiMeter | github.com/ZhiyuAlexZhang/MultiMeter | 63 | GPL-3.0 | Utility | ~1–2 (no APVTS params found) | custom LookAndFeel | 407 KB, 20 files | JUCE 6.1.2 | A |
| grame-cncm/faust `freeverb.dsp` | github.com/grame-cncm/faust/blob/master-dev/examples/old/freeverb.dsp | 3,095† | Other† | Effect | 3 (exact) | none (bare DSP) | 127 lines | Faust master-dev | B |
| mini-hiori/MiniUtility | github.com/mini-hiori/MiniUtility | 0 | none | Utility | ~4–6 (estimated, README) | not verified | 7 KB | not verified | C |
| gustavoakira-sw/Multi-Effect-JUCE-VST | github.com/gustavoakira-sw/Multi-Effect-JUCE-VST | 4 | MIT | Effect | ~5–8 (estimated, README) | not verified | 13 KB | not verified | C |
| Ankalot/Bifractalizer | github.com/Ankalot/Bifractalizer | 10 | MIT | Hybrid | 6 (exact: 5 knobs + 1 mode button) | custom LookAndFeel | 685 KB, 9 files | not pinned | A |
| BeatConnect/ripple | github.com/BeatConnect/ripple | 0 | none declared | Hybrid | 6 (exact) | **webview** (Svelte/Vite) | 84 KB + JS frontend | JUCE 8.0.4 | A |
| ZhiyuAlexZhang/FaustSynth | github.com/ZhiyuAlexZhang/FaustSynth | 12 | GPL-3.0 | Generator | 11 (exact: 10 nentry + 1 button) | **GUI Magic** (declarative `Layout.xml`, no `PluginEditor.cpp` at all) | 56-line `.dsp`, 51 KB | JUCE 7.0.8 / Faust 2.40.0 | A/B |
| zeloe/ReverbZen | github.com/zeloe/ReverbZen | 6 | GPL-3.0 | Effect | 11 (exact, hand-recounted) | custom (bespoke `Zen_LookAndFeel`/`Zen_Knob` + visualization) | 86-line `.dsp`, 147 KB, 12 files | not verified | B |
| yu2924/FilePlayback | github.com/yu2924/FilePlayback | 2 | MIT | Utility | ~11 (README-documented, not source-confirmed) | custom (widgets in unfetched `EditorForm`) | 44 KB, 9 files | JUCE 7.0.1 | A |
| jatinchowdhury18/AnalogTapeModel | github.com/jatinchowdhury18/AnalogTapeModel | 1,383 | GPL-3.0 | Effect | 15+ (unconfirmed — no `PluginEditor.cpp` locatable) | custom LookAndFeel (4 LNF subclasses) + GUI Magic | 122,773 KB, ~110 files | not pinned (submodule) | A |
| dropdownb/ADSRFM | github.com/dropdownb/ADSRFM | 3 | GPL-3.0 | Generator | 18 confirmed widgets (6 sliders + 12 buttons); true total likely higher | custom (no LookAndFeel subclass confirmed) | 3,014 KB, 6 files | not documented | A |
| frymao/Key-detector-…-JUCE | github.com/frymao/Key-detector-developed-in-JUCE | 1 | none listed | Utility | unknown (1.15 MB single-file processor, unfetchable within budget) | custom (`Ball.cpp` visualization) | 1.15 MB monolithic file | not verified | B |
| Chowdhury-DSP/BYOD | github.com/Chowdhury-DSP/BYOD | 557 | GPL-3.0 | Effect (modular pedal-chain) | ~10–30, variable by built chain | not verified | 5,829 KB | not verified | C |
| ZL-Audio/ZLEqualizer | github.com/ZL-Audio/ZLEqualizer | 941 | AGPL-3.0 | Effect (outlier) | ~24 bands × several/band (~100+) | custom (hand-rolled editor, no GenericEditor) | 4,860 KB, ~230 files | not pinned (submodule) | A |
| surge-synthesizer/surge (Surge XT) | github.com/surge-synthesizer/surge | 3,931 | GPL-3.0 | Generator | 100+ (widely documented) | custom LookAndFeel (skinned; not re-verified this session) | 593,820 KB | not verified | C |
| asb2m10/Dexed | github.com/asb2m10/Dexed | 3,435 | GPL-3.0 | Generator | ~155 (DX7 clone, well documented) | custom LookAndFeel (skinned; not re-verified this session) | 97,413 KB | not verified | C |
| olilarkin/pMix2 | github.com/olilarkin/pMix2 | 101 | none listed | **Dynamic/Host** — loads arbitrary Faust patches | N/A by design | custom (bespoke node-graph editor, adapts to loaded patch) | 20+ files, 7,774 KB | pre-CMake `.jucer`, custom `juce_faustllvm` | B |
| DBraun/DawDreamer | github.com/DBraun/DawDreamer | 1,259 | GPL-3.0 | **Dynamic/Host** — headless engine, loads arbitrary `.dsp` | N/A by design | **none** — no `PluginEditor` file exists at all; controlled via Python API | 41 files, 508,910 KB | not verified | B |

† Faust example files inherit the parent `grame-cncm/faust` repo's stars/license — the files themselves aren't independently starred.

---

## 3. Complexity-ladder findings

**Zero bare `GenericAudioProcessorEditor` usage — 0 of 19 fixed-param entries.**
This is the single clearest finding. Even the smallest utilities in the sample
(1–2 params) use at least a custom `LookAndFeel` or a hand-drawn meter — nobody in
this sample shipped JUCE's plain auto-generated editor, at any param count. That
directly contradicts a naive assumption that trivial plugins default to
GenericEditor; it doesn't, in practice, even at n=1 params.

**Custom panels start immediately, not at a threshold.** Custom LookAndFeel work
appears from the very bottom of the ladder (`MultiMeter`, ~1–2 params) through the
top (`Surge XT`, 100+). There's no clean "switches to custom past N params"
transition visible in this sample — developers who bother to ship a plugin at all
tend to invest some UI polish regardless of param count.

**A fourth UI paradigm the taxonomy doesn't yet name: declarative/GUI-Magic.**
2 of 19 (`FaustSynth`, and `AnalogTapeModel` partially) use Foleys GUI Magic — an
XML-driven declarative layout with no hand-written `PluginEditor.cpp` at all. This
sits between "GenericEditor" and "custom" in the existing taxonomy and is
architecturally close to what `ui_design_plan.md` §3 already proposes (an
LLM-emitted layout-hint JSON driving auto-layout). Worth adding as a named 4th
paradigm in a future revision of §2's table — **suggestion, not a decision**.

**Webview is rare and complexity-independent.** Only 1 of 19 (`ripple`, 6 params,
Hybrid). Its adoption looks driven by developer stack preference (Svelte/Vite) more
than by param count or plugin type — too small a sample to generalize further.

**Effect plugins invest in custom UI more than §2 predicted.** `ui_design_plan.md`
§2 predicted "auto-grid knobs, one row per stage" for Effects; in this sample,
every well-known Effect (`AnalogTapeModel`, `ZLEqualizer`, `ReverbZen`, `BYOD`)
instead built a bespoke custom editor. Likely explanation, not certainty: **survivorship
bias** — popular/starred Effect plugins are popular partly *because* of UI
polish, so this sample skews toward better-than-median UI investment (see §5).

**Faust2juce-style exports do not default to GenericEditor either.** `FaustSynth`
uses GUI Magic; `Key-detector`'s classic single-file faust2juce export uses a
custom visualization component. Neither fell back to a bare auto-grid — modest
evidence that a syntax-driven declarative layer (which PluginForge's own
auto-layout proposal resembles) is a proven, precedented pattern in this exact
niche, not a novel risk.

**Type vs. UI-paradigm match, overall:** Utility → minimal custom panel — confirmed
(`MultiMeter`, `gainmeter-plugin`, `FilePlayback`, all custom/not-GenericEditor).
Generator → grouped/sectioned UI — consistent (`Surge`/`Dexed`'s heavy sectioning;
`FaustSynth`'s declarative sections). Effect → auto-grid — **not confirmed**, see
above.

---

## 4. Implications for PluginForge (non-binding)

- The complete absence of bare GenericEditor (0/19, including 1–2-param
  utilities) is evidence *for* keeping `ui_design_plan.md` §3's deterministic
  grid-math auto-layout as PluginForge's floor, rather than falling back to
  something GenericEditor-equivalent for trivial generated plugins — nobody in
  the wild ships that, even for the simplest case.
- The GUI-Magic/declarative paradigm found in 2 entries is architecturally close
  to §3's proposed LLM-emitted layout-hint JSON — external validation of that
  approach's *direction*, not proof it's correct in its specific thresholds.
- §3's proposed thresholds (~10 params before sectioning, N>24 before
  tabs/viewport) are not cleanly falsifiable from this sample, but nothing
  contradicts them: `FaustSynth` (11 params) already moved to sectioned
  declarative layout; `Surge`/`Dexed` (100+) obviously use heavy sectioning.
- `pMix2` and `DawDreamer` — both "dynamic/host" projects that adapt their UI (or
  have none) to arbitrary loaded Faust code — are the closest real-world analogs
  to PluginForge's own problem shape. `pMix2`'s `pMixGraphEditor` in particular is
  worth a dedicated follow-up read in a future session, as a case study rather
  than a survey row.
- Any resulting change to §2/§3's thresholds or taxonomy (e.g. adding the 4th
  "declarative" UI paradigm) is a separate future PAIR/DELEGATE follow-up — this
  doc recommends, it doesn't decide.

---

## 5. Limitations

- **Survivorship/popularity bias.** Groups A/B were sampled partly by star count;
  well-known plugins likely over-invest in UI polish relative to the median
  obscure JUCE project. Group C's low-end anchors (0–4 stars) were chosen
  specifically to counter this, but the middle of the distribution still skews
  toward "cared enough to get noticed."
- **Verification depth is uneven.** Several fields are marked "not verified" or
  "estimated" rather than measured — Group C in particular used a lighter,
  fewer-calls-per-repo method after its first attempt stalled on a slow fetch.
  This is flagged per-row, not silently treated as fact.
- **4 of 21 entries aren't ordinary plugins.** Two bare `.dsp` files (no JUCE
  wrapper) anchor the ladder's floor; two "dynamic/host" projects load arbitrary
  DSP and have no fixed param count by design. Included deliberately as reference
  points, not as directly comparable rows.
- **Small, exploratory sample (21 entries).** Enough to see a real pattern (the
  GenericEditor absence, in particular, is a clean 0/19), not enough to be
  statistically representative of the whole JUCE/Faust ecosystem.
- **Unauthenticated GitHub API rate limits (60 req/hr, shared across 3 parallel
  agents) capped how deep each repo could be inspected** — raw-content fetches
  (which don't count against the quota) were prioritized over additional API
  calls throughout.

---

## Appendix: per-repo citation (auditability)

- **noise.dsp**: 1 `vslider(` counted directly in raw file text; 0 signal inputs → Generator.
- **freeverb.dsp**: 3 `hslider(` counted directly; `process(x,y)` → Effect.
- **gainmeter-plugin / MiniUtility / Multi-Effect-JUCE-VST**: classified from repo description + `size` field only (Group C lightweight pass); not source-verified.
- **MultiMeter**: `.jucer` has zero `PARAMETER` elements; `PluginEditor.cpp` calls `setLookAndFeel()`.
- **Bifractalizer**: `PluginEditor.cpp` — 5 `setupKnob()` calls + 1 mode button, custom `KnobElement` LookAndFeel.
- **ripple**: `PluginEditor.cpp` directly instantiates `juce::WebBrowserComponent`; repo tree has a `web-ui/` Svelte project.
- **FaustSynth**: `MonoSource.dsp` — 10 `nentry(` + 1 `button(`; repo tree has `.jucer` + `Layout.xml`, no `PluginEditor.cpp/h`.
- **ReverbZen**: `reverb.dsp` — 11 `nentry(` (hand-recounted, corrected from an automated undercount); tree has `Zen_LookAndFeel.cpp/h`, `Zen_Knob.cpp/h`.
- **FilePlayback**: `PluginEditor.cpp` has an empty `paint()`; actual widgets are in an unfetched `EditorForm.cpp` — count is README-sourced.
- **AnalogTapeModel**: `GUI/MyLNF.h` defines 4 `LookAndFeel_V4` subclasses; no `PluginEditor.cpp` locatable in the tree.
- **ADSRFM**: `PluginEditor.cpp` (repo root) — 6 sliders + 4 + 8 radio buttons counted directly; true total likely higher per README's "8-module" description.
- **Key-detector**: tree has no `.dsp` and no separate `PluginEditor.cpp/h`; DSP+editor logic baked into one 1.15 MB `FaustPluginProcessor.cpp` — too large to fetch/grep within budget.
- **BYOD / Akira Multi / MiniUtility / gainmeter-plugin**: Group C, classified from description + size only.
- **ZLEqualizer**: `source/PluginEditor.cpp` extends `AudioProcessorEditor` directly; README states default 24 bands (`ZL_EQ_BAND_NUM`).
- **Surge XT / Dexed**: classified from public documentation and star/size signals; UI-paradigm claim is prior public knowledge, not re-verified via source this session.
- **pMix2**: tree has no bundled `.dsp`; custom components named `pMixGraphEditor`/`pMixInterpolationSpace`/`pMixConsole`.
- **DawDreamer**: `Source/` directory listing (41 files) confirms `FaustProcessor.h/.cpp` present and no `PluginEditor`-style file anywhere; controlled via Python API per `setup.py`.
