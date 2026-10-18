# R8 — Licensing options for PluginForge / Incant Audio

**Status:** research only. **Not legal advice; not a substitute for real legal review before
any public release** (see §6). Every factual claim below is cited to a primary source with
an access date of 2026-10-12, or is explicitly marked unverified/secondhand where a primary
source could not be reached.

Scope: this answers whether PluginForge's current proprietary `LICENSE` + its GPL/AGPL-dual-
licensed JUCE dependency is a live compliance problem, and lays out the licensing options if
the project open-sources some or all of itself. It does not re-litigate ADR-001/ADR-003/
ADR-007 (Faust-vs-C++, JUCE-as-framework, Faust-vs-Cmajor) — those are accepted; this is about
the *license*, not the engine choice.

---

## 1. JUCE — what it actually says, for the version we actually use

PluginForge is pinned to **JUCE 7.0.9**, checked out at `/home/losera/JUCE` (outside this
repo, referenced via `JUCE_PATH`; confirmed via `git log` on that checkout —
commit `d054f0d`, 2023-11-20 — and `CMakeLists.txt:2`, `project(JUCE VERSION 7.0.9 ...)`).
That checkout's own `LICENSE.md` is the controlling text for what PluginForge is actually
bound by today, and it says, verbatim:

> "JUCE has tier-leveled license terms, with different terms for each available license:
> JUCE Personal (for developers or startup businesses with revenue under the 50K USD Revenue
> Limit; free), JUCE Indie (for small businesses with under 500K USD Revenue Limit;
> $40/month), JUCE Pro (no Revenue Limit; $130/month), and JUCE Educational (no Revenue Limit;
> free for bona fide educational institutions). All licenses allow you to commercially release
> applications as long as you do not exceed the Revenue Limit and pay the applicable Fees.
> Once your business hits the Revenue Limit for your JUCE license tier, to continue
> distributing your Applications you will either have to upgrade your JUCE license, or
> instead release your Applications under the **GNU General Public License v.3**... which
> means, amongst other things, that you must make the source code of your Applications
> available."

— `/home/losera/JUCE/LICENSE.md` (local checkout, read directly, 2026-10-12), cross-checked
against [JUCE 7 End User License Agreement](https://juce.com/legal/juce-7-license/)
(fetched 2026-10-12).

So for **JUCE 7.0.9 specifically**: four tiers (Personal/free under $50k revenue, Indie
$40/mo under $500k, Pro $130/mo unlimited, Educational free), and GPLv3 is the zero-fee
escape hatch once revenue exceeds the Personal threshold and you don't want to pay. **No fee
is owed to JUCE/PACE (Raw Material Software Ltd / the commercial arm) if you take the GPLv3
path** — the EULA frames GPLv3 and paying for a tier as alternatives, not both required.

**Important finding — this has changed for current JUCE.** As of JUCE 8 and the newly
released **JUCE 9** (per
[forum.juce.com: "JUCE 9: No pricing or EULA changes"](https://forum.juce.com/t/juce-9-no-pricing-or-eula-changes/69000),
fetched 2026-10-12 — JUCE 9 keeps the JUCE 8 EULA and pricing unchanged), the open-source
alternative is **AGPLv3, not GPLv3**:

> "The JUCE Framework modules are dual-licensed under the AGPLv3 and the commercial JUCE
> licence." — [github.com/juce-framework/JUCE/blob/master/LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
> (fetched 2026-10-12)

Current (JUCE 8/9) commercial tiers, per
[juce.com/get-juce/](https://juce.com/get-juce/) and
[juce.com/legal/juce-8-licence/](https://juce.com/legal/juce-8-licence/) (both fetched
2026-10-12): **Starter** (free, perpetual, revenue/funding up to $20,000), **Indie**
($40/user/month or $800 one-time perpetual, up to $300,000), **Pro** ($175/user/month, 12-month
minimum, or $3,500 perpetual, no revenue limit), **Educational** (free). The JUCE 8 EULA page
(fetched 2026-10-12) was read by the fetch tool as going further and stating licensees must
not do anything that could make the Framework "subject to any open source licence... that
requires disclosure in source code form" outside of the AGPLv3 path itself — i.e. the
AGPLv3-or-pay structure is still a real dual license, just with AGPL (not GPL) as the free
side. The practical difference for a project like PluginForge (a local JIT-compiling plugin,
no server component) is small — AGPL's extra "network use" trigger doesn't fire for software
that never runs as a network service — but it is a real difference in the general case and
the next JUCE upgrade would change which GPL-family license is PluginForge's free option.

JUCE's own FAQ language on when the open-source obligation actually triggers (quoted via
[juce.com/get-juce/](https://juce.com/get-juce/), fetched 2026-10-12, and corroborated by
multiple JUCE forum threads):

> "If you are not 'propagating' or 'conveying' closed-source software containing JUCE outside
> of your organisation then you may [not be] violating the terms of the AGPLv3." Internal
> tools and pre-release software not yet given to external testers are "usually permitted."

This is the crux of the urgency question in §4 below.

**Verified:** JUCE 7.0.9's actual tiers/thresholds (read directly from the local checkout's
`LICENSE.md`), JUCE 7's GPLv3 free-escape-hatch with no fee, current JUCE 8/9 tiers and the
AGPLv3 switch (from `juce.com` and the JUCE GitHub repo directly).
**Not independently verified from the full EULA PDF text** (only fetched/summarized via
automated page-fetch, not read in full by a human or by line-by-line primary citation): the
exact contractual mechanics of "upgrade vs. GPL" being true alternatives with literally zero
residual fee in every circumstance — the practical answer is yes based on every primary page
read, but a full legal read of the JUCE 7 EULA PDF itself was not performed here.

---

## 2. Faust / libfaust — LGPL 2.1, with two separate and relevant exceptions

Confirmed directly from GRAME's own files (fetched via `curl` against raw GitHub content,
2026-10-12, not secondhand summaries):

**(a) The Faust compiler itself** (`COPYING.txt` at the repository root, "FAUST compiler,
Version 2.88.0", copyright GRAME + INRIA):

> "This program is free software; you can redistribute it and/or modify it under the terms of
> the GNU **Lesser** General Public License as published by the Free Software Foundation;
> either version 2.1 of the License, or (at your option) any later version."

So the compiler is **LGPL 2.1+**, not plain GPL (a Wikipedia summary calls it "GPL" generically — that
secondary source is imprecise; the primary `COPYING.txt` is unambiguous and is what governs).
Source: `https://raw.githubusercontent.com/grame-cncm/faust/master/COPYING.txt`, fetched
2026-10-12.

**(b) The Faust standard library files** (e.g. `platform.lib`, same pattern across the
`faustlibraries` repo) are LGPL 2.1 **plus an explicit generated-code exception**, quoted
verbatim from the file header (fetched 2026-10-12):

> "EXCEPTION TO THE LGPL LICENSE: As a special exception, you may create a larger FAUST
> program which directly or indirectly imports this library file and still distribute the
> compiled code generated by the FAUST compiler, or a modified version of this compiled code,
> under your own copyright and license. This EXCEPTION TO THE LGPL LICENSE explicitly grants
> you the right to freely choose the license for the resulting compiled code. In particular
> the resulting compiled code has no obligation to be LGPL or GPL. For example you are free to
> choose a commercial or closed source license or any other license if you decide so."
> — `https://raw.githubusercontent.com/grame-cncm/faustlibraries/master/platform.lib`

This answers: Faust-generated DSP code (what the LLM produces, compiled from `.dsp` source
that `import("stdfaust.lib")`) carries **no license obligation at all** derived from the
Faust libraries it imports.

**(c) The embedding API — this is the one that actually matters for `FaustEngine.cpp`.**
`host/Source/FaustEngine.cpp:15` includes `<faust/dsp/llvm-dsp.h>` — confirmed by grepping the
file directly — and `host/CMakeLists.txt:108` resolves `libfaust` via `find_library`, i.e.
**dynamic linking against the system's `libfaust.so`** (the CMake file's own comment, line
106, says it "ships as a shared library on Arch Linux at `/usr/lib/libfaust.so`"). The exact
header PluginForge includes carries this license (fetched 2026-10-12 from
`https://raw.githubusercontent.com/grame-cncm/faust/master/architecture/faust/dsp/llvm-dsp.h`):

> "FAUST Architecture File... This program is free software... GNU Lesser General Public
> License... version 2.1... EXCEPTION: As a special exception, you may create a larger work
> that contains this FAUST architecture section and distribute that work under terms of your
> choice, so long as this FAUST architecture section is not modified."

This is a **broader** exception than (b): it is not about Faust-generated DSP code, it is
about the *host application* (PluginForge's own `host/Source/*`) that contains this
architecture header. It explicitly permits distributing that larger work — the whole JUCE
plugin binary — "under terms of your choice," i.e. proprietary/closed-source, so long as the
header itself (`llvm-dsp.h`) isn't modified. (Note: `compiler/generator/libfaust.h`, the
*internal* compiler implementation header, carries plain LGPL 2.1 with no such exception —
fetched 2026-10-12 — but that is not the header PluginForge includes; it includes the
architecture-file variant, which does carry the exception.)

**Bottom line: Faust/libfaust is not the limiting license in this stack.** Whether
PluginForge stays proprietary or goes GPLv3 for JUCE's sake, Faust's own terms do not force
either outcome — the architecture-file exception already clears it, and PluginForge doesn't
even statically embed libfaust/LLVM (see §3) which would make this analysis even more
favorable.

**A documentation discrepancy worth flagging:** `docs/decisions.md` ADR-001 states "The
plugin binary must link `libfaust` + LLVM (~30 MB added to binary size)" as a *consequence* of
choosing Faust. That describes **static** embedding. The actual current build
(`host/CMakeLists.txt:106-110`) dynamically links against a system-installed `libfaust.so` and
never links LLVM directly (CMakeLists.txt line 947's comment: the harness "must NOT pull
libLLVM in itself"). ADR-001's stated consequence does not match the current build; worth a
correction pass separate from this doc, since it affects both binary-size expectations and
(mildly) the licensing analysis in §3.

**Verified:** compiler license (COPYING.txt, direct), library-import exception
(platform.lib header, direct), the architecture-file exception specifically in the header
PluginForge includes (llvm-dsp.h, direct), and that PluginForge dynamically links against a
system libfaust.so rather than embedding it (CMakeLists.txt, direct).
**Unverified:** whether GRAME's own binary distribution of `libfaust.so` (e.g. the Arch Linux
`faust` package) itself links LLVM statically or dynamically, and what notices that package
carries — not this project's obligation to track unless PluginForge starts bundling that `.so`
itself, which it currently does not (see §3).

---

## 3. LLVM — Apache 2.0 with LLVM Exceptions; low risk, and arguably not even reached

Confirmed directly (fetched 2026-10-12 from `https://llvm.org/LICENSE.txt`):

> License name: **Apache License v2.0 with LLVM Exceptions**. Exception text: "As an
> exception, if, as a result of your compiling your source code, portions of this Software are
> embedded into an Object form of such source code, you may redistribute such embedded
> portions in such Object form without complying with the conditions of Sections 4(a), 4(b)
> and 4(d) of the License." A second exception permits combination with GPLv2 code.

This is a maximally permissive, OSI-approved license (no copyleft, no source-disclosure
requirement — only attribution/notice obligations under the base Apache 2.0 terms).

**More importantly for PluginForge specifically:** per §2(c), PluginForge links `libfaust.so`
dynamically and does not link LLVM directly or redistribute it — `tools/package_release.sh`
was checked directly and copies only the VST3 bundles, standalone executables, the Python
runtime, requirements, and license/readme files into the release tarball; it does not copy
`libfaust.so` or any LLVM artifact. So LLVM's code never enters PluginForge's own binary or
release archive at all under the current build; LLVM's license is GRAME's (and the Arch
package maintainer's) concern for `libfaust.so`'s own distribution, not something PluginForge
directly triggers. This is the lowest-risk item of the three, as expected going in.

**Verified** directly from `llvm.org/LICENSE.txt` and from reading `tools/package_release.sh`
and `host/CMakeLists.txt`.

---

## 4. Urgency verdict: not a live violation today — it is a decision gated on first public/commercial release, not an open compliance problem right now

Per JUCE's own FAQ language quoted in §1, the AGPLv3/GPLv3 disclosure obligation and the
commercial-license fee obligation both attach to **distributing/conveying/propagating** a
closed-source application containing JUCE **outside the organisation** — not to writing,
building, or privately running it. `docs/distribution.md` (read in full, 2026-10-12) confirms
PluginForge has never done a public release: "Creating an archive does not establish that its
third-party dependencies are cleared for public or commercial distribution" and describes only
an **internal clean-machine install rehearsal**, with distribution limited to Linux
VST3/Standalone, no AU/Windows/signing/installer, and the explicit caveat that "a release still
needs a pluginval scan and a real-DAW smoke test before it can be called verified."

So: **PluginForge's current state — a proprietary `LICENSE` plus a GPL/AGPL-dual-licensed JUCE
dependency, never publicly distributed — is not a live compliance violation.** It is a
decision that has not yet been triggered, and under JUCE's **current** JUCE Personal/Starter
free tier (revenue under $50k for the pinned JUCE 7.0.9, or under $20k if ever upgraded to
JUCE 8/9), the *first* public release — if it is free and generates no revenue — could
legitimately stay proprietary and pay JUCE nothing, with no GPL/AGPL obligation at all,
simply by being within the free closed-source tier. The GPL/AGPL question only becomes
*forced* (as opposed to *chosen*) once (a) the project distributes publicly or commercially
**and** (b) revenue crosses the Personal/Starter threshold and the project doesn't want to pay
for Indie/Pro. Today, neither condition (a) nor the revenue half of (b) applies, so there is
no live problem to remediate — but any public release, even a $0 one, is the trigger point
after which the choice (stay under the free closed-source revenue threshold vs. go GPL/AGPL vs.
pay) must be made explicitly and the proprietary `LICENSE` file's third-party-components
disclaimer (quoted in full below) should be understood as provisional rather than settled.

For reference, the current `LICENSE` file already disclaims exactly this gap, which is
accurate and should stay as-is until a decision is made:

> "Third-party components used by or distributed with PluginForge remain subject to their own
> license terms. This notice does not grant rights in JUCE, Faust, LLVM, or any other
> third-party software." — `/home/losera/PluginForge/LICENSE`, read in full 2026-10-12.

---

## 5. Decision table — licensing options if/when PluginForge open-sources

Audiences, per the task framing: **Linux/FOSS-audio community**, **academic/research**,
**hiring managers/peers**, **AI-agent developers** (people evaluating this as an example of
agentic/LLM-driven engineering work).

| Option | Satisfies JUCE 7.0.9's free tier? | Cost | Linux/FOSS audience | Academic/research | Hiring managers/peers | AI-agent devs | What it forecloses |
|---|---|---|---|---|---|---|---|
| **GPLv3** (whole repo, or at least everything linking JUCE) | **Yes, directly** — this is JUCE 7's own named free alternative, $0 fee (§1). If ever upgraded past JUCE 7, the equivalent free alternative becomes AGPLv3 instead, not GPLv3. | $0 to JUCE. Cost is strategic: forecloses ever dual-licensing `host/Source/*` for a fee without re-licensing every outside contributor's work. | Strong fit — matches the copyleft norm common in Linux audio (the FOSS-audio ecosystem skews toward GPL/LGPL tooling; this claim is **unverified/secondhand** here — no primary "LSP-project norms" document was found in this repo or externally during this research pass, only referenced in the task prompt as prior context). Also the cleanest fit for distro packaging (Arch/Debian prefer clear copyleft or permissive, not source-available). | Good — fully inspectable, reproducible, standard academic-citation-friendly license; GPL is well understood and uncontroversial in research contexts. | Mixed-positive — signals "this is real, not vaporware," but copyleft can be a mild negative for a reviewer hoping to reuse code in a closed-source project of their own. | Good — maximizes reuse by other agent/LLM-tooling researchers; GPL is a known quantity for agentic-coding benchmarks and reproducibility claims. | Forecloses closed-source commercial resale of `host/Source/*` without a dual-license (you'd own the copyright and could still dual-license your own code, but anyone else's merged GPL contributions complicate that). |
| **AGPLv3** | Only relevant if a server/SaaS component existed. **Low relevance today** — PluginForge is a local JIT-compiling plugin with no server component (confirmed: `host/CMakeLists.txt` targets are VST3/Standalone only, no network service). If PluginForge ever adds a hosted generation backend (e.g. a cloud LLM relay that isn't just calling a third-party provider API from the user's own machine), AGPL's network-use trigger would start to matter and would also become the *literal* free-tier match for JUCE 8/9 (§1). | Same as GPLv3 cost profile, plus the extra network-use disclosure obligation if a server ever exists. | Same as GPLv3, slightly stronger signal to the self-hosting/FOSS-server crowd specifically. | Same as GPLv3. | Same as GPLv3; mild extra friction since AGPL is less common and sometimes flagged by corporate open-source-use policies. | Same as GPLv3. | Same as GPLv3, plus forecloses offering current functionality as a hosted/SaaS product without open-sourcing that service too. |
| **MIT / Apache-2.0** | **No** — does not by itself satisfy JUCE's free-tier alternative. Choosing MIT/Apache for PluginForge's own code does not stop JUCE's dual-license from applying to the JUCE-linked parts; you would need to pay for JUCE Indie/Pro (JUCE 7: $40/mo or $800 perpetual up to $500k revenue, or $130/mo unlimited; current JUCE 8/9: $40/mo or $800 perpetual up to $300k, or $175/mo / $3,500 perpetual unlimited — §1) once past the free-tier revenue threshold, since MIT/Apache doesn't carry the copyleft JUCE's free alternative requires. | $0 up to the Personal/Starter revenue threshold; **$800–$3,500 one-time or $40–$175/month after**, paid to JUCE, if distributing past that threshold and keeping any part of the JUCE-linked code under a permissive license. | Weaker fit — a maximally permissive license is fine on its own terms, but doesn't carry the copyleft signal part of this community associates with "real" FOSS audio tooling (same unverified/secondhand caveat on community-norm claims as above). | Good — permissive licenses are also very normal in academic/research code; arguably *easier* for other researchers to build directly on without copyleft obligations. | Best of the four for a hiring-manager audience wanting to reuse snippets/patterns freely without copyleft entanglement. | Good — easiest for other agent-tooling builders to fork/embed without license friction. | Forecloses the free JUCE path once revenue crosses the threshold — you'd be paying JUCE rather than the Faust/LLVM stack (which stays effectively free regardless, per §2–3). |
| **Source-available / open-core** (the pattern the repo already uses for the CC-BY/MIT corpus carve-outs) | **Depends which parts.** JUCE's terms attach specifically to code that links against JUCE — i.e. `host/Source/*` (and anything else linking the JUCE modules) — not to `llm/` (the Python generation layer, prompts, error-classification), which never links JUCE at all. So: keeping `host/Source/*` closed/proprietary while open-sourcing `llm/` (generation logic, prompts, `llm/error_classes.py` which is already MIT, the recommendation/UI-face layers) is **legally viable independent of JUCE's terms**, because `llm/` was never subject to them in the first place. The JUCE GPL/AGPL question only has to be answered for `host/Source/*`, and under this pattern it can simply stay proprietary under the free JUCE Personal/Starter tier (so long as revenue stays under threshold) or be paid for once it doesn't. | $0 for the `llm/` open part (it isn't a JUCE-linked component, nothing to satisfy there); same JUCE fee-or-GPL/AGPL choice as above for `host/Source/*` whenever that specifically is distributed past the free tier. | Partial fit — this is exactly the "closed core, open periphery" pattern some of the FOSS-audio community is skeptical of relative to full copyleft, but it is a real, already-precedented pattern in this repo (the CC-BY-4.0 corpus + MIT reproduction-harness carve-outs in `LICENSE`, read in full above) and is strictly more open than the status quo. | Good — the generation/prompting/error-classification research (arguably the more academically interesting half of this project) becomes fully inspectable and reproducible, which is exactly what the existing `bench/repair_ab_repro/` MIT carve-out already optimizes for. | Good — lets a reviewer read the actual LLM-orchestration and error-correction logic (the part most relevant to "can this person build an agentic system") without needing the audio-engineering host code to be open at all. | Strong fit — this is the layer most directly interesting to AI-agent developers (prompting, retry/error-feedback loop, provider abstraction) and is exactly what's already carved out MIT for `llm/error_classes.py` and the repro harness; extending the same pattern to the rest of `llm/` is a natural, low-risk next step. | Forecloses less than full GPL — `host/Source/*` can still be dual-licensed or sold later since it stays proprietary; narrows community trust relative to full copyleft but preserves the most commercial optionality of any option here. |

**Note on the community-norm claims:** the task's framing that Linux/FOSS-audio norms "often
explicitly reject non-copyleft or closed tools" is treated here as **unverified/secondhand**
context supplied going in — no primary document (an "LSP-project norms" writeup, a
distro-packaging policy, etc.) was located in this repository or via external search during
this research pass to substantiate it as a citable fact, only the general real-world pattern
that major Linux-audio distro collections (e.g. KXStudio-era repositories) have historically
preferred GPL/LGPL plugins. Treat it as directionally plausible, not confirmed.

Similarly, **no formal written "open-source code" coverage criterion was found on Synth
Anatomy** during this research pass (searched `synthanatomy.com` for an explicit policy page
and found none — only a clear, longstanding pattern of covering free/open-source plugin
releases as a recurring content category). This is marked **unverified/secondhand** rather
than cited as a confirmed editorial policy.

---

## 6. This is not legal review

Everything above is drawn from primary license texts and official vendor FAQ/EULA pages,
cited with URLs and an access date of 2026-10-12, specifically so the next step can be
targeted rather than starting from zero. It is **not a substitute for actual legal review**
before any public or commercial release of PluginForge/Incant Audio. In particular: the exact
contractual mechanics of the JUCE EULA (the full PDF/HTML text was fetched and summarized by
an automated tool for this research, not read in full line-by-line by a human or cited at
JUCE-EULA-section granularity), the interaction between a chosen license and any future
contributor agreements, trademark questions around the "Incant Audio" name, and jurisdiction-
specific enforceability questions are all outside what this document establishes. Get a real
read from counsel (or at minimum a careful human line-by-line read of the live JUCE EULA and
the Faust/LLVM license files linked above) before shipping a public release under any of the
options in §5.
