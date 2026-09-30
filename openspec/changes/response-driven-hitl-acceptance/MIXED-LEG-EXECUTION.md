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

## The grading mode (landed the same day, as its own verified slice)

`analyse(leg="mixed")` now grades the leg with **A's record vocabulary beside B's overdub vocabulary**, and each
site is explicit about which it is:

| Link | The mixed leg's reading |
|---|---|
| `L1_a_stop_accepted_once` | A is a *record* interval: exactly one `RECS` stop row for A's slot (the record leg's own reading), not the overdub `DIAG,reentry,bound` edge — A has no overdub chain, and requiring one would fail a conforming run |
| `L2` / `L3` / `L4` | unchanged: no capacity denial, A's `reentry_window_open` (the scenario's "A's identified ownership window") and A's `canonical_release` |
| `L5_successor_started` | B's start is its own admission (`DIAG,reentry,admit`) and the stop it follows is A's **`RECS`** floor — the pairing each existing mode held half of. The "missing overlap is NOT_EXERCISED" mapping applies here too |
| `L5b_b_start_route` | the route decision spans A's record stop to B's admission, and the production row is the **capture-switch** (`Loop B: Switch gesture from slot A`): B has committed passes, so the S4f.6 carrier does not apply |
| `L6_b_stop_executes_once` | B is an *overdub* interval: its `stop_state` terminal edge (or its retirement) is the execution |
| `L7_one_press_per_stop` | the overdub leg's request counting: A's request is the switch gesture, B's is `Loop B: Stop Overdub` |
| `overdub_interval_identity` | **B only** — its admission, execution and retirement — and the check says A's chain is the record one, so a conforming mixed run is not failed for lacking an overdub chain on A |
| window / `family_accounting` / content / timing / teardown | unchanged and leg-independent |

**Evidence for the mode:** `MixedLegOracleTests` **7/7** — a complete mixed chain is `ACCEPTED_SEQUENCE_PASS` with
every link read in its own vocabulary; a second A stop fails L1; a playback answer instead of the capture-switch
fails `L5b_b_start_route` naming the route; a missing A stop is a FAIL rather than a gap; a window-less chain is a
staging outcome (`SETUP_INCOMPLETE`, exit 3); the *record* leg's own chain cannot prove a mixed leg; and the CLI
accepts the leg. **4 mutations red-proved** with byte-identical restores (`mutate_mixed2.py`): A's stop read with
the binding vocabulary, A's overdub chain required again, the production row read with the record carrier
vocabulary, and the execution link not computed for the mixed leg at all.

**The whole slice in numbers:** `test_accepted_sequence` **151/151** (13 new across both halves — 6 flow, 7
grading, with two of the flow-half pins superseded by the grading ones), corpus **3/3** (every retained fixture
keeps its verdict, so the record and overdub modes are untouched by the condition changes), scenario **38/38**,
authorization 24/24, `test_retention_handoff_plan` 33/33, host sweep **1,068 tests / 75 suites** re-measured for
this slice (single-process `unittest discover` over `scripts/test_*.py`, identical per-module), with only the
pre-existing failing suites — six, none of which imports this change's modules; `test_revision_load_serial_debug`
and `test_note_edit_fader_motor_timing` are direct-run harnesses with no `unittest` tests — and **8 mutations
red-proved** (4 flow, 4 grading), every file restored byte-identically and sha256-verified.

## Evidence

- The **flow half**'s pins: `MixedLegTests` (the plan as data — no transport gesture, no A slot press, one B slot
  gesture plus one B stop press, two record presses; the plan driven against the device's own rows to `DROVE_LEG`;
  a populated A answering playback/overdub as a named `WRONG_MODE_ROUTE`) plus a preparation pin in
  `EvidenceGateTests` that B is seeded while A is prepared empty and idle.
- `git diff --check` clean, `openspec validate --strict` valid.

## What remains for the mixed leg

1. **the mixed edge for the *window*:** the leg is drivable and gradable, but a *driven* mixed run still needs the
   spec's own gates closed end to end on one capture — content parity for **both** intervals being the
   content-pipeline half 5.2 integrated;
2. ~~registration (`accepted_sequence_mixed` and preset membership) with the scenario's translation/refusals~~,
   and ~~the mixed stimulus profile with its own declared authorization~~ — **landed 2026-10-01**: the scenario
   is registered (runner route, preset order `(rec, od, mixed)`, capture-owning) and its own
   `STIM-BASE-2-MIXED-01` profile declares the bench scope. The profile is its own declaration because its two
   targets carry *different* verified content preconditions — the record predecessor verified empty and idle,
   the overdub successor's committed content verified — which is what the requirement `Scoped same-boot fixture
   preparation` asks for. 6 registration mutations red-proved byte-identically;
   [MIXED-LEG-REGISTRATION-EXECUTION.md](MIXED-LEG-REGISTRATION-EXECUTION.md).
3. **the device leg**, with the rest of 6.x, under a per-session upload/target authorization.

The leg is now reachable and authorized through the runner (`--preset accepted_sequence` drives all three). Its
run report still names the temp variant it was driven as (`STIM-BASE-2-MIXED-01-TEMP-ACCEPTED-SEQ-MIXED`), which
is the sanctioned fork of the registered profile rather than a stand-in for a missing one.
