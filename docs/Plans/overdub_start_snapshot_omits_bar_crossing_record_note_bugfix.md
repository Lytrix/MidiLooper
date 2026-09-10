# Overdub start — current snapshot omits bar-crossing record note

**Status:** Stage 6 SHIPPED (last consumer containment) — HITL `base` PASS
[`210310`](../../captures/session_20260910_210310.log): **id26→0 = 0**, **id27→0 = 0**
(was sustained id26 hide on [`205031`](../../captures/session_20260910_205031.log)).  
**Architectural marker:** Stages 1–6 are **containment/diagnostic only**. They must
**not** be accepted as proof that snapshot membership is architecturally complete.
End-state invariant and publish RC:
[`overdub_snapshot_bar_crossing_membership_bugfix.md`](overdub_snapshot_bar_crossing_membership_bugfix.md).

**Next:** upstream membership RC (why published snapshot omits the NoteRef), then
unwind Stage 5–6 compensations.  
**Fail pin (OD open omit):** [`session_20260910_194531.log`](../../captures/session_20260910_194531.log)  
**Parent / adjacent:** [`overdub_live_wrap_playhead_tail_display_bugfix.md`](overdub_live_wrap_playhead_tail_display_bugfix.md) Option C (snapshot membership).

## Debugging boundary

```text
… → record reconstruct / visualCache (32 notes)     ← trust
 → published LoopSnapshot span membership           ← incomplete (31)
 → shouldGatherPublishedSnapshotForDrawSource       ← selects snapshot
 → shouldRetainUnpublishedVisualCacheOverStaleGather ← refuses retain when current
 → OLED / DISP frame                                ← drops one note
 → rebuildOverdubSourceSpanCache from=snapshot      ← skips mergeOmitted
 → STOPPED reconstruct                              ← note returns
```

Do **not** change DEC-051 seal, Occupy Hide/Shorten owners, or wrap Stage 2b upsert.

## Symptom

At first and second **OVERDUBBING** start, a record-pass note that **crosses a bar boundary** disappears from LOOP_EDIT OLED, then reappears at STOPPED.

User landmark: near `02:04:03:12` / pitch 73. Capture identity of the dropped row:

| Field | Value |
|---|---|
| NoteId | 27 |
| Pitch | **74** (adjacent to 73 @ 1452) |
| Span | 1500–1554 (crosses bar 3 @ 1536) |
| Sounding length | 54 |

Pitch **73** @ 1452–1500 stays in `srcspan`.

## Proof ([`194531`](../../captures/session_20260910_194531.log))

1. After record settle: `VCACHE` notes **32**; `DISP` PLAYING frame=32.
2. OD1 open: `lcr,src,why=open,from=snapshot,…,notes=31` — `srcspan` gaps `id=26 p=73` → `id=28 p=75` (missing id 27).
3. Same gap at OD2 open (`from=cache`, still no id 27).
4. `draw_src,diff,…,id,27,old,74,1500,1554,new,0,0,0` with `hidden=1` while `visual=32`.
5. Brief `DISP` OVERDUBBING `frame=31` vs `visual=32`.
6. STOPPED `DNTE` reconstruct / effective / frame all include `74,1500,…,54`.

Contrast [`193615`](../../captures/session_20260910_193615.log): pitch 74 entirely inside one bar (1697–1762) — present in `srcspan`.

## Root cause (two consumer gates)

### A — OLED / draw-source

`shouldGatherPublishedSnapshotForDrawSource` selects the **current** snapshot during overdub.  
`shouldRetainUnpublishedVisualCacheOverStaleGather` returns **false** whenever `publishedSnapshotIsCurrent` — even when this gather is **smaller** than a clean visualCache. Incomplete current snapshot wins → frame drops the note; visual still holds it (`hidden=1`).

### B — Overdub source view

`Loop::rebuildOverdubSourceSpanCache` copies snapshot spans and **skips** `mergeOmittedCommittedNoteOnIdentities` when `sourceOrigin == "snapshot"`. Omitted span identities never re-enter the source view until STOPPED reconstruct.

Snapshot publication may still omit the NoteRef (separate membership RC). This plan does **not** change publish/apply ownership; it restores consumer completeness from committed events + clean visual.

## Invariant

During OVERDUBBING (and live capture), a **clean nonempty visualCache** must not lose committed rows solely because the current published snapshot gather returned fewer notes. Overdub source span cache built `from=snapshot` must still merge NoteOn identities present in committed events but missing from the snapshot span list.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — extend `DisplayWindowUtils` retain predicate + existing `mergeOmittedCommittedNoteOnIdentities` call site |
| State transition change? | **NO** |
| Unbounded MIDI-interval owner? | **NO** — cold OD-open / full rebuild only; merge already used for non-snapshot origins |

## Stages

### Stage 1 — Paint retain (OLED)

Add `shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather` (or extend the existing retain helper with an explicit incomplete-current case):

- `captureOrOverdubActive`
- `publishedSnapshotIsCurrent`
- `!visualCacheDirty`
- `visualCacheCount > snapshotGatherCount`

Wire in `resolveDisplayNotesByDrawSource` after `gatherCommittedDrawSourceBars` (same site as unpublished retain). Pending Hide/Shorten overlays still run on the retained visual base.

Native: DWU truth table + gather fixture if present.

### Stage 2 — Source-view merge

In `rebuildOverdubSourceSpanCache`, run `mergeOmittedCommittedNoteOnIdentities` for `sourceOrigin == "snapshot"` as well (same materialize path as prepared/visual/passes).

Native: source-view / take-survival fixture — snapshot missing one closed pair that events still contain → after `establishOverdubSourceView`, note present.

### Stage 3 — Visual + adopt + full-loop snapshot reconstruct upsert (`200405`) — SHIPPED

**Proof:** OD1 source has id 27; OLED/`DISP` `visual=32` never gains 74; post-stop `adopt_partial` notes=48 without 74; OD2 `srcspan` omits id 27 (mergeOmitted alone insufficient after OD1).

1. `Loop::mergeOmittedCommittedNoteOnIdentitiesInto` — materialize + merge helper.
2. Call after snapshot resolve in `rebuildVisualCacheFromPasses`, after snapshot bar copy / snapshot-event reconstruct in idle slices, and in `adoptComposedDisplayNotesFromViewport`.
3. Full-loop `from=snapshot` also `mergeDisplayNotesByNoteId` from reconstruct (same as visual/prior origins) so OD2 source recovers identities mergeOmitted misses.

Device [`202323`](../../captures/session_20260910_202323.log): OD1 `visual=33`; id 27 in OD1+OD2; native `test_visual_cache_merges_omitted_bar_crossing_note_from_snapshot_200405`.

### Stage 4 — Do not reintroduce sealed Hide via reconstruct merge (`202713`)

**Symptom (user + serial):** At second overdub, bar-crossing note still drops from paint
(`draw_src` id 27 → `new,0,0,0` after wrap); notes that OD1 correctly Hidden/Shortened
reappear on OLED.

**Proof [`202713`](../../captures/session_20260910_202713.log):** Stage 3 full-loop
`mergeDisplayNotesByNoteId` from `reconstruct(materialize)` upserts NoteIds that sealed
Delete companions already removed from the snapshot. `mergeOmitted` from raw events does
the same. Companions were only reapplied when `!publishedSnapshotIsCurrent()`.

**Invariant:** After any event/reconstruct merge into span cache or visual notes, sealed
Hide/Shorten (and this-lap pending) must apply so intentional absences stay absent.
Publication misses (no Delete companion) still restore via `resolveDisplayNotes(passes)`
upsert + `mergeOmitted`.

**Architecture checkpoint:** Ownership NO · Transitions NO · Unbounded interval NO —
extend existing `applyPendingNoteChangesToDisplayNotes` after merge; swap reconstruct
upsert for `LoopContentResolution::resolveDisplayNotes`.

### Stage 5 — Retain dirty visual over incomplete snapshot after wrap (`204344`)

**Symptom:** Note briefly disappears between overdub loop wrap and the next lap
(playhead restart / “new overdub” feel).

**Proof [`204344`](../../captures/session_20260910_204344.log):** `VCACHE,stale*` then
`DIAG,wrap`, then `draw_src` layers `visual,50` / `hidden,2` with id 27
`old,74,…,new,0,0,0`. Stage 1 retain refused because `visualCacheDirty` after wrap
while the current snapshot gather still omitted the bar-crossing NoteRef. Sealed
preview overlays already re-enter after retain.

**Invariant:** During capture/OD, if current-snapshot gather note count is smaller
than nonempty visual, retain visual even when dirty. Pending/sealed overlays still
apply after.

**Architecture checkpoint:** Ownership NO · Transitions NO · Unbounded NO — extend
`shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather` only.

### Stage 6 — Last consumer containment: NoteId heal on draw-source (`205031`)

**Role:** Containment only — heals a **consumer view** of an incomplete snapshot,
not the snapshot itself. **Do not** treat Stage 6 as the immutable-snapshot end state.

**Symptom:** Same brief remove on **OD1 and OD2** between wrap and next lap (and
for much of OD1 while snap paint is selected).

**Proof:** Count-based retain requires `visual > gather`. When both are size 30
but snapshot omits id 26 and visual still has it, retain loses; `draw_src`
`hidden,1` for the whole OD1 growth. After wrap, gather can be *larger* than
visual (sealed adds) and still omit the bar-crossing NoteId.
`visual.count == snapshot.count` does **not** imply equal membership.

**Invariant:** After current-snapshot draw-source bar append during capture/OD,
merge omitted committed NoteOns and union missing `NoteId`s from visual before
overlays. Sealed/pending Hide/Shorten still apply after (Stage 4).

**Shipped:** HITL [`210310`](../../captures/session_20260910_210310.log) id26/id27
draw_src→0 counts **0**; native heal test PASS; RAM1 **7008**.

**Architecture checkpoint:** Ownership NO · Transitions NO · Unbounded NO —
extend `gatherCommittedDrawSourceBars` with existing `mergeOmitted` + NoteId union.

**Stopping rule:** After Stage 6 ships and HITL is stable, **no further consumer
exception stages** unless a new independent failure demands one. Next work is
upstream membership ([`overdub_snapshot_bar_crossing_membership_bugfix.md`](overdub_snapshot_bar_crossing_membership_bugfix.md)),
then remove/relax Stage 5–6 compensations where membership is complete.

### Out of scope (moved to membership plan)

- Why publish/apply omits the NoteRef — **end-state RC** (not another consumer heal)
- Option C wrap-held snapshot geometry unless membership investigation proves that path
- Making VisualCache a permanent Snapshot completeness authority

## Validation

- Focused: `test_display_window_utils`, `test_overdub_source_view` / `test_loop_take_survival` as wired
- `pio test -e native`
- HITL `base` — no `draw_src` zero-diff for record bar-crossing id; OD open `notes` matches visual committed count for that seed; STOPPED DNTE still holds the span

## Pre-implementation review

### Ready
- Owners traced: `shouldRetainUnpublishedVisualCacheOverStaleGather`, `gatherCommittedDrawSourceBars`, `rebuildOverdubSourceSpanCache`, `mergeOmittedCommittedNoteOnIdentities`
- Evidence pin [`194531`](../../captures/session_20260910_194531.log)

### Resolved
| Topic | Decision |
|---|---|
| Pitch identity | Dropped row is **74@1500–1554**, not 73@1452 |
| Fix layer | Consumer completeness first; publish membership follow-up |
| VC vs snapshot | Retain **clean** visual when current snapshot gather is smaller during capture/OD — not global VC preference |

### Open before coding
None — proceed Stage 1 then Stage 2.
