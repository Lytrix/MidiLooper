# E4.6d — undo into a cold state: the fetch (execution packet)

Authority: DEC-254 (approved 2026-11-11; owner: "a second undo press should skip the current loading undo and restart
the newly requested undo"), OpenSpec `fixed-memory-engine` E4.6d. After E4.6c (`e46c_cold_states.md`).

## Scope

- **Request.** An undo at a slot's oldest hot state with cold states below asks for a fetch (`ColdSink::requestFetch`)
  instead of stopping. The slot keeps playing its current state.
- **More presses** while it runs move its target one step deeper each; only the final target is switched to (the
  states in between are fetched on the way and kept hot, not played).
- **The fetch** (persistence grains, after any save in progress; no save starts meanwhile): for the slot's newest cold
  state, every run already in PSRAM (its record known) is reused; the others are allocated, admitted (rows reserved
  first, refused by name when there is no room) and read from their records — the header checked, then **at most four
  sectors a grain** (the boot's 32 took 31 ms), the rows' CRC at the end. Then a manifest, promoted as the slot's oldest
  hot state (`History::promote`; the table makes room elsewhere, never on the slot), and the cold descriptor dropped.
  Repeat until the target depth.
- **Switch.** The engine applies it like an undo (`SlotTransport::fetched`): the slot's current becomes its oldest hot
  state and the playing tracks follow — only if the slot is still at the state the presses were made on (a redo or a
  commit since: the fetched states stay, nothing switches).
- **Cancelled** (its runs released, nothing kept) when the slot's order changes under it — its oldest hot state or its
  cold states (a clear, a set load, a demotion); counted. A commit meanwhile does not cancel: the fetched states are
  still the ones below; they arrive and stay, and nothing switches (the slot moved).

Non-goals: rows pressure and boot loading hot states only (E4.6e); a screen indication (the UNDO level already shows
the depth; the save dots may later show a fetch).

## Transition oracle (native)

1. Undo at the cold floor fetches the newest cold state and switches to it: heard content = that state's; the UNDO
   level one less; the cold count one less; nothing else forgotten.
2. Three presses: one switch, three states deeper.
3. A damaged run record: the fetch fails by name, the slot stays where it was, its cold states intact.
4. A commit on the slot during the fetch: it arrives, nothing switches; a clear cancels it.
5. Grains: every fetch grain reads at most four sectors (fake card counts).

## Device gate

Undo past the hot floor on a slot with cold steps: the fetch time, the worst persistence grain (≤ ~0.9 ms), no tick
lateness beyond a run without it.

## Evidence (2026-11-11)

- Native: `test_undo_fetches_the_cold_state_below` (switch, depth, three presses one switch, redo back),
  `test_a_damaged_record_refuses_the_fetch` (the rows' CRC), `test_a_change_on_the_slot_cancels_the_fetch` (a commit
  keeps without switching; a clear cancels), `test_a_big_run_is_fetched_in_small_grains` (an 800-row take, ≤ 4 sectors a
  step); mutations (no switch, 32-sector grains, no rows CRC) each caught. Full native 4,559.
- Device `4a3e4f71…`: a take and two overdubs left 4 cold steps on T2 S3; undone to the hot floor and four more presses —
  four fetches, each in place within one serial poll (≤ 0.6 s), 0 / 0 / 1 / 2 sectors read, the worst persistence grain
  337 µs; T2 S3 redone to its top, the test slot undone.
