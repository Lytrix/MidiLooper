# Companion application scope: approved canonical reference

2026-09-13. Task 2.3 remains open. This investigation changes tests and
operational evidence only; the user has now endorsed the proposed canonical per-pass reference (DEC-060).

## Confirmed disagreement

`LoopPasses::materializeToEventVector` (both vector overloads) applies active
edit rows independently to each canonical pass and then merges the results.
`Loop::tryAdmitLastOverdubSealIntoMergedEvents` appends the newly sealed pass
to the merged playback stream (with duplicate filtering), then applies its
companion sequence to that combined stream.

The native characterization test
`test_companion_pass_scope_differs_from_merged_capture_scope` confirms:

- Same channel/pitch, record NoteId 1 at [0,10], captured NoteId 2 at [150,160],
  Length target 1 to 250: actual canonical pass materialization returns four
  events and preserves NoteId 2; append-then-apply returns two events and
  deletes NoteId 2 through overlap resolution. This is a general Length input;
  reachability from the production Shorten producer has not been established.
- Record NoteId 1 at [0,10], captured NoteId 1 at [150,160], Delete target 1:
  actual canonical pass materialization returns no events; append-then-apply
  retains the captured pair. This case needs no Length or overlap geometry and
  concerns the repeated identities already covered by the maintenance scope.

The merged-side fixture executes the same append-before-edit sequence and the
actual `applyNoteEditPassSequence`; it does not invoke the entire incremental
seal-admission method. Its events are distinct for the admission duplicate
predicate, so no duplicate filtering changes either fixture. These are native
input counterexamples, not a claim of an observed device trace or proof of all
capture-producer reachability conditions.

Validation: `pio test -e native -f test_edit_apply` passes **98/98**, including
both cases (`/tmp/resumable-companion-scope.log`). The prior full native/build
results remain historical; no firmware source changed in this investigation.
DEC-059's lane-run pairing contract remains in force in both reference paths.
It does not resolve edit scope across passes.

## Approved reference and implementation consequence

Use canonical per-pass application as the combined-capture reference:
apply the frozen companion sequence independently to each participating pass,
then merge the resulting events with canonical stable ordering. Incremental
admission must match that result. This preserves canonical materialization
semantics; choosing flattened application instead would explicitly change them.

Do not merely append frozen capture after applying companions to the flattened
published base: that loses pass scope within the base and misses companion
changes to captured instances sharing a NoteId. Conversely, concatenating the
base and capture before applying rows selects the other, conflicting reference.

The bounded apply/map/ranked compiler components remain valid for the exact
input stream supplied to them. Their flattened differential evidence does not
prove whole-pass canonical equivalence. The preparation owner needs immutable
pass membership/order plus the mapping to published base provenance, including
capture provenance and repeated identities. Its aggregate capacity, stop grains,
and ownership transfer must be reassessed before production integration.

The user explicitly endorsed canonical per-pass scope. DEC-060 and the delta
spec/design record that decision. Implement immutable pass provenance and bounded
per-pass application/merge next; task 2.3 and validation/HITL gates remain open.
