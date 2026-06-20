## Why

Edit-baseline HITL (`20260619_144458`) shows a coherent user-visible bug chain: a **lengthened moving note** moved over **P0** (same-pitch **neighbor note**) is **contained-deleted** (hidden), **reappears on pitch change**, then **subsequent moves / rematerialize** leave **M0 not at home** and **P0 gate wrong or missing**. Serial proves pitch restore uses **`movingNote.deletedNotes`**, not the committed **Record Take** baseline — so neighbor notes can be lost or wrong after the pitch step clears the scratch ledger.

This change is **narrower** than `note-move-pitch-overlap-flaky` (unified overlap engine): it targets **neighbor note lifecycle across pitch restore + later position moves** and alignment with **Take / NoteEditSession rematerialize**.

## What Changes

- **Document normative neighbor lifecycle** for lengthened-mover overlap: contained hide → pitch restore → re-hide → move-back.
- **Restore source of truth:** define when scratch `deletedNotes` vs **session baseline snapshot** (Record Take + applied EditChanges, not raw Take mutation) MUST supply neighbor geometry.
- **Pitch restore policy:** restoring same-pitch neighbors on lane change SHALL NOT clear inner-span tracking needed for move-back.
- **Shared-release pairing:** classify behavior when mover start shares tick with inner neighbor note-off (A@496 / M0@496).
- **Rematerialize parity:** live edit RAM and `applyEdits` replay MUST agree after overlap round-trip (native + HITL gates).
- **HITL serial gates:** tighten post-pitch and post-home snapshots (existing `_verify_change_length_store_rebuild`, `_verify_long_over_short_pitch_restore`).

## Capabilities

### New Capabilities

- `note-edit-overlap-neighbor-restore`: Neighbor note hide/show/restore semantics when a lengthened moving note moves over inner fixture neighbors, changes pitch, and returns without fader-1 reselect.

### Modified Capabilities

- *(none)* — no delta to archived `timeline-epochs` / `multi-loop-slots` storage contracts.

## Impact

- **Firmware:** `NoteMovementUtils.cpp`, `NoteEditManager.cpp` (pitch restore block), `EditManager::MovingNoteIdentity`, `EditApply.cpp` (rematerialize replay), possibly `Loop.cpp` session store reads.
- **Tests:** `test/test_edit_apply` (synthetic lengthen + move + pitch + move-back); HITL edit baseline verifiers.
- **Related:** `note-move-pitch-overlap-flaky` (shared overlap owner — implement together or sequence after snapshot contract); `m8-edit` (NoteEditSession baseline).
- **Brownfield:** `docs/Guides/MOVE_NOTE_LOGIC.md`, `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`.
- **Vocabulary:** `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc` — **overlap note**, **hidden** / **shortened** / **visible**.

## Non-Goals

- Rewriting Record / Overdub capture or Take chunk layout.
- Full M8 NoteEditSession migration in this change alone.
- Display / OLED refresh lag (`edit-record-display-length-mode`).
- Insert/reorder scenario failures unless same root cause is proven.

## Open Decisions (TBD)

- **Baseline source:** session-open snapshot of materialized loop vs read-only **Record Take** view vs clone at first overlap — pick one owner in design.
- **Pitch restore:** keep P0 visible after pitch (current) vs stay hidden until mover uncovers — product choice; HITL expects visible restore today.
- **Verifier `original_end=1535`:** confirm whether peak tracker is valid AC or false positive before gating on it.
