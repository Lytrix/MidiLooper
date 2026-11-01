# B.2b — seeded differential sequences under DEC-244

Date: 2026-11-01. Host-only; no firmware change. Suite: `test/test_revision_store_differential`
(`test_seeded_sequences_store_exact_and_production_multiset_under_dec244`).

## Set-up

- 40 seeds × 12 steps after a Record. Each step is an overdub commit (~68 %), an undo of the newest active overdub
  (~20 %, when possible) or a redo (~12 %, when possible). A commit after an undo discards the redo branch.
- Production path per step: `beginCapture` → `appendCaptureEvent` → `commitCapturePass`; undo/redo is
  `setCapturePassState(id, Disabled / Active)` (what the global undo stack drives); then `requestCommittedPassesRebuild`
  and publication until current; oracle `tryCopyCurrentSnapshotEditViewEvents` (B.2a).
- Store path: each pass is fed production's committed chunk rows; undo/redo are `Store::undo` / `redo`.
- Content per pass: 2–7 notes with distinct pitches (no same-pitch overlap inside a pass, so no span-less notes),
  lengths 24–384 ticks that may cross the wrap, and 0–3 sustain events on a 24-tick grid. Half of them are a
  same-tick `CC64 127` then `CC64 0`. Channels rotate 1–3, so passes collide on channel, tick and pitch.

Expected order: DEC-244 (tick, NoteOff < NoteOn < other, pass commit order, committed row order).

## Result

| Measure | Value |
|---|---|
| Revisions compared | 520 (75 undos, 22 redos) |
| **Store vs DEC-244, exact order** | **520 / 520 identical** |
| Production vs DEC-244, content (multiset per tick/class group) | identical once the cross-pass collapse below is modelled |
| Rows removed by production's cross-pass collapse | 16, in 12 revisions |
| (tick, class) groups compared, summed over revisions | 27,187 |
| Production groups not in DEC-244 order | 934 (NoteOff 279, NoteOn 293, other 362) |
| **Of those, a controller ends on a different value** | **352** |

The counts are summed over revisions, so a group that persists across later revisions is counted once per revision.

## Findings

1. **Audible: production reverses a same-tick sustain release.** One pass appends `CC64 127` then `CC64 0` on one
   tick, and the committed rows keep that order. Production's effective view emits `CC64 0` then `CC64 127`, which
   leaves the pedal **down**. First case: seed 1, step 7, tick 2544, channel 3. Cause:
   `LoopContentResolution` `resolvedEventLess` compares tick, type, channel, data1 and noteId, so it ties on the
   value, and `std::sort` is unstable. The B.2a probe kept the order only because its group was small. This is shown
   on the content-authority surface. Whether the playback window inherits it is not yet measured (the window build
   re-sorts with its own key; B.2c or a device check decides).
2. **Production collapses identical events across passes** (`midiEventMatchesSnapshot` / `eventAlreadyInSnapshot`,
   `LoopSnapshotPublish.cpp`). A later pass's row is not added when an earlier pass already holds one with the same
   tick, type and channel, plus the same CC number and value for CCs, or the same pitch for notes where either
   noteId is missing. Under DEC-244 that is a semantic divergence and can be audible: if pass A holds `CC64 0` and
   pass B holds `CC64 127, CC64 0` on one tick, production keeps A0, B127 (B0 is collapsed), so the pedal ends down.
   Program changes and other non-CC, non-note types match on tick, type and channel alone, so a second pass's
   different program on the same tick would be dropped (by code reading; not generated here). Owner decision:
   keep the collapse as semantics (and make the store adopt it), or treat it as a production defect.
3. **NoteOn/NoteOff order inside a same-tick group** differs from pass and append order in 572 group-revisions.
   It is ordered by channel and pitch instead. Simultaneous NoteOns on the same tick are rarely audible, but this is
   still a DEC-244 conformance gap.
4. Undo/redo and branch-after-undo show no content divergence: disabling a pass in production and moving the store's
   cursor give the same multiset every time.

## Not covered yet

- Live overdub sessions with pending note changes (`Hide` / `Shorten` — overlap-resolve) → B.3.
- NOTE_EDIT passes → B.3.
- Playback-window order (as opposed to the effective view) → B.2c / device.
- Shrinking: the harness reports seed and step of the first divergence; automatic minimisation is not built.
