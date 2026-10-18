# R6 — Visibility Ledger: How PluginForge/Incant Audio Gets Noticed (Oct 2026)

**Point-in-time research note, accessed 2026-10-12.** Read-only research; no code or product
claims changed by writing this. Follows the `docs/research/` convention (R1-R5):
every external claim below is cited with a URL I fetched or searched today; every internal
("what PluginForge can honestly claim") claim is grounded in the repo (`CLAUDE.md`,
`STATUS.md`, `docs/competitive_landscape.md`, `docs/BUGS.md`, the public
`github.com/Losera/incant-audio` repo). Unknowns are marked `UNKNOWN` rather than guessed.

**Scope correction up front, because it changes every row below:** the project is not
starting from zero visibility. `github.com/Losera/incant-audio` is already a **public**
repo (confirmed via GitHub fetch today) — but its `LICENSE` reads **"Proprietary; all
rights reserved."** That combination — *visible* source, *closed* license — is the single
fact every gatekeeper below cares about, and it currently fails the "open-source" bar most
of them now apply. It is 1 star / 0 forks / 0 watchers as of today — i.e., discoverable but
not yet discovered.

Also not starting from a quiet field: `docs/competitive_landscape.md` (this repo, last
updated 2026-08-06) documents that **Amorph** (Artists in DSP — Cmajor JIT, in-DAW,
free, open beta) already has press coverage at MusicRadar, KVR, Gearspace, Sonicstate,
and MusicTech for *the same pitch* ("prompt → compiled plugin → auto-UI"), and that
**Audio Plugin Coder (APC)** by Max Pfetscher — JUCE 8, open source, GitHub — got
promotional coverage at both Attack Magazine and rekkerd.org in February 2026
([Attack](https://www.attackmagazine.com/news/you-can-now-build-your-own-plugins-without-c-knowledge/),
[rekkerd](https://rekkerd.org/audio-plugin-coder-open-source-tool-brings-ai-powered-vst3-development-to-music-producers/),
accessed 2026-10-12). APC is the closest apples-to-apples precedent: same category, same
era, and it is the one that got in because it is open source. This is direct evidence for
the research question below, not inference.

---

## 1. The Synth Anatomy policy shift — what it actually says and what it doesn't

Source: [Synth Anatomy, "Vibe coding plugins: the good, the bad, and the impact on my news
coverage"](https://synthanatomy.com/2026/10/vibe-coding-plugins-the-good-the-bad-and-the-impact-on-my-news-coverage.html),
published 2026-10-12, byline "Tom," accessed 2026-10-12.

Important nuance the task brief should be corrected on: **the article does not publish a
numbered 5-point checklist.** Fetching the live page today found a looser policy than the
"(1)...(5)" framing implies:

- Synth Anatomy will keep labeling anything the author suspects is vibe-coded in running
  coverage ("I will continue to mention in articles whenever I feel something is
  'vibe-coded.' That is fair to the readers.").
- The explicit, actionable carve-out for full coverage is: **"If the developer can prove
  otherwise (open-source the code, etc.), they are welcome to get in touch with me."**
  Open-sourcing the code is the one concrete, named lever a developer can pull.
  Everything else (UI quality, "genuine innovation," "legitimate development
  practices") is described as impression, not a checklist item with a stated bar.
- A tiered "live blog/ticker" is planned for the volume (20-30 releases/day) that won't
  get full articles — implying most vibe-coded submissions default to *that* tier, not to
  silence.
- **Hügelton Instruments** is the cited exemplar, for a disclosure pattern that is worth
  copying verbatim. Their own public language (confirmed on the
  [Felucca GitHub repo](https://github.com/hugelton/Felucca) and covered at
  [Synth Anatomy](https://synthanatomy.com/2026/10/hugelton-instruments-felucca-custom-m-vave-fm-1-firmware-turns-it-into-a-multi-engine-synth.html),
  both accessed 2026-10-12): *"Felucca is developed with the assistance of AI coding
  agents. These tools are used for coding, testing, documentation, translation, and
  maintenance. The instrument's design, features, sound design, and overall direction
  are determined by the maintainer or community. No generative AI is used to create
  music, icons, or visual artwork for this project."* That sentence structure — what AI
  did, what AI explicitly did not do, who made the design calls — is the template.

**Implication for PluginForge:** the one lever Synth Anatomy actually names (open-source
the code) is one PluginForge is *structurally* closer to pulling than most vibe-coded
competitors, because the repo is already public — it is a license change, not a
from-scratch open-sourcing effort, and not an architecture decision in the ADR sense,
though it is still a decision this document flags rather than makes (see COLLABORATION.md
§2.3/§4 — a licensing change is consult-trigger territory, not something this research
note is authorized to recommend unilaterally). The Hügelton disclosure template, by
contrast, PluginForge could write today with zero code changes, because `CLAUDE.md`
already states the true division of labor (LLM writes Faust/C++, architecture and
UI decisions are human-gated by COLLABORATION.md) — that's a true sentence waiting to be
published, not a claim that needs new engineering.

---

## 2. Channel ledger

| Channel | Gatekeeper(s) | What they require | What PluginForge can honestly claim **today** | Gap | Effort |
|---|---|---|---|---|---|
| **Synth Anatomy** | Solo editor ("Tom") | Disclosure of AI use in running coverage; full-article treatment reserved for provable exceptions, open-sourcing the code being the one named lever ([source](https://synthanatomy.com/2026/10/vibe-coding-plugins-the-good-the-bad-and-the-impact-on-my-news-coverage.html), 2026-10-12) | Linux-only, Faust/JIT architecture is genuinely distinct from the Mac/Win Cmajor and cloud-SaaS competitors already covered; a documented self-correction history (PF-076 withdrawal, the arm-A overclaim correction) is real and citable | License is proprietary, not open-source — fails the one concrete criterion named; no Hügelton-style disclosure sentence published anywhere user-facing yet | **Low** to get labeled fairly (write the disclosure sentence); **Med** to clear the full-coverage bar (needs an actual license decision, human-gated) |
| **CDM (Create Digital Music)** | Peter Kirn, founder/editor, still active in 2026 ([cdm.link](https://cdm.link/), [author page](https://cdm.link/author/peter/), accessed 2026-10-12) | UNKNOWN explicit AI-disclosure policy — no policy page found in this research pass; CDM has historically covered experimental/DIY/open tooling favorably | Open-source-leaning story, Linux-first, deep technical writeup culture (ADRs, BUGS.md) fits CDM's traditional "process over product" bent | No direct contact made; no submission guideline located | Low (a cold-pitch email with the technical writeup attached) |
| **Bedroom Producers Blog** | Tomislav Zlatic, founder/Editor-in-Chief | No published formal submission process found; "How We Test" page only offers a general contact form ([bedroomproducersblog.com/how-we-test](https://bedroomproducersblog.com/how-we-test/), accessed 2026-10-12). Zlatic has publicly and specifically criticized "zero-effort vibe-coded plugin[s]...pushed out as a finished product," per [BPB's own April 2026 piece](https://bedroomproducersblog.com/2026/04/23/vibe-coding-synthedit/) | A measured, failure-documenting engineering culture is the opposite of "zero effort" — this is the single best-matched outlet for the "rigor, not polish" pitch | Needs a cold pitch; BPB's bar (what counts as "effort") is undocumented, so the pitch is a bet, not a known gate | Low |
| **rekkerd.org** | UNKNOWN named editor; news-aggregation model | No explicit AI-disclosure policy found; rekkerd ran a straightforwardly promotional writeup of APC (open-source, JUCE 8) in Feb 2026 ([rekkerd.org/audio-plugin-coder…](https://rekkerd.org/audio-plugin-coder-open-source-tool-brings-ai-powered-vst3-development-to-music-producers/), accessed 2026-10-12) with **no critical framing at all** | Same category as APC's successful pitch; APC is the direct precedent | APC's coverage leaned on "open-source tool," stated plainly in the headline — PluginForge cannot currently use that exact hook | Low once/if license changes; otherwise Med (pitch the Linux/JIT angle instead) |
| **Attack Magazine** | Editorial desk, `attack@attackmagazine.com` | Pitch via email with an outline/CV/writing sample for bylined pieces; **news items** (like the APC piece) appear to run off vendor pitches without a formal track; "no press releases or sales pitches" is the stated policy for the *bylined-article* track specifically ([Write for Attack](https://www.attackmagazine.com/news/write-for-attack/), accessed 2026-10-12) | A technical writeup (the self-correction story, the JIT architecture) is exactly the kind of "original idea" piece they solicit, distinct from a press release | No contact made | Low |
| **MusicTech** | News desk, `editors@musictech.com` for tips ([Contact](https://musictech.com/contact/), accessed 2026-10-12) | UNKNOWN formal gate; already covered Amorph positively, so the category is pre-approved editorially | Direct competitor already covered there — a cold pitch is at minimum not category-rejected | No contact made | Low |
| **KVR Audio — News section** | KVR Admin team; editorial approval required before posting ([KVR Submissions](https://www.kvraudio.com/submissions), accessed 2026-10-12) | Product Database listing + press-style info, or email `contactus@kvraudio.com` with details/images; subject to admin approval, no stated AI-disclosure requirement | Can list today — Developer Account signup is free and self-serve | Not yet listed in KVR's Product Database | Low |
| **KVR Audio — Forum** | Community moderators; no formal gate, but strong community *norm* of disclosure | The "If Your Plugin is Vibe Coded Please Divulge That Fact" thread ([kvraudio.com/forum/viewtopic.php?t=633778](https://www.kvraudio.com/forum/viewtopic.php?t=633778), accessed 2026-10-12) shows community sentiment is pro-disclosure but **there is no moderator-enforced rule** — it's social pressure, not a gate. One commenter's heuristic stuck out as directly actionable: "check out a developer's history and contributions on Github" | A real, inspectable GitHub history (even under a proprietary license) and a documented engineering process (ADRs, BUGS.md, self-corrections) directly answers that specific community scrutiny pattern | Nothing published *for that audience* — the repo exists but no forum post points to it | Low |
| **Gearspace forum (New Product Alert)** | Forum staff ("Gearspace Press Desk") for official pickup; moderators otherwise | Official coverage: email with images/links to the Press Desk. Self-posting: must disclose any brand affiliation; "shill posting" (undisclosed commercial interest) is bannable ([NPA posting guidelines](https://gearspace.com/threads/new-products-alert-coming-soon-posting-guidelines.1005628/), accessed 2026-10-12) | As the sole/primary developer, posting under one's own account with the AI-use facts stated upfront satisfies the disclosure norm directly | No post made yet | Low |
| **Sonicstate** | Small editorial team; actively opinionated on this exact topic — see their Oct 2026 piece ["A Storm Coming to our Plugin Market?"](https://sonicstate.com/news/2026/10/04/the-storm-coming-to-our-plugin-market/) (accessed 2026-10-12), which says the author is personally "attempting to abstain from vibe-coded music software and focus instead on hand-coded DSP instruments" | UNKNOWN formal submission process; the piece above suggests real skepticism toward pure vibe-coded tools specifically, which cuts against a naive pitch | The "hand-written Faust DSL + JIT, not black-box vibe-coded C++" framing is closer to "hand-coded DSP" than most competitors' pitch, and is true — the LLM emits a readable DSL, not opaque C++, and the Faust source is user-inspectable in the UI (per `README.md`'s "Live Iteration & Code Inspector" feature) | This nuance (DSL-mediated vs. raw-code vibe-coding) isn't articulated anywhere publicly yet | Low (write the distinction up once, pitch it) |
| **r/linuxaudio** | Volunteer subreddit moderators | Could not locate an explicit self-promotion rule for this subreddit in this research pass (searches returned general Linux-audio background, not subreddit rules) — **treat the exact self-promo threshold as UNKNOWN** until checked against the live sidebar/wiki at post time | Linux-only, Faust-based — this is the one community where "Linux-first" is a *selling point* rather than a limitation | Rule text unverified; general Reddit norms (contribute before promoting, no link-only drive-bys) likely apply | Low |
| **Linux Audio community (LAD/LAA mailing lists, LSP norms)** | List moderators; strong technical-only culture | LAD (linux-audio-dev) is for development discussion, "refrain from any personal references and focus on technical aspects"; **new project announcements belong on LAA (Linux Audio Announce), which is moderated**, not LAD ([linuxdj.com LAD/LAA pages](https://www.linuxdj.com/audio/lad/subscribelaa/), accessed 2026-10-12) | A Faust-based, libfaust/LLVM-JIT architecture is squarely on-topic and technically legible to this audience without any marketing translation needed | No announcement sent; wrong-list posting (LAD instead of LAA) would read as a norm violation to this community | Low |
| **r/WeAreTheMusicMakers** | Subreddit mods, ~1.7M members | Self-promotion largely confined to a designated feedback/promo thread; community strongly prefers process-sharing ("a plugin workflow, a mixing breakdown") over "dropping a generic link" ([general community-norm sources], accessed 2026-10-12 — subreddit's own current wiki text not independently verified, flagged UNKNOWN) | A workflow/process post ("how the self-correcting repair loop works," not "try my plugin") fits this norm exactly | No post made; exact current wiki rule text not directly confirmed | Low |
| **r/edmproduction** | Subreddit mods | Could not locate the current rules/wiki text in this research pass — **self-promotion policy is UNKNOWN**, general Reddit self-promo norms (contribute first, avoid pure link drops) assumed by analogy only | Lower fit generally — this is a producer-consumer audience, not a developer/academic one; weakest match of the communities surveyed for this project's actual goals (job search, DSP peers) | Policy unverified; also lowest alignment with stated goals | Low effort, **questionable value** — deprioritize |
| **Gearspace, KVR forum, r/WeAreTheMusicMakers — summary** | — | — | — | — | — |
| **Hacker News (Show HN)** | Community moderation (dang et al.), no formal gate beyond HN guidelines | See §3 below — pattern-matched from real 2025-2026 examples: a **working demo** (video or live link), **technical depth in the post itself** (not marketing copy), and **honest/measured claims** are what separates hits from pile-ons; open source is a strong plus but not strictly required (Anukari is proprietary and did very well) | A real end-to-end demo (QWERTY→heard notes, per `STATUS.md` session 017), a genuinely technical and unusual architecture (JIT-compiled Faust inside a live VST3, self-correcting repair loop with a documented, *falsified-and-corrected* mechanism), and an honest "here's what doesn't work yet" framing are an unusually strong match for what HN rewards | No post made; "a human has heard it" is true for at least two generated patches per session logs, but a public-facing demo video does not yet exist | **Med** — needs one polished demo video/GIF before posting, otherwise technical content is already written (BUGS.md, decisions.md) |
| **lobste.rs** | Invite-only membership; strong technical-rigor norm; visible friction over AI-generated content (see the "vibecoding" tag controversy, [lobste.rs/s/djzqbn](https://lobste.rs/s/djzqbn/can_we_fix_retire_vibecoding_tag), accessed 2026-10-12) | Needs an existing member to submit, or an invite to join; the community actively filters/resents the "vibecoding" label when applied to legitimate engineering work — i.e., **the self-correction/measurement story is exactly what this community wants to see distinguished from pejorative "vibe coding"** | A documented, adversarially-reviewed, twice-self-corrected measurement process (PF-076 withdrawal, the arm-A overclaim correction, `docs/records/`) is the single most lobste.rs-shaped artifact this project has | No lobste.rs account/invite in evidence; nothing submitted | Low once an invite exists; **membership access itself is the actual gap** |
| **Linux Audio Conference (LAC)** | Scientific/program committee, Grame-affiliated | Separate tracks: full peer-reviewed papers (4-8pp), **presentations without papers** (abstract-based, non-peer-reviewed, oral), **demos** ("informal project presentations...carried out between the papers sessions"), workshops, and music/installation submissions ([LAC 2026 CfP](https://lac26.mucs.club/), accessed 2026-10-12). LAC 2026: Maynooth University, Dublin, **18-20 June 2026**; submission deadline **2026-03-18** (already passed for 2026 — next window is the 2027 cycle) | A Faust-based project with LLM/FLOSS angle matches LAC 2026's explicit conference theme ("LLMs and FLOSS") almost exactly — this is a nearly perfect topical fit | The 2026 deadline has already passed; `docs/research/R5-publishable-run.md` already records "both 2026 venues closed... target the 2027 cycle" — this ledger confirms that conclusion independently rather than contradicting it | **Low effort, long lead time** — a demo-track submission (not a full paper) is a much lower bar than publication and should be the target for 2027 |
| **Audio Developer Conference (ADC)** | Audio.dev program committee | Full talk track (with public review phase) **and a separate, lighter poster track** ([ADC Bristol 26](https://audio.dev/adc-bristol-26/), accessed 2026-10-12). ADC Bristol 2026: Nov 9-11, 2026. Key dates found: talk submissions closed June 29, 2026; **poster submission deadline is 2026-10-12 — i.e., today** | Directly relevant technical content (JIT-compiled JUCE/Faust DSP, a measured self-correcting LLM repair loop) is squarely ADC's audience | **Talk track deadline already passed for 2026.** The poster deadline is today; whether a poster can still be submitted same-day is UNKNOWN (not verified against the live submission portal in this pass) | **Act today if at all** for 2026 poster; otherwise target ADC 2027's talk track with much longer lead time |

---

## 3. Hacker News — what actually works vs. what flops (real 2025-2026 examples)

Three concrete cases pulled directly from HN item pages today:

1. **[Show HN: I built a synth for my daughter](https://news.ycombinator.com/item?id=45900425)**
   — **1,278 points, 209 comments** (Nov 2025). Hand-built hardware synth with 3D-printed
   enclosure, step sequencer, diatonic-scale constraint. Proprietary, no open-sourcing.
   What landed: a demo video, visible from-scratch skill acquisition (PCB design, 3D
   printing, embedded code), and musically-constrained design that "can't sound bad."
   **Pattern: craft + a working demo + honest scope (a toy for one kid, not a product
   pitch) beat openness.**

2. **[Show HN: I built a synthesizer based on 3D physics (Anukari)](https://news.ycombinator.com/item?id=43873074)**
   — **512 points, 123 comments** (May 2025). GPU physics-modeling synth, proprietary,
   two-plus years of public dev-blog history. Praise centered on technical originality
   ("the most interesting synth I've ever seen") and a real downloadable demo with no
   signup wall. Criticism: the landing page needed better in-browser audio demos up
   front. **Pattern: same as above — proprietary is not disqualifying when the technical
   claim is genuinely novel and backed by a long, documented build history and an
   install-and-try demo.**

3. **[Show HN: I used AI to recreate a $4000 piece of audio hardware as a plugin](https://news.ycombinator.com/item?id=46471648)**
   — **160 points, 105 comments.** Mixed-to-critical, not a flop but a clear contrast
   case: the top criticisms were specifically about **unsubstantiated fidelity claims**
   ("100% faithful" with no frequency/time-domain testing shown) and skepticism that an
   LLM "recreation" wasn't just regurgitating training-data code. No open source, demo
   video existed but no measurement backed the headline claim. **Pattern: HN's DSP-literate
   readership punishes exactly the kind of overclaim PluginForge's own engineering culture
   already refuses to make** — this is the strongest direct evidence that "measured,
   self-correcting, no overclaiming" is a Show-HN-rewarded trait, not just an internal
   virtue.

**Synthesis across all three:** open source is a plus, not a requirement, on HN
specifically. What is consistently decisive is **(a)** a real, triable demo and **(b)**
whether the claims in the post text are backed by evidence the DSP-literate HN crowd can
check. PluginForge's actual differentiator — a documented, adversarially-reviewed, honest
measurement culture that has twice publicly corrected its own overclaims — is a better fit
for HN's actual reward function than for the consumer-plugin press, where "open-source the
code" is the blunter, more legible signal gatekeepers reach for.

---

## 4. Which channels reward open-source + engineering rigor over polished launches?

Ranked by how directly the evidence gathered above supports this:

1. **lobste.rs** — strongest fit. The community's own internal fight over the
   "vibecoding" tag shows it actively wants to separate rigorous engineering from
   marketing-flavored AI content; a twice-self-corrected measurement history is exactly
   the kind of post that tag-filtering regulars reward. Gate is membership, not content
   quality.
2. **Hacker News (Show HN)** — strong fit, with the caveat (§3) that a working demo
   matters as much as the rigor. The $4000-hardware-clone post is direct, dated evidence
   that this crowd punishes unverified claims — PluginForge's "we withdrew our own
   finding" posture is an asset here, not something to downplay.
3. **Linux Audio Conference / LAA mailing list** — strong fit by construction: these are
   technical, FLOSS-native, zero-marketing venues where a demo of a working JIT pipeline
   is received on its technical merits. LAC 2026's own stated theme (LLMs + FLOSS) makes
   this close to a tailored invitation for the 2027 cycle.
4. **KVR forum + Gearspace (community norms, not the press desks)** — moderate fit. Both
   explicitly reward a visible GitHub history and self-disclosure over a slick pitch
   (per the "check their GitHub" quote and the shill-posting ban), but this is social
   norm, not an editorial gate, so it rewards rigor only if someone looks.
5. **Synth Anatomy / rekkerd / Attack / BPB (the trade press)** — weakest fit of the
   group, by their own stated logic: Synth Anatomy's one *named* lever is a literal
   license change (open-source the code), not "rigor" in the abstract — a beautifully
   documented proprietary project does not clear their bar any better than an
   undocumented one. The press rewards the open-source *license*, specifically, not the
   engineering process behind it. This is the opposite of what LAC/lobste.rs/HN reward,
   and it's the reason the two groups of channels need different pitches.

---

## 5. Ranked recommendations — cheapest, most credible moves available right now

1. **Write and publish a Hügelton-style disclosure sentence on the public GitHub README,
   stating plainly what the LLM does (writes Faust/C++) and what it does not do (make
   architecture or UI decisions — both human-gated per `COLLABORATION.md`).** Targets:
   Synth Anatomy (clears their stated disclosure norm even without open-sourcing, and is
   the exact template their own exemplar uses), KVR forum, Gearspace. Cost: near-zero —
   the facts are already true and already written down in `CLAUDE.md`; this is a
   publishing task, not an engineering one.

2. **Record one short demo video of the existing working pipeline — the QWERTY→heard-notes
   session already logged in `docs/sessions/017-phase2-interactive-host.md` — and post it
   as a Show HN submission with the self-correction story (PF-076 withdrawal) told plainly
   in the post text.** Targets: Hacker News. Why: §3's evidence says a real demo plus
   evidence-backed, non-overclaiming prose is exactly what wins there, and this project
   already has unusually strong material for the second half (most Show HN posts have
   never publicly admitted a wrong finding; this one has, twice, on the record).

3. **Submit the project to KVR Audio's Product Database (free, self-serve) and post once,
   under one's own account, on the KVR forum and Gearspace's New Product Alert — leading
   with the Linux/Faust/JIT distinctness and the disclosure sentence from #1.** Targets:
   KVR, Gearspace. Why: lowest-effort listing action on this entire ledger; both venues'
   explicit norm (disclose affiliation, no shill posting) is already satisfied by posting
   under a real account with the facts stated.

4. **Pitch Bedroom Producers Blog and Attack Magazine directly by email, framing the
   pitch as "the measured/self-correcting engineering story," not "try my plugin."**
   Targets: BPB (whose editor has publicly and specifically criticized "zero-effort"
   vibe-coded plugins — this pitch answers that criticism directly) and Attack (whose
   "Write for Attack" track explicitly wants original technical ideas, not press
   releases). Why: both outlets are demonstrably willing to cover this exact category —
   APC got covered at the sibling outlet rekkerd for less differentiated material — and
   a direct pitch is the only effort required.

5. **Mark the LAC 2027 and ADC 2027 cycles now (LAC's 2026 CfP deadline already passed,
   2026-03-18; ADC Bristol 2026's poster deadline is today, 2026-10-12, talk deadline
   already passed) and prepare a demo-track (not full-paper) submission for the 2027
   window of whichever opens first.** Targets: LAC, ADC. Why: lowest-stakes academic/
   conference entry point (demo tracks, not peer review), nearly perfect topical fit
   (LAC 2026's own theme was "LLMs and FLOSS"), and directly serves the maintainer's
   stated personal goal (hiring-manager/DSP-peer visibility) better than any press outlet
   on this list — this is the one channel that doubles as a portfolio credential, not
   just a visibility stunt. `docs/research/R5-publishable-run.md` already reached the
   same "both 2026 venues closed, target 2027" conclusion independently; this is
   confirmation, not new information.

---

## Sources (consolidated; all accessed 2026-10-12 unless otherwise noted)

- [Synth Anatomy — vibe coding policy article](https://synthanatomy.com/2026/10/vibe-coding-plugins-the-good-the-bad-and-the-impact-on-my-news-coverage.html)
- [Synth Anatomy — Hügelton Felucca coverage](https://synthanatomy.com/2026/10/hugelton-instruments-felucca-custom-m-vave-fm-1-firmware-turns-it-into-a-multi-engine-synth.html)
- [Hügelton Felucca GitHub README](https://github.com/hugelton/Felucca)
- [Bedroom Producers Blog — "Vibe-coded plugins are giving me SynthEdit déjà vu"](https://bedroomproducersblog.com/2026/04/23/vibe-coding-synthedit/)
- [Bedroom Producers Blog — How We Test](https://bedroomproducersblog.com/how-we-test/)
- [rekkerd.org — Audio Plugin Coder coverage](https://rekkerd.org/audio-plugin-coder-open-source-tool-brings-ai-powered-vst3-development-to-music-producers/)
- [Attack Magazine — "You Can Now Build Your Own Plugins Without C++ Knowledge"](https://www.attackmagazine.com/news/you-can-now-build-your-own-plugins-without-c-knowledge/)
- [Attack Magazine — Write for Attack](https://www.attackmagazine.com/news/write-for-attack/)
- [MusicTech — Contact](https://musictech.com/contact/)
- [KVR Audio — News submissions](https://www.kvraudio.com/submissions)
- [KVR Audio forum — "If Your Plugin is Vibe Coded Please Divulge That Fact"](https://www.kvraudio.com/forum/viewtopic.php?t=633778)
- [KVR Developer Challenge 2026](https://www.kvraudio.com/kvr-developer-challenge/2026/)
- [Gearspace — New Product Alert posting guidelines](https://gearspace.com/threads/new-products-alert-coming-soon-posting-guidelines.1005628/)
- [Sonicstate — "A Storm Coming to our Plugin Market?"](https://sonicstate.com/news/2026/10/04/the-storm-coming-to-our-plugin-market/)
- [CDM — cdm.link](https://cdm.link/)
- [lobste.rs — "Can we fix (or retire) the vibecoding tag?"](https://lobste.rs/s/djzqbn/can_we_fix_retire_vibecoding_tag)
- [Show HN: I built a synth for my daughter](https://news.ycombinator.com/item?id=45900425)
- [Show HN: I built a synthesizer based on 3D physics (Anukari)](https://news.ycombinator.com/item?id=43873074)
- [Show HN: I used AI to recreate a $4000 piece of audio hardware as a plugin](https://news.ycombinator.com/item?id=46471648)
- [Linux Audio Conference 2026 CfP](https://lac26.mucs.club/)
- [ADC Bristol 26](https://audio.dev/adc-bristol-26/)
- [Linux Audio Announce/LAD mailing list guide](https://www.linuxdj.com/audio/lad/subscribelaa/)
- `github.com/Losera/incant-audio` — this project's own public repo (fetched directly for license/star count)
- `docs/competitive_landscape.md`, `STATUS.md`, `CLAUDE.md`, `docs/BUGS.md`,
  `docs/research/R5-publishable-run.md` — this repo, read directly, not web sources

**Marked UNKNOWN in this note** (do not treat as researched/confirmed): r/linuxaudio's
exact current self-promotion rule text; r/edmproduction's current wiki rules; r/WeAreTheMusicMakers'
exact current wiki rule text; CDM's and Sonicstate's and rekkerd's formal (if any)
AI-disclosure submission policy; whether ADC's poster track is still open same-day on
2026-10-12; any engagement numbers for this project's own prior (if any) outreach — none
is known to have happened.
