# 3.15a–c — one-reader oracle audit (read-only)

**Change:** `windowed-playback-generation` · **Tasks:** 3.15a (oracle freeze), 3.15b (one owner/lease), 3.15c (one
edge reader) · **Date:** 2026-10-21 · **Nature:** audit only. No firmware, no test, no checkbox.

Method: a route counts as **covered** only when a fixture drives the **production edge** (`Track::playMidiEvents` or
`TrackManager::updateAllTracks`) and asserts an independent ordered MIDI/ledger expectation. Suites that link the edge today:
`test_windowed_preview_integration`, `test_runtime_working_set_ownership`, `test_shared_capture_finalization`,
`test_rolling_record_retirement`, `test_playback_read_witness`, `test_note_edit_entry_boundary`.

## 3.15a — route by route

| Route (task text) | Status | Where |
|---|---|---|
| short loops | **covered** | dense whole-loop oracle (new, default gate), `test_3_7a_i3c_a_…straddles_its_wrap` |
| long loops | **covered** | dense 512-bar control (new), `test_3_15d_slice3_short_and_long…` |
| empty intervals | **covered** | `test_3_15d_an_empty_effective_interval_is_a_complete_silent_preview`; RWO `…empty_interval_is_ready` |
| same-tick density | **covered** | paged fixtures (multi-event ticks asserted non-vacuous) |
| first Record bar | **UNKNOWN** | E2 flagged fixtures drive Capture `ready`/reader, but the live first-bar `current` is named open in 3.15b's own notes |
| stop return | **UNKNOWN** | E2 frozen-Record bar-two reader exists (flagged); stop→committed handoff at the edge not shown |
| live overdub / wrap | **UNKNOWN** | `test_rolling_record_retirement` and RWO run the edge, but no fixture asserts the overdub overlay's ordered stream through the reader |
| NOTE_EDIT open/change/session undo/exit | **covered** | preview suite (2.12 closeout table) |
| layered/jam slot playback | **UNKNOWN** | no `playMidiEvents` fixture with a layered/jam slot found |
| slot switch | **covered** | `test_3_15d_slice2_a_slot_switch_yields_the_departing_slots_preview` |
| global undo/redo (audible) | **UNKNOWN** | 3.14's evidence is lane-level; no edge-order fixture located in this audit |
| clock-source / seek | **UNKNOWN** | `test_playback_read_witness` exercises the edge but not a clock-source transition |
| SD-restored legacy density | **partial** | the ring path is covered by the dense oracle; the **restore** acceptance route is not driven |

## 3.15b — source preparation through one owner

Its own notes list what is open, and this audit confirms none of it is closed: live first-Record `current`, future-demand
release/cancel, one-bar handoff (`test_one_bar_short_loop_does_not_bypass_the_declared_owner_deadline` shows the declared
contract cannot admit an empty replacement before the next one-bar wrap), the affected-tick successor, and the composed
service budget. **Covered:** Capture token/origin identity, the frozen Record source, ready→current adoption, and
publication — all at lane level with the reader in the loop.

## 3.15c — one completed-window reader at the edge

**Covered:** the short-loop two-wrap baseline characterization and the rolling selector's short-loop exclusion.
**Open:** the owner-turn/adoption/refusal fixture, the removal-of-new-reader RED proof, and the one-bar cutover — all three
are the task's own named gaps, and the merged fallback is still a live selection for short loops (3.15f's route item).

## Verdict

**No checkbox is earned by this audit.** 3.15a has six of twelve routes unproven at the edge; 3.15b and 3.15c each have
open items that their own notes already name. What the audit does provide is the split:

- **Cheapest route to 3.15a:** live overdub/wrap at the edge, and layered/jam — both are emission-order claims with existing
  harnesses, and both are needed by 3.15e's overdub clause anyway.
- **Cheapest route to 3.15c:** the removal-of-new-reader RED, which the cutover needs regardless of 3.15f's route decision.
- **Blocked elsewhere:** one-bar handoff and the composed service budget belong to 3.10/6.3/6.5; the restored-density
  acceptance route belongs to 5.2.

Recorded so the next slice starts from the shortest path rather than re-reading the histories.
