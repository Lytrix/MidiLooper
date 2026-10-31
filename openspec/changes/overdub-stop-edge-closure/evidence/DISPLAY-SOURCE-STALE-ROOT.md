# Parked checkpoint — stale published roots no longer override committed passes (2026-10-31)

**Status: implemented, pinned, parked uncommitted.** The working tree carries the change; it is staged in the
index as the exact display hunks, but it is deliberately **not committed** yet (see "Why parked").

> Stale published roots no longer override newer committed passes for draw-source gathering. Current roots
> remain root-backed. Long-chain committed-pass corruption remains independently RED and is not caused by this
> change.

## The defect (chain-48, capture `session_20261031_001418.log`)

Committed passes advanced while the published root stayed at the record-only revision. `visualCacheDirty`
made `shouldGatherPublishedSnapshotForDrawSource` prefer that stale root, so `gatherCommittedDrawSourceBars`
served the record pass only. Committed overdub rows were visible only through the transient live/stop preview
tiers, so they vanished and reappeared as the detailed window slid. Evidence in that capture:

- `draw_src,layers` during the whole chain: `visual=316, snap=1, preview=N` — the committed base is the record
  pass, previews supply every overdub row.
- `PAINT_BEAT`: `vcache=316, dirty=1` for every frame after the first window.
- `snapshot_unit`: 62 starts / 34 finishes / **1 publish**, immediately before the windows.

## The fix (`src/DisplayManager/DisplayNoteWindowGather.cpp`)

- `requestedSnapshot && rootStale` never uses the root. The fallback is the **window-local, wrap-aware**
  `LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, …)`.
- Every other selection is unchanged (current root, stopped-Record preview, pass-derived cache).
- A cache built from a root mirrors that root and is not a qualified committed source; a cache built from
  passes stays the partial-cache source.
- Overlay rows (sealed, retired-sealed, stop/live preview) are skipped when the committed base already owns
  that NoteId+start, so a just-committed pass cannot paint twice.
- `drawSourceCommittedPassGatherCountForTest()` counts the pass-branch gathers; the current-root control
  asserts it stays 0.

## Proofs

- `test_stale_root_draw_source_serves_committed_overdub_passes_across_the_sliding_window`
  (RED before the fix, GREEN after): stale empty root + record + overdub A (bar 5) + overdub B (bar 2);
  windows at bars 0/1/4/8 and a wrap-crossing window (18–33) must serve each committed NoteId with its exact
  geometry from the committed gather, with no preview tier.
- `test_current_root_draw_source_stays_root_backed_and_never_walks_the_passes` (negative control): current
  root + dirty cache → root content only, pass counter = 0.
- `test_live_capture_display_boundary`: 49/49 after the separate content repair below.

## Independent RED — resolved by DEC-234 (separate change)

`test_overdub_chain_preserves_frame_progress_long_loop` read the committed pass instead of the stale root and
exposed a pre-existing row corruption in the Record pass from the 24-bar / 22-overdub native chain:

```
record pass rows: 12/N|1  12/N|24  48/F|1  780/N|2 ...      (47 rows, expected 48)
id 24 (bar-23 pair) has only: 12/N     <- orphan On at the loop start, no Off
```

Reproduced with the **previous** gather too, so it was independent of this display change; the stale root had
been masking it. Traced by `committed-pass-identity-corruption` to the Record stop length rule: the last event
48 ticks inside bar 23 was treated as a bar-line row, the length trimmed to 23 bars, and the projection wrapped
the NoteOn while dropping the NoteOff. **DEC-234 — a record stop keeps the bar that contains the last event**
fixes the length rule; the sealed pass now holds 48 rows, the 24-bar chain is green, and the display suite is
49/49. This display-source repair's own two fixtures remain its proof; the chain oracle was not weakened.

## Why it was parked — and why it is now commit-ready

The display area in this working tree depends on pre-existing uncommitted work: `windowCoverageLoopBar`
(now inlined for self-containment), a pre-existing test function/registration in the display suite, and the
RAM1 placement. The staged-only build initially failed the target budget at `variables 96,224` vs approved
`96,192` because the new `gDrawSourceCommittedPassGathers` counter sat in DTCM.

**2026-10-31 — resolved without a re-base:** the counter is now `DMAMEM` (RAM2), and the display-only staged
build (HEAD + these hunks, no DEC-233/DEC-234) is **SUCCESS at the approved placement** (`variables 96,192`,
`locals 34,880`, ITCM `391,976 + 1,240`, 400 B above the floor). The separate 4,096 B DTCM transfer from the
DEC-233 diagnostic strings is carved to FLASH in the linker (`*MidiButtonActions*.o(.rodata...)`), so the full
working tree is also SUCCESS at 96,192. The staged index is the exact display checkpoint; it can be committed
as its own slice.
