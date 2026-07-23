# Competitive Landscape — Prompt-to-Plugin (living doc)

**Lane:** S7 Competitive Research — **read-only / advisory** (FLEET.md roster). S7 advises, never
edits code or other lanes' files; it routes findings to owning lanes via the FLEET cross-lane log
and the Advisory feed. **This is the single canonical *living* competitive-intelligence doc** —
updated as the field moves, not a point-in-time snapshot.

**Supersedes / absorbs:** `docs/juce_plugin_survey.md` (P10, 2026-07-20) is the point-in-time,
read-only predecessor — a one-shot survey of 21 open-source JUCE/Faust repos. This doc is its
living successor. P10's still-relevant headline is carried forward in "Prior-survey carryover"
below; the P10 file is left intact (not S7's to edit) as the historical record.

**First landed:** 2026-07-23. **Adversarial stance:** the job of this doc is to find where
competitors already beat us and where our thesis is *not* actually differentiated — not to
reassure. Read it as a threat model.

---

## TL;DR — the uncomfortable part

**Our core thesis is no longer novel, and at least one competitor has already shipped it.**
"Natural language → DSP → a compiled plugin that runs in your DAW with an auto-generated UI"
is exactly **Amorph** (Artists in DSP), which is in public open beta *today*, free, VST3/AU,
Mac+Win — and it auto-generates a knob for **every** parameter. We currently cap the UI at
**8 of 64** params (STATUS.md Broken #2) and **have never listened to a single generated
plugin** (STATUS.md "Assumed"). On the two things a user first sees — "does it make sound"
and "can I turn all the knobs" — the shipping competitor is ahead of us.

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

### pluginmaker.ai — the SaaS play
- **Browser-based, server-side generation.** No local compile; you get a downloadable binary.
- **In-browser instant test loop**: on-screen keyboard + sequencer, preview before you commit —
  a tight iterate/refine UX we do **not** have (ours is blocked on state persistence).
- Users praised for **"beautiful graphical user interfaces"** vs other vibe-coded plugins.
- **Pricing (verified 2026-07-23):** Free $0 / 50 one-time credits · Pro **$30/mo** / 3,300
  credits + "Pro mode" quality · Studio **$100/mo** / 12,000 credits · Enterprise custom
  (anti-piracy licensing, custom installer, cross-DAW optimization, Shopify). Top-ups **$1 /
  100 credits**, never expire. A generation costs **~50 credits (fast model) to 200+
  (state-of-the-art)**.
- Co-founder Dominik Bilski: *"in the coming years, writing code manually won't be a thing.
  What will be left is the fun stuff."* Explicitly targets **bedroom producers** priced out of
  boutique plugins.

### The gap nobody fills
The MusicRadar/Yahoo coverage is explicit: **"no compile success rates, sound quality
assessments, or systematic reliability data"** exist for any of these tools. Every competitor
sells the dream; **none publishes whether the output actually works or sounds right.** That is
an open flank — and it happens to be the exact thing our bench/efficacy/`--judge` machinery was
built to measure.

---

## Threats — where a competitor already beats PluginForge

1. **Amorph ships; we don't.** They have a public build people are making plugins with. Our
   `main` cannot yet persist a patch (Broken #1), shows 8/64 knobs (Broken #2), and has never
   been heard. *Every day of internal architecture polish is a day Amorph compounds a user base
   and a patch library.*
2. **Auto-UI for all params is table stakes we haven't met.** Amorph's headline feature —
   "knobs for every parameter" — is our open bug. This is S3's Wave-1 work; it is now
   **competitive parity, not a nice-to-have.**
3. **They have a distribution/monetization story; we have none.** Amorph "The Hub" and
   pluginmaker's marketplace both create a patch-sharing network effect. A lone great generator
   with no sharing layer loses to a good-enough one with a community.
4. **Amorph's external-LLM model dodges the cost problem that is actively hurting us.** They
   spend $0 on inference — the *user's* ChatGPT/Claude subscription pays. We fight free-tier
   quotas (Gemini ~20/day, per STATUS.md) and gate Anthropic behind paid. At any real usage,
   our integrated-LLM design is a **recurring cost liability**; theirs is free forever. This is
   a strategic weakness, not just an ops detail.
5. **pluginmaker's in-browser test/preview loop beats our (nonexistent) iterate loop.** Our
   refine/iterate UX is blocked behind state persistence (Broken #1 → S1). They already let you
   audition and re-roll before download.
6. **"Beautiful UIs" is a stated competitor strength and our weakest lane.** ParamGridPanel is
   unbuilt; the Amorph reviewer still called auto-graphics the hard part. Whoever makes the
   generated UI not-embarrassing wins a real preference.

---

## Moat — where PluginForge can actually win (and should aim)

1. **Closed-loop, self-correcting generation.** We feed compiler stderr back to the LLM and
   auto-retry up to 3× (CLAUDE.md). **Amorph makes the human the error handler** — copy the
   error, paste it back, hope. pluginmaker is a server black box. *A generate button that fixes
   its own compile errors is a categorically better UX than copy-paste-debug* — **if** it's
   reliable. This is our sharpest wedge. Protect and measure it.
2. **Measured reliability — own the gap nobody fills.** We already have `run_benchmark.py`,
   `run_efficacy_study.py`, a 25×5 tiered-prompt study, and an unused `--judge` semantic rubric.
   **No competitor publishes a compile-success or "sounds-like-the-words" number.** If we
   publish "N% first-try compile, M% within 3 retries, K% semantic match," we occupy ground
   they've explicitly left empty. (Caveat: STATUS.md says our current baselines are *void* post
   prompt-rewrite — so this moat is only real once S4's re-run lands.)
3. **RT-safety we can claim and they can't.** `OutputGuard` (NaN/Inf catch, DC-block,
   soft-limit, runaway mute), TSan-verified DSP swap, denormalized real-unit params. **Amorph
   literally tells users to insert a master limiter.** "Won't blow up your monitors" is a
   marketable, defensible engineering claim.
4. **Faust's verifiable stdlib as an anti-hallucination substrate.** We ground the system prompt
   in the *installed* `/usr/share/faust/*.lib` and gate every `ns.func` reference (STATUS.md).
   Faust's large, stable, documented stdlib is a better grounding target than Cmajor's smaller
   ecosystem or an opaque "Lego block" library. Correctness-by-construction is a real technical
   edge — *if* we keep measuring it.

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
  shipping; our biggest risk is polishing internals while unheard. (S4 script + human ears; in
  STATUS.md "Next three," already #2 — this doc argues it's #1.)
- **P0 — Lift the 8→64 param auto-UI (S3, Wave-1).** This is now parity with Amorph's headline
  feature, not an enhancement.
- **P1 — Land state persistence (S1, Broken #1).** It unblocks the iterate/refine loop that
  pluginmaker already ships. Our refine loop is our moat #1 (self-correction) made
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
  premature: you can't share patches you can't yet persist or hear. Park behind persistence + P6.
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

## Prior-survey carryover (from P10, `docs/juce_plugin_survey.md`)
The point-in-time P10 survey (21 open-source JUCE/Faust repos, 2026-07-20) is folded in here as
the living doc. Its findings that still bear on strategy:
- **Auto-layout is the right UI floor.** Zero of 19 fixed-param repos used a bare
  `GenericAudioProcessorEditor`, even at 1–2 params — evidence for keeping the planned
  deterministic auto-layout (S3) as the baseline UI rather than a Generic fallback. This
  reinforces Threat #2 (auto-UI parity with Amorph is table stakes) and the "beautiful UIs"
  competitor edge under Moat.
- **A 4th UI paradigm exists** — declarative/GUI-Magic — not yet named in
  `docs/ui_design_plan.md`'s taxonomy. Relevant if we ever expose UI authoring.
Everything else in P10 was open-source-repo mechanics (build systems, param patterns) with no
competitor-strategy bearing; it stays in the P10 file as historical detail.

## Changelog (living doc)
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
