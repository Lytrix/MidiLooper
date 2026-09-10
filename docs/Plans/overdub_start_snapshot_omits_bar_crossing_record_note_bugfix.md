# Overdub start — current snapshot omits bar-crossing record note

**Status:** Stages 1–2 SHIPPED — device HITL `base` PASS
[`200405`](../../captures/session_20260910_200405.log): OD1 open
`from=snapshot` **notes=33** (was 31); id 27 pitch 74 @ 1512–1570 present;
no `draw_src` zero-diff for pitches 70–80; DISP no frame=31 drop.  
**Evidence (fail pin):** [`session_20260910_194531.log`](../../captures/session_20260910_194531.log)  
**Parent / adjacent:** [`overdub_live_wrap_playhead_tail_display_bugfix.md`](overdub_live_wrap_playhead_tail_display_bugfix.md) Option C (snapshot membership) — this RC is the **consumer** completeness fix, not DEC-051 seal / wrap head-tail.

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

### Out of scope (follow-up)

- Why publish/apply omitted NoteId 27 (journal caps, open-note flush, membership apply) — new RC if Stage 1–2 pass and device still shows snapshot-only dumps missing the span.
- Option C wrap-held snapshot geometry (parent wrap plan).

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
