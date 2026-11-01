# C5.1 + C5.2 — the manifest window gives the consumer the snapshot window's tuple

Date: 2026-11-01. Owner rulings: DEC-246 amendments 19–21 (C5 proceeds now, behind the probe flag; C5.1 pins the full
consumer tuple). Host only: no device change yet.

## What the committed lane consumes (mapped from the code)

The committed rolling window is filled from the published root:
- `LoopPlaybackRuntime::beginCoverageQualification(snapshot)`;
- `LoopSnapshot::EventWindowCursor`;
- `RuntimePlaybackViewFill` scope `SnapshotWindow`.

It then runs the build's snapshot phases:
1. canonical order by (`canonicalOrder`, eventId);
2. ranks (`canonicalRanks[order[i]] = i`);
3. playback order by (projected phase, NoteOff < NoteOn < other, tick, rank).

Per row the fill writes:
- the `MidiEvent`;
- `phases = canonicalOrder`;
- `passOrigins = eventId` (pass, event).

Post-build consumers:
- `passOrigins[].pass` is read by the hot path, but only as "committed vs live overlay" (`kLiveOverlayPassOrigin`,
  `0xFFFF`).
- `canonicalRanks` are read by `RuntimePlaybackBridge` as **indices into canonical-position masks**, so rank numbering
  is part of the contract, not only the order.
- The ledger boundary and cursor anchor are supplied by the caller, not by the source.

## Implementation (behind `REVISION_MANIFEST_READER`)

- `CommittedWindowRowSource` (new interface) carries the snapshot cursor's read contract.
- `RevisionStore::ManifestWindowCursor<Cap>` implements it over `beginWindow` / `stepWindow`:
  - one row or one job transition per read unit;
  - it pins the manifest for the read and checks the expected identity and revision;
  - `SourceMoved`, `IndexOwed`, `ImportOwed` and `CapacityDenied` end the read `Inactive` with a named `result()`;
  - it never imports, builds an index or scans;
  - it accepts only the snapshot cursor's shapes: at most two bars, inside the loop, never wrapping. Production never
    requests a wrapping committed window: `EventWindowCursor::begin` refuses one.
- **Rank numbering equals the snapshot's.** The snapshot keys a committed row pass-major (merge order), then by its
  position in the pass. The store's window job now stamps each emitted row's `origin` with its manifest-wide source
  position in merge order (the run's cumulative origin base plus the row's origin, so it survives compaction). The
  cursor emits `canonicalOrder = eventId = origin << 16 | per-origin ordinal`.
- `RuntimePlaybackViewFill` gains scope `ManifestWindow`, and `RuntimePlaybackViewBuild` gains manifest
  `begin` / `beginCoverageQualification`. Both enter the snapshot phases unchanged.
- The pointer shares the snapshot cursor's union slot (the `sizeof(LoopPlaybackRuntime)` pin).

## Native evidence

`test_playback_prewarm`:

- `test_c5_manifest_window_gives_the_consumer_the_snapshot_windows_tuple`:
  - the loop is built from real committed passes: a Record plus an overdub, with a cross-pass equal-tick NoteOn tie, a
    same-pitch duplicate, and NoteOff + NoteOn on one tick;
  - the root is published the shipping way (`requestCommittedPassesRebuild`, `stepPendingSnapshotPublish`) and is
    current;
  - the manifest is imported with the production-equal importer;
  - four coverages: bars 0–1, bars 2–3, the last bar, and a mid-bar start.

  Through the real build, row for row in the final order, the two windows give:
  - an equal event (tick, type, channel, data);
  - an equal playback phase;
  - an **equal rank**;
  - committed provenance on both sides (never `0xFFFF`).

  `canonicalRanksReady` and `passOriginsReady` are equal too.
- **The fixture discriminates.** With window-index ranks it failed (`Expected 7 Was 1` on the overdub's tick-0 NoteOn).
  With unstamped origins it failed the same way, because every fresh run's rows carry origin 0. It passed only once ranks
  were numbered pass-major.
- `test_c5_manifest_window_cursor_refuses_by_name`: `SourceMoved` for a wrong identity (cursor idle), `ImportOwed` for
  no manifest, `CapacityDenied` for a wrapping or longer-than-two-bar coverage, and `CapacityDenied` for a window that
  does not fit its staging. Each ends `Inactive`, with the manifest pin released.

Suite: 254 / 254.

## Builds

| Image | ITCM code | Padding (floor 840) |
|---|---|---|
| default `teensy41-capture-serial` | 391,944 (unchanged) | 1,272 |
| probe, flag 0 | 392,072 (unchanged) | 1,144 |
| probe, flag 1 | 392,216 (+144) | 1,000 |

A first attempt put 528 B into ITCM on both images: a template instance and a guarded lambda do not inherit the
flash attribute. The repair:
- the snapshot copy loop is restored verbatim;
- the manifest step is a plain flash function;
- the build's guarded lambdas only call one flash-resident helper.

With the flag at 0, none of this code exists on the device. **C5.3 has 160 B of ITCM headroom on the probe**, so its
lane code must stay flash-resident.

## Not covered yet

- Hide/edit rows in the tuple fixture: hide parity was established in C4 hide/index. A tuple case with an edit run is
  added with C5.3.
- C5.3 (the lane switch) and C5.4 (device A/B).
