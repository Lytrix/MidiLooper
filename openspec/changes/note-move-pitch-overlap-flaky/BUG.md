# BUG — Note move over 2+ inner notes + pitch (flaky; patches regress)

**Status:** Root-cause phase **closed** (2026-06-18). **Design session required** before patch phase.  
**Evidence capture:** `captures/host_midi_automation_edit_baseline_20260618_231858_serial.log`

---

## Session structure (mandatory)

| Section | Purpose |
|---------|---------|
| **Goal** | What this session must produce |
| **Allowed inputs** | Evidence and commands permitted |
| **Required outputs** | Artifacts before patch phase |
| **Stop conditions** | When to abort patch and open design |
| **Session boundaries** | What is in / out of scope |

### Goal

Find the **root cause** of flakiness and regression churn in note-edit overlap behavior when a **long note moves over multiple inner notes**, **pitch changes without reselect**, **moves past and returns home without deselect**, and a **short note truncates a long note and returns** — without adding more ad-hoc branches until ownership and session transitions are understood.

### Allowed inputs

- Serial captures under `captures/host_midi_automation_edit_baseline_*` and `captures/*_serial.log`
- HITL script: `scripts/host_midi_automation_edit_baseline.py` (`_run_overlap_round_trip_case`, `_verify_long_over_short_pitch_restore`)
- Firmware paths: `NoteEditManager.cpp`, `NoteMovementUtils.cpp`, `EditManager` / `movingNote`, `MidiFaderProcessor.cpp`
- Guides: `docs/Guides/MOVE_NOTE_LOGIC.md`, `docs/Guides/FADER_STATE_SYSTEM.md`
- Native tests: `pio test -e native` (read-only during root-cause phase)

### Required outputs (before patch phase)

1. One **chosen hypothesis** with others **explicitly rejected** (with evidence).
2. **Acceptance criteria** — observable, serial-verifiable, tick-tolerant where loop_start applies.
3. **Architecture checkpoint answers** (ownership? state transitions?) — if either **yes**, design session required before patch.
4. Updated row in **Patch history / regression log** (this file) when a patch is attempted.

### Stop conditions

Abort patch → open design session when:

- Fix requires **changing ownership** (who stores/restores overlap neighbor notes, who owns pitch vs move overlap).
- Fix requires **changing state transitions** (`movingNote.active`, commit on fader switch, pitch-change session reset).
- A patch touches **both** `NoteEditManager` pitch path **and** `NoteMovementUtils` move path without a unified contract.
- HITL pass/fail swings with **verifier-only** changes (signals test/spec drift, not firmware truth).

Per global rule: [architecture-checkpoint-bugfix](/Users/eelkejager/.cursor/rules/architecture-checkpoint-bugfix.mdc).

### Session boundaries

| In scope | Out of scope (separate sessions) |
|----------|----------------------------------|
| Overlap round-trip: long M0 over B/A/P0, pitch 60→67, move past, return home, short B over long M0 | Record-button context undo routing |
| `movingNote` + `deletedNotes` restore semantics | Full M8 NoteEditSession migration |
| Pitch fader during active move session | Display lag (B4 stabilization) |
| HITL verifier accuracy vs firmware | Insert/delete/create NOTELEN warmup (unless blocking repro) |

---

## Example flow (this bug)

### 1. Describe bug

User moves a **lengthened long note (M0)** over **multiple inner notes** (same and different pitch), changes **pitch on fader 4 without fader-1 reselect**, moves **past** inner notes and **back to home** without deselect, then moves a **short note (B)** over the long note and **back** — expecting inner notes to **reappear when uncovered**, **stay stored when covered**, and **short-over-long truncate + restore** on the round trip.

Incremental firmware patches were added (`isNoteWithinMovingSpan`, `preserveInnerNotes`, cross-pitch short-over-long, session continuity after pitch). HITL still fails or passes on different axes; behavior feels **flaky** and **fixes introduce new failures**.

### 2. Expected behavior

| Step | Expected |
|------|----------|
| Long M0 move over P0 (same pitch) | P0 temporarily deleted; tracked in `movingNote.deletedNotes` |
| Pitch M0 60→67 **without reselect** | P0@60 restored; **inner A@67 (and B, P0) not permanently merged/deleted** |
| Move past A | A visible in store / reconstruction when no longer covered |
| Return M0 home **without reselect** | M0 at home span; inner A contained/hidden again (deleted list or contained-delete), **not lost from loop** |
| Select B once; move 4→8 over long M0 | Long M0 **shortened** at overlap |
| Move B 8→4 **without reselect** | Long M0 **restored** to pre-shorten span |
| Continuous session | `movingNote` identity stable across fader 2 → fader 4 → fader 2 until explicit fader-1 reselect or edit exit |

### 3. Actual behavior (observed)

Evidence: `captures/host_midi_automation_edit_baseline_20260618_231209_serial.log`, `..._231858_serial.log`, `..._231858.json`.

| Observation | Evidence |
|-------------|----------|
| Pitch change **merged** inner A into long span | `Merged 1 adjacent same-pitch notes into span 392-1160` (231209); `409-1176` (231858) |
| After pitch, move session **re-inited** at wrong origin | `Initialized moving note: pitch=67, start=392, end=1160` with `deletedNotes=0` on move-past (231209) |
| Inner A **missing** after round trip | Verifier `inner_a_missing_after_round_trip`; reconstruction without separate A@392 |
| Short-over-long **worked in one run** but verifier missed it | Serial: `Will shorten note (short-over-long): pitch=67, start=25, end=792->407` + `Restoring shortened note` (231858); verifier failed on exact `start=8` vs actual `start=25` |
| `no_reselect_before_pitch` false positive | Outbound `SENDING PITCHBEND: Position` (select-note sync) counted as user reselect |
| Partial pass after patches | 231858: `inner_a_after_pitch`, `inner_a_visible_after_move_past` true; still failed `inner_a_contained_at_home`, `m0_home_ok`, insert/create markers |

### 4. Reproduction steps

**HITL (preferred):**

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --record-bars 2 --start-transport \
  --state-sync-timeout-ms 15000 --undo-redo-delay-ms 500 \
  --boot-settle-ms 2000
```

Firmware: `teensy41-capture-serial`. Overlap case: `_run_overlap_round_trip_case` in edit baseline script.

**Manual (minimal):**

1. Record fixture on track 5 (M0@step0, B@4, A@8, P0@12, …).
2. Enter note edit; lengthen M0; select M0 once.
3. Fader 2 → step 10 (over inner notes); fader 4 pitch 60→67 **without** fader 1.
4. Fader 2 → past A → back home **without** fader 1.
5. Select B; fader 2 → step 8 → back step 4 **without** fader 1 between forward/back.

### 5. Suspected subsystem

| Layer | Files |
|-------|--------|
| Move + overlap | `src/Utils/NoteMovementUtils.cpp`, `include/Utils/NoteMovementUtils.h` |
| Pitch + adjacent merge | `src/NoteEditManager.cpp` (`handleNoteValueFader` / pitch CC path) |
| Session state | `EditManager::MovingNoteIdentity`, `src/NoteEditManager.cpp` (`moveNoteToPositionWithOverlapHandling`) |
| Fader / session commit | `src/MidiFaderProcessor.cpp` (`commitMovingNote`, fader switch) |
| Legacy duplicate path | `src/EditStates/EditStartNoteState.cpp` (encoder overlap — DRY debt) |
| Verification | `scripts/host_midi_automation_edit_baseline.py` |

---

**Related:** [MOVE_NOTE_LOGIC.md](../../../docs/Guides/MOVE_NOTE_LOGIC.md), [m8_edit_note_edit_hitl_automation_refinement.md](../../../docs/plans/m8_edit_note_edit_hitl_automation_refinement.md), OpenSpec **m8-edit** (NoteEditSession), global rule **architecture-checkpoint-bugfix**.

---

## Root-cause findings (2026-06-18)

### movingNote state table — overlap round-trip segment

Inferred from `231858_serial.log`. `origEnd` is **not logged**; derived from init + code (`NoteEditManager.cpp` only sets `origEnd` on first `!active` init; length edit updates `lastEnd` only).

| Time (s) | Event | active | origStart | origEnd (inferred) | lastStart | lastEnd | deletedNotes | A@409 separate? |
|----------|-------|--------|-----------|-------------------|-----------|---------|--------------|-----------------|
| 60.977 | First move init (beat fwd) | true | 24 | **120** | 24→216 | 120→336 | 0 | yes |
| 65.449 | Return home | true | 24 | **120** | 216→24 | 336→120 | 0 | yes |
| 71.245 | **Length edit** end 120→696 | true | 24 | **120** ⚠ stale | 24 | **696** | 0 | yes |
| 76.540 | Move to step 10 | true | 24 | **120** | 24→504 | 696→1176 | 0→1 (P0@600) | yes |
| 78.947 | **Pitch 60→67** | true | 24 | **120** | 504→**409** ⚠ | 1176 | 0→1 (A merged) | **no** — merged 409–1176 |
| 80.148 | Move past (→121) | true | 24 | **120** | 409→121 | 888 | 1 (A) | no — restore blocked |
| 82.557 | Return home (→25) | true | 24 | **120** | 121→25 | 888→792 | 1 (A) | no — `Cannot restore … overlaps 25-792` |
| 84.566 | Fader-1 select B | **reset** | — | — | new B@216 | 312 | 0 | n/a |

⚠ **Stale `origEnd=120`** after length edit to 696 explains why `isNoteWithinMovingSpan(A@409–504, origStart=24, origEnd=120)` is **false** → adjacent merge not skipped (no `Skipping adjacent merge` in log).

⚠ After pitch merge, **`lastStart` jumps 504→409** (merged note-on tick). Move-path identity no longer matches “M0 home at tick 24”.

### Pitch adjacent-merge trace @ 78.947

Sequence from serial (firmware build **before** confirmed post-fix HITL rerun):

1. P0@60 restored from move-session `deletedNotes` ✓  
2. Mover still pitch 60 @ 504–1176; target pitch 67  
3. A@67 @ 409–504 adjacent at `note.endTick == noteStart` (504)  
4. **No** `Skipping adjacent merge for inner note` — inner-span check failed (origEnd stale at 120)  
5. `applyShortenOrDelete` deletes A → `Stored deleted note: pitch=67, start=409`  
6. Note-on moved 504→409 → `Merged 1 adjacent same-pitch notes into span 409-1176`  
7. Pitch updated 60→67; session stays `active`; `deletedNotes` still holds merged-away A  

**Split-path proof:** move-over at 76.540 used **contained delete** for P0 (same pitch). Pitch change used **adjacent merge** for A (target pitch) — different mechanism, same user-visible “note under long note”.

### Verifier re-run on same capture (H3)

Re-ran `_verify_long_over_short_pitch_restore` on `231858_serial.log` with **updated** verifier (tick tolerance, select-sync filter):

```
ok=True, all overlap issues cleared
```

Same firmware log that **failed** HITL at run time now **passes** verifier. Confirms substantial **verifier false-negative** contribution to “flaky” reports.

**Verifier weakness remaining:** `inner_a_after_pitch` passes when a **merged** mega-note starts at `a_tick` (409–1176 looks like “A at 409”). AC2 must require separate A gate length or `Skipping adjacent merge` / no merge log.

### Architecture checkpoint

| Question | Answer | Evidence |
|----------|--------|----------|
| Changing ownership? | **Yes** | Pitch overlap vs move overlap; who may delete/merge inner notes |
| Changing state transitions? | **Yes** | `origEnd` vs `lastEnd` on length edit; pitch merge rewrites `lastStart`; `deletedNotes` semantics differ by path |

→ **Abort patch loop.** Open **design session** (not another local branch).

### Choose hypothesis — **CLOSED**

| Hypothesis | Status | Evidence |
|------------|--------|----------|
| **H1** Split overlap implementations | **Confirmed** | P0 contained-delete (move) vs A adjacent-merge (pitch); cross-pitch shorten only in move path |
| **H2** movingNote contract undefined | **Confirmed** | origEnd stuck at 120 after length→696; lastStart jumps on merge; length edit code updates `lastEnd` only (`NoteEditManager.cpp:1220–1221`) |
| **H3** Verifier drift only | **Rejected as sole root** | Real merge in serial; but **confirmed major false-fail source** — same log passes updated verifier |

**Rejected going forward:** More `preserveInnerNotes` / skip flags without contract + unified overlap owner (architecture-checkpoint violation).

### Recommended design session agenda

1. **Single overlap owner** — pitch change during active `movingNote` delegates to `NoteMovementUtils` (or shared `resolveOverlapForEditSession`) instead of adjacent-merge block in `NoteEditManager`.
2. **`movingNote` contract doc** — table of events that update `origStart`/`origEnd`/`last*`/`deletedNotes`; **length edit must bump `origEnd`** (or drop `origEnd` in favor of session baseline snapshot).
3. **Pitch-change modes** — explicit: *(a)* lane merge for D-delay when **not** covering inner fixture notes; *(b)* preserve inner notes when mover span covers them (round-trip).
4. **Tighten AC2** — separate inner note = distinct gate at `a_tick`, not merged span starting at `a_tick`.
5. **Optional:** align with m8-edit **NoteEditSession** store instead of growing `movingNote.deletedNotes` ad hoc.

**Design:** [design.md](./design.md) — agreed 2026-06-18. Patch phase unlocked for scoped tasks there.

---

## Root-cause phase — **CLOSED** (historical checklist)

### Generate max 3 hypotheses

#### H1 — Split overlap implementations (confidence: **high**)

**Claim:** Position overlap (`NoteMovementUtils::moveNoteWithOverlapHandling`) and pitch overlap (`NoteEditManager` adjacent merge + contained delete) are **two codepaths** with different rules for inner notes, `deletedNotes`, and session continuity. Each bugfix patches one path and breaks the other (e.g. D-delay pitch merge vs overlap round-trip preserve).

**Required evidence:**

- Side-by-side trace of same fixture ticks through **move** vs **pitch** handlers.
- List of behaviors only implemented in one path (cross-pitch shorten, inner-span skip, restore on move-back).
- Confirm D-delay scenario **requires** adjacent merge while round-trip **forbids** merge for inner A.

**Files to inspect:**

- `src/NoteEditManager.cpp` (~1500–1750)
- `src/Utils/NoteMovementUtils.cpp` (`findOverlaps`, move session restore loop)
- `docs/Guides/MOVE_NOTE_LOGIC.md` vs actual pitch path

#### H2 — `movingNote` session contract undefined (confidence: **high**)

**Claim:** `active`, `origStart`/`origEnd`, `lastStart`/`lastEnd`, and `deletedNotes` have **no single lifecycle spec** across length edit, fader 2 move, fader 4 pitch, fader switch commit stub, and fader-1 reselect. Patches adjust individual fields (`origEnd` max with `lastEnd`, keep `active` after pitch) without fixing the contract → flaky restore and re-init at wrong origin.

**Required evidence:**

- State table: event × `{active, orig*, last*, deletedNotes count}` for full HITL overlap segment from serial.
- Identify every code path that sets `active=false` or clears `deletedNotes` during overlap round-trip.
- Document whether `origEnd` updates on length edit (today: only set on first `!active` init).

**Files to inspect:**

- `src/NoteEditManager.cpp` (init block ~148–160, pitch success ~1719+)
- `src/MidiFaderProcessor.cpp` (`commitMovingNote`)
- `EditManager.h` / `MovingNoteIdentity`

#### H3 — HITL verifier / fixture tick drift (confidence: **medium**)

**Claim:** Failures are **partially artifact**: loop record aligns notes to `loop_start` (e.g. M0@tick 24 not 0), verifiers used hard-coded step ticks and outbound select-sync as “reselect”, causing false fail/pass independent of music logic.

**Required evidence:**

- Compare `record_layout.step_to_tick` vs verifier `m0_tick`/`a_tick` in JSON report.
- Re-run verification function alone on 231858 log with tolerance — which issues remain?
- Separate **firmware wrong** vs **verifier wrong** issue lists.

**Files to inspect:**

- `scripts/host_midi_automation_edit_baseline.py` (`_verify_long_over_short_pitch_restore`, `_fixture_step_tick`)
- `captures/host_midi_automation_edit_baseline_20260618_231858.json`

### Rank confidence (historical)

1. **H2** — session contract
2. **H1** — split implementations
3. **H3** — verifier drift (partial)

### Reject others explicitly

- **H3 alone:** Rejected — merge/delete in firmware serial.
- **“Just add more skip flags”:** Rejected — confirmed stale origEnd; flags without contract failed.

---

## Define acceptance criteria — **DRAFT (revise in design session)**

All must pass on **one** HITL run with serial log; ticks use `record_layout` + tolerance ±24 unless noted.

| ID | Criterion | Serial / store signal |
|----|-----------|------------------------|
| AC1 | No fader-1 **user** input between move-over-P0 and pitch change | No `Received pitchbend: ch=16` / `Accepting fader 1 input` in window |
| AC2 | After pitch change, inner A exists as **separate** note (gate ≈96t), not mega-span | Separate `Final note: pitch=67, start=<a_tick>, end=<a_tick+~96>` OR `Skipping adjacent merge`; **fail** if only `Merged … span <a_tick>-<large>` |
| AC3 | Move past A reveals A | `Final note` or inventory with A@`<a_tick>` after move to past step |
| AC4 | Return home without reselect; M0 home; A not permanently lost | Contained-delete or `Cannot restore note` for A@`<a_tick>` on home move; A restorable on next move away |
| AC5 | Short B over long M0 forward | `Will shorten note` with pitch=67, start within tolerance of `m0_tick` |
| AC6 | Short B back restores long M0 | `Restoring shortened note` same pitch/start within tolerance |
| AC7 | No regression on D-delay pitch merge | Existing insert/reorder checks in edit baseline still pass |
| AC8 | Native suite green | `pio test -e native` 77/77 |

---

## Patch phase — **LOCKED until root-cause closed**

Rules when unlocked:

- **Smallest possible change** — one hypothesis, one contract change.
- **No opportunistic refactors** — no DRY sweep across `EditStartNoteState` unless scoped.
- **No architecture changes** without design session — if m8 NoteEditSession is the fix, that is a **change proposal**, not a stealth patch.

### Patch history / regression log

| Date | Change | Result |
|------|--------|--------|
| 2026-06-18 | `isNoteWithinMovingSpan`, `preserveInnerNotes`, keep `active` after pitch | Still merges A in 231858; partial HITL pass |
| 2026-06-18 | Cross-pitch short-over-long + truncated-zone restore | Shorten/restore in serial; verifier tick mismatch |
| 2026-06-18 | `origSpanEnd = max(origEnd, lastEnd)` | Uploaded; full rerun interrupted — **inconclusive** |
| 2026-06-18 | Verifier: tick tolerance, select-sync filter | Same 231858 log → overlap verifier **passes**; firmware merge still present |
| 2026-06-18 | **Root-cause closed** | H1+H2 confirmed; design session required |
| 2026-06-19 | **AC4** (M0 home, overlap round-trip) | Pass on HITL `203729`: `m0_home_ok`, `mover_at_home`, split overlap `inner_a_recaptured_at_home`; firmware via [note-edit-modification-session](../note-edit-modification-session/tasks.md) + [change-length-commit-rematerialize](../change-length-commit-rematerialize/BUG.md) Track A/B. **AC5–AC6** short-over-long: verifier flags still false (`short_over_long_forward`/`restore`); **AC7** insert/reorder still fail (Track C). |

---

## Validation (when patch phase opens)

| Layer | Command / action |
|-------|------------------|
| Existing tests | `pio test -e native` |
| Manual repro | Overlap round-trip manual steps above |
| HITL regression | Full `host_midi_automation_edit_baseline.py` |
| Regression checks | D-delay merge, insert/delete, undo/redo markers in same script |
| Capture | Save `captures/host_midi_automation_edit_baseline_<stamp>_serial.log` + JSON report |

---

## Close session

Root-cause phase **closed** 2026-06-18.

1. **Recorded** — state table, pitch trace, verifier re-run in **Root-cause findings**.
2. **Next context** — **design session** (see agenda). Patch phase **locked**.
3. Do **not** continue ad-hoc patches without `design.md` agreement.

---

## Next actions — **design session** (not patch)

1. User agrees design agenda items 1–2 (minimum) in **Recommended design session agenda** above.
2. Write `design.md` in this change folder with chosen overlap owner + `movingNote` lifecycle table.
3. Then patch phase: one PR, native tests + single HITL capture with revised AC2.
