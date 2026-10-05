# Continuity evidence handoff — task 6.8

What this change hands to the runtime change's continuity acceptance, and what it explicitly does **not** claim. The
authority for every end-to-end threshold lives in the runtime change, not here:
[`runtime-working-set-ownership/specs/…/spec.md`](../runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md)
— *Continuous runtime operation has one end-to-end acceptance contract* — with **S7.5a — envelope/timing oracle**
([tasks.md § S7.5a](../runtime-working-set-ownership/tasks.md)) **upstream**, and **S7.5b — continuity dependency
closure** and **S7.5 — combined continuity acceptance** downstream

([`design.md`'s proof order](../runtime-working-set-ownership/design.md): *"S7.5a — envelope/timing oracle fixes
justified limits before qualification; the sibling budgets and verifies its work against them"*). S7.5b names **this
task (6.8)** among the evidence it collects.

## 1. No local thresholds

This document states **no** end-to-end timing, gap, capacity or residency limit of its own. Every such number belongs to
S7.5a, whose own entry requires limits to be *"derive[d] before qualifying runs, never fit … after observing a run"*, and
requires unsupported combinations to be stated rather than silently shrunk. As of 2026-10-05 **S7.5a is open** — it has
no numeric limits recorded — so this change's contribution is the **method, the fixtures and the rows**, not a competing
set of numbers.

## 2. What must be allocated against the shared budget before the affected paths are enabled

Task 6.8 asks for the window/index/retention/page/closure work to be *allocated and verified against that shared service
and memory budget* before the affected production paths are enabled. The honest state:

| Work | Allocation against S7.5a's budget | Why |
|---|---|---|
| window build/publication (the lane's per-turn work) | **not yet allocated** | the oracle's limits are unpinned; instead this change's native sweeps measure the composed cost law `drainTurns = H × (1 + K) × serviceTurns` and its density/span/boundary cases, and the lane's per-turn cost is charged as **one declared grain** |
| index over the selected snapshot | **not yet allocated** | task 3.6's interval index is unfinished, so there is nothing to charge |
| retention (pinned source) | **not yet allocated** | 3.6a's retained-source figure is recorded but not budgeted against S7.5a |
| paging (the legacy paged path) | **partially** | 5.5's envelope statement and the `S312b` paged-path work bound it within its declared envelope; the shared-budget allocation is still owed |
| closure reconciliation | **partially** | 4.5's fault-injection results are kept **separate** here, as task 6.8 requires |

**The enablement consequence, stated plainly:** this change's rolling path is enabled **only** in the dedicated
qualification env `teensy41-capture-serial-window-qualification` (the shape the firmware's own guard requires), and
**not** in the shared default. Nothing is enabled in normal operation, and the allocation above must exist before anything
else is.

## 3. Evidence this change supplies now

**Native transition evidence (controlled ticks, explicit expected MIDI, no sleeps, no copied state machines).** The ten
interleavings task 6.5 inventories are forced on the real request→worker→publication→consumption path; the two that were
gaps are implemented in
[`s65-native-verification-split`](evidence/2026-10-05-s65-native-verification-split/EVIDENCE.md) — **A→B→A view-slot
reuse with the first completion arriving last**, and **cancellation between every preparation grain** — alongside the
admission-boundary fixtures (`s310z`), the `H × K` contention sweep (`s310x`) and the activation audit (`s310y`). Those
are the transition evidence S7.5's *"native forces production-path interleavings"* clause asks for, and they are red-proved
by named mutations.

**Hardware grain/service-gap and input/output timing evidence — partial, and named as partial.** The authorized device
runs (`s64-s65-device-leg-01`, `f14-overrun-tier`) measured, on the qualification image:

* **service under transport**: 50 `win_gap` blocks with `running=1` and a rising playhead phase — the lane *is* dispatched
  while a transport runs — every block at the declared `permitted=64` with **`overruns=0`**;
* **`eligible=0` and `progress=0` in every block**, i.e. **no window work was owed**, so **no** grain duration, **no**
  service gap under load, and **no** receipt or publication was measured. The lane was observed idle-but-served, not
  loaded;
* the leg's target shape was exercised (a 24-bar take, `length=18432`) with the legacy merged-window path active
  (4 996 `playback_build` rows).

**Not measured on hardware, and therefore not offered as evidence:** grain duration and maximum service gaps under load,
sustained multi-track playback with capture/persistence contention, external-clock behaviour and clock transitions,
memory high-water marks with boundary content parity, and route-specific physical MIDI output timing. Task **6.5**'s
hardware half is **NOT RUN** in that sense, and the reason is measured rather than assumed: the leg's gestures owed no
window demand (the next step is a leg that drives a demoted slot switch), and the lane's coverage/overrun rows needed the
F-14 classification and the post-stop drain before they could be read at all.

**The instrument that read was missing (2026-10-05, `rolling-gate-row`) — it changes what the *next* read will be, not
what has been measured.** The reason the hardware half could not be read is now itself addressable: the lane's **demand**
path (`Track::serviceRollingPlaybackWindow`, the off-clock turn `src/main.cpp` drives) returned a typed `RollingOutcome`
to its caller with **no row**, so "owed nothing" and "refused, and by which term" were the same absence (F-16). The new
qualification-only `DIAG,win_gate` row names that exit, the active slot, the inputs that decided it (`ep`/`av`/`al` tell
apart the entry decision's clock stamp, demotion bound and span alignment; `rq` carries the request's ordinal) and is
emitted once per state change. It observes only, is out of every non-qualification image, and changes no
`RollingOutcome`, guard order or return value. **Not measured by it:** everything listed in the paragraph above — a row
is not a duration. It exists so that the next authorized run *measures* rather than infers which term refuses the lane's
demand.

**And it was then run on the device (2026-10-05, authorized): the inference is over, and the answer is not a lane defect.**
`ep=1` in every row (the deadline clock *is* stamped), and the demand dies at its **entry** because the leg's committed
loop is not a whole number of two-bar spans (`len=19200`, `al=0`) while it is demoted — so **this shape gives the lane no
demand at all**, which is why the hardware half has nothing to measure yet. Where the loop *is* aligned the second gate is
named (`x=win_not_servable`, `rt=0`). The device also corrected the row itself (its change key alternated forever through
two observation bits: 809 → 57 rows). The registered verifier, replayed over the run, says the same thing in its own
terms: `window_demand_observed` false, coverage and overrun checks true, receipts 0, `build_complete` 0. **Nothing in
§3's "not measured" list changes** — the lane was never given a demand — and the next leg shape needs a span-aligned
committed length (a leg-shape decision), with the root-currency question still the owner decision on record. See
[`rolling-gate-row/DEVICE-RUN.md`](evidence/2026-10-05-rolling-gate-row/DEVICE-RUN.md).

**And the one controlled switch into an even-length target put the remaining blocker one step earlier in the chain.** With
the target verified by the device's own rows as **24 bars (span-aligned), committed, demoted, inactive and rootless**, the
trace is: switch pending → launch preparation (its republish request **accepted**) → **source publication FAILED** →
never reached: initial view ready/bound, rolling request, preparation, adoption. The failure is not a refusal: the
`Snapshot` owner selects the target in 135 of 136 rows with `p=1, sep=1, elig=1`, and **no publication unit is serviced
anywhere in the 22 s the switch is pending** (the units begin only after the transport stops), after which the switch
reports its bound and stays pending. So the chain's blocker is *serviced publication work under transport*, which is a
different question from the restore-versus-rootless one and is **not** an S7.5 input until the lane is given a demand.
See [`switch-trace/TRACE.md`](evidence/2026-10-05-switch-trace/TRACE.md).

## 4. The assertions S7.5 will judge, and how this change relates to them

S7.5 requires, under sustained simultaneous playback / record-overdub / undo-redo / switching with
persistence-display-load contention: **zero** unintended note-off or retrigger, **zero** capture loss, **zero** MIDI-content
refusal, **zero** MIDI-deadline violation, **zero** coverage fault and **zero** involuntary stop; and a **one-pass-late
edit is measured as audible-action latency, never hidden as immediate execution**.

This change's contribution to each, in its own words:

* **coverage fault** — the lane's own names are `one_pass_late`, `lag_breach` and the *named* `win_gap_overrun`; the
  native fixtures assert the first two are **named rather than silent**, and F-14 (2026-10-05) made the **overrun** row
  survive the timing-critical shed path, which is a prerequisite for S7.5 being able to see a coverage fault under
  transport at all;
* **one-pass-late as latency, never hidden execution** — this is the change's own lag rule (3.6b): the previous revision
  stays authoritative for exactly one traversal, the serving window is byte-for-byte unchanged, no wait and no defer, and
  the second consecutive miss is reported **once** by name. The handoff's contribution is that the rule exists and is
  red-proved natively; whether a given pass is *audible* inside S7.5a's limits is S7.5a's number, not this document's;
* **unintended note interruption / involuntary stop** — the native closure fixtures (4.5) and the stop/switch fixtures
  assert the change's own contribution; their fault-injection results are kept **separate** here, as task 6.8 requires;
* **capacity refusal inside the envelope** — the boundary pair (`s310z`, `s310x`) names the refusal and the retained
  obligation; whether a given density is *in* the envelope is S7.5a's declaration to make.

## 5. Routing

* **Upstream of this handoff:** S7.5a's envelope/timing oracle. Until its numeric limits exist, this change cannot
  allocate against them and **says so** instead of inventing local thresholds.
* **Downstream consumers of this handoff:** **S7.5b** (dependency closure — it collects this task explicitly) and
  **S7.5** (combined continuity acceptance). Neither is a prerequisite for producing this handoff, and **passing this
  subsystem does not certify the whole runtime**.
* **Kept separate:** task 4.5's fault-injection/recovery results, and the safety policies they exercise.
