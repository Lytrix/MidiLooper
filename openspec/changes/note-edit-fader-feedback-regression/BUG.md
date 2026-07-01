# BUG — NOTE_EDIT fader feedback regression (select + NOTELEN length)

**Change:** `note-edit-fader-feedback-regression`  
**Status:** Phase 3 coordinator **shipped** (2026-06-30); HITL timing verifier **PASS** (`session_20260630_191718`); **RC11** loop-relative coordinate bug **open** — Phase 8 next.  
**Introduced by:** commit `d49e4c8` — *"Improve NOTE edit display alignment and deferred session fader sync"* (2026-06-29); commit message notes: *"Known issue: fader 2 coarse updates still do not run reliably on every session entry."*

**Related:** Archived `edit-record-display-length-mode` (D3 length-mode lifecycle — shipped 2026-06-24); `d576f85` NOTELEN length mapping (linear coarse, anchor fine).

---

## Reported symptoms (user)

1. **NOTE_EDIT entry:** Fader1 input is correctly blocked during feedback ignore, but **fader1 motor does not update** to the select bracket after the ignore window.
2. **Note select (fader1):** **Fader3** updates to match the note; **fader2 does not** (motor stays at prior position).
3. **Position edit:** Moving fader2 **does** move the note (inbound path works).
4. **NOTELEN toggle → length edit:** Moving fader2 only allows a **tiny** length change; behavior suggests a **very high tick anchor** — full loop-length coarse range is not available.

Length entry path confirmed: **NOTELEN / DROID button** (`toggleLengthEditingMode`), not encoder cycle.

---

## Expected behavior

| Phase | Fader1 (select) | Fader2 (coarse) | Fader3 (fine) |
|-------|-----------------|-----------------|---------------|
| NOTE_EDIT entry (note selected) | Motor moves to bracket slot within ≤2 s after ignore | Motor moves to **note start** (position mode) | Motor moves to start fine offset |
| Fader1 note select | Motor updates to new slot | Motor updates **with** fader3 to new note start | Motor updates **with** fader2 |
| NOTELEN enable | Unchanged (select) | Motor moves to **note end** (`endTick % loopLength`) | Motor moves to end fine anchor |
| NOTELEN + fader2 move | Unchanged | Full loop-length coarse range changes note end | Fine ±1/16th from anchor |

Inbound fader2 in position mode SHALL move note start after grace period. Inbound fader2 in length mode SHALL map pitchbend across `0..loopLength-1`.

---

## Actual behavior (observed)

| Symptom | Evidence / hypothesis |
|---------|----------------------|
| Fader1 motor stale after entry | Deferred `processSessionFaderSync` step 1 at t+1600 ms may race with `performSelectnoteFaderUpdate`; `selectFaderFeedbackIgnoreUntilMs_` extended multiple times |
| Fader3 updates, fader2 does not on select | Session sync step 3 sends fader3/4 without repeating coarse; `performSelectnoteFaderUpdate` may skip fader2/3 when `inPitchbendIgnorePeriod` |
| Fader2 inbound OK | `handleCoarseFaderInput` accepts input when `startEditingEnabled` — outbound-only failure |
| NOTELEN tiny range | Stale fader2 motor (loop-edit position near loop end) + `applyLengthEndTargetRules` clamp against real note geometry |

---

## Root causes (code-backed)

### RC1 — Deferred session fader sync blocks alternate feedback

`sendNoteEditSessionFaderFeedback` → `deferSelectFaderSyncToBracket` schedules steps at **1600 ms** intervals (fader1 → fader2 coarse → fader3/4). While `sessionFaderSyncStep_ != 0`:

- `handleFaderInput` blocks **all** faders (~4.8 s total chain).
- `enableStartEditing` does not run — 750 ms grace fallback for fader2–4 never fires.

### RC2 — Competing schedulers; fader2/fader3 split

On session open, both run:

1. `deferSelectFaderSync` (3-step chain)
2. `syncNoteEditSessionStateToUi` → `sendSelectnoteFaderUpdate` (1600 ms delayed)

Step 3 can send fader3 without step 2 coarse completing — explains fader3-only update.

### RC3 — NOTELEN with stale coarse motor

`toggleLengthEditingMode` sends coarse feedback, but if motor never received start-position sync (RC2), first length move maps from wrong physical pitchbend.

### RC4 — Coarse `lastSentTime` inconsistency

`sendCoarseFaderPosition` sets `lastSentPitchbend` but not always `lastSentTime` unless routed through `sendFaderUpdate`.

### RC5 — Preempt-on-request during fader1 select movement (Phase 2)

**Status:** **Resolved (Phase 3)** — non-preemptive coalesce + 400 ms user-classified quiet gate.

Every `requestFaderOutbound()` call unconditionally invokes `cancelActiveFaderOutbound()` before restarting (`NoteEditManager.cpp`). On fader1 note select, `handleSelectFaderInput` → `sendNoteSelectFaderFeedback` → `requestFaderOutbound(NoteSelect, skipFader1=true)` fires **immediately** on each selection change. While the user is still moving fader1 across notes, each new select cancels an in-flight ch15 pipeline (coarse → trigger → fine → trigger → note-value → trigger).

**Symptoms:** Fader2 updates once then stops; F3/F4 may starve or update only after long delays; direct coupling (F2→F1 via `Fader1Only`) works because it does not restart the ch15 pipeline.

**Entry points that restart ch15 during fader1 movement:**

| Source | Call |
|--------|------|
| `handleSelectFaderInput` | `sendNoteSelectFaderFeedback` |
| `EditManager::selectNoteAtBracket` / GPIO / bar-step | `sendNoteSelectFaderFeedback` |
| `EditManager::applySelectNav` | `requestFaderOutbound(NoteSelect)` when `requestFaderSync` |

**Phase 2 fix:** Deferred dependent refresh after fader1 quiet (≥1000 ms); non-preemptive execution — coalesce pending refresh instead of cancel mid-pipeline.

### RC6 — Fader1Only clears pending dependent refresh (Phase 2 follow-up)

**Status:** **Resolved (Phase 3)** — inline `sendFader1BracketFeedback` in `scheduleOtherFaderUpdates`; no outbound state machine restart on geometry F1 feedback.

`scheduleOtherFaderUpdates(FADER_COARSE/FINE/NOTE_VALUE)` called `requestFaderOutbound(Fader1Only)`, which preempts and sets `pendingRefresh_.pending = false`. Any fader2–4 edit after fader1 note select **cancelled** the scheduled F2–F4 refresh. Capture `session_20260630_103043.log`: after successful `BEGIN_REFRESH` at 47.701 s, fader2 inbound at 54.090 s produced repeated `DONE` (Fader1Only) with no further `WAIT_STABLE` until 57.998 s.

### RC7 — Navigation deadband suppresses trailing select (Phase 3)

**Status:** Fixed in Phase 3 — `SELECT_MOVEMENT_THRESHOLD` was used to suppress `applyNoteSelectFromFader1Pitchbend` on micro-moves after fast sweeps. Trailing pitch updates `lastUserSelectFaderValue` but selection and F2 feedback lag — user report: fader1 at min, fader2 still on prior note.

**Phase 3 fix:** Slot-index change triggers immediate selection apply; pitch deadband removed from navigation path (D12).

### RC8 — Settle timer arms only on slot-change or significant motion (Phase 3)

**Status:** Fixed in Phase 3 — same-slot trailing creep never re-armed settle; prior quiet window could fire mid-gesture.

**Phase 3 fix:** User-classified quiet gate (400 ms) replaces millis settle arm logic (D13).

### RC9 — Selection lags fader1 pitch (Phase 3)

**Status:** Fixed in Phase 3 — F2 outbound reflects `selectedNoteIdx` while fader1 pitch already maps to a different slot.

**Phase 3 fix:** Live slot-index apply + quiet-time final re-apply before dependent refresh.

### RC10 — Overlapping millis policies (Phase 3)

**Status:** Fixed in Phase 3 — `SELECT_STABILITY_TIME`, settle reschedule, `FEEDBACK_IGNORE_PERIOD`, `NOTE_SELECTION_GRACE_PERIOD`, and `SELECTNOTE_UPDATE_DELAY` lockout interacted around slow fader1 moves (multi-minute effective delay when motor echo reset settle).

**Phase 3 fix:** Single outbound state machine; not a MIDI TX queue issue (`MidiHandler` sends synchronously).

### RC11 — F2/F3 outbound uses storage tick, not loop-relative tick (Phase 8)

**Status:** Open — coordinate bug separate from RC5–RC10 timing fixes.

**Symptom:** F2 motor ~50% offset from expected note start when `loopStartTick ≠ 0`; updates responsively but wrong coordinate. Same class on F3 position-mode fine offset (Phase 10).

**Cause:** `sendCoarseFaderPosition` and `sendFineFaderPosition` (position mode) use storage tick (`liveNote.startTick % loopLength`). F1 select navigation and F2 **inbound** use `SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength)`.

**Capture proof** (`session_20260630_191718.log`, `loop_start=424`, `loop_len=768`):

| Storage tick | Rel tick | Sent F2 | Expected (rel) |
|-------------|----------|---------|----------------|
| 0 | 344 | 0.0% | ~47.8% |
| 448 | 24 | 62.2% | ~3.3% |
| 509 | 85 | 70.7% | ~11.8% |

After flash with `2ecf25d`, every `#DBG outbound_ctx` row shows `pb != expected_pb_rel` when `anchor_tick != rel_tick`.

**Phase 8 fix:** Use `noteRelativeTick` before `loopTickToCoarsePitchbend` / fine offset math in outbound send helpers.

**Architecture checkpoint:**

| Question | Answer |
|----------|--------|
| Ownership change? | **No** — tick input fix only in send helpers. |
| State transition change? | **No** — no new session type or mode. |

### RC12 — Pace-skip overreach in `droidMotorOutboundPriority_` (2026-06-30)

**Status:** Fixed in §7.6.1 (unified handoff).

**Symptom:** After `3adb27b` ch15 motor bypass, F2 coarse motor latch regressed (~80% user-visible on `session_20260630_222821`).

**Cause:** `paceDroidUsbHostBeforeSend()` returned early when `droidMotorOutboundPriority_` was active, skipping inter-packet gap pacing for motor fader USB host sends — not only LED drain.

**Fix:** Remove pace-skip early return; keep LED queue bypass (`queueAsLed = isLedChannel && !droidMotorOutboundPriority_`) and `processDroidUsbHostOutbound` early return when priority active.

### RC13 — Duplicate `QUIET_REFRESH` and trigger-only pipeline steps (2026-06-30)

**Status:** Fixed in §7.6.2–7.6.4.

**Symptom:** `session_20260630_222821`: 13/13 quiet bursts repeated same `pb` + tick; `SEND_F2` logged on empty slot with trigger-only burst; double F2 motor triggers (send helper + pipeline).

**Fix:** `processFaderSelectQuiet` schedules outbound only when selection/slot changed; send helpers return `bool`; pipeline skips trigger steps on no-op; motor triggers owned by pipeline `TriggerCoarse` / `TriggerFine` / `TriggerNoteValue` only.

**Architecture checkpoint:**

| Question | Answer |
|----------|--------|
| Ownership change? | **No** — transport pacing + pipeline gating only. |
| State transition change? | **No** — no new session type or mode. |

### RC14 — Fader motor skip perceptual rate (2026-07-01)

**Status:** Investigation complete (Phase 0–1); Phase 2 hybrid gate **deferred** pending DROID ch13 ack spike.

**Symptom:** Slow F1 glide across adjacent notes — user reports ~60% of notes not updating fader motors; fast sweeps track better.

**Dual root cause:**

1. **Firmware gate (note-only motor sync):** `handleSelectFaderInput` calls `syncMotorsFromSelectTarget` only when `noteIdx` changes. Same-note F1 crawl logs `unchanged_note` (~95% suppression). Empty steps log `empty_step_ignored` — no position sync.
2. **DROID motor path (MO ≠ MI):** `sent=1` and `MO,224,14` prove Teensy **commanded** an update; they do **not** prove the physical fader moved or reported position back.

**MO vs MI matrix (correlator on existing captures):**

| Session | MO ch14 | MI ch14 | MO→MI pairs | miss rate | note_changed | visible F2 (MI) |
|---------|---------|---------|-------------|-----------|--------------|-----------------|
| `session_20260701_164040` | 8 | 0 | 0/8 | **1.00** | 8 | 0/8 (0%) |
| `session_20260701_163558` | 95 | 19 | 19/95 | **0.80** | 94 | 34/94 (36%) |

**164040 detail:** 153 F1 select events, 8 `note_changed` apply, 145 `unchanged_note`, 8 `sent=1` with distinct F2 pitchbend MO — **zero** `MI,H,224,14` echoes. F4 MO sent 8 times but only 4 distinct CC values (revisit same pitch → no visible movement).

**163558 detail:** 94 `note_changed`, 952 `unchanged_note` (95% suppressed). DNTE–motor false positives fixed (±50 ms window): `dnte_motor_misses` 28 → 1 with updated analyzer.

**Phase 0–1 shipped:**

- `scripts/hitl/verify/fader_motor_echo_correlation.py` — MO→MI pairing + ch13 ack support
- DROID `midilooper_v1.ini` — ch13 motor-ack circuits (notes 80–87: clear/set_changed per fader)
- `#DBG select_motor_sync` extended with `f2_pb`, `f4_cc`, `prior_f2_pb`, `prior_f4_cc`, `motor_value_changed`
- Analyzer integration in `scripts/analyze_fader2_select_feedback.py` (`--mi-strict` flag)

**Next gate:** Reload DROID patch in Forge; re-capture 164040 scenario; correlator must show ch13 clear acks matching every MO notegate before Phase 2 hybrid motor gate.

**Analyze:**

```bash
PYTHONPATH=scripts python3 scripts/analyze_fader2_select_feedback.py captures/session_20260701_164040.log
PYTHONPATH=scripts python3 scripts/analyze_fader2_select_feedback.py captures/session_20260701_163558.log --after 29
```

---

## Spike 2026-06-30 — `session_20260630_113422.log` (Phase 3)

| Metric | Value |
|--------|-------|
| `immediate dependent` bursts | 23 |
| `settle refresh` | 5 |
| Max gap between F2 bursts | **52.4 s** |
| `#DBG outbound_step=SEND_F1` without `SEND_F2` | Many (hybrid build) |

Fast fader1 sweeps (12–20 s) show 0.1–0.3 s F2 burst spacing; slow tails and post-sweep micro-moves often have no F2 within 3 s. Fader1 pitchbend min (`MI,H,224,16,0,94` at ~28.5 s) precedes F2 coarse by ~2.4 s — selection/feedback lag.

---

## Spike 2026-06-30 — `session_20260630_191718.log` (Phase 3 + RC11)

| Check | Result |
|-------|--------|
| HITL `note_edit_fader_select_refresh` | **PASS** — 0 clusters missing F2, max gap 3.1 s |
| F1 inbound | Full −8192..8191, slots 0..29 |
| F1→F2 dependent refresh | 57 F2 value changes; responsive |
| F2→F1 bracket follow | 91 F1 outbound during F2 manual phase |
| `#DBG outbound_ctx` | Confirms RC11 — `pb != expected_pb_rel` when `anchor_tick != rel_tick` |

**Analyze:**

```bash
python3 scripts/analyze_fader2_select_feedback.py captures/session_20260630_191718.log --after 51.0
rg '#DBG outbound_ctx|#DBG select_slot|#DBG outbound_step' captures/session_20260630_191718.log
```

**Phase 8 pass gate:** every position-mode `outbound_ctx` line has `pb == expected_pb_rel`.

---

## Repro (manual)

1. Flash `teensy41-capture-serial`.
2. Record a 2-bar loop with several notes on one track.
3. Enter NOTE_EDIT — wait through ignore window (~1.5–5 s).
4. Observe fader1 motor (bracket) and fader2 (start position) vs piano roll.
5. Fader1-select a different note — observe fader3 moves, fader2 does not.
6. Move fader2 — note start moves (confirm inbound OK).
7. Press NOTELEN — move fader2 across full travel.

**Pass:** note length spans usable range (~full loop). **Fail:** tiny length change; serial shows `Skipping fader 2` or missing `Session fader sync: sent fader 2 coarse`.

---

## Serial pass criteria

After NOTE_EDIT entry with selected note:

- Log includes `Session fader sync: sent fader 1 to select bracket` within ≤2 s of entry.
- Log includes `Session fader sync: sent fader 2 coarse to selected note` before or with fader3 sync.
- No `Skipping fader 2 update` on first select after entry (unless user edited within `FADER2_PROTECTION_PERIOD`).

After fader1 note select (`!lengthEditingMode`):

- Log includes coarse + fine feedback for fader2/3 together (not fader3-only).

After NOTELEN enable:

- Log includes `Length editing mode ENABLED` and coarse feedback with `LENGTH EDIT` anchor tick.
- Subsequent `LENGTH EDIT: pitchbend` lines show target ticks across usable loop range (not clamped to tiny delta on first move).

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **No** — `NoteEditManager` remains fader feedback owner. |
| State transition change? | **Minor** — coalesce schedulers; coarse+fine sent as pair. No new session type. |

Patch allowed without formal reassessment.

---

## Patch history

| Date | Result |
|------|--------|
| 2026-06-29 | OpenSpec `note-edit-fader-feedback-regression` opened from manual hardware report; regression tied to `d49e4c8` deferred session fader sync. |
| 2026-06-29 | **Capture:** `captures/session_20260629_203110.log` (1162 lines, manual repro). See spike notes below. |
| 2026-06-29 | **Firmware fix:** session sync coordinator — paired coarse+fine step 2, fader4-only step 3, deduped schedulers, narrowed input gating, NOTELEN anchor INFO log. Native 297/297. |
| 2026-06-30 | **Capture `session_20260630_013027.log`:** firmware emits `MO,224,14` + `MO,144,14,0` on note select (24×) but **no `MI,H,224,14` echo** — DROID motor not moving. Fader1/fader3 use notegate note **0**; coarse used note **1**. Fix: `MOTOR_TRIGGER_NOTE=0`, inline coarse on select. |
| 2026-06-30 | **Phase 2:** Deferred dependent refresh pipeline — non-preemptive NoteSelect coalesce, 1000 ms fader1 quiet gate, `#DBG outbound_step` instrumentation. Native tests extended. |
| 2026-06-30 | **Diagnostic path:** immediate F2–F4 + PC re-arm + settle timer — partial motor improvement, erratic on fast-then-slow (RC7–RC10). |
| 2026-06-30 | **Phase 3:** Selection-driven outbound state machine — slot-index navigation, 400 ms user-classified quiet, frame-stepped coordinator restored. |
| 2026-06-30 | **`ab1e3b0`:** WIP Phases 1–3 outbound coordinator + capture diagnosis. |
| 2026-06-30 | **`2ecf25d`:** F2 outbound capture diagnostics (`#DBG outbound_ctx`) + `scripts/analyze_fader2_select_feedback.py`. |
| 2026-06-30 | **Capture `session_20260630_191718.log`:** HITL timing verifier **PASS**; RC11 confirmed via `outbound_ctx` (`pb != expected_pb_rel`). |
| 2026-07-01 | **RC14 Phase 0–1:** MO→MI correlator + DROID ch13 ack patch + extended `select_motor_sync` logs; `164040` 8/8 MO without MI echo; Phase 2 deferred. |

---

## Spike 2026-06-29 — `session_20260629_203110.log`

**Setup:** `capture_session.py` on `/dev/cu.usbmodem154944801`; firmware already on device (post-`d49e4c8`). `Session fader sync` lines are **LOG_DEBUG** — not present in capture; infer from `#CAP,MO,*` outbound MIDI.

### NOTE_EDIT entry — no ch15 feedback until select + grace

| Time (s) | Event |
|----------|--------|
| 2805.287 | Edit Mode → NOTE_EDIT |
| 2806.915 | Edit Mode toggle (NOTE_EDIT again) |
| 2806.924–2817.3 | Fader1 select pitchbend only (`MI,H,224,16,*`) — **no** `MO,192,15` / `MO,224,15` outbound |
| 2817.345 | `Start editing enabled - grace period elapsed` |
| 2817.345 | **First ch15 feedback:** `MO,192,15,2` + `MO,224,15,22,91` (coarse) + `MO,176,15,2,68` (fine) + `MO,176,15,3,70` (note) |

**~11 s** from NOTE_EDIT entry to first fader2/3 motor feedback — matches deferred sync + grace, not immediate session entry sync.

### Fader1 reselect — coarse + fine sent together (pass)

| 2818.185 | Second grace + `MO,224,15,99,91` (coarse) + `MO,176,15,2,75` (fine) + `MO,176,15,3,55` (note) |

Coarse and fine outbound in same burst (lines 115–124).

### Position edit — fader2 inbound OK

`MI,H,224,15,*` inbound sweep (lines 137–386); `#CAP,DNTE,55,...` shows note 55 start moving (e.g. 1067 → 251 → 203 → 155).

### NOTELEN — outbound then tiny length commit (fail symptom)

| 2828.623 | `Length editing mode ENABLED` |
| 2828.624 | Outbound: `MO,224,15,29,106` (coarse end) + `MO,176,15,2,64` (fine center) |
| 2829–2835 | Extensive fader2 inbound (`MI,H,224,15,*` lines 415–586) |
| 2835.705 | `Length editing mode DISABLED`; commit `baselineEnd=1274 newEnd=1221` (**Δ53 ticks** only) |

Large fader2 travel during length mode produced only a **53-tick** end change — consistent with stale motor / clamp / anchor mismatch (RC3).

### Serial pass criteria (this capture)

| Check | Result |
|-------|--------|
| ch15 coarse+fine within 3200 ms of NOTE_EDIT entry | **Fail** — first burst at ~11 s |
| coarse+fine paired on select | **Pass** (2817, 2818) |
| NOTELEN usable range | **Fail** — Δ53 ticks after full sweep |
| `Session fader sync:` log lines | **N/A** — DEBUG not in capture |

---

## Spike 2026-06-29 — capture pack `214220/214540/214824`

### Scope and purpose

These captures were taken after multiple uploads during the same session and show two scheduler variants:

- `session_20260629_214220.log`: note-select outbound/coalesce path (`note_select_outbound`)
- `session_20260629_214824.log`: deferred note-select path (`note_select_deferred`)
- `session_20260629_214540.log`: deferred path + NOTELEN interaction

### Evidence 1 — deferred fader2/3 sync while fader1 keeps moving (root-cause)

From `session_20260629_214824.log`:

- repeated `scheduleOtherFaderUpdates ... note_select_deferred` while fader1 messages continue (`MI,H,224,16,*`)
- no immediate ch15 sync at each select change
- ch15 sync starts only after grace expiry:
  - `Start editing enabled - grace period elapsed (750 ms since selection)`
  - then `enableStartEditing ... ch15_sync`
  - then `beginChannel15NoteSelectSync` + outbound ch15 coarse/fine/note-value

This proves fader1 movement does not trigger immediate fader2 update in deferred mode; update is intentionally postponed until no further select changes happen during grace.

### Evidence 2 — length-mode coarse movement continuously reschedules delayed fader1 sync

From `session_20260629_214540.log` after `Length editing mode ENABLED`:

- repeated lines:
  - `scheduleOtherFaderUpdates ... fader1_resync`
  - `sendSelectnoteFaderUpdate ... scheduled (1600)`
- high-rate inbound coarse messages (`MI,H,224,15,*`) during this period

This proves length-mode coarse input repeatedly arms delayed fader1 resync, producing scheduler churn and making outbound behavior appear unreliable while movement is continuous.

### Evidence 3 — coarse ignore window blocks immediate inbound coarse edits

From `session_20260629_214540.log` after outbound ch15 send:

- repeated `shouldIgnoreFaderInput ... coarse_blanket_ignore` while coarse inbound (`MI,H,224,15,*`) is present

This confirms first coarse moves during the ignore window are discarded by design, which amplifies the "fader2 not responding" perception.

### Evidence 4 — DROID parameter mismatch is rejected by capture

Across all three captures:

- fader1 inbound arrives as expected (`MI,H,224,16,<lsb>,<msb>`)
- fader2 inbound arrives as expected (`MI,H,224,15,<lsb>,<msb>`)
- firmware emits corresponding outbound (`MO,192,15,*`, `MO,224,15,*`, `MO,176,15,*`)

There is no missing DROID parameter requirement in these traces; transport payload shape is valid and consumed.

### Updated root-cause statement

The current regression is a scheduler-policy issue inside `NoteEditManager`, not a MIDI payload-parameter issue:

1. **Deferred note-select policy** (`note_select_deferred`) with grace gating prevents immediate ch15 updates during active fader1 movement.
2. **Length-mode coarse edits** repeatedly schedule delayed fader1 resync (`sendSelectnoteFaderUpdate`), creating continuous deferral churn.
3. **Coarse blanket ignore window** drops immediate post-outbound coarse inbound events.

---

## Dwell-gap fix — serial pass criteria (2026-07-01)

**Symptom:** Fast F1 sweeps track; slow F1 crawls do not — delta/time gates and `apply=0` motor silence.

**Fix:** `syncMotorsFromSelectTarget` when selected **note** changes (`noteIdx`); F1 echo-only ignore; geometry/time walls removed from select path. Same-note F1 crawl logs `unchanged_note` and does not send motors.

**Analyzer:** `python scripts/analyze_fader2_select_feedback.py captures/<session>.log` — exit code 2 when `fader_select_dwell_gap_ok` is false.

| Gate | Rule |
|------|------|
| Dwell motor gap | Same `select_slot idx`, F1 pitch span ≥ 200 → `#CAP MO,224,14` value change within 300 ms |
| DNTE–motor coupling | `DNTE` selectedIdx change → F2 (`MO,224,14`) or F4 (`MO,176,15,3,*`) change within 50 ms |
| Ignored rate | `select_ignored_rate` ≈ 0 on slow segment (echo-only `reason=echo`) |
| Not sufficient | `apply=1` → `SEND_F*` alone — FAIL if dwell gap > 0 |

**Manual:** Slow 3-note F1 glide — F2/F3/F4 motors move visibly at each step.

**Capture log markers:** `#DBG outbound_ctx f2 mode=SELECT_SYNC`, `#DBG select_slot ignored=1 reason=echo`.

### Select-dependent settle window (2026-07-01)

**Symptom:** F2/F3/F4 motor echo after `SELECT_SYNC` passes the 200 ms `POST_UPDATE_GRACE_PERIOD` and triggers `moveNoteWithOverlapHandling` / pitch edits (~200–400 ms after sync burst). Stale `noteSelectionTime` from session entry fails the 1500 ms post-select coarse block.

**Fix:**

1. **Settle gate** — `SELECT_DEPENDENT_SETTLE_MS` (450): after any select-dependent motor send (`syncMotorsFromSelectTarget` or pipeline `SendCoarse`/`SendFine`/`SendNoteValue`), F2/F3/F4 inbound is validation-only (`recordFaderInputForValidation`) — no geometry edits.
2. **Note-changed motor sync** — `syncMotorsFromSelectTarget` only when `noteIdx` changes; apply selection before sync so F4 pitch reflects applied note.

**Capture pass criteria:**

| Gate | Rule |
|------|------|
| Echo edit block | Zero `moveNoteWithOverlapHandling` within 450 ms of `#DBG outbound_ctx f2 mode=SELECT_SYNC` |
| F4 echo block | Zero F4 pitch-edit CC within 450 ms of `SELECT_SYNC` |
| Settle armed | `#DBG select_dependent_settle until_ms=...` after each sync burst |
| Note crossing motors | `display_note_changed` → `select_motor_sync sent=1` + `MO,224,14` |

### Display-driven motor sync (2026-07-01)

**Symptom:** Capture `session_20260701_173857` — step/slot motor dedup (`shouldSyncMotorsOnSelectTarget`) decoupled motor sync from display note info; `step_changed` never fired; same-step F1 crawl logic blocked intended pitch (F4) updates.

**Fix:**

1. **Motor sync on display index change** — `EditManager::applySelectNav` calls `syncMotorsForDisplaySelection` when `priorSelection.displayIdx != displayIdx` and `requestFaderSync=false`.
2. **Note-only apply gate** — `handleSelectFaderInput` uses `editManager.getSelectedNoteIdx()` + `shouldApplySelectionOnNoteChange`; no inline motor sync.
3. **Remove step/slot motor state** — drop `lastMotorSynced*`, `shouldSyncMotorsOnSelectTarget`, `markMotorsSyncedForSelectTarget`.

**Capture pass criteria:**

| Gate | Rule |
|------|------|
| Note crossing | `select_motor_sync sent=1 reason=display_note_changed` + `#DBG outbound_ctx f2 mode=SELECT_SYNC` on every display index change |
| Same-note crawl | No apply, no motor sync |
| Revisit | Motor sync even when F2/F4 values match prior sends |

### Same-bracket sibling F4-only motor sync (2026-07-01)

**Symptom:** Capture `session_20260701_175927` — grouped same-tick notes update display NOTE row but F4 pitch fader appears stuck; `syncMotorsFromSelectTarget` sent full F2+F3+F4 burst when only F4 should move (`mo_mi_f4_miss_rate=0.898` on DROID path).

**Fix:**

1. **Snapshot-driven motor sync gate** — `applySelectNav` uses `displayNoteInfoChanged` (pitch, storageStart, displayStartTick, selectedIdx) + `NoteRef` change; not index-only.
2. **F4-only same-bracket path** — `planForSelectDependentFromDelta` drives `syncMotorsFromSelectTarget`; same bracket + different noteIdx → F4 CC + trigger only (`reason=display_pitch_changed_same_bracket`).
3. **Apply gate** — `handleSelectFaderInput` uses `shouldApplySelectionOnTargetChange` (bracket + noteIdx).

**Capture pass criteria:**

| Gate | Rule |
|------|------|
| Same-bracket sibling apply | `select_motor_sync sent=1` within 50 ms; F4 MO value change within 50 ms |
| Same-bracket F4-only | `reason=display_pitch_changed_same_bracket`; no F2 MO required |
| Different bracket | `reason=display_note_changed`; full F2+F3+F4 burst |
| Analyzer | `fader_select_sibling_sync_ok` via `scripts/hitl/verify/fader_select_sibling_sync.py` |

### Note-changed-only motor sync (2026-07-01)

**Symptom:** Capture `session_20260701_160025` — missing F2/F3/F4 motors during slow F1 crawl were **`apply=0` on same note**, not a rate-limit failure. Slot-index apply + per-pitch `syncMotorsFromSelectTarget` + `shouldSyncMotorsOnSelectTarget` duplicated gates and fired motors on every pitchbend before silently skipping.

**Fix:**

1. **Note-only apply + sync** — live F1 path uses `shouldApplySelectionOnNoteChange(priorNoteIdx, newNoteIdx)`; apply selection and `syncMotorsFromSelectTarget` only when `noteIdx` changes (`noteIdx >= 0`).
2. **Empty steps ignored** — `noteIdx == -1` → no apply, no sync; prior note selection held until F1 lands on another note.
3. **Drop live rate-limit** — remove `forceSync` / `shouldSyncMotorsOnSelectTarget` from live path; sync always sends when invoked.
4. **Diagnostic logging** — `#DBG select_motor_sync sent=… reason=sent|unchanged_note|empty_step_ignored`; `#DBG select_dependent_settle_block` (first per settle window).

**Capture pass criteria:**

| Gate | Rule |
|------|------|
| Note crossing | `select_motor_sync sent=1 reason=sent` + `#DBG outbound_ctx f2 mode=SELECT_SYNC` on every `note_changed` apply |
| Same-note crawl | `select_motor_sync sent=0 reason=unchanged_note` — no MO expected |
| Empty slot pass | `select_motor_sync sent=0 reason=empty_step_ignored` — prior note held |
| Settle block | `#DBG select_dependent_settle_block` at most once per settle window |
