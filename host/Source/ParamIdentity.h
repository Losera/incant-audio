#pragma once
#include <string>
#include <vector>
#include <cctype>

// ---------------------------------------------------------------------------
// ParamIdentity — the stable, semantic name of a parameter.
//
// WHY THIS EXISTS. Until this header, a parameter's identity WAS its ordinal
// position: ParamPool::remap copied params[i] into slot i unconditionally
// (ParamPool.cpp:33-53, before this change). Everything downstream inherited
// that, and four separate defects are the same fact seen from four angles:
//
//   - PF-038, "knobs appear alphabetically" -- that is Faust's own path order
//     showing through, because nothing else names a parameter.
//   - STATUS.md Broken #6, "the DAW still sees raw slots" -- the host is shown
//     `Macro 7` because slot 7 is the only name that exists.
//   - The forced-Fresh instrument-boundary reset (PluginProcessor.cpp) -- a
//     MITIGATION whose own comment names label-keyed remap as the real fix,
//     because an index shift lands a cutoff value on a pitch control.
//   - Silent truncation past 64 params (PF-051) -- nothing owns the question
//     "which parameter is this", so the 65th simply vanishes.
//
// The project has already run this experiment once and won it: PF-001
// (denormalisation) and PF-037 (the display showing raw slot numbers) were one
// bug seen twice, and the fix was not two fixes -- it was centralising the
// conversion into ParamMap.h, whose header comment says so in as many words.
// This header is the same medicine for the same disease.
//
// ⚠️ THIS IS INTERNAL IDENTITY, NOT HOST IDENTITY. The DAW continues to see
// `macro_0..macro_63` forever -- that is ADR-004's fixed pool and it must never
// move, because host automation lanes are bound to it. The id below is the key;
// the slot is the storage. They compose, they do not compete.
//
// ⚠️ THE DERIVATION RULE IS A ONE-WAY DOOR. Once these strings are written into
// a saved user project, changing how they are computed orphans every restored
// value in every saved session. That is why the rule is versioned
// (kSchemeVersion) and why the persisted blob records which version wrote it:
// a future change must migrate, not silently mismatch.
//
// WHY A READABLE SLUG AND NOT A HASH. This identity is never seen by the host,
// so it is under no size or format constraint, and its only consumers are the
// state blob, the UI IR and (later) MIDI CC mapping. A human debugging a failed
// restore can read `filter/cutoff -> macro_3`; nobody can read `a3f9c1`.
// Revisit only if identities ever become host-visible or size-constrained.
//
// No JUCE and no Faust types on purpose, exactly like ParamGridLayout.h: the
// rule is pure string arithmetic and must be exercisable without a compiled
// DSP, a message thread or a live editor. host/tests/ParamIdentityTest.cpp is
// that exercise, and unlike the test ParamGridLayout.h's header once named, it
// exists.
// ---------------------------------------------------------------------------
namespace ParamIdentity
{

// Bumped ONLY when slug()/base() change what they produce for an input they
// already accepted. Recorded in the persisted state blob as `idScheme` so a
// later change can migrate old projects instead of orphaning them.
inline const char* kSchemeVersion = "v1";

// A label or group name reduced to a stable key.
//   lowercase; every character outside [a-z0-9] becomes '_';
//   runs of '_' collapse to one; leading and trailing '_' are trimmed.
//
// An input that contains no alphanumeric character at all (a label like "---",
// which Faust permits) would reduce to the empty string, and an empty component
// would make `osc/` and a genuinely ungrouped param indistinguishable. Such
// inputs yield "param" instead; two of them in one group then collide, which
// disambiguate() resolves the same way it resolves any other collision.
inline std::string slug(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    bool pendingUnderscore = false;
    for (const char raw : s)
    {
        // Cast through unsigned char before std::isalnum/std::tolower. Faust
        // labels are author-typed and may be UTF-8, whose continuation bytes are
        // negative as plain `char` wherever char is signed -- which is x86-64
        // Linux, this project's only target.
        //
        // Stated precisely, because the usual telling of this rule is stronger
        // than what this box actually does: ISO C requires the ctype functions to
        // work for `unsigned char' values and EOF only (/usr/include/ctype.h:73-74),
        // but glibc explicitly supports negative `signed char' too -- ":74-75, we
        // also support negative `signed char' values for broken old programs",
        // implemented as the range guard `__c >= -128 && __c < 256` at :209. So
        // the cast is NOT preventing UB here; it is preventing a WRONG ANSWER, and
        // keeping the rule portable. Without it, byte 0xC3 of a UTF-8 "é" arrives
        // as -61 and indexes the negative half of the table, where glibc returns
        // it unchanged rather than classifying it -- so the byte would fall to the
        // separator branch on one platform and the alphanumeric branch on another,
        // and a saved project's ids would not survive a move between them.
        const auto uc = static_cast<unsigned char>(raw);

        if (std::isalnum(uc))
        {
            if (pendingUnderscore && ! out.empty())
                out += '_';
            pendingUnderscore = false;
            out += static_cast<char>(std::tolower(uc));
        }
        else
        {
            // Deferred rather than appended: emitting here and collapsing later
            // needs a second pass, and trailing separators would have to be
            // trimmed afterwards. Recording "a separator is owed" and paying it
            // only when the next alphanumeric arrives collapses runs AND drops
            // leading/trailing separators in one pass, with no trimming step.
            pendingUnderscore = true;
        }
    }

    return out.empty() ? "param" : out;
}

// The identity a parameter would have if nothing else claimed it:
//   grouped   -> "<group slug>/<label slug>"   e.g. "filter/cutoff"
//   ungrouped -> "<label slug>"                e.g. "gain"
//
// A nested group path arrives here already slash-joined by ParamCapture
// (FaustEngine.h, ParamInfo::group), and each component is slugged separately so
// the separators survive: "Filter/Env 2" -> "filter/env_2".
inline std::string base(const std::string& group, const std::string& label)
{
    std::string out;

    // Split on '/' so an existing separator stays a separator instead of being
    // flattened into '_' by slug().
    std::string component;
    for (size_t i = 0; i <= group.size(); ++i)
    {
        if (i == group.size() || group[i] == '/')
        {
            if (! component.empty())
            {
                if (! out.empty())
                    out += '/';
                out += slug(component);
            }
            component.clear();
        }
        else
        {
            component += group[i];
        }
    }

    if (! out.empty())
        out += '/';
    out += slug(label);
    return out;
}

// Resolve `candidate` against the ids already assigned in this capture pass.
// The first occurrence keeps the bare name; the 2nd..Nth get "#2".."#N".
//
// Faust emits groups alphabetically and widgets alphabetically inside each
// (FaustEngine.h, the ordering note on ParamInfo::group), so capture order is
// deterministic for a given patch -- which is what makes a positional
// tie-breaker safe here. It is NOT stable across an edit that renames a
// colliding param, and it cannot be: two controls that the patch itself gives
// the same name in the same group are genuinely indistinguishable, and no
// derivation can recover an identity the source never expressed. Recorded as a
// known limit rather than papered over.
inline std::string disambiguate(const std::string& candidate,
                                const std::vector<std::string>& taken)
{
    bool clash = false;
    for (const auto& t : taken)
        if (t == candidate) { clash = true; break; }

    if (! clash)
        return candidate;

    for (int n = 2; ; ++n)
    {
        const std::string attempt = candidate + "#" + std::to_string(n);
        bool used = false;
        for (const auto& t : taken)
            if (t == attempt) { used = true; break; }
        if (! used)
            return attempt;
    }
}

} // namespace ParamIdentity
