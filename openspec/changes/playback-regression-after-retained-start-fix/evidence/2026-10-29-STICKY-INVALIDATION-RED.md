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

## Dispatch probe — 2026-10-29 (3.5b), and the chain it names

Added `PlaybackDispatchRefusal` (same header) and two report-only rows: the gate is now named **where the holder's
`canProgress` is computed** (an eligible-but-blocked holder is skipped before the service body, so the retry's own row
could never explain it), and the step reports where a turn stopped when it did not reach the committed retry.
Behaviour-neutral: focused suite still 248 passing after the slice; target build SUCCESS, RAM1 unchanged
(`variables 96,192`, code `391,656`, padding `1,560`), RAM2 `170,016` (+128 B for the two last-value tables).

### Named chain (host, `test_dec193_a_restarted_rootless_slot_keeps_owing_a_build_that_is_never_served`)

1. demand active, epoch adopted, `positionInvalidated == false`;
2. `playbackWindowServiceOffer(...).eligible == true` — the slot offers itself;
3. `canProgress == false` — because the retry gate answers **`gate_no_role`**
   (`!runtime->coverageRoleAvailable(privateRootOwed)`), so `fillPlaybackHolders` marks it blocked;
4. `selectPlaybackHolder` skips blocked holders → **`dispatch_no_holder`**;
5. `stepCommittedPlaybackRetry` is never reached → readiness stays `OwesBuild` → no window, no `playback_read`.

So the defect is a **view-role leak**: after the promise's position is invalidated, a role in the prewarmed
`RuntimePlaybackViewSet` stays occupied (no build role available) and nothing retires it, so the next legitimate epoch
cannot start a build. It is not retention, epoch identity, dispatch eligibility or the gate's own logic.

### Why this fits the device

The device's silent sessions show `playback_build,pending_position_invalidated` (the invalidation) followed by no
`playback_read`, with no pending stop and no retained Start. `gate_no_role` is exactly the state that produces that
shape: invalidation first, then a slot that can never build again. The device row to look for on the next authorized
image is `playback_build,gate_no_role` (or the dispatch token) during the silence.

### Next

- **Device correlation (minimal):** one authorized capture, rootless silent slot, look for the `gate_no_role` token.
  Same name → repair; different name → the fixture is missing one production condition.
- **Repair candidate (not authorized yet):** retire the stale role when the demand's position is invalidated, so
  `coverageRoleAvailable` is true for the new epoch. One existing owner, no new state; must keep both oracles
  (playback restored, retained-Start retry still cheap).

## Device image prepared for 3.5c (not flashed)

- HEX `3f74ecd5f07fc283438850ec71dd403cc4fb18fc0fff06b0951d89179f9f0718`, built from `0d475fb1` (clean tree),
  env `teensy41-capture-serial`, RAM1 `96,192 / 391,656 / 1,560`, RAM2 `170,016`.
- Bundle per the upload guide's retention rule: `captures/dec193-naming-image-20261029/` (firmware.hex, sha256.txt,
  git-head.txt, git-status.txt, build-flags.txt).
- Why a flash is required at all: the `playback_build,<gate|dispatch>` rows are new; the installed `c4841c91…` cannot
  emit them, and no existing capture can answer the correlation.
- Caveat: this image still carries DEC-192 recovery, so the silent window may be short. Capture from boot and take the
  Stop/Start sequence early; if the device recovers before any row, the run is INCONCLUSIVE rather than negative.

## Device run `session_20261029_032119.log` — INCONCLUSIVE (gesture timing), and one probe bug found

Image `3f74ecd5…` (naming rows present), boot `crash=0`, 24-slot restore sweep, `t7s3` restored.

What the log shows:

- `root_rev,0` until **32.10 s**; `snapshot_unit kind=finish/publish` at 32.10–32.11 s; first `playback_build,snapshot`
  at 32.13 s. The slot is rootless for the first ~32 s, exactly the reported product defect.
- Transport **started at 40.9 s and stopped at 49.1 s** — i.e. *after* the root published. The 8 s of playing had a
  root, so the window lane was never asked in the silent state.
- Zero `gate_*` / `dispatch_*` rows. That is consistent with the maintenance owner not being serviced during
  boot→32 s (transport stopped), not with a refusal.

Probe bug found and fixed: `PlaybackDispatchRefusal::Retry` is enum value **0**, and the last-value table is
zero-initialised, so the *reach proof* (`dispatch_retry`) could never be emitted on a fresh boot. The table id is now
`value + 1` so zero means "unseen" only. This does not affect the `gate_open` / `gate_*` rows already on the board.

Verdict: **inconclusive, not a divergence.** The run must Start playback immediately after boot and stay playing while
the root is still absent. No flash is needed for that: the flashed image reports `gate_open` (0xFF) on the first open
gate, so the reach question is still answerable.

Second attempt recipe: reset with `capture_session.py --boot-reset`, press Start as soon as the first paint appears
(≤5 s), keep playing ~20 s through the root build, then Stop → Start → Stop.

## Device run 2 `session_20261029_032402.log` — the device answers with whole-root publication

Setup note: `--boot-reset` was **refused** (`!REBOOT is transport-gated`, the transport was still running), so this is
a capture of the already-playing device, not a fresh boot. That makes it the relevant state: **playing, rootless,
demand invalidated**.

Timeline (device micros):

- `20.156 s` `playback_build,pending_position_invalidated,0,23808,0,1,7,3,…` — the invalidated demand for t7s3.
- `20.170 s` `snapshot_unit,kind=start … q=2,a=0,if=1` — a **whole-root publication** begins 1 ms later.
- `58.493 s` `snapshot_unit,kind=publish … n=7990` — the root lands. **~38.3 s** after the invalidation.
- Zero `gate_*` rows and zero `dispatch_*` rows in the whole log.

Interpretation, with its limit stated:

- The device's producer after invalidation is the **root publication** (DEC-192's recovery), not a window build.
- No `gate_*` row means the retry gate never refused this slot. But the flashed image cannot report
  `dispatch_retry` (the sentinel bug fixed after it was built), so "no dispatch row" cannot distinguish "retry
  reached for another slot" from "lane not offered". Only `gate_*` is trustworthy here, and it is absent.

**Verdict: divergent, not correlated.** Host: retained source → `OwesBuild` → `gate_no_role` (role leak). Device:
no retained source → `OwesSource`-shaped slot → the lane's only producer is the whole-root publication. Per the
agreed rule, **3.5d (role retirement) is NOT authorized** by this evidence.

The product defect the owner reports is confirmed and measured: playback for the restored slot waits ~38 s for a
whole-loop root, and that wait is the thing to remove.

### Where the two shapes differ (the missing production condition)

| | host fixture | device |
|---|---|---|
| retained source | seeded by the fixture (`retainPlaybackSourceForSlot`) | absent at boot/Start |
| readiness | `OwesBuild` | `OwesSource`-shaped (root publication owns it) |
| producer | window retry (blocked by `gate_no_role`) | `snapshot_unit` republish |
| outcome | silent 256 turns | audible only after ~38 s |

So the device never reaches the role question: without a retained source there is no window demand to admit. The
role leak would only matter **after** a first-window producer exists.
