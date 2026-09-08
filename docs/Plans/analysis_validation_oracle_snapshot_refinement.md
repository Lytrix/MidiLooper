# Analysis and validation leftover oracles vs current snapshot

**Status:** Stage 1 shipped (device [`194438`](../../captures/session_20260908_194438.log)). Stage 2 recorded (no firmware).  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4g`  
**Parent:** 4.4f closed
([`note_edit_leftover_oracle_snapshot_refinement.md`](note_edit_leftover_oracle_snapshot_refinement.md)).
**Does not start:** hydrate Stage 4a, Overlay-as-display, raise `N`, 4.5, 4.6,
SEVT / `take_only` capture dumps.

## Debugging boundary

```text
4.4a overdub source current-root snapshot gather
    ← closed (rebuildOverdubSourceSpanCache skips pass flatten)
4.4d–4.4f NOTE_EDIT visual / rematerialize / leftover open+rows
    ← closed
NOTE_EDIT Validation / Analysis (NoteGeometryResolver)
    ← already session store (editAwareMidiEvents)
Track::validateAndCleanupMidiEvents → mergeActiveCapturePasses
    ← Stage 1 leftover idle-validate flatten
SEVT / take_only / CaptureIncrementalSanity / LCR Phase::Materialize
    ← capture or diagnostic oracles; do not change
```

## Leftover oracles (code)

| Caller | What it still does | After 4.4a–4.4f |
|---|---|---|
| `rebuildOverdubSourceSpanCache` when `publishedSnapshotIsCurrent()` | Span membership from the current root; no `materializeToEventVector` | Closed in 4.4a. Fallback flatten stays for prepared / prior / visual / passes. |
| Occupy live path | Ledger + source-view hold ids | Prepared LCR compare is `SESSION_CAPTURE` observe only. |
| `NoteGeometryResolver` Validation / Analysis | `editAwareMidiEvents()` session store | Session store is snapshot-sourced on a current root (4.4e). |
| `Track::validateAndCleanupMidiEvents` | Always `mergeActiveCapturePasses` then `validateLoopEvents` | Leftover. On a current root this is pass flatten, not span-filtered snapshot events. |
| Deferred SEVT / `take_only` | `mergeActiveCapturePasses` | Capture-only dumps. Do not change. |

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `Track::validateAndCleanupMidiEvents` and `Loop` snapshot copy |
| State-transition change? | NO — idle validate still deferred, still log-only |
| Unbounded MIDI-interval owner? | NO — same idle owner; not on the interval path |

Reuse: YES. Pass rematerialize stays the no-current-root fallback.

## Invariant

```text
When the published LoopSnapshot is current, idle validate reads that
root's span-filtered events. It does not flatten passes through
mergeActiveCapturePasses.
```

## Stages

### Stage 1 — idle validate uses the current snapshot root — shipped

`Loop::copyIdleValidateEvents` copies
`tryCopyCurrentSnapshotEditViewEvents` when the root is current.
No-current-root keeps `mergeActiveCapturePasses`.
`Track::validateAndCleanupMidiEvents` uses that copy.
RAM1 locals **6848**. Native **1773 succeeded, 1 skipped**.

### Stage 2 — overdub analysis current-root confirm (no firmware)

Current-root `rebuildOverdubSourceSpanCache` already skips pass flatten
when `sourceOrigin == "snapshot"`. The later
`materializeToEventVector` + reconstruct runs only when the source is
not snapshot. Occupy live path stays ledger / source-view.
`NoteGeometryResolver` stays on `editAwareMidiEvents()`.
This stage records that fact only.

## Out of scope

- Hydrate Stage 4a
- Changing occupy live source (ledger / source view)
- SEVT / `take_only` / `CaptureIncrementalSanity`
- LCR device-gate `Phase::Materialize`
- Removing `mergeActiveCapturePasses`

## Tests

| Gate | What |
|---|---|
| Native Stage 1 | `copyIdleValidateEvents` on a published current root keeps span NoteIds (drops event-only 64) |
| Native fallback | No current root matches `mergeActiveCapturePasses` |
| RAM1 | `teensy41-capture-serial` locals ≥ 4000 |
| Device | [`193700`](../../captures/session_20260908_193700.log): Stage 2
  current-root open `from=snapshot` `root_rev=7` `play_rev=7` `notes=159`.
  First overdub open `from=visual` `root_rev=0` `play_rev=1` at 66.025 s
  is before `pending=0` `root_rev=1` at 66.111 s.
  [`194039`](../../captures/session_20260908_194039.log): continuation
  NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`. Close
  `NoteEditPassClosed` `edits=3`, no `live capture baked`. Stage 1 idle
  validate not exercised: overdub stop calls `finalizeLoopAtStop(..., false)`
  and does not re-queue `deferredFullMidiValidate`.
  [`194438`](../../captures/session_20260908_194438.log): Stage 1 ran at
  600.015 s, 60 s after record-to-STOPPED. Root was current
  (`pending=0` `root_rev=1` `playback_rev=1`). Log-only
  `would remove 2 orphaned events`, 116 remaining; `DFRAME` **58**.
  Both overdub opens in this file are `from=snapshot` on a current root. |

## Pre-implementation review

### Ready

- Leftover idle-validate flatten named from `Track` / `Loop`
- Snapshot copy helper already exists

### Resolved

| Topic | Decision |
|---|---|
| Overdub analysis current-root | Already 4.4a; no extra flatten |
| SEVT / `take_only` | Unchanged |
| Live E: / NOTE_EDIT Analysis | Session store; unchanged |

### Open before coding

None.
