# Tasks — edit-record-display-length-mode

**Gate:** Spike D1 serial before large DisplayManager refactor. Fix D3 before D2 (user-visible corruption). Run `pio test -e native` before push; HITL edit baseline after D3.

---

## 0. Spike / evidence

- [ ] Capture one RECORD session with `#CAP DISP` + REVT: confirm whether `frameNotes==0` mid-record while loop length grows (document in BUG.md patch history).
- [ ] Capture NOTE_EDIT session after overlap round-trip: grep serial for `LENGTH EDIT` + `lengthEditingMode` / `Length editing mode DISABLED` around fader 1 reselect; note if P0 stretch reproduces on demand.

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

- [ ] Trace `captureDisplayRevision` / `capturePreview.revision` bump on capture insert; fix missing bump if preview stale mid-record.
- [ ] Fix `resolveDisplayNotes` live branch to rebuild when capture revision changes.
- [ ] Manual verify: fixture notes visible on piano roll before record stop (transport running).
- [ ] Manual verify: stopped-transport growing capture still shows notes (if product supports).

## 4. HITL / docs

- [ ] Document edit-baseline transport policy (record = play, edit = stopped) in script header + `BUG.md`.
- [ ] Optional `--play-during-edit` flag (TBD — only if product approves in proposal TBD).
- [ ] Cross-link from `note-move-pitch-overlap-flaky/BUG.md` — store fix vs display fix boundaries.

## 5. Verification

- [ ] `pio test -e native`
- [ ] Edit baseline HITL: overlap round-trip + P0 gates + new post-reselect P0 gate
- [ ] User confirms on hardware: record shows notes during capture; P0 not stretched on fader 1 after overlap scenario
