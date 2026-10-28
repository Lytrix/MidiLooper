# Handoff — find the root cause of the remaining raw-only overdub retires

**Date:** 2026-10-28
**Branch:** `feature/unified-rolling-playback-path` (last commit `cee80f8f`)
**Image on the board:** `sha256 551cac47…` (`teensy41-capture-serial`, boot `crash=0`, `BOOT,load,ok`)
**Change:** `openspec/changes/deferred-canonical-convergence` — **accepted on device for its main symptom, not
archived** because this handoff's subject is still open.

Read `AGENTS.md` first; it is the entry point and this file does not override it. Then read the change's
`REVIEW.md` (it contains the full device history) and `DEC-172` in `docs/DECISION_LOG.md`.

---

## 1. What is already fixed (do not re-litigate)

The owner's complaint was: **overdubbed notes stay audible but disappear from the display.** Four corrections
closed it, each device-evidenced and now committed:

| Correction | Mechanism | Fixture / mutation |
|---|---|---|
| 1 | per-job abandonment (`PendingStopAdmission::canonicalBaseAbandoned`), no phase-wide `causalBaseCurrent` gate, obligation recorded **after** the raw commit, `reset()` no longer cancels the obligation | 1.10, 1.11 / M-series |
| 2 | a **window** denial keeps the companion path (`canonicalBridgeUnavailable`), commits pass + companions without a bridge | 1.12 / M9 |
| 3 | a **Ready** companion derivation publishes losslessly across a moved `playbackRevision` (store-unchanged proof instead of `matchesCanonicalOwner`) | 1.13 / M10 |
| 4 | a **window-denied** job may take the same lossless route | 1.14 / M11 |

Final evidence (`captures/session_20261028_112311.log`, image `551cac47…`): the early stops log
`window denied; companion path kept` → `superseded base; interval companions publish losslessly`, the slot's
`edit` pass count goes **0 → 7**, display note count grows **246 → 433**, and **the owner confirmed the notes are
visible**.

**The remaining defect is a different shape**, described next. Do not "fix" it by re-enabling any of the removed
gates: each removal is pinned by a fixture that reds if it returns.

---

## 2. The open defect

**Statement.** Some overdub stops still commit **raw-only** and permanently discard the companion / note-edit
representation. Their notes therefore never appear on the display, even though they play back. The deferred
convergence obligation is marked on these routes, but it rebuilds the **canonical bridge-base window**, which is
not the artifact the display reads, and its target identity would move when a companion commit lands
(task 2.0's known fallback: re-scope the target to the raw-pass commit revision).

**Two named routes currently produce it** (`src/Track/TrackOverdubLifecycle.cpp`, `CanonicalTail` phase):

1. **Retargeted jobs with no canonical base** — line ~3308:
   `if (stop.retargetedFromPlaying && !runtime->canonicalBaseReady())` → `retireSkippedCompanionPreparation(false)`,
   `discardPreparedCompanionHistory`, message `Overdub stop: companion path skipped (no canonical base for
   retargeted job); raw pass only`.
   The DEC-168 note there says the escape exists to avoid DEC-070's 512-grain bound (seven queued retargeted jobs
   held the reserved session-exit record `Populated` for ~26 s on `session_20261027_172742.log`). **Latency was the
   motive; content fidelity was never solved.**
2. **Abandoned base** (prefix denied) — line ~3295: `stopCompanionPathIsUnreachable(...)` → message
   `Overdub stop: companion path skipped (no canonical base); raw pass only`.

**Device evidence for the residual symptom** (`captures/session_20261028_112311.log`):

| time | row | meaning |
|---|---|---|
| 135.0 s | `Overdub stop: companion path skipped (no canonical base); raw pass only` + `atomic_transfer … causal_base_superseded` | one interval lost its companion |
| 142.4 / 144.2 / 145.2 s | `Overdub start refused: the session-exit publication obligation is still owned (slot=3 state=populated)` | the reserved session-exit record stayed populated and refused new overdubs (line ~527) |
| 147.0 s | transport stop; `All Notes Off` ×7; `Overdub stop retarget: phase=1 dest=1 companions=kept` | seven queued stops retargeted |
| 147.1–147.3 s | `no canonical base for retargeted job; raw pass only` ×3 + `atomic_transfer … causal_base_superseded` ×3 | three intervals lost their companions |

Earlier session, same shape: `captures/session_20261028_110951.log` at 137.6–137.8 s
(`retarget: phase=5 dest=1 companions=kept`, then two skips).

**What "root cause" means here** — the question to answer, not just to patch:

> Why is `canonicalBaseReady()` false for a **retargeted** stop at all, and can the companion representation be
> derived for such a stop (or converged afterwards) without reintroducing the 512-grain stall the DEC-168 escape
> removed?

Candidate sub-questions, none answered yet:

- Is the retargeted job's base genuinely unavailable, or is the base ready *for another interval* and merely not
  bound to this job's receipt (the DEC-166-style identity mismatch)? `retargetStoppedRecovery`
  (`TrackPreparedStopCommit.cpp:236`) only **rebinds an existing** base — check whether the base exists in the
  runtime at all at that moment (`canonicalBaseReady()`, `captureStopOwner`, horizon phase).
- At a transport stop, is the base torn down by the stop path *before* the queued jobs reach `CanonicalTail`? If
  so, the real defect is ordering/lifetime, not derivation.
- Does `discardPreparedCompanionHistory(stop.intervalToken())` on that route throw away work that **was** already
  prepared for the interval (the same trap correction 4 fixed on the window route)? Check
  `preparedCompanionHistoryReady/Ids` before the discard.
- Is the session-exit obligation (`claimOverdubSessionExitObligation`, DEC-163) starved by these same jobs — i.e.
  do the three `state=populated` refusals have the retargeted burst as their cause or an independent one?

---

## 3. Where to look (code map)

| Concern | Location |
|---|---|
| Stop lifecycle phases (CanonicalTail, CompanionHistory, CompanionBridge, AtomicTransfer) | `src/Track/TrackOverdubLifecycle.cpp` (~3270–3620) |
| Retarget escape (the defect) | `TrackOverdubLifecycle.cpp:3308` and `:3295` |
| Retarget of a queued job | `TrackPreparedStopCommit::retargetStoppedRecovery` (`src/Track/TrackPreparedStopCommit.cpp:236`), caller `TrackOverdubLifecycle.cpp:1949` |
| No-bridge commit + supersession proof plumbing | `src/Track/TrackPreparedStopCommit.cpp` (`commitPreparedWithoutBridge`, `advanceAtomicTransferWithoutBridge*`, `advanceAtomicTransferSupersededWithCompanions*`) |
| Companion horizon and its publish proofs | `src/Loop/LoopPreparedCompanionHistory.cpp`, declarations in `include/Loop.h` |
| Obligation state machine + target identity | `include/TrackPlaybackRuntime.h` (~1180–1230), `src/Track/TrackPlaybackRuntime.cpp:1222` (`markDeferredConvergenceOwed`) |
| Deferred owner turn | `Track::serviceDeferredCanonicalConvergence` (`src/Track/TrackDeferredMaintenance.cpp:948`, called at `:1114`) |
| Session-exit obligation refusal | `TrackOverdubLifecycle.cpp:527` |

---

## 4. How to reproduce and prove (host-only first)

The whole change is testable on the host; do **not** ask for a flash until a native fixture reds.

- Suite: `pio test -e native -f test_shared_capture_finalization` (full: `pio test -e native`).
  **`--filter` is unreliable** — it runs the whole suite.
- Test file: `test/test_shared_capture_finalization/test_shared_capture_finalization.cpp`. ~143 fixtures in one
  translation unit; registrations are **unindented** `RUN_TEST(...)` lines inside `main()` (~line 10100+).
  Insert a new fixture immediately before `int main()` and register it beside its siblings.
- Useful existing helpers in `TrackCaptureStopTestAccess` and the file: `prepareSlot`, `start`, `stop`,
  `advanceToPhase`, `driveOverdubStopToRetirement`, `driveAbandonedBaseStop`, `forceCanonicalWindowDenied`,
  `installCurrentRootForDeferredConvergence`, `head(t).admission`, `deferredIdle`, `jobs`, `phase`, `step`,
  `boundary`/`closureRuntime` accessors.
- Fixtures 1.11–1.14 are the closest models; 1.14 shows how to move the owner (`++loop.playbackRevision`) at a
  precise phase and assert which route was taken (`head(t).admission.supersededCompanionExchange`).
- Diagnostic printing from `src/` only appears in Unity runs with `-v`; wrap temporary probes in a local
  `#define … DEBUG` block and remove them before the final build (a stray probe in the test file is cheap, in
  `src/` it is not).

**Suggested first fixture (the oracle this handoff proposes):**

> Bind two overdub stops to one slot, mark the second `retargetedFromPlaying`, drive the transport-stop path so the
> base is unavailable exactly as on device, and assert that after retirement the interval's companion
> representation is either published inline **or** converged by the obligation within N owner turns — and that the
> slot's `editPasses` and the display-relevant row count do not end up lower than before the stop.

If the answer is "the base is recoverable here, the escape is too broad", the repair is local. If the answer is
"the base is genuinely gone", the repair is the task-2.0 re-scope: the obligation must own the **companion**
derivation and target the **raw-pass commit revision**, not the canonical window.

---

## 5. Acceptance

Not "tests pass" — this:

- After a session with rapid overdubs **and a transport stop with queued stops**, every interval ends with either
  an inline companion representation or a converged rebuild; no stop leaves `causal_base_superseded` as its last
  word.
- The display's resolved note count does not fall across the session (it must not drop when a retargeted burst
  commits).
- The session-exit obligation drains; no repeated `session-exit publication obligation is still owned`.
- The DEC-168 latency gain is preserved: the retargeted burst must still retire in bounded turns (its original
  motive — seven jobs × ~2.1 s held the exit record `Populated` for ~26 s — must not return).

---

## 6. Secondary defects seen in the same captures (separate handoffs, do not mix into the main fix)

1. **Truncated-take Record-normalization wedge.** `captures/session_20261028_104547.log`: a first Record whose
   stop tick exceeded the loop length (`Record stop truncation rewind: raw=16208 final=16128`) entered
   `CanonicalOrder` normalization and denied **terminally**, 82,080 identical rows
   `Deferred Record stop stalled: normalization preparation_denied`, with all later overdub stops retained behind
   it. The lane is sticky by design (`RecordPreparation` must not rebind over a partially rewritten store).
   The stall row now names the inner predicate (`deny=<reason> cursor=<phase>`), so the next occurrence is
   attributable from serial alone. A healthy long take **without** truncation normalizes fine
   (`test_long_first_record_stop_with_early_playhead_normalizes`). Suspect the truncation/rewind path's geometry
   receipt (`phaseTick` vs `loopLengthTicks`) feeding `RecordPreparation::Geometry`.
2. **Session-exit obligation starvation** (the three `state=populated` refusals): may be a consequence of the
   retargeted burst, or independent (DEC-163 reserved-obligation drain). Decide with evidence before touching it.

---

## 7. Ground rules for this repo (short)

- Firmware env: `teensy41-capture-serial`. **Ask before flashing**; use Homebrew `teensy_loader_cli` 2.3+
  (`/opt/homebrew/bin/teensy_loader_cli`), never PlatformIO's `-t upload`.
- Run `pio test -e native` before push/merge. Firmware commits go on the working branch, never `dev`; PR only on
  explicit request.
- Do not archive `deferred-canonical-convergence` while tasks 6.1b/6.2 are open.
- SD/device content is user data: no quarantine/delete/rename without explicit permission in the same session.
- Instrumentation stays behind session capture or an explicit debug gate; the stall rows in this area are
  once-per-job by design, so treat a missing row as weak evidence (DEC-166 lesson: a budget-8 flush once dropped a
  whole diagnostic family).
