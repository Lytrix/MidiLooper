# 3.15f — the short-loop window-route mechanism: C, absolute future handoff positioning

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Status: implemented natively (core); supersession/miss fixtures and the device leg remain.** The earlier position-free proposal in this folder was
rejected as a second admission path; this is the replacement contract.

## Owner decision (2026-10-23)

**C — demand/anchor-driven lead, expressed as absolute future handoff positioning.** One windowed playback state machine
for short and long loops; the DEC-136 safety envelope unchanged; **no second "position-free" admission route**.

The defect is the **positioning assumption**, not the loop category:

```
prepare this window
→ handoff at the next matching phase
→ available lead < 1 536
→ required lead ≥ 1 792
→ impossible forever
```

The demand names its desired coverage/phase; the owner chooses the **earliest future occurrence of that handoff whose
absolute lead satisfies the existing cost bound**:

```
requiredLead = CoverageServiceContract cost for this demand
handoff      = first eligible occurrence of the target phase
               such that handoffAbsoluteTick - now >= requiredLead
```

A short loop skips one or more recurrences; a long loop's first occurrence already fits. Same algorithm, same rule.

## The common route (single state machine)

```
current serving window remains authoritative
        ↓
new demand appears
        ↓
derive coverage + actual build cost
        ↓
position at earliest safe future handoff (absolute tick)
        ↓
bounded owner builds the window
        ↓
ready may finish early and waits for the positioned handoff
        ↓
clock edge reaches the positioned handoff
        ↓
adopt
```

`canCompleteWithinLead()` still decides exactly what it decides today; it is simply fed the **real** absolute lead of
the chosen occurrence, not an artificially impossible next-cycle deadline.

## Latency contract (explicit)

Edits become audible at the **earliest safely positioned adoption boundary**; the previously served immutable window (or
the current NOTE_EDIT fallback before the first window) continues uninterrupted until then. With a 1 536-tick loop and a
1 792-tick minimum preparation requirement, that boundary is roughly two recurrences out. If the product requires "my
edit is audible within the next loop", C cannot satisfy it under the current bound — that would require A / a formal
bound reassessment, with measurement, not a silent weakening.

## Span is tunable; route identity is not

DEC-116 already allows a tunable span. The split to remove is **short → merged route / long → window route**, not
"every loop uses one interval size". It is fine that a short loop uses span X and a long loop span Y, provided both go
through: **request → cost derivation → absolute positioning → bounded owner → ready → edge adoption → the same
emitter/ledger**. Coverage stays demand-derived; **handoff positioning is universal**.

## Required behaviour

1. **Absolute future positioning.** Admission computes the cost, then the earliest occurrence of the demanded handoff
   phase whose absolute lead admits it. The chosen occurrence's absolute tick is recorded on the admitted work/ready.
2. **Ready-early wait.** A completed window waits for its positioned handoff; it is not adopted at an earlier matching
   phase.
3. **Edge adoption.** The clock edge adopts the ready exactly at/after the positioned absolute tick (phase alone is no
   longer sufficient).
4. **Miss → reposition, not permanent refusal.** If the selected handoff passes while the demand is current, the owner
   repositions to the next eligible occurrence with the same cost calculation (DEC-147's pattern), rather than latching
   `CannotCompleteBeforeHandoff` forever.
5. **Supersession.** A demand positioned several cycles ahead is cancelled/repositioned by identity when the source or
   edit revision changes; a stale ready is never adopted.
6. **No envelope change.** `canCompleteWithinLead`, its constants (`leadTicks`, advance, denial allowance) are
   untouched. No position-free reason, no short-loop classification.

## Code touch points (for the G2 packet)

- `Track::requestPlaybackViewMaintenance` — replace the next-occurrence `canCompleteBeforeHandoff` gate with the
  absolute-occurrence search (`handoffLeadTicks + k × loopLength`, smallest `k ≥ 0` that admits); record the absolute
  `deadlineTick` (already a field) and the occurrence.
- `Track::requestPreviewWindow` — the preview demand passes its coverage/phase and lets the owner position; no
  whole-loop special case for admission.
- The ready/adoption path (`resolveNoteEditPreviewSourceForTurn` and the clock edge) — require the positioned absolute
  tick, not merely the phase; the ready waits.
- The miss/lag path (`lagState`, `countedMissPromiseTick`) — reposition on a missed positioned handoff while the demand
  is current (DEC-147).
- `Track::servicePreviewWindowForNoteEditSession` — the re-cover cadence stays; a positioned demand is not re-asked
  while it is in flight.

## RED before production code (owner's three cases)

1. **Short-loop currently impossible.** Two-bar loop; required lead > one recurrence; the **next** occurrence fails; the
   owner positions at the first **later** occurrence satisfying the same `canCompleteWithinLead()`; reaches
   `Ready → Serving`; **no merged fallback**.
2. **Long-loop control.** Enough lead at the ordinary first handoff; positioning stays at the first occurrence; no extra
   cycle of latency.
3. **Supersession.** Demand positioned several cycles ahead; source/edit revision changes before adoption; the old
   demand/window is cancelled/repositioned by identity; a stale ready is never adopted.

Plus the named miss rule: **missing the selected handoff does not become a permanent refusal while the demand is
current; reposition to the next eligible occurrence with the same cost calculation.**

## Acceptance and gates

- The three RED cases above, plus the existing short/long stream-parity fixture updated to the new premise (the short
  route selects the window).
- Measure the actual NOTE_EDIT latency (positioned handoff minus demand tick) for a two-bar loop and record it as the
  product contract evidence; do not trade the bound for it.
- Focused preview/RWO/prewarm suites, full native, both firmware builds at their pins, strict OpenSpec validation, diff
  check. Device leg after the native contract is green.

## Implementation landed (2026-10-23, native)

- `requestPlaybackViewMaintenance` now searches occurrences (`earliestEligibleHandoffLead`: the next occurrence first,
  then whole loops) and admits the first whose **absolute** lead passes the unchanged `canCompleteWithinLead`; the
  chosen occurrence is on `PlaybackViewAdmittedWork` (`deadlineTick`, `positionedLater`).
- `beginCoverageBuild` accepts the positioned lead (both overloads; default 0 keeps the phase check for other callers).
- `resolveNoteEditPreviewSourceForTurn` adopts a positioned ready only at/after its absolute tick; an ordinary
  first-occurrence window keeps the phase adoption, and an unqualified clock keeps the legacy phase path.
- `Track::playbackWindowInterval` is **unchanged** (span tunable); the short/long split is gone at the **route** level:
  both are admitted, positioned and adopted by the same path.
- **Measured latency:** the two-bar NOTE_EDIT demand positions at **2,972 ticks** (1.94 loop periods; ≈7.7 s at
  120 BPM). Edits remain on the previously served source until that boundary.
- **Gates (after the lifecycle slice):** `test_windowed_preview_integration` **35/35**,
  `test_runtime_working_set_ownership` **239/239**, full native **3,419 / 7 skipped / 3,412 succeeded**, both images at
  the exact pins (`391,688 + 1,528` / `391,912 + 1,304`).

### Lifecycle slice landed (2026-10-23, native)

- **Miss → reposition:** a positioned attempt whose absolute handoff passes while the demand is still current has only
  that attempt retired; the same unchanged cost rule positions the next occurrence. The fixture asserts
  `newDeadline > oldDeadline`, `newDeadline ≥ oldDeadline + loopLength` (the missed occurrence is not retried), and
  `canCompleteWithinLead(newDeadline − newAdmitted)` on the replacement. **RED-proved** by disabling the miss check
  (`Expected 3072 to be greater than 3072`).
- **Supersession:** a stale preview ready is retired by identity rather than treated as a foreign successor, and the
  adoption requires the demand's revision, so reaching the old H never adopts the stale candidate. The fixture stages
  the ready with the source already yielded (isolating the identity check from the mutation's cancel) and asserts the
  replacement revision and that the old H does not adopt the old identity.
- **Negative control:** if the demand becomes obsolete before/at the missed boundary, only retirement happens; the
  replacement identity is the new revision, never a reposition of the old one.

### Immediate next RED — the timed adoption is not yet proven end-to-end (two findings)

1. **The first preview window for a revision is authoritative at completion**, so it publishes as the *current* and
   bypasses the positioned wait; the wait applies to a **successor**.
2. **The successor's waiting ready was not adopted at its positioned tick** in the steady-state fixture either: after
   the first preview serves, the mutation's new demand completes into the ready role (complete, unpublished, positioned
   `H`), but the clock edge at `H` did not publish/adopt it. The likely interaction is the serving old preview (or the
   `adoptPreparedPlaybackWindow` refusal) in `resolveNoteEditPreviewSourceForTurn`; that is the next investigation, not
   a fixture tweak.

The adoption gate is implemented and the positioned wait is applied to a ready, but the **edge path that turns that
ready into the served frame** needs its own RED. Do not claim C's timed adoption until it is green.

**Three-point witness (2026-10-23) — the H adoption itself works.** The same successor staged through the production
first publication, then a mutation, then the drain reports:

```
after service: build=1 posLater=1 deadline=3072 admitted=100 epoch=1 cur=2 ready=1 (empty) preRev=3
after drain:   build=0 posLater=1 deadline=3072 admitted=100 epoch=1 cur=2 ready=0 complete=1 pub=0
               rRev=3 cov=0+1536 preRev=3
at H=3072:     cur=0 (adopted), firstFalse=0
```

So the ready is complete/unpublished with the new revision and full coverage, and the edge adopts it exactly at H.
The first defect was that the position lived on the mutable `viewBuildWork`. **Landed:** the runtime now carries the
ready's own position (`readyPositionedLater/DeadlineTick/ClockEpoch`), set at admission, kept when the build publishes
as a **ready** successor, cleared when it publishes as the authoritative **current** or is adopted/retired. The
`sizeof(LoopPlaybackRuntime)` pins moved by the measured **+16 B** (7,904 -> 7,920 target, 10,104 -> 10,120 host); the
runtime is externally allocated per slot and no Track/RAM1 field changed.

**Remaining defect (same witness):** `adoptPreparedPlaybackWindow` now refuses a positioned ready before its tick, but
the before-H turn still adopts early through a **different** adoption path -- the committed/rolling lane calls
`handoffReadyCoverage` directly (bypassing `adoptPreparedPlaybackWindow`). The position must be honoured by **every**
adoption route. Next: guard the direct `handoffReadyCoverage`/`publishCompletedBuildAsReady*` call sites with the
runtime's ready-position predicate (a runtime helper taking the deadline clock), then the timed fixture goes green. No
scheduling change, no seeded state, no coverage weakening.

**Earlier witness result (2026-10-23).** A test-only witness now evaluates the exact adoption terms once at the edge and
records the first false term, `adoptPreparedPlaybackWindow`'s refusal code, the current/ready roles, the work's
position identity (`positionedLater`, deadline, admitted, epoch), the ready and preview revisions, and the coverage.
At the fixture's claimed H it reports:

```
firstFalse=2 (ready not complete)  refusal=0  current=0  ready=1  newRev=3  readyRev=0
positionedLater=0  deadline=0  admitted=0  epoch=0  raw=0  phase=0  loop=1536  cov=0+0
```

So the first false term is **not** the adoption handoff and not a covered/positioned refusal: the successor's ready and
work are **already reset** by the time the fixture calls H, and the captured deadline is 0 (no qualified position). The
claim "the ready waits but the edge refuses" is therefore not yet measured at a true positioned H. The next RED must
witness the same record at three points -- immediately after the mutation's service, after the drain, and at the first
edge attempt -- to find where the work/ready position is lost. Do not seed a current view, alter scheduling, or weaken
coverage; the fixture's staging is the suspect, and the witness is committed (test-only) to locate it.

### Still owed

- The successor-shaped timed adoption fixture (or the first-publication decision above).
- Device leg after the native contract is complete. No checkbox.

## Not claimed

No device parity; no 3.15f checkoff. The supersession/miss slice and the end-to-end adoption timing are the next REDs.
