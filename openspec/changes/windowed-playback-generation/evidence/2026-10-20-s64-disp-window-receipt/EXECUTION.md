# 6.4 — display-window transition receipt + timestamp-ordered read: device execution record

Task: **6.4** (`tasks.md`), continuing
[`s64-multi-run`](../2026-10-20-s64-multi-run/EXECUTION.md). Contract and non-goals: this slice's
[`EXECUTION-PACKET.md`](EXECUTION-PACKET.md). Lane terms (F-7, F-8, AC-8) and the `oracle_complete` deferrals are
outside this slice and unchanged (`oracle_complete: false` on every run below, same two deferred terms).

Image: `teensy41-capture-serial-window-qualification`, HEX
**`3ef153c5deb5f5337675ca96bb2b75872730f2efd7eab6b30e1a961c1c8bffdc`**, built from `a3c5d051` + this slice's tree.
RAM1: `variables 96,192 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,880`, approved floor `padding >= 840`
(default image: `code 392,216 + padding 1,000`, byte-identical to the 4.5 baseline). Device `/dev/cu.usbmodem154944801`;
no shutdown latch before or after.

## What the device now reports

`DIAG,disp_window,t=<track>,s=<slot>,st=<state>,ed=<noteSessionOpen>,es=<editSession>,len=<ticks>,w=<start>,b=<bars>` —
one row per **change** of what the display holds and shows, sampled by the window owner (not the paint path) while the
track plays, plus one closing row when it leaves `PLAYING`. Tier-A through the existing `DIAG,disp_` family; delivered
direct when USB has room, ring otherwise.

## Runs

| # | shape | receipt rows | lane | leg | ring (tier-A) | outcome |
|---|---|---|---|---|---|---|
| 01 | post-flash, **first draft** (raw-start dedupe, sampled in every state) | **2 757** | red | red | **`tier_a_evicted=1`** | flood: a row per sample during recording (loop length grows every sample); it also filled the Tier-A ring |
| 02 | post-flash, band-quantised + PLAYING-gated firmware | 11 | green | red | 0 | leg read `unique_starts=[3851, 4401]`: the pre-entry band row (108.4 s) was being compared with the entry row (109.9 s) |
| 03 | `--no-boot-reset` | 15 | **green (6/6)** | **green** | 0 | **green** |
| 04 | `--no-boot-reset` | 15 | **green (6/6)** | **green** | 0 | **green** |
| 05 | `--no-boot-reset` | 15 | **green (6/6)** | **green** | 0 | **green** |

Three consecutive verdict-grade combined greens (03/04/05) on one boot: leg `ok`, all six lane checks, snapshot checks
and ring integrity `critical_evidence_loss_rows=0`. The leg's hold term is now proved from the receipt itself
(`hold_verify_mode=disp_window_rows`, 6 rows, ≥2 distinct starts) instead of the indirect release witness; the only
remaining leg warning on 03/04/05 is the measured `RECORDING not confirmed; retrying record press` host retry, which is
the state-aware guard doing its job.

Evidence dirs: `captures/i5-s64-receipt-24bar-01` … `-05` (gitignored capture data; the reports are named in each).

## What each failure taught (kept, not smoothed)

1. **A receipt is only cheap if its key is coarse where the data is dense.** Raw window start changes on nearly every
   sample (measured ~24 ticks per 63 ms) and loop length changes on nearly every sample while recording. The first
   draft therefore streamed 2 757 Tier-A rows and *displaced* a Tier-A row — a self-inflicted version of the very
   problem this slice exists to remove. Fixed with the **bar-band** key and the **PLAYING** gate; cost is now 15 rows
   for a full run, and no eviction in 02–05.
2. **The entry sample can land on either side of the acknowledgement.** The receipt is stamped on the device clock; the
   leg's `Edit session: NOTE_EDIT` marker follows a MIDI ack that carries a 10 ms delay. Run 02's entry row was 11 ms
   *after* the marker, so the row it was compared against was a bar-band row from 1.5 s earlier. Fixed by using the
   pre-marker row only as an entry **anchor** (and only when it belongs to the overlay's own display state), never as a
   drift witness against a later entry row.
3. **The freeze bracket must end where the gesture starts.** The play/stop button is held ~3 s before the long press is
   detected and the window follows while it is held, so a bracket that ran to the long-press line measured the hold as
   freeze drift and then read the snap as a no-op (run 01, `unique_starts=[4546, 6144]`,
   `long_press_snap_did_not_move_window start=6144`). The bracket now ends at the button's note-on and the snap term
   compares against the **frozen** start. Both directions are host-pinned, including "the window never moved" → fails.

Replaying run 02's session log through the final verifier is green on the leg terms (`disp_window_row_count 880`,
`long_loop_disp_row_count 282`, `disp_window_transition_row_count 11`), i.e. the receipt data was already sufficient and
the remaining red was the bracket definition — recorded here so the fix is attributed to the verifier, not to new data.

## Verification

- `pio run -e teensy41-capture-serial` → SUCCESS, `variables 96,192 / ITCM 393,216 (code 392,216 + padding 1,000) /
  locals 34,880` (baseline-identical).
- `pio run -e teensy41-capture-serial-window-qualification` → SUCCESS, `code 392,376 + padding 840` (approved floor).
- `python3 scripts/check_marker_contract.py 'DIAG,disp_window,'` → `emitted=src/Utils/DebugSessionCapture.cpp:2200
  tier-a-prefix='DIAG,disp_' (10/31 bytes)`.
- `python3 -m unittest scripts.test_long_loop_display_serial_verify` → **27/27** (5 receipt tests + 2 bracket tests
  added to the 19 gesture/retry tests).
- `python3 -m unittest scripts.test_windowed_content_parity_verify` → **57/57**.
- `pio test -e native` → **3338 cases: 7 skipped, 3331 succeeded** (the 7 skips are the isolated-recovery/parked cases
  named in `tasks.md` 6.1, unchanged).
- `test/test_capture_ring` (Tier-A pin for the new row) → 38/38 in the focused run.

## Open, unchanged by this slice

- **3.7d** device read still owed (no run reached `win_blocked_by_request`).
- **4.5** stays open for 3.14c, displayed provenance (RWO S4e) and its device leg.
- 6.4's own checkoff is a separate judgement; this record supplies the previously missing leg half and nothing else.
