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

## 3.15c — removal-of-new-reader RED proof (2026-10-21)

The task requires fixtures to **fail when the migrated route is removed**. Mutation performed and restored:

| Step | Command / result |
|---|---|
| Mutation | `PagedEmissionState::admittedForEmission()` forced to `false` (the unified paged reader stops being selectable) |
| Focused RWO suite | **6 failed / 227 succeeded** — `test_3_15e_a_dense_long_interval_emits_its_whole_content_through_the_edge` **Expected 2 500 Was 0**, `test_3_15e_a_dense_whole_loop_interval_emits_its_whole_content_through_the_edge` **Expected ≥5 000 Was 0**, plus four pre-existing paged fixtures |
| Restore | header restored byte-identically; focused suite **233/233** |

So the dense oracle is **new-reader-dependent** in the required direction: remove the reader and the route that serves restored
density emits nothing. That is the RED half of 3.15c's open item for the paged route; the same proof is still owed for the
**window** route (a short/long sparse loop served by `fixedView`/rolling) before 3.15c can be considered covered.

## 3.15c — removal-of-window-route RED proof (2026-10-21)

Second mutation, same technique, on the **NOTE_EDIT window route** (`Track::resolveNoteEditPreviewSourceForTurn` forced to
`return false`):

| Fixture that went RED | Clause it names |
|---|---|
| `test_3_15d_slice2_note_edit_serves_the_preview_interval_at_the_clock_edge` | "the clock edge binds the preview window inside its interval" |
| `test_3_15d_slice2_the_preview_to_merged_seam_carries_a_crossing_note_once` | "the preview serves inside its interval" |
| `test_3_15d_slice2_the_lane_re_covers_the_playhead_across_intervals` | "the re-covered interval serves the next stretch, not a gap" |
| `test_3_15d_slice2_adoption_closes_the_identity_the_session_removed` | "the interval's adoption is the turn's route" |
| `test_3_15d_slice2_the_session_exit_yields_the_preview_frame` | "the session's preview is serving at the clock edge" |
| `test_3_15d_slice2_a_slot_switch_yields_the_departing_slots_preview` | "the departing slot's preview is serving before the switch" |

Restored byte-identically; focused suite back to **29/29**.

**What is proved:** the preview window route is new-reader-dependent, and the mutation is *discriminating* — each failure names the
clause it covers. **What is still owed for 3.15c:** the same RED for the **committed** window route (a sparse short/long loop served
by `fixedView`/rolling with no NOTE_EDIT). That needs its own edge fixture before 3.15c can claim the pair.

## 3.15c — the committed-window fixture needs a root built from the committed passes (2026-10-21)

Attempted the third removal proof (sparse long loop, no NOTE_EDIT, served by the committed window). Two harness facts stopped it,
and both are worth recording because they would mislead any later reader:

1. **A committed long loop with no published root emits nothing at the edge** (measured: 0 events) — the committed lane gathers
   from a published root, so a fixture built only from `commitEmissionLoop` has no window source. This is the same constraint the
   NOTE_EDIT slice fixtures hit, which is why they call `publishCommittedRootForSlot`.
2. **`publishCommittedRootForSlot(loop, bars)` publishes synthetic content** — `bars` notes at `bar * 768`, ids 2000+bar — **not**
   the helper's committed events. Using it to make a "sparse committed" fixture measures the synthetic root instead: with 64 bars it
   emitted 16 events for a fixture expecting 4, and `committedWindowTurns == 0`.

So the sparse committed-window fixture is **not** a production question yet; it needs a harness seam that publishes a root **built
from the committed passes** (the comment names `Loop::storePublishedSnapshotForTest` as the seam). Until that exists, the third
removal proof cannot be taken, and 3.15c's committed-window claim stays open.

**What this does not say:** it is not evidence that the committed window fails to serve sparse content. The probe never reached the
route under test; the synthetic root measured a different content set.
