# S7.5b.2 — NOTE_EDIT committed-base playback (G2 execution packet)

## Identity and scope

- **Task:** `runtime-working-set-ownership` S7.5b.2 — NOTE_EDIT preview preserves committed playback.
- **Owner/path:** `Track`'s existing playback interval representation and `EditManager` / `NoteEditCurrentState` session overlay. No new owner, scheduler, queue, canonical model, or whole-loop materialization.
- **Evidence/source:** newest NOTE_EDIT trace `captures/s64-37c-01/host_midi_hitl_long_loop_display_serial_20261006_224312.log`: current root, `visual_notes=368`, `session_events=0`; production builder's `noteEditPreview` branch fully replaces committed `mergedEvents` with `sessionMidiEvents()` (`TrackPlaybackWindowBuild.cpp`). DEC-055 — NOTE_EDIT working set is overlay delta on opened LoopSnapshot; DEC-029 — NoteEditCurrentState owns editable state.
- **Non-goals:** changing queued slot/start behavior, changing note-edit geometry/undo semantics, materializing a second whole-loop MIDI vector, claiming physical device timing from native ordering.

## Oracle before production edit

Use current-root long-loop content with at least two committed notes in the same requested playback interval; include one unaffected note and one selected/edited identity.

1. **Pre-state:** track is PLAYING; current LoopSnapshot and production playback interval contain both note pairs. Open NOTE_EDIT through `EditManager::openNoteEditSession`; assert active session, current-root open, empty session MIDI delta, and unchanged committed playback identity/content.
2. **No-edit playback:** drive the production interval builder and production MIDI send edge through the due ticks. Assert exact On/Off output for both notes; NOTE_EDIT entry produces no broad NoteOff and no silent/empty replacement.
3. **Partial edit:** drive `NoteEditCurrentState`/session-store projection for one changed identity through the existing owner state. Rebuild the requested interval. Assert the changed note uses its edited geometry, an unaffected committed note still emits at exact ticks, and output equals an independently composed committed-interval-plus-edit oracle.
4. **Removal:** mark the same owner row Deleted, then drive the real Track playback entry to the next tick. Assert exactly one immediate-edge NoteOff for that sounding identity and continued output for the untouched note; no retrigger/duplicate closure.
5. **Resource invariant:** no full-loop rematerialization or second whole-loop representation, and no mutation of the published LoopSnapshot/committed source. This packet does not claim a newly proven no-allocation property for the pre-existing playback builder.
6. **Red-proof:** mutation restoring full replacement from `sessionMidiEvents()` must fail the empty-open and partial-edit exact-output assertions.

## Controls and verification

- Start with `test_windowed_preview_integration`, which already links the real `EditManager`, `NoteEditSessionLifecycle`, window builder and TrackManager. Extend/link only the existing Track playback hot path/send seam needed to observe actual production sends; do not replace the asserted production path with a success stub.
- If that harness cannot link the production send edge without coupling unrelated owner families, record the measured link barrier and move the fixture into the existing suite that owns that edge; do not weaken to a display/cache assertion.
- Focused preview suite, full `pio test -e native`, default `teensy41-capture-serial` build and strict OpenSpec validation. No upload without explicit user confirmation.

## Status

- **Baseline regression:** before the committed-base composition, the new current-root production-builder fixture failed with `Expected 0 to be greater than 0` on the empty NOTE_EDIT delta; the ten existing preview cases passed.
- **Implemented/native:** focused `test_windowed_preview_integration` **11/11**. Empty current-root open emits both committed note pairs through `Track::playMidiEvents` and `MidiHandler`; a one-note span edit replaces only that identity while the other pair emits unchanged; deleting the edited sounding emits exactly one identity-specific NoteOff on the immediate handler edge while the unrelated note remains represented. Restoring the full-replacement branch reproduces the empty-open failure. `test_runtime_working_set_ownership` 177/177 also passes with the real playback path omitted in its intentional stub configuration.
- **Broad/build:** full native **2,858 / 2,851 succeeded / 7 skipped / 0 failed**; both capture builds SUCCESS through RAM1; strict OpenSpec validation and `git diff --check` pass. One focused review found no concrete defects.
- **Device attempt (2026-10-07): BLOCKED before NOTE_EDIT.** Same authorized capture run failed in fixture setup: two clear attempts on Track 5/slot 1 were refused by `waiting_commit_gate` and reported slot untouched; no record or NOTE_EDIT transition occurred. Capture: [`session_20261007_000314.log`](../../../../../captures/s75b-device-20261007/session_20261007_000314.log); metadata pins git `339c94bfffd00d97166e15831f452759fedca783`, firmware SHA-256 `b8f63af3a1bbab57d07ec015a207c1e23d886db3f0ef22f08892d587feafcdbd`. This is not a device playback result; hardware confirmation remains open.
