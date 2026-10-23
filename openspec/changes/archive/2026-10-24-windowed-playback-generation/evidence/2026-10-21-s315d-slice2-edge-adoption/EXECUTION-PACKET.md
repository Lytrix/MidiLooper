# 3.15d slice 2 — clock-edge `Preview` adoption during NOTE_EDIT (G2 execution packet)

Authority: [IMPLEMENTATION_READINESS.md § G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
This packet covers **one slice** of task 3.15d. It is not evidence that 3.15d, 2.12, 3.7b or 6.4 passed.

Status: **implemented and verified native-side; device leg owed to 3.15f.** Completion record, RED and mutation runs:
[`EXECUTION.md`](EXECUTION.md). Superseded draft (kept for the decision trail): [`CONTRACT-DRAFT.md`](CONTRACT-DRAFT.md).

## What this slice changes

Slice 1 composed and published a `Preview`-origin window for the interval the committed route reads, but nothing
consumed it: `resolveQualificationPlaybackSourceForTurn` was only reached with `fixedViewEligible = isActive &&
isPlaying() && !editManager.isNoteEditActive()`, and `resolveRollingPlaybackWindow` refuses during NOTE_EDIT. The
merged NOTE_EDIT fallback therefore stayed the only audible route.

Slice 2 makes the session's own window the **audible route while it is current**, with the merged reader as the named
compatibility fallback, and gives the lane a **re-cover cadence** so the preview keeps serving across intervals:

| Term | Rule |
|---|---|
| Currency | the window's `source.origin == Preview` **and** `source.playbackRevision == editManager.sessionPlaybackPreviewRevision()` (the **live session** revision, not the lane's frozen `previewSourceRevision`) |
| Serving | the served frame serves while that window is complete, published, current and `coverage.contains(phase)`, and the frame's `boundStorageGeneration` is that role's |
| Frame scope | the seam only ever ends a frame it bound itself; a frame bound to a published non-`Preview` view is left to the committed/capture lane |
| Adoption | a complete, current, covering `Preview` window in the `ready` role is adopted (`adoptPreparedPlaybackWindow`); otherwise the covering `current` role serves as-is (the session's first publication already covers the playhead) |
| Handoff | served out / superseded → frame `Idle`, `loop.nextEventIndex = 0` **once**, merged serves |
| Yields | the same handoff runs when the **preview source is cancelled** (session exit, slot depart, selected-track change) and the frame is bound to the `Preview` view: the seam only serves while the session's revision is live, and the committed reader rejects a non-`Committed` bound view into `Blocked`, which never re-requests |
| Closures | adoption runs `reconcileDeletedNoteEditPreviewSoundings(runtime, currentTick)` (the merged rebuild's selective closure, which does not run while the preview serves) |
| Re-cover cadence | the lane **serves the playhead's own interval** (slice 1's geometry) and asks again **from the playhead** as soon as that coverage no longer contains it (`lead = loopLength − halfWindow`). While the served coverage contains the playhead it asks for nothing |
| Retirement | a prepared successor whose interval lies **behind** the playhead is retired with `retireSupersededReady` (no identity invalidation, so the served frame and sibling slots keep their generation); `applyPlaybackViewTransition(slot, ClearOrLoad)` is only the fallback when the role accounting refuses; a source is cancelled (`cancelPreviewWindowForSlot(SourceMutated)`) only on an actual revision change |

The cadence runs on the **same unconditional per-track owner visit** as the committed requester
(`src/main.cpp`, `for i < offerCount` → `serviceRollingPlaybackWindow(windowClock)`), which is what keeps it turning when
no build is pending. Routing it through `stepPlaybackViewMaintenance` instead would make every NOTE_EDIT fixture's
`hasPendingPlaybackViewMaintenance()` permanently true and change the lane shape the 3.7a/2.12 fixtures pin.

### The successor-ahead cadence was withdrawn — measured

The draft cadence prepared a successor whose interval contained `phase + 2 × TICKS_PER_BAR`, i.e. one whose coverage
**starts one bar ahead** of the playhead. Its true handoff lead is therefore one bar (768 ticks), and
`CoverageServiceContract::canCompleteBeforeHandoff` requires `leadTicks` (768) plus `grains × (denied + 1) × advance`
(24 turns × 64 = 1 536 for a three-grain window): **2 304 ticks**. The request was admitted only because
`requestPreviewWindow` was passing the interval's own centre in as the admission's `currentPhase`, which declares
`loopLength − 768` of lead instead of the distance that exists.

Measured, with the admission given the true playhead (`requestPreviewWindow`'s separate `playheadPhase` /
`intervalPhase` parameters): the two cadence fixtures failed and every adoption fixture passed, and a direct request at
that position returns `CannotCompleteBeforeHandoff` (pinned by `S5`). The contract is **not** weakened and the false
lead is **not** kept; the lane instead re-covers from the playhead, which is admissible and keeps the edge served.

Open, recorded rather than fixed here: for a **two-bar loop** the demoted interval is the whole loop, so its start is
`0` and the lead is at most `loopLength` (1 536) — also below 2 304, so a non-empty preview window cannot be admitted
there either. That is slice 1's admission basis, not something this slice changes; it belongs to 3.15f's route-parity
decision.

## Identity and scope

- **Task / fixture:** `3.15d` — slice 2 of three; fixture `2.12`'s preview-source half stays as slice 1 left it.
- **Approved contract:** DEC-116 §2 and the design's *NOTE_EDIT is a source composition, not a second playback engine*;
  DEC-147 / `boundLaunchView` and DEC-136 admission constants are **read, never re-based**. No new DEC: this slice
  executes a recorded contract.
- **Owner and path:** `Track::resolveNoteEditPreviewSourceForTurn` (new, `FLASHMEM`, called once from the ITCM resolver
  `resolveQualificationPlaybackSourceForTurn`) → `runtime.rolling` frame (reused, **no new fields**) →
  `adoptPreparedPlaybackWindow` / `reconcileDeletedNoteEditPreviewSoundings` / `applyPlaybackViewTransition` /
  `retireSupersededReady`; the re-cover cadence in `Track::servicePreviewWindowForNoteEditSession` (`TRACK_COLD_MEM`);
  the yield in `Track::cancelPreviewWindowForSlot` (`TRACK_COLD_MEM`).
- **Primary files:** `src/Track/TrackPlaybackWindowBuild.cpp` (the seam, the re-cover cadence, the yield, the served-frame
  guard in `serviceRollingPlaybackWindow`), `include/Track.h` (declarations; `requestPreviewWindow` now takes the playhead
  and the interval phase separately), `src/main.cpp` (the owner-visit caller), `src/EditManager/EditSessionDepart.cpp`
  (the departing slot's cancel), `test/test_windowed_preview_integration/test_windowed_preview_integration.cpp` (harness).
- **Preconditions present:** slice 1's interval composition + `Preview` origin; the merged NOTE_EDIT fallback; the
  rolling frame's `phase/boundView/epoch/generation/loopLength/startPhase/lastTick`; `ActiveNoteLedger` +
  `reconcileDeletedNoteEditPreviewSoundings`; the seeded 3.7a/3.13 fixtures.
- **Explicit non-goals:** route parity for short loops (3.15f); any change to `boundLaunchView`, `LedgerBoundarySummary`
  capture, admission constants, or the merged fallback's own composition; display (RWO S4e); save/storage; 3.7b.
- **Commit boundary:** independently revertible (the seam is behind `PLAYBACK_WINDOW_ROLLING_ENABLED`; the default image
  compiles none of it). RAM1/ITCM/SRAM pins are **unchanged**, verified below.

## Oracle fixed before the production edit

Harness home: `test_windowed_preview_integration` with a file-top `#define PLAYBACK_WINDOW_ROLLING_ENABLED 1` (the
default native build does not compile the seam). Every fixture drives `track.playMidiEvents(tick, true)` through the
**real** edge and reads the `SentPlaybackMidiForTest` sink; closures are read at the handler boundary
(`MidiSendCapture`), which is where `closePlannedHandoffClosures` emits them.

Base: the suite's 49 152-tick long loop built by `commitEmissionLoop` (explicit `NoteId`s, no published root — the
session rematerializes from the passes), played with `readyEmissionPlaying(track, slot, kPlayhead = 500)` and a
`publishEmissionClock` stamp. A preview request at **phase 0** is refused (the interval would start at the playhead:
lead 0), so every fixture opens its session at a nonzero phase.

**F1 — `LedgerBoundarySummary` is exact.** An empty summary matches only an empty ledger; a summary holding the
playhead's open identities matches that ledger and not a different one. *Settles the draft's hazard 2*: an empty summary
means "empty ledger", never "unconstrained", and it is consulted only by the bridge selection and the stopped-oracle
publication — a plain served view (the preview route) has **no** ledger admission gate.

**S1 — the clock edge serves the preview's interval.** Open at 500, drain, edge turns at 500 and 900: the 900 turn binds
a `Preview` window (`servedPreviewWindowForTurn != nullptr`), emits the note inside the interval, and the running
session's revision is the window's freeze token. *Counterexample that must fail:* the pre-slice resolver returns no
view (the merged reader serves) → RED.

**S2a — preview → merged seam, crossing note.** Content `on@900 off@1900` (crosses the interval's end) plus
`on@1100 off@1200`. The lane is driven only until its **first** publication (`publishFirstPreview`), modelling a late
cadence; the walk to 2 400 must emit exactly one On and one Off, no duplicate On, no missed Off.
*Counterexample:* nothing on the preview route → RED; a retained cursor/duplicate On at the seam → RED.

**S2b — the same seam while the lane re-covers.** Same content, plus `on@3100 off@3200`; the walk to 3 600 drives a
production owner visit (`serviceLaneVisit`: requester + drain) each 100 ticks: after the first interval's coverage ends
the lane asks again from the playhead, the edge adopts the replacement, and the walk hears the 3100 note with one On
and one Off. *Counterexample:* no re-cover → the 3100 note is skipped; a retained cursor → a duplicate On.

**S3 — adoption closes the identity the session removed.** The merged reader sounds `on@600` (id 5 201) while the
session is open but its first preview is only *admitted*; the row is then deleted, the projection refreshed and the
revision flushed; the edge turn at 700 adopts the post-delete interval and must emit **exactly one** NoteOff (no re-send)
and leave the ledger empty. *Counterexample:* adoption without `reconcileDeletedNoteEditPreviewSoundings` → RED.

**S4 — a superseded revision never serves.** A window published at revision N, then a mutation to N+1 with no drain:
the turn at 1200 is routed to the merged reader (not the stale `Preview`) and hears the mutation's note.
*Counterexample:* a stale `Preview` serving after the session moved.

**S5 — the lane re-covers the playhead when its interval ends, and only then.** After the first publication (the
playhead's own interval) the fixture derives the phase past that coverage: visits at the covered phase prepare nothing;
visits at the past-coverage phase re-cover it and the edge adopts and serves the replacement. The fixture then pins the
**measured** alternative: a request whose interval centre is two bars ahead, admitted from the true playhead, returns
`CannotCompleteBeforeHandoff` — the reason the cadence takes this shape.

**S6 — the session exit yields the preview frame.** A serving preview, then `closeNoteEditSession`: the published
`Preview` window stays readable (the handoff is still a boundary) and `previewHandoffOwed` is set, while the frame that
claimed it stops `Serving`. One production MIDI turn and one committed owner visit then leave the frame **not**
`Blocked` and the requester not `Rejected`. *Counterexample:* the exit leaves the frame bound → the committed reader
rejects it → `Blocked` → the slot's windowed lane never re-requests.

**S7 — a slot switch yields the departing slot's preview.** `beforeSelectedSlotChange` (the production depart hook)
cancels the departing slot's preview and ends its frame, without owing a handoff. *Counterexample:* the departing slot
keeps a `Preview`-bound frame its edge never revisits → its layered reader turns `Blocked`.

**Preserved invariants:** no allocation on the edge; at most one prepared successor; no new `LoopPlaybackRuntime` field
(size pins untouched); no per-turn cursor reset; the merged fallback's fixtures unchanged; the 3.7a/3.13 mutation-edge,
exit and re-focus fixtures unchanged and green.

## Controls and coverage

- **Seams reused:** `clockManager.assignCurrentTickSilently` + `nativePlaybackDeadlineClock` (the requester and the edge
  must agree on the tick), `prewarmPlaybackForSlot`, `stepPlaybackViewMaintenance` (one grain), `LoopEventStore::initPool`,
  `editManager.openNoteEditSession` / `refreshNoteEditSessionProjection` / `flushSessionPreviewRevision`.
- **Production paths exercised:** the real `playMidiEvents` edge, the real quantification resolver, the real lane build
  (`requestPlaybackViewMaintenance` → grains → publication), the real owner visit (`main.cpp`'s per-track call), the real
  ledger and handler boundary. No fixture calls `requestPreviewWindow` to establish a route.
- **RED controls:** the whole production diff stashed → S1, S2a, S2b, S3, S5 RED (F1, S4, S6, S7 green by design: F1/S4
  pin invariants, S6/S7 pin yields added after the review). Mutations with teeth: **B** (drop the adoption reconcile) →
  S3 RED only; **C** (disable the exit yield in `cancelPreviewWindowForSlot`) → S6 RED only; **D** (disable the depart
  hook's cancel) → S7 RED only. Mutation **A** (drop the yield-time cursor reset) → **no** RED; recorded honestly in
  `EXECUTION.md`.
- **Untestable before link:** none; the seam is compiled only under the gate, so the RED run needs the same TU set.

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` (25 cases: 16 existing + 9 new).
- Full: `pio test -e native`.
- Builds + RAM1: `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification`;
  pins to preserve: default `variables 96 192 / code 392 216 + padding 1 000 / locals 34 880`, qualification
  `392 376 + 840` (floor).
- Specs/diff: `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- **Device:** not run for this slice. The seam is qualified-image only and its audible parity is 3.15f's; the board keeps
  the shipping image of slice 1 (`75d47454`) and transport is stopped.

## ITCM finding (recorded because it constrains the seam's shape)

This object file's `*(.text*)` is routed to ITCM by `linker/imxrt1062_t41_lcr.ld`; only `.flashmem*` and the named
per-object overrides reach flash. Consequence: **every lambda written here costs ITCM**. The first draft of the seam used
two lambdas (`sessionPreview`, `serveAdopted`) and grew ITCM by 272 B (qualification `392 648 + 568`, floor 840 → budget
FAILED). Rewriting the seam lambda-free with one adoption site and a `FLASHMEM` function restored the pins exactly
(`392 376 + 840`); the ITCM caller pays one call.
