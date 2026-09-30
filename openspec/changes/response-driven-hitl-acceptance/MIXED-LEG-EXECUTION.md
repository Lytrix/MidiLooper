# Execution record — the mixed leg's flow half (5.1's mixed scenario)

Slice: `response-driven-hitl-acceptance`, the spec's *Mixed record predecessor and overdub successor* scenario
(5.1's last open clause). Landed 2026-09-30. Host tooling only; firmware untouched, no build, no device action.

Authority: the spec scenario — *"WHEN a record interval A stops and seeded B begins overdubbing while A finalizes
THEN one B stop is accepted during A's identified ownership window and eventually executes once AND the same
continuous-transport, content, timing and identity gates apply"* — and design decision 5's two recipes, which the
leg composes rather than extends: A's start is the **record** recipe's, B's successor is the **overdub** recipe's.

## What landed

**The plan** (`leg_gestures("mixed", …)`) is the two recipes' shared shape:

```
select_track → a_record_start (record button)     # A is a record capture, on an empty idle target
             → b_switch (B's slot button)          # ONE gesture: A's stop + B selected
             → b_overdub_start (record button)     # B overdubs into its seeded content
             → b_stop_request (B's slot button)    # ONE press, denied inside A's window
```

One slot gesture still carries A's stop, and there is no transport gesture anywhere. The difference from the
record leg is exactly what the firmware's carrier rule implies: B **has** committed passes, so
`handleToggleRecordForSlot`'s S4f.6 carrier (`!track.hasCommittedPassesInSlot`) does not apply — the switch moves
the selection to B and stops A, and B's start then takes its own record-control press, which is why the plan has
B's `b_overdub_start` and the record leg does not.

**The preparation** is the leg's own branch, and its states are the requirement:
`seed_take(B)` → `prepare_empty(A)` → `require_idle(A)`. A seeded A would make A's press an overdub and B an
uncommitted carrier target (a different scenario), while an armed A would consume the recipe's start press
(`handleArmedRecordPress` *cancels* an arm under a running transport, the 4.2 finding). A is prepared last so it
is the selected target when the leg starts, because the record button acts on the selected slot.

**The driven steps** need no new code: the mixed plan's `a_record_start` takes the **record** branch (A's real
record capture, observed through its slot-scoped `RECORDING` execution) and its `b_switch` / `b_overdub_start` /
`b_stop_request` take the **overdub** branches (the capture-switch route wait, B's `Overdubbing started` gate and
content, B's stop). So the leg composes the two verified sequences instead of duplicating either.

**The wrapper CLI accepts the leg** (`--leg mixed`) so it can be driven, and the run records the temporary
stimulus variant it was driven as (`STIM-BASE-2-MIXED-01-TEMP-ACCEPTED-SEQ-MIXED`): the mixed leg's *profile*
(A empty **and** B seeded — a different preparation from either existing profile) is not registered yet.

## What is deliberately **not** done: grading

`analyse(leg="mixed")` **refuses by name** rather than misgrading: A is a record interval and B an overdub
interval, so the links need A's record vocabulary (`RECS`, the finalization window) on one side and the overdub
families (`DIAG,reentry,admit|bound`, `stop_state`, `canonical_release`) on the other. Grading with either
existing mode would fail a conforming run, so the verdict is `SETUP_INCOMPLETE` with a `mixed_leg_grading` check
that says exactly that. The consequences, stated rather than implied:

- a mixed run is **drivable and ungradable** today, and the report says so in its own vocabulary;
- the mixed leg is **not registered** as a scenario, so the runner route cannot reach it yet;
- its stimulus profile is not declared, so it runs on the wrapper's `cli` authorization path only.

The oracle sites that need the mixed vocabulary, identified while scoping: the identity extraction and L1 (A's
`RECS` stop side vs B's overdub side), L5 (B's start from `od_b["starts"]` while A's stop comes from `stops_a`),
L5b (the route span from A's record stop to B's overdub admission, with the capture-switch as the production
row), L6 (B's `stop_state` execution), L7 (the overdub request counting) — each of which the existing branches
already implement for one side.

## Evidence

- `scripts/test_accepted_sequence.py` **146/146**, of which 6 are new: `MixedLegTests` (the plan as data —
  no transport gesture, no A slot press, one B slot gesture plus one B stop press, two record presses; the plan
  driven against the device's own rows to `DROVE_LEG`; and a populated A answering playback/overdub as a named
  `WRONG_MODE_ROUTE`) and `MixedLegOracleTests` (the refusal, and the CLI accepting the leg). One
  preparation-level pin in `EvidenceGateTests`: B is seeded while A is prepared empty and idle.
- Neighbours green together: corpus 3/3, scenario 38/38, authorization 24/24.
- Full host sweep **1,074 tests / 75 suites**, with only the seven pre-existing unrelated failures.
- **4 mutations red-proved** with byte-identical restores (`mutate_mixed.py`): B's overdub start dropped, B's
  stop turned into a record press, A no longer required to be empty and idle, and the oracle's refusal removed so
  the mixed leg would be graded with the record vocabulary.
- `git diff --check` clean, `openspec validate --strict` valid.

## What remains for the mixed leg

1. the oracle's `mixed` mode (the site list above), with its own tests over a synthetic mixed chain — A's stop
   from `RECS`/the window, B's identity from the overdub families — and the corpus-style determinism the other
   modes have;
2. registration (`accepted_sequence_mixed`, preset membership) and the scenario's translation/refusals;
3. the mixed stimulus profile with its own declared authorization (A empty + B seeded), which is what the spec's
   *Scoped same-boot fixture preparation* asks for ("record and overdub fixtures MUST have different verified
   content preconditions");
4. the device leg, with the rest of 6.x, under a per-session upload/target authorization.
