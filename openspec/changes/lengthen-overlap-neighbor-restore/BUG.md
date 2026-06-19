# BUG — Lengthened mover overlap neighbors: pitch restore vs move-back / Take baseline

**Change:** `lengthen-overlap-neighbor-restore`  
**Status:** OpenSpec proposed (2026-06-19) — implement after `/opsx:apply`  
**Evidence:** `captures/host_midi_automation_edit_baseline_20260619_144458_serial.log` + JSON report

**Related:**

- `note-move-pitch-overlap-flaky` — unified overlap engine + session snapshot (design agreed; partial patches)
- `m8-edit` — NoteEditSession / EditChange replay (rematerialize path)
- `edit-record-display-length-mode` — length-mode leak (separate; partially fixed 2026-06-19)

**Vocabulary:** [Naming-Vocabulary-Teensy-Looper.mdc](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc) — **moving note** + **overlap note**; **hidden** / **shortened** / **visible** store states.

---

## User report (2026-06-19)

When a **lengthened moving note (M0)** is moved over an **existing same-pitch overlap note (P0)**:

1. Overlap handling **resolves note-off / note-on** (shared release at mover start).
2. On the **next move**, the inner overlap note is **contained-deleted** (hidden in store).
3. The overlap note **reappears when the moving note changes pitch** (60→67).
4. Suspected root cause: restore uses **`movingNote.deletedNotes` scratch**, **not** the committed **Record Take** baseline — so later moves lose neighbors or leave wrong gate lengths.

---

## Expected behavior

| Step | Expected |
|------|----------|
| Lengthen M0 in edit | M0 span extends; Record Take unchanged |
| Move M0 over P0 (same pitch) | P0 temporarily **hidden**; gate preserved in session ledger |
| Move again while still over P0 | P0 stays hidden; **no duplicate delete**; ledger stable |
| Pitch M0 60→67 **without fader-1 reselect** | P0@60 **visible** again (no pitch conflict); inner A@67 **not merged/lost** |
| Move past inner neighbors | Uncovered neighbors **visible** with record gate (~96 ticks) |
| Return M0 home **without reselect** | M0 at fixture home; inner neighbors **hidden or visible** per span, not lost |
| End of overlap round-trip | Store matches fixture topology; P0@592 end≈688, not loop-end stretch |

Overlap notes SHALL survive the full overlap round-trip using **one coherent restore model**
(baselineMap + **overlapNotes** — see design).

---

## Actual behavior (serial evidence @ 20260619_144458)

### 1. Move over P0 — contained delete + shared release

@ **63.416s** — `POSITION EDIT: step 0 → 10`

```
Initialized moving note: pitch=60, start=16, end=688
Will delete completely contained note: pitch=60, start=592, end=688 (within moving note 496-1168)
Temporarily deleting MIDI event pair: NoteOn pitch=60 tick=592, NoteOff pitch=60 tick=688
Stored deleted note: pitch=60, start=592, end=688, length=96
Moved note events: pitch=60 start->496 end->1168
```

Reconstruction shows **shared release @ tick 496**: mover note-on @496, A@67 note-off @496.

### 2. Pitch change — neighbor restored from scratch, not Take

@ **65.818s** — pitch 60→67

```
Will restore note after pitch change: pitch=60, start=592, end=688 (no longer conflicts with new pitch 67)
Restoring deleted note: pitch=60, start=592, end=688
Restored 1 notes after pitch change
Skipping adjacent merge for inner note under original span: pitch=67, start=403, end=496
Note value changed successfully: 60 -> 67
```

**P0 reappears** in reconstruction (`Final note: pitch=60, start=592, end=688`) — matches user observation.

**No** log line referencing Take rematerialize, `applyEdits`, or `readStore()` baseline for P0.

### 3. Next move — inner skip, empty deletedNotes

@ **67.019s** — move step 10→2

```
deletedNotes=0
Skipping overlap on inner note under session span: pitch=67, start=403, end=496 (mover 112-784)
Found 0 notes to restore
```

P0 was already restored on pitch; **not** re-entered in `deletedNotes` when mover covers it again.

### 4. HITL verification failures (same run)

| Check | Result |
|-------|--------|
| Beat moves POSITION | **pass** |
| `contained_delete` / `pitch_restore_log` / `p0_restore_event` | **pass** |
| `m0_home_ok` | **fail** |
| `inner_a_contained_at_home` | **fail** |
| `after_overlap_pitch_change:native_m0_count:0!=1` | **fail** (rematerialize snapshot) |
| `shortened_victim_gone_after_restore:p60@592` | **fail** (legacy issue key; means hidden neighbor not restored) |
| `shortened_still_short_after_restore:p60@592:end=688<original=1535` | **fail** |

---

## Hypotheses (to resolve in design)

| ID | Claim | Confidence |
|----|-------|------------|
| **H1** | Pitch restore reads **`movingNote.deletedNotes` only**; no fallback to **Record Take** / pre-edit snapshot | **High** — serial shows restore path, no Take I/O |
| **H2** | After pitch restore, **deletedNotes cleared** → subsequent contained overlap **not tracked** → move-back cannot restore | **High** — `deletedNotes=0` after pitch @ 67s |
| **H3** | **Shared-release LIFO pairing** at mover start (tick 496) leaves ambiguous note-on ownership (A off / M0 on) | **Medium** — reconstruction lines @ 63.417 |
| **H4** | **Edit rematerialize** (`applyEdits` / session store) diverges from live scratch after pitch+move chain | **Medium** — native parity fails post-pitch snapshot |
| **H5** | P0 `original_end=1535` in note-length verifier is **stale peak** (loop-end artifact), not Take truth | **Medium** — investigate before using as AC |

---

## Repro

**HITL:**

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --record-bars 2 --start-transport \
  --boot-settle-ms 12000 --phase-wait-ms 500 --final-wait-ms 3000 \
  --press-ms 120 --undo-redo-delay-ms 500
```

Focus segment: `_run_overlap_round_trip_case` after `_extend_m0_for_long_over_short`.

---

## Architecture checkpoint

| Question | Preliminary answer |
|----------|-------------------|
| Ownership of neighbor restore? | **Yes** — split between `movingNote.deletedNotes`, pitch handler, move handler, and (future) Take/session baseline |
| Take storage mutated? | **No** on Record Take; edits in RAM / EditChange list until `saveEdit()` |
| State transition change? | **Yes** — pitch restore clears ledger; need normative lifecycle (see `note-move-pitch-overlap-flaky` design + this change) |

---

## Serial pass criteria (additions)

After overlap round-trip on standard fixture:

- Pitch step: `Restoring deleted note: pitch=60, start=<p0_tick>` **or** documented Take-baseline restore log (new).
- After pitch: separate P0@60 gate **and** M0@67 mover; **no** mega-span merge log.
- After return home: M0 start within tolerance of `m0_tick`; P0 gate end within tolerance of record gate.
- Post-pitch rematerialize snapshot (`_verify_change_length_store_rebuild`): native M0/M0@67 counts pass.
- No `Final note: pitch=60, start=<p0_tick>, end=<loop_length>` unless explicit length edit.

---

## Patch history

| Date | Result |
|------|--------|
| 2026-06-19 | OpenSpec `lengthen-overlap-neighbor-restore` opened from user report + `144458` capture |
| 2026-06-19 | Renamed from `lengthen-overlap-victim-restore`; vocabulary **neighbor note** + **hidden** (not covered/victim) |
| 2026-06-19 | Track A+B sign-off capture `203729` — rematerialize + `m0_home_ok` + `inner_p0_ok` + `change_length_store` + `note_length_integrity` green; `inner_a_contained_at_home` open |
