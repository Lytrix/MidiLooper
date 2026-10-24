# 2.1 prepared pair — device gate (clean pair)

Date: 2026-10-24
Board: `/dev/cu.usbmodem154944801`
Bench: `151444` campaign tree; only the authorized scratch slot (CLI track 5 / slot 1 = firmware t=4,s=0) was
cleared for this run. Other campaign slots untouched.
Loader: Homebrew `teensy_loader_cli` 2.3.

## Clean-slot recipe (standard actions, no hand-rolled MIDI)

The base preset does not retire committed passes, so the first cold pair accumulated the previous run (32/40 vs
16/20) with the same distinct notes. The scratch slot was cleared with the documented actions:

1. `ensure_loop_edit_before_record` (LOOP_EDIT proven by the long edit press ack).
2. `slots.press_track(5)` then `slots.press_slot_record(1)` (select the scratch row/slot).
3. `slots.press_slot_clear(1)` through `stopped_preparation.clear_slot`; outcome
   `ClearStatus.CLEARED`, serial `Track cleared @ tick 3448 / 440 (active_slot=0)`.

The retention tool's own `--prepare-only` clear refused on its internal tempo read-back (`CC102`), so the lighter
standard `clear_slot` path was used. Only slot 1 was cleared.

## Clean pair (same args, `--clock-source host --tempo-bpm 120`, no boot reset)

| run | image | result | overdub | second | spans | bars | display |
|---|---|---|---|---|---|---|---|
| `2026-10-24-2.1-base-ship-clean` | shipping `e5a26dd68ecf9a46…` | `INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)`; `record_stored_revt_missing` | 16 / 15, last 1488 | 20 / 7, last 1520 | 1296 / 1209 | 2/2 | overdub/transport ok |
| `2026-10-24-2.1-base-cold-clean-2` | cold `e7e4b4fc4934d833…` | `PASS` | 16 / 15, last 1448 | 20 / 7, last 1448 | 1296 / 1209 | 2/2 | overdub/transport ok |

Both images store the **same distinct inventory and the same stored event counts** (16 and 20), the same minimum
spans (1296 and 1209 ticks) and 2/2 bars; the display and undo/redo checks pass. The `last` tick differs by 40–72
ticks (record/overdub timing jitter), not a content difference. The loop is the 2-bar 1536.

The shipping label is the known host evidence gate on the shipping image (REVT instrumentation absent), the same
gate accepted for the 1.4 shipping base; it is not a content failure.

## Not rerun

- Mixed switch (`accepted_sequence_mixed`): setup-limited (seed_b finalization release) and it targets the campaign
  slots 4/5. The 2.1 switch route is proven natively (`test_2_1_the_full_take_switch_serves_the_merged_fallback`);
  the mixed-oracle limitation is carried, not waived.

## Rollback

- Shipping `e5a26dd68ecf9a46…` ↔ cold `e7e4b4fc4934d833…`.
- Prior shipping `15103188953f919d` (increment 1) / `09a5ae69…` (stable-demand).
- Board left on the shipping image.
