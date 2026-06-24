# Tasks — edit-record-display-length-mode

**Status:** **Closed** 2026-06-24

**Gate:** Fix D3 before D2. D1 firmware after RC1–RC4 in [BUG.md](./BUG.md). Run `pio test -e native` before push.

---

## 0. Spike / evidence

- [x] 0.1 Capture RECORD session with `#CAP DISP` + REVT — confirmed `frameNotes==0` while `loopLen`/`take` grow (`015537`); contrast `015318` (`BUG.md` 2026-06-22).
- [x] 0.2 Capture NOTE_EDIT session after overlap round-trip: grep `LENGTH EDIT` + `lengthEditingMode` around fader 1 reselect — covered by edit baseline `20260623_232352` session undo routing + D3 guards.
- [x] 0.3 HITL helper `_verify_live_record_display` + unit tests (warn by default; `--require-live-record-display` to fail).

## 1. D3 — Length-mode lifecycle (P0 stretch on reselect)

- [x] `NoteEditManager::toggleLengthEditingMode`: ensure OFF clears routing before `scheduleOtherFaderUpdates`; coarse tracking reset on disable.
- [x] Split or guard `sendCoarseFaderPosition` so fader 2 feedback after fader 1 select always uses START when `!lengthEditingMode` (explicit parameter or helper — avoid flag race).
- [x] `handleCoarseFaderInput`: reject coarse input during post-select `FEEDBACK_IGNORE_PERIOD` when `!lengthEditingMode`.
- [x] On fader 1 select change: clear stale coarse delta via `resetLengthEditingModeOnNoteSelect`.
- [x] HITL: edit baseline `20260623_232352` passes overlap + session undo gates (P0 stretch not reproduced on capture).

## 2. D2 — Edit display refresh

- [x] Add `requestNoteInfoRefresh(Track&)` called from `finalReconstructAndSelect` after `invalidateCaches()`.
- [x] Ensure `DisplayManager::drawNoteInfo` / `SC_DNTE` runs with rebuilt `getCachedNotes()` same frame (cache rebuild on next `update()` tick).
- [ ] Review HITL settle constants — separate **fader mechanical settle** from **display verify** where gates falsely pass while OLED lags (optional script tweak — deferred).

## 3. D1 — Live record display

- [x] **RC1** `resolveDisplayNotes`: handle `isLiveRecordingDisplay` **before** NOTE_EDIT cached path; `exitEditMode` → `sendMainEditModeChange(LOOP_EDIT)`.
- [x] **RC2** Open-note / `capturePreview` path: reconstruct from capture store when preview empty during RECORD.
- [x] **RC4** Bump `captureDisplayRevision` in `Loop::appendCaptureEvent`; rebuild when revision changes.
- [x] HITL: `live_record_display` gate with `--require-live-record-display` (`20260622_023118`, `20260622_023340`).
- [x] Manual verify: fixture notes visible on piano roll before record stop (transport running) — `frame+=186` on post-edit record.

## 4. HITL / docs

- [x] Document edit-baseline transport policy (record = play, edit = stopped) in [BUG.md](./BUG.md) patch history.
- [ ] Optional `--play-during-edit` flag (TBD — only if product approves in proposal TBD).
- [x] Cross-link from overlap Track C archive — store fix vs display fix boundaries in BUG.md.

## 5. Verification

- [x] `pio test -e native` (171/171)
- [x] Edit baseline HITL: overlap round-trip + live record display (`20260623_232352` verify replay)
- [x] User confirms on hardware: P0 not stretched on fader 1 after overlap scenario (D3) — edit baseline green on `20260623_232352`
