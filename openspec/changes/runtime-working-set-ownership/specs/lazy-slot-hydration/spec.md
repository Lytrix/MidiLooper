## MODIFIED Requirements

### Requirement: The blocking persistence drain preserves and reports the restore it waits on

**Delivered scope of this requirement.** The blocking deferred-save drain SHALL service and advance the
restore that holds its SD session where the applicable gate admits, using the existing selection and
admission, within a declared per-iteration budget, and SHALL NOT introduce a second loader, a second
queue, or a parallel admission policy. The drain SHALL NOT cancel, supersede, or discard the restore in
order to complete; where the gate denies, it SHALL preserve the request and report the **named** wait.

**Verification boundary.** Restore advancement was delivered natively by `runtime-working-set-ownership`
S2b.7 — drain-side restore service. Complete commit/cleanup bounding remains owned by
`resumable-midi-boundary-playback-maintenance` task3.3 — bounded commit/cleanup; this requirement does
not authorize bypassing that gate. Device confirmation is tracked separately by the runtime change's
drain-service acceptance evidence and is not implied by the native implementation or this wording correction.

The drain SHALL derive its admission from the **same actual runtime eligibility** the normal load path
uses, evaluated across all tracks — it SHALL NOT assume that a CLEAR leaves the runtime non-timing-critical,
because CLEAR stops only the selected track. Where that eligibility denies commit or cleanup, the drain
SHALL preserve the request and report the named wait; it SHALL NOT bypass the gate to make CLEAR succeed.

Restore work SHALL be accounted for in the drain's budget and progress detection: the drain's step budget
SHALL include active and parked restore work, and restore progress SHALL count as progress so that a drain
advancing a large restore is not misreported as stuck or as exceeding its budget. While draining, the drain
SHALL admit at most the bounded fresh queue work it needs to clear the save gate. The existing post-load
commit holdoff SHALL be honored and reported by name.

While the drain owns the thread, the normal deferred-load service path is unavailable; the requirement is
therefore that the drain itself makes the restore progress. If the restore reaches a state the drain must
not break — capture active on that slot, or a denying commit gate — the drain SHALL report that named
reason instead of spinning its stuck budget and aborting.

#### Scenario: A blocking drain completes without cancelling the restore

- **WHEN** a deferred save is owed, a restore for `(track, slot, requestGeneration, setEpoch)` has started
  and is unresolved at the CLEAR edge, and another track is audibly playing
- **THEN** the drain services and advances the restore through the shared selection and bounded service
  path wherever runtime eligibility admits
- **AND** the owed save completes when the blocking SD ownership is released, or the drain returns the
  restore's named wait if it reaches a gate the drain must not break
- **AND** the restore request remains tracked with its identity and named wait, and was not cancelled,
  superseded, or discarded to achieve that
- **AND** a denied commit or cleanup remains denied while another track is timing-critical

#### Scenario: A parked job is serviced, not only an active one

- **WHEN** the restore that holds the save gate is a **parked** job instance rather than the active one
- **THEN** the drain services the parked instance and the wait clears

#### Scenario: Another track playing preserves the commit wait

- **WHEN** CLEAR is pressed on one track while another track is still audibly playing or recording
- **AND** a restore has commit or cleanup pending
- **THEN** the drain does not admit the unbounded commit
- **AND** the request stays tracked and the drain reports the named commit-gate wait
- **AND** the selected track's slot is still reset only through the existing clear path

#### Scenario: Restore progress is not misreported as stuck or over budget

- **WHEN** a small deferred save is owed behind a restore whose advancement takes multiple drain slices
- **THEN** the drain's budget accounts for the restore work
- **AND** restore progress counts as drain progress
- **AND** the drain completes rather than reporting a stuck state or exceeding its step budget

#### Scenario: The drain does not become an unbounded background fill

- **WHEN** the drain services a restore while other payloads are queued
- **THEN** it admits at most the bounded fresh queue work it needs to clear the save gate
- **AND** it does not drain the whole restore queue as a side effect

#### Scenario: A post-load holdoff is reported by name

- **WHEN** the drain cannot advance because the post-load commit holdoff is in effect
- **THEN** it reports the holdoff as its named reason

#### Scenario: A wait the drain must not break is named

- **WHEN** the restore cannot progress because a track on that slot is recording
- **THEN** the drain reports the request's named wait reason
- **AND** it does not report a generic stuck state and does not spin its full stuck budget

#### Scenario: The drain does not cancel the restore

- **WHEN** the drain completes with a restore outstanding
- **THEN** the restore request is still tracked with its identity and state
- **AND** it was not discarded to unblock the save
