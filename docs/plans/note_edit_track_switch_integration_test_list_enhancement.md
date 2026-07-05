# NOTE_EDIT track switch — integration test backlog

**Date:** 2026-07-05  
**Branch:** `derived-note-overlap-logic`  
**Status:** **Deferred** — session-level native coverage shipped; integration items below are not implemented  
**Related fix:** `EditManager::beforeSelectedTrackChange` + `onTrackChanged` wired from `TrackManager::setSelectedTrack`  
**Evidence:** `captures/session_20260705_213626.log` (track 5 → 6 in NOTE_EDIT: wrong DISP note count until edit mode cycle)

---

## Shipped (unit / session invariant)

| ID | Suite | What it locks |
|----|-------|----------------|
| **NTS-U1** | [`test/test_note_edit_track_switch/`](../../test/test_note_edit_track_switch/) | Departing **NoteEditSession.store** + new **loopLength** → wrong display count; **reopen** rematerialize from new loop restores correct count and **filterSelectableDisplayNotes** parity |

Run: `pio test -e native -f test_note_edit_track_switch`

This does **not** exercise `TrackManager`, `EditManager` globals, `DisplayManager`, or transport.

---

## Deferred integration backlog

Priority order within each tier is top → bottom. Gate each tier on the previous unless noted.

### Tier 1 — Native wiring (stub harness)

**Goal:** Link `EditManager` + `TrackManager::setSelectedTrack` on host with minimal stubs (`clockManager`, `displayManager`, `noteEditManager` outbound no-ops). New suite candidate: `test_note_edit_track_switch_wiring` (or extend harness under `test_support/`).

| ID | Case | Preconditions | Assert |
|----|------|---------------|--------|
| **NTS-I1** | `setSelectedTrack` calls lifecycle hooks | NOTE_EDIT open on track A; stub two tracks with different `loopLengthTicks` and note counts | `beforeSelectedTrackChange` runs before index change; `onTrackChanged` runs after; session rematerialized from track B (flat size / note count = B) |
| **NTS-I2** | Pending geometry committed on depart | NOTE_EDIT on A; moving-note or length edit in progress (non-idle edit state) | `commitAllPendingNoteEditActions` invoked on departing track before index change; no stale `focus` on B |
| **NTS-I3** | Focus sync on depart | `editSession.focus.active` on A | `syncNoteEditFocusLastFromSessionStore(departingTrack)` runs; after switch, focus rebuilt or cleared per `reopenNoteEditSession` |
| **NTS-I4** | Display cache invalidated | NOTE_EDIT active; stub records `invalidateLiveDisplayCache` | Called once from `onTrackChanged` after reopen |
| **NTS-I5** | No-op when NOTE_EDIT inactive | Transport idle; edit session closed | `setSelectedTrack` does not rematerialize session or touch undo stack |
| **NTS-I6** | LOOP_EDIT mode track switch | `EditSessionType::Loop` active | `loopEditManager.onEnterLoopEditSession(newTrack)` — not `reopenNoteEditSession` |

**Harness notes:** Reuse patterns from [`test_note_edit_focus`](../../test/test_note_edit_focus/) (direct `.cpp` includes). Avoid full `Track` pool unless `ensureLoopsAllocated` is stubbed. Prefer two `Loop` fixtures behind lightweight `Track` test doubles.

---

### Tier 2 — Native playback / editMidiEvents coupling

**Goal:** Cover cross-module contracts that caused related regressions on the same branch (playback preview scoping, mid-play reopen).

| ID | Case | Preconditions | Assert |
|----|------|---------------|--------|
| **NTS-I7** | `editAwareMidiEvents` / display path after switch | NOTE_EDIT reopened on B | `editMidiEvents(trackB)` and `sessionMidiEvents()` match B materialization; **not** A flat |
| **NTS-I8** | Playback preview scoped to selected track only | NOTE_EDIT on selected track A; track B playing | `ensurePlaybackWindowBuilt` on B uses loop MIDI, not session preview (regression for `cdfa032`) |
| **NTS-I9** | Mid-play NOTE_EDIT entry reanchor | Transport PLAYING; enter NOTE_EDIT on selected track | Playback continues past beat 2; `reanchorPlaybackIndex` / window revision stable (open bug — test may start as **xfail** until fix lands) |
| **NTS-I10** | NOTE_EDIT + STOPPED → play | NOTE_EDIT open; transport STOPPED; start play on selected track | No crash / hang; audition uses session preview on selected track only |

Tier 2 likely needs `Track` + partial `Track.cpp` or extracted test seams; mark **NTS-I9** / **NTS-I10** blocked until reproduction is stable in capture.

---

### Tier 3 — HITL (serial + MIDI automation)

**Goal:** End-to-end verification with `SESSION_CAPTURE` firmware and [`host_midi_hitl.py`](../../scripts/host_midi_hitl.py). Register new scenarios in [`HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md) when implemented.

| ID | Scenario name (proposed) | Steps | Serial / script gates |
|----|--------------------------|-------|------------------------|
| **NTS-H1** | `note_edit_track_switch_display` | Record 2-bar loop track 5; enter NOTE_EDIT; switch to track 6 (long loop, e.g. 24 bar); **do not** exit edit mode | `DISP` line: note count matches track 6 (`count_ok`); `loopLength` = track 6; no departing-track note stretch (cf. `213626.log`) |
| **NTS-H2** | `note_edit_track_switch_round_trip` | NTS-H1 + switch back to track 5 | Each track’s DISP count / loop length correct without cycling NOTE_EDIT |
| **NTS-H3** | `note_edit_track_switch_with_pending_move` | Start move on track 5; switch to 6 before bracket settle | Departing geometry committed; track 6 display clean; optional `EDIT` / focus serial markers |
| **NTS-H4** | `note_edit_track_switch_while_playing` | PLAYING + NOTE_EDIT on 5 → switch to 6 | Transport keeps running; MO on ch6 only; no beat-2 stop (pairs with **NTS-I9**) |
| **NTS-H5** | `note_edit_track_switch_playback_audition` | NOTE_EDIT on 5, play; switch to 6 while playing | Track 5 audition stops; track 6 loop plays; selected-track session preview when returning to edit on 6 |

**Fixture hints:** Mirror log geometry — track 5 `loopLength=1536`, track 6 `loopLength=18432`; use `--track` / `--midi-channel` aliases from [HITL-Test-Flow.mdc](../../.cursor/rules/HITL-Test-Flow.mdc).

---

## Implementation checklist (when picking up this backlog)

1. [ ] Tier 1 harness: stub globals + `test_note_edit_track_switch_wiring` (**NTS-I1**–**I6**)
2. [ ] Tier 2: playback/display coupling (**NTS-I7**–**I10**); file xfail markers for open bugs
3. [ ] Tier 3: `scripts/hitl/scenarios/note_edit_track_switch_display.py` + verifier (**NTS-H1**)
4. [ ] Extend HITL matrix for **H2**–**H5**; host verifier unit tests under `scripts/test_*_serial_verify.py`
5. [ ] Full gate: `pio test -e native` + manual or CI HITL with `--verify-serial-log`

---

## Out of scope (unless product asks)

- Slot switch inside NOTE_EDIT (loop slot change on same track) — separate from **track** switch
- SD load / set revision overlay during NOTE_EDIT
- Multi-loop jam / arrangement capture (D13)

---

## Cross-links

- OpenSpec backlog: [`openspec/changes/unified-interval-projection/tasks.md`](../../openspec/changes/unified-interval-projection/tasks.md) § TODO (**NTS-***)
- Native session tests: [`test_note_edit_track_switch/test_note_edit_track_switch.cpp`](../../test/test_note_edit_track_switch/test_note_edit_track_switch.cpp)
- HITL catalog: [`docs/Guides/HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md)
- UIP Phase 5 HITL (parent matrix): [`unified_interval_projection_enhancement.md`](unified_interval_projection_enhancement.md)
