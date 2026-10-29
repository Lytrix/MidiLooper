# DEC-196 — the boot hold ends at the playback set (device analysis, native + build green, not flashed)

Owner report: *"if I start playing directly after boot only track 5 plays … can you make it so all tracks can play
after boot?"* This file records what `captures/session_20261029_113133.log` actually shows, what changed, and what is
still unproven.

## Correction to the earlier read

The previous conclusion — "all tracks audible by 18.5 s" — was wrong. At **18.47 s the owner stopped the transport**
(`[TRACK] All Notes Off` ×5, `Transport stopped`, tick 2608). The commits after that point happened *because* the
transport stopped, not while it played. The owner's own test — press Play right after boot — is the failing case.

## What the capture shows (one boot, `#CAP` timestamps are µs)

| Time | Row | Meaning |
|---|---|---|
| 7.99 s | `RRQ,0,0,…queued` … `RRQ,7,3,…queued` (8 rows, interleaved with `BOOT,scan,t0..t7`) | boot playback set queued: one slot per track |
| 9.12–9.26 s | `RRQ,0,7`, `RRQ,0,1`, … `RRQ,7,2`, `RRQ,7,4` (16 rows, after `BOOT,scan,done`) | the focus footer queues every track's neighbours |
| 10.64 s | `RRQ,4,0,…terminal,none,committed` | focus slot committed → **10.65 s** `BOOT,usb_host,begin`, i.e. DEC-184 released the hold here |
| 11.16 / 11.43 s | `RRQ,4,7`, `RRQ,4,1` committed | focus track's two neighbours |
| 11.66 s | `Playback started @ tick 0` ×5, `Transport started` | owner pressed Play; five tracks had a loop length, one had content |
| 11.99 s | `RRQ,5,0,…,waiting,waiting_commit_gate` | track 5's restore prepared, but its **commit** is denied while the transport runs |
| 18.47 s | `Transport stopped` | owner stopped |
| 18.63 s | `RRQ,5,0,…terminal,committed` | the commit lands the instant playback stops |
| 18.77–19.47 s | `5/1`, `5/7`, `6/0`, `6/1`, `6/7`, `7/2`, `7/3` committed | drain resumes, per track |
| 23.4–29.03 s | `0/0` … `3/7` committed | the last three tracks' content arrives **12–18 s** after the release |

Three separate defects, in the order they bite:

1. **Wrong release set.** `BootGate::focusSlotSettled` released on track 4 alone. Seven tracks were still unloaded
   when the panel became interactive.
2. **The drain is gated by the transport.** `canRunBackgroundLoadLoopNow()` is false while `sequencerRunning`, and
   `bootTitleLoadDrain_` (which bypasses that gate) is already false after the release. The set can therefore only
   finish while the owner is *not* playing — the opposite of the reported use.
3. **The boot ranking was overwritten.** The served order is per-track (`4/0, 4/7, 4/1, 5/0, 5/1, 5/7, 6/…`), i.e.
   `computeDeferredRestorePriority`, not DEC-194's `computeBootRestorePriority`. Cause:
   `reprioritizeDeferredLoopSlotRestoreEntries()` recomputes *every* pending entry on each idle selection turn
   (`LoadLoopJob.cpp`) and on each of the footer's eight per-track `prioritizeLoopSlotRestoreForFocus` calls
   (`applyActiveLoopIndex`). DEC-194's ranking never survived the first of them.

## What changed (DEC-196)

- `include/BootGate.h` — `playbackSetSettled(focusSettled, playbackSetRestoreWorkOwed)`; `bootTitleMayRelease`
  keeps the whole-queue fallback.
- `src/StorageManager.cpp` + `include/StorageManager.h` — `StorageManager::playbackSetRestoreWorkOwed()`: one slot
  per track (its active slot), answered with the existing per-slot predicates (active job, parked job, queued
  request). A payload-less slot is never queued and so never holds the boot.
- `src/main.cpp` (boot release) — release at the playback set; a 15 s RAM2 deadline bounds the hold so a corrupt or
  refused slot cannot wedge the panel.
- `include/Utils/BootLoopSlotRestore.h` — `computeBootRestorePriority` is now total:
  `0` focus selected, `1 + trackIndex` any track's active **or** selected slot, `>= kBootBackgroundRestorePriorityBase`
  (200) everything else.
- `src/StorageManager/LoopSlotRestoreQueue.cpp` — `reprioritizeDeferredLoopSlotRestoreEntries()` keeps the boot order
  while `getBootTitleLoadDrain()`; `currentBootRestorePriority()` recomputes it from the runtime (after the footer
  active == selected, which is the slot playback uses).

Because `canRunBackgroundLoadLoopNow()` already returns true while the drain flag is set, the extended hold also keeps
the commit admission open, so a Play pressed *during* the hold cannot stall the set the hold is waiting for.

## Gates

- `test_boot_focus_handoff` 12/12 (4 new DEC-196 cases; the 8 DEC-184 cases unchanged).
- `test_slot_focus_policy` 29/29 (reworked expectations + `test_dec196_every_playback_slot_ranks_ahead_of_every_background_slot`).
- Full native **3608 cases, 3 failed, 7 skipped, 3596 succeeded**. Failures are the pre-existing DEC-193 RED, the
  DEC-194 RED, and the order-dependent `test_record_note_stream_survives_four_sequential_overdub_publications`
  (`test_playback_prewarm` alone: 252/252; it fails only inside the composite `test_frozen_record_reader_survival`).
- `teensy41-capture-serial` SUCCESS: RAM1 `variables 96,192`, ITCM `code 392,248 + padding 968` (floor 840). The
  DEC-194 report-only probe (`BOOT,track,…` once per 4 s for the first minute) is still uncommitted in the tree and
  costs RAM2 only; stripping it returns the previous headroom.

## Expected device effect (unproven)

1. `RRQ` order: `4/0`, then the other seven playback slots (`0/0, 1/0, 2/0, 3/0, 5/0, 6/0, 7/3`) — every playback slot
   before any neighbour.
2. `BOOT,usb_host,begin` lands after the eighth playback-slot `terminal` (≈2–3 s later than the 10.65 s measured here).
3. A Play pressed right after boot sounds every track that has content.
4. `BOOT,track,…` (the probe) should show `passes=1` for all eight tracks at the first sample after release.

## Not claimed

- No flash, no device run, no SD operation in this session.
- The two DFRAME/`win_gap`/display rows in this capture are untouched.
- If a track's selected slot has no payload there is nothing to restore; it stays silent by content, not by this gate.

## Device result — PASS (flashed `bea5a99b…`, boot `session_20261029_120230.log`)

Built clean, retained in `captures/bea5a99b/` (HEX + hash + head/status/diff + build flags + upload log). Homebrew
Teensy Loader CLI **2.3**: `Found HalfKay Bootloader` → `Programming` → `Booting`, exit 0; HEX
`sha256 bea5a99b25c0b2afd4b0ba257ee522c73ae99c8ae2e8f8f2ab0d5fd90a4d9845`. Boot proved separately from the owner's
capture: `boot_reset,crash=0`, `BOOT,load,ok`, `BOOT,ram1,192512`, `#DMA_XFER` advancing with `timeouts=0`, USB
`16C0:0489`.

The DEC-194 probe was replaced by **one** report on the release turn (`BOOT,ready,…`), as directed.

| Time | Row | Meaning |
|---|---|---|
| 2.11 s | `boot_reset,crash=0` | boot |
| 4.05–5.03 s | `RRQ` queued: 8 playback slots, then 16 neighbours | set queued first, neighbours behind |
| 6.90–7.38 s | `RRQ` terminal, committed: **`4/0, 0/0, 1/0, 2/0, 3/0, 5/0, 6/0, 7/3`** | all eight playback slots, in set order, in 0.48 s |
| 7.4 s | `BOOT,ready,settled=1,tracks=8` (8 rows) then `BOOT,usb_host,begin` | release at the set — **2.9 s earlier** than the pre-fix 10.65 s |
| 7.92 s | `DISP,0,STOPPED,1536,58,58,…` | focus slot painting |
| 8.03–8.47 s | `RRQ,4,7`, `4/1`, `5/1`, `5/7` committed | neighbours continue behind the release |
| 9.07 s | `Transport started`; `MO,144,ch,note` on **ch 1, 2, 3, 5, 6, 8** within 14 ms; `#PAINT_BEAT … notes=58,out=48` | one Play right after boot sounds six channels at once |

`BOOT,ready` per track (`slot,state,enabled,passes,root,vcache` at the release):

```
track=0 slot=0 passes=1   track=1 slot=0 passes=1   track=2 slot=0 passes=1   track=3 slot=0 passes=0
track=4 slot=0 passes=1   track=5 slot=0 passes=1   track=6 slot=0 passes=0   track=7 slot=3 passes=1
```

So the gate did its job: the eight playback slots are committed before the panel/USB host came up, and a Play at that
point sounds every track that has content. The two silent channels are the two tracks whose slot body restores **zero
committed passes** (tracks 3 and 6; `stored_notes` has no row for either, while tracks 4/0 = 58, 2/0 = 32, 0/0 = 5,
1/0 = 215, 5/0 = 42, 7/3 = 672 notes are reported). That is a card-content fact, not the release/gate defect: those
slots report `terminal,committed` with an empty body.

**Resolved by the owner:** tracks **3 and 6 (0-based) are empty by design**, so six sounding channels is the correct
expectation for this set — every track that has content sounds on the first Play. Nothing about the empty restore is a
defect. A later idle capture (`session_20261029_120430.log`, 51.5–112.8 s of the same session) shows the steady state
with the transport stopped and no note traffic, i.e. the acceptance window is the one above.

Cost note: the hold does **not** delay this boot — the ordering fix completes the set at 7.4 s, where the pre-fix
release at 10.65 s had loaded one slot. Boot-to-first-note: 9.07 s (pre-fix: first note 11.74 s on one channel only).

Owner confirmation: *"I think it works properly now, I see all tracks sending midinotes directly after boot."*
