## Title
`diagnostics`: an `FRS-EVAL-0099` "recursive evaluation loop" carries an "unsupported or malformed intermediate form" cause note

## Body

Small inconsistency between a diagnostic's headline message and its own `cause` note,
found via a diagnostic-coverage sweep. Confirmed live today against current `main`
(`2199d069`, 2026-10-06):

```faust
import("stdfaust.lib");

cut = hslider("Cut", 0, 0, 1, 0.01) : si.smoo : ba.bypass2(2, process);

effect = par(i, 2, _ * (1 - cut)) : par(i, 2, _ * cut);

process = effect, cut;
```

```
$ faust-rs --check --error-format json recur.dsp
...
{
  "code": "FRS-EVAL-0099",
  "message": "recursive evaluation loop on node 55",
  "notes": [
    "cause: evaluator reached an unsupported or malformed intermediate form",
    "node_id=55",
    "box_expr=BOXPAR(BOXIDENT(sym(\"effect\")), BOXIDENT(sym(\"cut\")))",
    "expr=(effect, cut)"
  ]
}
```

The headline says "recursive evaluation loop" (and the program genuinely does have one —
`cut` recursively refers to `process`, which refers back to `cut` via `effect`). The
`cause:` note says "unsupported or malformed intermediate form," which reads like a
different failure mode (a box-expression the evaluator doesn't know how to reduce, as
opposed to a cycle it detected). Both might be true simultaneously depending on internals,
but as external text they read as contradictory, and the note doesn't actually explain the
recursion. If they're meant to describe two different sub-cases dispatched to the same
`FRS-EVAL-0099` code, it might be worth splitting the `cause:` text by sub-case, or at least
making the recursive-loop note say something about the cycle (e.g. which symbols are
involved) rather than "malformed intermediate form."
