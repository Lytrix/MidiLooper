# C5.3a — the designated track's committed lane reads RevisionManifest only

Date: 2026-11-01. Host and build evidence only. Nothing was flashed, and C5.4 is not part of this slice.

Owner rulings:
- DEC-246 amendment 22: reader authority is statically assigned per track.
- DEC-246 amendment 23: the routed track stays strict; continuity comes before C5.4.

## Routing (amendment 22)

- **Routed:** the committed windows of the designated track's active slot (`REVISION_MANIFEST_READER_TRACK`, default 0).
  They come from the manifest only.
- **Not routed:** every other track and slot keeps the snapshot reader and never consults the seam.
- The routing is static, chosen before any request.
  - `Track::manifestLaneRoutesSlot`: the designated track, its active slot, and a reader installed.
  - UI selection moves nothing. `RevisionShadow` now imports the designated track's active slot, not the selected track.
- Host suites that install no reader keep the snapshot lane, so the 3,900+ existing cases are unchanged.

## The seam

`include/CommittedManifestLane.h` holds data only (inline variables):
- the `Reader` interface;
- `gServable`: the words the tick reads (identity, revision, source revision, state);
- the one window owner;
- report-only counters.

The lane reaches the reader only from main-loop owners:
- `Track::refreshManifestLane`, called each lane turn from `refreshRetainedDemandIdentityForSlot`, from the request
  path and from the producer turn;
- `beginManifestQualificationForSlot`.

The tick reads `gServable` only (DEC-241).

What changed on the routed slot (flag 1 only; each site keeps the snapshot term verbatim under `#if`):

| Term | Snapshot lane | Routed slot |
|---|---|---|
| Window source current (`playbackWindowSourceIsCurrentForSlot`) | root current, or retained revision | servable manifest for the loop's current source revision |
| Demand identity | `{rev, gen, root}` | `{rev, gen, nullptr, manifestIdentity}`, stamped when the read begins |
| Qualification source | `EventWindowCursor` over the root | the reader's `ManifestWindowCursor` (`beginManifestCoverageQualification`) |
| Retained gather / paged ring / root pricing | used when the root is stale | never entered |
| Build gate | `SourceNotCurrent` / `RevisionMismatch` | `ManifestNoCommittedSource` / `ManifestImportOwed` / `ManifestIndexOwed` / `ManifestCapacityDenied` |
| Serving (cold), ready, boundary, revalidate, launch seed | `snapshotIdentity == root` | `manifestIdentity == servable` (`manifestIdentityServable`; `RouteReject::ManifestOwed`) |
| Record-return dispatch, holder offer, Record/overdub job takeover | root current, root-backed reader | servable manifest, manifest-backed reader |
| Merged fallback on a lane-owned miss | builds or serves | refused: a contained, named miss (`legacyRefused`), with `lastTickInLoop` recorded and the slot's open notes closed on gap entry (`gapClosures`) |
| Stale cold-ready cancel (`staleColdRollingReady`) | snapshot term | stale unless it is the servable manifest's, so a snapshot cold candidate kept from before routing is cancelled |

Notes on the table:
- **Retirement.** A frozen manifest read whose identity is no longer servable is retired by name
  (`manifest_read_source_moved`, counted `SourceMoved`). A completed read releases its pin at publication
  (`releaseManifestCoverage`); there are no retirement steps.
- **Turns the lane did not own** (overdub, note edit, a deferred stop, a queued start) keep today's route and are
  counted as `legacyGated`, so C5.4 can separate them.
- **Unchanged:** the capture-, overlay- and canonical-prefix lanes, the hot path (ITCM), and every write path.

## Native evidence: `test_revision_manifest_lane` (new, 9 cases)

The suite runs the real Track/Loop/storage closure with rolling on. It drives the production owner shape: requester,
holder offer, retry, and `playMidiEvents`. A host reader owner (store, production importer, C5.2 cursor) stands in for
`RevisionShadow`; its import and index work runs only when a case calls it.

1. **Current-boot content.**
   - Served windows carry the exact manifest identity and no root.
   - The root's window cursor is never used (0 ticks).
   - Inside every manifest-served interval, the MIDI equals the snapshot lane's for the same take, event for event.
   - The gaps are silent: 0 events outside served windows.
2. **Owed manifest.**
   - `ImportOwed` is named; the reader sees 0 window reads, 0 imports and 0 events.
   - Lane-owned misses are refused the merged fallback.
   - The interrupt tick makes 0 reader calls, records 0 misses and owes the merged owner nothing.
   - Once the owner imports, the content is served.
3. **No committed source, then content.**
   - `NoCommittedSource` before content, then `ImportOwed`.
   - The owner imports, the state becomes `Ready`, and only take notes are served.
   - The reboot-restored state is the same shape: amendment 18 / C3c showed a restore installs `loop.passes`. The device
     reboot is C5.4.
4. **The manifest moves during preparation.**
   - The read in progress is retired by name (`SourceMoved` ≥ 1).
   - Every served window carries the new identity.
5. **Index owed, then ready.**
   - A production Delete edit produces a hidden note.
   - `IndexOwed` is named with 0 window reads and 0 events; the lane never scans.
   - After the owner's index, the lane serves and the hidden note never sounds.
6. **Both index slots pinned.**
   - Two pinned indexed manifests and a third with a hide give `CapacityDenied`, and neither pinned index is evicted.
   - Releasing one pin lets the owner build it.
7. **Source move while serving.**
   - The served copy stays immutable to its end (the rolling contract).
   - The move is named `ImportOwed`, and every window begun afterwards carries the new identity.
   - The deleted note sounds only from the window that was already serving.

8. **A note sounding into a gap.** A 5-bar take has held notes crossing the window boundaries. A window ends with one
   of them sounding, and the gap entry closes it (`gapClosures` > 0). Every NoteOn of a held note gets its NoteOff.
9. **A snapshot cold ready before routing.** A 1-bar slot's cold full-loop candidate is prepared from the root with no
   reader installed. Once the reader is installed, that ready is cancelled and re-prepared from the manifest: its
   identity is the manifest's and it carries no root.

**Independent review** (AGENTS.md, one focused pass). It found two should-fix defects, both now fixed and each pinned
by a fixture above:
- **Case 9:** a stale snapshot cold-ready on the routed slot was never cancelled, so the slot wedged.
- **Case 8:** advancing the playhead on a refused tick skipped the NoteOff of a note sounding into the gap, so the note
  hung.
- Also taken: on a direct Ready → Ready change, `gServable.identity` is now cleared before the other words change.
- Left as notes: after a slot switch, `gServable` and the window owner follow on the next refresh (bounded, not
  covered by a switch-mid-read fixture); a double `endWindow` is idempotent.

**Discrimination (mutations, each reverted):**

| Mutation | Fails |
|---|---|
| Re-enable the merged fallback on the routed slot | cases 1, 2 and 5 (gap events: 20 / 8 / 6) |
| Drop the routed retirement of a stale frozen read | case 4 (`SourceMoved` never named) |
| `manifestIdentityServable` ignores the identity | case 4 ("a window read from the moved manifest was served") |
| No gap-entry closure | case 8 (`ons=2 offs=1`, the ledger left open) |
| `staleColdRollingReady` ignores routing | case 9 (the snapshot ready is never replaced) |

**Full native suite** (final tree): 3,925 cases, 8 skipped, 3,917 passed. Before the review fixes it was 3,923 cases, 8 skipped,
3,915 passed (C5.2: 3,916 / 3,908). Host size pins re-based under
the flag, compiler measured: `LoopPlaybackRuntime` 9,984 → 10,160 B.

## Measured finding: the gap route (amendment 23)

Snapshot lane, steady 4-bar slot, 9,216 ticks: view 1,536 ticks, merged fallback 7,680 (83 %). Two reasons:
- the requester returns while `Serving`, so no successor is prepared;
- each window is positioned about 1.5 traversals ahead.

On the routed track those 83 % are named, contained misses, by the owner's choice. C5.4 therefore waits for the
source-independent continuity primitive ("serving does not suppress successor demand"), common to both probe builds.

## Device builds (not flashed)

| Image | ITCM code + padding (floor 840) | RAM1 variables / locals |
|---|---|---|
| default `teensy41-capture-serial` | 391,944 + 1,272 (unchanged) | 96,192 / 34,880 (unchanged) |
| `-revstore-probe` (flag 0, the A/B "off" side) | 392,072 + 1,144 (unchanged) | 96,192 / 34,880 (unchanged) |
| `-revstore-reader` (flag 1, new env) | 392,280 + 936 | 96,224 / 34,848 |

- **Byte identity.** `HEAD` (`e3fb9598`) and this change were built from one identical path with
  `SOURCE_DATE_EPOCH` pinned (`RtcTime.cpp` embeds `__DATE__`/`__TIME__`). The HEX images are identical for the default
  (`b4ee9921…`) and the flag-0 probe (`37e5ebc1…`).
- **Flag 1.**
  - +64 B ITCM over C5.2: the out-of-line `ownsManifestCoverage` call in `ownsCoverageSource`.
  - +32 B DTCM (one section granule): the lane words and the reader object. The report-only counters are DMAMEM,
    zeroed at boot by the reader owner.
  - Target size pins re-based under the flag: `LoopPlaybackRuntime` 7,848 → 8,024 B and `CaptureStopJobs`
    33,088 → 33,152 B, both extmem.
  - The lane cursor is extmem and shares the digest producer's staging buffer; the producer yields while the lane reads
    (`producer_yields`).
  - `!REVSHADOW_STATUS` prints `REVSHADOW,lane,…,source=manifest,…,legacy_refused=,legacy_gated=` for the C5.4 routing
    proof.

## Not covered yet

- **Production undo/redo.** The source move is exercised through a production Delete edit, not `TrackUndo`.
- **On the device:** the owner chain and a reboot-restored slot (C5.4). Also the cold one-bar loop shape and the
  non-active-slot launch (`CommittedLaunch` stays snapshot: the non-active slot is not routed).
- **Next:** the continuity primitive with its gate (lane-owned merged ticks = 0 in both probe builds), then C5.4.
