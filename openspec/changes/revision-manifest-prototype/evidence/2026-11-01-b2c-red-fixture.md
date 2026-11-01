# B.2c — the 25-bar RED fixture through the store

Date: 2026-11-01. Host-only; no firmware change. Fixture: `test_record_note_stream_survives_four_stops_before_predecessor_publication`
(`test/test_playback_prewarm`, compiled by `test_frozen_record_reader_survival`), RED baseline `44baf7be`.

## Set-up

The fixture records a 25-bar take (25 Record notes plus a closing note) and admits four overdub Stops (pitches
100–103) while the Record publication is still at the FIFO head. It then replays one full loop through the
production playback and deferred owners and captures the emitted MIDI.

After the replay, an additive block (production code and existing assertions are unchanged) feeds the committed
passes into a `RevisionStore::Store`: the Record pass, then the overdubs in `mergeSequence` order, each from its
`committedChunkIds`. It then composes every two-bar window of the resulting manifest.

## Result

| Surface | Rows | Overdub NoteOns (of 4) |
|---|---|---|
| Production replay (emitted MIDI, one traversal) | — | **1** (BASELINE, unchanged) |
| Production content authority after the replay (`tryCopyCurrentSnapshotEditViewEvents`, root current) | 60 | **4** |
| Store manifest, revision 5, union of the 13 two-bar windows | 60 (= committed rows) | **4** |

Routes during the replay: view 2,302 slot-ticks, merged 16,896, bridge/paged/none 0.

## Reading

- **The store satisfies the B.2c gate:** at the newest revision every two-bar window holds every committed row inside
  it, so all four overdubs are present. By construction, the pending Record publication cannot hide them: revision 5
  exists from the fourth overdub's commit.
- **Production's missing overdubs are a serving defect, not a content defect.** The content authority holds the same
  60 rows and all four overdubs, but the replay sounded one. This narrows the overdub-audibility investigation to the
  serving readers (merged fallback / frozen Record readers / windows), consistent with its device witness: an empty
  merged build reused for revision 7.
- **Caveat:** production's effective view was read *after* the replay. The overdubs sit in bars 0–2, early in the
  traversal, so the root may not yet have been current when those ticks were served. The fixture does not show
  whether the root was current at those ticks.

## Not covered

Whether the playback window reverses a same-tick sustain release (B.2b finding 1) is left to the separate DEC-244
conformance task, whose session did not start (disk space).
