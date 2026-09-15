# Create history ordering — approved reference correction

2026-09-14. **Approved and applied**, following the user's explicit “yes approve
and continue.” DEC-061 records this separate semantic correction. Task 2.3 and
all outstanding validation/HITL gates remain open.

## Existing reference and evidence

`applyAddNote` in `src/EditManager/EditApply.cpp` appends the Create payload and
calls `std::sort` with only `a.tick < b.tick`. Equal-tick order is not stable.
The bounded canonical sorter preserves input order for equal raw ticks, so
substituting it would change the observed reference result.

`create_history_order_repro.cpp` uses the real `MidiEvent` and `MidiEventVec`
types and reproduces that sort operation. It does not call the complete edit
history helper and does not claim a device MIDI trace. On the current native
compiler, 25 events with ticks `i % 3` and distinct identities produce:

- Legacy tick-zero original indexes: `0,24,6,9,12,15,3,18,21`.
- Stable tick-zero original indexes: `0,3,6,9,12,15,18,21,24`.

The first mismatch is output index 1: original event 24 versus event 3. This is
observable event identity/order, not only a timing or allocation difference.
The example also represents a Create row appending all 25 events to an empty
pass. The target compiler has its own standard-library sort implementation;
the native permutation is not asserted to be the target permutation.

Reproduce from repository root:

```sh
c++ -std=c++17 -DPIO_UNIT_TEST_NATIVE -Iinclude openspec/changes/resumable-midi-boundary-playback-maintenance/create_history_order_repro.cpp -o /tmp/create-history-order-repro
/tmp/create-history-order-repro
```

## Approved explicit contract

For each Create row in a canonical per-pass history sequence:

1. Append the row's payload in its stored order to that pass's current events.
2. Sort the complete resulting pass by ascending raw tick, stably. Equal ticks
   retain their order immediately before this sort: existing events precede
   appended events at equal ticks, and each group preserves its internal order.
3. An empty Create payload still performs this stable whole-pass sort, matching
   the reference's unconditional sorting step.
4. Do not introduce an Off-before-On tie-break into the stored Create result.
   DEC-059's filtered-lane pairing rule and the playback ordering rule remain
   separate. DEC-060's independent per-pass application and canonical merge
   remain unchanged.
5. Preserve Create's present-identity bookkeeping: added nonzero NoteOn IDs
   re-enter the sequence's present-ID set, including an ID deleted by an earlier
   row. Do not implement this by retaining the current prior-Delete-only guard.

This is an explicit semantic correction. Parity must be measured against the
revised reference, not the old unstable permutation. Previously stored histories
that contain Create rows can materialize equal-tick events in a different order;
no persistence bytes or schema need to change for that effect to occur.

## Reviewable patch and remaining work

`CREATE-HISTORY-REFERENCE-PROPOSAL.patch` changes only the Create reference sort
to `std::stable_sort` and adds a regression through `applyNoteEditPassSequence`.
It covers all-created, mixed existing/created, and empty-Create cases using the
25-event example. The patch was checked and then applied after approval. The
patch file remains the historical proposal; subsequent tests and implementation
are recorded in `BRIDGE-PROGRESS.md`.

The decision log/design/delta contract now name the approved correction.
Bounded Create append/order and present-ID reactivation use generation/capacity
guards; no prefix may be published on capacity or unsupported-history failure.
Full history/input gathering and production admission remain incomplete until
integrated and validated.

The alternative is preserving a specifically selected legacy sort algorithm's
equal-tick permutation. That requires selecting the target/reference toolchain
and implementing its bounded equivalent, rather than assuming a stable merge
is equivalent. No such algorithm pinning is approved in the current contract.
