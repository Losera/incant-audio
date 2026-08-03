// 06_effect_metered — a patch that publishes a METER alongside its knobs.
//
// The fixture for FaustEngine::Kind::Meter. Before 2026-08-02 ParamCapture's
// addHorizontalBargraph/addVerticalBargraph were empty function bodies, so this
// patch's `Out` bargraph was invisible to the entire host: it never entered
// ParamList, so nothing downstream could know a meter existed. No amount of UI
// work could have shown it.
//
// Deliberately mixes both directions in one patch -- two writable controls and
// one output -- because the property under test is that they are told APART:
// the meter must be captured AND must take no macro slot, while Drive and Level
// take slots exactly as before.
import("stdfaust.lib");

drive = hslider("Drive", 2.0, 1.0, 10.0, 0.01);
level = hslider("Level", 0.5, 0.0, 1.0, 0.01);

clip  = min(1.0) : max(-1.0);
chain = *(drive) : clip : *(level);

// The measured signal, published back to the UI. attach() is what keeps the
// bargraph in the compiled program: Faust deletes a control nothing reads, and
// a meter is read by no signal path by definition.
meter(x) = attach(x, abs(x) : ba.linear2db : hbargraph("Out", -60, 0));

// Parenthesised deliberately. `A : meter, A` parses as `A : (meter, A)`, which
// feeds 2 outputs into 1 input -- the sequential-composition arity error that is
// PF-024's most common shape. The parentheses are the fix and the reason this
// comment exists.
process = (chain : meter), chain;
