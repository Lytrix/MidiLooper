# Undo publication and bounded reclamation reassessment

2026-09-14. **Accepted by the user: “accept recommended solution”. DEC-063.** Task 2.3 and all remaining gates stay open.
Approval authorizes implementation of the recommended transaction and latest-request
coalescing rule. Implementation, native validation and HITL remain separate gates.

## Reason for reassessment

The next global undo publication step cannot simply swap in a prepared stack and
retain everything it removed while claiming the same memory-feedback result.
`trimGlobalUndoStackForMemory` rechecks pressure after each oldest-entry
destruction. Last-reference release can clear pressure and change the surviving
undo prefix. `dropRedoBranch` also destroys payloads and invokes pass reclamation.

Native test `test_retained_undo_root_changes_feedback_based_trim_survivors`
executes the actual trim routine with a controlled heap-pressure signal. Starting
with 11 applied entries, destruction of the oldest root clears pressure:

- Immediate root release trims one entry and retains IDs 2–11 (10 entries).
- Retaining that root keeps pressure high, trims three and retains IDs 4–11 (8 entries).

This is a controlled lifetime/pressure counterexample, not a measurement of actual
Teensy allocation sizes. Focused `test_pool_budget`: **18/18** passes
(`/tmp/resumable-undo-trim-reference-focused.log`). Retaining roots can cause more
history to be discarded; a fixed trim count or latched-pressure reference would
need explicit approval, rather than being presented as identical behavior.

## Authority and historical review

Searched `docs/DECISION_LOG.md`, current OpenSpec artifacts, archived
`undo-memory-trim` / `pass-reclaim` requirements and relevant undo handoffs for
undo ownership, pressure, trim, snapshots, release and reclamation.

- DEC-038 retains one global OverdubPassAdded entry per session.
- DEC-024 and `ARCHITECTURE_RULES.md` retain TrackUndo's existing global undo responsibility.
- `openspec/specs/undo-memory-trim/spec.md` requires feedback-based oldest-entry
  trimming while pressure remains, redo removal on new pass, and reclaim after mutation.
- `openspec/specs/pass-reclaim/spec.md` requires releasing removed ClearSlot
  snapshot payloads and reclaiming unreferenced disabled passes.
- DEC-062 approves 6240 ranked grains only; it does not change undo retention,
  authorize a new undo routing rule, or establish reclamation latency.

Existing extension points: TrackUndo / GlobalUndoStack, PassReclaim and
TrackManager's reclaim traversal, and LoopSnapshot's existing resource allocation
and shared-root lifetime code. Reuse these owners; introduce no new Manager,
canonical undo kind or persistent schema.

## Accepted evolution

Preserve the feedback-based trim rule and add the missing bounded transaction:

1. During prewarm/admission, reserve the private entry, stack bookkeeping and
   retirement continuation capacity before accepting capture that needs them.
   Include all work in explicit stop reservations, separately from ranked grains.
2. Once semantic preparation and source checks are complete, protect the affected
   Track's undo transaction from competing mutations. Keep intermediate stack
   mutation invisible to normal readers; defer snapshot-after attachments into
   reserved storage when necessary. Audit every global-stack reader/writer.
3. Process removed redo entries and oldest-entry retirement in bounded units.
   Extend the existing snapshot resource owner with resumable release of final
   references, including membership/page and persisted-snapshot/chunk payloads.
   Recheck memory pressure only after that entry's scheduled release completes;
   do not replace physical feedback with a preselected trim count.
4. Preserve one final canonical/audible/undo publication boundary. Defer disabled
   canonical-pass reclamation until it cannot invalidate the prepared capture/
   history proof, while accounting for its complete bounded continuation.
5. Finish through the latched destination; transport Stop still takes precedence,
   preserves accepted capture and cannot allow a return to PLAYING.

The visibility/transaction design must be proved in native tests before active
selection. In particular, physical retirement must not destroy state needed to
recover an interrupted transaction. This review does not claim that proof yet.

## Accepted user-visible command rule

Accepted rule: undo/redo received while this Track's protected stop transaction is
in progress is deferred until it finishes, rather than operating on an incomplete
stack. Reserve one bounded pending command slot per affected Track; each additional
Undo or Redo replaces the pending request. Transport
Stop remains immediate. No command should silently mutate the partially prepared
undo state. This is a new routing rule for OVERDUB_STOPPING, beyond its already
specified capture-input freeze, and is explicitly approved by the user.

Accepted coalescing rule: retain the most recent pending Undo or Redo request
for the affected Track, execute it once after finalization under the destination's
normal command policy, and report deferral. Other Tracks retain their own commands.
This explicitly permits coalescing repeated presses.

## Alternatives

- Explicitly adopt a deterministic pressure snapshot for trimming and retain
  retired resources until later cleanup. This simplifies planning but can retain
  fewer undo entries than the current feedback reference. Not recommended without
  a separate semantic decision and updated undo-memory-trim delta.
- Exclude redo/pressure cases from the active path. This restricts capture
  eligibility and does not complete the general requested behavior; not adopted.
- Release each UndoEntry synchronously inside the atomic handoff. Rejected as an
  implementation route: snapshot graphs, payload destruction and reclaim are
  content-dependent and have no declared active grain bound.

## Formal trigger and approval

`docs/ARCHITECTURE_REASSESSMENT.md` names **Undo semantics change** and **undo
routing** as review/approval triggers. The proposed protected transaction changes
routing during OVERDUB_STOPPING and extends resource-lifetime control from
synchronous shared-pointer destruction into scheduled reclamation. The OpenSpec
apply skill also requires surfacing added scope/behavior exceptions before adopting
them. Earlier pairing, per-pass and ranked-budget approvals do not cover this rule.

**Approval received: YES**, on 2026-09-14 for the recommended protected
transaction and latest pending Undo/Redo coalescing behavior. DEC-063 and the
current MIDI-boundary delta record the accepted contract before firmware changes.
Existing feedback-based history retention remains the reference.

## Migration and verification

No SD migration or new UndoEntry wire shape. Affected existing owners: TrackUndo,
Track's stop state, LoopSnapshot resource lifetime, PassReclaim and TrackManager
reclamation. Native gates: mixed snapshot ownership, last-reference pressure
relief, redo clearing, oldest-entry survivor parity, source/cursor/slot races,
competing commands, stale snapshot attachments, cancellation/recovery, retained
accepted Offs, and per-quantum allocation/release bounds. Rebuild firmware and
check RAM1 after each linked integration group. HITL follows once the complete
transaction and stop path are runnable; it is not the next gate yet.
