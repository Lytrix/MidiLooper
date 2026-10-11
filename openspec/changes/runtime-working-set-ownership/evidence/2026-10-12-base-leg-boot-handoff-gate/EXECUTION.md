# Base-leg boot-start gate and edit-mode proof race — execution packet

Slice: host tooling (`scripts/hitl/**`), owner-directed on 2026-10-12 from the I3a device
qualification. Related: **I3a — boot** in
[`docs/Plans/openspec_iterative_closeout_plan.md`](../../../../../docs/Plans/openspec_iterative_closeout_plan.md)
and [`evidence/2026-10-10-edge-effect-boot/I3A-QUALIFICATION-PACKET.md`](2026-10-10-edge-effect-boot/I3A-QUALIFICATION-PACKET.md).

## Identity and scope

- **Owner instruction (verbatim, 2026-10-12):** *"1. something in the test is starting during bootup so too
  soon which makes the display hang after boot. 2. the legacy base hitl already passes with 2 overdubs (see
  earlier captures) and runs just fine, use the logic of that one as a working foundation to alter the
  stimulus (like using external midi clock to ensure midi note timing)"* and *"fix the stimulus first"*.
- **Status:** host slice; device re-run follows this packet.
- **Files:** new `scripts/hitl/boot_handoff.py`; `scripts/hitl/legacy_record_baseline.py` (the gate's one call
  site); `scripts/hitl/edit_mode_precondition.py` (the proof race); new
  `scripts/test_hitl_boot_handoff.py`; `scripts/test_edit_mode_precondition.py`. No firmware, no marker,
  no stimulus/threshold change, no other driver.
- **Non-goals:** the second-overdub `no stop-completion receipt` refusal (S7.5b.5's own slice), the boot-title
  first-frame repair (landed as `e6c1118f`), `legacy_edit_baseline.py` and the scenario drivers (they share
  the model, not this call path), and any change to the restore/`post_restore_census` readiness contract.
- **Authority named, not bypassed:** `response-driven-hitl-acceptance`'s tasks record **PX1 (2026-09-29)** —
  *"the boot wait itself is gone"*: `auto` attaches a stopped device with no reset, ends the settle on that
  evidence, and reads the firmware's `!REBOOT` acknowledgement instead of a blind window; `s4f22-step11-h3`
  then ran clear → record → overdub → second overdub straight after boot. This packet **deliberately amends
  that policy under the owner's authority**, and it is not the removed blind wait: it is a bounded wait on
  two device rows, it never fires for a run that joined an already-booted device, and on timeout it refuses
  by name instead of pressing. The amendment is recorded in that change's tasks so its prose cannot describe
  the old behavior.

## Measured basis (same fixture, same image family, three runs)

| | `…-t8s4-01` (PASS, image `1f9282b6…`) | `…-rerun-t8s4-02` (abort) | `…-rerun-t8s4-03` (abort) |
|---|---|---|---|
| first control press | 7,406,543 µs (Track 8) | 9,440,738 µs — Track 8 **+ Edit Mode (note 38)** + Loop 4 inside 1.2 ms | 7,374,166 µs (Track 8) |
| `DisplayManager: Boot setup complete` | ~15 s | ~15.5 s | ~17 s |
| `#CAP,…DIAG,loop_rem,boot_commit,…` | 17,521,199 µs | 15,791,140 µs | 17,838,519 µs |
| leg outcome | PASS (2 overdubs + undo/redo) | `recording not confirmed before note stream` | `second overdub start refused: no stop-completion receipt` |

- The leg presses **7–10 s before the device's own boot handoff rows** in every run (owner's point 1, measured).
- `…-rerun-t8s4-02` also carries the second defect: the `LOOP_EDIT` proof missed the boot's own ack, so the
  helper pressed the edit button and the device read it as a **1 ms short press**
  (`[BTN] Short press detected: duration=1 < effectiveLongPress=600` → `Button press: Edit Mode (short)` →
  `MIDI Edit Mode: Short press - cycled LOOP_EDIT ↔ NOTE_EDIT` → `Edit session: NOTE_EDIT`), flipping the mode
  so the arm could not land. The passing run of the same invocation printed
  `[hitl] LOOP_EDIT already proven by the capture's ack` and sent no press.
- **Named, not smoothed:** the third run's abort is the *pre-existing* S7.5b.5 class (the same fixture passed
  in run 1 on the previous image), so this slice claims no fix for it. The passing run also shows that an early
  press is not by itself fatal — the owner's panel observation (display hangs after boot when the test drives
  during boot) is the authority for the gate, and it is recorded as such rather than inferred from these rows.

## Oracle fixed before the code change

1. **Fresh-boot run, no handoff rows yet:** the gate returns `timeout` inside its bound, prints the named
   refusal, and **sends no control message** — the leg never presses into an unfinished boot.
2. **Fresh-boot run, both handoff rows present:** the gate returns `complete` and the leg proceeds exactly as
   before (no extra delay beyond the poll interval).
3. **Run that joined an already-booted device** (no `DIAG,boot_reset` in the window): the gate returns
   `not-fresh-boot` **without waiting**, so attach latency for the reuse path is unchanged.
4. **Stale rows do not count:** a banner/handoff pair outside the examined window must not satisfy the gate.
5. **Edit-mode proof:** a mode ack that arrives late but inside the bounded window produces **no** press; a
   genuinely unproven mode still produces exactly one **long** press at the configured length; the 1 ms press
   is impossible to emit silently.
6. **Counterexample that must fail:** reverting the gate (or its call-site order) must fail case 1 and the
   source-order tripwire, not merely pass a paraphrase.

## Verification recipe

- Focused: `.venv/bin/python scripts/test_hitl_boot_handoff.py`,
  `.venv/bin/python scripts/test_edit_mode_precondition.py`.
- Neighbours: `scripts/test_clear_before_record_precondition.py`, `scripts/test_hitl_boot_settle.py`,
  `scripts/test_hitl_host_grid_record.py`, `scripts/test_baseline_canonical_args.py`.
- Repository gates: `pio test -e native` before finishing (no C++ change, run because the rule is
  unconditional before finishing); no firmware build is required by this slice, and no native evidence is
  promoted to a device claim.
- Device leg (owner-authorized, panel observed): the same registered stimulus `STIM-BASE-2-2-01@v1` through
  `host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --boot-reset
  --capture-boot-wait 30 --clock-source host --tempo-bpm 300` — external MIDI clock per the owner's point 2 —
  asserting no control press before the handoff rows, then the leg's own verdict.
- Evidence: this folder; the I3a packet and the plan handoff are updated from it.

## Completion record

*(filled after the implementation and the device leg; see the appended record below)*

## Completion record (host slice)

**Landed.** `scripts/hitl/boot_handoff.py` (`fresh_boot_in_window`, `boot_handoff_complete`,
`wait_for_boot_handoff` → `not-fresh-boot` / `complete` / `timeout`; reads the collector's rows only,
never writes), its one call site in `legacy_record_baseline.run()` immediately after the serial-collector
setup and before every control gesture (prints `[hitl] boot handoff: <result>`; on `timeout` prints the
named refusal to stderr and raises `SystemExit(3)` — no gesture into an unfinished boot), and the
proof-race repair in `edit_mode_precondition.ensure_loop_edit_before_record` (bounded pre-press wait for
an already-arriving mode ack; `DEVICE_LONG_PRESS_THRESHOLD_MS = 600` guard so a degenerate press length
cannot be sent silently; the long-press-not-short rationale and the fail-closed refusal unchanged).

**Verified locally.** `scripts/test_hitl_boot_handoff.py` **13/13**, `scripts/test_edit_mode_precondition.py`
**13/13**, `scripts/test_clear_before_record_precondition.py` **42/42**, `scripts/test_hitl_boot_settle.py`
**16/16**, `scripts/test_hitl_host_grid_record.py` **23/23**, `scripts/test_baseline_canonical_args.py`
**3/3** — all exit 0 (110 tests). `pio test -e native` **3,072 cases / 3,065 succeeded / 7 skipped /
0 failed** (the recorded baseline). Two evidence-path typos (`…-rerun-t08s4-02`) were corrected in this
slice's own new comments; nothing else of the fixer's work was altered.

**Verified in production (`captures/i3a-edge-effect-boot-t8s4-04`, image `e6c1118f` / `7678b719…`).**
`[hitl] boot handoff: complete` appears before the first gesture; the leg's first control press moves to
device micros **12,052,522** against `#CAP,11770543,DIAG,loop_rem,boot_commit` — i.e. the first press now
*follows* the handoff (it preceded it by ~10 s in runs 01/03). No edit-mode press was sent
(`[hitl] LOOP_EDIT already proven by the capture's ack`, twice). **The leg then completed every phase** —
`ARMED→RECORDING` 1, `RECORDING→STOPPED_RECORDING` 1, `STOPPED_RECORDING→PLAYING` 1, `PLAYING→OVERDUBBING` 2,
`OVERDUBBING→PLAYING` 2, RECA 2 / RECS 8, undo=1 redo=1, no abort reason and no evidence gap — where run 03 had
aborted at the second-overdub refusal. `record loop length 1536/1536 (raw 1536)`, stored record grid
`48-48, bad 0/2`, first-note capture latency `0/8` clocks (bound 12).

**Named, not smoothed — a separate device finding this run surfaced (not this slice's defect, not waived):**
the same run's verdict is **`FAIL (FIRMWARE_CONTRADICTION)`** on content scoring,
`overdub_stored_span_too_short` and `second_overdub_stored_span_too_short` — stored overdub spans
`last=2241` / `min_span=1296` / `bars=3/2` in a 1536-tick loop, `span_ok=False` on both, while the identical
shape scored `span_ok=True` in the device-clock run 01. That is the stored-span/wrap content class the S7.5a
packet already records as owed; it needs its own leg and it is handed over, not folded into this slice.

**Not run / not claimed:** no firmware, stimulus, threshold, marker or SD change; no `dev`-branch or Git
delivery (host-tooling commits on the working branch are the owner's call, not automatic); no claim that the
gate fixes the content contradiction.
