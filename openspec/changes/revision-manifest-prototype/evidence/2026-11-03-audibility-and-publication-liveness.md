# Audibility before publication, and publication liveness (owner ruling 2026-11-03)

Evidence first; no fix. Four device runs of the same `lane_cadence` sweep on track 8 / slot 4.

| run | image | session log |
|---|---|---|
| A1 | probe `f8a30f07` (manifest reader routed to track 8) | `captures/session_20261103_211405.log` |
| A2 | default `310580ef` (shipping rolling lane, unrouted) | `captures/session_20261103_213204.log` |
| B1-B3 | earlier probe runs | `session_20261103_171618`, `_190327`, `_193923` |

Witnesses:
- `DIAG,tick_route`: per 5 s, which source served each playback tick, over all 8 tracks; about 600 ticks per track.
- `DIAG,track_send`: messages sent.
- `DIAG,win_gate` / `route_reject` / `win_frozen_readiness`: the lane's window state for track 7, the 0-based HITL
  track.
- `DIAG,snap_gate`: the Snapshot owner's eligibility and construction units.
- `DIAG,alloc_census`: the internal arena tail.

## Gate A: audible playback before publication

**The root never published in any of these runs**: `root_rev=0` at run end in all five. Playback was nevertheless
served for almost the whole run, so audibility is not gated on publication in this sweep.

| window | A1 probe (routed) | A2 default (unrouted) |
|---|---|---|
| Record stop | `none=10` for one 5 s window | `none=0` |
| overdub | track 8 served from `merged` (merged 4,800) | same |
| Overdub stop → +20 s | **`none` 357 / 597 / 600 / 601 / 381; sends 169 → 26 → 0 → 0 → 166** | `none=0`; view back at 560 |
| after the sweep's restart | `none` 250 / 601 / 600 / 44 (~15 s) | `none=0` |

The A1 gap after the Overdub stop belongs to the manifest lane, track 7, and has two parts:
1. **77.2 → 84.3 s:** `win_not_servable`, then `win_busy`, about 7 s.
   - The lane rebuilds source qualification after the stop (`build_drop`: `qualification_started`, whole-loop,
     `handoff_unreachable`, `released_without_site`).
2. **84.7 → 97.9 s:** `win_requested`, then `win_ready` at 85.4 s, but `win_serving` only at 97.9 s; ready but
   not serving for 12.5 s.
   - Under the C5.3b rule, a missed successor is a named miss with no merged fallback, so the routed track is silent
     the whole time. No `merged_build` row exists for track 7 in the gap, while track 3 keeps building merged windows.

**Reading.** In this sweep the measured audible defect is specific to the probe's manifest-routed track, after a
stop or a restart. Shipping playback shows no gap. The owner's "inaudible until publication about 100 s later" is not
reproduced here as publication-gated: the routed track resumes at 97.9 s with no root published. Still to establish:
- whether the owner's observation was on the probe image;
- the gesture sequence, if it was not this sweep.

## Gate B: finite publication does not converge

The publication owner is **stalled, not slow**.
- On track 7, construction units freeze mid-fragment at the overdub stop, with `elig=1` on every turn.
- The freeze point differs per run: `cu=196` (A1), 146 / 70 / 14 (B1-B3).
- No fragment completes for the rest of the run (more than 100 s). The default image's snapshot units also stop at
  about 80 s.

**Cause, measured on both images.** The internal arena tail steps down during the overdub and never recovers:

| image | tail before | during the overdub | after |
|---|---|---|---|
| A1 probe | 98,304 B | 65,536 B | 36,864 B |
| A2 default | 110,592 B | 77,824 B | 32,768 B |

- `admitSnapshotPublicationTurn` → `MemoryMonitor::admitOperationMemory(SnapshotReserve, 8 KiB)` requires
  `tail >= 32 KiB floor + 8 KiB`.
- Its own commentary records that the tail "never recovers because `_sbrk` is a pure bump" and is "a false denial"
  (tail 36 KB while the largest free chunk was 79.4 KB). That is why user gestures moved to a read-only free-list
  walk.
- `SnapshotReserve` was deliberately left on the tail, because it gates every turn. Every publication turn is
  therefore refused once the tail is below 40 KiB.

**Host RED:** `test_publication_red_converges_when_the_tail_is_low_but_memory_is_free`
(`test/test_continuous_capture/wrap_publication_reds.inc`, run with `PUBLICATION_LIVENESS_RED=1`).
- It uses the measured tail of 36,864 B and the measured largest free block, 80 KiB.
- Wrap, then the overdub stop, then the production stop job and Snapshot owner. Publication never converges: FAILS.
- Control: the same fixture with only the tail raised to 80 KiB converges (PASS). The RED therefore isolates the
  tail-only admission.
- Not decided here (owner): how `SnapshotReserve` should judge memory without the per-turn probe storm the earlier
  withdrawal measured (`2776cae3`). Options include the read-only walk at a bounded cadence or on denial edges, or a
  cached largest-free value.
- Also open: what consumes the tail during the overdub (`cap` 1 → 3, `undo` 1 → 2, `ib`/`ub` 0 → 8).

## Gate C: display bars 20-32

Not yet traced. Track 7's display rows in the gap show `disp_turn ... st=ret, why=hold_gap` over a published window
that the playhead is entering. That is the lead for the viewport-demand trace.

## Gate A, follow-up (owner: the observation was on the probe image's track 8)

**Host RED:** `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop` in `test_revision_manifest_lane`,
registered with `ROUTED_AUDIBILITY_RED=1`. A four-bar take on the routed slot, played on the production owners with
the reader owner serviced every tick: two traversals, then an overdub (one note), its deferred stop, and four more.

| traversal | manifest-served ticks | take notes played |
|---|---|---|
| 0 | 0 | none |
| 1 | 1,536 / 3,072 | bars 2-3 |
| 2 (overdub, merged route) | 0 | bars 0-3 + overdub note |
| 3 (after the stop) | 0 | **none** |
| 4 | 1,536 | bars 2-3 |
| 5 | 0 | **none** |
| 6 | 1,536 | bars 2-3 |

**Reading:**
- The routed manifest lane is audible only inside a window that is already serving. The next window is not prepared
  ahead, and a gap is silent. This happens in steady state, not only after a stop.
- That is amendment 23 as written: lane-owned gaps are named misses, never merged. The existing case
  `test_c5_3_current_boot_content_is_served_from_the_manifest_only` asserts "outside them the routed lane is silent
  by contract".
- `tasks.md` keeps C5.3b successor preparation open, pending the owner's successor price.
- The overdub stop adds a longer gap on top: the revision move makes the serving window unservable. On device, the
  lane's whole-loop re-qualification followed (about 7 s busy, then a window ready 12.5 s before it served).
- The defect is therefore not publication-gated. The owner's contract ("a newly recorded loop plays immediately")
  conflicts with amendment 23's interim silence on the routed track. Turning the RED green needs either C5.3b
  successor preparation (plus a post-stop serving rule), or an amended amendment 23 that gives the routed track an
  interim audible fallback. Owner decision.

## Gate A reconciliation (owner ruling: reconcile with the earlier successor handoffs)

- **The measured handoffs are correct, and they are not production.**
  - The 20/20, 21/21 and 5/5 handoffs come from the C5.3b-m **measurement requester** (`RollingSuccessorProbe`),
    which the probe image enables at boot with the DEC-147 lead price waived (`installAtBoot`:
    `bypassLeadPrice = true`).
  - With the price in force, production admits no successor at N's end (`test_rolling_successor_lead`:
    `test_c5_3b_the_declared_price_refuses_every_successor_at_n_end`).
  - The manifest-lane host suite never enables the requester. The first Gate A RED therefore exercised production
    with no successor preparation, which is why it showed a gap at every boundary.
- **Same path, same knobs.** The host Gate A case with the probe image's knobs plays steady state continuously: every
  tick served and every take note, in traversals 5-6. That matches the device's continuous `view` route.
- **Classification of every silent interval** (host with probe knobs, and device A1):

| interval | successor state |
|---|---|
| playback start (traversal 0 -> bar 2 of traversal 1) | the chain's **first** window: none covers the current phase; ready, but servable only from a later two-bar boundary |
| after the Overdub stop (traversal 3 -> bar 2 of traversal 4; device 77.2 -> 97.9 s) | the revision move ends the serving chain (`win_not_servable`). The chain restarts with a first window: on device about 7 s of busy lane plus whole-loop qualification, then ready at 85.4 s but reachable only at 97.9 s |
| after the sweep's restart (device 144 -> 159 s) | the same chain cold start |
| steady state with the requester on | none: successors are requested, prepared, adopted and handed off with audible coverage |

- **What the counters miss.** No interval is a successor that was counted as handed off without audible coverage.
  The counters are honest about successors. They simply never count a chain's opening window, and every silence
  observed is one.
- **The defect to fix (not implemented).** A chain cold start (playback start, a revision move, a restart) has no
  window covering the current phase until the first window's start is reached.

## Gate B (DEC-249)

- **Fixed:** `SnapshotReserve` resolves a low tail with the read-only walk; see DEC-249.
- **Device:** tail 32,768 B, largest free 54,824 B, walk 13 µs, publication resumed and published.
- **Open:** a roughly 50 s publication-idle window after the stop on the default image, with memory admitted.
