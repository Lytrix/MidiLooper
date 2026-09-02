# Overdub wrap — pairing, companions, occupancy

**Status:** RC2 fixture-closed (no pairing firmware). RC3 overlay shipped (`b3448759`); stop hide re-admit **PASS** [`204043`](../../captures/session_20260901_204043.log). RC5 GUS kinds **PASS** ([`181116`](../../captures/session_20260901_181116.log)). RC5b undo cache selection shipped (host). RC2b **parked** — reopen only on the recapture gates below. RC4 occupy populate shipped; `hs=768` stays separate.
**Date:** 2026-09-01
**Kind:** bugfix
**Evidence:** [`181116`](../../captures/session_20260901_181116.log), [`172532`](../../captures/session_20260901_172532.log), [`171835`](../../captures/session_20260901_171835.log), [`170444`](../../captures/session_20260901_170444.log), [`155630`](../../captures/session_20260901_155630.log), [`153517`](../../captures/session_20260901_153517.log)
**Parent:** [`display_seal_handoff_coverage_and_wrap_pairing_bugfix.md`](display_seal_handoff_coverage_and_wrap_pairing_bugfix.md)

**Debugging boundary:**

```text
RC1 builtFromRevision over-claim
    ← committed c74b8e21; not on the 155630 board
sealed capture retention
    ← landed; replaces same pitch+start only
RC2 wrap-held pairing
    ← host A!=B already keeps 2304→2784; device still rewrites the same NoteId
RC3 overlay paint
    ← shipped b3448759; do not reopen
RC2b same-NoteId closure overwrite (181116)
    ← parked 2026-09-01; reopen if recapture shows wrap-held rewrite or 79@240 dual Off
RC4 occupy miss at hs=0 / hs=48 / first hs=768
    ← source view has no present-at-hold row; no Hide/Shorten to apply
RC5 GUS record vs session-overdub
    ← device PASS: kind=1 passIds_n=5 then kind=0
RC5b first U: still paints overdubs
    ← slice_keep 74 after OverlayRejected fallback
```

RC3 overlay stays closed. RC3 must not absorb RC4. If occupancy never created a Hide, no seam overlay may manufacture one.

---

## Contracts

### RC2 — authoritative pairing

A snapshot note is wrap-held only when **that same `NoteId` is still open at the pass boundary**.

The painter must not reinterpret `2304→576` into a nicer span.

### RC3 — companion persistence

A sealed Hide/Shorten remains **projectable against stale committed content** until the **selected** committed representation carries that companion state.

Failure to find the target in the stale parent does **not** discard an identity-addressed companion operation.

### RC4 — occupancy authority

If occupancy never resolves the hold, no downstream seam mechanism is responsible for manufacturing a Hide.

---

## What 155630 proves

Record/loop wrap is clean: no occupancy Hides exist to lose.

Overdub wrap is not. `Track::commitOverdubWrapAtSessionStart` does:

```text
commitCapturePass
queueOverdubWrapSnapshotJournal(passId)   ← copies pendingNoteChanges_ into journal
sealPendingNoteChangesToEditPasses()      ← writes EditPasses, then pendingNoteChanges_.clear()
pushOverdubSessionPass / recordLastOverdubSeal
beginCapture                              ← capturePreview cleared (retention holds closed rows)
```

After that, `applyPendingNoteChangesToDisplayNotes` is a no-op. Snapshot stays stale (`journals=1`) for ~2 s.

Bar 4 occupancy **does** resolve on the 2nd+ hit (`hs=2304,a=1`). The Hide targets the previous overdub note, which is **not** on the published parent. Both `appendPendingNoteChange` and `appendEditRowChange` drop Hide/Shorten/Delete when `parent.findNote` fails, so the journal never keeps the operation.

The EditPass still exists. The published snapshot after the wrap does not. Stage C then paints wrap-held `2304→576` and later `2304→144`. That is RC2 on this capture.

Bar 2 occupancy **never** resolves (`hs=768` always `n=0,a=0`). No Hide is created. During the pass the new capture row covers the old 768 note. At wrap the preview is gone and the old row returns. Same class as `hs=0`. RC4 only.

---

## RC2 — pairing (approved)

Pin-first fixture (do not assume the match key):

```text
pass N:   On(id A, 2304) Off(id A, same-pass NoteOff) → A closed
wrap
pass N+1: On(id B, 768)  Off(id B, 864)               → B independent
drain → neither event closes the other
```

The fixture must assert:

- capture-assigned `A != B`
- snapshot note A: `start = 2304`, `end =` that same-pass NoteOff
- snapshot note B: `start = 768`, `end = 864`
- A is not wrap-held
- A's events keep A's `NoteId`; B's keep B's

Do not assert `2784` or `2872` until the fixture (or a recapture) names the Off tick. `155630` MIDI closes the 2304 note in the same pass; the expected end is that Off, not a range of possible transforms.

Owner: wrap-journal / pass open-note close in `LoopSnapshotPublish` (`updateOpenPassNoteFromEvent` / `appendOpenNoteChange` / `appendNextPassChangeFragment`). No painter span-split workaround.

Host fixture result (`test_wrap_journal_closed_notes_do_not_pair_across_passes`,
`test_session_exit_closed_notes_do_not_pair_across_passes`): with capture-assigned
`A != B`, wrap journal and session-exit pass-change already keep A at `2304→2784`
and B at `768→864`. `findOpenPassNote` already keys by `NoteId`.

Do not write pairing firmware against that path. The device wrap-held rows
(`2304→576` / `2304→144` / `2304→864`) are not reproduced when the two notes
have distinct IDs. Next pin is whatever assigns the same `NoteId` or constructs
a pending Add / snapshot note with those wrap-held ticks — not a painter
workaround and not a change to `updateOpenPassNoteFromEvent`.

---

## RC3 — companion overlay (approved in principle; tighten before firmware)

**Invariant:** a Hide/Shorten that occupancy already resolved stays in the painted committed prefix until the selected committed representation includes that companion state.

**Owner:** `Loop` owns companion state. Display only consumes an already-bounded overlay. Not a fourth draw tier. DisplayManager must not interpret `EditPass` rows.

### What `lastOverdubSealCompanionIds_` is

Pinned from `recordLastOverdubSeal` / `sealPendingNoteChangesToEditPasses`:

| Question | Answer |
|---|---|
| Type | `EditPassIdList` — IDs of **EditPass rows**, not note IDs |
| Source | `sealPendingNoteChangesToEditPasses()` return value, stored by `recordLastOverdubSeal` |
| Hide row | `EditActionType::Delete`, `targetNoteId = change.noteId` |
| Shorten row | `EditActionType::Update`, `EditPropertyType::Length`, `targetNoteId = change.noteId` |
| Existing consumer | `tryAdmitLastOverdubSealIntoMergedEvents` loads those rows and runs `applyNoteEditPassSequence` |

The seam needs the **transformations** those rows represent: Hide/Shorten addressed by `EditPass::targetNoteId`. The ID list is sufficient to load that set from `passes.editPasses`. If a host test cannot derive a complete overlay from those IDs, stop and do not invent a Display-owned store.

### Overlay contract

`Loop` exposes the effective sealed companion overlay for the currently selected committed representation. Display applies that overlay to the display prefix.

`applyPendingHideAndShortenToNotes` is already that projection:

- caller passes a `PendingNoteChangeVec`; the function does **not** read `pendingNoteChanges_`
- match is by `noteId` (overlap match is only the optional display Hide path)
- Hide omits the note; Shorten rewrites ticks
- no authoritative-content lookup
- missing target is a no-op

Reuse it. Do not add an EditPass interpreter in DisplayManager.

Preferred reuse: `Loop::applyPendingNoteChangesToDisplayNotes` (or a Loop sibling it calls) also applies the sealed overlay when live pending is empty and the selected representation does not yet carry the seal. Display call sites stay unchanged.

Do **not** skip `pendingNoteChanges_.clear()`. Sealed EditPasses remain the durable copy.

### Journal: identity-addressed, idempotent

Today `appendPendingNoteChange` and `appendEditRowChange` treat `parent.findNote == null` as “drop the operation.”

A missing target at journal **construction** is not evidence that the operation is invalid; it only means the current parent cannot apply it yet. The parent may be stale, or an earlier companion may already have removed the note.

The journal must preserve the operation as an **identity-addressed transformation**. Application is idempotent:

```text
Hide(noteId)
    → if target exists: remove/hide
    → if target absent: no-op
```

Do not authorize “queue Remove/Replace by `noteId` against this parent” as the only representation. The queued form must survive a parent that cannot apply it yet.

### Release / coverage

Stop applying the overlay when the **selected** committed representation **contains the sealed companion state**.

`playbackRevision` is monotonic, and each revision fully includes all preceding companions. Under that invariant, and under the RC1 rule that `builtFromRevision = R` only when the gather actually consumed committed content at R, `revision >= lastOverdubSealPlaybackRevision_` is sufficient.

Do not treat a numeric comparison as coverage without those two invariants. RC1 already forbids stamping coverage from an OverlayRejected gather.

Host seam must assert the numeric implication directly:

```text
Rseal = 10

selected representation R=9
    → overlay required
selected representation R=10
    → overlay no longer required
selected representation R=11
    → overlay no longer required
```

That proves `revision >= lastOverdubSealPlaybackRevision_` means cumulative companion coverage, not merely a monotonic counter.

**Amended 2026-09-01 ([`201956`](../../captures/session_20260901_201956.log)):** revision >= Rseal is not sufficient when the Hide target is still in snapshot membership (`63@768→1151` next to `768→864`). Display keeps projecting sealed Hide while companion IDs exist. See [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](overdub_occupy_same_start_stop_hide_readmit_bugfix.md).

**Closed 2026-09-01 ([`204043`](../../captures/session_20260901_204043.log)):** that re-admit did not return after snapshot-current (`pending=0` `root_rev=15`/`playback_rev=15`, frame `63@768` len 95, `DFRAME` 81). Do not reopen RC3 for `768→1151`.

### Host tests before RC3 firmware

**Seam projection (same identity)**

```text
snapshot:   A(noteId=123, pitch=64, start=2304)
companion:  Delete(targetNoteId=123)
wrap:       companion EditPass sealed; pendingNoteChanges_ cleared
stale snapshot still contains A
Loop overlay applied to display notes from that snapshot: A is absent
current snapshot contains the companion result
    → overlay no longer required
```

The hidden note must be the same `NoteId`, not a pitch/start match.

**Mismatched-NoteId negative**

```text
snapshot:  A(noteId=123, pitch=64, start=2304)
overlay:   Hide(noteId=456, pitch=64, start=2304)
result:    A remains
```

**Cumulative revision**

`Rseal=10`: overlay required at selected R=9; not required at R=10 or R=11.

**Journal**

Hide of a `noteId` absent from the parent snapshot remains in the wrap journal. After drain, the published snapshot does not contain that note.

`queueOverdubWrapSnapshotJournal` returns `false` when no fragment was queued
(`fragments.empty()`), or when wrapPassId / parent / loop length is invalid.
It does not mean a broader construction failure. Retaining an identity-addressed
Hide must produce a fragment so the function returns `true` because something
was journaled.

Journal firmware (this slice): Hide/Delete and Shorten/Update stay in the
change set when `parent.findNote` fails. Payload is `Remove`/`Replace` with
`next.noteId` set and `previous` empty. Apply resolves `previous` from the
construction candidate by `NoteId`; missing target skips the change (no-op)
and does not fail construction. Do not reconstruct the missing note.

Overlay paint (this slice): `Loop::applyPendingNoteChangesToDisplayNotes` also
projects the sealed companion overlay when the selected committed representation
is still behind `lastOverdubSealPlaybackRevision_`. Match is `noteId` only.
Display call sites stay consumers; they do not read `EditPass` rows. Admit
receipt clear does not drop companion IDs or `Rseal`. RC4 remains untouched.

**Negative (overlay is not a second content store)**

```text
Hide(A)
Hide(A) again     → still no A
Hide(A) after A already absent → still no A; no resurrection
```

**Does not authorize:** changing occupancy lookup, changing the capture-retention release predicate, holding `capturePreview` past the existing retention rule, or making the companion overlay mutate, replace, or become authoritative storage for committed loop content. The overlay exists only because publication is asynchronous.

If bar 2 disappears as a side effect of RC3, RC3 has absorbed RC4 and must be rejected.

---

## What 172532 proves

Four overdub wraps, loop 3072, transport stop (`OverdubStopToStopped`, commit **Skipped**, `push_overdub_undo` still **ok**).

**Lengthen** is a same-`NoteId` end rewrite, not a second note:

| id | Composed | Drawn |
|---|---|---|
| 85 `64` | `2304→2496` (linear, 192) | `2304→432` (wrap-held) |
| 86/93 `69` | `2352→2400` | `2352→96` |
| 94 `64` | `2536→2736` | `2536→432` |

`findOpenPassNote` already keys by `NoteId`. The closed-A-then-closed-B fixture does not reproduce this. The remaining constructor is whatever **replaces id 85’s end** after the linear close. Same-pass Off for id 85 stays in LCR (`mmspan` / `mmevt` `2304→2496`). `draw_src` also drops the bar-1 sibling (`64@240→432` / `69@48→96` → `new,0,0,0`).

**Shorten ignored:** later holds on the painted bar-1 head stay unresolved — `64@hs=0` is `n=0`; `69@hs=48` is `a=0`. Occupancy only walks `overdubSourceViewNotes_` via `displayNotePresentAtHold`. A linear `2304→2496` is **not** present at tick 0. A wrap-held `2304→432` **would** be (unwrap + one-loop shift). `a=0` means the source view does not carry the wrap-held span (or identity is rejected). Overlay must not invent a Hide.

**Undo:** record stop ran `pushRecordPassAdded`; overdub stop ran `pushOverdubSessionOnStop` on the Skipped path. The file has **no** post-stop **U:**. Firmware contract is already two GUS kinds ([DEC-038.2](../Authority/ARCHITECTURE_RULES.md): wraps collapse into one `OverdubPassAdded`; record stays `RecordPassAdded`). This slice proves the stack, then fixes only if the device has one slot entry.

---

## RC2b — same-NoteId closure overwritten by wrap-head reconstruction

**Semantic invariant:** a note whose same `NoteId` has a NoteOff in the same capture pass is closed at that NoteOff. No later display reconstruction may reinterpret it as wrap-held. Intermediate representations may transform coordinates; closure identity and effective endpoint must not change.

```text
same-pass Off exists     → closed; cannot become wrap-held
same-pass Off absent     → may become wrap-held
```

Forbidden: `id A On@2304 Off@2880` → `2304→1008` or `2304→loopLength-1`.

This is not a pairing problem. Journal/gather A!=B already keeps a closed linear end. Do not reopen `findOpenPassNote`.

**Owner after the fixture names it.** Candidates to trace, not to patch in advance:

- `appendNextPassChangeFragment` / `appendOpenNoteChange` — only if the fixture shows a journal Add/Replace of id 85 with `end=432`
- Display gather wrap-head continuation in `DisplayNoteResolveLiveCapture.cpp`
- Visual-cache wrap compose (existing native: `test_visual_cache_keeps_overdub_wrap_held_without_stretching_record`)

**Do not:** change `updateOpenPassNoteFromEvent` / `findOpenPassNote`; painter span-split; treat this as overlay.

**Host pin (write first, expect red on current tree if the rewriter is host-reachable):**

```text
parent/snapshot: id 85 pitch 64 start 2304 end 2496  (closed, not wrap-held)
wrap drain / gather used on device after wrap 2 publish
assert: id 85 end still 2496 (or the captured same-pass Off)
assert: not 432, not wrap-held
```

Then extend only the function the fixture names.

**Host pin result (2026-09-01):** `test_closed_same_note_id_keeps_linear_end_after_wrap_two_gather` and
`test_reconstruct_closed_bar_four_keeps_linear_end_beside_bar_one_sibling` are **green**.
Wrap journal drain, snapshot gather, full visual-cache rebuild, and reconstruct
(with and without `overdubPassWrapPairing`) keep `2304→2496`. Do not write pairing
firmware against those paths. The remaining constructor is the draw-source
rewrite (`draw_src` composed `2496` vs drawn `432`) and is not host-reachable
without DisplayManager/Track. No RC2b firmware from this pin.

**Device [`181116`](../../captures/session_20260901_181116.log):** LCR / source-view
keep the linear close (`id=74` `2304→2880`, `id=82` `2304→2496`). `draw_src`
still rewrites the same `NoteId` (`74` drawn `2304→1008`; `82`/`98` drawn
`2304→960` / `2304→672`) and drops bar-0/1 siblings to `new,0,0,0`. Same
constructor as 172532. No pairing firmware.

**Same-pass host pin (write next, expect red):** one pass with `A 2304→2880`, sibling `B 768→1008`, and open `C On@2304` with no Off. Assert `A` stays `2880`, is not `1008` or `loopLength-1`, and `C` may be wrap-held. Earlier separate-wrap fixtures were green and cannot reproduce this. Extend only the function this pin fails.

**Same-pass host pin result (2026-09-01):** `test_same_pass_closed_note_keeps_linear_end_beside_bar_one_sibling`,
`test_same_pass_closed_bar_four_keeps_end_beside_hold_head` (`190847` ticks
`2304→2976` / `0→576`), and `test_same_pass_snapshot_keeps_closed_bar_four_end`
are **green**. `applyCaptureEventToPreview`, `reconstructDisplayNotes` with
`overdubPassWrapPairing`, and wrap-journal snapshot membership keep the linear
close. Do not write pairing or reconstruct firmware against those paths.

**Device [`190847`](../../captures/session_20260901_190847.log):** first wrap
`draw_src` still rewrites `id 77` compose `2304→2976` to drawn `2304→576` and
drops bar-0 siblings (`id 79` `0→576`, `id 80` `48→96`). Compose remains the
linear close. The remaining writer is `resolveDisplayNotesByDrawSource` (the
`new` side of `draw_src`), not `findOpenPassNote`. No RC2b firmware from this
pin.

**Draw-source host pin result (2026-09-01):**
`test_same_pass_draw_source_keeps_closed_bar_four_end` is **green**. The
committed+capture bar union inside `resolveDisplayNotesByDrawSource` (snapshot
membership, visual-cache intersect, sealed upsert, `selectDrawSourceForBar`
capture union) keeps `id A` `2304→2976` on the `190847` same-pass wrap. Do not
write pairing, reconstruct, or gather-union firmware against that path. The
function still applies `applyCapturePlayheadTails` after the union; that step
is DisplayManager-only and is not in this pin.

**Playhead-tails host pin result (2026-09-01):**
`test_same_pass_playhead_tails_keep_closed_bar_four_end` is **red**. Overdub
`applyCapturePlayheadTails` (`allowWrapContinuation=false`) matches the first
capture-suffix row at pitch+start. With `192435` rows `A 2296→2976` and open
`C On@2296` in that suffix, `A` becomes `2296→2296` (playhead `1720`). That is
not the device `draw_src` end `672`. Do not patch tails for `672` from this
pin. Do not patch reconstruct, `findOpenPassNote`, or the gather union.

---

## RC4 — occupy the span that is actually sounding

**Invariant:** if a wrap-held span is authoritative, `collectOverdubSourceHoldParticipantIds` at `hs=0` / `hs=48` returns that `NoteId` and `resolveOccupancyAtNoteOn` emits Shorten. If RC2b forbids wrap-held, occupy `a=0` at those holds is correct and must stay visible.

**Owner:** `Loop::collectOverdubSourceHoldParticipantIds` / source-view rebuild — population of `overdubSourceViewNotes_`, not a new match key and not `displayNotePresentAtHold` (unwrap already handles `end < start`).

**Sequence:** RC2b first. Then host fixture with the **device’s source-view rows** at `holdStart=0` and `48` (use existing `lcr,nopart` rows). Fail-loud if the device still reports `n=0` / `a=0` while a wrap-held row is in that view.

Remaining after RC2b (do not absorb): first-hit `64@hs=768` `n=0` (same class as 155630 bar 2). Separate fixture; still no invented Hide.

**Host pin result (2026-09-01):** linear `2304→2496` in the source view is absent at `hs=0` / `hs=48` (correct after RC2b). True wrap-held `On@2304 Off@432` reconstructs as split head+tail; same-`NoteId` merge kept only the tail, so occupy missed `hs=0`. Population now collapses that split into one `end < start` row in `appendLastCommittedOverdubPassToSourceView` / `rebuildOverdubSourceView`.

**Device [`181116`](../../captures/session_20260901_181116.log):** after wrap 1,
`64@hs=0` is `n=0` with source-view rows `72` `1536→2112` and `74` `2304→2880`
(linear; correctly absent). After wrap 3, wrap-held `90` `2296→0` is in the
view and occupies `hs=2304` (`a=1`); `hs=0` stays `n=0` because
`displayNotePresentAtHold` is exclusive-end (`[2296, 3072)` does not contain 0).

`hs=768` and `hs=0` stay `n=0` on every pass in 155630. That is why bar 2 was never resolved in the ledger — only covered by live capture. Out of RC2/RC3/RC2b.

---

## RC5 — two GUS entries after overdub stop

**Invariant:** after record + one overdub session + stop, the **slot** stack is `RecordPassAdded` then `OverdubPassAdded`. First **U:** disables only session wrap `passIds`. Second **U:** disables the record pass. Wraps stay one `OverdubPassAdded` (DEC-038.2).

**Owner:** existing `TrackUndo::pushRecordPassAdded` + `pushOverdubSessionOnStop`. Skipped stop already pushes session ids only (`kInvalidPassId`, `includeLastPass=false`). Do not add a third kind. Do not put the record `passId` into `overdubSessionPassIds_`.

**Prove first (no firmware until the fixture or a GUS log shows one slot entry):**

1. Host: `pushRecordPassAdded` → two `pushOverdubSessionPass` → `pushOverdubSessionOnStop` on the Skipped signature → assert two slot entries, kinds, and `passIds.size()==2` on the overdub entry.
2. Device: one `#CAP` line after stop: `kind` + `passId` + `passIds_n` for the top two **slot** entries (not track-global `undo_entries=4`).
3. Recapture: `STOPPED`, then one **U:**. Expect overdub notes gone, record notes still painted, and `Overdub undone` once.

Firmware only if that pin shows a single slot entry (who dropped `RecordPassAdded`) or `OverdubPassAdded.passIds` contains the record id.

**Host pin result (2026-09-01):** `collectOverdubSessionUndoPasses` after two wraps is two session ids and does not contain the record `passId`. The constructed slot stack is `RecordPassAdded` then `OverdubPassAdded` with `passIds.size()==2`. No undo-routing firmware. `pushOverdubSessionOnStop` now emits `DIAG,gus,slot,kind,passId,passIds_n` for the top two slot entries so a recapture can prove the device stack.

**Device [`181116`](../../captures/session_20260901_181116.log):** after four wraps + published stop, slot log is
`kind=1,passId=18,passIds_n=5` then `kind=0,passId=1,passIds_n=0`. First **U:**
`Undo (entries=2)` / `kind=1`. Second **U:** `Undo (entries=1)` / `kind=0`.
Triple-press redo restores `kind=0` first. Record is a second slot entry.
No undo-routing firmware.

### RC5b — first U still paints overdubs

U1 disables the five session `passIds` (`kind=1`). Visual cache then `stale_all` 74 → OverlayRejected Fallback → `slice_keep` 74 → `DISP` 74. [`shouldKeepVisualCacheOverSmallerFullLoopSlice`](../../include/Utils/DisplayWindowUtils.h) treats the smaller Active-only gather as incomplete.

**Invariant:** after `OverdubPassAdded` undo, painted notes are the Active record pass only.

**Guard:** pass-state refresh **and** gather complete for the selected Active state → replace with that result. Do not bypass keep-larger on pass-state change alone. Do not weaken the global keep-larger predicate (`000013` / `234233`).

**Host pin (expect red):** before U1 cache has record+overdub; after disable, selected Active gather is complete, `noteIds ∩ disabledPassNoteIds == ∅`, and replacement is permitted.

**Host pin result (2026-09-01):** `test_pass_state_refresh_replaces_cache_when_selected_active_gather_is_complete` was red on `TEST_ASSERT_NULL` for the disabled overdub `NoteId` after `refreshVisualCacheAfterPassStateChange` — `slice_keep` retained the larger pre-undo cache. Completeness and disjointness already held.

**Fix:** `refreshVisualCacheAfterPassStateChange` sets `visualCachePassStateRefreshActive_` while it fills idle slices. `rebuildVisualCacheIdleSlice` replaces a smaller full-loop slice when that flag is set **and** the selected-state gather is complete (`usedSnapshot` / `usedPrepared` / `fallbackGatherComplete`). `shouldKeepVisualCacheOverSmallerFullLoopSlice` is unchanged.

**Device [`190847`](../../captures/session_20260901_190847.log):** first **U:** `Undo (entries=2)` / `Overdub undone` / `DISP` 84→63. Wrap lengthen remains RC2b (`id 77` compose `2304→2976`, drawn `2304→576`).

---

## Architecture checkpoint (RC2 + RC3 + RC2b / RC4 / RC5 / RC5b)

| Question | RC2 / RC2b | RC3 | RC4 | RC5 |
|---|---|---|---|---|
| Ownership change? | NO — extend the named rewriter | NO — Loop already owns companions; Display still consumes a bounded overlay | NO — source-view population | NO — existing GUS kinds |
| Transition change? | NO | NO — wrap still seals then clears pending | NO | NO — record stop / overdub stop already push |
| Unbounded interval owner? | NO | NO — apply existing bounded companion list | NO | NO |
| Reuse | YES — after fixture names the owner | YES — `lastOverdubSealCompanionIds_` → `PendingNoteChange` overlay → `applyPendingHideAndShortenToNotes` | YES — `collectOverdubSourceHoldParticipantIds` | YES — `pushRecordPassAdded` / `pushOverdubSessionOnStop` |

---

## Expected recapture

After RC2 only:

```text
id A:
    start = 2304
    end   = captured same-pass NoteOff
    not wrap-held
id B:
    start = 768
    end   = 864
```

After RC3 (bar 4 only):

```text
overdub wrap
  pending Hides sealed into companions
  pending cleared
  snapshot still stale
      → paint applies Loop's sealed companion overlay
      → bar 4 stays hidden
  snapshot publishes with companion state
      → overlay stops
```

Bar 2 will still restore until RC4, and that must be visible so the residuals stay honest.

After RC2b: id 85 stays `2304→2496` through wrap-2 drain/gather/draw; not wrap-held `2304→432`.

After RC4: if wrap-held remains authoritative, `hs=0` / `hs=48` occupy that `NoteId`. If RC2b forbade wrap-held, `a=0` at those holds stays visible.

After RC5: slot stack is `RecordPassAdded` then one `OverdubPassAdded` with session wrap `passIds`. One **U:** removes only overdub.

After RC5b: first **U:** painted notes are Active record only (`slice_keep` does not retain Disabled-pass notes). Second **U:** empties the slot.

---

## Parked — reopen if recaptured (RC2b / `79@240`)

Not Now implementing after [`204043`](../../captures/session_20260901_204043.log). Classic wrap-held rewrite (`2296→672`, `2304→2976`) is absent there. Host pin `test_same_pass_playhead_tails_keep_closed_bar_four_end` stays red (`2976` → `2296`); do **not** patch `applyCapturePlayheadTails` from that pin.

| Gate | Reopen when a future capture shows |
|------|-------------------------------------|
| Wrap-held rewrite | `draw_src` or frame turns a closed same-`NoteId` into wrap-held (`2296→672`, `2304→2976` / `576` / `1008`) |
| Dual Off `79@240` | compose/effective end **272** and drawn/frame end **288** with `SEVT` `N@240` `F@272` `F@288` (no overdub of 79). Occupy cannot explain this ([`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) Shorten never lengthens) |

Do not reopen RC2b because reconstruct still stacks same-start lengths.

---

## Tests

- RC2: pin-first fixture in `test_loop_take_survival` (closed A then closed B, drain, assert IDs and ends).
- RC2b: closed `2304→2496` same `NoteId` after wrap-2 drain/gather stays `2496`, not `432`.
- RC2b draw source: `test_same_pass_draw_source_keeps_closed_bar_four_end` keeps `2304→2976` through the committed+capture union.
- RC2b tails: `test_same_pass_playhead_tails_keep_closed_bar_four_end` is red (`2976` → `2296` by pitch+start). Device `draw_src` is `672`.
- RC3 host seam: IDs derive overlay; Hide(`123`) removes A(`123`); Hide(`456`) leaves A; R=9 requires overlay, R=10 and R=11 do not.
- RC3 journal: missing parent target does not drop the operation; apply is idempotent.
- RC3 negative: Hide of an already-absent target does not resurrect it.
- RC4: source-view rows at `holdStart=0` and `48`; fail-loud only if a wrap-held row is present and occupy still returns empty.
- RC5: two slot GUS entries after record + session wraps + Skipped stop; overdub `passIds.size()==2` and does not contain the record id.
- RC5b: after disable, complete Active gather is disjoint from disabled ids and `refreshVisualCacheAfterPassStateChange` replaces the larger cache.
- `pio run -e teensy41-capture-serial` RAM1 locals >= 4000, then `pio test -e native`.

---

## Pre-implementation review (RC5b / RC2b, 2026-09-01)

### Ready
- RC5b owner is `refreshVisualCacheAfterPassStateChange` → `rebuildVisualCacheIdleSlice` keep-larger.
- RC2b tails pin is red at playhead `2296`, not device `672`. Writer of `672` stays unnamed.

### Resolved
| Topic | Decision |
|---|---|
| Keep-larger | do not change `shouldKeepVisualCacheOverSmallerFullLoopSlice` |
| Completeness | replace only when pass-state refresh and selected-state gather is complete |
| Pin home | `test_loop_take_survival` |

### Open before coding
1. RC2b owner — pin-down only.

### Proceed?
YES.
