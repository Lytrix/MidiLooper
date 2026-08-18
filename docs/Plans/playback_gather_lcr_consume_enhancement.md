# Playback gather LCR consume

**Status:** Decided — not now. Firmware not authorized.  
**Date:** 2026-08-17  
**Kind:** enhancement  
**Work identity:** this file. Separate from remaining `loop-content-resolution` 6.x, NOTE_EDIT hydrate, and LED lookup.  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) amendment 2026-08-17; [Playback.md](../Authority/Architecture/Playback.md)  
**Does not authorize:** firmware; a new OpenSpec change; a new DEC; putting `resolveWindow` on `handleMidiInput` or the BAR `loop()` prefix; deleting `materializeToEventVector`

---

## What this work is

Long-loop playback still gathers `mergedEvents` the current way (`ensurePlaybackWindowBuilt` / 2-bar gather around the playhead). OpenSpec `loop-content-resolution` task 6.3 named the later swap: that gather consumes prepared `resolveWindow` / `ResolvedEvent`.

This file is that **work path** so 6.3 is not leftover production-swap firmware on the LCR change. LCR stays the producer of prepared derived state. Playback stays the play consumer ([DerivedViews.md](../Authority/Architecture/DerivedViews.md) § Consumers).

```text
prepared LCR
    │
    └─ Play: resolveWindow around currentTick → mergedEvents
         miss → today’s gather
```

Same 6.0 rule: PLAYING MIDI must not construct, sort, checkpoint, or resolve LCR in order to send notes. Consume already-prepared state only. Miss keeps today’s gather. `resolveNotes` must not become the playback primitive.

---

## What this work is not

| Not | Why |
|-----|-----|
| Remaining LCR `loop-content-resolution` 6.3 firmware | Producer stays DEC-037. This is a **consumer** work item. OpenSpec 6.3 is a pointer only. |
| NOTE_EDIT hydrate | Editor analyze around `selectedTick` — [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md) |
| LED lookup | Sixteen-step / bar pads read `visualCache.notes` — [`post_undo_led_lookup_resumable_source_refinement.md`](post_undo_led_lookup_resumable_source_refinement.md) |
| 6C reconstruct on the overdub button | 6C is closed as consume-when-ready; 3b stays the fast path |
| Making all of LCR incrementally live | 6D closed the overdub-query slice only |

---

## Prerequisites (already decided)

| Prerequisite | Status |
|--------------|--------|
| LCR `resolveWindow` native | PASS |
| 6A idle display consume | device PASS |
| 6C consume-when-ready | **closed** [`205928`](../../captures/session_20260815_205928.log) / [`210508`](../../captures/session_20260815_210508.log); 3b stays |
| 6D.4 post-commit publish | **HITL PASS** same captures; not all of LCR live |

Start firmware only when this file is in CURRENT_WORK § Now implementing.

---

## When this work starts

1. Pre-implementation review against Playback.md + DEC-037 (owner `ensurePlaybackWindowBuilt`, 2-bar long-loop interval, miss path).
2. Native first. Commit each verified stage.
3. Device gate: long loop, no `PlaybackFullMaterialize` on the gather path when the stamp matches; `clockrate` holds; 6.0 still forbids construct on MIDI.
