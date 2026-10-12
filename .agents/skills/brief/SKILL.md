---
name: brief
description: Produce a cold-reentry PluginForge briefing with architecture, recent history, open defects, roadmap tension, and exactly three evidence-backed next-task candidates. Use for full onboarding, task selection, or recovery after substantial time away.
---

# Brief

Start with `$orient`, then:

1. Fetch `origin` and record HEAD, branch, divergence, CI state, and dirty paths.
2. Describe the implemented prompt-to-audio pipeline from `host/Source/` and
   `llm/`, separating shipped behavior from stubs.
3. Group recent commits and session records by motive, not merely date.
4. Rank open `PF-###` defects by what they block. Run `tools/check.sh assumed`.
5. Reconcile `STATUS.md`, `PLUGIN_HEALTH_PLAN.md`, `docs/ux_roadmap.md`, and the
   current product-development architecture document. Name stale conflicts.
6. End with exactly three ranked tasks: why now, what each unblocks, cost,
   evidence produced, and what it would not prove. At least one must reduce an
   evidence gap. Mark architecture, schema, provider-contract, dependency, and
   distribution changes consult-first.

Cite every load-bearing claim as `path:line`, or mark it unverified. Status and
decision documents are reports, not proof. Parallelize exploration only when the
runtime supports safe isolated readers; never make parallel agents mandatory.
