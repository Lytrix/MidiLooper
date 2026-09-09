# NOTE_EDIT control-plane record + display stall

**Status:** RC1 shipped — RC2 shipped — RC3 shipped — RC4 in tree — RC5 on device — RC6 implementing  
**Kind:** bugfix (six RCs, one commit each)  
**Evidence:** [`session_20260909_170818.log`](../../captures/session_20260909_170818.log) apply stall; [`session_20260909_183453.log`](../../captures/session_20260909_183453.log) RC2 device + deselect-commit stall; [`session_20260909_190408.log`](../../captures/session_20260909_190408.log) RC5 motors + RC6 mover drop  
**Does not reopen:** [`note_edit_overdub_select_pitch_hang_bugfix.md`](note_edit_overdub_select_pitch_hang_bugfix.md) overdub `value_only` (keep), compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04` recipes, hydrate, snapshot publication  
**Does not start:** a new apply scheduler, interval reservation, `RuntimeWorkBudget::admit`, a new control-plane noun, persist scheduler / `saveState()`, a second derived-representation owner, a deferred-commit FSM

## Debugging boundary

```text
DROID ch13 motor-ack notes 80–87 + ch14/15 fader voice
    → MidiHandler::isControlChannel / RECORD_EXCLUDE
    ← RC1

queuePendingPlayingEdit* while transport running
    → processPendingPlayingEditGeometry
    → ensureNoteEditDisplayProjectionCachesBuilt CaptureAwareGather [0, loopLength)
    → NoteGeometryResolver::resolve + finalReconstructAndSelect
    ← RC2 ([RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md))

NoteEditFaderMotorTiming::runMotorFaderBurst delay()
    while PLAYING (shouldRunBlockingMotorBurst(false) == true)
    ← RC3

macroCommitPendingEditsBeforeSelectNav
    → commitAllPendingNoteEditActions
    → commitEditAction logChangeLengthCommitTrace reconstructNotes × 4
      + mergeActiveCapturePasses(takeOnlyFlat) for take_only log only
    ← RC4

OVERDUBBING ValueOnly motor flush
    → pitchbend / CC only (no DROID notegate note 0)
    ← RC5

projectNoteEditDisplayNotes currentStateRowAllowedAsPaintExtra
    → capture-session mover omitted from snapshot committed base
    ← RC6
```

## Architecture checkpoint

| Question | RC1 | RC2 | RC3 | RC4 | RC5 | RC6 |
|---|---|---|---|---|---|---|
| Ownership change? | NO — extend `RECORD_EXCLUDE_*` used by `MidiHandler::isControlChannel` | NO — extend `ensureNoteEditDisplayProjectionCachesBuilt` + `NoteGeometryResolver::resolve` | NO — extend `NoteEditFaderMotorTiming::shouldRunBlockingMotorBurst` | NO — extend `commitEditAction` / `logChangeLengthCommitTrace` | NO — extend `sendFader1MotorTimedBurst` / `sendDependentFaderNotegates` | NO — extend `currentStateRowAllowedAsPaintExtra` |
| State transition change? | NO | NO | NO | NO — select-nav still commits before focus rebuild | NO | NO |
| Unbounded interval owner? | NO — filter before `Track::noteOn` | NO if the slice *removes* the full-loop gather/reconstruct on apply. STOP if it adds a content-unbounded owner on the MIDI interval | NO — removes `delay()` from the PLAYING motor path | NO — removes four `reconstructNotes` walks and a log-only `mergeActiveCapturePasses` from the USB MIDI callback | NO — notegate On/Off without `delay()` | NO — same projection walk, one extra predicate |

RC2 follows [RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md) (DEC-016): apply is a **Runtime Request** (derived notes × NOTE_EDIT interval). It must not rebuild the full derived representation. If RC2 needs a new apply owner, a deferred-apply FSM, or a new representation, stop and open a design session.

## Capture pin ([`170818`](../../captures/session_20260909_170818.log))

### Ghost notes (RC1)

DROID `midilooper_v1.ini` ch13 NOTE_EDIT motor acks: F1 80/81, F2 82/83, F3 84/85, F4 86/87.

| Fact | Proof |
|---|---|
| Hardware NoteOn ch13 pitches 83/84/86 (also 80–82, 85, 87) | `MI,H,144,13,…` |
| Those notes enter overdub storage | `COORD` `ch,13,note,84` / `83` / `86` / `85` / `87` |
| Channel 15 fader CCs also arrive | `MI,H,176,15` CC 2/3 |
| Record filter is only channel 16 | `MidiConfig::RECORD_EXCLUDE_MIN/MAX = 16` |
| Thru already treats 13–16 as DROID control plane | `THRU_EXCLUDE_MIN/MAX = 13/16` |
| After overdub stop, a 1-tick 84 sits on the roll | `DNTE,84,477,477,1` then `DNTE,84,333,333,1` |

`handleNoteOn` / `handleControlChange` / `handlePitchBend` skip record only when `isControlChannel()` is true. Channel 13–15 therefore write the capture during OVERDUBBING.

### Display stall — OVERDUBBING (RC2)

Transport-running faders queue (`queuePendingPlayingEditMove` / `queuePendingPlayingEditPitch`). Apply is `processPendingPlayingEditGeometry`. That apply is a consumer request, not a representation rebuild ([RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md)).

| Window | Queue → first apply | `GEOM_APPLY resolve` | Playing same kind |
|---|---|---|---|
| Move 1418.052–1419.507 | first queue tick 336, first dequeue **1.36 s** later at tick 96; dequeue age **269 ms** | **91,511 µs** then **93,149 µs** | **3.6–4.0 ms**, dequeue age 0–2 ms |
| Pitch 1424.232–1425.551 | coalesced CC 3 | **157–234 ms** (`234,368 µs` first) | — |

Overdub phase split on the 91 ms move: `setup` **29,977 µs**, `reconstruct` **30,629 µs**. Playing move: `setup` **125 µs**, `reconstruct` **1,508 µs**. `DNTE` after overdub move is 254–450 ms after the action; playing move `DNTE` is ~8 ms.

Playing in this file is the Snapshot / visual-cache committed-base path (no live Capture). Overdubbing takes `NoteEditCommittedBaseKind::CaptureAwareGather`: `ensureNoteEditDisplayProjectionCachesBuilt` calls `rebuildDisplayNotesInWindow(..., 0, loopLength)` whenever `captureActive()` and the committed-base keys miss. Live Capture bumps `captureDisplayRevision`, so every apply rebuilds a content-sized display list. `NoteGeometryResolver::resolve` then walks that list (`unionAnalyzeOverlapNoteIds` while capture is active). `finalReconstructAndSelect` is the `reconstruct` phase.

That collapses **representation** and **interval** on a timing-critical path. Architecture: a stale representation must not trigger a content-sized full-loop rebuild there; NOTE_EDIT analyze is a neighborhood around `selectedTick` / the mover span; PLAYING/OVERDUBBING display **defers** full representation rebuild ([Display.md](../Authority/Architecture/Display.md), [DerivedViews.md](../Authority/Architecture/DerivedViews.md)).

Ch13 acks in this file also grew the live capture (`snapshot_idle` `n` 1831 → 3571). RC1 removes that leak. Recapture after RC1 measures remaining gather cost from real Capture notes; it does not create the layer collapse. The gather is `[0, loopLength)` whenever capture is active.

### Display stall — PLAYING (RC3)

Playing geometry in this file is not the stall (resolve 3.6–4.0 ms, `DNTE` ~8 ms). After `select_motor_sync sequence=parallel_timed_burst` and `geometry_motor_sync sequence=timed_burst`, `midi_gap` is **265,402 µs** and **253,157 µs**. That matches `runMotorFaderBurst`: `delay(kInterPositionGapMs)` × 2 + `delay(kMotorNoteDurationMs)` ≈ 250 ms. `shouldRunBlockingMotorBurst` is false only while overdubbing.

## Sequencing

1. **RC1** — stop recording DROID control-plane voice. Commit. Recapture NOTE_EDIT move + pitch during OVERDUBBING and PLAYING.
2. **RC2** — after RC1 recapture, ship if CaptureAwareGather / resolver / reconstruct still rebuilds `[0, loopLength)` on apply (or resolve stays far above the playing ~4 ms move). One owner, one commit.
3. **RC3** — PLAYING motor `value_only`. Independent of RC2. May ship after RC1 even if RC2 is skipped.
4. **RC4** — after RC2 recapture, stop `commitEditAction` four-view reconstruct on select-nav leave. Do not fold into RC2 or RC3. Do not defer the commit.

Do not amend RC1 when HITL unmasks RC2. Do not amend RC2 when HITL unmasks RC4.

---

## RC1 — record-exclude the DROID control plane

**Invariant:** Channels `THRU_EXCLUDE_MIN`..`THRU_EXCLUDE_MAX` (13–16) are not appended to record/overdub capture. Instrument channels 1–12 still record.

**Owner:** `MidiConfig::RECORD_EXCLUDE_*` + `MidiHandler::isControlChannel`. Call sites already gate `Track::noteOn` / `noteOff` / `recordMidiEvents`.

**Change:** Set `RECORD_EXCLUDE_MIN = 13` and `RECORD_EXCLUDE_MAX = 16` so they match `THRU_EXCLUDE_*`. Update the header comment, `MidiHandler` “skip control channels 13–16” comment (make it true), and [`MIDI_CONFIG_GUIDE.md`](../Guides/MIDI_CONFIG_GUIDE.md) § Record exclusion. Do not add a new exclude alias.

**Native:** Host fixture that `isControlChannel` is true for 13–16 and false for 1–12. If no MidiHandler native harness exists, add a focused test next to existing MidiConfig / fader-feedback suites — do not invent a second filter helper.

**HITL:** Reuse `--preset note_edit_overdub_compose_last_bar_reopen` (`STIM-NE-OD-COMPOSE-04`). Do not edit `01`/`02`/`03`/`04`. Extend `verify_note_edit_overdub_compose`:

- no `COORD` `ch,13` notes 80–87 while OVERDUBBING
- no capture append of ch14 pitchbend / ch15 CC 2/3 as loop voice

Device recapture of the 170818 pattern must show ch13 `MI,H,144,13,8x` still arriving (DROID still acks) and **no** matching `COORD` / stored note.

**Shipped:** Manual overdub PASS (no ch13 notes recorded). HITL [`174457`](../../captures/session_20260909_174457.log) / [`174535`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_174535.json) `control_plane_not_captured` PASS (`overdub_coord_ch13_ack=0`, `stored_control_plane_sevt=0`). Compose shorten FAIL on that run is a 3-bar seed, not RC1. Native `test_note_edit_fader_feedback` + `pio test -e native` **1807 succeeded, 1 skipped**. RAM1 locals **7008**.

---

## RC2 — NOTE_EDIT apply requests notes; it does not rebuild the loop

Authority: [RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md) (DEC-016), [Display.md](../Authority/Architecture/Display.md), [DerivedViews.md](../Authority/Architecture/DerivedViews.md) § Runtime requests.

**Recapture after RC1** still required (ghost notes inflate Capture). If that log shows no `rebuildDisplayNotesInWindow(..., 0, loopLength)` on apply and overdub move `GEOM_APPLY resolve` stays in the 170818 PLAYING band (~4 ms), freeze this RC as **not needed**. The code path below is the collapse until proven otherwise.

**Invariant:** `processPendingPlayingEditGeometry` is a Runtime Request: current derived notes × the NOTE_EDIT interval (neighborhood around `selectedTick` / mover span). Changing that interval does not rebuild the full display or event representation. Live Capture and `NoteEditSession.store` stay transient overlays. Overlay remains neighborhood `NoteEditCurrentState` rows (mover + participants), not a full-loop store and not a current-state row per cache note (DEC-037).

**Owner:** `EditManager::ensureNoteEditDisplayProjectionCachesBuilt` (committed-base kind) and `NoteGeometryResolver::resolve`. `finalReconstructAndSelect` may only re-select from the already-built selectable list; it must not become a second full-loop reconstruct. Keep `queuePendingPlayingEdit*` / `processPendingPlayingEditGeometry`. Do not apply on the USB MIDI callback.

**Layer split (do not collapse):**

| Layer | This apply |
|---|---|
| Authoritative content | Published `LoopSnapshot` / passes + live Capture overlay. RC1 keeps control-plane voice out of Capture. |
| Derived representation | Existing NOTE_EDIT committed-base cache and overdub paint compose (snapshot / `visualCache.notes` + `capturePreview`). Rebuild only when the snapshot source revision or committed-base keys require it — not because the fader moved. |
| Interval | NOTE_EDIT analyze/select neighborhood around `selectedTick` / mover `[S, E)`. Not `[0, loopLength)`. |
| Runtime request | Resolver + paint read that representation at that interval. |

Today `selectNoteEditCommittedBaseKind` picks `CaptureAwareGather` whenever `captureActive()`, and that gather is `rebuildDisplayNotesInWindow(..., 0, loopLength)`. That is a content-sized representation rebuild on the apply path. PLAYING/OVERDUBBING display policy is to **defer** full rebuild and compose sealed committed source + capture preview.

**Allowed:** extend the existing committed-base / cache-reuse path so a capture-active apply reuses the current derived notes and requests the neighborhood. Bound or drop `unionAnalyzeOverlapNoteIds` over a full-loop `committedDisplayNotes` list on that path. Pin the hot phase from the RC1 recapture (`setup` vs `reconstruct`).

**Forbidden without a design session:** a new apply scheduler or persist-style slice FSM; applying every fader tick on the MIDI interval; a new derived-representation owner; rematerialize `EditSession.store` for paint (already forbidden in `ensureNoteEditDisplayProjectionCachesBuilt`); growing `committedDisplayNotes` walks without a cap.

**Native:** Fixture: capture-active NOTE_EDIT Move/Pitch does not call `rebuildDisplayNotesInWindow` for `[0, loopLength)` when the snapshot source revision is unchanged. Reuse existing `test_note_edit_*` / compose overlap fixtures; do not add a second resolver.

**HITL:** Same compose-reopen preset. Serial: `overdub_apply_reuses_committed_base` — in-edit overdub `GEOM_APPLY,resolve` ≤ 20,000 µs (170818 PLAYING band 3.6–4.0 ms; gather was 91–234 ms). `DNTE` follows the applied tick/pitch without a 1 s+ gap after the first queue of that gesture.

**Shipped:** Device [`183453`](../../captures/session_20260909_183453.log) `GEOM_APPLY,resolve` n=183 min **5899** µs p50 **12873** p90 **16295** max **21608** (170818 overdub gather was 91–234 ms). Capture-active apply no longer takes `CaptureAwareGather`. Compose-reopen HITL still does not land a real overdub resolve (3-bar seed / clock); do not keep retrying `04` for this RC.

---

## RC3 — PLAYING NOTE_EDIT motor send is ValueOnly

**Invariant:** While the selected track is PLAYING or OVERDUBBING, NOTE_EDIT motor updates do not run `runMotorFaderBurst` / `parallel_timed_burst` `delay()`. They still send DROID notegate (RC5). Timed burst may remain when transport is stopped.

**Owner:** `NoteEditFaderMotorTiming::shouldRunBlockingMotorBurst`. Today it is `return !overdubbing`. Extend so PLAYING also returns false. Keep the ValueOnly call sites in `FaderMotorSync` / `FaderOutbound` unchanged.

**Native:** Update `test_should_not_run_blocking_motor_burst_while_overdubbing` (or rename it) so PLAYING and OVERDUBBING are ValueOnly and stopped transport still allows burst.

**HITL:** Reuse a PLAYING NOTE_EDIT preset that already syncs motors (`note_edit_reselect_stacked` or compose after overdub stop). Require `#DBG select_motor_sync` / `geometry_motor_sync` `sequence=value_only` and no `midi_gap` ≥ 200,000 µs on the motor-sync sample. Do not create a new stimulus if an existing preset already logs those lines.

**Shipped:** Native `test_should_not_run_blocking_motor_burst_while_playing_or_overdubbing`. RAM1 locals **7008**. Device [`232026`](../../captures/session_20260909_232026.log) PLAYING `select_motor_sync` / `geometry_motor_sync` `sequence=value_only` (judged 3, timed_burst 0, no motor-sync `midi_gap` ≥ 200000). Reselect stacked nav still failed empty-step on that seed — not this RC.

---

## RC4 — select-nav commit does not reconstruct the loop to print traces

**Invariant:** `commitEditAction` rematerializes the session store from committed passes. It does not call `NoteUtils::reconstructNotes` on four flats, and it does not call `Loop::mergeActiveCapturePasses` solely to feed `logChangeLengthCommitTrace("take_only")`. Select-nav still runs `macroCommitPendingEditsBeforeSelectNav` → `commitAllPendingNoteEditActions` before focus rebuild.

**Owner:** `EditManager::commitEditAction` and `logChangeLengthCommitTrace`. Do not add a deferred-commit owner. Do not skip `copyNoteEditCommitBase` + `loadFromEvents` rematerialize. Do not move `rebuildVisualCacheFromPasses` in this RC.

**Capture pin ([`183453`](../../captures/session_20260909_183453.log)):** First F1 leave after a pending edit (note or empty step) runs the same macro commit. Empty-step with nothing pending is cheap (`2255.244` no `commitEditAction` lines). Three commits in this file:

| Host time | Trigger | `commitEditAction` traces | USB callback |
|---|---|---|---|
| 2253.650 | note 34→33 | replay_flat 2253.733 … loop_materialized 2253.883 (each `reconstructNotes` on 145–150 events) | `hostdrain` **605232** µs |
| 2279.376 | note 13→14 | 2279.497–2279.725; selection 13→14 at 2279.903 | `hostdrain` **916528** µs |
| 2295.797 | note 16→15 | 2295.956–2296.254; selection 16→15 at 2296.492 | later `hostdrain` **899170** µs |

`logChangeLengthCommitTrace` always calls `reconstructNotes(flat, loopLength, false)` for `replay_flat`, `take_only`, `session_store`, and `loop_materialized`. `takeOnlyFlat` is written only by `mergeActiveCapturePasses` and read only by that log.

**Allowed:** gate the four-view reconstruct (and the take_only merge) behind a `constexpr bool` that defaults false. Keep persist-identity / incoming-row / `commitEditAction trace` lines.

**Forbidden without a design session:** deferring macro commit off select-nav; a commit slice FSM; skipping session-store rematerialize; changing `rebuildVisualCacheFromPasses` on this path.

**Native:** Existing `test_edit_apply` commit fixtures. No second commit owner.

**HITL:** Reuse compose verify. Serial: `commit_skips_trace_reconstruct` — no `commitEditAction replay_flat` / `take_only` / `session_store` / `loop_materialized` lines. Device recapture of the 183453 leave-after-edit pattern: those four lines absent; `hostdrain` after that leave is not the 600–900 ms reconstruct batch.

---

## RC5 — overdub motor flush still sends DROID notegate

**Invariant:** While OVERDUBBING, NOTE_EDIT motor flush (`sendFader1MotorTimedBurst`, `syncMotorsFromSelectTarget`, outbound `SendCoarse`) sends position then notegate (note 0 / 1 / 2) with no `delay()`. `publishDependentFaderLatch` stays ValueOnly (no notegate on every geometry tick).

**Owner:** `sendSelectFaderNotegate` + `sendDependentFaderNotegates`. Do not restore `runMotorFaderBurst` on the overdub path.

**Capture pin ([`184740`](../../captures/session_20260909_184740.log)):** DROID moves on notegate ([`DROID_MOTORFADER_PITCHBEND.md`](../Guides/DROID_MOTORFADER_PITCHBEND.md)). PLAYING `geometry_motor_sync sequence=timed_burst` at 288.257 sends `MO,224,16` × 3 then `MO,144,16,0,127`. OVERDUBBING `sequence=value_only` at 233.493 sends `MO,224,16` only. Select flush at 212.478 sends ch14 PB + ch15 CC 2/3 and no notegate.

**Native:** Existing fader-feedback notegate note numbers. No second motor owner.

**HITL:** Compose verify `overdub_motor_sends_notegate` — each in-edit overdub `select_motor_sync sent=1` / `geometry_motor_sync sent=1` is followed by `MO,144` notegate on ch16 note 0, ch14 note 0, or ch15 note 1/2.

---

## RC6 — capture-session mover stays in paint after overlap Hide + Pitch

**Invariant:** `projectNoteEditDisplayNotes` always paints `focus.movingNoteId` from current-state / `focus.last` when that row is visible. Snapshot / visual-cache committed base may omit a capture-session mover. Occupant `HideNote` still suppresses the overlapped note.

**Owner:** `currentStateRowAllowedAsPaintExtra` in `projectNoteEditDisplayNotes`. Do not rematerialize the session store. Do not restore `CaptureAwareGather` `[0, loopLength)`.

**Capture pin ([`190408`](../../captures/session_20260909_190408.log)):** Faders update (RC5). Fine of focus `noteId=94` at 110.585–112.273 keeps `DNTE` on 94 (`2244`…`2286`, length 472) while `ShortenNote` then `HideNote` of 85. Pitch of 94 `2481–2953` at 121.284: `HideNote` 60 on pitch 73 + `ChangePitch` 94→73. **No `DNTE` for the whole pitch gesture** (next `DNTE` is 70@2208 at 123.023). Loop length in `DISP` is **3840**; 2481 is past the original 3-bar (2304) snapshot window. Native fixture: mover absent from committed base, occupant Hidden, ChangePitch 94→73 — projection dropped the mover until the extra-reject allowed `movingNoteId`.

**Allowed:** treat `focus.movingNoteId` as a paint extra even when the snapshot span does not match.

**Forbidden without a design session:** a second paint owner; rematerialize `EditSession.store` for paint; full-loop gather on apply.

**Native:** `test_project_pitch_overlap_keeps_mover_and_highlight_190408` + `test_project_pitch_capture_only_mover_not_in_snapshot_190408`.

**HITL:** Compose verify `pitch_keeps_mover_dnte` — each `EditSessionAction` type=5 is followed by `DNTE` with that pitch and start. Vacuous pass when the run has no type=5. Device recapture of the 190408 pitch-into-overlap pattern: `DNTE` for the mover pitch/start after the first type=5.

---

## HITL / docs closeout

| Gate | When |
|---|---|
| `pio run -e teensy41-capture-serial` RAM1 locals ≥ 4000 | each firmware RC |
| `pio test -e native` | each firmware RC |
| Compose-reopen HITL | RC1 (and RC2 if implemented) — upload+run without asking |
| PLAYING motor HITL | RC3 |
| Compose verify `commit_skips_trace_reconstruct` + device recapture | RC4 |
| Compose verify `overdub_motor_sends_notegate` + device recapture | RC5 |
| Compose verify `pitch_keeps_mover_dnte` + device recapture of 190408 pitch | RC6 |
| [`MIDI_CONFIG_GUIDE.md`](../Guides/MIDI_CONFIG_GUIDE.md) + `MidiConfig.h` comments | RC1 |
| [`HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md) verifier bullets | when a verify check is added |
| [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md) | each shipped RC |

## Out of scope

- Changing DROID ack note numbers or removing ch13 acks on the module
- Editing `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`
- Snapshot idle serial volume (separate from apply/motor)
- Persist scheduler, `requestDeferredSaveState` / `saveState()`, seal/queue
- All Notes Off / LED channel 15 policy beyond not recording it
