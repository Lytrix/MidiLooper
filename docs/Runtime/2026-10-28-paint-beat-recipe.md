# HITL recipe — `#PAINT_BEAT` (DEC-187 diagnostic)

Filled from [HITL_TEST_RECIPE.md](../Templates/HITL_TEST_RECIPE.md) for the frozen-panel report
(`captures/session_20261028_225450.log`). Instrumentation only: no behaviour change, no stimulus.

| Field | Fill in |
|---|---|
| Acceptance criterion (one sentence) | One `#PAINT_BEAT` row per second for the duration of a frozen panel decides whether the **painted tick** follows the playhead (paint input frozen) or matches it while the panel stays stale (transfer/panel path). |
| Reused stimulus / flow (`stimulus list`) | None. The owner's manual reproducer is the scenario: play, two or three overdubs, stop the last one, leave it playing ~20 s, then transport Stop. No canonical stimulus is edited and no new `STIM-…` is promoted. |
| New stimulus? variant name | No. |
| Gestures used | Manual buttons by the owner (no scripted MIDI, so `control_constants.py` is not involved). |
| Expected markers | `#PAINT_BEAT,` — tier: **other** (direct `Serial.printf_P`, not a `#CAP` record, so it cannot be shed). Max rate 1/s. Carries `paint`/`play` ticks, `notes`, pending `phase`, `xfer`, `dma`, plus **`out=`** (note-ons emitted since the previous row, from `DebugSessionCapture::midiOutNoteOnCount`), **`last=`** (last emitted pitch), and — for DEC-188 — **`dirty=`** (`Loop::visualCacheDirty`) and **`vcache=`** (`visualCache.notes.size()`), so a frozen painted set is attributable: `dirty=1` with stale `notes` means the lane never drained the marked bars; `dirty=0` with `vcache` still holding the undone rows means the rebuild ran against a stale source. Emitted from `DisplayManager::reportPaintBeatTransport` (`src/DisplayManager.cpp`), called from the capture-gated per-update hook beside `reportDisplayDmaTransport`. |
| Marker tier test | n.a. — not a `#CAP` record, so `CaptureLineTier`/`test_capture_line_tier` do not classify it. The existing `test_capture_ring` 38/38 and `test_display_dma_health` 9/9 stay green (no ring contract touched). |
| `check_marker_contract.py` output | n.a. for the same reason: that pre-flight checks `#CAP,…,DIAG,<tag>` records. The row is un-timestamped in the `#CAP` sense (its second field is `millis()`), deliberately: `#DMA_XFER` in this same function is proven visible in both the owner's reader and a raw boot read. |
| RAM1 impact | Instrumentation is capture-gated and FLASH-resident; state (`gPaintBeat`, the note-on counter/last pitch) is `DMAMEM` (RAM2) and treated as NOLOAD (armed magic, clamped delta). Padding 2,232 → **1,832**, `variables` **96,192 unchanged**, 992 B above the floor. |
| Native test that fails without the change | n.a. (report-only row). Verified instead by `pio run -e teensy41-capture-serial` SUCCESS + budget gate. |
| Boot-census / smoke check | One bounded serial read after boot: boot lines appear, and **no** `#PAINT_BEAT` before playback (`isAudiblePlaybackActive() || isRecording()` guard). |
| Full scenario entry point | Owner manual gesture with their capture reader; bound the observation at ~60 s. |
| Verdict rule | `paint` lagging `play` by many ticks ⇒ the **paint input** is frozen (resolve/window lane). `paint ≈ play` with `dma` advancing while the panel looks frozen ⇒ the **transfer/panel** path. `phase=3` with `xfer=1` across rows ⇒ a busy transport never completing. `phase=0` with `paint` frozen ⇒ no frame is being admitted at all. |
| Evidence to record | Reader log tail; `grep '^#PAINT_BEAT' <log>`; the matching `#DMA_XFER` and `RING,` rows; `RING,stats` presence decides whether any `#CAP` absence is evidence. |

## Why the previous capture could not answer it

`captures/session_20261028_225450.log` lost rows: `RING,shed,15069` and
`RING,tier_a_evicted,13` (first Tier-A eviction at 168.19 s), no `RING,stats` row. `DISP,3` only fires
when the note count / state / loop length change (`DisplayNoteResolve.cpp`), so its silence is expected
while the count is steady. `#DMA_XFER` shows `dma_frames` advancing 2396 → 2906 with `timeouts=0`, so the
transport was **not** stuck — the question is what the paint path handed it.

## `#STOP_TRACE` (DEC-189)

Second un-sheddable row, same rules (direct `Serial.printf_P`, at most 1/s, emitted from the 2 s diagnostics
turn in `src/main.cpp` while a stop job **or** a publication is pending):

```
#STOP_TRACE,<ms>,stop=<0|1>,mode=,phase=,gen=,order=,canon=,ranked=,undo=,pub=,q=,a=,if=,passes=,len=
```

Verdict rule: `phase` flat across rows with `stop=1` means the deferred job is being serviced but not
advancing (DEC-189's Record case); `pub=1` with `q`/`a` flat and `if=1` means the publication unit is in
flight and not progressing. `passes=0` with `len>0` after a stop is the "nothing committed" signature the
display then holds on.

RAM1: the rate stamp is `DMAMEM` (`gStopTraceLastMs`); a plain static cost +32 B of DTCM and the budget gate
refused it. Padding 1,832 -> **1,688**, `variables` **96,192** unchanged.
