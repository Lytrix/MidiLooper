# Runtime architecture — display consumer

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Display read paths; avoid new `*View` types.

---

## Runtime request

```
Derived Display Representation  +  Display interval  →  Visible pixels / CAP telemetry
```

| Input | Today |
|-------|-------|
| Representation | Sealed committed source (`LoopSnapshot` when complete, else bounded prepared/committed) plus `capturePreview.notes`. `visualCache.notes` is a reuse/handoff seed only — it does not decide whether committed notes exist. |
| Interval | `DetailedWindowContext` / `DisplayWindowUtils` `TickInterval` on **post-projection** notes |

Display is the piano-roll consumer. It does **not** feed `playMidiEvents`. Idle fills dirty bars (2–4) into `visualCache`; OLED filters the paint window. Sixteen-step and bar LED pads read the same `visualCache.notes` list (plus `capture.store` while overdubbing); they do not paint OLED. See [DerivedViews.md](DerivedViews.md) § Consumers.

---

## Owner

**`DisplayManager`** owns draw orchestration. **`Loop`** owns display representation build (`rebuildVisualCacheFromPasses`, `visualCache.revision`).

Display **does not** mutate passes or session store.

---

## Build policy (target)

| Phase | Policy |
|-------|--------|
| Stopped / NOTE_EDIT | Keep `visualCache.notes` on STOPPED track/slot switch (`markDisplayCachesStale`). STOPPED committed display finalization uses `LoopContentResolution::resolveDisplayNotes`, so the first frame reuses effective geometry instead of finalizing stale cache content. Idle fills dirty bars with a budgeted gather and deferred wrap-append. Rebuild display representation when revision stale; filter by window on read. **Target compose (DEC-037 Editor amendment):** same as overdub — committed `visualCache.notes` + this-session overlay. Overlay is neighborhood `NoteEditCurrentState` rows (mover + participants), not a full-loop `EditSession.store` and not a current-state row per cache note. Today: `projectedNoteEditDisplayNotes` uses the current `LoopSnapshot` as committed base when `visualCache` is dirty and that root is current (`ensureNoteEditDisplayProjectionCachesBuilt`, 4.4d); otherwise the Runtime Request consumes `getVisualNotesForSlot` / VisualCache (may be stale — dirty bars are idle maintenance debt). Rematerialize-when-dirty on the request is forbidden (RC-A). A missing current-state row is not Hidden (paint the committed-base note). Unedited Existing Visible rows take the visual-cache span. Paint does not add rematerialize-only Visible rows except this-session Added. Overlap evaluate / leave-restore / `focus.last` use the painted DisplayNote span. Macro commit does not emit Length or NoteRange to `loopLength` for an overlap participant that `visualCache.notes` does not paint (or paints with `endTick == startTick`). Rematerialize-when-dirty for paint is not the architecture. |
| PLAYING / OVERDUBBING | **Defer** full representation rebuild. Overdub paint composes from the complete sealed committed source (`LoopSnapshot` when complete, else bounded prepared/committed plus uncovered `sealedCapturePreview`) plus `capturePreview`. `visualCache.notes` may reuse that frame only when it does not predate the last seal and covers the requested window. Overdub wrap marks affected bars via `markAffectedDisplayCacheRanges` and keeps `visualCache.notes`; it does not `markDisplayCachesStale` / clear notes. Schedule cache fill in `Track::processDeferredIdleMaintenance`. **DEC-210:** a track focus change (`DisplayManager::onTrackFocusChanged`) preserves the target loop's cache; only a slot change takes `invalidateForSlotChange`'s PLAYING clear path. The boot sweep waits, bounded, for every playback-set slot's first 16-bar window so the preserved cache is the prewarmed one. |
| Live record | Incremental `capturePreview` — not full-loop flatten per frame |

**Anti-pattern (64-bar regression):** `ensureVisualCacheBuilt()` on every PLAYING frame or from `MidiLedManager` bar-LED path.

---

## Two-step display (required)

1. **Representation** — `IntervalProjection::projectDisplayNotes` → `DisplayNote` list (head/tail, open tails).
2. **Interval** — viewport filter via `TickInterval::intersects` on that list.

Window move should adjust step 2 only when representation revision unchanged.

`Loop::rebuildVisualCacheIdleSlice` publishes an existing cache revision on each successful partial content slice,
not only when all dirty bars are clean. Stationary draw-source reuse must observe newly rebuilt visible bars;
no-op/denied slices do not publish a revision. This does not bypass the Record publication-gap held-frame policy.
[Partial-cache regression and end-to-end limits](../../../openspec/changes/shipping-rolling-activation/evidence/2026-10-24-2.2-fixed-bridge/SUCCESSOR-DISPLAY.md).

### Record publication gap with live Capture

The no-published-root session-exit hold retains the complete **committed prefix**
and its track/slot/interval metadata, not a frozen live overlay. The existing
draw-source owner reapplies accepted Capture previews into its owned compose
scratch and atomically publishes the composed frame. Preserving a frame through
cache invalidation also preserves its source key. While replacement committed
content is unavailable, the window owner keeps paint on that qualified interval;
it does not combine an old-window prefix with new-window overlays. Complete root
handoff permits source and viewport movement together. This is logical content/
interval coherence, not a measured physical-pixel freshness limit.

The gap includes the corresponding slot's stopped-source obligation before a
Snapshot request exists, not only the queued Snapshot interval. Display queries
existing Track stop metadata; it does not read playback-view storage. Live Record
without such a stopped obligation and already-published sources keep their
ordinary selection rules.
[Production regression and verification](../../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-11-live-display-regression/EXECUTION.md).

During an audible committed publication gap, `Track::processDeferredIdleMaintenance` also services
the existing bounded display-source continuation on VisualCache turns, after stop/Snapshot priority
and only when private predecessor work is not owed. This wakeup is independent of cache reconstruction:
the latter intentionally waits during the gap and active overdub. Small pass sets use the same
continuation in this state because their capped synchronous cache slice cannot run there either.
The selected display's active-slot gap source is protected from unrelated background cache production
until it can be consumed; ordinary background production remains available outside that gap/focus.
No new lease/state, paint gather, playback change or whole-root completion is introduced.
Sparse/dense24bar and two-audible-track native tests require original recorded identities in bars16–24
plus the closed overdub identity before root publication. The separate early pre-RecordPublished
NoteOff hardware report remains unconfirmed by the passing dense early-frame control.
[Production source wakeup and limits](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-27-002014-DISPLAY.md).

**084714 correction/limit:** the initial wakeup sat after the pending-Stop early return and therefore
did not serve the continuous Stop workload. A local, unshipped correction reuses the paired VisualCache
source opportunity after the stop grain: owed private source first, otherwise the committed gap source.
That is not a complete continuity repair. The enforced first-post-wrap fixture has a complete qualified
source before wrap and its original Record identity still allocated afterward, but the painted frame
omits it. Current-request identity qualification rejects the prior result, and re-keying clears it;
the retained frame covers only its old interval. Explicit current/candidate result lifetime, coverage,
destructive invalidation and peak-memory contract is required before changing that model. Earlier
eventual-drain tests must not be cited as first-wrap or device acceptance.
[Failing continuity oracle and proposed contract](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-27-084714-DISPLAY.md).

An admitted stop's borrowed preview is not the live successor preview. `capturePreviewView()`
selects a populated producer preview before a predecessor borrow. Detail/overview paint also query
`Track::captureStopPreviewForSlot` over its bounded live stop-ring window, rejecting slot/LoopId
replacement and reading each existing job-owned preview before the live tier. These synchronous
non-owning reads retain no pointers or extra resource state. The overview keeps incremental append/
open-tail folding and resets its existing mask/cursor when preview identities/replacements change.
No MIDI-storage scan or preview copy runs on Start/Stop. Native tests retain A/B/C identities through
C's Stop before A publication, and preserve finalizing/live overview density bands.
[Preview regression evidence](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-REGRESSION-EXECUTION.md).

When a completed overdub job no longer owns the live session, deferred cleanup still retires its
closed preview through the existing `retiredSealedDisplayNotes_` handoff. It does not close or reset
the successor session. Leaving these rows in the shared sealed tier would let a later
`openOverdubSession` clear them before a committed display source covers them. The retired tier
remains a display-only overlay with existing coverage-qualified release and pass-disable/reset
cleanup; no MIDI pass, published-root currency or live-capture ownership changes. This reuses the
existing deferred row merge/upsert, not a command-edge gather or a new timing guarantee.
Native frame tests preserve both earlier overdub NoteIds through third/fourth Starts while snapshot
publication is withheld and their stop descriptors have retired.
[Completed-predecessor display regression](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-185957-DISPLAY.md).


### Truncated Record stop and closed-note retention

Record Stop moves its existing complete CapturePreview into the stop job by constant-work swap,
before a successor can clear the producer preview. `recordStopPreviewForSlot` provides the qualified
display-only representation and frozen closure tick; it never advertises committed currency.
When truncation makes the prior tail window incompatible with the rewound viewport, draw-source
gather can publish a coherent replacement from that complete preview. Trimmed trailing rows do not
wrap into bar one; open Record holds use the same frozen endpoint as canonical closure. The normal
held-window policy remains when no complete replacement representation exists. Closed live-overdub
notes remain in compose after NoteOff; they cannot depend solely on temporary open-tail paint.
Record preview borrow/reuse cleanup is predecessor-local on publication, empty, retirement and
supersession. No raw MIDI scan or full-loop reconstruction is added to Stop; capacity is reused by
the existing active/finalizing roles, with physical memory/timing qualification still separate.
[Native regression and review evidence](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-DISPLAY-NOTEOFF.md).

Rounded-up Record Stop leaves the prior tail viewport valid but does not make it the only coherent
interval. While the complete qualified stopped Record preview remains available, ordinary follow can
project it into the wrapped playhead's requested interval; otherwise the existing held-window fallback
still applies. This prevents new first-bar overdub input from being hidden outside an old tail window.
Live-tail growth reads only the producer `capturePreview`, never a borrowed predecessor from
`capturePreviewView()`. Stopped-job open rows are locally projected to their job's frozen closure
endpoint before window intersection/composition, so an empty successor cannot extend predecessor
geometry or manufacture transient wrap tails. No source preview/body is changed by paint.
[Rounded-stop and frozen-tail evidence](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-160416-DISPLAY.md).



## Overdub-stop handoff

`Track::stopOverdubbing` publishes the sealed overdub pass before changing to
`PLAYING` and queues `queueSessionExitSnapshot`. `DisplayManager::refreshViewportAfterOverdubStop`
holds the last OVERDUBBING composed `liveDisplayNotes` vector. It does not
pass-gather or snapshot-gather on the stop stack. The first new committed paint
uses `LoopContentResolution::resolveDisplayNotes(*snapshot)` only after
`publishedSnapshotIsCurrent()`. Occupy/pending geometry on the held frame
(`225234`) is accepted until that successor is current.

The invariant is observable in `#CAP,DISP`: the first `OVERDUBBING → PLAYING`
frame must have the same frame and effective counts as the settled frame.
HITL [`session_20260907_230846.log`](../../captures/session_20260907_230846.log)
passed this invariant (`98`/`98` on the first and settled PLAYING frames).

---

## Telemetry

`#CAP DISP` / `SC_DISP` report bounded-window fields for HITL — interval metadata, not a second representation.

`#CAP DIAG,disp_window` / `SC_DISP_WINDOW_TRANSITION` are the **window owner's own transition rows** (`DEC-149`):
`t,s,st,ed,es,len,w,b` where `w`/`b` are the values the next paint resolves from (`detailedWindowStartTick_`,
`detailedWindowBars_`). Raised by `DisplayManager::observeDetailedWindowTransition` from the per-update hook — **never from
the paint path**, so a window that stops repainting is still observed — one row per change of what the display holds and
shows, sampled only while the track plays (plus one closing row), keyed on the window start **in whole bars**. Tier-A via
the `DIAG,disp_` family; direct when USB has room, ring otherwise. They supplement `DISP`, which stays paint-driven and
note-counter-gated: a frozen window raises no `DISP` row by design.

Future long-loop overview strip: binary presence bins per segment (cheap), not full note list per frame — see `openspec/changes/long-loop-piano-roll-window/`.

---

## Related

- [DerivedViews.md](DerivedViews.md) — display representation owner
- [IntervalProjection.md](IntervalProjection.md) — wrap before viewport filter
- [Guides/control-surface/Display.md](../../Guides/control-surface/Display.md) — control surface
- [plans/long_loop_piano_roll_overview_enhancement.md](../../Plans/long_loop_piano_roll_overview_enhancement.md) — window UX
