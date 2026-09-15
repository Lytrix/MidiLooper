# Companion pairing: accepted reference-order correction

2026-09-13. The user explicitly accepted the deterministic lane-run rule.
Task 2.3 and the listed validation/HITL gates remain open. The initial review
changed no repository files or firmware; the subsequent implementation applies
the approved correction to the reference resolver. No firmware was uploaded.

## Finding

The former `src/EditManager/EditApply.cpp:152` comparator ordered a channel/pitch lane by event kind
when ticks are equal, and by original vector index otherwise. This comparator
is not a strict weak ordering. For A = index 0 On@96, B = index 1 On@0, and
C = index 2 Off@96, it reports A < B, B < C, and C < A.

Consequently `std::stable_sort` is being used outside its comparator contract.
A bounded replacement cannot promise byte-identical results against this
reference for every accepted input; preserving the comparator does not preserve
its observed output across sorting algorithms or library implementations.

## Reproducer

The checked-in standalone `companion_pair_order_repro.cpp` enumerates short
single-channel/single-pitch lanes. Build with C++17 and run it. It compares the
former comparator under the host standard library's stable sort and a bounded
bottom-up merge, then applies the reference LIFO pairing walk.

Observed host output:

```
target 0
events 0:On@96 1:On@0 2:Off@96 3:Off@96 4:Off@0
std stable_sort 0 1 2 3 4 pair 3
bounded merge 2 3 0 4 1 pair 4
```

The target On is paired with Off@96 by the former host reference, but Off@0 by
the merge. This is an algorithm-level reproducer, not a claim about a captured
device trace. The comparator cycle itself is independent of platform.

## Accepted rule and applied patch

Runs are contiguous within the filtered channel/pitch lane of note events.
Unrelated channels, pitches, and non-note events do not split a lane run.
Equal ticks separated by a different tick in that lane remain separate runs.
Each run processes Offs before Ons, preserving order within each kind.
Existing LIFO pairing and wrap fallback remain unchanged.

For the counterexample above, target 0 pairs with Off index 3 at tick 96.
This is an explicit semantic correction: parity is measured against this
revised deterministic reference, not the former invalid comparator.

`COMPANION-PAIRING-PROPOSAL.patch` is retained as the accepted patch and has now
been applied to `findNoteOffForOnIndex`. The design and delta spec state the
contract; DEC-059 records the user's authorization. Playback's global phase
ordering is unchanged.

`RuntimeNotePairLookup` implements the same lookup in bounded grains using
prewarmed lane, LIFO-stack, and paired-Off storage. Regression tests cover the
counterexample, filtered-lane runs, stable per-kind ordering, tagged and
unique/ambiguous wrap fallback, 500 generated differential fixtures, and the
2,048-event capacity boundary. This is the NoteOff lookup component, not the
complete Delete/Length companion resolver or production handoff.

Task 2.3 remains open. Full companion overlay resolution, canonical/audible
handoff, coalescing, recovery, cleanup, and the listed validation/HITL gates
remain required. See BRIDGE-PROGRESS.md for current test evidence.
