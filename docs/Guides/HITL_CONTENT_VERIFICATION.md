# HITL content verification

This guide defines what the dense **record + overdub → content dump → playback
inventory** procedure proves about a running device, what each artifact means,
and which differences are expected. The executable recipe is the
[`hitl-content-verification`](../../.agents/skills/hitl-content-verification/SKILL.md)
skill; this guide is the durable semantics it cites.

Musical timing for these runs (bar counts, phase lengths, the device's real tempo) is a separate
contract: [`HITL_MIDI_CLOCK.md`](HITL_MIDI_CLOCK.md) documents the device's USB MIDI clock, how
the host reads it, and how the host can drive it.

## What it proves

The procedure answers one question: **does the content the device committed
reach the MIDI output, row for row?** It separates three stages that can each
lose content, and compares them:

```
capture chunks ──commit/seal──▶ committed passes ──materialize──▶ effective snapshot ──snapshot publication──▶ played MIDI
    (record + overdub raw)         (record + overdub)               (canonical merge)          (restored playback)
```

It is a *representation/parity* check, not a timing or musicality check. A
`content-review.json` verdict is about **inventory** (which rows, how many);
order and timing are reported separately.

## The device boots into LOOP_EDIT — set it explicitly

**The Teensy always starts in `LOOP_EDIT` after a reboot.** That is a boot-state fact and it is
still not something a run may rely on: the gestures a HITL run performs (the pre-transport clear
selects a track row and a loop row and long-presses record; a rollback or a manual session does
the same) move the device **out of** `LOOP_EDIT`, and the record/overdub gestures mean something
else once it is gone — a loop-row select becomes geometry editing, and the record arm lands on a
different slot (`RECA slot=0`).

So every record/overdub leg **establishes** `LOOP_EDIT` explicitly and proves it from the device's
own acks (`hitl.edit_mode_precondition.ensure_loop_edit_before_record`). The mode is *caused*,
because the device only announces it when a session event happens:
`ControlSurfaceManager::sendEditSessionMidi` logs `Edit session: <MODE> (Program N, Note M trigger)`
**when a session opens**, and leaving `NOTE_EDIT` logs `MIDI Encoder: Long press - exited edit mode`.
A capture that attached after the boot's session-open event holds neither, so sampling for a row
that is not there proves nothing:

* the capture already names `LOOP_EDIT` → nothing to do;
* otherwise the edit button (note 38, `MidiConfig::NOTE_EDIT_MODE`) is held **long** — its bound
  `EXIT_EDIT_MODE` action, which always ends in `LOOP_EDIT` (`EditManager::exitEditMode` →
  `sendEditSessionChange(EditSessionType::Loop)`) — and the ack that press leaves is required:
  either `Edit session: LOOP_EDIT` or the `exited edit mode` row. Device evidence
  (`captures/session_20260930_142350.log`): `Long press detected: duration=663 >= effectiveLongPress=600`
  → `Button press: Edit Mode (long)` → `MIDI Encoder: Long press - exited edit mode`;
* `LOOP_EDIT` cannot be proven → **refuse by name**, with the `--boot-reset` remedy (that puts the
  boot's `Edit session: LOOP_EDIT` row in the capture). An unproven mode is not accepted: running
  with the wrong gesture meaning produced the `missing_phase_boundaries` /
  `overdub start refused: no predecessor stop-completion receipt` class, where the record leg
  looked fine and the successor leg could never be admitted.

The **short** press is deliberately not the proof: it is bound to `CYCLE_EDIT_MODE`, so what it
lands in depends on the mode the run is trying to prove. Never press the edit button twice inside
the double-tap window (300 ms) either — a double press opens the load/save overlay.

Do not "fix" a run by assuming the reboot put the device in `LOOP_EDIT`; assert it.

## The three captured views

`!CAPTURE_CONTENT` (capture build only, transport **stopped**) prints one loop's
worth of rows in three sections:

| Section | Owner | Meaning |
|---|---|---|
| `record` | `loop.passes.recordPass.committedChunkIds` | The record pass's committed chunk events, as stored. |
| `overdub` | each active `OverdubPass.committedChunkIds` | Each committed overdub pass's events, as stored. |
| `effective` | `LoopPasses::materializeToEventVector` | The canonical merged content: exactly what the snapshot/published state should represent. |

Line shape:

```
CAPTURE_CONTENT,begin,track=<n>,slot=<n>,length=<ticks>
CAPTURE_CONTENT,rows,<kind>,<pass>,<count>
CAPTURE_CONTENT,event,<kind>,<pass>,<tick>,<status>,<channel>,<d1>,<d2>,<noteId>
CAPTURE_CONTENT,end
```

**Invariant:** `count(record) + count(overdub) == count(effective)` for this
procedure's material. Storage → effective is lossless here; minimum-length pair
removal already happened at capture time, so it does **not** appear as an
effective/played difference.

The invariant holds *while no absorbing overlay is active*. Where a later
producer's note covers an earlier note of the same pitch, the committed edit
pass **Hides** that note (or **Shortens** it) instead of rewriting storage, so
the two views legitimately differ and the count relation does not hold. That
deduction is validated and **named**, never waved through:
`hitl.content_deduction.validate_deduction` requires every effective row to be a
storage row (or a storage row whose NoteOff moved to a coverer's NoteOn − 1) and
every missing storage row to be covered by a *later producer's* same-pitch row
that is sounding there — a Hide needs the covered NoteOn at or after the
covering NoteOn, a Shorten needs the covering NoteOn inside the covered note's
sounding (wrap-aware; a note with no stored NoteOff sounds a full loop from its
own NoteOn). See DEC-112. Anything else is reported as unexplained and fails, so
a real difference is never absorbed by the deduction. `!CAPTURE_CONTENT` reports
both readings: `resolved_storage_match` (identical or fully accounted) beside
`storage_matches_effective` (the raw comparison).

## `content-review.json`

The playback verifier captures several loop cycles from the MIDI output
(channel of the selected track) and compares each cycle against `effective`,
rotated to a repeating anchor event:

| Field | Meaning |
|---|---|
| `raw_counts` | The declared per-section row counts from the dump. |
| `effective_rows_all` | Total effective rows in one loop. |
| `cycles[].rows` | Rows observed in that playback cycle. |
| `cycles[].missing` / `extra` | Inventory difference vs effective (multiset). |
| `cycles[].inventory_equal` | No missing and no extra. |
| `cycles[].exact_order` | Ordered sequence identical (reported separately; not required for inventory PASS). |

A healthy run is `inventory_equal: true` on every cycle. **Exit code 0 alone is
not evidence** — a process can exit 0 while the wrong content was exercised.

## Expected residuals (ledger/pairing, not loss)

These are known, reproducible differences between `effective` and played output.
They are **not** count loss and do not indicate a snapshot defect:

1. **Loop-boundary terminal Off.** A note whose Off lands on the last tick
   (`tick == loopLength - 1`) may be stored with `NoteId = 0` (unpaired terminal
   closure). Playback does not emit that Off; the note is treated as held across
   the wrap and retriggered. Look for an Off at the final tick with `NoteId = 0`.
2. **Coincident same-pitch Offs.** When two distinct notes of the same pitch
   (different `NoteId`s) end on the same tick, playback emits one Off, not two.
   Look for two Offs sharing `(tick, channel, note)` with different `NoteId`s.

Both are ledger/pairing semantics owned by playback, tracked separately from
snapshot construction.

## The capacity class this was built for

A **bar holding more event references than the membership representation can
encode** used to fail snapshot construction mid-fragment. Because pass-change
progress had already advanced, the failed fragment was dropped and a truncated
candidate was published, so playback repeated the same omissions every cycle.

After the [DEC-047](../DECISION_LOG.md) amendment (2026-09-16):

- Per-bar capacity is the explicit `kSnapshotMembershipMaxRefsPerBar` envelope,
  applied to event-bar and note-bar membership.
- Fragment incorporation is transactional; a failed fragment is retried against
  unchanged authoritative progress, and a partial candidate is never published.

A healthy dense run may legitimately have a bar **above the old 256-ref
ceiling** with zero capacity-driven missing rows. Confirm the capacity path was
actually exercised by computing the per-bar effective counts from the dump; a
run whose densest bar is below the ceiling is a parity check, not a capacity
re-stress.

## The interruption fixture (concept)

`scripts/hitl_stop_interruption.py` runs the single-overdub recipe while
wrapping the overdub-stop phase with a `StopInterventionPort`, so a transport
Stop is sent a configured delay after the overdub-stop button release. It writes:

- `interruption-result.json` — PASS/FAIL/INCONCLUSIVE with stop-precedence
  timings (`intervention_offset_us`, `recovery_us`, `all_notes_off_delay_us`,
  post-stop silence).
- `command.json` — **the inner `host_midi_hitl.py run` argv. This is a run
  artifact, not an entry point.** Executing it directly omits the intervention
  wrapper and does not reproduce the fixture.

The fixture's focused verdict covers stop precedence and silence only; it does
not assert snapshot content. Content parity is the separate procedure above.

## Deterministic edges for timed probes

A probe that must act on a device edge (a successor press inside a predecessor's deferred
window, a stop retry after a release) paces from a **human-log marker**, not from a fixed
delay and not from a `#CAP` diagnostic. Capture-tier rows queue behind heavy telemetry: in
`captures/successor-reentry-19` the `ST,Track,OVERDUBBING,OVERDUB_STOPPING` row with device
time 50.395970 appeared in the arrival-ordered log thousands of lines after human lines for
50.896, so any live pacing on it lands seconds late. The markers used by the reentry probe are
`Reentry window open slot=<n>` and `Reentry released`, with `DIAG,reentry_window_open` and
`DIAG,canonical_release` as the evidence tier.

Marker discipline (see `.cursor/rules/HITL-Implementation-Evaluation.mdc`): short fixed-format
strings, one line per transition, waited on through `find_line` rather than a snapshot copy,
and the build's `free for local variables` checked afterwards — a long marker literal tipped a
4 KB `.data`/`.bss` alignment snap once with no symbol-size warning.

## Related

- Recipe: [`.agents/skills/hitl-content-verification`](../../.agents/skills/hitl-content-verification/SKILL.md)
- Read-only restored-library attribution (a different question: *which slots* the device restored, not *what the loop contains*): [`RUNTIME_STORAGE_AND_PERSISTENCE.md` § The on-demand library observation](RUNTIME_STORAGE_AND_PERSISTENCE.md#the-on-demand-library-observation)
- HITL flow rules: [`.cursor/rules/HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc)
- Scenario catalog: [`docs/Guides/HITL_TEST_SCENARIOS.md`](HITL_TEST_SCENARIOS.md)
- Membership/construction decision: [DEC-047](../DECISION_LOG.md)
- Execution bounds: [`docs/Authority/Architecture/ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md)

## Record evidence loss diagnostics

`!CAP_IDENTITY_STATS` is a stopped-only, read-only request. It emits the existing
`RID,stats` row and independent `RING,record_stats,RECA` / `RECS` rows. Each family
reports emitter entries (`entered`), successful enqueues (`enq`), rejected
appends (`rej`), transmitted rows (`tx`) and evicted rows (`evict`). Counters span
the current boot, so compare before/after snapshots when attributing a single run;
a queued row need not have been transmitted yet. The new RECA/RECS counters saturate
at UINT32_MAX. These snapshots bypass the ring so pressure cannot erase the
diagnostic answer. They do not assert content or timing correctness.

`RING,critical_rejected,<count>,<first_failure_us>` reports a refused critical
append, including failure to allocate the ring. Like `RING,overflow`, it makes
`capture_integrity` INCOMPLETE. The notice is retained until a safe USB flush has
space, even when there is no allocated ring. Ordinary `RING,shed` remains separate.

Ordinary `RING,shed,<count>` stays a **non-loss**: the playback flush discards only
non-Tier-A diagnostics rather than blocking the transport on USB I/O and reports it as
neither loss kind, so no oracle may fail a run on a shed row alone. The session-invalidating
set is `overflow`, `critical_rejected` and `wiped` — the three `scripts/hitl/serial_collector.py`
marks `CaptureIntegrity.INCOMPLETE` — plus `tier_a_evicted` where an oracle's own gate pins it
(`scripts/hitl/accepted_sequence_probe.py`, which reports shed separately as
`ring_shed_diagnostics_only`).
Tier-A reentry/release diagnostics are not critical and cannot evict critical
Record rows; actual critical-to-critical displacement remains possible.

### Managed baseline end-of-run retrieval

The managed baseline performs Record evidence retrieval after its measured leg,
including when that leg aborts, while the MIDI port and the original serial
reader remain open. It sends an idempotent MIDI Stop, confirms stopped-only
fenced `!STOP_STATUS` / `!CAP_IDENTITY_STATS` replies, and repeats fenced
`!TRACE_DRAIN` requests within a 30-second cap. Host-side serial quiet is not an
empty-ring receipt. No serial reopen or reset is part of retrieval.

Completion requires the selected Track's stop queue to be idle and each Record
family to reconcile its counters. Newly transmitted rows must be present in this
session log. The first stopped stats reply is the retrieval baseline; each count
is matched to the exact reply fence, so prior-boot transmissions outside the log
and later asynchronous flushes cannot distort the delta. Earlier leg evidence is
still checked by the baseline verifier. Reported loss, saturation, reset/regression,
missing replies or unaccounted transmissions leave retrieval incomplete. Existing
boot-lifetime loss is conservatively unassignable without a pre-leg snapshot.

The baseline report retains `assertions.record_evidence_drain`, including the
before/after counters, batch count and reason. Its final verification reads the
same capture file to include rows the follower thread has not yet consumed.
An incomplete drain marks capture integrity incomplete without replacing the
original leg failure. This proves retrieval of queued Record evidence, not full
ring/persistence completion or acceptance of the measured behavior.

### Missing evidence versus a firmware contradiction

A serial-verification issue means one of two different things, and the verdict now
says which:

- **Absent evidence** — the row that would prove the claim is not in the capture,
  for behavior other evidence already places. The claim stays unproven:
  `INCONCLUSIVE_CAPTURE` with reason `EVIDENCE_MISSING`, `overall_ok` false, and
  `assertions.evidence_gap_issues` naming the rows. In this class:
  `record_stored_revt_missing`, `record_recs_missing`,
  `record_stop_stage_missing:*`, `missing_phase_boundaries`,
  `*_stored_sevt_missing`, `persistence_active_snapshot_unavailable_during_save`,
  `persist_admission_not_observed`, `record_start_identity_missing`, `display_*`.
- **Contradiction** — the device asserted something inconsistent with itself. This
  stays `FAIL` / `FIRMWARE_CONTRADICTION`, listed under
  `assertions.firmware_contradiction_issues`. In this class: a missing note *event*
  (`record_first_note_missing` and its overdub counterparts — the note should have
  sounded), a length mismatch, a bad stored grid, a short span/count/bar set, an
  oversized persistence slice, and a stop whose save was not deferred.

Neither class is a PASS, and neither is a reason to enlarge a timeout. Ring loss
has its own route: `RING,overflow` and `RING,critical_rejected` make capture
integrity INCOMPLETE, which is neither class above and is always fatal to a
verdict. The verify adapter applies the same split, so a per-suite verdict cannot
disagree with the baseline report.
