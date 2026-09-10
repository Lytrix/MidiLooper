# Overdub live wrap — open loop-end playhead tail display

**Status:** Stage 2b device-verified for STOPPED wrap split
([`193615`](../../captures/session_20260910_193615.log): `reconstruct` =
`effective` = `frame` = 58; wrap pitches keep On→loopEnd + head). HITL `base`
still FAIL `persistence_stage_missing:dispatch` (separate).  
**Separate from:** OpenSpec 4.6 Stage 4 (`742852ec`)  
**Observed (fixed class):** During OVERDUBBING / STOPPED, wrap On→loopEnd was
dropped while head remained — shared `NoteId` upsert/unique-by-id collapsed
head+tail ([`191636`](../../captures/session_20260910_191636.log) pitch 18 len
223).  
**Not this:** DisplayNoteWindowGather stash; LOOP_EDIT length-fader mid-capture;
DEC-051 seal; Occupy/Shorten/Hide; 4.6 identity; Option B VC preference;
persistence dispatch missing.

## Debugging boundary

```text
… → capturePreview open indices / wrapHeld state     ← trust unless logs disagree
 → resolveDisplayNotes path fork (NOTE vs draw-source)
 → allowWrapContinuation during OVERDUBBING          ← Stage 2 (open preview)
 → applyCapturePlayheadTails linear vs wrap split
 → resolveDisplayNotesCommitted + append             ← Stage 3A
 → resolveDisplayNotesByDrawSource + append          ← Stage 3B (OLED)
 → STOPPED seal / storage WRAP rows                  ← out (reconstruct OK)
 → snapshot span membership completeness             ← follow-up publish RC;
                                                       consumer fix:
                                                       overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md
```

Do **not** make NOTE_EDIT the LOOP_EDIT display owner. Do **not** reopen
NOTE_EDIT projection architecture — only compare its wrap paint result as an
oracle for Stage 0.

## Symptom (product)

During **OVERDUBBING**, an open capture-preview note near loop end should paint
as a live playhead tail, then as the documented live wrap split after phase-0.
Instead the pre-wrap tail disappears and only the post-wrap head remains.

At **STOPPED**, reconstruct / visualCache / effective keep On→loopEnd + head,
but **LOOP_EDIT OLED/`frame`** keeps only the head.

### NOTE_EDIT vs LOOP_EDIT (user observation — path fork)

Wrapped notes that start near loop end **paint correctly after entering
NOTE_EDIT**, then **revert when returning to LOOP_EDIT**. That is not a missing
reconstruct: wrap geometry already exists somewhere; **LOOP_EDIT is not using
the same display projection path**.

Proven fork in `DisplayManager::resolveDisplayNotes`:

| Session | Path |
|---|---|
| `EditSessionType::Note` | `editManager.projectedNoteEditDisplayNotes(track)` |
| else (incl. LOOP_EDIT) | `resolveDisplayNotesByDrawSource` → Stage 2 tails / Stage 3B completion |

## Desired invariant — pinned **(c) both**

### A — Before phase-0 wrap (open preview)

```text
expected paint: NoteOn → currentTick
```

### B — After phase-0 wrap (open preview, still held)

```text
expected paint: NoteOn → loopEnd ; loopStart → currentTick
```

### C — STOPPED (committed paint)

```text
expected paint: same split as reconstruct / WRAP storage
  NoteOn → loopEnd  (tail)
  0 → Off           (head)
```

## Policy truth table (pinned) — Stages 1–2

`shouldAllowCapturePlayheadWrapContinuation(recording, playing, overdubbing)`:

| State | Wrap continuation |
|---|---:|
| RECORDING (any playing) | **NO** |
| OVERDUBBING + PLAYING | **YES** (Stage 2) |
| stopped / sealed | n/a — committed path (Stage 3A/3B) |

## Architecture checkpoint

| Question | Stage 2 | Stage 3A | Stage 3B |
|---|---|---|---|
| Ownership change? | **NO** | **NO** | **NO** — completion step only |
| State-transition change? | **NO** | **NO** | **NO** |
| Unbounded MIDI-interval owner? | **NO** | **NO** | **NO** — STOPPED display |
| Reuse | policy predicate | `appendOverdubPassDisplayNotes` on committed resolver | same append on draw-source settle |

**Rejected:** prefer VisualCache over current snapshot when STOPPED (Option B —
reopens last-occupy / fights snapshot-first authority).  
**Deferred:** put wrap-held geometry into snapshot spans at publish (Option C —
snapshot membership design session).

## Stages

### Stage 0 — Capture proof — LOGGED

| Capture | Role |
|---|---|
| [`175831`](../../captures/session_20260910_175831.log) | Stage 1 — OD2 pitch 18; B head-only while OVERDUBBING |
| [`180916`](../../captures/session_20260910_180916.log) | Stage 2 — C fails on painted frame |
| [`183321`](../../captures/session_20260910_183321.log) | Stage 3A — effective=reconstruct; frame still short |
| [`183741`](../../captures/session_20260910_183741.log) | Stage 3A + committed snapshot append — still `frame` 57 vs 60 |
| [`191636`](../../captures/session_20260910_191636.log) | Pitch 18 WRAP `2181→100` len **223**; `preview_miss` wants `2181→2303`, frame `end=100` |
| [`193615`](../../captures/session_20260910_193615.log) | After Stage 2b: STOPPED `frame`=`reconstruct`=58; wrap 16/37 keep both segments |

### Stage 1 — Policy predicate — SHIPPED (`df24167f`)

### Stage 2 — OVERDUBBING wrap continuation predicate — SHIPPED (in tree)

### Stage 2b — Shared NoteId wrap head/tail collapse — SHIPPED (device gate [`193615`](../../captures/session_20260910_193615.log))

#### Proof ([`191636`](../../captures/session_20260910_191636.log))

- On@2181, wrap @6912, Off@100 → `WRAP,2181,100` sounding **123+100=223**.
- After Off: `preview_miss id=57 p=18 s=2181 e=2303` while
  `compose/frame … end=100` — closed preview has On→loopEnd **and** head with
  the **same NoteId**; upsert/unique-by-id collapsed to one row.

#### Fix

1. `upsertDisplayNoteAtPitchStartFrom`: same NoteId + different `startTick` →
   keep both.
2. `appendDisplayNoteUniqueById`: id+start uniqueness (VC/snapshot bar gather).
3. `Track::commitOverdubWrap`: re-appended open Ons get `wrapHeld=true`.

Native: `test_overlay_same_id_keeps_wrap_on_to_loop_end_and_head_191636`,
`test_append_display_note_unique_by_id_keeps_wrap_segments`.
RAM1 locals **7008**; native **1832/1**.

### Stage 3A — Committed resolver wrap completion — SHIPPED (in tree)

### Stage 3B — STOPPED draw-source wrap append — SHIPPED (in tree; with 2b)

Device [`193615`](../../captures/session_20260910_193615.log): STOPPED
`DISP frame=58 visual=58`; DNTE `R-F` empty including wrap heads+tails.

## Explicit non-goals (plan-wide)

- Popping DisplayNoteWindowGather stash  
- Changing wrap seal at phase 0 (DEC-051)  
- Occupy resolve / Shorten-Hide  
- OpenSpec 4.6 / `legacyMidiEventsFromCommitted`  
- Globally enabling wrap continuation outside open capture-preview OVERDUBBING  
- Making NOTE_EDIT the LOOP_EDIT / overdub display owner  
- Changing NOTE_EDIT projection architecture (oracle only in Stage 0)  

## Approval

**APPROVED** with (c) pinned. Option **A** chosen for Stage 3B (2026-09-10):
STOPPED draw-source completion only; B rejected; C deferred.
