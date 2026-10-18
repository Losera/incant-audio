# Competitive Landscape — Prompt-to-Plugin (living doc)

**Advisory only:** this doc is read-only / advisory — it advises, never edits code or other
teams' files. (The S-lane / FLEET roster it was written under was retired 2026-07-25,
`c58a281`.) **This is the single canonical *living* competitive-intelligence doc** — updated as
the field moves, not a point-in-time snapshot.

**Supersedes / absorbs:** the point-in-time P10 survey (2026-07-20, a one-shot survey of 21
open-source JUCE/Faust repos) is folded in here as the living successor. P10's still-relevant
headline is carried forward in "Prior-survey carryover" below.

**First landed:** 2026-07-23. **Adversarial stance:** the job of this doc is to find where
competitors already beat us and where our thesis is *not* actually differentiated — not to
reassure. Read it as a threat model.

---

## TL;DR — the uncomfortable part

**Our core thesis is no longer novel, and at least one competitor has already shipped it.**
"Natural language → DSP → a compiled plugin that runs in your DAW with an auto-generated UI"
is exactly **Amorph** (Artists in DSP), which is in public open beta *today*, free, VST3/AU,
Mac+Win — and it auto-generates a knob for **every** parameter. Our param grid renders all
64 pool slots in a scrollable viewport (`PluginEditor.cpp:256-262`), but we **have never
listened to a single generated plugin** — that judgment (COLLABORATION.md §1) is still
unperformed. On the one thing a user first hears — "does it make sound" — the shipping
competitor is ahead of us.

We are not out of the game. But our remaining moat is **not** the concept. It is **execution
discipline**: a closed self-correcting compile loop, RT-safety, and *measured* reliability —
none of which any competitor publishes. We should stop selling the idea and start selling the
guarantees.

---

## The field

| Tool | Engine / language | Where it compiles | LLM | Delivery | Business model | Formats |
|---|---|---|---|---|---|---|
| **PluginForge (us)** | Faust → libfaust/LLVM JIT | **In-DAW, integrated** | **Built-in** (`generate.py`, auto-retry on stderr) | Single JUCE host plugin | none yet (free providers) | VST3 + Standalone, **Linux-first** |
| **Amorph** (Artists in DSP) | **Cmajor** JIT | In-DAW, integrated | **External** — "Copy Prompt" → user pastes into ChatGPT/Claude/Gemini → pastes code back | Single host plugin | **Free**; planned marketplace "The Hub", 10% cut | VST3/AU, **Mac+Win** |
| **pluginmaker.ai** | undisclosed (cloud) | **Server-side, in browser** | Built-in, server-side | Downloadable VST3/AU binary | **Subscription credits** | VST3/AU, Mac+Win |
| **ChatDSP** (Dillon Bastan) | Max/MSP (Max for Live) | In Ableton | External agent + API key | M4L device | $10 + user's API credits | Ableton Live Suite only |
| **ConjureDSP / ConjureIDE** | Python or Rust (user picks) | **In-DAW**, interpreted (Python instant-reload) or WASM (Rust via wasmtime) | **External** — BYO coding agent (Claude Code, ChatGPT, any), wired in via an **embedded in-process MCP server** (not copy-paste) | AUv3 plugin | **Free, open source** (GPL-3) | **AUv3 only, macOS 15+ Apple Silicon** |
| **Audio Plugin Coder (APC)** | Real C++/JUCE DSP (generates actual plugin source, not a DSL) | Local build, user's machine (CMake/JUCE 9) | **External** — explicitly agent-agnostic (Claude Code, Cursor, Antigravity, Kilo, Codex, OpenCode); APC bundles no model at all | **Source framework**, not a binary — output is a JUCE project the user builds | **Free, open source (MIT)** | VST3/AU + **CLAP (beta, v1.8.0)**, Win/Mac/**Linux** |
| **MAGDA** | **Faust** (interpreted; JIT "planned") for its vibe-codeable device, Tracktion Engine for native DSP | **In-app**, in-DAW | **External/BYOAK** — currently OpenAI GPT-5 only (CFG-constrained generation drives the DAW's internal DSL and, reportedly, the Faust device's AI panel) | **Not a plugin — a whole free DAW** that hosts VST/AU | **Free, open source (GPL-3)** | Native app, Win/Mac/**Linux** |
| **Sonorie** | C++ DSP via proprietary in-house models ("S1"/"S1 Mini") | Server + browser (compiles to WASM, live preview) | Built-in/proprietary, no external agent | Downloadable binary | **$19.99/mo subscription, no free tier** | VST3/AU/CLAP, Win/Mac/**Linux** |
| **Chaos Audio "AI FX Builder"** | undisclosed | In-app, cloud | Built-in, server-side | Generally-available product feature | undisclosed | undisclosed |

Sources: MusicRadar deep-dive, KVR, Gearspace, Sonicstate, MusicTech (links at bottom).

### Amorph — our closest and most dangerous analog
- **Same architecture as us**, one language over: Cmajor instead of Faust; in-DAW JIT compile;
  auto-generated UI with "knobs for every parameter"; **fully offline** ("No internet
  connection required").
- **Workflow: ASK → COMPILE → PLAY.** A built-in **Copy Prompt** button emits a "pre-optimized
  payload"; the user pastes it into *their own* LLM subscription, gets code, pastes it back;
  Amorph JIT-compiles and builds the UI.
- **The LLM lives outside the plugin.** This is the single biggest architectural fork between us
  and them, and it cuts both ways (see "Threats", #4).
- Ships as **open beta v0.9.9**, **free**, Gumroad. VST3/AU, Win10/11, macOS 10.13+.
- One reviewer: *"I wrote a couple of vst's in under an hour it was amazing"* — and noted the
  **auto-generated graphics needed more effort than the audio code**. 5/5.
- Ships with a blunt tell on reliability: **"Use a master limiter when compiling new code."**
  They are shipping unguarded audio. We built `OutputGuard` for exactly this (see "Moat", #3).
- Framing: "Lego blocks" of **pre-optimized DSP code** the LLM assembles — pitched partly as a
  copyright-safety story (curated blocks, not raw generation).
- Roadmap they've announced: **direct AI integration** (kill the copy-paste), and **"The Hub"**
  marketplace for sharing/selling patches (10% platform cut).
- CEO Leo Biasca-Caroni: *"AI as a creative partner and not as a substitution."*
- **Update, verified 2026-10-12 — they shipped the roadmap item that mattered most to us.**
  Amorph **1.0 beta** (Synthtopia, 2026-09-22) replaced pure copy-paste with: an **in-plugin
  AI assistant that runs on the user's own API key** (voice input supported), *and* **MCP
  connections for Claude Code / Cursor / VS Code / Claude Desktop** — confirmed from
  [`Artists-in-DSP/amorph-for-agents`](https://github.com/Artists-in-DSP/amorph-for-agents)
  (accessed 2026-10-12), whose README documents an MCP hub at `http://127.0.0.1:7330/mcp`
  exposing `get_host_status` / `edit_lines` / `task_complete` / `apply_draft` — live,
  tool-based editing of a running patch, not a paste-the-error-back loop. Status: **v1 beta**;
  the README still notes "older v0.99 installs are copy-paste only," so this is a live
  migration, not a finished cutover. **"The Hub" shipped as "Explore"** — cloud preset
  browsing with Google/email sign-in, likes and ratings; upload/comment from inside the
  plugin is still "coming next," not yet live (per rekkerd.org coverage, accessed 2026-10-12).
  **Amorph MIDI shipped** — a dedicated variant that prompts arpeggiators, sequencers, and
  MIDI logic tools from text (rekkerd.org, accessed 2026-10-12). Patch library is now
  **40+ free patches, each with source open to read/edit** (Synthtopia, 2026-09-22) — the
  patch-sharing network effect we flagged as Threat #3 is visibly compounding. **User signal
  (verified 2026-10-12):** Gumroad shows **183 ratings, 4.9★, 92% five-star**; KVR shows only
  **1 review (5.0★)** and an overall KVR rank of 569 — so community sentiment is strong where
  it's visible (Gumroad) but KVR-specific review volume is still thin. This materially updates
  Threat #4 below — see "Threats" #4.

### pluginmaker.ai — the SaaS play
- **Browser-based, server-side generation.** No local compile; you get a downloadable binary.
- **In-browser instant test loop**: on-screen keyboard + sequencer, preview before you commit —
  a tight iterate/refine UX we do **not** have (ours is single-turn; see Threat #5).
- Users praised for **"beautiful graphical user interfaces"** vs other vibe-coded plugins.
- **Pricing (verified 2026-07-23):** Free $0 / 50 one-time credits · Pro **$30/mo** / 3,300
  credits + "Pro mode" quality · Studio **$100/mo** / 12,000 credits · Enterprise custom
  (anti-piracy licensing, custom installer, cross-DAW optimization, Shopify). Top-ups **$1 /
  100 credits**, never expire. A generation costs **~50 credits (fast model) to 200+
  (state-of-the-art)**.
- Co-founder Dominik Bilski: *"in the coming years, writing code manually won't be a thing.
  What will be left is the fun stuff."* Explicitly targets **bedroom producers** priced out of
  boutique plugins.

### ConjureDSP / ConjureIDE — BYO-agent via MCP, not copy-paste *(new since 2026-08-06)*
- macOS AUv3 plugin, **free and open source (GPL-3)**, from the same team (`michaeljancsy`) that
  later shipped ConjureAlign. Open beta since April 2026; at **v3.0** by July 2026 per Synth
  Anatomy's coverage (accessed 2026-10-12), confirmed current on the KVR product page (accessed
  2026-10-12, which also confirms free pricing and GPL-3 licensing).
- **"ConjureDSP itself doesn't host or route any AI... you select your preferred coding
  assistant, and ConjureDSP plugs into it"** (Rekkerd-sourced coverage, accessed 2026-10-12) —
  same external-LLM economics as Amorph's original model, but the integration is an
  **embedded in-process MCP server**, not copy-paste. Two independent searches (accessed
  2026-10-12) both returned an identical 12-tool list — `compile_and_run`, `get_script`,
  `get_error`, `set_parameter`, `get_parameters`, `get_audio_state`, `list_presets`,
  `save_preset`, `toggle_bypass`, `get_docs`, `list_packages`, `list_tones` — which is strong
  corroboration it's real. **Could not verify the briefed "9 specialized MCP tools" figure**:
  one direct fetch of `conjuredsp.com/docs/` returned this same 12-tool list, a second fetch of
  the same URL returned a 19-tool list adding bundle/UI-authoring tools (`get_bundle_info`,
  `read_bundle_file`, `write_bundle_file`, `validate_bundle`, `smoke_test_ui`, `dsp_probe`,
  `duplicate_bundle`). The discrepancy is flagged rather than resolved — treat the tool count as
  "roughly a dozen, possibly up to ~19," not a settled number, and treat "9" as unconfirmed.
- DSP code: **the ConjureIDE engine itself is open source (GPL-3)**; the Python/Rust scripts a
  user writes or vibe-codes are the user's own, un-licensed by the project. So: open-source
  tool, user-owned output — there is no "closed proprietary DSP library" the way pluginmaker's
  server-side generation is a black box.
- **Before/after/diff spectrogram confirmed** — "ConjureIDE includes a spectrogram view with
  input, output, and difference modes" (two independent search summaries, accessed 2026-10-12,
  consistent with each other).
- **ConjureAlign** (released 2026-08-26, per Bedroom Producers Blog and KVR news, accessed
  2026-10-12): a **free, open-source (GPL-3) FFT cross-correlation phase-alignment plugin**
  (VST3/CLAP/AU, Win+Mac) — not a prompt-to-plugin competitor itself, but it shows the same team
  shipping adjacent free open-source tooling and building a reputation beyond the flagship
  product, the same "ecosystem of small free tools" play Amorph is attempting with its patch
  library.

### Audio Plugin Coder (APC) — the open-source, agent-agnostic framework *(new since 2026-08-06)*
- `github.com/Noizefield/audio-plugin-coder`, verified live 2026-10-12: **MIT license**, **330
  stars, 53 forks**, latest release **v1.8.0**.
- It is a **true framework, not a shipped product** — it bundles **no LLM at all**. It supplies
  a JUCE 9 project skeleton, a tracked 5-phase workflow (**Dream → Plan → Design → Implement →
  Ship**, plus a Setup step), and packaging — then the user points whichever coding agent they
  already pay for (Claude Code, Cursor, Google Antigravity, Kilo, Codex, OpenCode) at the repo.
  Explicitly **agent-agnostic** by design.
- **v1.8.0 added CLAP support (beta)**: *"New plugins can now opt into a CLAP build during
  `/apc-plan` — clap-juce-extensions wraps the same JUCE AudioProcessor and editor, so no
  plugin-code changes are needed"* (CHANGELOG, accessed 2026-10-12) — flagged beta, "comprehensive
  DAW testing remains incomplete" per the project's own notes.
- Built on **JUCE 9** (confirmed from the live repo description 2026-10-12 — resolves the
  briefed JUCE-8-vs-9 ambiguity in favor of 9) and CMake; targets Win/Mac/**Linux**.
- Output is **source you build yourself**, not a one-click binary — closer to a dev toolkit than
  a consumer product. 330 stars / 53 forks is a real adoption signal for a framework this narrow
  (first seen publicly well after Amorph), but it is a GitHub-native, developer-facing audience,
  not Amorph's Gumroad/KVR producer audience — the two aren't competing for the same install base
  yet.

### MAGDA — the free, open-source DAW that also vibe-codes Faust *(new since 2026-08-06, the single biggest find)*
- `github.com/Conceptual-Machines/magda-core`. **Free, open source (GPL-3.0)**, built on
  C++23/JUCE/Tracktion Engine. Initial release 2026-03-19 (Bedroom Producers Blog, accessed
  2026-10-12), v0.5.0 by April (rekkerd.org), actively developed (933 commits on `main`, 234
  stars, 26 forks, verified live 2026-10-12). Win/Mac/**Linux**.
- **This is not a plugin — it is a full DAW**: clip-based session view + timeline arrangement +
  mixer, hybrid audio/MIDI tracks, racks with nestable parallel chains. README (fetched
  directly, accessed 2026-10-12) confirms **"16 LFOs (with bezier curve editor) and 16 macro
  knobs per device and rack."**
- **AI chat does natural-language DAW control**: "create a track called Bass with Serum 2",
  "select all clips below length 1 bar", "transpose the selected notes up an octave" — it works
  over symbolic data (tracks/clips/notes/plugin assignments), translated via a GPT-5
  CFG-constrained-generation step into a custom internal DSL. **BYOAK (bring your own API
  key)**, and currently **OpenAI-only** — not provider-agnostic yet.
- **The decisive new fact: MAGDA ships a Faust device, and community coverage says its AI panel
  vibe-codes Faust from a prompt.** Gearspace coverage (two independent search passes, accessed
  2026-10-12, near-identical wording both times): *"a new Faust device where you can write your
  own .dsp inside the app, or vibe-code it via the device's AI panel — describe the effect you
  want, the model writes the FAUST and the device loads it."* The same source adds it currently
  **runs through the Faust interpreter, not a JIT** ("CPU is much higher than for the built-in
  devices... a JIT compiler is planned for the next release"). **Caveat, stated plainly: I could
  not confirm this from MAGDA's own docs.** A direct fetch of the live
  `devices/faust-instrument/` manual page (accessed 2026-10-12) describes only manual
  `.dsp`-file loading and in-app code editing, with no AI-panel mention on that specific page.
  I did not find a `CHANGELOG` entry or release note that names the feature explicitly. So: **two
  independent secondary sources agree on the AI-panel claim, one primary doc page I could reach
  doesn't corroborate it (it may simply be undocumented, newer than that page, or described
  elsewhere in the manual I didn't locate) — treat as likely true but not Tier-2 primary-sourced.**
- **Why this matters more than anything else in this refresh:** if the AI-panel claim holds,
  **MAGDA already combines natural-language DAW control and natural-language DSP/plugin
  generation in one tool** — using **Faust**, the same engine PluginForge is built on. That
  falsifies the premise that "nobody does both," and it does it with an open-source, free,
  Linux-native tool, which is PluginForge's own stated beachhead. See Threats #9 and Moat #4
  below. Our one remaining lever against it: MAGDA's Faust path is interpreted, not JIT'd — if
  we actually ship the listening pass and the JIT path is meaningfully faster/more real-time-safe
  in practice, that is a concrete, measurable, currently-true technical edge, not a marketing claim.

### Sonorie — a second SaaS entrant, structurally like pluginmaker.ai *(new since 2026-08-06)*
- Launched 2026-09-08 (weraveyou.com coverage, accessed 2026-10-12). **$19.99/mo subscription,
  no free tier** — a user cannot evaluate output quality before paying, which the covering press
  itself flags as a real adoption friction ("recommending producers hear results before
  purchasing").
- Generates **real C++ DSP**, not a DSL, via an in-house model family ("**S1**" for complex
  builds, "**S1 Mini**" for simpler ones) — compiled to WebAssembly and previewed live in the
  browser before export. Exports genuine **VST3/AU/CLAP**, Win/Mac/**Linux**.
- **First sign anyone is probing the "no reliability data" flank**: Sonorie's marketing claims
  results are **"measured against real hardware behaviour"** — but publishes no methodology, no
  number, no benchmark table. This is a claim of rigor, not evidence of it. See the Decisive
  Question section below.

### Chaos Audio "AI FX Builder" — a tenth production system, flagged for triage *(new since 2026-10-12)*
Not independently researched beyond confirming it exists and is live — flagged here rather
than fully profiled, per R9 §B3's note that this doc's maintainers should triage it
separately.
- Found via GRAME's own Faust-and-LLM guidance page, which documents **nine** production
  systems already combining LLMs with Faust; Chaos Audio's "AI FX Builder" is one of them.
  Confirmed live and generally available via Chaos Audio's own announcement:
  [chaosaudio.com — Introducing AI FX Builder](https://chaosaudio.com/blogs/whats-new/introducing-ai-fx-builder-create-any-effect-you-can-imagine)
  (accessed 2026-10-14).
- **Not yet established:** pricing, delivery mechanism (plugin vs. browser vs. standalone),
  engine/formats, or whether it discloses AI use. The table row above is marked
  "undisclosed" throughout rather than guessed.
- Also surfaced in the same GRAME page: "pluginmaker.ai" (already tracked above) and "Radium
  V8.0" (unverified, not independently checked, not added to the table).

### Hügelton Instruments — a positioning model, not a prompt-to-plugin competitor *(new since 2026-08-06)*
Found via Synth Anatomy's broader October 2026 disclosure-norms piece. Hügelton is **not** a
natural-language-to-plugin tool — it's a conventional plugin developer (SynBe, Nikolator,
Felucca) who **discloses AI coding-assistance use and pairs it with human-made UI, sound design,
and graphical assets.** Worth tracking as a *positioning* reference, not a threat:
- Synth Anatomy, on SynBe (accessed 2026-10-12): *"He disclosed on the website that he used AI
  tools (Claude...) to develop the plugin. However, humans created the UI, sound design, and all
  graphical assets."* The author's take, replying to a "vibe plugin" critic: *"The developer is
  honest and says it's coded with AI. Most developers don't say it and ask for $50. I prefer
  this! And he created a custom UI that doesn't look like vibe-coding slop."*
- The same disclosure pattern repeats on Nikolator (Claude + Codex for coding/testing, human
  sound/visual design) per Synth Anatomy (accessed 2026-10-12).
- **The lesson for us:** the press reward isn't "no AI" vs. "AI" — it's **disclosed AI +
  demonstrably human-finished output** vs. undisclosed, generic-looking "vibe-coding slop." This
  is direct supporting evidence for keeping our auto-UI investment (Threat #2/#6 below) — press
  coverage explicitly rewards a non-generic UI even when the DSP code is AI-written and
  AI-written-ness is disclosed.
- Synth Anatomy's companion piece (2026-10, accessed 2026-10-12) quantifies the flood this doc
  keeps referencing: *"On average, I estimate 20-30 new plugins or apps enter the music tech
  market from newcomers every day"* and states intent to keep flagging undisclosed AI use —
  disclosure norms are visibly becoming a press differentiator, not just an ethics footnote.

---

## Decisive question — has anyone started publishing systematic reliability data? *(answered 2026-10-12)*

**Short answer: no. The flank is still open, but it is being probed for the first time.**

Searches run (accessed 2026-10-12), each returning no on-point result for an audio-DSP-specific
tool: `AI DSP plugin generator compile success rate benchmark reliability published`; `Amorph
pluginmaker ConjureDSP "compile success rate" OR "sound quality score" OR reliability data
published`. The second search's own synthesis is explicit: *"the specific metrics you're looking
for—compile success rate, sound quality score, or published reliability data—are not available
in the search results... Neither tool appears to have published formal reliability data, compile
success rates, or standardized sound quality scores in publicly available sources."*

What *does* exist, and should not be mistaken for the same thing:
- **Generic (non-audio) LLM-codegen benchmarks exist and are maturing fast** — smart-contract
  generation at ~86.5% compile success (SmartEval), GPU kernels 60.9-100%, frontend code
  87.1-96%, C++ 90-92% vs. 98.2% for human code, multi-step pipelines 84-100% (various arXiv
  papers, accessed 2026-10-12). This proves the *practice* of publishing such numbers is now
  normal in adjacent fields — it has simply not crossed over into audio DSP / prompt-to-plugin
  tools yet.
- **Sonorie's "measured against real hardware behaviour"** (launched 2026-09-08) is the closest
  thing to a reliability *claim* found anywhere in this category — and it is marketing copy with
  no published number or methodology behind it, not systematic data.
- **One anecdotal forum-style data point for Amorph**, found via search (accessed 2026-10-12,
  not independently re-verified against the original post): a user reported building "10 patches
  for Audio FX and MIDI FX, with 5 usable ones" — an informal 50%-ish personal hit rate, not a
  company-published number, and a sample size of one person's one session.
- Nothing found for ConjureDSP, APC, MAGDA, or ChatDSP resembling even an informal number.

**Conclusion for PluginForge's moat #2:** it still holds — no competitor has shipped a published
compile-success-rate, sound-quality, or correctness benchmark for AI-generated DSP. But Sonorie's
unsupported claim is a signal the *rhetoric* of reliability is starting to appear in this market's
marketing even before the *evidence* does. Whoever publishes a real, methodology-backed number
first — even a modest one — gets to define what "reliable" means in this category. That window is
still open as of 2026-10-12, but it is not guaranteed to stay open long, given how fast adjacent
fields (smart contracts, GPU kernels) went from "nobody measures this" to "everybody has a
benchmark."

---

## Threats — where a competitor already beats PluginForge

1. **Amorph ships; we don't.** They have a public build people are making plugins with. Our
   `main` still has never been heard by a human and has never loaded in a DAW (Broken #2).
   *Every day of internal architecture polish is a day Amorph compounds a user base
   and a patch library.*
2. **Auto-UI is table stakes — our parity is functional, not beautiful.** Amorph's headline
   feature — "knobs for every parameter" — is now met: all 64 pool slots render in a
   scrollable grid (`PluginEditor.cpp:256-262`). But the layout is Faust's own knob order
   (PF-038), the UiIr sectioned layout is still dead code, and there is no design tab. The
   parity gap is closed; the polish race is not won.
3. **They have a distribution/monetization story; we have none.** Amorph "The Hub" and
   pluginmaker's marketplace both create a patch-sharing network effect. A lone great generator
   with no sharing layer loses to a good-enough one with a community.
4. **The external-LLM model dodges the cost problem that is actively hurting us — and it just
   got a second, tighter implementation.** Amorph and ConjureDSP both spend $0 on inference; the
   *user's* subscription or API key pays. We fight free-tier quotas (Gemini ~20/day, per
   STATUS.md) and gate Anthropic behind paid. **Updated 2026-10-12:** this is no longer "external
   LLM via copy-paste" for either of them. Amorph 1.0 beta ships an in-plugin assistant on the
   user's own API key *plus* MCP connections for Claude Code/Cursor/VS Code; ConjureDSP was
   MCP-native from the start (an embedded in-process MCP server with roughly a dozen tools —
   `compile_and_run`, `set_parameter`, `get_error`, etc. — see "The field" above). Both now give
   an external agent **live, tool-based control of a running patch** — compile, read the error,
   edit, retry — which is close to our closed-loop UX, funded by someone else's subscription. At
   any real usage, our integrated-LLM design is still a **recurring cost liability**; theirs is
   free forever and now nearly as smooth to iterate with.
5. **pluginmaker's in-browser test/preview loop beats our (young) iterate loop.** State
   persistence (`PluginProcessor.cpp:608`) and refine-with-prior-source (`3a94080`/`5090b55`)
   have landed, but refine is still a single crude toggle — no guided clarification loop, no
   surgical-add mode (Track 2). They already let you audition and re-roll before download.
   **Sonorie (new, 2026-09-08) is a second entrant in this exact lane** — browser WASM preview
   before VST3/AU/CLAP export — but with no free tier ($19.99/mo), which is a real adoption
   friction the covering press called out.
6. **"Beautiful UIs" is a stated competitor strength and our weakest lane.** ParamGridPanel is
   built and styled (`ForgeLookAndFeel.h`), but the generated layout is functional, not
   designed — Faust's own knob order, one hardcoded theme, no design-tab presets (Track 3). The
   Amorph reviewer still called auto-graphics the hard part. Whoever makes the generated UI
   not-embarrassing wins a real preference. **Hügelton Instruments (new find, 2026-10-12) shows
   the press reward isn't "no AI," it's "disclosed AI + a UI that doesn't look like vibe-coding
   slop"** — direct supporting evidence this lane is worth the investment even without hiding
   that generation is AI-assisted.
7. **ConjureDSP's MCP server is a second data point that "the LLM lives outside the plugin" is
   becoming the default shape of this market, not Amorph's idiosyncrasy.** Two of our three
   original comparators (Amorph, ChatDSP) already used an external LLM; ConjureDSP is a third,
   and it's the cleanest implementation yet (in-process MCP, not copy-paste or a bolted-on
   panel). If BYO-LLM is where the market is converging, our integrated-LLM default increasingly
   looks like the outlier, not the differentiator — see Moat #1's caveat below and the
   already-authorized BYO-LLM hedge (`docs/byo_llm_plan.md`).
8. **Audio Plugin Coder (APC) attacks Moat #1 directly by making "self-correcting generation"
   something any agent can do against a free, open skeleton.** APC bundles no model and sells no
   generation quality — it just gets a JUCE 9 project and a 5-phase workflow in front of whatever
   agent (Claude Code, Cursor, Antigravity, Kilo, Codex) the user already has. 330 stars / 53
   forks (verified 2026-10-12) is real adoption for a framework this young. If "point a good
   agent at an open skeleton" turns out to be good enough, owning a closed self-correcting loop
   stops being a moat — it becomes one implementation choice among several, and not obviously
   the best-funded one (APC's "model quality" is whatever Claude Opus or GPT-5 ships next; ours
   is whatever we can afford to call).
9. **MAGDA is the single biggest new threat this refresh found, and it is not really about UI or
   distribution — it's existential to the "prompt → plugin, built on Faust" thesis.** It is a
   free, open-source (GPL-3), Linux-native DAW, built on Faust — our exact DSP language — that
   (per two independent, not-fully-primary-sourced community reports, see "The field" above)
   lets a user vibe-code a Faust effect from a prompt *and* control the whole DAW (tracks, clips,
   notes, plugin assignment) in natural language, in one tool. If that holds, the premise that
   "nobody combines NL DAW control and NL plugin generation" — which is relevant to whether
   PluginForge should ever consider a DAW-ward pivot — is already false, by an open-source
   project with no business model to protect. Its Faust path is interpreted, not JIT'd, which is
   a real, current, measurable gap in our favor — but it is also the kind of gap that closes with
   one release (they've stated a JIT is planned).
10. **Sonorie's "measured against real hardware behaviour" is the first probe of Moat #2's open
    flank.** No methodology, no number — but it is marketing language gesturing at exactly the
    kind of claim we have the actual machinery (`run_benchmark.py`, `--judge`) to back with real
    data. See "Decisive question" above: the flank is still open, but someone is now standing
    near it.

---

## Moat — where PluginForge can actually win (and should aim)

1. **Closed-loop, self-correcting generation.** We feed compiler stderr back to the LLM and
   auto-retry up to 3× (CLAUDE.md). **Amorph makes the human the error handler** — copy the
   error, paste it back, hope. pluginmaker is a server black box. *A generate button that fixes
   its own compile errors is a categorically better UX than copy-paste-debug* — **if** it's
   reliable. This is our sharpest wedge. Protect and measure it.
   **Caveat added 2026-10-12, this is weaker than it was in August:** Amorph's MCP-connected
   agents and ConjureDSP's embedded MCP server both now give an *external* agent live
   compile→error→edit→retry control of a running patch (Threat #4), and **APC removes the need
   for us to own the loop at all** — it hands a good-enough agent a free, open skeleton and lets
   the agent's own self-correction do the work (Threat #8). The wedge is still real — *we* close
   the loop without the user needing any agent subscription or setup — but "closed loop" is no
   longer something only we can offer; it is increasingly something any BYO-agent setup can
   approximate.
2. **Measured reliability — own the gap nobody fills.** We already have `run_benchmark.py`,
   `run_efficacy_study.py`, a 25×5 tiered-prompt study, and an unused `--judge` semantic rubric.
   **No competitor publishes a compile-success or "sounds-like-the-words" number** — reconfirmed
   2026-10-12, see "Decisive question" above for the searches run and what was/wasn't found. If
   we publish "N% first-try compile, M% within 3 retries, K% semantic match," we occupy ground
   they've explicitly left empty. (Caveat: STATUS.md says our current baselines are *void* post
   prompt-rewrite — so this moat is only real once S4's re-run lands.) **Updated caveat
   (2026-10-12):** this flank is no longer untouched — Sonorie's unpublished "measured against
   real hardware behaviour" marketing claim shows a competitor is starting to gesture at
   reliability as a selling point, even without data. The window to be first with an actual
   number is still open, but it is not static.
3. **RT-safety we can claim and they can't.** `OutputGuard` (NaN/Inf catch, DC-block,
   soft-limit, runaway mute), TSan-verified DSP swap, denormalized real-unit params. **Amorph
   literally tells users to insert a master limiter.** "Won't blow up your monitors" is a
   marketable, defensible engineering claim.
4. **Faust's verifiable stdlib as an anti-hallucination substrate.** We ground the system prompt
   in the *installed* `/usr/share/faust/*.lib` and gate every `ns.func` reference (STATUS.md).
   Faust's large, stable, documented stdlib is a better grounding target than Cmajor's smaller
   ecosystem or an opaque "Lego block" library. Correctness-by-construction is a real technical
   edge — *if* we keep measuring it.
   **Caveat added 2026-10-12, and it's a real one: we are no longer the only prompt-to-DSP tool
   grounded in Faust.** MAGDA's Faust device (reportedly) vibe-codes Faust from a prompt too
   (Threat #9) — so "Faust's stdlib is a better grounding target than Cmajor" no longer
   distinguishes us from every competitor, only from Amorph specifically. What's left of this
   moat once MAGDA is accounted for: we JIT-compile (MAGDA currently interprets, by their own
   stated roadmap), we're an integrated single-plugin product rather than a whole DAW install,
   and we have the RT-safety net (Moat #3) MAGDA's docs don't mention having. Those are real,
   current, *narrower* edges — not "we're the only one who thought of grounding in Faust."

---

## Recommendations (mapped to existing lanes — no scope grab)

**Positioning (overseer / human):**
- Stop leading with the concept ("prompt → plugin"); Amorph owns that in the press already.
  **Lead with the guarantees:** *self-correcting* (fixes its own errors), *safe* (won't damage
  output), *measured* (published reliability). That's the three-legged story competitors can't
  match today.

**Product priorities — a competitive re-rank of the existing backlog:**
- **P0 — Ship something listenable (P6 battery).** We cannot make *any* "sounds like the words"
  claim, or credibly compete, until one generated plugin has been heard. Competitors are
  shipping; our biggest risk is polishing internals while unheard. (S4 script + human ears; the
  EFFECTS listening pass sits in STATUS.md "Waiting on you" — this doc argues it's #1.)
- **P0 — Make the auto-UI beautiful, not just complete.** All 64 params now render scrollably
  (parity), but the layout is Faust's own knob order (PF-038), UiIr's sectioned layout is still
  dead code, and there is one hardcoded theme. Track 1 (two-panel + UiIr) and Track 3 (presets)
  are where the "beautiful UIs" parity gap actually closes.
- **P1 — Real refine architecture.** State persistence and refine-with-prior-source have
  landed; what remains is the two-mode split (surgical-add vs regenerate-with-context) and a
  guided clarification loop (Track 2). Our refine loop is moat #1 (self-correction) made
  user-visible — it's worth more than it looks on the roadmap.
- **P1 — Re-establish and then *publish* the benchmark + semantic numbers (S4 → overseer →
  human).** Turns moat #2 from a claim into evidence, into the gap the whole field left open.

**Strategic option worth a human decision (route via overseer):**
- **Consider an Amorph-style "bring-your-own-LLM" mode** — a "Copy Prompt / paste code" path
  alongside the integrated generator. It directly neutralizes Threat #4 (our inference-cost /
  quota problem), works fully offline, and costs us nothing per generation. It slightly dilutes
  moat #1 (the closed loop only closes when *we* hold the LLM), so it's a genuine trade, not a
  freebie — hence a human/overseer call, not an S-lane decision. Flagged as a cross-lane item.

**Non-goals for now (explicitly de-prioritized):**
- Marketplace / patch-sharing ("The Hub" analog). Real long-term threat (network effects), but
  premature: you can't share patches nobody has heard yet. Park behind the P6 listening pass.
- macOS/Windows/AU parity. We're Linux-first; competitors are Mac/Win. This is a real
  addressable-market gap but a large build; note it, don't chase it mid-beta.

---

## Open questions for the fleet
- **S1:** how hard is a "paste external Cmajor/Faust code, skip the LLM call" compile path,
  given `loadFaustCode()` already exists? (Feasibility of the BYO-LLM hedge.)
- **S4:** once the baseline re-run lands, can we express results as the three headline numbers
  (first-try / within-retry / semantic) so they're publishable as a competitive claim?
- **Overseer/human:** is "self-correcting + safe + measured" the positioning we want to commit
  to? *(BYO-LLM mode: **decided yes** — human-authorized 2026-07-23, FLEET req #6; plan in
  `docs/byo_llm_plan.md`, Phase 0 in build.)*

---

## Prior-survey carryover (from the P10 survey, 2026-07-20)
The point-in-time P10 survey (21 open-source JUCE/Faust repos; the source file was retired
with the fleet apparatus, `c58a281`) is folded in here as the living doc. Its findings that
still bear on strategy:
- **Auto-layout is the right UI floor.** Zero of 19 fixed-param repos used a bare
  `GenericAudioProcessorEditor`, even at 1–2 params — evidence for keeping the planned
  deterministic auto-layout (S3) as the baseline UI rather than a Generic fallback. This
  reinforces Threat #2 (auto-UI parity with Amorph is table stakes) and the "beautiful UIs"
  competitor edge under Moat.
- **A 4th UI paradigm exists** — declarative/GUI-Magic — not yet named in
  `docs/ui_design_plan.md`'s taxonomy. Relevant if we ever expose UI authoring.
Everything else in P10 was open-source-repo mechanics (build systems, param patterns) with no
competitor-strategy bearing; it was historical detail and is not carried forward here.

## Changelog (living doc)
- **2026-10-18** — Added **Chaos Audio "AI FX Builder"** to "The field" and as its own short
  section — the tenth production system named on GRAME's Faust-and-LLM guidance page,
  surfaced by R9 §B3 after the 2026-10-12 refresh pass had already closed, and explicitly
  flagged there for triage rather than left untracked. Not independently profiled beyond
  confirming it is live and generally available; pricing/delivery/formats are unknown and
  marked as such rather than guessed.
- **2026-10-12** — Refresh past the 2026-08-06 snapshot (all claims below verified/searched this
  date). **Four new competitors added to "The field":** ConjureDSP/ConjureIDE (free, open-source
  GPL-3 macOS AUv3, BYO-agent via an embedded MCP server rather than copy-paste), Audio Plugin
  Coder / APC (open-source MIT, JUCE 9, explicitly agent-agnostic framework — no bundled model,
  330★/53 forks), MAGDA (free open-source GPL-3 DAW on Faust + Tracktion Engine, NL control of
  tracks/clips/notes/plugin assignment, 16 LFOs/16 macros per device+rack — confirmed from its
  own README), and Sonorie ($19.99/mo SaaS, no free tier, launched 2026-09-08, C++→WASM,
  exports real VST3/AU/CLAP). Hügelton Instruments added as a *positioning* reference (disclosed
  AI + human-finished UI earns full press coverage), not a table row — it isn't a prompt-to-plugin
  tool. **Amorph updated:** 1.0 beta ships an in-plugin assistant on the user's own API key plus
  MCP for Claude Code/Cursor/VS Code (the copy-paste model is being phased out, not finished
  phasing out); "The Hub" shipped as "Explore" (cloud preset browsing, no upload yet); Amorph MIDI
  shipped; 40+ open-source patches; Gumroad 183 ratings/4.9★ vs. KVR's 1 review. **Decisive
  question answered explicitly** (new subsection): no competitor publishes systematic
  compile-success/sound-quality/reliability data as of this date — reconfirmed by targeted search
  — but Sonorie's unevidenced "measured against real hardware behaviour" claim is the first sign
  of the rhetoric arriving before the data. **Biggest single finding: MAGDA, per two independent
  (not fully primary-sourced) community reports, may already combine NL DAW control with NL
  Faust-DSP generation in one free, open-source, Linux-native tool** — which would falsify the
  "nobody does both" premise and directly erodes Moat #4 ("Faust's stdlib as our anti-hallucination
  substrate" no longer distinguishes us from every competitor, just from Amorph). Threats #4, #6
  amended; Threats #7-10 added (BYO-LLM convergence, APC vs. Moat #1, MAGDA, Sonorie vs. Moat #2).
  Moat #1/#2/#4 given caveats reflecting the above; Moat #3 unchanged (no competitor found who
  addresses RT-safety). Everything not re-verified here (Amorph's core architecture, pluginmaker's
  pricing structure, ChatDSP) is carried forward from 2026-08-06 unchanged — see that entry.
- **2026-08-06** — Stale claims refreshed against the current repo: the "8 of 64 param cap"
  (grid now renders all 64 slots scrollably, `PluginEditor.cpp:256-262`), "blocked on state
  persistence" (landed, `PluginProcessor.cpp:608`), "ParamGridPanel unbuilt" (built + styled,
  `ForgeLookAndFeel.h`), and refine "cannot carry source" (landed, `3a94080`/`5090b55`). Threats
  #2/#5/#6 and the P0/P1 re-rank rewritten around the actual remaining gaps (UiIr dead code,
  PF-038 knob order, Track 2/3). The one constant: **no generated plugin has been heard yet.**
- **2026-07-23** — First landed as `competitive_research.md`; renamed to canonical
  `competitive_landscape.md` and made the living successor to P10. Field mapped
  (Amorph / pluginmaker.ai / ChatDSP); BYO-LLM recommendation authorized (req #6).

---

## Sources
- MusicRadar — ["…writing code manually won't be a thing…" (AI prompt-plugins deep dive)](https://www.musicradar.com/music-tech/we-strongly-believe-that-in-the-coming-years-writing-code-manually-wont-be-a-thing-what-will-be-left-is-the-fun-stuff-inside-the-new-wave-of-ai-tools-turning-prompts-into-plugins)
- Yahoo/Tech — [Inside the new wave of AI tools turning prompts into plugins](https://tech.yahoo.com/ai/chatgpt/articles/inside-wave-ai-tools-turning-140818640.html)
- KVR — [Amorph by Artists in DSP](https://www.kvraudio.com/product/amorph-by-artists-in-dsp)
- Gearspace — [Artists in DSP announces AMORPH (early access)](https://gearspace.com/board/new-product-alert-2-older-threads/1460848-artists-dsp-announces-amorph-prompt-driven-audio-plugin-early-access.html) (403 to fetcher; via search excerpt)
- Sonicstate — [Turn Ideas Into Instruments/FX](https://sonicstate.com/news/2026/02/12/-turns-idea-into-instrumentsfx-/)
- MusicTech — [Amorph turns text prompts into instruments and effects](https://musictech.com/news/music/artists-in-dsp-amorph/)
- Amorph — [product page (Gumroad)](https://artistsindsp.gumroad.com/l/amorph) · [artistsindsp.com](https://artistsindsp.com/)
- pluginmaker.ai — [product](https://pluginmaker.ai/) · [pricing](https://pluginmaker.ai/pricing)

### Sources added 2026-10-18
- GRAME Faust LLM guidance (fetched directly) — https://faustdoc.grame.fr/manual/llm/
- Chaos Audio — [Introducing AI FX Builder](https://chaosaudio.com/blogs/whats-new/introducing-ai-fx-builder-create-any-effect-you-can-imagine)

### Sources added 2026-10-12
- Synth Anatomy — [ConjureDSP 3: vibe-code your own effects in your DAW](https://synthanatomy.com/2026/07/conjuredsp-3-vibe-code-your-own-effects-in-your-daw.html) · [Vibe coding plugins: the good, the bad, and the impact on my news coverage](https://synthanatomy.com/2026/10/vibe-coding-plugins-the-good-the-bad-and-the-impact-on-my-news-coverage.html) · [Hügelton Instruments SynBe](https://synthanatomy.com/2026/09/hugelton-instruments-synbe-a-minikorg-700-inspired-mono-synth.html)
- KVR — [ConjureDSP by ConjureDSP](https://www.kvraudio.com/product/conjuredsp-by-conjuredsp) · [Amorph reviews](https://www.kvraudio.com/product/amorph-by-artists-in-dsp/reviews)
- GitHub — [michaeljancsy/ConjureIDE](https://github.com/michaeljancsy/ConjureIDE) · [michaeljancsy/ConjureAlign](https://github.com/michaeljancsy/ConjureAlign) · [Artists-in-DSP/amorph-for-agents](https://github.com/Artists-in-DSP/amorph-for-agents) · [Noizefield/audio-plugin-coder](https://github.com/Noizefield/audio-plugin-coder) · [Conceptual-Machines/magda-core](https://github.com/Conceptual-Machines/magda-core)
- ConjureDSP — [conjuredsp.com](https://www.conjuredsp.com/) · [docs](https://www.conjuredsp.com/docs/) · [ConjureAlign](https://www.conjuredsp.com/align/)
- Bedroom Producers Blog — [ConjureAlign release](https://bedroomproducersblog.com/2026/08/26/conjuredsp-conjurealign/) · [Conceptual Machines releases MAGDA](https://bedroomproducersblog.com/2026/04/09/conceptual-machines-magda/)
- rekkerd.org — [MAGDA updated to v0.5.0](https://rekkerd.org/magda-free-open-source-daw-updated-to-v0-5-0/) · [Amorph gets custom UIs & MIDI plugin generation](https://rekkerd.org/amorph-text-to-dsp-plugin-gets-custom-user-interfaces-midi-plugin-generation/)
- MAGDA — [JUCE forum announcement](https://forum.juce.com/t/magda-open-source-daw-with-integrated-ai-powered-by-tracktion-engine/68529) · [manual](https://conceptual-machines.github.io/magda-core/) · [Faust Instrument device docs](https://conceptual-machines.github.io/magda-core/devices/faust-instrument/) · [gearspace thread](https://gearspace.com/board/new-product-alert-2-older-threads/1462315-magda-free-open-source-daw-integrated-ai.html)
- Synthtopia — [Free Tool, Amorph, Builds Custom Plugin Instruments & Effects](https://www.synthtopia.com/content/2026/09/22/free-tool-amorph-builds-custom-plugin-instruments-effects-based-on-your-description/)
- weraveyou.com — [Sonorie: AI-generated audio plugins (VST3)](https://weraveyou.com/2026/09/sonorie-ai-generated-audio-plugins-vst3/)
- giovannicordova.com — [An AI Can Drive Your DAW. It Still Cannot Hear It.](https://giovannicordova.com/blog/audio-processing-agentic-ai-era) (checked for the "does anyone combine NL DAW control + NL plugin generation" question; discusses AbletonMCP and AI mastering as separate capabilities, does not identify a combined tool)
- arXiv (generic LLM-codegen reliability benchmarks, cited only to show the practice exists outside audio DSP) — SmartEval (`2605.09610`), AscendCraft (`2601.22760`), "Are LLM-Generated GPU Kernels Production-Ready?" (`2607.14541`), PlanCompiler (`2604.13092`)

All URLs above accessed 2026-10-12 via WebSearch/WebFetch during this refresh; several (noted
inline above) returned results via a fetch-and-summarize pass rather than verbatim page text —
where two independent fetches of the same claim disagreed (ConjureDSP's MCP tool count; MAGDA's
Faust-device AI panel), the disagreement is flagged in the relevant section above rather than
silently resolved.
