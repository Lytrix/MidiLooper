# NOTE_EDIT control-plane record + display stall

**Status:** RC1 shipped — RC2 implementing — RC3 not started  
**Kind:** bugfix (three RCs, one commit each)  
**Evidence:** [`session_20260909_170818.log`](../../captures/session_20260909_170818.log)  
**Does not reopen:** [`note_edit_overdub_select_pitch_hang_bugfix.md`](note_edit_overdub_select_pitch_hang_bugfix.md) overdub `value_only` (keep), compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04` recipes, hydrate, snapshot publication  
**Does not start:** a new apply scheduler, interval reservation, `RuntimeWorkBudget::admit`, a new control-plane noun, persist scheduler / `saveState()`, a second derived-representation owner

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
```

## Architecture checkpoint

| Question | RC1 | RC2 | RC3 |
|---|---|---|---|
| Ownership change? | NO — extend `RECORD_EXCLUDE_*` used by `MidiHandler::isControlChannel` | NO — extend `ensureNoteEditDisplayProjectionCachesBuilt` + `NoteGeometryResolver::resolve` | NO — extend `NoteEditFaderMotorTiming::shouldRunBlockingMotorBurst` |
| State transition change? | NO | NO | NO |
| Unbounded interval owner? | NO — filter before `Track::noteOn` | NO if the slice *removes* the full-loop gather/reconstruct on apply. STOP if it adds a content-unbounded owner on the MIDI interval | NO — removes `delay()` from the PLAYING motor path |

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

Do not amend RC1 when HITL unmasks RC2.

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

**HITL:** Same compose-reopen preset. Serial: no full-loop gather on in-edit overdub Move/Pitch while the published root is unchanged; `GEOM_APPLY,resolve` in the playing-class band from the RC1 recapture; `DNTE` follows the applied tick/pitch without a 1 s+ gap after the first queue of that gesture.

---

## RC3 — PLAYING NOTE_EDIT motor send is ValueOnly

**Invariant:** While the selected track is PLAYING or OVERDUBBING, NOTE_EDIT motor updates use the existing ValueOnly send. `runMotorFaderBurst` / `parallel_timed_burst` `delay()` does not run. Timed burst may remain when transport is stopped.

**Owner:** `NoteEditFaderMotorTiming::shouldRunBlockingMotorBurst`. Today it is `return !overdubbing`. Extend so PLAYING also returns false. Keep the ValueOnly call sites in `FaderMotorSync` / `FaderOutbound` unchanged.

**Native:** Update `test_should_not_run_blocking_motor_burst_while_overdubbing` (or rename it) so PLAYING and OVERDUBBING are ValueOnly and stopped transport still allows burst.

**HITL:** Reuse a PLAYING NOTE_EDIT preset that already syncs motors (`note_edit_reselect_stacked` or compose after overdub stop). Require `#DBG select_motor_sync` / `geometry_motor_sync` `sequence=value_only` and no `midi_gap` ≥ 200,000 µs on the motor-sync sample. Do not create a new stimulus if an existing preset already logs those lines.

---

## HITL / docs closeout

| Gate | When |
|---|---|
| `pio run -e teensy41-capture-serial` RAM1 locals ≥ 4000 | each firmware RC |
| `pio test -e native` | each firmware RC |
| Compose-reopen HITL | RC1 (and RC2 if implemented) — upload+run without asking |
| PLAYING motor HITL | RC3 |
| [`MIDI_CONFIG_GUIDE.md`](../Guides/MIDI_CONFIG_GUIDE.md) + `MidiConfig.h` comments | RC1 |
| [`HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md) verifier bullets | when a verify check is added |
| [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md) | each shipped RC |

## Out of scope

- Changing DROID ack note numbers or removing ch13 acks on the module
- Editing `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`
- Snapshot idle serial volume (separate from apply/motor)
- Persist scheduler, `requestDeferredSaveState` / `saveState()`, seal/queue
- All Notes Off / LED channel 15 policy beyond not recording it
