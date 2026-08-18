# Capture — hot-stop cleanup + incremental sanity

**Date:** 2026-07-04  
**Follow-on OpenSpec:** `capture-pass-boundary-materialization` (Q9 boundary split deferred)  
**Parent:** [`edit_session_action_geometry_prior_art_refinement.md`](edit_session_action_geometry_prior_art_refinement.md) Q16

---

## Decision (Q16 locked)

| Item | Value |
|------|--------|
| **Setting** | **`noteMinLengthTicks`** (threshold) + **`noteMinLengthRemoveEnabled`** (on/off) |
| **Defaults** | **12 ticks** + **enabled** |
| **Disable** | Set **`noteMinLengthRemoveEnabled = false`** — hot stop skips short-pair removal (flams/grace preserved) |
| **When** | **Hot stop** (`sealCapture` on record and overdub **stop**; not `CommitReason::OverdubWrap`) |
| **What** | Remove **completed pairs** (on+off) with linear span **&lt; `noteMinLengthTicks`** |
| **Not** | NOTE_EDIT overlap floor (32nd D16); not macro **`noteEditPass`**; not idle-only v1 |

---

## Three capture cleanup tiers

| Tier | When | Module |
|------|------|--------|
| **Incremental** | **Disabled v1** — no live `capture.store` mutation | — |
| **Hot stop** | Record and overdub `sealCapture` | `LoopStopFinalize` + Q16 + verify |
| **Deferred idle** | ~60s after record stop (fallback) | `validateAndCleanupMidiEvents` (log-only v1) |

---

## Incremental capture sanity (tier 1b) — disabled v1

**Files:** `include/Utils/CaptureIncrementalSanity.h`, `src/Utils/CaptureIncrementalSanity.cpp` (module + unit tests only)

Live capture is append-only + incremental `capturePreview`. **Do not** mutate `capture.store` on `appendCaptureEvent` or in the main loop.

1. **Pair-close hook** — **not wired** (wrap context needs full pass at seal)
2. **Wrap-window slice** — **disabled v1**
3. **Main-loop budget slice** — **disabled v1**

---

## Hot stop pipeline

### Record stop (`sealCapture` on `capture.store`)

```
finalizePendingNotes (before commit)
  → truncate capture at loop length
  → commitCapturePass → sealCapture
       → finalizeWrapWindowOnStore
       → removePairsShorterThanNoteMinLength
       → verifyCaptureHotStop (log only)
```

### Overdub stop (`sealCapture` on `capture.store`)

```
flushPendingNotesIntoCapture (before commit)
  → commitCapturePass → sealCapture
       → finalizeWrapWindowOnStore
       → removePairsShorterThanNoteMinLength
       → verifyCaptureHotStop (log only)
  → publish overdub pass (separate pass row; no merge-commit)
  → finalizeLoopAtStop: schedule deferred validate only (no write-back)
```

Each overdub pass is sealed independently; record and earlier overdub passes are untouched (undo-safe).

### Overdub wrap (`sealCapture` on `capture.store`)

`CommitReason::OverdubWrap` still runs `finalizeWrapWindowOnStore` + `verifyCaptureHotStop`. It does **not** run `removePairsShorterThanNoteMinLength` — Q16 is hot stop only ([`overdub_loop_head_playback_ledger_bugfix.md`](overdub_loop_head_playback_ledger_bugfix.md)).

---

## Config

| Symbol | Role |
|--------|------|
| `Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` | Compile-time default threshold (12) |
| `Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED` | Compile-time default **on** |
| `noteMinLengthTicks` | Runtime threshold; **`loadConfig` / `saveConfig`** when persistence wired |
| `noteMinLengthRemoveEnabled` | Runtime **enable** — `false` disables removal entirely |
| `Config::DUPLICATE_TICK_TOLERANCE` | Unchanged — live **event** dedup during append |

---

## Implementation tasks

1. `CaptureIncrementalSanity` — Q16 removal + verify (hot stop only); module tests
2. `LoopEventValidation::repairOrphanNoteEvents` — shared with idle validate (log-only v1)
3. Wire `sealCapture` (record + overdub), `flushPendingNotesIntoCapture`, slim `finalizeLoopAtStop`
4. Native `test/test_capture_incremental_sanity/` + `test/test_capture_note_min_length/` + `test_loop_take_survival`
5. Wire `loadConfig` / `saveConfig` when EEPROM/SD user settings exist

---

## Distinction from NOTE_EDIT

| | Capture **NoteMinLength** | NOTE_EDIT D16 floor |
|--|---------------------------|---------------------|
| Threshold | User global, default **12t** | **32nd** (24t) hide after overlap shorten |
| When | Hot stop (`sealCapture`) | Every geometry tick |
| Scope | Published capture pass | Live **NoteEditSession.store** |
