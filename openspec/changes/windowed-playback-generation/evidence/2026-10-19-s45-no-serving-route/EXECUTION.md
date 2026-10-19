# 4.5 — the owner-level "no serving route at all" predicate

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.5, production containment
(*DEC-108 §13f*; *DEC-146*) · **Type:** firmware, native evidence only.

Follows [`2026-10-19-s45-containment-g0/INVESTIGATION.md`](../2026-10-19-s45-containment-g0/INVESTIGATION.md) (the
blocker) and [`2026-10-19-s45-latch-gating/EXECUTION.md`](../2026-10-19-s45-latch-gating/EXECUTION.md) (DEC-145, the
latch's capability boundary).

## Scope pin

**One statement:** containment is asked — and therefore the approved last resort may be applied — only on a turn where
**no serving route covers the traversal phase**. Not "the window lane refused".

## The predicate (beside route selection)

Route selection in `Track::playCommittedLoopMidi` (`src/Track/TrackPlaybackHotPath.cpp`) offers five alternatives. After
selection:

```
anyServingRouteSelected = fixedBridge != nullptr || fixedView != nullptr || livePair != nullptr || pagedEmission
if (!anyServingRouteSelected)
    containPlaybackTurnIfNoRouteForSlot(slot, currentTick, isActive)     // FLASH
```

`Track::containPlaybackTurnIfNoRouteForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp`, `TRACK_COLD_MEM`) owns the rest,
because only the merged fallback remains and its coverage needs the phase:

| Term | Meaning |
|---|---|
| `!shouldAvoidFullVisualRebuild(len)` | short loop: the merged store is the whole loop, so it covers by construction |
| `mergedWindow.windowLengthTicks > 0 && phase in [start, start+len)` | demoted loop: the catch-all's **own bounded window** covers this phase |
| otherwise | **no route covers** → `applyLastResortForSlot(slot, phase)` |

Then the existing policy decides: `containmentForSlot` still asks the *published-window* coverage question, so a slot
whose coverage remains answers `ServeLastCoherentRevision` and is **left alone**; only a slot with neither route coverage
nor published coverage reaches `LastResortTargetedSilence`. The application stays slot-scoped (`silenceSlotMidiOutput` +
`runtime->ledger.clear()`), rebuilds nothing and never touches the transport.

**Two properties the shape pins:**
- **Availability, not content.** An event-empty source is still a route ("no new events"), so silence can never be
  inferred from an empty window — the sustained-note clause.
- **Coverage, not freshness.** A stale merged window that covers still serves (that *is* "the last coherent revision"),
  so it keeps the slot out of containment exactly like a current window. Only a phase nobody covers is contained.

## Why the first form failed and this one does not

The withdrawn form (`eefd90a5`, review 2026-10-06) judged containment at the **resolver's rejections**. Those rejections
are precisely the turns the retained merged fallback serves, so it would close a sounding another route is about to play
(F1). This predicate asks the cross-route question *after* selection and answers it with coverage, so a refused window
lane is never mistaken for "no route".

## Transition oracle (production path, controllable)

`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp::test_the_last_resort_is_applied_only_when_no_serving_route_covers_the_phase`
— demoted 64-bar slot (`shouldAvoidFullVisualRebuild` true), fault latched through its own owner (two consecutive misses
via `advancePlaybackLagTraversal`), no published window, no bridge/view/pair/paged, and the merged build refused
(`mergedMidiEventsBuildInProgress = true`) so its window covers nothing. Driven through the real MIDI turn
(`rollingTestTick(track, 1000, false)` → `trackManager.updateAllTracks` → `playCommittedLoopMidi`).

| Case | Expectation | Result |
|---|---|---|
| no route covers | `last_resort_slot_silence` **1**, fault still latched (not a recovery) | PASS |
| control: build allowed, same turn | merged store covers → `last_resort_slot_silence` **0** | PASS |
| RED (production change reverted) | exactly this fixture fails (`Expected 1 Was 0`), nothing else | measured |

The fixture clears the refusal flag **before** its first assertion: a failing assertion aborts the function, and a leaked
flag silently broke two later handoff cases in the first RED run (recorded, because it is exactly the kind of
fixture-state leak that hides a green).

## Budget

`teensy41-capture-serial` (default):

| Build | code | padding | headroom above the 840 floor |
|---|---|---|---|
| HEAD (DEC-145) | 392,088 | 1,128 | 288 B |
| first inline predicate | 392,280 | 936 | 96 B (**+192 B**) |
| **landed: cold owner** | **392,216** | **1,000** | **160 B (+128 B)** |

`teensy41-capture-serial-window-qualification` (**the lane image**, measured with the two changed sources at `HEAD~1`
and again at `HEAD`): `variables 96,192 / code 392,376 + padding 840` **both times** — this slice costs the lane image
**nothing** (the same code lands where it does not move that image's ITCM end).

The first inline form computed the phase on the MIDI edge. Moving the projection arithmetic (and the merged-window
coverage test) into the cold owner brought the default image's cost to **+128 B**, below the withdrawn form's +144 B.
**No re-base**, and no unrelated code was shaved.

## Gates

| Gate | Result |
|---|---|
| focused suite | **215 / 215** |
| full native | **3332 cases: 3325 succeeded, 7 skipped, 0 failed** |
| `teensy41-capture-serial` | **SUCCESS**, `variables 96,192 / code 392,216 + padding 1,000 / locals 34,880` |
| `openspec validate --strict`, `git diff --check` | valid, clean |

No upload. **Correction (recorded the same day, after the measurement above):** the lane-enabled qualification image is
**not** over its RAM1 budget — it builds at `code 392,376 + padding 840`, exactly the approved floor, and it is the image
earlier sessions flashed. So the device leg is **not** blocked by the image; what blocks it is the receipt gap recorded
below (and the lane image has **zero** headroom, which is the real budget boundary for any further lane work).

## DEC-145's gate: deliberately **not** lifted

The owner's constraint for this slice was that the latch gate is lifted only in an image that also gains a legitimate
clear boundary. This slice adds no clear boundary: the two clear sites remain the lane's adoption boundary, and the
default image has none. So `deadlineContractFaultLatched` stays `#if PLAYBACK_WINDOW_ROLLING_ENABLED`. Consequence,
stated plainly: **the predicate is ungated and evaluated in both images, but containment can only *act* in the lane
image**, because only there does a fault exist. That is the honest capability boundary, not an oversight — and it is why
the shipping image's always-selectable merged fallback cannot produce a permanent silence.

## What remains in 4.5

- **Displayed provenance** (RWO **S4e**) — untouched.
- **The device leg** — needs an image that runs the lane and fits RAM1 (6.4/6.5 territory).
- **3.7b** — the legacy emission branches (including the merged catch-all) are untouched; the predicate works *with* them
  rather than requiring their removal, which is why 3.7b stays a separate stage.
- The moving closure inside a long/demoted slot's serving handoff (3.7a/3.14).

## Appendix — the 6.4 receipt question, measured (next slice's input)

The owner's order is *4.5 containment → 6.4's missing `win_serving` / `win_ready` receipts → 4.4 parity depth*. The first
step of the 6.4 half is a census, and it is decisive.

`DIAG,win_gate` is **state-change-emitted** (`Track::reportPlaybackRollingGateRow` compares the exit token, the deciding
flag bits, the slot, the loop length and the request ordinal against the last row for that track), so an absent exit is a
fact about the lane's decisions, not a sampling gap. Globbed over **every** log name under `captures/` (the recorded
lesson: `serial.log` is a subset — `session_*.log` and drain logs carry the decisive rows):

| exit | rows |
|---|---|
| `win_track_not_playing` | 18 106 |
| `win_window_ineligible` | 1 635 |
| `win_not_servable` | 522 |
| `win_no_runtime` | 67 |
| `win_blocked` | 29 |
| `win_waiting` | 25 |
| `win_requested` | 25 |
| `win_blocked_by_request` | 25 |
| `win_busy` | 5 |
| **`win_serving`** | **0** |
| **`win_ready`** | **0** |

Total 20 446 rows.

**The matched 25 / 25 / 25 set is the finding.** Every demand-creation episode that ever reached the row ran: *create*
(`win_requested`) → *wait* (`win_waiting`) → *block by request* (`win_blocked_by_request`). Not one episode reached
`win_ready` (the build completed and the successor sits in the ready role) or `win_serving` (the resolver adopted it and
the slot is served from a window).

So the two rows are not *lost* evidence to be recovered by re-reading or by a longer run: **the lane's own frame has
never advanced past `Waiting` on device.** The windows that F-7/F-8 did observe being built and served on the demoted
targets came from a **different producer** — the serving gather / record-return path — which is consistent with those same
runs' rows reading `av=1, al=0`: the demand lane *excluded* the loop (`al=0`, not span-aligned at that moment) while
another route served it.

**What that means for "the device side can prove the same route selection":**

1. Observing `win_ready → win_serving` needs a configuration where a demand actually **completes** (demoted, span-aligned,
   with a current source and a free coverage role, and no bound record-return job). No recorded run held that
   configuration, so this is a run-design question, not a re-read.
2. The 4.5 predicate itself answers "which route served?" in the hot path and reports nothing. A receipt naming the
   serving **route** would be the direct analogue — but it is a hot-path row, and the lane image sits at **exactly** its
   840-byte floor, so any lane-image growth is a genuine budget boundary rather than a shave.
