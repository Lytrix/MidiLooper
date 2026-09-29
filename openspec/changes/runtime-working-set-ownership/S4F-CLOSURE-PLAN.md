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

### §3 status — 2026-09-29 (S17.1 + S17.2 landed; caller cutover open)

The executable slice sequence S17.1–S17.5 — the mode-conditioned admission with the record
receipts, rerouting both stop entry points through the drive, the manager continuations consuming
a job-qualified geometry-ready result, the S4f.20 residuals, and the enforced gates and mutations
— is pinned with the fixed fixture requirements and risks in the
[cutover packet](S4F17-CUTOVER-PACKET.md).

The strict contract after §1+§2 was **45 cases, 3 failed** (R→R and R→OD: record still publishes
inline; OD→R: B's deferred record job is absent; OD→OD passes).

**S17.1 — Record admission bind: done.** `Track::beginDeferredRecordStopForSlot` admits a Record
stop through the same ring, interval-identity rule and capture-stop owner as the overdub form,
binding the interval's own slot/interval, the `canSealCaptureRange`-preflighted raw cut, the
job-owned geometry receipt and the held-note receipt, with the final length taken from the
maintained maximum-tick summary (no store walk at the stop edge) and the private prepared output
reserved at the record start (`prewarmCanonicalCaptureOutput`). No routing changed, so the strict
oracle is **46 cases, 3 failed — the same three**. Full native: 2 612 cases, 2 605 succeeded,
7 skipped. `teensy41-capture-serial`: SUCCESS, RAM1 padding 1 016 (floor 840).

The ITCM cost of the slice — the held-note receipt transfer compiles to an out-of-line 160 B
`_Hashtable::swap` that the linker keeps in ITCM and reaches from FLASH through an 8 B veneer —
was reclaimed by moving the cold, `SESSION_CAPTURE`-gated `PersistenceDiagnostics::maybeEmitPeriodic`
out of ITCM rather than by re-basing the floor.

**S17.2 — Both stop entry points routed through the drive: done.** `stopRecording` and
`stopRecordingToStopped` admit, run the transport edge (`Track::completeAdmittedRecordStop`) and
return; a denied stop is retained and admitted later by `driveAcceptedInteraction`, which now
dispatches on the retained request's capture mode. The inline Record fallback is gone from both
entry points, and the pass, the `RecordPassAdded` undo entry and the once-only persistence
admission come from the worker's publication.

The strict contract is now the **green** exit of the cutover's first half: **45 cases, 45
succeeded** (R→R, R→OD, OD→R, OD→OD), unchanged by `S4F16_REQUIRE_CONTRACT`. Full native: 2 612
cases, 2 605 succeeded, 7 skipped. `teensy41-capture-serial`: SUCCESS, RAM1 padding 2 040 (floor
840).

The converted fixtures — the three `recordNormalizationInventory` modes, the larger-than-staging
take, the filtered-empty reset, the exact held-note closures and the working-set transport-stop
successor take — are what verify the deferred chain's *content* rather than its coverage, and
they exposed three defects that the synchronous inline route had hidden: the deferred
normalization closed open tails at loop end instead of the stop playhead's phase; the deferred
grain prologue required the live loop length to equal the job's, parking a completed job forever
once an all-filtered take or a successor reset the slot; and the S4f.19 continuity gate waited on
a frozen window build that only the playback owner can advance, deadlocking retirement on an empty
take. All three are fixed in the slice, and the Record admission now declares its bounded
preparation estimate.

**S17.3a — switch guard for a retained Record stop: done.** `finalizeCaptureAndSelectSlot`'s Record
branch now mirrors the overdub branch's slot-scoped rule: an admitted Record job does not hold the
active slot (the switch commits, so a different-slot successor is not serialized behind
predecessor cleanup), and an accepted-but-unbound Record stop refuses the switch and leaves the
active slot unchanged. Without it, S17.2's deferred stop let the switch advance `captureInterval_`
past the retained interval, and `driveAcceptedInteraction` then judged the obligation superseded
and dropped the take. Fixture `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one`;
the guard-removal mutation fails it. Full native 2 613 cases / 2 606 succeeded / 7 skipped;
capture-serial build PASS, RAM1 padding 1 976.

**S17.3b — job-qualified geometry result: done.** `Track::recordStopGeometryForSlot` reports
`Final` (no stop in flight for that slot, so the length is authoritative) or `PendingStop` (an
admitted job or a retained obligation still owns it). `stopRecordingTrack`,
`finalizeCaptureAndSelectSlot`'s Record branch and `handleTransportStop` adopt the geometry once,
only when final; until then they retain a per-track obligation that
`servicePendingRecordStopGeometry` consumes at the next dispatch boundary, and auto-align goes
through the new slot-qualified `Track::setLoopLengthForSlot`. This closes a real defect: the stop
edge stamps the *predicted* length, so the synchronous read adopted a `masterLoopLength` from a
take the minimum-length policy then deleted and auto-aligned the emptied interval with it.
Fixtures `test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction` and
`test_filtered_record_take_publishes_no_master_length`; reverting the transport continuation to the
stop-edge read fails both. `scripts/ram1_budget.json` was re-based deliberately (+32 B DTCM for the
obligation, offset by moving the continuation to FLASH: padding 1,976 → 1,992), with the reason
recorded in the cutover packet.

**S17.4 — S4f.20 residuals: done.** The deferred Record publication queues the REVT stored note-on
scan the inline commit queued, and the queue is slot-qualified
(`deferredRecordRevtsPending` bool → `deferredRecordRevtsSlot`, same byte) so it scans the
publishing interval rather than whatever is active. `invalidateCaches()` parity is proven against
the actual consumer contract: `notifyCommittedContentChanged`/`markPassDerivedStale` already covers
the Loop's derived caches, and the gap — the Track-level committed-MIDI scratch, the note-edit
selected-slot invalidation and the REVT queue — is now applied by
`Track::invalidateCommittedContentForSlot(slot, queueRecordRevts)` against the job's slot. Two
fixtures, each with a failing mutation.

**S17.5a — transport stop during a Record finalization: done.** The retarget is mode-aware
(`requestDeferredOverdubStopToStopped` returns after the destination change for a Record job,
instead of running the overdub recovery against roles a Record job never populated), and
"transport stop wins" is resolved at that edge (`TRACK_STOPPED` + silence + playback-state reset).
Before this a transport stop during a Record finalization left the track in `TRACK_PLAYING` while
the job's destination said STOPPED. The playback-runtime reset path deliberately does *not* force
the transport state (its retained window is still the sounding source), which the playing-inventory
fixture pins.

**S17.5 — gates, mutations and accounting (native leg): done.** The finalization suite is an
**enforced** gate: `S4F16_REQUIRE_CONTRACT` is no longer read, so the ordinary suite asserts the
deferred production shape unconditionally and there is no weaker mode to select. Nine mutations are
red-proven, including the three the packet names. Accounting is the compile-time structure pins
(`sizeof(Track) == 384`, `sizeof(CaptureStopJobs) == 28936`, active under
SESSION_CAPTURE+PERF_TELEMETRY) plus the post-link RAM1 gate
(`variables 96,192 / ITCM 393,216 (code 391,224 + padding 1,992) / locals 34,880`, floor 840).
Strict matrix 47/47; full native 2,617 cases / 2,610 succeeded / 7 skipped; capture-serial SUCCESS.

**§3 exit breadth.** Transport Stop is covered across the deferred stop's phases by
`test_continuous_capture` (canonical-copy, mid-flight playing retarget, live successor, B03
retarget, accept-then-transport-stop) plus this change's Record retarget fixture; clock-source
classification is covered by `test_clock_transport_master`. **Still owed:** a deferred Record stop
under an **external** clock, and a slot **load** / slot **clear** issued while a Record stop is in
flight (replacement is covered; load and clear are not). No device acceptance is claimed from host
results.

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

### §2 status — 2026-09-28 (G0 complete, S19.2 landed)

Read-only G0 is complete and pinned in the
[playback-handoff packet](S4F19-PLAYBACK-HANDOFF-PACKET.md): verdict **PASS with named
prerequisites**. §2 is not a wiring change — the MIDI entry early-returns without a committed
pass, the retained bar-1 window is neither bound nor reachable on the deferred route, the
projected frozen-source fill has no production caller, no onward-window builder exists, and
retirement can invalidate a source a playback fill would read.

**S19.2 landed (partial implementation):** the deferred worker now binds the retained bar-1
window to the job's sealed source generation and final geometry through
`LoopPlaybackRuntime::bindDeferredRecordReturnWindow`, and the bind **transforms** the raw bar
(overflow NoteOn remap, overflow non-note drop, held-note closure at the loop end, canonical
re-order, coverage clamped to the prepared-bar/final-loop intersection). Fixtures verify the
transformed content, the stale/unsealed refusals and the pending-owner eligibility predicate;
mutations fail assertions. No new view role or job byte; RAM1 padding stays 840.

**Open:** S19.1 MIDI-entry admission (held until a native fixture can observe emission),
onward windows from the owned frozen source, and the continuity-gated retirement — retirement
must wait until a matching committed replacement is handed off, all builders have detached, and
playback no longer needs the range. Production Record admission must not be enabled before
those land. S4f.19 stays open.


