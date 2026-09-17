# HITL content verification

This guide defines what the dense **record + overdub → content dump → playback
inventory** procedure proves about a running device, what each artifact means,
and which differences are expected. The executable recipe is the
[`hitl-content-verification`](../../.agents/skills/hitl-content-verification/SKILL.md)
skill; this guide is the durable semantics it cites.

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
- HITL flow rules: [`.cursor/rules/HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc)
- Scenario catalog: [`docs/Guides/HITL_TEST_SCENARIOS.md`](HITL_TEST_SCENARIOS.md)
- Membership/construction decision: [DEC-047](../DECISION_LOG.md)
- Execution bounds: [`docs/Authority/Architecture/ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md)
