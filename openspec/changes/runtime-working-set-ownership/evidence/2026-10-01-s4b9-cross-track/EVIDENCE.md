# EVIDENCE — S4b.9 cross-track switch while a predecessor finalizes (partially observed)

**Date:** 2026-10-01
**Runs:** `captures/s4b9-cross-track-01 … -07` (attempt 03 is the primary record: the leg completed with the
destination's occupancy present; 01/02/04 corroborate; 05/06/07 are the attempts at the leg's remaining clause and
each failed *before* its intervention)
**Image:** hex sha256 `cdab16f4e5f65988a7383b9fd4633d965c76a77c226c73ce462abfa6e7afde3c` (re-flashed unchanged
twice in the session; builds byte-identical)
**Entry point:** `scripts/hitl_s4b8_slot_switch_during_finalization.py` with `--track-number 4 --loop-slot 1
--switch-track-number 5 --switch-loop-slot 2` (attempts 01–04) or `… 3` (attempts 05–07)
**Authorization:** track 4 (the leg's own A track) and all track-5 slots, plus upload — granted for this session.
**Destination preparation:** attempt 03's destination was populated by a seed leg in the same boot
(`captures/s4b9-seed-t5s2`: `Recording stopped @ tick 252`, `PERS,result,…,ok`).

## The gesture and what the device did (attempt 03, verbatim)

```
[767.839] [INFO] MIDI Button A: Stop Overdub
[767.840] [INFO] [TRACK] Reentry window open slot=0
[768.272] [INFO] Button press: Track 5 (short)
[768.284] [DEBUG] [MIDI] Sent loop length CC feedback: 2 bars (1536 ticks) -> CC=1
[768.292] [INFO] Selected track 5
[768.367] [INFO] Reentry released slot=0 generation=42
[768.901] [INFO] Button press: Loop 2 (short)
[768.908] [INFO] Loop 2: Toggle Play/Stop
[768.908] [DEBUG] [TRACK] Playback started @ tick 3413
```

with A's own stop reaching its terminal exactly once, on track 4:

```
#CAP,762693430,DIAG,reentry,admit,1031,6,0,0
#CAP,767840374,DIAG,reentry,bound,3005,6,0,0
#CAP,767840412,DIAG,reentry_window_open,0,3005,1536
#CAP,768367547,DIAG,canonical_release,0,42,1536,8
```

The runner's own ledger locates the presses: `stop_button_release_sent`, then
`switch_track_select_sent {track_index: 4}` at +0.433 s and
`switch_slot_select_sent {target_slot: 2, target_track_index: 4}` at +1.062 s.

## What is claimed

1. **The track switch is accepted inside A's finalization window.** The track press lands at 768.272 s — after A's
   stop was *admitted* (762.693 s) and **before** it was *released* (768.367 s) — and the device answers
   `Selected track 5` at 768.292 s. The switch did not disturb that acceptance: A's stop still reached its
   released terminal with its own generation (42).
2. **A's occupancy is not released by the switch.** The session contains **one** `canonical_release` for A's slot,
   at the reentry-released edge of A's own stop (768.367 s) and carrying A's own generation — and A's content
   stays on track 4. Nothing about the release is attributable to the switch.
3. **The selection/display authority moves with the switch.** `Selected track 5`, `LOOP_EDIT session refreshed for
   focus change`, `Edit state reset for new track`, and the geometry the device then reports is read from **track
   5's stored content** (`Sent loop length CC feedback: 2 bars (1536 ticks)`), not from A's loop — i.e. the new
   track's own frame, not a stale one of the outgoing track.
4. **The transport stays MIDI-continuous across the switch.** `overflow_free: true` and `call_site_gate_ok: true`
   in attempts 01–04; attempts 01 and 04 have an empty `timing_warnings` list, while 02/03 additionally carry
   `record_first_note_too_late` (a *record*-leg latency item, not one of this gate's boundary terms).

## The clause this leg does **not** yet observe, and the device semantics behind it

The leg's text also asks that *track 5's inbound occupancy is provisioned at a cold boundary (S4b.7) with no
`trySlot` on the clock path*. Neither half of that shape occurred, because the press after the track switch is
resolved by the **new track's own state**, not as a cross-track switch:

* destination slot with **no RAM content** → the press is the **record carrier**: attempt 01 `Loop 2: Start
  Recording` at 461.147 s, attempt 02 at 564.636 s (the harness's documented "an empty destination takes the
  record carrier" rule, now measured on the cross-track path);
* press on the new track's **active** slot **with** content → `Loop 2: Toggle Play/Stop` + `Playback started`
  (attempts 03 at 768.908 s and 04 at 45.370 s).

So the shape whose inbound occupancy S4b.7 provisions — a `Queued playback switch` to a **non-active,
catalog-filled** destination slot — needs `--switch-loop-slot 3` (track 5's Loop 3 is catalog-filled but not its
active slot). Three attempts at exactly that were made and **each failed before its intervention**, never on the
leg's own assertions:

| attempt | abort | what it is |
|---|---|---|
| 05 | `firmware capture ring discarded unreadable content` | capture-integrity gate (a second leg in the same boot) |
| 06 | `record stop did not reach PLAYING` | the record leg's own stop confirmation |
| 07 | `overdub start refused: no predecessor stop-completion receipt` | the successor-start precondition, **even at the 25 s wait** |

## Holder census

| run | first `[Memory] ext` row | last |
|---|---|---|
| attempt 01 | 433.00 s live 4751 KB, blocks 1038/1256, chunks_free 395, heap_free 136 KB | 503.05 s live 7878 KB, blocks 1330/1334, chunks_free 361, heap_free 132 KB |
| attempt 03 | 739.36 s live 7957 KB, blocks 1385/1453, chunks_free 346, heap_free 124 KB | 835.81 s live 7926 KB, blocks 1392/1494, chunks_free 331, heap_free 116 KB |

`refused=0/0B` in every row of every attempt — no pool refusal appears anywhere in this leg.

## Not claimed

* **No commit.** None of the seven attempts contains a `LoopEnd playback commit` row: attempts 01–04 never queued
  a switch (the destination press resolved to record/toggle as above), and 05–07 never reached the intervention.
* **S4b.9 is therefore `[~]`, not checked:** the acceptance, occupancy-pinning, selection-transfer and
  MIDI-continuity clauses are observed; the cold-boundary **provisioning** clause remains unexercised on the
  cross-track path (it is carried on device by
  [`../2026-10-01-s4b-repeated-switches/EVIDENCE.md`](../2026-10-01-s4b-repeated-switches/EVIDENCE.md)'s queued
  switch plus the S4b.6/S4b.7 native fixtures).
* The base recipe's own verdicts are `INCONCLUSIVE_CAPTURE` in every attempt (its documented overdub issues), and
  attempts 02/03 also aborted at teardown with `!CAP_IDENTITY_STATS: response fence missing` — neither is this
  leg's subject.
* **Writes this leg made, recorded:** track 4 / Loop 1 (the predecessor take, 2 bars in attempt 03, 3 bars in
  attempt 01/04's record legs) and, in attempts 01/02, an incidental destination recording on track 5 / Loop 2
  (18 bars, cut by the transport stop, so it did not commit); the seed leg wrote track 5 / Loop 2 (2 bars). All on
  tracks/slots this session authorized.

## Attempts at the clause, continued (08–11) — and the device state behind both failure modes

Attempt 08 (first run after a flash) targeted Loop 3 and showed the destination **empty in the catalog too**
(`Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)`), so that press became a record: in that boot,
track 5's only *filled* slot was its **active** one (Loop 2, `2 bars` on selection) — the shape that toggles. The
clause therefore also needs a **second** filled destination slot. Route taken: seed Loop 3
(`captures/s4b9-seed2-t5s3`: `Loop 3: Start Recording`, `Recording stopped @ tick 324 (recStart=0 length=1536)`,
`PERS,result,…,ok`), then re-flash so that Loop 3 is track 5's *selected* slot and Loop 2 is filled-but-non-active,
then run the leg at Loop 2.

| attempt | state | abort (before its intervention) |
|---|---|---|
| 09 | after the seed, same boot | `overdub start refused: no predecessor stop-completion receipt` |
| 10 | first run after a flash | same receipt refusal |
| 11 | first run after a flash + 150 s of settling | `firmware capture ring discarded unreadable content` |

**The device state behind both modes is measured, not assumed.** That boot queues **64 restore requests** — one per
slot of a library this session has filled — and drains all of them (`RRQ,…,queued` 64 rows, 64 `,terminal,` rows),
so the SD/persistence lane carries a large batch alongside the leg. That is when the stop-completion receipt arrives
late or not at all (the same refusal also ends run 02 and attempts 07/09/10), and the same batch's log volume is
what fills the capture ring (attempts 05/11). **Neither mode is the leg's own assertion.**
**On the plus side, the same boot confirms the Loop 8 churn is gone:** `RRQ,4,7` is an ordinary
queued/running/terminal triple with **no** `LSV1 parse failed` row, i.e. that body loads normally now (consistent
with the read-only follow-up in
[`../2026-10-01-restore-retry-churn/FINDING.md`](../2026-10-01-restore-retry-churn/FINDING.md)).
