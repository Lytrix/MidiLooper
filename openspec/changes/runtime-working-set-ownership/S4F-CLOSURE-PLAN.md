# Shared finalization and response-driven HITL — remaining closure steps

Reviewed 2026-09-28 against `bd7b3bba` (clean tree before this documentation review).
This refines the existing [shared contract](SHARED-CAPTURE-FINALIZATION.md) and
[tasks](tasks.md); it does not change acceptance, capacity, ownership or task checkoffs.
The companion [HITL tasks](../response-driven-hitl-acceptance/tasks.md) remain binding.
Use this dependency order instead of historical execution packets' “next slice” text.

## Scope and current evidence

Closing this milestone means S4f.17–S4f.22 — shared finalization, S4f.13 — accepted
sequence, and the response-driven HITL change. It does **not** archive the whole
runtime change: CLI reports 50/176 runtime tasks complete and 3/21 HITL tasks complete.
Remaining runtime stages, cross-track acceptance, role/window budgets, cold state,
canonical residency, external dependencies and final S7 acceptance retain their gates.

Implemented assets to reuse: Record composition/order/identities, union workspace,
frozen-source staging and closure merge, per-track preparation role, pass transfer,
Record publication/retirement and job-owned geometry; host gesture/corpus fixtures,
fenced command forwarding, census-based preparation and boot capture/metadata.
These are substantial implementation progress, not proof of production reachability.

Fresh check: `S4F16_REQUIRE_CONTRACT=1 pio test -e native -f test_shared_capture_finalization`
still fails R→R, R→OD and OD→R; OD→OD passes. Tool summary: 30 successes, 3 assertion
failures, followed by SIGQUIT / ERRORED (34 reported cases). `/tmp/closure-strict.log`.
This is an expected failing integration baseline, not a green native run.
No new full-suite, build, device run or fault-injection result is claimed here.

The recorded `20260928-rec-verdict3` bundle exposes a missing successor start, but
cannot isolate the cause to firmware: today's record recipe also seeds A and sends
a global transport Stop. Its L1–L4 results do not establish the specified record-mode,
continuous-play acceptance. Preserve the bundle as diagnostic evidence.

## 1. Repair preparation and publication boundaries before dispatch

Maps to S4f.18 — bounded Record preparation and S4f.20 — atomic publication.
The following are source-verified paths; new adversarial reproductions are required
before fixes. They were not exercised as new device failures in this review.

- **Staging capacity and retry:** `Track::stepDeferredRecordSourceStaging` tops up
  `canonicalCaptureChunks` only when binding, to `ACTIVE_OVERDUB_TRANSFER_CHUNKS = 8`.
  `RecordSourceStaging::Cursor::step` appends the whole frozen take using those credits;
  exhaustion returns Denied. The Track wrapper discards/rebinds the cursor without
  rolling back already appended rows. A later retry can append the prefix again.
  Prove arbitrary accepted Record size within existing admission, not the OD staging
  ceiling; preserve source position/output receipt on transient pressure, or provide
  bounded explicit abandonment/reconstruction that never duplicates closure/source rows.
  Account for simultaneous raw input, prepared output, tail Offs and a live successor.
  Also review replenishment's transitive allocator/pool scan work before calling it bounded.
- **Global seal scope:** both `RecordSourceStaging::Cursor::begin` and
  `RecordPreparationCursor::begin` enter the global ephemeral-seal scope and keep it
  through Pending turns. Another track's normal capture can seal a chunk while that
  scope is active and miss normal persistence admission. Limit the scope to the
  actual private append work within a turn; it must be absent whenever control returns.
- **Partial normalization retry:** `stepDeferredRecordNormalization` discards the role
  on Denied while private output may already be sorted, repaired or marked for removal.
  Rebinding is not a rollback. Distinguish resumable resource denial from stale identity;
  preserve exactly-once identities/repair and never re-read temporary filter markers as
  original events. Verify interruptions in every mutating composition phase.
- **Empty results:** normalization rejects an initially empty store; pass transfer
  rejects filtered-empty output. Give all-dropped/all-filtered/empty Record their
  existing no-content geometry/undo/publication semantics and a terminal retirement.
  A valid empty take must not become an indefinitely pending stop.
- **Transfer continuation:** `stepDeferredRecordPassTransfer` rejects `output.store.empty()`
  before examining the job's existing transfer receipt. `stepCommittedChunkTransfer`
  can empty the store and return Pending while reversing destination IDs. Allow that
  owned continuation. Reproduce with Q=1 and two chunks, and the production Q=32 with
  32 chunks; assert exact directory order and no lost/repeated reference.
- **Publication preflight:** `stepDeferredRecordPublication` commits the pass and clears
  `ownsPendingPass` before checking `hasRecordGeometry` / `recordGeometryLoopId`.
  Missing geometry can therefore publish content and then return Denied before undo or
  persistence, with no retryable pass receipt. Validate every rejectable prerequisite
  before irreversible publication; prove pass, undo and geometry visibility agree, with
  bounded, exactly-once post-commit handoffs. Audit actual undo preparation costs too.

Exit: production worker fixtures for each counterexample, pressure→recovery and stale
replacement, exact content/resource parity and discriminating mutations. Interleave a
second track's normal append between every private grain to prove seal/persistence
isolation. Reuse the current primitives; do not create a second finalizer or lower
accepted-input capacity. Keep preparation and publication parents open until these pass.

## 2. Make playback survive the deferred interval

Maps to S4f.19 — first-bar and onward playback handoff. Complete before production
Record admission can expose an unpublished interval to playback.

Bind retained first-bar coverage to job source/generation/final geometry; feed later
windows from the frozen source until committed content replaces it. Preserve source
borrowers through successor start, publication and retirement. Readiness must cover
projected rows and held-note closure, not merely raw first-bar allocation.

Exit: production playback fixtures for incomplete preparation, sub-bar stop, remapped
notes, held notes, traversal beyond bar one and wrap while finalization is pending,
multiple sounding tracks and stale source replacement. Pin exact emitted content and
ownership before/after every boundary; old generation and premature retirement mutations
must fail. No full-take fallback on the MIDI edge.

## 3. Connect Record admission and all caller continuations as one cutover

Maps to S4f.17 — common stop admission, S4f.20 — publication parity and
S4f.21 — caller migration. Start only after steps 1–2; do not enable one entry point
while manager callers still assume inline completion.

`stopRecording` / `stopRecordingToStopped` still call `prepareRecordStop` and
`commitCaptureForStop` inline. `beginDeferredOverdubStopForSlot` still requires
`isOverdubbing()`. Bind Record's owned raw range, geometry and held-note receipts
through the common latch/job drive without a content-sized action-edge walk.

Migrate `stopRecordingTrack`, `finalizeCaptureAndSelectSlot`, `stopAllTracks` and
all direct callers. Consume job-qualified execution/geometry receipts for master
length, alignment, rewind and persistence exactly once. Protect same-slot destructive
restart while permitting different-slot successors. Resolve the named deferred revts
validation's active-slot dependence and prove `invalidateCaches()` parity against
actual consumers, rather than assuming `notifyCommittedContentChanged` is equivalent.

Exit: strict R→R, R→OD, OD→R, OD→OD production matrix passes, both stop destinations,
transport Stop in every phase, internal/external clock, load/clear/undo/save, wrong-slot
and successor-baseline cases. Remove the inline Record fallback and duplicate caller
save. Make the strict contract an enforced regression gate; ordinary suite success
with `S4F16_REQUIRE_CONTRACT` unset is insufficient. Full native + capture-serial build,
RAM1 padding >=840 and per-track/job/raw/output/overlap PSRAM accounting must pass.

## 4. Finish correlated observations and correct the actual test recipes

Maps to HITL 1.1 — execution packet, 2 — observations, 3 — actions/lifecycle,
4 — recipes, and 5 — oracle. Can proceed alongside steps 1–3 without claiming device
acceptance. Reuse command forwarding, census and gesture fixtures already implemented.

- Complete a live, bounded identity snapshot and loss-detectable sequenced edges at
  gesture, accepted obligation, execution, start/switch, publication and retirement.
  `LIBOBS` is a preparation census, not this live protocol. Include boot, track, slot,
  mode and interval; prove malformed/busy/stalled/partial/disabled-capture behavior.
- Replace generic string waits and operational sleeps with those receipts. Keep only
  firmware-defined gesture pulse/tap windows and musical stimulus timing. A deadline
  aborts; it never grants permission to press again. Verify cancellation suppresses all
  later edges and preserves evidence/shutdown on every exception/timeout path.
- Correct the record recipe: SD-aware empty A and B, actual Record start, B's slot gesture
  carrying A's stop/switch/B start, then exactly one B stop; no global Stop inside the
  window. Today `seed(A)` plus `leg_gestures('rec')`'s `a_stop/transport` contradict this.
  OD uses confirmed populated A/B and the actual capture-switch path; mixed R→OD is
  separate. Census chooses state, not a permanently hard-coded “empty slot 8”.
- Finish authorized-target clear/save completion and repeat preparation. Complete but
  unsettled census is not a timeless emptiness proof: correlate final target state and
  durable clear completion before relying on SD-aware emptiness. Never mutate unrelated
  slots or treat an incomplete window as empty.
- Register the scenarios in the existing runner; keep the standalone CLI a compatible
  wrapper. `DROVE_LEG` / exit zero from the current driver is not acceptance. Final
  outcome must include identity, single recognized press, natural overlap, continuous
  transport, content, timing and independent persistence-aware shutdown.
- Replace record-only RECA/RECS and human retirement inference. The current OD oracle
  intentionally returns UNKNOWN and L7 cannot prove a single input gesture. Keep these
  fail-closed until correlated evidence exists, with hostile replay cases for every edge.
- Boot provenance must support a **single anchored boot across repeat attempts**. The
  current `fresh_boot` check searches the first 80 lines of each log; do not solve repeat
  qualification by resetting per attempt. Reuse a continuous capture/boot identity or
  an explicitly linked anchor, reject resets between runs, and prove the running image
  matches the recorded image hash (a host git/hash snapshot alone is not that proof).
  Optional explicit initial `--boot-reset` is not automatic recovery or repeat setup.

Exit: hostile corpus/mutations pass, no false PASS on missing/wrong identity or lost
rows, watchdog/cancellation and architecture/import gates pass, and all recipe actions
run through the production DROID gesture mappings. Close individual HITL tasks only
against their complete acceptance criteria, not a log of a successfully driven leg.

## 5. Qualify once both firmware and driver gates pass

Maps to S4f.22 — integration gates, S4f.13 — accepted sequence and HITL 6 — qualification.
Use a G2 packet with registered/reused stimuli, explicit authorized target manifest,
finite attempt/density budget, identified running image, pinned display/clock config
and instrumentation overhead/resource bounds. No artificial device pause or manual pass.

On one boot: three consecutive complete PASS attempts for Record and three for OD,
plus the specified mixed R→OD sequence. Response-confirmed scoped preparation between
attempts; no reboot or global Stop in an acceptance window. Preserve every failed run
and reset consecutive counts on failure. Missing natural overlap is NOT_EXERCISED;
required evidence loss is non-PASS, not a reason to enlarge timeouts blindly.

Require exact content, one undo unit with correct geometry, persisted content, bounded
acceptance→execution latency, existing MIDI timing gates and clean independent shutdown.
First measure observation overhead; stop qualification if telemetry changes behavior.
Only then check the shared-finalization parents and S4f.13, close the HITL change's
remaining gates and update shipped documentation. Runtime archive still requires every
other runtime gate; cross-track S4f.14 and later stages are not waived by this milestone.

## Recommended next implementation slice

Step 1: production-worker denial/retry and interleaved-seal fixtures, then repair the
proven preparation/publication boundaries. This yields discriminating tests before
switching real Record traffic onto the new chain. Do not repeat hardware acceptance
while its recipe and live identity oracle are known incomplete.

## Step 1 status — 2026-09-28

**All six boundary repairs in §1 are implemented and verified** on `bd7b3bba` plus this slice:
turn-local ephemeral seal, resumable staging credits with a sticky fatal deny, resumable
normalization pressure with a sticky deny that keeps the role bound, the `RecordEmpty`
terminal for all-dropped/all-filtered takes, owned transfer continuation (with the
prewarmed ownership row sized from the take), and publication preflight before the
irreversible commit. Focused 63/63; full native 2,591 passed / 7 skipped; capture-serial
build PASS with RAM1 padding 840; six mutations red-prove the fixes; strict OpenSpec valid.
[Evidence](S4F18-CLOSURE-BOUNDARIES-EXECUTION.md). The strict shared-finalization contract
baseline still fails R→R/R→OD/OD→R because Record stop is still inline (S4f.17/S4f.21);
the destination-row capacity and geometry receipt are currently established at the first
transfer grain / bind fixture until that production bind site exists. **Next slice: §2 —
first-bar and onward playback handoff (S4f.19).**

### §2 status — 2026-09-28 (G0 complete)

Read-only G0 is complete and pinned in the
[playback-handoff packet](S4F19-PLAYBACK-HANDOFF-PACKET.md): verdict **PASS with named
prerequisites**. §2 is not a wiring change — the MIDI entry early-returns without a committed
pass, the retained bar-1 window is neither bound nor reachable on the deferred route, the
projected frozen-source fill has no production caller, no onward-window builder exists, and
retirement can invalidate a source a playback fill would read. The packet fixes the slice
sequence (S19.1 admission → S19.2 bounded return-window bind → S19.3 onward windows from the
owned frozen source → S19.4 ownership → S19.5 acceptance), the resource accounting to record
before the first production edit, and the open decisions with preferred paths. No firmware
behavior changed; S4f.19 stays open.


