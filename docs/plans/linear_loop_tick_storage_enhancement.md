# Handoff — Linear loop tick storage

**Date:** 2026-07-03 (updated: dual normalize boundaries, edit closure set, set-window fader rules)  
**OpenSpec:** [`openspec/changes/linear-loop-tick-storage/`](../../openspec/changes/linear-loop-tick-storage/)  
**Plan:** Cursor plan `linear_loop_tick_storage_4c7cb38f.plan.md`  
**Apply:** `/opsx:apply` on `tasks.md`  
**Phase 1 status (2026-07-03):** `LoopTickNormalize`, `LoopEventValidation`, native tests, spec expansions, `validateAndCleanupMidiEvents` orphan-only refactor — **shipped**. Phases 2–6 remain in `tasks.md`.

---

## One-line goal

Canonical loop MIDI storage is **linear** (`NoteOff.tick >= NoteOn.tick`, may exceed `loopLength`). **Dual normalize:** `normalizeWindow` on edit closure set at fader latch (micro); `normalizeAll` at `commitAllPendingNoteEditActions` (macro, persistent canonical). **Validate** asserts; **projection** derives wrap and never writes back.

---

## Why now

- **152335:** move past loop end sets `NoteOff` to `0` via `% loopLength` — note cut off
- **Loop stretch:** synth offs at `loopLength - 1` inflate display length on extension
- Supersedes partial fix `note-edit-tick-coordinates-and-audition`

---

## Architecture

| Subsystem | Role |
|-----------|------|
| `LoopTickNormalize` | **Micro:** `normalizeWindow(closureSet)` at fader latch. **Macro:** `normalizeAll` at edit commit |
| `LoopEventValidation` | Pure predicates; no mutation |
| Projection | `reconstructNotes`, display, fader anchors — no write-back |

**Dual boundaries:**

| Boundary | Hook | API |
|----------|------|-----|
| Micro | `publishDependentFaderLatch` | `normalizeWindow` on **edit closure set** |
| Macro | `commitAllPendingNoteEditActions` | `normalizeAll` — persistent canonical + undo |

**Edit closure set:** seed = modified `NoteId`s → expand paired on/off, overlap participants, wrap interactors (not UI window).

**NOTE_EDIT playback:** Tier 2 `sessionMidiEvents()` — §2.4 is verification only.

**Set window:** drives F1/F2 range and selectable notes; when window = loop length, fader extremes wrap. Partial-window slide = future (§2.7).

**Loop shorten:** projection hides; storage retained.

---

## Dev operator steps

1. Flash firmware with linear-tick change
2. Send serial `!DEV_RESET_SD` (SESSION_CAPTURE build)
3. Confirm `#CAP,PERS,dev_reset_sd,…,ok`
4. Re-record test loops (pre-linear SD data is intentionally discarded)
5. Run HITL regression matrix (move across loop end, stretch, faders)

Load rejects non-canonical slot files (`LOAD_REJECT non-canonical tick storage`) as safety net.

---

## Delivery phases

| Phase | Deliverable |
|-------|-------------|
| 1 | Normalize + validate + projection boundary + native tests |
| 2 | Linear off writes + dual normalize + set-window fader range + playback verification |
| 3 | Capture/stop linear offs |
| 4 | Loop length stretch preservation |
| 5 | Dev SD reset + load reject + passes |
| 6 | Full native + HITL + archive |

---

## Key files

- New: `LoopTickNormalize.*`, `LoopEventValidate.*`
- Edit: `NoteMovementUtils.cpp`, `NoteEditManager.cpp`
- Stop: `LoopStopFinalize.h`, `Track.cpp`, `Loop.cpp`
- SD: `StorageManager.cpp`, `StorageLoopIo.cpp`
- Tests: `test_loop_tick_normalize` (new)

---

## Relationship to other work

- **Orthogonal** to `set-revision-persistence` overlay (CURRENT_WORK primary)
- **Supersedes** `note-edit-tick-coordinates-and-audition`
- Shares HITL patterns with `rev_nuke_sets` / `!REV_CLEANUP`

---

## Acceptance (regression matrix)

Move/resize across loop end, extend/shorten loop, undo wrapped edit, save/load post-reset, overdub wrap, same-pitch overlap, playback unchanged, display wrap correct.
