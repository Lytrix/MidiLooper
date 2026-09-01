# Overdub wrap — pairing, companions, occupancy

**Status:** RC2 fixture-closed (no pairing firmware). RC3 journal host+firmware in tree. Overlay paint not started. RC4 separate.
**Date:** 2026-09-01
**Kind:** bugfix
**Evidence:** [`155630`](../../captures/session_20260901_155630.log), [`153517`](../../captures/session_20260901_153517.log)
**Parent:** [`display_seal_handoff_coverage_and_wrap_pairing_bugfix.md`](display_seal_handoff_coverage_and_wrap_pairing_bugfix.md)

**Debugging boundary:**

```text
RC1 builtFromRevision over-claim
    ← committed c74b8e21; not on the 155630 board
sealed capture retention
    ← landed; replaces same pitch+start only
RC2 wrap-journal / pass pairing
    ← 155630 paints 2304→576 / 2304→144; 153517 paints 2304→864
RC3 sealed companion overlay across publication lag
    ← bar 4 Hide exists, then pending is cleared while snapshot is stale
RC4 occupancy never resolves hs=768 / hs=0
    ← no Hide exists to carry; capture only masked the old row
```

RC3 must not absorb RC4. If occupancy never created a Hide, no seam overlay may manufacture one.

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

Overlay paint is a later RC3 slice. RC4 remains untouched.

**Negative (overlay is not a second content store)**

```text
Hide(A)
Hide(A) again     → still no A
Hide(A) after A already absent → still no A; no resurrection
```

**Does not authorize:** changing occupancy lookup, changing the capture-retention release predicate, holding `capturePreview` past the existing retention rule, or making the companion overlay mutate, replace, or become authoritative storage for committed loop content. The overlay exists only because publication is asynchronous.

If bar 2 disappears as a side effect of RC3, RC3 has absorbed RC4 and must be rejected.

---

## RC4 — occupancy at 768 / 0 (separate)

`hs=768` and `hs=0` stay `n=0` on every pass in 155630. That is why bar 2 was never resolved in the ledger — only covered by live capture.

Do not design the match key here. Host fixture with the device’s source-view rows at `holdStart=768`, then fail-loud if the device still reports `n=0`. Out of RC2/RC3.

---

## Architecture checkpoint (RC2 + RC3)

| Question | RC2 | RC3 |
|---|---|---|
| Ownership change? | NO | NO — Loop already owns companions; Display still consumes a bounded overlay |
| Transition change? | NO | NO — wrap still seals then clears pending |
| Unbounded interval owner? | NO | NO — apply existing bounded companion list |
| Reuse | YES — journal / pass open-note close | YES — `lastOverdubSealCompanionIds_` → `PendingNoteChange` overlay → `applyPendingHideAndShortenToNotes` |

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

---

## Tests

- RC2: pin-first fixture in `test_loop_take_survival` (closed A then closed B, drain, assert IDs and ends).
- RC3 host seam: IDs derive overlay; Hide(`123`) removes A(`123`); Hide(`456`) leaves A; R=9 requires overlay, R=10 and R=11 do not.
- RC3 journal: missing parent target does not drop the operation; apply is idempotent.
- RC3 negative: Hide of an already-absent target does not resurrect it.
- `pio run -e teensy41-capture-serial` RAM1 locals >= 4000, then `pio test -e native`.
