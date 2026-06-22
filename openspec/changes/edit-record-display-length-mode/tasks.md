# Tasks — edit-record-display-length-mode

**Status:** **Active** (reopened 2026-06-22) — see [PARKED.md](./PARKED.md)

**Gate:** Fix D3 before D2. D1 firmware after RC1–RC4 in [BUG.md](./BUG.md). Run `pio test -e native` before push.

---

## 0. Spike / evidence

- [x] 0.1 Capture RECORD session with `#CAP DISP` + REVT — confirmed `frameNotes==0` while `loopLen`/`take` grow (`015537`); contrast `015318` (`BUG.md` 2026-06-22).
- [ ] 0.2 Capture NOTE_EDIT session after overlap round-trip: grep `LENGTH EDIT` + `lengthEditingMode` around fader 1 reselect.
- [x] 0.3 HITL helper `_verify_live_record_display` + unit tests (warn by default; `--require-live-record-display` to fail).

## 1. D3 — Length-mode lifecycle (P0 stretch on reselect)

- [ ] `NoteEditManager::toggleLengthEditingMode`: ensure OFF clears routing before `scheduleOtherFaderUpdates`; debug assert `!lengthEditingMode` in `sendCoarseFaderPosition` when mode disabled.
- [ ] Split or guard `sendCoarseFaderPosition` so fader 2 feedback after fader 1 select always uses START when `!lengthEditingMode` (explicit parameter or helper — avoid flag race).
- [ ] `handleCoarseFaderMovement`: reject LENGTH EDIT branch when `!lengthEditingMode` even if pitchbend matches prior END feedback.
- [ ] On fader 1 select change: clear stale coarse delta / ignore first coarse input until position routing confirmed (if spike shows feedback-triggered LENGTH EDIT).
- [ ] HITL: add `require_p0_record_gate` after fader 1 reselect on M0 post-overlap (if not already); require no `LENGTH EDIT` with P0 start tick in window.

## 2. D2 — Edit display refresh

- [ ] Add `requestNoteInfoRefresh(Track&)` (or equivalent) called from `finalReconstructAndSelect` and length/pitch commit paths after `invalidateCaches()`.
- [ ] Ensure `DisplayManager::drawNoteInfo` / `SC_DNTE` runs with rebuilt `getCachedNotes()` same frame.
- [ ] Review HITL settle constants — separate **fader mechanical settle** from **display verify** where gates falsely pass while OLED lags (optional script tweak).

## 3. D1 — Live record display

- [x] **RC1** `resolveDisplayNotes`: handle `isLiveRecordingDisplay` **before** NOTE_EDIT cached path; `exitEditMode` → `sendMainEditModeChange(LOOP_EDIT)`.
- [ ] **RC2** Open-note / `capturePreview` path: ensure growing capture shows in-flight note-ons (fix `findOpenNoteOns` vs growing length or always merge `capturePreview.notes`).
- [ ] **RC4** Bump `captureDisplayRevision` in `Loop::appendCaptureEvent`; rebuild when `capturePreview.revision` changes.
- [x] HITL: `live_record_display` gate with `--require-live-record-display` (`20260622_023118`, `20260622_023340`).
- [x] Manual verify: fixture notes visible on piano roll before record stop (transport running) — `frame+=186` on post-edit record.

## 4. HITL / docs

- [ ] Document edit-baseline transport policy (record = play, edit = stopped) in script header + `BUG.md`.
- [ ] Optional `--play-during-edit` flag (TBD — only if product approves in proposal TBD).
- [ ] Cross-link from `note-move-pitch-overlap-flaky/BUG.md` — store fix vs display fix boundaries.

## 5. Verification

- [x] `pio test -e native` (137/137 after RC1)
- [x] Edit baseline HITL: overlap round-trip + live record display (`20260622_023340`)
- [ ] User confirms on hardware: P0 not stretched on fader 1 after overlap scenario (D3)
