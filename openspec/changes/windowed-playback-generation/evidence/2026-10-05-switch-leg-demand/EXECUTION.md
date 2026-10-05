# The switch leg: giving the window lane a demand on hardware (task 6.4's F-7/F-8 driver)

Execution packet, written **before** the edits. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), task 6.4's wording
(*"verify the profile is registered, authorized and **used** for the F-7/F-8 device legs"*), and the measured state the
previous slice handed over: the lane is **dispatched under transport** (50 blocks with `running=1`) but was **never given
work** (`eligible=progress=0` everywhere, no receipt), because a plain record-and-play owes no window.

## Identity and scope

- **Slice:** add a **switch leg** to the windowed profile's driver — after the reused leg has recorded and is playing, the
  wrapper selects a **filled, non-active target** and presses its record control, which the firmware turns into a
  **queued playback switch** to that target. A switching target *owes* a window (the launch path prepares it), so the lane
  finally has work under transport, and the post-stop drain (already built) reads it.
- **Primary files:** `scripts/hitl/scenarios/windowed_content_parity.py` (the driver),
  `scripts/test_windowed_content_parity_verify.py` (its pins), and this directory.
- **Non-goals:** no firmware change; no new stimulus profile (the leg belongs to `STIM-WINDOW-CONTENT-01`); no claim that
  the envelope is measured — this slice makes it *observable*; no widening of the profile's declared scope.

## Why this is the right gesture, and why it cannot destroy the target

Read from `src/MidiButtonActions.cpp` before anything was driven, because the target holds content:

```cpp
const bool slotHasCommittedMidi = track.hasCommittedPassesInSlot(slotIndex);
const bool slotCanArmForRecord = !slotHasCommittedMidi;
...
} else if (track.isPlaying()) {
    if (slotCanArmForRecord) {                 // EMPTY slot -> the recording flow, then `return`
        ...
    }
    // Filled slot while playing: queue playback switch
    logger.info("Loop %d: Queued playback switch (filled slot)", slotIndex + 1);
    queuePlayingSlotSwitch(trackIdx, track, slotIndex, now);
}
```

With the track **playing** and the target **filled**, the recording branch is unreachable (`slotCanArmForRecord` is
false), so the press can only queue a playback switch. That is the safety argument, and it is a code fact rather than a
hope. The leg's target is the bench slot with **24 bars** of committed content — the **demoted** shape (> the 16-bar
bounded display window) the native fixtures use for the windowed launch — inside the profile's declared scope
(`track 8 slots 4,5`), and the pre/post content readback is captured so non-destruction is evidenced, not asserted.

## Oracle fixed before behaviour edits

| Case | Action | Expected |
|---|---|---|
| the switch is **requested** | select the target slot, press record while the transport runs | the device logs `Queued playback switch (filled slot)` for that slot — never `Start Recording` |
| the lane is **given work** | drive the switch, wait, then stop and drain | drain blocks with `eligible_turns > 0` and/or a `win_readiness` receipt (the launch's owed window), where the previous runs measured zero of both |
| the switch **commits** | the same capture | a `LoopEnd playback commit` for the target, or a named retention if it does not; either is recorded, neither is smoothed |
| no regression in the base leg | `--switch-target-slot` absent | the driver behaves exactly as before (selection + leg + drain) |

## Controls and coverage

- Reused seams: `select_loop_slot` (the slot's own select control), `_send_short_press` with `RECORD_BUTTON_NOTE`,
  `SerialCaptureCollector`, `press_global_transport_stop`, `last_transport_row_running`, the device-time rule in
  `hitl.verify.windowed_lane`.
- New host pins: the flag's parsing (default = no switch leg), the press **order** (select first, then record — a record
  press without the selection would hit whatever slot was active), the gate (the switch leg only runs when the transport
  is running, because a switch while stopped is not the F-7/F-8 shape), and that the base behaviour is unchanged without
  the flag.
- **Red-proofs planned:** M1 the selection dropped (the record press would hit the wrong slot) → the order pin fails;
  M2 the gate removed (the switch leg runs with the transport stopped) → the gate pin fails.

## Verification recipe

- Host: the windowed verifier suite plus the registry/scenario suites; `pio test -e native`; strict validation;
  `git diff --check`.
- Device: read the target's content read-only (**before**), flash the qualification image (unchanged from the last run),
  drive with the switch leg, read the drain (**after**), and read the target's content again to evidence non-destruction.
- Everything the run does **not** measure is named; the envelope (grain duration, gaps under load, contention, external
  clock, residency) still needs the remaining 6.5 legs.

## Completion record

- [x] Switch leg implemented and gated on a running transport; base behaviour unchanged without the flag.
- [x] Host pins written (flag parsing/consumption, the press order, the gate, the press controls); **M1** (order reversed)
  and **M2** (gate removed) red-prove them, restored byte-identically (`sha256 9fdda38ac53815bc…`).
- [x] Host 22/22 + registry/scenario suites; native **2796 / 2789 succeeded / 7 skipped / 0 failed**; both changes validate
  strict; `git diff --check` clean; **no firmware line changed**.
- [x] Device run 01: **`Loop 4: Queued playback switch`** observed (never `Start Recording` — the safety argument held on
  hardware), the switch **held and named** at its bound, and the drain read. **The lane was still not given work**, and
  the *reason is measured*: the target was **cold** (its on-demand restore was queued — `RRQ,7,3,…queued`,
  `capture_prewarm … ready=0` — and never completed inside the run), so nothing could be prepared. Run 02 aborted at its
  own precondition and is recorded as a setup failure, not evidence.
- [x] Evidence written; the next step is named precisely (warm the target first, or let the queued restore complete).
- [ ] Task effect: the gesture half of 6.4's "used" clause is supplied; **the demand is still not observed**, so 6.4/6.5
  stay open and now carry a measured reason rather than a hypothesis.
