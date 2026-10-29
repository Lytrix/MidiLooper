# Sticky-invalidation native RED — 2026-10-29

Read-only investigation plus one new native fixture. No build for target, no upload, no SD action.

## What was asked

After an interval gets `positionInvalidated`, does the next legitimate playback epoch (transport stop/start)
create a genuinely fresh request, or does the sticky invalidation survive and suppress the producer?

## Fixture

`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`
`test_dec193_a_restarted_rootless_slot_keeps_owing_a_build_that_is_never_served`

Sequence, all through production seams:

1. `prepareWindowOwnerSlot(0)` — committed content, **no published root**; `retainPlaybackSourceForSlot` supplies
   the passes-backed source; track forced to `TRACK_PLAYING`.
2. `requestPlaybackWindow(0, 500, scratch, {1000, 1})` → `Admitted` (one promise from the retained source).
3. One `stepPlaybackViewMaintenance()` turn, then the transport restarts: `assignCurrentTickSilently(0)` and the
   substituted deadline clock becomes `{0, 2}` (new epoch). The gather's next turn voids the promise's position.
4. 256 production turns, each: re-issue the demand with the advancing playhead
   (`requestCommittedPlaybackWindowForSlot`), step the window lane (`stepPlaybackViewMaintenance`), service the
   rolling owner (`serviceRollingPlaybackWindow`).
5. Assert: demand still owed; demand adopts the new epoch; demand not born-rejected; deferred turn eligible; no
   gather refusal; readiness reaches `Ready`; a serving window is published for the retained revision.

Command: `pio test -e native -f test_runtime_working_set_ownership`
Log: `/private/var/folders/ms/9n65q9vx7gxf9k911t93v34w0000gn/T/opencode/dec193-sticky-final-red.log`

## Result — RED at one point only

251 cases: **2 failed, 248 succeeded**. One failure is the pre-existing characterization
`test_pass_only_playback_serves_due_notes_before_whole_root_publication` (`Expected 1 Was 0`), owned by the frozen
packet. The other is this fixture, failing at **class 4a**:

| Class | Question | Answer |
|---|---|---|
| 1 | is the demand silently dropped? | **no** — still active after the restart |
| 2 | does it keep the stale epoch? | **no** — it adopts the restarted epoch |
| 3 | does it carry the invalidation forward? | **no** — `positionInvalidated` is false |
| 4b | does the owed build keep the turn eligible? | **yes** — `hasPendingPlaybackViewMaintenance()` is true |
| 4c | is the retained gather refusing? | **no** — `retainedGatherRefusal == None` |
| 4a | does readiness reach `Ready`? | **no** — stays `OwesBuild` for all 256 turns |

## What that means

The simple "sticky invalidation suppresses the producer" hypothesis is **wrong** for this shape: retention,
coalescing, epoch identity and dispatch eligibility all behave. The stall is inside the **build's own
admission/service**: an owed, eligible, source-qualified-enough demand is never turned into a published window.

That is a narrower and better-named boundary than the one the fixture started from, and it does not involve
DEC-191: nothing in these lanes was touched by the deltas.

## Limits

- Harness-only. No physical output and no device claim.
- The fixture drives the window lane (`PlaybackViewMaintenance` + rolling owner). If production serves a rootless
  slot's **first** window exclusively through the merged/fallback reader, the lane's refusal here could be a
  designed fallback rather than the device defect. The device row (`playback_build,pending_position_invalidated`
  with `playback_read` 0) shows the lane *was* active, so the shape is relevant, but the two must be tied together
  before calling it the cause.
- 256 turns is a fixture bound, not a proof of permanence; the stall is steady across every turn.

## Next micro-probe

Instrument `requestPlaybackViewMaintenance` / `stepPlaybackViewMaintenance` for this exact state and name the
refusing predicate (the same one-implementation naming pattern DEC-186/189 used). The candidate refusals live in
the admission below `playbackWindowServiceOffer`, not in the retention helpers.

## Naming probe (report-only) — 2026-10-29

Added one canonical refusal name for the retry gate, so host and device report the same word for the same state:

- `include/Utils/PlaybackBuildRefusal.h` — `PlaybackBuildRefusal` (19 terms) plus `playbackBuildRefusalName()`
  (if/else, flash literals). Not a table; no decision reads it.
- `Track::playbackBuildGateRefusal()` — the gate's order written once; `committedPlaybackRetryCanProgress()`
  now delegates to it, so the yes/no answer and the reported term cannot disagree.
- `stepCommittedPlaybackRetry()` reports the term **once per change** per (track, slot) through the existing
  `playback_build,<token>` row (`reportPlaybackViewMaintenanceRow`), so no new capture tag and no tier change.
  The last-value table is 64 B in RAM2 on target.

Fixture assertions added, so the failure names its class:

- class 5 — the gate's own term (`gate_*` token, `none` when open);
- class 6 — whether the owing slot is offered at all (`playbackWindowServiceOffer(...).eligible`).

### Result: still RED, one step further down

- class 6 **passes**: the owing slot is an eligible holder.
- class 5 **passes as `none`**: the retry gate is open (or never reached) — it is not the refusing term.
- class 4a still fails: readiness stays `OwesBuild`.

So the stall is in `stepPlaybackViewMaintenance`'s service branch **between holder selection and the
`stepCommittedPlaybackRetry` call**, not in retention, epoch identity, dispatch eligibility, the offer, or the gate.
That is the next place to name (same one-enum pattern, next slice).

### Behaviour neutrality and cost

- Focused suite unchanged at 251 cases / 2 failed (this fixture + the pre-existing frozen-packet
  characterization) / 248 succeeded — the gate refactor changed no passing behaviour.
- `pio run -e teensy41-capture-serial` **SUCCESS**: RAM1 `variables 96,192`, code `391,656`, padding `1,560`
  (unchanged); RAM2 `variables 169,952` (+64 B for the refusal table). No flash, no upload.
