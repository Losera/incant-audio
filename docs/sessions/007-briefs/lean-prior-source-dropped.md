touches:  host/Source/PromptPanel.h, host/Source/PromptPanel.cpp,
          host/tests/FakeGenerator.h, host/tests/EditorSessionTest.cpp
depends:  llm/CONTRACT.md:3-13 (generate.py's response schema and its additive keys)
provides: nothing new

## The gap

generate.py marks a response prior_source_dropped: true when the refine payload did not
survive the token preflight, so the host can tell the user their refine silently became a
from-scratch regeneration (llm/generate.py:338-342 and :381-386; docs/decisions.md:296-302
states that intent in as many words). The host never reads the key --
grep -rn prior_source_dropped host/ returns nothing. Close that.

## Done means

1. A response carrying prior_source_dropped: true leaves the user able to tell their
   refine was dropped, through a surface PromptPanel already owns.
2. The key absent -> behaviour unchanged. An older generate.py must keep working.
3. The notice is still readable once the patch is live, not only in the instant before it.
4. A scenario in host/tests/EditorSessionTest.cpp proves 1, 2 and 3 by driving the real
   editor, not by calling a setter directly.
5. tools/check.sh full is green.

## Work it out from the source

- host/Source/PromptPanel.cpp:520-535 parses the response today. `reason` (:533) is the
  precedent for an additive optional key, including what to do when it is absent.
- Before you choose where the notice lives, run grep -rn "setStatus\|statusLabel"
  host/Source and answer this in your change report: who else writes that surface after a
  successful generation, and what does that mean for how long your notice survives?
- host/tests/EditorSessionTest.cpp:1279 is the existing refine scenario and the pattern to
  follow; host/tests/FakeGenerator.h is what it drives the panel with.
- Extend scenario16 or add a new one -- your call, one line saying which and why.

## Constraints

- Nothing outside the declared touches. llm/ is explicitly not in scope: whether the
  preflight should be provider-aware instead of hardcoded to groq's ceiling
  (llm/providers.py:150-162) is a separate, larger question. An opinion goes in YOUR MOVE,
  not in a diff.
- Red case required (CLAUDE.md). Report what you broke, what you saw fail, and that it
  passed again once restored.
- COLLABORATION.md §3 Tier 2, §4 five-line change report.
