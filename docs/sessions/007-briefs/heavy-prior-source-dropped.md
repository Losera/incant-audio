touches:  host/Source/PromptPanel.h, host/Source/PromptPanel.cpp,
          host/tests/FakeGenerator.h, host/tests/EditorSessionTest.cpp
depends:  llm/CONTRACT.md:3-13 -- generate.py's subprocess response schema. The relevant
          clause: "Schema: {success, faust_code, attempts, error, reason}, plus ADDITIVE
          kind, prior_source_dropped (generate_json docstring, :285-302)." Note
          llm/CONTRACT.md:33-36 records as a VIOLATION that the schema has no version
          field, so an old host cannot detect which shape it received -- which is exactly
          why the absent-key case below is mandatory, not optional politeness.
provides: nothing new. Do not write a CONTRACT.md for PromptPanel; if you judge one is
          warranted, say so in the change report instead.

Do not touch any file outside the four above. No CMake change is needed: PromptPanel.cpp
and EditorSessionTest.cpp are already in host/CMakeLists.txt's target_sources lists, and
FakeGenerator.h is header-only and already included by EditorSessionTest.cpp.

## What is broken

llm/generate.py:338-342:

    prior_source = request.get("prior_source") or None
    prior_source_dropped = False
    if prior_source:
        candidate = _REFINE_PREAMBLE.format(prior=prior_source) + prompt
        if not providers.preflight_prior_source(system_prompt, candidate, MAX_OUTPUT_TOKENS):
            prior_source, prior_source_dropped = None, True

and llm/generate.py:381-386, on the success path only:

    if prior_source_dropped:
        response["prior_source_dropped"] = True

with the stated intent, verbatim from docs/decisions.md:298-302: "The response schema
gains an optional prior_source_dropped: true, additive on the success path only ... Lets
PromptPanel tell the user their refine silently became a regeneration instead of staying
quiet."

PromptPanel never reads it. `grep -rn prior_source_dropped host/` returns zero hits
(confirmed 2026-08-05). The full extent of the host-side parse is
host/Source/PromptPanel.cpp:526-533:

    bool success   = parsed.getProperty("success", false);
    auto faustCode = parsed.getProperty("faust_code", juce::String()).toString();
    auto errorMsg  = parsed.getProperty("error", juce::String()).toString();
    auto reason    = parsed.getProperty("reason", juce::String()).toString();

So today: user ticks Refine, the preflight drops the prior source (for the effects
system_prompt this happens whenever the prior patch exceeds roughly 371 chars -- headroom
is 8000 - 4096 - 3698 = 206 est. tokens; llm/providers.py:104,121,140-162), the LLM
regenerates from scratch, and the UI reports an ordinary success.

## Read first, in this order

1. host/Source/PromptPanel.cpp:33-57 -- statusForReason(). This is the EXACT precedent for
   an additive optional key: unknown or absent falls back, never asserts, because "a newer
   host must keep working against an older generate.py" (the comment at :39-40). Your
   handling of prior_source_dropped must have the same property in the other direction.
2. host/Source/PromptPanel.cpp:505-585 -- the whole response-handling tail, including the
   success branch's callAsync at :566-583.
3. host/Source/PromptPanel.cpp:586-606 and PromptPanel.h:80,87,91,104,109,130,132 --
   setStatus / setError / clearError, statusLabel and errorBox, and the two test readbacks
   statusTextForTest() and errorTextForTest().
4. host/Source/PluginEditor.cpp:75-120 and :279 -- the OTHER writers of that status label.
5. host/tests/FakeGenerator.h in full, especially writeSuccessCapturing (:142-170).
6. host/tests/EditorSessionTest.cpp:1272-1367 (scenario16_refineCarriesTheSource), plus
   the check()/scenario()/pumpUntil()/struct Session helpers it uses.

## THE TRAP -- read this twice

PromptPanel.cpp:579 sets the status label to "JIT compiling: <first 40 chars>...". That is
NOT the final status. PluginEditor.cpp:92-110 registers processor.onFaustCompileSuccess,
which hops to the message thread and calls:

    safeThis->promptPanel.setStatus("Ready \xe2\x80\x94 DSP live, " + numParams + " params mapped.");

overwriting whatever PromptPanel just wrote. PluginEditor.cpp:79-87 (compile failure) and
:279 (output-guard mute) overwrite it too. So a notice written straight into statusLabel
from the response handler is DESTROYED a few message-thread hops later and the user never
sees it. A test that pumps only until the notice appears would pass against an
implementation that is invisible in the real app -- the exact "control never seen failing"
failure class CLAUDE.md exists to prevent.

Required shape: hold the dropped state on PromptPanel (a bool member set on the message
thread from the response handler), have setStatus() append the notice to whatever text it
is given while that flag is set, and clear the flag on the next submit -- the same
lifetime clearError() already implements at PromptPanel.cpp:602 and PF-021 explains.
Placing the notice in errorBox instead is acceptable IF you keep that same lifetime, but
errorBox is styled and worded as an error region and this is not an error; prefer the
status flag. Do not add a new Label -- that changes resized() and the layout at :746, which
is outside this brief's intent even though PromptPanel.cpp is in touches.

## FakeGenerator change

writeSuccessCapturing (:142-170) hardcodes five properties. Add an optional trailing
parameter -- e.g. `bool priorSourceDropped = false` -- that adds
obj->setProperty("prior_source_dropped", true) when set. Default false, so all existing
call sites (scenario16 at :1290 and :1345) are unaffected. Do NOT change writeSuccess() or
writeFailure(). Keep allOnOneLine=true on JSON::toString -- the comment at :155-156
explains that pretty-printed JSON makes PromptPanel's "last line starting with {" scan
match the bare opening brace and report a phantom failure.

## The scenario

Extend scenario16_refineCarriesTheSource (EditorSessionTest.cpp:1279) with a fourth
section, or add scenario23 -- either is acceptable; scenario16 already builds the Refine-on
state you need, so extending is cheaper. Three checks are mandatory:

1. POSITIVE: fake emits prior_source_dropped:true -> after the patch is live (pumpUntil
   statusTextForTest().contains("DSP live")), statusTextForTest() ALSO carries the notice.
   Asserting AFTER "DSP live" is what makes this test capable of catching the trap above.
2. NEGATIVE: a plain writeSuccessCapturing (no flag) -> statusTextForTest() carries no
   notice. Without this, "always show the notice" would pass.
3. ABSENT-KEY: covered by 2 (the fake omits the key entirely, exactly as an older
   generate.py would).

## Bookkeeping

If you add a scenario rather than extending 16: EditorSessionTest.cpp:1749 prints "22
scenarios" -- bump it; register the call alongside :1770-1777; do not disturb the
PF_SUMMARY print at :1787, tools/health_report.py parses its format.

## Red-case discipline (required, not optional)

After the scenario passes: locally revert your read of prior_source_dropped in
PromptPanel.cpp, rebuild, confirm the new check FAILS, restore it, confirm it passes.
Then, separately, confirm the trap check has teeth: temporarily write the notice directly
into statusLabel at the response handler instead of through the flag, rebuild, and confirm
check 1 fails because PluginEditor's "Ready -- DSP live" overwrote it. Report both results
explicitly. CLAUDE.md: "A control counts only once it has been seen failing."

## End state

tools/check.sh full green. COLLABORATION.md §4 five-line change report with the two
red-case results, at least one file:line citation read (not recalled), and a non-empty
RISK line.

## Out of scope

Do not touch llm/ at all. In particular: whether preflight_prior_source should be
provider-aware instead of hardcoded to groq's GROQ_TPM_LIMIT (llm/providers.py:121,140-162)
regardless of the selected provider is a real and larger design question -- put it in YOUR
MOVE, not in a diff. Do not touch PluginEditor.*, generate.py, providers.py, CONTRACT.md,
STATUS.md, or docs/decisions.md.
