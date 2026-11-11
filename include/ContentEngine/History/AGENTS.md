# History — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § History in the decision log.

## Owns
- `SlotUndoHistory`: per-slot undo / redo over manifests, sessions (overdub, NOTE_EDIT), the hot table and its
  eviction. `ColdTier`: the states below a slot's oldest hot state, rows on the card only. `HistoryCost`: the
  on-request cost walk.

## Invariants
- Undo / redo are O(1) pointer moves. A slot keeps at most `kSlotDepth` (99) states, hot and cold (DEC-254).
- A full hot table demotes a slot's oldest hot state to cold when its runs are on the card — kept, not forgotten.
  Rows pressure demotes the same way (`demoteOldest`).
- An undo past the hot floor requests a fetch; the switch follows once the state is in.
- A session's start state is protected from eviction; `endSession` folds a session into one step.
- One overdub session and one NOTE_EDIT session may be open at once, on different slots.
- A redo chain has no holes: eviction of a redo state drops the whole branch.
- Every dropped state releases its manifest reference, so its runs reach reclaim. No allocation.
- A load forgets each slot's whole history (DEC-259); refused while a session is open on the slot.

## Decisions
- DEC-038 (overdub wrap commit) · DEC-254 (per-slot undo, cold steps on the card) · DEC-259 (whole-set load)
