# Phase 4 — length gates removed (DEC-242): native and device result (2026-11-01)

## Change

- DEC-200's keystone (`requestCommittedPlaybackWindowForSlot`) and its producer twin (`win_window_ineligible`) no longer
  refuse by length.
- `playbackWindowInterval` is one rule: two bars at the playhead, clamped by the loop.
- **Report-only addition.** `TrackManager::reportPlaybackRouteRejects` re-emits a slot's row when the producer's exit
  changes while the slot is still on the merged fallback (`chg=0`). Without it, an idle frame hid its producer exit.

## Native and builds

- **Re-derived fixtures:** 7, listed in DEC-242. None weakened.
- **Phase-3 baseline:** defect A is inverted (every length is acquired on the production lane).
- **Full native:** 3,837 / 8 skipped / 3,829 for the gate change. The run including the producer-exit row is recorded
  in `CURRENT_WORK.md`.
- **Builds:** all three envs. RAM1 96,192; default and no-grain 391,944 / 1,272; profile 392,344 / 872. All unchanged.

## Device

Two runs, both with boot proven (`boot_reset,crash=0`, `BOOT,load,ok`) and a valid chain (12 overdubs, 0
`order_deny`).

| Image | Capture | Merged share | Interrupt misses | Internal `midi_gap` median / max |
|---|---|---|---|---|
| `3e4161e9…` (gates removed) | `session_20261101_122656.log` | 85.6 % | 417 | 12.2 / 14.9 ms |
| `093e69e4…` (+ producer-exit row) | `session_20261101_123322.log` | 85.6 % | 433 | — |
| phase-2 reference `1799e442…` | `session_20261101_114703.log` | 66.9 % | 2 | 6.2 / 8.8 ms |

### 1. Four restored tracks that were silent now play (behavior change, needs the owner's ear)

NoteOns per MIDI channel, same card, same rig:

| Run | ch1 | ch2 | ch3 | ch4 | ch5 | ch6 | ch8 | ch16 |
|---|---|---|---|---|---|---|---|---|
| phase 2, before DEC-242 | 232 | 1,046 | **0** | **0** | **0** | **0** | 587 | 17 |
| after DEC-242 | 210 | 950 | 283 | 758 | 967 | 393 | 531 | 23 |

- Tracks 3–6 (t2–t5) carry restored, enabled slot-0 content (`Boot transport … enabledMask=0x1`). Before DEC-242 they
  produced no MIDI at all and never reached route selection.
- They play after DEC-242, served by the merged fallback.
- **This explains the merged share and the miss count:** about twice the slot-ticks reach route selection
  (170,050 against 80,448), and those slots' merged fallbacks are what the ISR owes.
- **Not yet established:**
  - which DEC-242 term made them audible: the per-turn production demand now reaches `requestCommittedPlaybackWindowForSlot`
    for short loops, which retains the source (DEC-194);
  - whether the earlier silence was a defect. It is consistent with an enabled restored slot that should sound, but the
    owner has to confirm it.

### 2. Every restored slot is refused by the producer while playing: `win_not_servable`

The reason timeline (`parse_tick_routes.py`, `chg=0` rows) names the producer exit for t0–t5 (1, 8, 4, 31, 2 and
4 bars): `win_track_not_playing` while stopped, and **`win_not_servable` from every transport start onward**, for the
whole run.

`win_not_servable` is `!set || set->lifecycle.current >= set->views.size() || !playbackWindowSourceIsCurrentForSlot(...)`:
- **No view set.** Boot prewarm is `prewarmPlaybackForSlot(s, false)`, which reserves no views (`capture_prewarm_split
  views=0` for t0–t6).
- **No current source.** The producer asks `playbackWindowSourceIsCurrentForSlot` **before** the request. The request
  is the only place a rootless slot's source is retained (DEC-194), so a pass-only restore can never pass this check.

This is the phase-5 invariant on device. A PLAYING route depends on an admission-time preparation (view set, retained
source) that does not recur while playing. The 31-bar t3 shows the gate removal was necessary but not sufficient: it
was never length-gated and was refused just the same.

### 3. The long loop: unchanged

t7 s3 (20 bars) still ends each transport restart as `frame_blocked(clock_epoch_changed)` → `win_frame_invalidated`
(defect C, phase 5). In between, its frame is `frame_idle` / `win_busy` during overdubs.

## Consequence for the plan

- Phase 4's own gain on routes is **zero until** restored slots are servable while playing. That work belongs to
  phase 5 (acquire the view set and retain the source off-clock while playing, bounded; precedent:
  `preparePlaybackLaunchForSlot` → `prewarmRollingViews`).
- Defect B (successor demand) needs an owner decision (DEC-242 "Open").

## Task 5.0 RED (native) and the acquisition cost (device), recorded 2026-11-01

- **Native.** `test_restored_slot_servability_while_playing_baseline` (`CapabilityFixture::Entry`):
  - a **rootless** slot whose view set is prewarmed is acquired at 5,944 ticks (8 and 20 bars), so DEC-194's source
    retention is enough;
  - the same slot after **`prewarmBootPlaybackRuntimeSlots`** (no view set) is **never** acquired: `frame_idle`, with the
    producer at `win_not_servable`. This is exactly the device shape.

  The missing term is the view set, not the source.
- **Device per-unit cost** (`view_prewarm_step`, `session_20261101_123322.log`, t7 s3's off-edge prewarm):

  | Units (progress) | Content | Each |
  |---|---|---|
  | 1–10 | capture stagings | 9.0–18.5 ms |
  | 11–13 | the 3 rolling views the lane needs | 12.0–12.4 ms |
  | 14–17 | bridge bases and bridges (full-take admissible) | 1.0–15 ms per sub-step |
  | 18–27 | canonical output, companions | 0–17.6 ms |

  `viewSet->prewarmed`, which the lane reads, is set only at unit 17: about 190 ms per slot. Each unit is a set of
  external-memory allocations of `kEventsPerView = 2048` events.
- **Consequence.** No unit the lane needs fits the main loop's real-time budget during playback (DEC-212: 8 ms child
  budget; the external-clock tick runs on the main loop). That is why `stepViewPrewarmForIdle` is gated
  `!timingCritical`. Running it while playing is not an admissible repair as it stands.
