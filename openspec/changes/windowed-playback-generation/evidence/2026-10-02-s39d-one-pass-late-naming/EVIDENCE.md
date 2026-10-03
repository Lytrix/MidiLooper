# Slice 3.9 (increment 4) — the one-pass-late traversal names itself, through a seam that makes the lane's rows observable

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`. Task **3.9 stays open** on the items at the end;
this increment is also the record of a **3.6b gap it closes** (below).

## What was missing, in one line each

- The **breach** (`BreachSecondMiss`) was row-visible (`lag_breach`, once per episode) while the **allowance**
  (`PlaybackOnePass` / `PreviousOnePass`) — *the state the spec protects*: previous revision authoritative, one pass
  late, never torn — was **silent**. AC-5's scenario "Further edits do not extend the allowance" ends with
  **"the stale condition continues to be named"**, so the allowance's own naming was owed.
- Worse, and found on the way: **the lane's path rows were observable nowhere.** `reportPlaybackViewMaintenanceRow`
  called `DebugSessionCapture::playbackRebuild(...)` inside `#if defined(SESSION_CAPTURE)` with **no macro seam**, so
  under `native` the row was a no-op. 3.6b's claim "the breach is reported once per episode" could therefore only be
  asserted as a **counter** (`breachReports`), never as **the row the claim is about**.

## Authority

| Source | What it says |
|---|---|
| spec, "One pass late is the only stale authority" | a missed deadline keeps the previous revision authoritative, is **reported rather than rebuilt inline**, and the served content is never a mixture of the two versions |
| spec, "Further edits do not extend the allowance" | **"THEN** the existing lag allowance is not reset or extended / **AND** the stale condition continues to be named" |
| design **D10** | receipts follow the delivery rules the repo already paid for, with diagnostic cadence bounded through `Utils/TelemetryCadence.h` |

The behavioural halves of that scenario were **already proved** by 3.6b's fixture (W-22: the first miss keeps the
previous revision and does not tear the window; W-23: a further edit does not reset the allowance — the second
consecutive miss is the breach). What this increment adds is the **naming half**.

## The change

| File | Change |
|---|---|
| `include/Utils/DebugSessionCapture.h` | `DebugSessionCapture::playbackBuildRow(track, slot, loopLengthTicks, revision, path)` — the lane's call wrapped once — plus the `#ifndef`-guarded **`SC_PLAYBACK_BUILD_ROW`** macro in **both** regions (the `SC_ACCEPTED_LATCH` / `SC_WINDOW_READINESS` pattern); the no-capture form is `((void)0)` |
| `src/Utils/DebugSessionCapture.cpp` | `playbackBuildRow` calls `playbackRebuild(0, loopLengthTicks, 0, revision, track, slot, 0, path)` — **byte-for-byte the row a capture build produced before the seam existed** |
| `src/Track/TrackPlaybackWindowBuild.cpp` | `reportPlaybackViewMaintenanceRow` reaches the row **through the seam** (one change, so the whole lane family becomes observable); `advancePlaybackLagTraversal` emits **`one_pass_late`** on a `PreviousOnePass` traversal |

**Why no cadence counter is needed here, stated as the bound it is:** the row is emitted once per **traversal**, which
is the rate the decision itself is made at — the row can never be more frequent than the condition it names, so there
is no counter, no new state and no pin move. **The bound is the caller's discipline**, not the emitter's: the contract
is one traversal per pass per interval (3.6b), and a caller that invoked the traversal point per grain would raise the
volume. Recorded because it is an assumption about callers, not a property of this code.

**The two names never blur:** `one_pass_late` is emitted for the allowance traversal only; the breach keeps 3.6b's
once-per-episode rule and its own name.

## The fixture

3.6b's fixture `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss` is **extended**, not
replaced, with a `PlaybackPathRows` recorder substituted for `SC_PLAYBACK_BUILD_ROW`:

| Assertion | What it pins |
|---|---|
| (W-22 rows) | the allowance traversal names itself — `one_pass_late` present **once**, `lag_breach` absent, because the breach has not happened yet |
| (W-23 rows) | the breach row is emitted (`lag_breach` **once**) — *3.6b's claim, now checked as the row itself* — and the allowance's name is **not** reused for a breach traversal |
| third stale traversal | no further breach row: the row obeys once-per-episode too |
| the eligible boundary | adds **no** row at all |
| episode total | **exactly two** rows for the whole episode (one allowance + one breach) — **measured**, not assumed |

## Red-proofs

| # | Mutation | Observed failure (measured) |
|---|---|---|
| M90 | the `one_pass_late` emission removed | `Expected 1 Was 0. the one-pass-late traversal is named in the evidence stream` |
| M91 | the emission made unconditional (`if (true)`) | `Expected 1 Was 2. and the allowance's name is not reused for a breach traversal` — the no-blur assertion speaks |
| M92 | the allowance row **added to the breach branch** | same assertion, `Was 2` — a second way to blur the two names, caught by the same assertion |
| M93 | the breach's `!lag.breachReported` gate removed (per-traversal breach rows) | `a third miss keeps breaching without re-reporting`, **and** 4.5's containment fixture (`the breach is named once per episode, not once per miss`) |
| M94 | the row emitted **outside the seam** — the pre-seam `#if defined(SESSION_CAPTURE)` call restored | `Expected 1 Was 0. the one-pass-late traversal is named in the evidence stream` — i.e. **the seam is what makes these rows observable**, which is the 3.6b gap this increment closes |

Anchors had their **occurrence count asserted to be 1**, and every case restored **byte-identically** (TU sha256
`d79ad359…c5674c7` before and after all five).

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **70/70** (the 3.6b fixture extended; suite count unchanged) |
| `pio test -e native` (full) | **2705 cases: 7 skipped, 2698 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — **true rebuild** (79.9 s, cache cleared); RAM1 line **byte-identical**: `variables 96,192 / ITCM 393,216 (code 392,328 + padding 888) / locals 34,880`, **48 B headroom**, **floor not re-based** |

## Not claimed

- **No device observation.** Every check here is native, through the substituted recorder.
- **The row cannot occur in production yet.** `advancePlaybackLagTraversal` has **no production caller** — it is reached
  by tests and by the declared contract only, exactly like the readiness receipt (`stepPlaybackViewMaintenance` returns
  `Idle` in production because nothing requests windows). The lane's evidence is therefore **ahead of its wiring**: this
  increment makes the naming true and checkable, not observed on hardware.
- **After the breach's single row, further stale traversals are named by state, not by a new row.** The staleness stays
  visible through `playbackLagDecisionForSlot` for every traversal, and through the latch that 4.5 reads — but the row
  family deliberately does not repeat the breach. That is the residual of the once-per-episode rule, recorded rather
  than smoothed.
- **The row's volume is the caller's traversal rate** (above): no counter guards it, and the argument that none is
  needed rests on a caller discipline that no production caller currently exercises.

## Mapping to the sibling contract's AC-8 (verified by reading it, not assumed)

This change must accept `runtime-working-set-ownership`'s `S4C1-READINESS-CURRENCY-CONTRACT.md` criteria. **AC-8** reads:

> **AC-8 — Readiness evidence is delivered where it is produced.** A row an acceptance surface fences on must **not depend
> on bulk-retention policy for its delivery**, and a diagnostic **must not evict the evidence it explains** … A windowed
> publication's readiness receipts are exactly the rows this trap catches. *Evidence:* the sibling's receipt rows in a
> device leg with the transport running throughout.

Read against what the increments actually did:

| AC-8 clause | Where it is met |
|---|---|
| "must **not depend on bulk-retention policy for its delivery**" | **increment 3.** Measured: the receipt's ring fallback *did* depend on the ring's retention policy, and under the transport flush budget (8) a non-Tier-A head record is **shed silently** — so the fallback was a loss, not a delay. The direct path plus the Tier-A classification remove the dependency |
| "a diagnostic **must not evict the evidence it explains**" | **increment 2**, the declared cadence (`Utils/TelemetryCadence.h`, one row per declared interval, verified as a rate in the increment-2 addendum) |
| "*Evidence:* the sibling's receipt rows **in a device leg with the transport running throughout**" | **not met, and not claimable natively** — this is the device leg, and task **6.4** scopes it in its own words ("the F-7/F-8 device legs and the **AC-8 receipts under a running transport**"). Every increment here is native |

Two consequences worth stating plainly: (i) increment 3 was not only a prose correction — it closed AC-8's **first**
clause, which the lane did not meet before it; (ii) **AC-8's own evidence requirement is the device leg**, so 3.9's
remaining open item is not an inference of mine but a citation, and it lands with 6.4.
