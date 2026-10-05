# The switch leg: the gesture is safe and reproducible; the *demand* is blocked by a cold target (measured)

Evidence for `EXECUTION.md` in this directory. One device run succeeded and it changed the picture: the switch gesture
behaves exactly as the code promised, the lane still had no work, and **the reason is now measured rather than guessed**.

## The driver half — delivered

`run_windowed_content_parity` gained a **switch leg**, gated on the transport actually running: select a filled
non-active target, press its record control, wait for the switch, then the existing stop-and-drain. Absent by default, so
the base leg's two earlier runs stay comparable.

**Two host defects were found by driving it, and both were mine:**

1. The wrapper's own flags were forwarded to the reused leg, whose argument parser is **strict** — the first attempt
   aborted inside the leg before it pressed anything (`unrecognized arguments: --switch-target-slot 4`). The flags are now
   consumed by the wrapper (`split_flag`), with a pin asserting they never reach the leg.
2. The earlier port lookup had already taught this lesson once (the drain read `args.serial_port` while the runner keeps
   it in the leg's arguments); the same shape recurred here and is now pinned rather than remembered.

| ID | Change | Result |
|---|---|---|
| **M1** | the switch leg's press order reversed (record before select) | the order pin fails; the other 21 stay green |
| **M2** | the transport gate removed from the post-leg phase | the gate pin fails with `FileNotFoundError: /dev/cu.stub` — i.e. the phase *proceeds* and touches the device while the transport is stopped |

Both restored **byte-identically** (`scripts/hitl/scenarios/windowed_content_parity.py` sha256 `9fdda38ac53815bc…`).

## The device run (01): the gesture is safe, and the hold is named

Read from `captures/s64-switch-01/`:

```
[145.752] [INFO] Button press: Loop 4 (short)
[145.839] [INFO] Loop 4: Queued playback switch
[146.431] [INFO] Loop 4: Reaffirmed playback switch
[154.389] [INFO] Slot switch hold slot_switch_hold_exceeded track=7 target=3 age=3072 ticks
```

* **`Queued playback switch` — not `Start Recording`.** The safety argument was read from `MidiButtonActions.cpp` before
  anything was driven (`slotCanArmForRecord = !hasCommittedPassesInSlot(slot)` makes the recording branch unreachable for
  a filled slot while the track plays) and the device **confirms it on hardware**. The second press *reaffirmed* the
  queue rather than arming a record. The target (bench slot 4, a 2-bar take with 3 passes) was not written to.
* **The switch was held and reported by name** at its bound (`age=3072 ticks` = 4 bars) — the S4b.7 behaviour, once per
  episode, as a **report and not a decision**.

## Why the lane *still* had no work — pinned by the rows

The lane's own rows in the drained capture: **85 `win_gap` blocks, 66 with `running=1`**, and **every one
`eligible=0`, `progress=0`**, `permitted=64`, `overruns=0`; **zero** `win_readiness` receipts; **zero**
`build_complete` publications. So the switch leg did **not** give the lane work either, and the mechanism is visible in
the same capture:

```
#CAP,145771236,RRQ,7,3,0,0,queued,none,none          <- the TARGET's on-demand restore is queued
#CAP,145778737,RRQ,7,2,0,0,queued,none,none
#CAP,145849837,DIAG,capture_prewarm,track=7,slot=3,full=0,ready=0,credits=0,free_chunks=448,heap_free=151552
```

and **no restore terminal at all** (`LSV1` rows 0, no `restore` completion row) before the capture ended.

**The finding:** the switch target was **cold** — its content lives on the card and the runtime holds none of it — so the
device queued the target's restore, the restore **never completed inside the run**, and therefore *nothing could be
prepared*: the legacy merged-window path had no source, and the lane's launch preparation had none either. `eligible=0`
and the absence of receipts follow directly, and the switch hold is the device **naming exactly that** rather than a
silent no-op. This is a different reason from the previous slices' (there, nothing was owed; here, something was owed and
its **source was not resident yet**).

**A second attempt was made and is recorded as a setup failure, not evidence:** `captures/s64-switch-02` aborted at its
own precondition (`clear failed` after the retry), so the leg never reached the switch. Nothing about it is read as a
result.

## What this means for the next attempt (precise, and cheap)

The gesture and the readout are now in place; what is missing is a **resident source** at switch time. Two ways, both
driver-level and neither requiring firmware work:

1. **Warm the target first:** play the target slot once in the same boot (so its runtime content and current root are
   resident), then switch **away** and **back** to it — the switch-back needs no restore, so the launch can prepare a
   window and the lane gets its demand.
2. **Or let the queued restore complete** (it is served in idle windows) and re-run the same command unchanged; the
   restore for track 8 slot 4 was already queued by run 01.

A third possibility worth naming but not chosen: a target inside the *same* loop family as the playing slot (a sibling of
an already-resident slot) may avoid the restore entirely.

## Verification

* `test_windowed_content_parity_verify` — **22 tests, OK** (flag parsing, the order pin, the gate pin, the press
  controls, the device-time rule, the port lookup).
* `test_hitl_stimulus_contract` 26, `test_accepted_sequence_scenario` 40 — OK; `pio test -e native`
  **2796 / 2789 succeeded / 7 skipped / 0 failed**.
* Strict validation valid for both changes; `git diff --check` clean; `src`/`include` diffs empty (**no firmware change**
  in this slice).
* Device: run 01 completed end to end with the switch leg; run 02 aborted at its precondition and is recorded as such.

## Not claimed

* That the lane was given work — it was **not**, and the reason is measured (cold target, restore never completed).
* F-7/F-8 or AC-8: no receipt, no publication, no boundary crossing.
* The envelope: grain duration, service gaps under load, contention, external clock and residency remain **NOT RUN**.
* That the switch *committed*: it was held and named, which is the firmware's own report, not a failure of this slice.

---

## Correction, same session — run 04 revises the cold-target reading and names a sharper blocker

Two further runs were made. The preparation for them matters: the **reset must be a flash of the qualification image**,
because `scripts/hitl_boot_census.py` (the documented reset-by-re-upload path) hard-codes
`pio run -e teensy41-capture-serial -t upload` — the **default** env, where the rolling lane is compiled **off**. Using it
would silently produce a run whose lane rows mean nothing, so the reset used here was
`pio run -e teensy41-capture-serial-window-qualification -t upload` (its distinctive RAM1 line `code 392,232 + padding 984`
confirms which image is on the board).

* **Runs 02 and 03** (no reboot between runs) aborted at the leg's own precondition — `clear failed` after its retry —
  so they never reached the switch. Recorded as setup failures, not evidence. The pattern is consistent: run 01 started
  from a fresh boot, these did not.
* **Run 04** (fresh qualification flash, then the same switch leg) reached the switch **and completed the target's
  restore**: `RRQ,7,3,0,0,queued` → `RRQ,7,3,0,0,running` → **`RRQ,7,3,0,0,terminal,none,committed`**. So for this run the
  "cold target" reading of run 01 does **not** apply — the source was resident — and the switch **still** held
  (`slot_switch_hold_exceeded track=7 target=3 age=3073 ticks`).

**What the rows show instead, and it is sharper than either earlier reading.** The whole window between the queue and the
hold is 29 rows, and they contain **no preparation activity for the target at all**: no `capture_prewarm,track=7,slot=3`
row (run 01 had one), no provision row, no `playback_build` for the slot, no launch row. The queue is reaffirmed and then
nothing happens for eight seconds until the bound reports:

```
[132.981] Loop edit session entered — sent loop start/length/tempo fader feedback
[132.981] [TRACK] LOOP_EDIT session refreshed for focus change
[133.026] Loop 4: Queued playback switch
[133.630] Loop 4: Reaffirmed playback switch
[141.575] Slot switch hold slot_switch_hold_exceeded track=7 target=3 age=3073 ticks
```

**The named candidate, from the row immediately before the queue:** the *slot selection itself* opened a **LOOP_EDIT
session** (`Loop edit session entered`, `LOOP_EDIT session refreshed for focus change`), and the pending switch's
preparation is then never reached — which is consistent with the lane's own rules (`serviceRollingPlaybackWindow` refuses
while an edit session is active) and with the S4b device legs, whose switches were prepared while the track played
*without* one. That is now the leading explanation, and it is a **hypothesis with evidence**, not a measured mechanism:
what is measured is the complete absence of preparation activity with the edit session open.

**So the next attempt is precise and small:** exit the edit session (or select the target without entering one) **before**
pressing its record control, then switch. Everything else — the switch gesture, the gating, the drain, the device-time
reader — is already in place and red-proved.

**The lane result is unchanged in both runs:** 45 and 85 `win_gap` blocks respectively (29 and 66 with `running=1`), every
one `eligible=0`/`progress=0`, zero receipts, zero publications. The lane is dispatched under transport and is never given
work, and each run's own capture says why for that run.
