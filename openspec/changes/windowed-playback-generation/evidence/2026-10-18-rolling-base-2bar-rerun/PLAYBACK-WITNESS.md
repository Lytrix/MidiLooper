# Playback-read witness during persistence — execution packet

Scope: user-requested truthful witness for `persistence_active_snapshot_unavailable_during_save`, task **3.15g — wrapped short-loop Record finalization**. Existing Track reader and DebugSessionCapture remain owners. No readiness/scheduling/admission/content change, no budget rebase, no canonical stimulus edit.

Historical review: **DEC-081 — audible playback never gates persistence**, **DEC-142 — Record presentation lifetime split**. Reuse selected immutable views and the existing foreground capture drain; no ownership transfer or reassessment trigger.

## Transition oracle

After a successful cursor turn over a complete published current view covering the actual phase, record source/storage generation, origin, coverage and read timestamp. No receipt for missing/unpublished/stale/out-of-coverage views, muted/output-disabled turns or invalid cursor walks. Silent complete intervals qualify without manufacturing MIDI. The clock path stores bounded scalars only. Capture foreground delivery retains observation time and rate-limits each slot to at most four receipts/second. The verifier accepts strict versioned receipts alongside existing snapshot-build/MO evidence with the same ±500ms window. Missing or malformed receipts remain failures.

## HITL recipe

| Field | Contract |
|---|---|
| Acceptance | Existing registered base recipe has overall_ok=true with all active persistence windows witnessed by genuine reader observations or existing evidence |
| Reused stimulus | STIM-BASE-2-2-01@v1, base preset, track8/slot4/channel8; list recorded in session scratch |
| New stimulus | None; canonical registry unchanged |
| Gestures | Existing baseline actions; no custom notes/buttons |
| Marker | DIAG,playback_read, schema1; Tier A in .cpp classifier; max4Hz/slot; observation after Track reader cursor, serialization from foreground drain |
| Marker tests | Capture-ring production sink/tier/retained timestamp/cadence tests; native real-reader fixture; host malformed/missing/stale receipt controls |
| Marker preflight | Pending implementation |
| RAM1 | SESSION_CAPTURE gate, FLASH helpers, RAM2 scalar mailboxes. Baseline default padding1144/qualification856; unchanged floor840 |
| Native RED | Real reader emits a qualified silent interval receipt; missing/stale/current-role controls; receipt delivery test |
| Smoke | Managed no-content health, canonical two Stops/drains; exact image pin; read retained row after registered run |
| Full entry | host_midi_hitl.py run --preset base --track8 --loop-slot4 --midi-channel8 --build-env teensy41-capture-serial-window-qualification --boot-reset (normal CLI spacing), managed360s bound |
| Verdict | overall_ok=true plus all original verifier gates; exit0 alone never PASS |
| Evidence | Native/host/build/marker results, exact HEX/ELF, run report, canonical shutdown and prior-image restore/postflight |

Verification pending. Previously restored default08f43e45… remains the required restore image. No SD cleanup or unrelated-slot stimulus authorized.

## Local evidence

- Host parser RED before support, then13/13 GREEN. The strict decoder lives in `hitl.serial.protocol`; verifier keeps the same ±500ms and all existing persistence gates.
- Native real-reader hook removal RED at missing observation; restored byte-identically (`TrackPlaybackHotPath.cpp` SHA-25681a4090d…). New reader+capture-ring suites39/39 GREEN, including silent turn, preparation/noncurrent/incomplete/stale/muted controls, original timestamp,4Hz cadence, undefined origin and full-ring progress.
- One focused review found a mailbox stuck behind a full Tier-A ring and undefined origin3 acceptance; both fixed/retested. Pre-walk enqueue allows the established Tier-A eviction policy while direct USB keeps its reentry guard. No broad repeat review.
- Marker preflight OK: emitted/Tier-A/FIT,19/31 prefix bytes. Classification remains .cpp-only; new prefix is PROGMEM. Initial literal placement snapped variables+4096; FLASH placement repaired it without a budget change.
- Final resource builds PASS: default variables96,192/code392,088/padding1,128/locals34,880; qualification variables96,192/code392,376/padding840/locals34,880. Floor840 unchanged. Mailboxes are RAM2, no Track/runtime size rebase.
- Qualification HEX085e5e9d59602214be6e26bf5e0d77f303e136cbd402850152337112828a2228; ELF4aea635863db3a4f3b5f0990b93f7522b7c155b34939f1fa2b5ced7c4b6b6293. Images retained in session scratch. Default HEX41c495f3… is NOT the installed historical restore.
- Device connected; `captures/i5-playback-witness-preflight-01/health.json` PASS:STOPPED,pending0,queue0/4,both heaps complete,two canonical drains. No upload/content stimulus yet for this witness candidate.
- Full native, final strict/diff and new registered HITL verdict pending.

## Device attempt and corrected observation carrier

First witness image085e5e9d… (`captures/i5-playback-witness-base-01`) is **FAIL**, not accepted: `record_stored_note_grid_bad` (three bad grid deltas vs budgettwo). Original persistence gate passed8/8 windows, but raw `playback_read` receipt count was zero, so this run does NOT prove the new witness was responsible. Record/overdubs/undo-redo and canonical two-drain shutdown completed; after-base healthPASS/pending0/queue0/4/bothheaps. No thresholds or stimulus changed.

The observation carrier was corrected to follow the exact selected fixed storage or compiled bridge, rather than fixed-view pointer only. It recognizes the complete published current view, the actually selected prepared pair (its publication generation), or the exact selected complete/stream-prepared bridge. All keep their original source origin; no source is relabeled or admitted by telemetry. The callback remains after a Completed cursor walk and is silent for incomplete/noncurrent sources and disabled output.

Additionally, Track playback generation starts at0;0 is a legal matched generation, not an unavailable marker. The initial sink's zero-generation refusal was wrong. Matching is proven at the reader owner; the sink/parser now permit0 while still rejecting zero storage/publication identity, out-of-range generation values, undefined origins, invalid coverage and malformed rows. Explicit generation0 success controls added. This is not an admission or verifier-window relaxation.

Corrected focused native40/40 (real view and prepared-pair walks plus bridge guard controls and capture-ring mailbox), host/import18/18, markerpreflightPASS. Both rebuilt resource gates unchanged: variables96,192; default code392,088+padding1,128,qualification392,376+840,floor840. Final full native and corrected-image HITL pending; do not reuse the first image's verdict or pin.

## Hardware verification on the real witness image (2026-10-19)

**The HITL harness never flashes.** `--build-env` selects build-derived verifier expectations and records the
*built* HEX hash in `metadata.json`; the run itself talks to whatever image the board already holds. Two runs made
before this was noticed (`captures/i5-playback-witness-base-03`, `-04`) therefore executed the historical default
image that was still flashed from the earlier restore. Their `PASS` is **not** acceptance evidence for this change:
their `win_frozen_readiness` rows carry the previous 11-field wire form, the current source emits 14, and every
qualification-only family (`snap_gate`, `win_gate`, `playback_read`, `playback_available`) is absent.

The pinned qualification image was then uploaded explicitly
(`pio run -e teensy41-capture-serial-window-qualification -t upload`, HEX
`3e50ccf326a4d0f35defd80d00e9f97f58ed64e2e93d929ce6fc6df9e335df7d`, ELF
`33a1cf39da7d9cc4f90158777ce73f228c49f861b41864af37a690cd1c2df46e`) and the registered base preset re-run:
`captures/i5-playback-witness-base-05`.

| Gate | Result |
|---|---|
| Witness rows | **106 `playback_read` + 22 `playback_available`**, all 128 parsed by `hitl.serial.protocol`; every receipt origin 0 (Committed), 112 with source generation 0 |
| Record / overdub phases | 32/32, 16/16, 8/8 note pairs, no open or out-of-range notes, latency 8/3 clocks ≤ 12 |
| Stored record grid | `bad=0/2` (delta 47-49) — the two earlier `record_stored_note_grid_bad` runs were host pacing, not the witness |
| Overdub starts | both admitted; no `prewarm_failed`, no `MEM_DENIED` |
| Verdict | **INCONCLUSIVE_CAPTURE**, sole issue `persistence_active_snapshot_unavailable_during_save` |
| Persistence windows | 9 complete, 8 playback-active, **5 covered** (uncovered `slot:7:5` 42.878-42.965 s, `slot:7:6` 43.428-43.531 s, `slot:7:7` 44.008-44.098 s) |

The three uncovered windows are **not** a witness gap. Measured on the same log: after the wrapped short-loop record
stop at 41.994 s the rolling-lane image produces **no note output for 3.078 s**, and all four post-stop `LoopPersist`
saves complete inside that silence (0.34 / 0.88 / 1.43 / 2.01 s). The same measurement on the default image
(`-04`, rolling off) resumes in **0.002 s** with 99 note-ons in the next 10 s; on the presentation-candidate image
(`a2313c1d…`, `i5-presentation-review-base-02`) it is **3.203 s**. The track stays `PLAYING` and the transport tick
advances throughout, so playback is genuinely not being served while the save runs. A receipt there would be a
fabrication, and the witness correctly emits none.

Consequence: this gate now fails for the real reason it exists to catch, and the verifier, its ±500 ms window and all
thresholds stay untouched. Restoring playback continuity across the post-record save belongs to task 3.15g's
presentation ownership and is the next firmware step; the image and this capture stay pinned as its reproducer.

## Safety and leave-behind state

No SD quarantine, delete, rename or cleanup, no unrelated-slot stimulus, no default rolling activation, no archive or
commit. The exact prior default `08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964` was re-flashed
no-build (13.94 s, the pinned hex written over the configured build target, hash verified before upload) and a fresh
managed postflight `captures/i5-witness-restored-final-02/health.json` is **PASS**: fresh boot, `STOPPED`, `tick=0`,
`pending=0`, `queue=0/4`, both heaps complete, two canonical drains, no incomplete-shutdown latch. One earlier
postflight read `#HEAP_STATS,refused=storage_work` because the 30 s boot settle was still inside the library
restore; the settle was raised to 45 s and the retry is the recorded one — the first attempt is kept as a timing
observation, not as a device fault. The qualification image stays pinned in session scratch
(`$PI_SCRATCH_DIR/playback-witness-arm-images/`) as the next device run's reproducer.
