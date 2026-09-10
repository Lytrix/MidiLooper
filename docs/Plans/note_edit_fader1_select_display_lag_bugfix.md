# NOTE_EDIT F1 select display lag

**Status:** FROZEN — RC1 shipped (device PASS)  
**Kind:** bugfix  
**Evidence (fail):** [`session_20260909_235958.log`](../../captures/session_20260909_235958.log) vs [`session_20260909_234815.log`](../../captures/session_20260909_234815.log)  
**Evidence (pass):** [`session_20260910_095539.log`](../../captures/session_20260910_095539.log) (77 `apply=1` / 87 `DNTE`); [`session_20260910_100844.log`](../../captures/session_20260910_100844.log) (150 / 149)  
**Does not reopen:** control-plane RC3 PLAYING `value_only` (keep — `170818` `midi_gap` 265402 / 253157 after `timed_burst`), RC7 notegate hold, RC8 linear outbound, compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`  
**Does not start:** restoring `runMotorFaderBurst` `delay()` while PLAYING, a new apply scheduler, `RuntimeWorkBudget::admit`

## Debugging boundary

```text
shouldRunBlockingMotorBurst / timed_burst delay()
    ← control-plane RC3 — not this

ValueOnly notegate hold / F1 linear outbound
    ← control-plane RC7 / RC8 — not this

handleSelectFaderInput
    → resolveFader1SelectTarget
    → selectNavSlotIndexForPitchbend → resolveFader1SelectTarget again
    → applyNoteSelectFromFader1Pitchbend → buildSelectNavigationSlots again
    → applySelectNav / sidebar SC_DNTE / DFRAME
    ← RC1
```

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `handleSelectFaderInput` / `resolveFader1SelectTarget` / `applyNoteSelectFromFader1Pitchbend` |
| State transition change? | NO — select still applies on F1 pitchbend |
| Unbounded interval owner? | NO — remove duplicate slot rebuilds; do not add a content-unbounded walk |

## Capture pin

| Fact | Proof |
|---|---|
| RC7 F1 sweep paints the selected note | [`234815`](../../captures/session_20260909_234815.log) 68 `select_apply apply=1`, 79 `DNTE`, median apply spacing 40 ms |
| After RC8 flash, F1 sweep applies without highlight | [`235958`](../../captures/session_20260909_235958.log) 48 `apply=1` (ticks 856→0), **3** `DNTE` (760, 328, 14) |
| Display frame skips the same sweep | `DFRAME` 171.629 then 177.720 |
| Motor path is still ValueOnly | `geometry_motor_sync` / `select_motor_sync sequence=value_only` |
| Large `midi_gap` in `235958` is not timed_burst | `midi_gap,452226` after `playback_build`; `midi_gap,276991` after playback `MO` |
| After one-resolve flash, highlight follows | [`095539`](../../captures/session_20260910_095539.log) 77 / 87; [`100844`](../../captures/session_20260910_100844.log) 150 / 149 |

Live `DNTE` during NOTE_EDIT is `SidebarAndInfo` `SC_DNTE` on sidebar draw. If `DFRAME` does not run, highlight `DNTE` does not run.

## Invariant

Each inbound F1 pitchbend resolves the select target once. `applyNoteSelectFromFader1Pitchbend` uses that target. A PLAYING F1 sweep must keep emitting `DNTE` for `apply=1` note changes (234815 band), not 3 highlights for 48 applies.

## Shipped (RC1)

`handleSelectFaderInput` reuses `navigationTarget`. `applyNoteSelectFromFader1Pitchbend(Track&, const Fader1SelectTarget&)` does not rebuild navigation slots. Post-commit `buildSelectNavigationSlots` stays recovery when the `NoteId` is missing after commit. Do not restore PLAYING timed burst.

Native: `pio test -e native` 1819 succeeded, 1 skipped. Host: `test_f1_highlight_starved_fails_when_applies_outpace_dnte`. HITL verifier: `select_apply_highlight_follows` on `note_edit_reselect_stacked`.

## Misattribution guard

Do not reopen RC3 `timed_burst` to chase F1 display lag. Do not fold a later `rebuildNoteEditFocusForDisplayNote` / `SelectionChanged` cost into this RC — that would be RC2.
