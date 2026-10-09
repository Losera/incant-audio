## Title
`eval`: a non-constant filter order reports a stack-overflow depth-budget error instead of a constant-ness error

## Body

Related to #16 (the depth-budget guard itself) — this is a case where hitting that guard is
the *wrong* diagnostic for the actual program error, found via a diagnostic-coverage sweep
over LLM-generated DSP programs.

```faust
import("stdfaust.lib");
fc = hslider("Center Frequency [unit:Hz]", 1000, 20, 20000, 1) : si.smoo;
q = hslider("Q", 1.0, 0.5, 20, 0.01) : si.smoo;
process = fi.bandpass(q, fc, fc), fi.bandpass(q, fc, fc);
```

`fi.bandpass`'s order argument (`q` here) must be a compile-time constant — the program
passes a slider instead. Confirmed live today against current `main` (`2199d069`,
2026-10-06):

```
$ faust-rs --check --error-format json bandpass.dsp
{
  "diagnostics": [{
    "code": "FRS-EVAL-0099",
    "message": "stack overflow in eval (depth budget 32768)",
    "notes": [
      "cause: evaluator recursion exhausted the guarded stack budget",
      "computed: the evaluator crossed its recursion budget before finishing (32768 frames)"
    ],
    "help": ["check recursive definitions for a missing base case or non-decreasing recursive call"],
    "stage": "eval"
  }]
}
```

(Same symptom, budget 8192, on the tagged `0.8.0` build — so #0e32f8b0's 512→32,768 raise
didn't resolve this one, just moved the ceiling.)

There's no recursion here, missing base case or otherwise — the program is trying to build
a filter whose order is a runtime-variable signal, which the eval phase presumably has to
unroll/specialize against, and that unrolling is what exhausts the budget. The `help` text
("check recursive definitions") doesn't point a user at the actual fix (make the order
argument a constant). Would a dedicated constant-ness check ahead of this eval path — the
kind of thing the C++ compiler's `-strict`/type-checking already catches for some
non-constant parameters — be in scope, or is that a different enough mechanism that it's
not worth special-casing against the general depth-budget error?

Happy to send the repro as a `tests/corpus` or `cpp_parity_known_gaps` fixture if useful.
