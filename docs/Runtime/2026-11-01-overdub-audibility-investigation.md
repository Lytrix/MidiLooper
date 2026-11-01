# Overdubs not audible — investigation (2026-11-01, open)

Owner report, 2026-11-01: "the overdubs are not audible since yesterday."

## Established (device)

- **The overdubs are recorded and committed.**
  - `disp_src` shows t7 s3 committed revision / pass count advancing once per overdub (1 → 14) in every chain.
  - The stored-MIDI verification dump at transport stop (`SEVT`, `session_20261031_151347.log`) lists **170 chord
    note-ons** (pitches 39 / 45 / 48 / 51 / 59 from `chords.mid`) in the stored take, beside the 16ths.
- **They are not played back.** Split per owner-rig chain (overdubs on t7 s3, output ch8):

  | Capture | Image | Chord NoteOns on ch8 during / after the chain |
  |---|---|---|
  | `20261030_210458`, `20261030_220124`, `20261031_000131`, `20261031_001418` | 30–31 Oct | 0 / 0 |
  | `20261031_151347` | | 0 / 15 |
  | `20261031_183306` (3 chains) | | 2 / 9, 5 / 0, 0 / 0 |
  | every 2026-11-01 chain, up to `125610` (`fa77ebdf…`) | | 0 / 0 |

- The chord notes counted earlier in `20261030_220124` (326) and `20261031_001418` (227) are **outside** the rig
  chains (45–423 s and 644–775 s of those captures). They are probably the owner's own manual sessions, which did play
  overdub-pitch content then.
- The rig's arguments are identical across all these runs (`midifile-chain.json`: same patterns, channel, windows,
  slot). This is firmware behavior, not a rig change.

## Reproduced (native)

`test_record_note_stream_survives_four_stops_before_predecessor_publication` (`test_playback_prewarm`, run by
`test_frozen_record_reader_survival`):
- a 25-bar Record take;
- four committed overdubs (pitches 100–103 in bars 0–3) while the Record publication is still pending, with the stops
  admitted behind it.

**One replay traversal sounds all 25 Record pairs but only 1 of the 4 overdubs.** The 8-bar sibling
(`…four_sequential_overdub_publications`), whose stops drain between overdubs, sounds 4 of 4.

Reader trace during the replay:
- bars 0–2 are served by the **frozen Record readers**: `origin=Capture`, view revision 0 while the loop is at revision
  1…5. These are `adoptFirstRecordingBarReturnWindow`, `adoptOrServiceOnwardRecordWindow` and the E2 frozen reader in
  `resolveQualificationPlaybackSourceForTurn`. Each window is a copy of the Record take alone.
- from bar 3 on, the merged fallback serves current content, so only the bar-3 overdub sounds.

The case is pinned as a BASELINE assertion (`overdubOns == 1`), to be inverted by the repair.

## Tried and reverted

1. **Merge committed overdub passes into the DEC-143 record-overlay gather.** In `Loop::tryGatherPublishedSnapshotEventsInWindow`,
   with no root, the Record extent is returned alone. The change was logically sound but had no native effect, and **no
   device effect**: `60c89fe5…`, `session_20261101_131433.log`, still 0 chord notes. Reverted.
2. **Refuse the frozen Record readers once an overdub is committed over the take.** This breaks the 8-bar fixture,
   whose stop and presentation owners only retire after that reader has adopted ("four-OD fixture must retire stop and
   presentation owners"). The readers are part of the Record job's retirement lifecycle, so they cannot simply be gated.
   Reverted.

The board is back on the committed image `fa77ebdf…` (boot verified, `session_20261101_132130.log`).

## Open

- **On device, which reader serves t7 s3 while the chords are missing?** The phase-2 / DEC-242 reason rows show t7 s3
  on the merged fallback after each restart, and the chords are absent there too. So on device the merged fallback also
  lacks the overdubs, through a path that try 1 did not cover. The default image's `playback_build` rows are throttled;
  a per-build source and event-count row for the merged fallback is the next witness.
- **The repair** has to hand a pending Record take's frozen readers over to a source that includes committed overdubs,
  without breaking the Record job's retirement. That is a lifecycle design point, not a one-line gate.

## Device witness (2026-11-01, image `9ddaea2c…`, `session_20261101_143559.log`)

Image: the committed tree plus the report-only `DIAG,merged_build` row. Boot verified (`boot_reset,crash=0`,
`BOOT,load,ok`). Track 8 / slot 4 was seeded with a take by hand, then the pre-12:00 rig (`94278d67`) ran unchanged:
- Record at 55.96–95.60 s;
- 12 overdubs at 110.1–152.9 s;
- the rig's content gate reported `cap=7 < 13`, so the rig does not count the run as content evidence.

t7 s3 merged-fallback builds:

| t | `req` | path | root / cov | overlay | Record pass | overdubs | `ev` built | `exp` (committed passes, same window) |
|---|---|---|---|---|---|---|---|---|
| 107.6 s | 1 | 6 `record_overlay` | none | 1 | 1 | 0 | 63 | 63 |
| 181.1 s | 7 | **1 `GatherMissHoldEmpty`** | none | 1 | 1 | 6 | **0** | **98** |
| 211.4 s | 7 | **1 `GatherMissHoldEmpty`** | none | 1 | 1 | 6 | **0** | **101** |

**Classification: the merged fallback serves a revision-incomplete source, and here an empty one.**
- At revision 7 there is still no published root, and the Record overlay is active.
- The overlay gather misses, and the committed-pass copy (`committedPassesServeWindow`) is refused, so the build falls
  through to "hold empty".
- The empty build is stamped with the overlay's identity (DEC-143's bounded-work rule), so the reuse rule keeps serving
  the empty window for the whole revision.

Audible effect on ch8:

| | before the overdubs | during | after |
|---|---|---|---|
| Record 16ths | 398 | 43 | 8 |
| chord notes | 0 | 0 | 0 |

**The slot goes almost silent once overdubs begin, not only its overdubs.**

**Next witness (not yet captured):** the term that refused each of the two sources at `req=7`:
- the overlay: extent not ready, unreadable, or empty;
- the committed-pass copy: retention refused, or paged.
