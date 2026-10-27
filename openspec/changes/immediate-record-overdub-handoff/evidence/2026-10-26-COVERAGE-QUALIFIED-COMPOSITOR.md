# Coverage-qualified compositor — C5.6 provider inside the content hold

## Symptom this answers
`DISPLAY-HOLD-SPLIT.md` verified the split: the viewport follows the request while a content hold keeps the
retained frame. That fix left one honest hole, recorded by `COMPOSITOR-ATTEMPT-ARCHIVE.md`: **bars outside the
retained coverage stay empty even when the loop's committed content exists for them**, because the split draws
nothing the retained frame never qualified. The archived compositor attempt had tried to close it and died on a
provider balance error, leaving the display tree non-compiling and the composition code inert.

## Where the state actually was (verified before any edit)
- Probe baseline reproduced exactly as the handoff recorded: controls **4/4**, `IMMEDIATE_VISUAL_CASE=bound`
  **6/6**, `HOLD_SPLIT … retained_key=6128,12288 drawn_rows=1,2,3 content_notes=4`.
- The unregistered COMPOSITION case held a stale 24-bar/8-bar-window draft whose geometry does not reproduce.
- The capture build did **not** compile: `SC_DISP_WINDOW_TURN` was called with 9 of its 11 arguments (the
  `key=<start>,<len>` pair the archived attempt dropped). That is a pre-existing tree defect, not a result of
  this slice, and it blocked the RAM1 gate.

## The RED, written before any compositor code
`test_request_outside_retained_coverage_is_served_by_a_qualified_source`, registered under
`IMMEDIATE_VISUAL_CASE=bound`:

- take **40 bars** (30720 ticks), window **16 bars** (12288) — the maximum `kMaxDetailedWindowBars`;
- the retained frame is the growing take's window ending at tick `24 * bar`, so its coverage is
  **bars 8..24** = `windowStart 6144`, `windowLength 12288`;
- the request is driven to `start = 24 * bar` (18432) by playhead `32 * bar` — the clamped maximum
  (`length - windowLength`), wholly outside that coverage;
- committed pairs at bars 26, 30 and 34 inside the request, plus a bar-39 anchor pair the record stop's
  `min(transport, content)` length rule needs for the take to keep 40 bars;
- a fresh overdub pair at `32 * bar + 3000`.

Two turns: **turn 1** requests the interval while nothing is qualified for it, **turn 2** requests it again after
the take's committed content is made a qualified provider (`prepareQualifiedCommittedSource`, the existing
device-gate session, `deviceGateComplete(playbackRevision)`).

First run, before any compositor code — the predicted failure:

```
test_immediate_visual_cache_probe.cpp:602:
  test_request_outside_retained_coverage_is_served_by_a_qualified_source:
  (c) committed notes inside the request must be drawn   [FAILED]
```

## The change
One bounded branch in `DisplayManager::resolveDisplayNotesByDrawSource`, using the provider side that already
existed (`Loop::collectQualifiedCommittedDisplayNotes`, C5.6):

1. `syncDetailedPaintWindow` gained an optional `bool* outHeldRetainedContent` (defaulted, so the six other
   call sites are untouched) and reports when the hold substituted the retained interval for the request.
2. When the hold fired **and** the request left the retained interval, the display asks the provider for the
   *requested* interval. On success the frame is the provider's committed rows for that interval, the recognised
   live overlay is composed once on top (`applyDrawSourceOverlays`), the content key is the interval the frame
   was served for, and `DIAG,disp_turn` names the provider (`why=prepared|bound|cache`) plus the key pair.
3. On failure nothing is invented: the candidate is discarded and the retained frame keeps its honest hole
   exactly as the split left it. The retained rows are **never** stitched into a served frame, so no
   cross-generation frame can be built.
4. `Loop::collectQualifiedCommittedDisplayNotes` became `const` (its only write is the one-byte diagnostic stamp,
   now `mutable`), which removes the `const_cast` the archived attempt needed at the paint call site.
5. The pre-existing `SC_DISP_WINDOW_TURN` arity break was repaired **as designed**: `emitDisplayTurnReceipt`
   carries the key interval through to the `key=` field the header documents.

## Verification
- `COMPOSITION retained=6144,12288 request=18432,12288 hole_rows=0 served_rows=5 served_content=5 provider=prepared`
  — no qualified source ⇒ empty; qualified ⇒ 4 committed rows + the live row, each drawn exactly once.
- Probe controls **4/4**; `IMMEDIATE_VISUAL_CASE=bound` **7/7** (6 + this case); `HOLD_SPLIT` unchanged
  (`view_starts=0,2000,4000 … retained_key=6128,12288 drawn_rows=1,2,3 content_notes=4`).
- Display suite **22/22**; handoff `source` **10/10**, `stop` **4/4**, `empty` **3/3**.
- Full native **3 494 / 7 skipped / 3 487 succeeded / 0 failed** (0 errored suites; run taken after clearing
  the native build cache — two overlapping `pio test` runs in one build dir had produced spurious `ERRORED`
  suites and a phantom `transportStartResumesTrackWithPendingDeferredOverdubStop` failure).
- `teensy41-capture-serial` **SUCCESS**: `variables 96,192` unchanged, `code 389,992 + padding 3,224`, floor 840,
  **2,384 B headroom** — no re-base.
- Mutation red-proof (restored byte-identically, sha256 `b9758fa9…`): publishing the served rows while claiming
  the retained interval fails at *"Expected 18432 Was 6144. (c) the frame is claimed for the interval it was
  served for"*.

## Not claimed
- No device evidence: not uploaded, no capture, no `DIAG,disp_turn` rows read from hardware. The wire row now
  carries `why=<provider>` and `key=`, so the next capture can attribute which frame carried the content.
- Partial overlap (a request that only partly leaves the retained coverage) is served by the provider for the
  whole requested interval or not at all; a per-bar merge of retained and provider rows is deliberately not
  implemented, because it would be the cross-generation stitch the contract forbids.
- No commit, no push, no SD or library mutation.
