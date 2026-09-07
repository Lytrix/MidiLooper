# Overdub same-start A loss and D# note-off pairing bugfix

**Status:** Stage 1–3 + overlay compose in tree; remaining HITL miss is pending/tails [`204112`](../../captures/session_20260907_204112.log)  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_183043.log`](../../captures/session_20260907_183043.log) · [`session_20260907_185822.log`](../../captures/session_20260907_185822.log) · [`session_20260907_204112.log`](../../captures/session_20260907_204112.log)  
**Related RC:** [`overdub_occupy_wrap_norowid_noteoff_bugfix.md`](overdub_occupy_wrap_norowid_noteoff_bugfix.md)  
**Evaluation catalogs:** [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) · [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md)

## Scope and boundary

This plan starts after committed pass content has been captured and focuses on
resolved display/source geometry:

```text
capture / pass storage
    → LoopContentResolution / NoteUtils pairing and merge  ← this plan
    → source/display row merge and pending transforms     ← this plan
    → paint
```

Do not reopen the already-verified wrap ledger closure from `174151`, the
`170537` display handoff, MIDI interval scheduling, or paint tail ownership.
Do not add a new Manager or a second note-pairing authority.

## Observed failures

### A: same-start A identity disappears

The capture introduces A rows in the first bar and then reports a missing
preview row:

```text
draw_src,diff ... id,121 ... new,69,48,88
draw_src,layers ... preview,5 ... missed_preview,1
draw_src,diff ... id,129 ... new,69,48,96
draw_src,layers ... preview,3 ... missed_preview,1
```

The committed source also contains repeated A rows from different passes,
including the same start phase at `1488`:

```text
lcr,mmevt,pitch=69,...,t=1488,...,k=on,id=32
lcr,mmevt,pitch=69,...,t=1488,...,k=on,id=86
```

The product identity is `NoteId`; pitch and start are geometry fields. These
rows must not be collapsed solely because they share pitch and start.

### D#: note-offs are lengthened to a later On

The capture repeatedly paints D# (`pitch=63`) rows with ends that move to the
current occupy phase instead of retaining their committed Off geometry:

```text
painted_pending_shorten id=107 old=63,768,1440 new=63,768,1199
painted_pending_shorten id=109 old=63,1536,2208 new=63,1536,1871
painted_pending_shorten id=111 old=63,2304,2976 new=63,2304,2639
```

The capture does not emit the complete D# resolved event stream, so the
exact offending D# On/Off pair cannot be named from serial evidence alone.
The pairing defect is nevertheless established by the current implementation:

1. `emitApplyEditsPerCapturePass` applies edits per capture pass.
2. It then merges pass event vectors with `mergeSortedMidiVectors`, whose
   equal-tick rule keeps the existing/base pass first.
3. The merged stream is reconstructed by
   `NoteUtils::appendCanonicalSpansFromMidiRange`.
4. That reconstruction owns one pitch stack and pairs untagged Offs by LIFO.

When two passes contain same-pitch Ons before their respective Offs, the
merged event stream no longer carries enough ordering to preserve each
pass's On→Off relationship. A later Off can therefore close an earlier or
later pass On, producing a lengthened or exchanged span.

## Root causes

### RC1 — cross-pass event reconstruction loses On→Off ownership

**Owner:** `LoopContentResolution` resolved-window/materialization path,
with pairing performed by `NoteUtils::appendCanonicalSpansFromMidiRange`.

The current event merge is valid for chronological playback events but is not
valid as the sole source of display geometry when untagged same-pitch Offs
from multiple passes are reconstructed through one LIFO stack. The existing
per-pass edit application does not preserve the pair after the vectors are
merged.

This is the root cause of D# Offs being replaced by later-pass or
next-On-derived geometry. It is not a paint-tail rule.

### RC2 — source/display row merge uses geometry as identity

**Owners:** `Loop::appendOverdubPassWrapPairedNotes`,
`Loop::mergeDisplayNotesIntoVisualCache`, and related source/display merge
helpers in `LoopVisualCache.cpp`.

`displayNotePitchStartPresent` treats `(pitch, startTick)` as sufficient
uniqueness. That rule intentionally prevented some interior duplicate
geometry, but it also discards a distinct pass identity when two passes have
the same pitch and start. Separately,
`dropLongerSamePitchStartSiblingsMarkedBySourceTransform` uses an
`endTick` tie plus vector position to discard one equal-geometry sibling.
Neither rule is valid for distinct `NoteId`s that must coexist.

RC2 is downstream of RC1: preserving correct per-pass endpoints is necessary
before changing row admission, otherwise the merge can preserve incorrect
spans.

## Architecture checkpoint for implementation

| Question | Proposed answer |
|---|---|
| Owner module | `LoopContentResolution` / `NoteUtils` for pairing; `LoopVisualCache` / source-view merge for row identity |
| Primary invariant | Every committed NoteOn retains its own committed Off geometry across pass resolution, and distinct valid `NoteId`s are not removed solely for sharing pitch/start/end |
| Ownership change? | NO — extend existing resolution and source merge owners |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO — this is cold resolved-view work, not USB/clock interval work |
| Behavior-preserving? | NO for incorrect same-pitch multi-pass geometry; preserve existing single-pass and wrap pairing behavior |
| New authority? | NO — retain `NoteUtils` pairing and `LoopContentResolution` resolution |

## Proposed implementation stages

### Stage 1 — preserve pair ownership before cross-pass merge

Extend the existing `LoopContentResolution` resolved-view owner so each
capture pass's edited events are paired before pass vectors are combined.
The preferred shape is to carry resolved note spans/paired rows through the
merge, rather than reconstructing one untagged multi-pass stream with a
single pitch LIFO stack.

Requirements:

- retain each valid `NoteId`;
- preserve each pass's NoteOff endpoint;
- keep equal-tick Off-before-On behavior;
- keep wrap-held and `Off@0` handling in the existing `NoteUtils` owner;
- do not stamp capture Offs in canonical stored MIDI;
- do not change playback ledger ownership;
- retain edit-pass Delete/Length semantics.

If the code review proves that a smaller comparator-only change fully
preserves pair ownership for the fixture, it may be used only with native
proof that same-start and overlapping same-pitch passes retain their original
endpoints. A comparator change alone is not accepted without that proof.

### Stage 2 — merge rows by NoteId, not pitch/start geometry

After Stage 1, update the existing display/source merge paths:

- a valid `NoteId` is the row identity;
- distinct valid IDs may share pitch, start, and end;
- a row for the same ID is replaced only by the authoritative resolved
  geometry;
- invalid-ID compatibility rows continue to use the existing geometry rule,
  because they have no identity;
- remove the equal-end vector-position deletion for distinct valid IDs;
- retain the existing same-start suppression only where a documented
  compatibility row has no valid identity and the suppression is required.

Keep the `174151` rule: cache incompleteness is not committed-content
absence. This plan changes identity preservation, not cache authority.

### Stage 3 — paint provenance, not a global keep/drop

HITL [`185822`](../../captures/session_20260907_185822.log) is a paint
identity/provenance gap, not another source-admission or `currentTick`
problem. Closed first-bar A/D# rows remain in `capturePreview` and are
omitted from the snap-composed frame (`missed_preview`; `hidden,0`;
frame ~151). `visualCache` has no `69@48`. This is not 4-bar window clip
and not `174151`.

Do **not** choose either global rule:

- always keep distinct valid `NoteId`s at the same pitch+start
- always drop the longer same-pitch+start sibling

`123236` / `000402` remain: a longer same-pitch+start sibling can be a
stale reconstruction/sealed remnant and must still be droppable. The
`185822` rows are a different class: valid closed incoming preview.

**Question:** which source/provenance classes are eligible for
same-pitch+start sibling suppression?

Stage 2 keeps identity preservation. The older remnant rule is a narrow
exception for stale reconstruction/sealed geometry. Do not weaken
`123236` / `000402` to admit the preview class.

`DisplayNote` has no provenance field (only `noteId`, pitch, velocity,
start, end). Do not add one. Do not infer “remnant” from absence in
`capturePreview`. Stage 3 only adds a keep-guard; remnant eligibility
stays the existing `123236` / `000402` rule.

**Predicate** — `displayNoteIsClosedCapturePreviewIdentity(note, capturePreview.notes)`:

```text
eligibleForLongerSiblingDrop(note):
    if note.noteId is valid
       AND some capturePreview.notes[i].noteId == note.noteId
       AND that preview row is closed (endTick != startTick):
           false
    otherwise:
           existing 123236/000402 eligibility
```

The owner is `Loop::capturePreview.notes` (current incoming identities).
Open holds (`endTick == startTick`) are not this class; they already skip
drop via `capturePreviewNoteIsOpenHold`. `sealedCapturePreviewNotes` is
not an exemption source.

#### Pipeline pin (overlay identity shipped separately)

**1. Overlay admission** — `applyDrawSourceOverlays` during capture:

- Open holds: `appendDisplayNoteUniqueById` (NoteId).
- Closed preview: `upsertDisplayNoteAtPitchStartFrom(out, committedEnd, preview, true)`.
- That search starts at `committedEnd`. A prefix sealed/snapshot row X at
  the same pitch+start is **not** replaced. Closed preview Y **appends**.
  X and Y coexist after overlay.
- Sealed union uses the same helper from index `0` during capture. Distinct
  valid `NoteId`s at the same pitch+start now coexist (`195409`); the same
  valid `NoteId` updates the suffix row. Invalid-ID rows still replace by
  pitch+start. Stopped-path prefix skip (`154310`) is unchanged.
  Plan: [`overdub_overlay_identity_bugfix.md`](overdub_overlay_identity_bugfix.md).

So for `sealed 69@48 → X` then `preview 69@48 → Y` with distinct IDs,
Y **coexists** with prefix X. Overlay is not the deletion owner for the
`185822` miss.

**2. Transform marking** — `applyPendingHideAndShortenToNotes` rewrites
by `NoteId` only. X (occupied prior pass) receives Shorten/Hide. Y is
not rewritten. `sourceTransformMarksPitchStart` then marks **every**
closed row at that pitch+start, including Y.

**3. Sibling drop** — `dropLongerSamePitchStartSiblingsMarkedBySourceTransform`
drops Y when a shorter closed sibling exists at the same pitch+start
(`liveCaptureBegin` is unused). Equal-end distinct IDs are kept
(Stage 2). Different-end closed preview Y is classified as a removable
longer remnant. That is the failing predicate.

#### Central native regression (add with the Stage 3 implementation)

Same pitch+start, different `NoteId`s, one reconstruct/sealed remnant,
one closed incoming preview:

- remnant may still drop per `123236` / `000402` (those tests stay
  unchanged);
- closed incoming preview survives into the composed frame;
- that preview stays closed at its capture Off and does not take
  `currentTick` / occupy-phase end.

## Native tests

Add fixtures to the existing `test_overdub_source_view`,
`test_pending_note_change`, and/or `test_noteutils_reconstruct` suites:

1. Two active passes with same-pitch, same-start, same-end rows retain both
   valid NoteIds.
2. Two active passes with same-pitch and same-start but different Off ticks
   retain each pass's own endpoint.
3. A later pass's Off does not lengthen or close the earlier pass's row.
4. D# fixture matching the capture retains the committed endpoints
   `768→1440`, `1536→2208`, and `2304→2976` before pending occupancy
   Shorten is applied.
5. Pending Shorten may change a row only when its target NoteId is selected;
   it must not delete a distinct equal-geometry sibling.
6. A first-bar A row with the same geometry as another pass remains in the
   composed source and painted frame.
7. Existing single-pass, equal-tick, wrap-held, and `Off@0` tests remain
   unchanged.

Required negative assertions:

- geometry equality never substitutes for valid NoteId identity;
- a paint `missed_preview` result cannot be caused by source merge dropping
  a distinct valid ID;
- a committed NoteOff is never replaced by the next same-pitch NoteOn's
  geometry.

## Observability

Add cold-path native diagnostics or assertions, gated consistently with
existing capture instrumentation:

- resolved row count by valid NoteId;
- pair source (`pass-local` versus compatibility fallback);
- duplicate geometry with distinct NoteIds;
- any dropped valid NoteId and the owner/reason.

Do not suppress `painted_pending_shorten`; it remains evidence that the
pending transform, not the pair resolver, changed a row.

## Validation gates

1. Focused native pairing and source-view fixtures.
2. `pio run -e teensy41-capture-serial`; RAM1 locals must remain at least
   4000.
3. `pio test -e native`.
4. HITL recapture of the same multi-pass sequence.

HITL acceptance:

- the first-bar A rows with equal geometry remain visible with their distinct
  identities;
- `missed_preview=0` for those closed A rows;
- D# rows retain their committed Off endpoints until an explicit pending
  Shorten targets them;
- no D# row ends at the next same-pitch On merely because another pass was
  merged;
- no new `norowid`, ledger growth, or currentTick replacement is introduced.

## Implementation boundary

Stage 1 carries paired `DisplayNote` rows through the merge. Playback
`resolveWindow` / `mergeSortedMidiVectors` is unchanged. `NoteUtils` remains
the pairer. Device sliced `RebuildSpans` still reconstructs the merged event
stream; incremental publish already pairs per pass, and live source/display
use `resolveDisplayNotes`.

## Pre-implementation review

### Ready
- Stage 1 owner is `LoopContentResolution`; pairing stays in `NoteUtils`.
- Stage 2 owners are `LoopVisualCache` / `DisplayWindowUtils` / pending sibling drop.

### Resolved
| Topic | Decision |
|---|---|
| Resolved representation | Paired `DisplayNote` rows, not an identity-aware event comparator |
| Playback event merge | Unchanged |
| Stamp Offs | No |

### Open before coding
None.

## Implemented (2026-09-07)

**Stage 1.** `LoopContentResolution::resolveDisplayNotes` applies edits per
capture pass, pairs through `NoteUtils::reconstructDisplayNotes` (wrap pairing
off), and merges rows by `NoteId`. Wired into the source-view pass-reconstruction
fallback. Playback `resolveWindow`, `resolveNotes`, `resolveState`, and device
sliced `RebuildSpans` stay on the merged event stream. Wrap-held fill stays on
`appendOverdubPassWrapPairedNotes`.

**Stage 2.** Source/body admission uses valid `NoteId`. Wrap-held append still
keys on pitch+start so a linear sibling of the same ID cannot block the wrap
gap. `keepShortestEndAtPitchStart` no longer drops distinct valid IDs.
Equal-end sibling drop after pending Shorten keeps distinct valid IDs.

**Stage 3.** `dropLongerSamePitchStartSiblingsMarkedBySourceTransform`
exempts a row only when `displayNoteIsClosedCapturePreviewIdentity`
matches `Loop::capturePreview.notes`. Remnant drop for `123236` /
`000402` is unchanged. Overlay admission is unchanged.

**Not in this commit:** HITL recapture of [`183043`](../../captures/session_20260907_183043.log)
/ [`185822`](../../captures/session_20260907_185822.log).

## Follow-up: capture pairing and bar-1 composition

The next investigation is split into two independent root-cause slices; do
not combine their fixes:

- **RC-A — draw-source admission:** host shipped. Recover inserts at the
  committed prefix cut so a closed `capturePreview` identity stays in the
  suffix. Stage 3 exemption unchanged. Plan:
  [`overdub_bar1_compose_miss_bugfix.md`](overdub_bar1_compose_miss_bugfix.md).
- **Overlay identity:** host shipped; device compose **PASS**
  [`204112`](../../captures/session_20260907_204112.log). Distinct valid
  `NoteId`s survive sealed/current suffix overlay at the same pitch+start.
  All 17 overdub miss frames are `compose_miss=0` with
  `compose,1,<id>,96` for `69@48–96` (`id=107` / `116` / `126`). Plan:
  [`overdub_overlay_identity_bugfix.md`](overdub_overlay_identity_bugfix.md).
- **Pending / tails frame drop:** next slice. After NoteOff@96 the painted
  frame has no `69@48` row (`frame,0,0,0`) while sealed still holds the
  prior-pass sibling. Occupy at that On selects ledger `69@48–96`. First
  wrap stays clean. Do not reopen overlay admission. Owners to investigate:
  `applyPendingHideAndShortenToNotes`,
  `dropLongerSamePitchStartSiblingsMarkedBySourceTransform`, tails.
  Plan:
  [`overdub_pending_paint_frame_drop_bugfix.md`](overdub_pending_paint_frame_drop_bugfix.md).
- **RC-B — unmatched capture NoteOff:** independently test and then guard
  `Loop::appendCaptureEventWithResult` so a NoteOff is admitted only when the
  existing pairing rule identifies a currently open matching NoteOn. Do not
  alter existing LIFO/channel semantics for valid overlapping NoteOns.

Evidence and execution plan:
[`overdub_capture_pairing_followup_4089eaed.plan.md`](../../.cursor/plans/overdub_capture_pairing_followup_4089eaed.plan.md).
