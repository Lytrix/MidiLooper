**2026-10-02 — the record-stop stall is fixed on device; the repeat campaign's blocker moved to our own boot link.**
The named denial (`raw_range_unsealable`) led to one owner: `Track::stepDeferredRecordRetirement` cleared
`job.ownsRawRange` without returning the sealed extent or re-arming the descriptor, so the second cycle's successor
directory was never a prepared empty one. The retirement now releases one grain and re-reserves its tail
(`CaptureRange::needsSourceRelease()`, `reservedEmpty()`), with `Track::serviceCaptureStopDescriptorReservations()` as a
bounded idle safety net (`descriptor_rearm,ready|deny`) — it skips the two healthy states, after a guard-less version
re-reserved a healthy descriptor every turn (48,737 rows). Native fixture
`test_a_record_retirement_returns_its_extent_and_rearms_the_descriptor` red-proves the leak,
`test_shared_capture_finalization` **60/60**, native trio 185/185, capture build SUCCESS with the RAM1 line unchanged
(`variables 96,192`, `code 392,008`, padding 1,208), uploaded. On device: **three consecutive `DROVE_LEG` on one boot**
(`s45-c4-…-180740-od1b`/`-180856-od2`/`-180957-od3`), every stop's `finalize` reading `deferred` and **zero**
`descriptor_rearm` rows in the steady state. The campaign then hit our own oracle: pass 1
`s45-fixcamp-20261002-181057-od1` is a sealed fresh-boot `ACCEPTED_SEQUENCE_PASS` (parity, timing, bundle), and the
next attempt was refused by the **boot link** — its anchor time base was a maximum over *every* row, and pass 1's
drained prefix holds the **previous** boot's tail (194 649 907 us) while the capture's own boot ended at
104 510 708 us; the attempt side read the first stamp of a drained `#DMA_XFER` row (119 333 us), a family that stamps
its own base. Now `device_time.current_boot_last_clock_us` + `cap_row_clock_us` place the anchor on the boot it
**ended** on, and a repeat must match the anchor's close clock against the elapsed wall time
(`CLOCK_WALL_TOLERANCE_US = 10 s`); red-proved in both halves, restored byte-identically, and the refused attempt
re-grades to `fresh_boot: ok` (device advance 64 919 717 us against 65 348 073 us of wall time — no reset happened, so
the interleaved parity read is exonerated). That attempt is still a non-pass for two of its own reasons: its successor
was **serialized** (**DEC-111 — a serialized acceptance run is NOT_EXERCISED**: the released interval held 0 events,
its slot 4 carries a **resolved Hide** — effective 61 vs storage 63, the overdub's `(1225,On,71)` covering the
and its slot 4 carries a **resolved Hide** — effective 61 vs storage 63, the overdub's `(1225,On,71)` covering the
compared raw `storage == effective`. **DEC-112 (owner ruling) decides both questions, and both are implemented:** the
deduction is validated as a **positive pass** (`hitl.content_deduction`, every difference named, an unexplained
remainder still failing) — the live device re-read reads `pass: true` with `slot 4: record#0 note 71 (On 1238,
Off 1250) hidden by overdub#0 On 1225`, and that attempt's re-grade reads `content_parity: ok` beside the boot link
`ok`, leaving the serialized successor (DEC-111) as its only non-pass reason — and the reproducer must be
**clock/response-driven** rather than raw-MIDI-timed. That second ruling was implemented and then **measured**
(`captures/s45-flow-20261002-191802-od1`): the predecessor's window row is ring-buffered — it reached the capture
file after the switch's directly printed row and only once the 1.194 s window had closed — so no press can be placed
on it; the flow keeps its immediate post-switch press, the blocking wait was reverted with two tests pinning the
measurement, and the failed attempt's serialization is **device-shaped** (192 ms window, 0-event release, against
~600 ms of the device's own admission latency). S4.5 stays open: owed are the **device investigation of that short,
0-event predecessor window**, three consecutive od passes, the record leg's own campaign (1 of 3) and S4f.11's
cold-root undo/redo route. `deadline_safety_proven` remains false. Verification: accepted-sequence **187/187**,
scenario **40/40**, corpus **3/3**, `test_hitl_device_time` **18/18**, `test_interaction_latency` **19/19**,
`test_retention_probe` **43/43**, `test_content_deduction` **15/15**, `git diff --check` clean.
**The short-window anomaly is root-caused and reproduced.** The leg crossed its loop end; the mid-interval wrap
(`src/Track/TrackOverdubWrap.cpp:145-147`) resets the capture staging and reseeds `captureGeneration` from a
`playbackGeneration` of **0** (`include/Track.h:667`), so the stop arms a 0-generation marker with an **empty**
post-wrap staging — nothing owed, the finalization window collapses to ~200 ms, and the successor's own ~600 ms
admission lands after the release (`captures/s45-clock-20261002-194915-od1`, reproduced; the two healthy runs never
wrap inside the leg and read generation 32 with 1.194–1.581 s windows). `TrackPlaybackRuntime.h:150` declares 0 as
"no admitted interval" and nothing enforces it. The host change made here — `seed_take` pacing the take's **length**
on the device's clock (192-clock span, 120.0 BPM, jitter 0.53 ms, a **named** refusal without a clock; three new
`ClockPacedTakeLengthTests`) — is device-verified but **not sufficient**: a 3840-tick paced span still produced
1536/768-tick loops, so `loopLengthTicks` is assigned by another owner. That is the next question. **Answered and
fixed the same session:** the device runs at **192 ticks per beat**, so the paced seed *was* its intended two bars
(1536 ticks = 4.0 s) and the wrap is a **phase** effect — a leg needs ~6 beats (4 of burst + the host's measured
~2-beat overhead) while the room at its start tick was 8.5 beats in the healthy runs against 4.5 in the two that
wrapped. `_run_plan` now sizes the burst from the device's own numbers (loop length from its `RECS,stopToStopped` row,
the leg's `@ tick` phase from its start row, `bars` for the tick/clock conversion) and refuses by name
(`NO_ROOM_IN_PREDECESSOR_LOOP`) instead of wrapping. **The od leg's campaign is closed: three consecutive
`ACCEPTED_SEQUENCE_PASS` on one boot** (`captures/s45-camp4-20261002-200100-od1/-od2/-od3`, each replay-verified, all
checks ok, non-zero generations). **The record leg's campaign is closed too** — `captures/s45-camp5-rec-1` (fresh
boot), `-2` and `-3` (anchored to pass 1), all checks ok (`fresh_boot`/boot link, content parity, timing "no issue"),
`canonical_release,3,1,768,8` / `,3,3,768,8` / `,3,5,768,8`, `L5` `RECA,4,29952` "0 after A's release edge" each, all
three bundles replay rc 0. **S4f.11's two gaps are findings, not routes:** the queued *playback* switch is already
exercised by registered scenarios (`Loop 4: Queued playback switch (filled slot)` in
`captures/s45-hitl-20261002-150603-od` and three more), needing only a measurement-owner class; and **cold-root
undo/redo is gated by S5** — Revision 4 makes a root cold by *demotion* ("slot left the active set"), and no
slot-demotion mechanism exists in the firmware, so every reachable undo root is hot and the ~1-2 s figure has nothing
to measure until S5.1/S5.2a. S4f.11 therefore cannot close before S5, and neither can S4.5.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).
**2026-10-02 — the campaign's repeat blocker is named on the board: `raw_range_unsealable`.**
Both evaluation routes were taken. **Native two-cycle fixture** (`test_second_record_cycle_is_admitted_with_every_guard_precondition_pinned`,
record → stop → retire → clear → record → stop, pinning every admission precondition): **59/59**, so the two-cycle
pipeline is sound in isolation and the device's refusal must come from state the harness cannot express. **Named
refusal on the board**: the record-stop admission now names each refusal (`RECS,stage,deny_reason,<name>`; +64 B
flash, no RAM1 change — one file-scope slot, since a `Track` member trips the S3.0 eight-way size assertion). After a
fresh boot and upload: pass 1 `DROVE_LEG`, then the repeat reports
`RECS,stage,deny_reason,…,raw_range_unsealable` before `accepted_wait` — so the refusal is
`loop.capture.store.canSealCaptureRange(job.rawRange)`, i.e. the job's destination is **not a prepared empty
successor directory**, while the finalizer owner, canonical output, job ring, held notes, identity, pools, phase and
state transition are all satisfied. `Loop::beginCapture`'s `resetCaptureExtentPreservingReservedStorage()` (whose
fallback is a plain `capture.store.clear()`) is the site to trace; the pending save and `transport_active` drain gate
recorded earlier are consequences of the still-live take, not the cause. Campaign: 1 of 3 od passes; reproducible per
boot, now named. Verification: native **59/59**, host **224/224**, build + upload green with the RAM1 line unchanged.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — the campaign's repeat blocker reproduces on a fresh boot; the record-stop denial is code-localized.**
After the authorized reboot: pass 1 is a clean fresh-boot `ACCEPTED_SEQUENCE_PASS`
(`captures/s45-campaign3-20261002-172937-od1`, sealed bundle), and the second attempt on that same boot
(`captures/s45-campaign3-20261002-173113-od2`) fails identically to the earlier repeat — so the stall belongs to the
**second preparation of a boot**, not to idle time. Device rows: the clear saves
(`PERS,work,…,LoopPersist,slot:7:0,…,done,ok` 100.808 s), the take records (`RECA,4,0`), the host's commit stop lands
(`Transport stopped` 105.996 s), the stop is **denied admission** (`RECS,stage,finalize,…,accepted_wait` exists only
on `Track::stopRecording`'s denial path, `TrackCaptureStopCommit.cpp`, where DEC-067 latches it and the interval keeps
recording: `DISP,4,RECORDING,1856,…`), a 135,168-byte bundle stays owed
(`PERS,request,0,135168,135168,already_pending`) and no `PERS,drain` slice runs. The newly-trippable guards name a
resource the previous cycle still holds (`captureStopOwner != nullptr`, or the canonical capture output not admitted),
and the drain gate refuses while a capture is live — circular wait between the record-stop admission and the
persistence drain, with the firing guard not yet identified. No firmware change made; the campaign stands at 1 of 3
od passes. Next: name the denial reason at the admission, or write the native two-cycle reproducer.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — Phase 3 campaign blocked on device: an accepted record stop never executed, and the state blocked the next attempt.**
The three-consecutive-pass campaign needs one boot, so the repeat attempts could not reboot and the oracle gained a
checkable substitute for the banner gate: `--boot-anchor <capture-or-run-dir>` links a repeat to the boot it
continues (anchor's own banner, same firmware image, no device-restart row here, and the device clock continuing past
the anchor's last stamp — modulo the 32-bit `micros()`, and conservatively refusing a capture that leads with drained
rows). `BootAnchorTests` **11/11** pin each part separately and reverting the link red-proves the class;
`device_time.first_row_clock_us` is the owner-side half. Suites: accepted-sequence **184/184**, device-time
**17/17**, corpus/scenario unchanged, `git diff --check` clean, both changes strict-valid.
**Pass 1** (fresh boot) is the sealed pass. **Attempt 2** (`captures/s45-campaign-20261002-171929-od2`, same boot —
device clock 2570.5 s vs pass 1's 62.4 s) cleared slot 5, armed and started recording (`RECA,4,0` 2574.014 s), the
commit stop was **accepted** (`RECS,stage,record_stop,…,entered` 2579.057993 s / `RECS,stage,finalize,…,accepted_wait`
2579.058085 s) — and then **never executed**: no terminal `RECS` row, no retirement, and the slot kept recording
(`DISP,4,RECORDING,1912,…` 2579.106 s). The host named it `seed_b_committed=false` → `PREPARATION_FAILED` (preserved).
**Attempt 3** (`…-od2b`) could not even prepare: 39,936 bytes of save owed with the drain refused while the transport
reads active (`PERS,drain,transport_active,256,256,13,0,2339,39936`), so the scoped clear was refused four times
(`Clear refused: deferred save temporarily blocked by transport_active; … save still owed`) and the run ended
`SHUTDOWN_INCOMPLETE_TIMEOUT`. This is the acceptance's own subject on hardware — an accepted stop that never
executes, leaving the interval live and the next preparation refused — and it is not a host timeout artefact. The
campaign is blocked at 1 of 3 od passes with the consecutive count reset per the plan; clearing the device state
needs a recovery reset, which the plan forbids the harness from doing automatically for repeat qualification.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — S4f.11 — interaction latency measured per class from device rows; two classes named open (Phase 2).**
New measurement owner `hitl.interaction_latency` plus `scripts/hitl_interaction_latency.py`: acceptance → execution
from the device's own rows only, a family's own event time used where it carries one (`stop_state`'s `at_us`, never the
ring's flush prefix — measured 169.5 ms apart on this leg), pairs bounded by their own interaction, and a class the
capture does not exercise reported as a named gap rather than a number. Measured
(`captures/s45-odleg1a-20261002-163640-od`, fresh boot, image `ae591d95…`; `captures/s4f20-odundo-10-3` for undo):
stop overdub **106 µs / 87 µs** (within "microseconds" — the same service turn); record/overdub start **103 µs**
(informational: Revision 17 declares no numeric arm deadline); switch route **177.7 ms** and capture handoff
**645.2 ms** (both exceed S4b's 16–31 ms forward target, inside today's 2,000 ms bar unit); undo/redo **47 / 52 ms on
a warm root** (the ~1–2 s figure is declared for a *cold* root); load loop **1.554 s dispatch / 1.673 s complete**
for the leg's own slot (the 64-slot boot restore's 51.5 s maximum is the restore queue). Three exceedances reported
as events with their owner named and **no budget adjusted**. **Open:** cold-root undo/redo (no registered scenario
drives a cold reconstruction) and the queued-playback switch S4b actually tunes; S4f.11 stays unchecked.
Verification: `test_interaction_latency` **19/19**, accepted-sequence **170/170**, nine neighbouring suites
**194/194**. [LATENCY.md](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/LATENCY.md).

**2026-10-02 — Phase 1d — the overlap wording is pinned: a serialized acceptance run is NOT_EXERCISED (DEC-111).**
The ruling the earlier plan owed, taken on evidence rather than preference: the overlap link stays required in **every
counted pass** for the od and mixed legs, and a run whose successor is admitted after the predecessor released is
`SETUP_INCOMPLETE` in the setup class — preserved as operational evidence, never a pass, and it **breaks** the
three-pass streak. Decided because the bench demonstrably produces the overlap (this leg: B admitted 0.637 s into
A's 1.523 s window), so a serialized run is an arrangement finding and no timeout, window or budget is widened for it.
Recorded in `DECISION_LOG.md` (DEC-111 — a serialized acceptance run is NOT_EXERCISED), `S4F-CLOSURE-PLAN.md` §5, the
S4.5 — P3 task note and the S4.5 evidence file. No firmware, no requirement change.

**2026-10-02 — S4.5 — the overdub leg passes on device; the "missing overlap" was the oracle reading A's *seed* interval.**
Phases 1a/1b of the close plan. The leg (`captures/s45-odleg1a-20261002-163640-od`, image `ae591d95…`) was driven
with `--boot-reset --no-upload` through the registered scenario and is **`ACCEPTED_SEQUENCE_PASS`** with a sealed
bundle (`acceptance-bundle.json`, replay-verified digest) — fresh boot, identity pinned, no ring loss, content
parity for **both** slots (slot 4 = 64 rows = 32 record + 32 overdub, slot 5 = 34 = 32 + 2, storage ⇄ effective
equal), timing owner clean, teardown complete, and every link green: L1 (`reentry,bound,1770,3,3,0`), L2, L3
(`reentry_window_open,3,1770,2304` at 48.013 s), L4 (`canonical_release,3,32,2304,8` at 49.536 s), L5 (B admitted
48.649 s, i.e. **inside** A's window), L5b (capture-switch route), L6, L7, `overdub_interval_identity` and the
measured window. **The previous "no overlap" reading was host-side, not the arrangement:** the preparation seeds A
and B with records, a record stop opens a window and retires in its own right, and the oracle's L3/L4/L5 took the
*first* slot-wide match — the seed's rows (`reentry_window_open,3,1994` 35.452 s, `canonical_release,3,2`
42.839 s) — then bounded B's start against the seed's release. L3/L4/L5 now read A's **own leg interval** (floored
by its start admission; L4 from the interval-qualified release, with no slot-wide fallback), red-proved (three new
tests fail under the reverted selection, and the same device log re-grades `SETUP_INCOMPLETE` naming the seed rows)
and restored byte-identically. Phase 1b (content): the overdub leg's A and B content is clock-paced and **named**
when it never arrives (`OVERDUB_CONTENT_UNCONFIRMED`) instead of a blind 20 ms send that the note-length policy
drops (7.7 ticks against `DEFAULT_NOTE_MIN_LENGTH_TICKS = 8`). Verification: `test_accepted_sequence` **170/170**,
corpus **3/3**, scenario **40/40**, nine neighbouring suites **194/194**; `od-13`'s L3/L4/L5/identity re-read
identically. `deadline_safety_proven` remains false. **Still owed for S4.5:** S4f.11 — interaction latency matrix,
the three-consecutive-pass campaign with sealed bundles for both legs (record 1 of 3), the serialized-vs-overlap
wording decision, and closeout.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — DEC-110 — a contentless slot keeps no persisted payload: implemented, natively pinned and device-verified.**
The owner chose the destructive-route option for the last S4.5 blocker, and it works on hardware. `StorageManager`
now owns a bounded per-slot payload removal; the explicit CLEAR of a card-backed slot removes its body and reports
it, and the workspace save neither writes nor keeps a payload for a slot with no content (a live capture is never
"no content"). Verification: `test_current_set_storage` **14/14** with the new policy case red-proved; capture
build SUCCESS with RAM1 unchanged (`code 391,944 + padding 1,272`, no new state); image `ae591d95…`. Device
`captures/s45-clearfix-20261002-160609-rec3` (authorized track 8 slots 4/5): preparation reads `ALREADY_EMPTY`
for the emptied slot and the record leg drives end-to-end; the oracle passes every lifecycle link (L1–L7,
window, identity, timing, teardown) with **only** the content-parity link failing because the recipe's B note is
sent blindly before B's take starts (root-caused, fix identified). Also this session: host idle-read and record
oracle fixes (suite **166/166**, scenario **40/40**). S4.5 — accepted-sequence acceptance stays open.
[Evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — S4.5 — accepted-sequence acceptance: three root causes pinned; two fixed, one needs a decision.**
**A (host, fixed):** `require_idle` read only the arming rows, so a target the device had already released stayed
`armed` (device: `DISP,4,ARMED` 22.335 s → clear's `DISP,4,STOPPED` 23.490 s). `_slot_ready_state` now consumes
the slot-scoped `DISP` state family, orders by device clock with unclocked rows ranked live-by-construction, and
keeps file position as tie-break only. **D (host, fixed):** the record oracle's L5b sliced by file position and
missed the timestamped carrier (102.841 s at line 3508 vs A's stop at line 11075), and L7 had no passing branch
and never read the driver's gesture report. Both fixed; the existing record capture now re-grades
**`ACCEPTED_SEQUENCE_PASS`**; suite **166/166** plus scenario **40/40**. **B (device, needs a decision):** the
authorized target cannot be prepared — its card body holds *empty* content (`RRQ,7,4 → terminal,committed` yet
`k=0,a=0,l=0` after a fresh boot), the census maps payload presence to populated, and the clear gesture's
predicate (`Loop::hasData()`) is RAM-only, so the long press is a silent no-op. All 64 slots carry a verified
payload; there is no census-empty slot and no per-slot body-delete command. S4.5 stays open; remaining: fresh-boot
legs once a clearable target exists, `accepted_stop_finalizer_wait`/busy-owner evidence, S4f.11 latency numbers,
the three-pass campaign.
[Root causes and evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — Same-slot record finalization stall fixed natively; HITL qualification remains blocked.**
Record admission retained an old frozen rewrite binding after record → retire → clear → record, leaving
the second stop in ReservedClosure. It now reuses overdub admission's guarded scratch reset. The production
shared-finalization fixture red-proved the stale binding; focused native **58/58**, host preparation **26/26**,
capture build/upload successful, RAM1 padding **1,272 B**, image `0a7c89f9…`. Host preparation also corrects
occupied→empty inversion and aborts on a missing seed release. Pre-fix hardware showed a pending finalizer,
heap free **144 KB** and allocation refusals **0/0B**, not a heap-exhaustion signature. Post-fix registered
HITL stops before the verification window on empty-RAM/present-SD and armed-target preparation evidence;
all runs shut down cleanly. S4.5 — accepted-sequence acceptance stays open, including record-oracle
correlation and interaction-latency evidence. Full suites skipped at user request. No commit.
[Fix, checks and device paths](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-02-s45-preparation/FIX.md).

**2026-10-02 — Continuous-runtime acceptance is linked across the two active OpenSpecs; not proven.**
The runtime change owns the authoritative workload/timing requirement and combined acceptance; the windowed
change owns its bounded publication contribution and explicit note-preserving undo/redo handoff integration.
See DEC-109 — continuity acceptance ownership and the [runtime continuity requirement](../../openspec/changes/runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md#requirement-continuous-runtime-operation-has-one-end-to-end-acceptance-contract).
All new implementation/evidence tasks remain unchecked. Capacity refusal, fault silence and STOPPED recovery
cannot count as normal continuity success; existing safety policies are preserved. Documentation-only update:
no firmware, native suite, build or device run; numeric limits and end-to-end proof remain outstanding.
Verification: both active changes pass `openspec validate <change> --strict`; added relative links/anchors,
new unchecked task IDs and whitespace checks pass. No existing completed task was changed.

**2026-10-02 — Next closure rerun `captures/s4f13-closure-rec7/`: record leg drove successfully but is not acceptance evidence.** The direct B-stop execution path is now bounded by `RECS` or the firmware's direct `Recording stopped @ tick` row; forced-boot runs without a fresh boot banner fail fast before census retries. Rec7 used authorized track 8 slots 4/5, no upload, fresh boot, complete shutdown, and no critical capture loss. The official record oracle is `INSUFFICIENT_EVIDENCE`: B execution is present, but route attribution, one-press correlation, measured window, content parity, timing report and teardown bundle are absent. S4.5/P3 and S4f.11 remain open.
**2026-10-02 — Next closure slice: seed-release gate corrected; overdub drives, record remains blocked.** The accepted-sequence preparation now treats a seed as settled only after its slot-scoped `canonical_release_interval`, not an earlier `PERS,result` persistence receipt. Host suite remains **155/155**, and `teensy41-capture-serial` builds/uploads with RAM1 padding **1,272 B**. Same-boot campaign `captures/s4f13-closure1/` was stopped honestly at the record leg: the device emitted `Loop 5: Record start carried on the switch`, but never executed B's slot-scoped `RECORDING`/`RECA` path within the bounded wait; later rows show only A's record execution, so result `B_RECORD_START_UNCONFIRMED`. Isolated overdub retry `captures/s4f13-closure-od2/` now reaches `DROVE_LEG` after the gate fix, with complete shutdown. No new S4.5/P3 or 6.3 claim; record execution and full content/timing/identity bundle evidence remain open.
**2026-10-02 — Mixed admission issue fixed in the driver and device-verified.** The mixed flow no longer applies the 350 ms slot-tap pacing delay after `b_switch`, because the next gesture is the record button; that delay pushed B's quantized overdub admission past A's finalization window. Focused mixed flow tests pass **4/4** and the full accepted-sequence host suite passes **155/155**. Fresh authorized capture `captures/s4f13-mixed-fixed/` on track 8 slots 4/5 now has B admission at device time `81798945` before A slot-4's release at `82205994` (about 407 ms overlap), complete shutdown, passing content parity for both slots, and final verifier **`ACCEPTED_SEQUENCE_PASS`**. Timing is overflow-free and call-site clean; `deadline_safety_proven=false` remains an evidence gap. This closes the mixed admission issue, but not the three-consecutive-attempt S4.5/P3 campaign or S4f.11.
**2026-10-02 — Fresh mixed HITL capture completed on authorized track 8, slots 4/5; acceptance remains open.** Firmware `teensy41-capture-serial` uploaded successfully (git `2ab2f7d10ec0ab5dff2f5690101869e0bb7f33d6`, image SHA-256 `be3e24c73db57d58dd44c2f544991bb94203bf59d515e29f69234ddbe3ade84e`). Capture `captures/s4f13-mixed-fresh2/` drove A record, the capture-switch, and B overdub with complete shutdown. Final verifier result is `SETUP_INCOMPLETE` solely because B's admission (`DIAG,reentry,admit` at device time `47000052`) occurs after A's `canonical_release` (`46844653`); the required B-after-A-release overlap was not exercised. Content parity passes for both slots (`inventory_equal=true`, `storage_matches_effective=true`); timing is overflow-free with the expected MIDI call-site set, but `deadline_safety_proven=false`. No S4.5/P3 or S4f.11 claim; the populated-successor scheduling contract remains unchanged pending an explicit runtime decision.
**2026-10-02 — Mixed evidence attribution repaired and capture build restored.** The accepted-sequence oracle now filters legacy slot-based retirement rows after B’s own stop terminal/binding/start edge, so reused-slot seed releases are not counted as a second B retirement; interval-qualified rows remain preferred. Focused mixed oracle coverage is **9/9**, full native is **2,671 cases / 2,664 succeeded / 7 skipped**, and `teensy41-capture-serial` builds with RAM1 padding **1,272 B** (432 B above the 840-B floor). Regrading `s4f13-mixed-rerun-20261002-082833` now shows one B execution/retirement; it remains `SETUP_INCOMPLETE` solely because B starts after A release. No upload or S4.5/P3/S4f.11 claim. The post-release overlap is a real conflict with the existing populated-successor switch contract and needs an explicit runtime decision before behavior changes.
**2026-10-02 — Mixed HITL rerun `s4f13-mixed-rerun-20261002-082833` is operational but non-qualifying.** Registered `STIM-BASE-2-MIXED-01` on authorized track 8 slots 4/5 completed with fresh boot, pinned image `16aff0e2…`, MIDI-clock A content (16 notes, 0 clock-reader gaps), complete shutdown, no Tier-A ring loss, and clean timing gates (`overflow_free=true`, `call_site_gate_ok=true`, `tier_a_degraded=false`); one diagnostic `RING,shed` row was non-blocking. Mixed content parity passes for both slots with complete dumps. Direct mixed analysis is `SETUP_INCOMPLETE`: B has one terminal but two retirement rows, and B starts after A’s release, so the required overlap and one-stop gates fail; `deadline_safety_proven=false` remains an evidence gap. S4.5/P3 and S4f.11 remain open.
**2026-10-02 — Mixed-evidence repair slice implemented; firmware gate still blocked.** Record retirement now arms the diagnostic A-slot release marker, the mixed oracle uses device-time ordering for stamped route rows, and stopped slot-specific content dumps plus `mixed_content_report.py` cover A/B parity under the existing evidence adapter. Mixed oracle tests **7/7**, full native **2,664/2,671 with 7 skipped**, strict OpenSpec valid. Clean capture build fails RAM1 (`code 407,448`, `padding 18,536`, free locals `-26,560`), therefore no flash/HITL qualification was performed. S4.5/P3 and S4f.11 remain open.
**2026-10-02 — Rerun `s4f13-mixed-20261002-19` confirms operational flow only.** Authorized track 8 slots 4/5 reached `DROVE_LEG`, complete shutdown, A record, capture-switch, B admission/stop and B `canonical_release` on image `6875c6cc…`; no critical loss, `overflow_free=true`, and `call_site_gate_ok=true`. The verifier remains `SETUP_INCOMPLETE`: its fresh-boot position gate misses the banner, A slot 3 has no release edge (only B slot 4 releases), route ordering is unresolved due buffered `#CAP` rows, and A’s device-clock content has one reader gap. S4.5/P3 and S4f.11 remain open.
**2026-10-02 — Fresh mixed retry `s4f13-mixed-20261002-18` remains non-qualifying.** The authorized track 8 slots 4/5 run has a fresh boot (`boot_reset,crash=0`), pinned image `6875c6cc…`, `DROVE_LEG`, complete shutdown, MIDI-clock A content with 16 notes and no reader gaps, A’s finalization window, B admission/stop, and no critical-row loss. Timing-owner summary is clean (`overflow_free=true`, `call_site_gate_ok=true`, no issues); three ordinary `RING,shed` rows remain diagnostic-only. The official content verifier is `INCONCLUSIVE_CAPTURE`, so mixed content parity is not established. The mixed oracle is `SETUP_INCOMPLETE`: no A slot-3 release edge (only B slot 4 released) and unresolved device-time route ordering due buffered `#CAP` stop rows versus the direct human switch row. Evidence is under `captures/s4f13-mixed-20261002-18/` and `-playback/`; S4.5/P3 and S4f.11 remain open.
**2026-10-02 — Bounded critical-row journal implemented and device-verified.** The capture build now keeps up to 32 rejected/evicted critical rows in fixed DMAMEM storage and emits them through the existing serial stream during the stopped/non-timing-critical flush, with explicit `EVIDENCE,critical_journal_begin/end` fences and a counted journal-overflow condition. Native capture-ring **15/15**, full native **2,664/2,671 with 7 skipped**, capture build RAM1 padding **1,272 B**, and strict OpenSpec validation are green. Uploaded image `6875c6cc…`; authorized mixed capture `s4f13-mixed-20261002-17` completed and showed A’s `reentry_window_open`/`canonical_release` rows in the ordinary stopped drain, so the recovery journal was correctly dormant. The registered verifier remains `SETUP_INCOMPLETE` for missing content/timing reports and unresolved device-time release/route ordering; S4.5/P3 and S4f.11 remain open.
**2026-10-02 — MIDI-clock-timed mixed retries completed but did not qualify S4.5/P3.** Fresh authorized track 8 slots 4/5 captures `s4f13-mixed-20261002-14` and `-15` used the updated A-record path: A emitted 16 and 32 notes respectively from `MidiClockReader` (`clock_source=device_clock`, 16th grid, no reader gaps), with complete `DROVE_LEG` reports and clean shutdown. Capture `-14` lacked A's deferred-finalization window/release evidence. Capture `-15` showed real overlap by device timestamps (B admission before A's undo-reset), but still lacked A's `reentry_window_open`/release rows and also lacked a fresh boot banner. The registered mixed verifier remained `SETUP_INCOMPLETE`; content-parity and timing reports were not supplied. No S4.5/P3 or S4f.11 checkoff is claimed. The remaining question is the firmware evidence shape for a record-A stop: `TrackOverdubLifecycle` emits `reentry_window_open` on the overdub lifecycle, while these record-A stops emitted only `RECS`/undo-reset evidence.
**2026-10-02 — HITL `s4f13-mixed-20261002-09` showed healthy device progress; host evidence wording fixed.** Direct retry cleared authorized slot 5, seeded B, committed the seed, and shut down cleanly. It stopped before the mixed leg because the host could not verify track 8 slot 4 from read-only `!LIB_OBSERVE` (`28 occupied / 36 unknown`, `device_settled=True`), not because the hardware looked failed. The preparation message now says this is a host evidence gap and names the destructive-clear safety reason. Targeted host regression passed; full host suite was skipped per user request. S4.5 — P3 acceptance and S4f.11 — interaction latency remain open.

**2026-10-02 — Host test correction; hardware retries skipped.** Restored the accidentally removed `test_ignored_empty_target_is_idempotent_success_but_another_target_is_not` declaration in `scripts/test_accepted_sequence.py`. The latest interrupted test command was aborted, not failed; the earlier missing-import errors were already repaired. Fresh full host rerun: **152/152 passed**, `git diff --check` clean. The host preparation candidate remains device-unverified: attempts `s4f13-mixed-20261002-07` and `-08` both reported `PREPARATION_FAILED` with complete shutdown; the subsequent human-row receipt change has not been device-run. Further hardware retries skipped at user request. S4.5 — P3 acceptance and S4f.11 — interaction latency remain open.

**2026-10-02 — S4.5/P3 retry remained preparation-blocked.** The registered `STIM-BASE-2-MIXED-01` path ran on fresh boot against authorized track 8 slots 4/5 with image `2ab2f7d1` (`eb2a6212…`), then stopped and drained cleanly. Capture `s4f13-mixed-20261002-06` reached census but failed before any leg gesture: the slot-5 preparation press produced `Loop 5: Toggle Play/Stop` / `Playback started` instead of a slot-selection `DISP` receipt, so the host refused to clear and did not continue. This is preparation evidence only; no new latency measurement, S4.5/P3 acceptance, or S4f.11 class is claimed.
**2026-10-02 — S4.5/P3 device acceptance attempted, not closed.** Authorized track 8 slots 4/5 received image `2ab2f7d1` (`eb2a6212…`). Fresh-boot mixed-leg captures `s4f13-mixed-20261002-02..04` reached preparation and one record-start path; device rows show `RECA`/`RID`/`RECORDING`, but the host receipt gate ended `A_RECORD_START_UNCONFIRMED`, and later preparation could not identity-select the repopulated slot 4. Scoped clear evidence is `s4f13-clear-slot4-20261002-01`. Retained record-start deltas are ~440 ms and ~463 ms only; no five-class S4f.11 matrix or S4.5/P3 acceptance is claimed.

**2026-10-01 — S4f.23 device-accepted: a 24-bar record's stop job now retires, and the overdub pass survives its wrap.** The wedge the silent-hang investigation diagnosed is fixed and verified on hardware (DEC-107, commit `75f5c4d4`, image `db95f8ab…`). Before: the record job published and then sat at `RecordPublished` with `pending=1` for 30 s (`build_active=1` on every poll) because the S4f.19 retirement wait is on a frozen-window build whose two writers both sit on other owners — the playback hot path and the maintenance owner's `serviceFirstRecordingBar`, gated on `TRACK_RECORDING` exactly — and the interval had left that state; every later stop on the track was refused (`stop_job_pending`), DEC-069 deferred that interval's wrap seal, and its pass was never sealed. The wait is now bounded in DEC-070's shape (grain budget with an unchanged build phase; progress resets it; the stale build is discarded before the raw range is released). After, on the same shape and slot (`captures/s15-24bar-03`): `state=STOPPED,pending=0,queue=0/4` with **0** parked rows, `Overdub wrap committed @ tick 18432` fired, no refusal, `DIAG,canonical_release,0,119,18432,8`, and `!CAPTURE_CONTENT` shows `rows,record,0,1148` + `rows,overdub,0,454` + `rows,overdub,1,114` = `effective` **1,716** — an exact sum, where before there was no overdub row at all. No crash, no hang. Host: new fixture red-proved (`Expected 1 Was 0`), full native **2,668 / 2,661 / 7 skipped / 0 failed**, RAM1 `padding 1,272` (floor 840). Honest: the run's verdict is `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on a ring overflow (the known shed), so the acceptance is read from the device's rows; the harness's stored-overdub-span oracle (`count=62/191`) stays unmet and unattributed, and the hang's abort site remains unidentified.
**2026-10-01 — REGRESSION FIXED AND DEVICE-VERIFIED: a 24-bar take crashed the device and then crash-looped it at every boot; the snapshot change set no longer allocates from the internal heap, and the restored library — the 24-bar body included — now boots.** Found while resolving S1.5: the take faulted at its **overdub wrap commit** (`captures/s15-24bar-01`: `Overdubbing wrap committed @ tick 18432` → reconnect → `boot_reset,crash=1`), after which the persisted body made every boot fault (~14 s cycles with the USB CDC dropping each time, each cycle's last activity `RRQ,4,0,…running` = **track 5 Loop 1**, exactly where the take was recorded). **Decoded:** `CrashReport` = **DACCVIOL, accessed address `0xB000`**, code at `0x600928B6` ⇒ `std::vector<SnapshotEventChange>::_M_realloc_insert`, `0xB000` = 45,056 = 512 × 88 = a vector with 512 elements and a **null `data()`**; the body-load path builds exactly that vector (`StorageLoopIoBodyFormatV1.cpp`, one `changes.events.push_back` per gathered event). **Why:** Teensyduino's `operator new` is `return malloc(size);` with no null check, while `std::vector` growth assumes a throwing allocator — a failed `malloc` on the fragmented internal heap (a ~45–180 KB request) hands the vector NULL and the growth copies into it (the first 32 KB of unmapped writes corrupt ITCM; `0xB000` is the first unmapped address). This one mechanism explains all three recorded `_M_realloc_insert` faults. **Fix (`cce0739a`):** `LoopSnapshotChangeSet` allocates its vectors through **`ExternalMemoryFirstAllocator`** (PSRAM-first; a **named FATAL** if both pools are exhausted), the body-load path **reserves** both vectors up front from the gather's known counts, and two **`static_assert`s** pin the types — reverting them **fails the build** (red-proofed, file restored byte-identically). **Device verification, all three approved steps (image `eca0b0139d864734ae5a3164126c20e18cbde60eb2c6b437df851126f988fc5a`):** flashed successfully (RAM1 guard `padding 1,336`, floor 840 met); a clean boot (`boot_reset,crash=0`, **no `CrashReport`**, one boot in 75 s, 71.9 s continuous uptime); `!RESTORE_WORKSPACE 5202` returned the library (`current.bad.5202 → current`, `checkpoints.bad.5202 → checkpoints`, four flags = 1); and on **two consecutive boots** the 24-bar body on track 5 Loop 1 **loads and reads back current** — `LIBOBS,S,t=4,s=0,…,l=18432,v=1152,kp=1,kr=1,kc=1` — with the library admitted (`post_restore_census … admission_complete=1 interactive_ready=1 undo_hydrated=1 transport_stopped=1`) and the observation complete over 64/64 slots with `restarts=0`. The block on large-content device acceptance is lifted; S4f.14 resumes. Evidence: [`evidence/2026-10-01-boot-crash-loop-24bar-take/FINDING.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-boot-crash-loop-24bar-take/FINDING.md).

**2026-10-01 — `runtime-working-set-ownership` FOUR GATES CLOSED: S1.5's refusal-N resolved by measurement, and with it S1 gates, S2 gates, the P2 release gate S4 gates and S4b gates (one long-take device leg; no firmware change, no flash).** **S1.5's third clause — "the smallest N at which a 24-bar overdub is refused" — does not exist inside today's topology, measured at both ends.** *Device*, on the grown library (`captures/s15-24bar-01`, the authoritative `hitl_stop_interruption.py` wrapper with `--record-bars 24 --overdub-bars 24 --normal-stop`): the 24-bar record completed (`Recording stopped … length=18432`, exactly 24 bars) and **`Overdubbing started @ tick 1591`** — the 24-bar overdub reserve admission was reached and **admitted**, with **zero denials of any kind** (`refused=0/0B` on every census row, no `MEM_EXT_REFUSED`, no `op=overdub_start` denial), the pool falling 404 → 392 free chunks for the take; the same boot measured `post_restore_free_chunks = 412` with the walk flat at `512 → 480`. *Native*: the clause-3 fixture already records **admitted at every N up to 63**. So no refusing N exists ≤ 63 native and ≤ the device's current library, and the only device refusal in the vicinity belongs to the **record** admission at N = 56 (`Overdubbing started = 0`, the S1.3 hard-failure class — a different admission, already checked). The premise was removed by **S2b**: the per-committed-slot reservation is **exactly zero** on both legs, so N does not price the reserve at all. **Recorded rather than glossed:** that run's own verdict was `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on a single **`RING,shed,1`** — the diagnostic-eviction trap at 24-bar volume on a grown library — so the finding rests on the device's own rows, which carry it; a long-take leg needs its rows read directly. **Gate effect:** **S1.5 checked → S1 gates closed** (full native 2,667/7/2,660/0, RAM1 `padding 1,720` ≥ 840, red fixture, S1.1–S1.4 cited) → **S2 gates closed** (the target-ABI lease-size measurement + the class-budget cap derivation; `test_playback_prewarm` green) → **S4 gates (P2) closed** (S3 gates; reuse and stale-owner fixtures S4.0/S4.1/S4.1a/S4.2/S4.3/S4.4 checked; its three carried items — S2b.7(a), S2.2/S2.3's class budget, the device clause — resolved; S4.5 is explicitly outside the P2 gate) → **S4b gates closed** (S4 gates; the MIDI-boundary timing evidence green across repeated switches in `captures/s4b8-repeated-switches-04`; S4b.1–S4b.9 and the device-state fixes checked). **The chain now reads S0 ✓ S1 ✓ S2 ✓ S2b ✓ S3 ✓ S4 ✓ (P2) S4b ✓ S4c ✓.** What remains: **S5, S6, S7** with their own work, S4.5's P3 acceptance, and the phases B–E device legs.

**2026-10-01 — `runtime-working-set-ownership` S1.4's third clause measured: the pass→chunk conversion, so the undo pin's chunk footprint is known (test-only; no `src/`/`include/` change, RAM1 line unchanged, no device action).** S1.4's residual was left open because the device telemetry it seemed to need would have cost padding at 64 B of headroom; the conversion is a **storage-layer property**, so it is measured on the real transfer path in the store's own host suite instead and the headroom was never spent. `test_pass_chunk_conversion_bounds_the_pinned_footprint` (`test_loop_event_store`, **43/43**) appends a deliberately unaligned pass (3 × `CHUNK_CAPACITY` + 5 events), detaches it through the production transfer (`detachChunksToCommittedChunkIds`) and asserts the pass holds exactly `ceil(E / CHUNK_CAPACITY)` chunks — the conversion, so the pin scales with the pinned **pass count** and never with loop length. Device sizes measured by cross-compiling the real headers (cortex-m7): `sizeof(MidiEvent)` = **20**, `CHUNK_CAPACITY` = **256**, **`sizeof(EventChunk)` = 5,132 B** (5,120 of events + 12 of header). So clause 2's device maximum — **8 pinned passes** — is at this density **32 chunks = 164,224 B (~160 KiB)**, about **6 %** of the **2,635–2,659 KB** arena retention clause 1 measured: the two clauses are compatible because the undo root's *arena* is the big term, not its chunk pin. **Red-proof:** dropping the tail chunk in the transfer (`tryAssignCommittedChunkIds(..., count - 1)`) fails the fixture at *"Expected 4 Was 3"* (and two pre-existing transfer fixtures), with `src/LoopEventStore.cpp` restored **byte-identically** (sha256 `74e2571447d59fb1…`). **Honest note on the fixture's teeth:** mutating `CHUNK_CAPACITY` (256 → 128) does **not** fail it, because the expectation derives from the same constant — and that constant is already pinned where it belongs (`test_append_fills_chunks_without_realloc_pattern`, *"Expected 2 Was 3"* under that mutation); this fixture's job is the **path** relationship, not the constant. **Full native 2,667 cases / 7 skipped / 2,660 succeeded / 0 failed**; `teensy41-capture-serial` SUCCESS with RAM1 unchanged (`variables 96,192 / ITAG 393,216 = code 391,496 + padding 1,720`, 880 B above the floor). **Gate effect:** **S1 gates is down to ONE item** — S1.5's *overdub* refusal-N (bracket still (2, 56]; needs a lower-N target or an explicit boundary decision) — and **S2 gates' own two clauses are now satisfied** (the lease-size measurement and the cap derivation, `test_playback_prewarm` green), so S2 gates waits only on that same remainder.

**2026-10-01 — `runtime-working-set-ownership` S4c CLOSED: checkpoint parity, the sibling's fixture list and the readiness-contract gate (contract only; no firmware, no `src/`/`include/` change, no build, no device action).** [`S4C45-CHECKPOINT-PARITY-AND-FIXTURES.md`](../../openspec/changes/runtime-working-set-ownership/S4C45-CHECKPOINT-PARITY-AND-FIXTURES.md) answers the two questions `S4C0-PUBLICATION-INVENTORY.md` §4 handed the sibling. **S4c.4 — checkpoint parity.** The checkpoint's root is the **published** root, read from the code rather than assumed: the persisted body is serialized from `loop.publishedSnapshot()` (`StorageLoopIo.cpp:1317`, `WorkspaceSave.cpp:671`/`:699`), the library observation reads the same root for its identity and presence test (`LibraryObservationSession.cpp:200`/`:222`), and the **restore edge installs the loaded body as that root marking the coverage outcome `kSnapshotCoverageCurrent`** (`StorageLoopIo.cpp:1363`) — the divergence the inventory logged as open question 3, recorded as the premise F inherits rather than a defect to fix here. Four requirements F must state: the published root stays the **immutable checkpoint authority for undo and save** (Revision 11's carve-out) until F proves otherwise; a lagging checkpoint is judged against the **current** revision and must be **named** in the S4c.1 AC-3/AC-5 vocabulary, never silently "current"; **undo parity and save parity are one requirement on two consumers**, neither consulting the window's lag — the **one-pass-late rule is a playback rule**, not a checkpoint rule; and nothing here removes a root dependency (`IMPLEMENTATION_CONTRACT.md` §4 keeps reactivation preparing "a whole-loop root until F"). **S4c.5 — the fixture list:** F-1 window identity/immutability, F-2 readiness per affected tick inside the §Sizing deadline (**16-31 ms; the deadline, not a bar count, is the oracle**), F-3 one pass late and only that, F-4 nothing allocates or blocks on the edge (the `SlotProvision` precedent), F-5/F-6 the checkpoint-parity pair, F-7 content parity across a window boundary, and **F-8 length-independent root residency** — the inventory's open question 2, expressed as a *residency* statement per slot plus content parity for the demoted state and **swept** over loop lengths (`design.md` row 2's "64 bars ≈ 4 bars"). Two ownership rules keep it from duplicating work: F-7/F-8 **extend the existing content-verification pipeline** (which the `S5 gates` line already owes the demoted case through), and none of F-1…F-8 restates `test_playback_prewarm` (that suite pins today's reserved-capacity formula). **The gate's wording reconciliation** is done as its §3: `ARCHITECTURE-REVIEW.md`'s P5 text says "the five gather sites/checkpoint consumers" while the tree has **nine call sites of two entry points across six files**, so "five" is a consumer-group count, and P5 now says so. **S4c gate closed with no claim of shipped windowed publication** — the unit, the coverage vocabulary and the revision identity are unchanged, no root dependency is removed, phase F's own artifacts/DEC/reassessment remain required before implementation, and `S7 gates` names exactly that dependency. `openspec validate --strict` valid.

**2026-10-01 — `runtime-working-set-ownership` S2b.7(a) SATISFIED on device, and S3 gates is CLOSED (device leg; no firmware change, no build, no flash).** The one mechanism that unlocked it: a **background clock thread keeps the transport running through the CLEAR**. The five earlier legs let the clock stream end first, so the transport lost its clock and the sounding track stopped just before the drain edge — which is why each of them showed two of the three conjuncts. With the clock held through the press, `captures/s2b7a-drain-shapeb-10.log` at **24.318 s** holds all of it at **one** edge: `Clear waiting for deferred save completion` (**a save owed**, and it stays owed) → `PERS,drain_progress,665,665,`**`2`**`,transport_active` — `flags` bit 1 is **`deferredSaveBlockedByActiveSlotLoadSd()` true at the drain entry** (**a restore holding the SD session**; adjacent `RRQ,4,4,running` at 20.26 s, `RRQ,4,5,running` at 20.34 s) — with the drain's own verdict naming **`transport_active`** (**another track playing**: `Selected track 5` → `Loop 1: Start Recording` at 20.221 s while the clear targets track 6 Loop 8), and the clear **refused by that name** with the slot left untouched. No external advancement inside any run. Stated rather than inflated: the third conjunct is carried by the **drain's named gate** plus the same-window activity of a *different* track, not by a note row (that run's ring shed twice). Corroboration: `-01` (bit 1 + progress moved inside the call), `-05`/`-06` (a restore terminal 3 ms before the drain; the drain naming `post_load_holdoff` and refusing by name — the named `TemporarilyBlocked` outcome the earlier slice had recorded as not occurring). **With this, every S3-gates requirement is satisfied** (S2b gates' slope sweep, S2.2/S2.3/S2.4's class budget, S2b.3's post-restore half, S2b.7 (a)/(b)/(c), the firmware-boundary suite, `test_playback_prewarm` unchanged, and the gate's device clause in the S4 gates holder census), so **S3 gates is checked** and the two remainders it carried since 2026-09-25 are discharged. Device state (not reversible): track 6 Loop 8's card payload was lost in `-02`, where the drain **completed** after that clear and persisted the cleared state; track 5 Loop 1 carries the scratch take; no other slot was cleared.

**2026-10-01 — `runtime-working-set-ownership` S2b.7(a) ARRANGED on device: the blocking drain's mechanism is now measured, and one edge holds all three conditions (five device legs; no firmware change, no build, no flash — one soft `!REBOOT` per leg).** S2b.7(a) asks for one drain edge carrying *a save owed* + *a restore holding the SD session* + *another track playing*. `evidence/2026-10-01-s2b7a-drain-edge-arrangement/EVIDENCE.md` records the mechanism the legs established: the blocking drain has exactly **two** reachable call sites (the CLEAR long-press in `MidiButtonActions.cpp`, and the save-new-set pre-flight in `SavedSetIo.cpp:365`), both gated on `hasDeferredSaveWork()` — which is why a leg whose take's save had already run produces **no row at all**; `deferredSaveBlockedByActiveSlotLoadSd()` is `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()` (`LoadLoopJob.cpp:855`), i.e. about a **session or an open SD file** and *not* about queued `RRQ` requests — a run can show hundreds of `RRQ` rows and still read `flags=0` at the edge; and the work that makes a session active comes from the **boot** sweep, so each leg needs a soft `!REBOOT` (`StorageManagerHitlSerial.cpp`, transport-gated, the port re-enumerates; the first attempt, run against a device left up from a previous state, produced `RRQ=0` and no row). **The edge that holds all three** is `captures/s2b7a-drain-shapeb-05` at 12.155–12.158 s: `Clear waiting for deferred save completion` (save owed) → `RRQ,5,6,0,0,terminal,none,committed` at 12.1581 s (a restore on track 6 Loop 7 completing **3 ms before** the drain) → `PERS,drain_progress,24,24,0,post_load_holdoff` → `Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left untouched, save still owed` — **the drain naming its wait, and the *(ii)* permitted outcome this item had recorded as not occurring** — with notes sounding across the edge (`MO,144,6,58` 12.022 s, `MO,144,6,48` 12.162, `MO,128,6,48` 12.277, `MO,144,6,49` 12.344). **Not closed, and precisely so:** in `-05` `flags` **bit 1 is 0 at the drain entry** (the session had already reached its post-load holdoff) while the literal bit-1 clause is observed in `captures/s2b7-drain-probe-01` (where no track played at that instant), and the sounding channel is the **clear target's own track**, so "another track playing" rests on the boot rows (tracks 4/5/7 resume playing) rather than a note row. The named next arrangement is a leg that records a **dense scratch take first**, leaves it playing, and only then clears a second slot inside the sweep window. **Device state (not reversible):** track 6 Loop 8 — the clear target the user authorized — no longer has its card payload, because `-02`'s drain **completed** after that clear and persisted the cleared state; no other slot was cleared, and no firmware was changed or flashed.

**2026-10-01 — `runtime-working-set-ownership` S2.2/S2.3/S2.4 CHECKED: each class is measured separately and §2's envelope is reported as it stands — the conservative sum does NOT fit, and the playback term is not what overflows it (measurement/report only; no firmware, no `src/`/`include/` change, no build, no device action).** [`S2C23-CLASS-BUDGET.md`](../../openspec/changes/runtime-working-set-ownership/S2C23-CLASS-BUDGET.md) charges the envelope `fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O + canonical + roots + undo + visual + standby + reserve`. **S2.2, each class separately:** `P` is the shared per-slot-runtime `RuntimePlaybackViewSet` (**831 KiB whole-take / 331 KiB windowed**, derived in `S2C1-PLAYBACK-WINDOW-LEASE.md`); `C` is **383 KiB** of *its own* lifetime class (b) — the six prewarms required during RECORDING (`canonicalPassInputs` 144, `frozenCompanionPasses` 96, `frozenCompanionApply` 66, `reconstruction` 64, `pairLookup` 10, `spans` 2) plus the transfer horizon, i.e. **not** a multiple of P and unmoved by P's derived capacity; `D` is **308 KiB** of class (c), capacity that must already exist at arm (`frozenCaptureCompanions` 80, `bridgeMap` 68, the two 80 KiB history vectors); `O` is ≤ **1 windowed P** per in-flight switch against S4b.2's *measured* **1 pool chunk** for the turn itself; **`I` is deliberately not sized here** (`Imax = 1` is §2's, but its byte figure belongs to the visual (S4e/S2.8) and undo owners). **Shared backing is charged once**, per §2's own rule: P, C and D all read the one view-set allocation. **S2.3, the envelope:** the pool is **8 MiB** and its ceiling is *measured* — the 24-bar heap runs refused `bytes=8192` at `live=8527KB`, and an allocator `FATAL` followed `external live 8144 KiB / 1176 blocks`. The anchors are device figures: boot `live` **1,801 KB** (1,949 settled) on a full library with `canonical ≈ 0` because committed slots are descriptors only (S2b), and **6,505 KB** settled at N = 8 with one take's runtime resident (S4 gates census), margin 1,687 KB. **Holder maxima are source-derived and their guards verified to exist:** `Cmax = Dmax = Omax = 8` via `Track::hasLiveCaptureInterval`, `Track::hasPendingDeferredOverdubStop` (one admitted stop per track — the 32-descriptor ceiling is explicitly *not* evidence of 32 finalizers) and `SlotStateMachine::hasPendingSlotSwitch` (one inbound preparation per track, replacements retiring their predecessor); `Imax = 1`. **The conservative sum — every class at its maximum at once — is 12,626 KiB against 8,192 KiB: it does NOT fit, over by 4,434 KiB** (20,626 KiB with whole-take `P`). **Reported, not resolved**, exactly as §2 instructs ("do not shrink cardinalities silently, claim a proved N, or pre-provision extra finalizers"), and for a structural reason worth keeping: `Cmax*C + Dmax*D + Omax*O` = **8,177 KiB** is the whole pool on its own, so the conservative sum cannot fit at **any** N — it is not the playback term that overflows it. **The representative cases fit with margin:** N = 8 sounding 4,449 KiB; + 1 capture 4,832; + 1 finalizer 5,140; + 1 in-flight switch 5,471 (margins 3,743 → 2,721), and the census's own 6,505 KB settled is consistent in magnitude. **S2.4, the capability:** today is **N = 8** (the design's stated capability, not a derived bound); the measured margin buys **≈ 13** sounding slots at today's windowed `P`, while **N = 16 alone would take 5,296 KiB** and 32/48/64 would take 10,592/15,888/21,184 KiB — unreachable **for the playback term** at today's ceiling; at the **derived** window `P` (27.9 KiB) the same targets cost **447/894/1,340/1,787 KiB**, so the playback term stops being binding and `N` is then bounded by `canonical`/`roots`/`undo`/`visual`/`standby`/`reserve`. Recorded as **provisional, bounded by phase F**. **Owed, named rather than claimed:** §2's own validation (*simultaneous live capture/finalization on **separate tracks**, switching on multiple tracks, target replacement during preparation — a single-track trace cannot prove the device budget*; the nearest evidence is the one-track S4b leg), the terms owned by other stages, the product limit, and `I`'s byte figure. **Verification:** no code changed, so no red-proof and no build apply; the class separation is already pinned where it lives (the lease's role vocabulary and its S3.1/S4.4 fixtures) and is cited rather than duplicated; `openspec validate --strict` valid. **S3 gates now rests on ONE item: S2b.7(a)'s third conjunct** (a track playing at the same blocking-drain edge — an arrangement, not a wait).

**2026-10-01 — `runtime-working-set-ownership` S2.1 CHECKED: the playback window lease is derived from the window instead of the 2048-event ceiling, and the `P` term is measured on the target ABI (derivation only; no firmware, no `src/`/`include/` change, no build, no device action).** S2.1 is the promotion of **RC-MEM-2's outstanding item** ("per-slot bundles are sized by the representable ceiling, not by the window"), and it is the term the budget multiplies by the derived N; it was blocked on phase F's contract interfaces, which **S4c.1 landed earlier this session**. [`S2C1-PLAYBACK-WINDOW-LEASE.md`](../../openspec/changes/runtime-working-set-ownership/S2C1-PLAYBACK-WINDOW-LEASE.md) records the derivation. **The composition:** `P` is one `RuntimePlaybackViewSet`, and the code already computes its total (`fixedExternalBytes()`, pinned by `test_runtime_view_set_prewarm_has_fixed_capacity_and_no_growth`), so the derivation is about its **inputs** — and those are measured on the **target ABI, not the host**: `MidiEvent` carries a `sysexData` pointer, so `sizeof(MidiEvent)` is **20 B on cortex-m7 and 32 B on x86-64**, and a host-derived figure would inflate every `P` figure by **34 %**. The sizes come from cross-compiling the real headers with the project's own toolchain (`arm-none-eabi-c++`, `-mcpu=cortex-m7 …`, the Teensy framework MIDI library), each value read out of the compiler. Result: **35 B/event** per view (the design's own figure, now verified on device), **29 B/event** for capture staging, **331 KiB per holder** windowed-only (the long-loop shape RC-MEM-2's release path produces) and **831 KiB** whole-take reserved. **Four exact cross-checks against existing authority figures verify the arithmetic:** `design.md`'s 35 B/event; `ExecutionBounds.md`'s views+bases **350.0 KiB**, bridges **360.0 KiB**, `captureStagings[2]` **61,952 B each** (59,392 + 128 × 20) and `canonicalPassInputs` **147,456 B at C=2048**. **The derivation:** capacity is the events the *resolved window* may hold (`eventsPerBar × windowBars`, at the design's ~72/bar typical and 4,096 refs/bar ceiling), not the global 2048 — and it is `min(declared product limit, windowEvents)`, because at the ceiling density a 2-bar window needs **8,192** events, *more* than today's ceiling, so the window sets the shape and a declared limit must set the size. A 2-bar window at typical density is **28.6 KB**, ~**12×** smaller than today (1 bar: 20×), which is what makes `N × P` affordable rather than merely paid; and length independence becomes a property of the formula, since nothing in it references the loop length. **One coincidence is named rather than conflated:** `design.md` Revision 11's "~325 KB per loop, ~2.6 MB at N = 8" is the **`N × whole-root`** term — the published snapshot root, a *different* structure — and its closeness to this one (331 KiB) is exactly why the two must not be treated as the same term. **Owed and named:** the **product limit** value (what one interval may capture — the decision RC-MEM-2's own record called "the decision to take"); **`N`** itself (S2.3/S2.4, provisional per phase F); and C3(a)'s per-slot saturation claim (`committedEvents + intervalHorizon`) with the `C`/`D`/`I` classes, which are **S2.2/S2.3**'s measurements. **Verification:** no code changed, so no red-proof applies and no build or upload was needed; the formula this derivation sizes recomputes from the `sizeof`s, so a capacity edit cannot silently change the figure without that pinned fixture seeing it. `openspec validate --strict` valid. **S3 gates therefore rests on two items:** **S2b.7(a)**'s third conjunct (a track playing at the same blocking-drain edge — an arrangement, not a wait) and **S2.2/S2.3**.

**2026-10-01 — `runtime-working-set-ownership` S4c.1 CHECKED: the interval/window readiness and immutable-currency specification for the sibling is written, and it unblocks S2.1 (specification only; no firmware, no build, no device action).** S4c.1's job is to turn `design.md` **Revision 11** ("immutable readiness at re-entry, not whole-loop publication") and its row 11 into acceptance criteria a sibling can be held to **without changing this change's publication unit** — and it is the item the remaining half of S3 gates was waiting on, because the `P` class *is* the playback window lease that S2.1 may create only once the phase-F contract's interfaces exist. [`S4C1-READINESS-CURRENCY-CONTRACT.md`](../../openspec/changes/runtime-working-set-ownership/S4C1-READINESS-CURRENCY-CONTRACT.md) states **nine criteria the sibling must accept**: AC-1 readiness is owed per **affected tick** before the playhead returns to it (whole-loop publication sufficient, never asserted as the unit); AC-2 a window is immutable and identified by `(slot, revision, interval)`, with no consumer read straddling a replacement; AC-3 readiness is a predicate whose failure **names its holder**; AC-4 currency compares against `playbackRevision` with **one** definition of current (`publishedSnapshotIsCurrent()`, readiness being the merged window — `builtFromRevision == playbackRevision` **and** non-empty, read in `TrackPlaybackWindowBuild.cpp`) and **no second validity counter**, the existing coverage outcomes named as its cases; AC-5 the one-pass-late rule as the **only** stale authority, bounded at one pass, named when exceeded, and never blocking a consumer; AC-6 nothing allocates or blocks on a timing-critical edge, with the S4b.6/S4b.7 off-clock `SlotProvision` turn cited as the precedent to match; AC-7 the existing resumable machinery is the mechanism (row 28), a second publication scheduler being a recorded failure mode; AC-8 readiness receipts obey the two delivery rules this change's device work produced; AC-9 length independence is swept, not assumed. It also fixes the **four interfaces S2.1 needs** (window identity, the `P`-class accounting, the readiness predicate plus lag bound, the off-clock owner) and what stays **undecided** (the window size `N` = S2.3/S2.6; the publication unit itself = the sibling's own DEC/reassessment; S4c.4/S4c.5; S4e's envelope). **Every citation was verified live before check-off** — Revision 11, rows 11/12/16/28, §4 of `IMPLEMENTATION_CONTRACT.md`, the 16-31 ms jammed-switch figures, the readiness predicate's actual terms, and each named symbol — and the S3/S4-gates remainder lines that named "S4c.1 → S2.1" now record it as landed. `openspec validate --strict` valid.

**2026-10-01 — `runtime-working-set-ownership`: S2b gates is checked, S2b.3 and S2b.7's observables land, and S3 gates' remaining items are named (device + firmware instrumentation; image `a1f4e4c0…`-class, RAM1 unchanged at `variables 96,192`).** Work on the chain's earliest open gate. **S2b.3's post-restore half:** on a full library, `post_restore_free_chunks = 397` of 512 against the required 16 (`s2b3_condition_met = True`), readiness attested (`admission/interactive/undo/stopped/deferred_prewarm_idle`), the library walk **flat** (`480 → 480`, `credits=0` on all 17 rows), boot pool `live` 1801 KB recorded against S1.5. **S2b.7's two device observables had no row at all.** A host read-back was tried and **rejected on evidence**: calling the restore-state probe links a 4 KB USB receive buffer into DTCM (`RAM1 variables 96,192 → 100,288`), so the row is emitted *where the drain is* — from predicates and counters `SyncDrainSaveState.cpp` already uses, at **no RAM1 cost**: `PERS,drain_progress,<before>,<after>,<flags>,<verdict>` with bit 0 = `restoreProgressCounter()` moved **inside** the call and bit 1 = the save was blocked by the active slot load. Observed once as `795,801,3,ok` with a continuous `RRQ` restore sweep and no publication inside the window, which satisfies S2b.7 (b) and (c) and partially observes (a) ("another track playing" at that same edge is not simultaneous; the blocking drain is not the path a mid-take deferred save takes). **The slope sweep ("varying N across boots") is taken** with the repo's own `scripts/hitl_library_growth_sweep.py` on the authorized scratch range (track 6, slots 1..8, 4-bar takes, a verified reboot per step): eight boots, `reset_ok`/`banner` true and `reconcile_warnings=0` every time (the catalog-reconcile failure that stopped the 2026-09-24 attempt did not recur), and **identical figures at every step — `512 → 480` with the library walk `480 → 480` and `credits=0` on all 16 rows — while the library's committed-slot count moved between 35 and 44**. No failure point was reached, which is this gate's own expectation (its text keeps the 60-long-slot and total-residency gates in **S6/S7**), and `s13-sweep-05-full` (older image, 8 steps, zero non-zero credits) corroborates the shape. So **S2b gates is checked**; S1.5's first clause gains its device half (per-committed-slot reservation **zero** before activation) while its refusal clause (smallest N refusing a **24-bar** overdub — not this 4-bar sweep) stays open. **S3 gates therefore rests on two items, both named:** S2b.7(a)'s third conjunct, and **S2.2/S2.3's per-class budget**, which runs through **S4c.1 → S2.1** (the `P` class *is* the playback window lease, and S2.1 may not edit behaviour before the phase-F contract exists). Verification: full native **2,666 cases / 2,659 succeeded / 7 skipped / 0 failed**; RAM1 **unchanged** (the 4 KB pull was avoided rather than re-based into `ram1_budget.json`); `openspec validate --strict` valid. Evidence: `evidence/2026-10-01-s2b3-post-restore-census/`, `evidence/2026-10-01-s2b7-drain-edge/`, `evidence/2026-10-01-s2b-slope-sweep/`.

**2026-10-01 — `runtime-working-set-ownership`: two device-state defects fixed on the way to S4b.9's clause, and the clause itself observed on hardware (firmware + host tooling; image `a1f4e4c0…`, RAM1 unchanged at `variables 96,192`).** **(1) A corrupt head prefix no longer costs the ring.** `captures/s4b-gate-newimage-01` recorded `RING,head_bad,1,op=4,head=604322,tail=613846,used=9524,type=55,plen=12588,prefix=37322c313030…` — with the header layout (`CaptureRecordHeader` = `{type, payloadLen}` plus a pad byte) those fields are provably read out of *text*, with a valid header exactly **7 bytes later**, while `head + used == tail` still held: the structure was self-consistent, the bytes were not, and the blunt whole-ring reset discarded **9 524 readable bytes** and invalidated the capture, aborting an unrelated leg. The fix is a bounded resynchronization (`headResyncDropBytes`, ≤256 B scan, a candidate accepted only when the record after it is plausible, the loss counted and named `RING,head_resync,…`, the walk still draining the intact records, and the wipe kept as the fallback). Regression on the same three-switch leg: `captures/s4b-gate-newimage-02` has `wiped=0`, `head_bad=0`, `integrity: COMPLETE` and **two commits** with an empty warning list. **(2) A receipt no longer waits for the stop.** With the ring healthy the leg still refused at the successor gate; the device's own rows say why — `captures/s4b9-cross-track-18` has the `PERS,result` receipt 3.1 s (device time) after the record stop while the harness's 25 s wait expired, and `-20` shows a 120 s wait expiring with the row in the capture. The ring is **retained, not drained, under transport** by design, so a gesture that keeps a track playing throughout holds the receipt until after the stop — after its consumer has moved on. The one rare, latency-critical save-completion row is now emitted where it is produced when USB has room (`persistenceResultImmediate` via the `SC_PERSIST_RESULT` seam, ring fallback so it is delayed rather than lost). **(3) The gesture, and the clause.** After a cross-track switch the new track is selected but its loop is **not playing**, so a press there is a selection/toggle; the runner now starts the destination track's loop and hands the focus back (`--preplay-track-number`/`--preplay-loop-slot`, ledgered as `preplay_*`). `captures/s4b9-cross-track-21` then shows the clause in one run: `49.001 Loop 3: Queued playback switch` with the destination **cold** at the press (`capture_prewarm,track=4,slot=2,full=0,ready=0`) and **`51.992 LoopEnd playback commit track=4 1->2 start=0 len=2304`** 2.99 s later — the occupancy taken off-clock between the press and the commit, exactly as S4b.6's binding requires; one `canonical_release` for A's own stop (`0,24,1536,8`), `integrity: COMPLETE`, gates green, **zero** hold reports. **S4b.9 is checked.** Verification: full native **2,666 cases / 2,659 succeeded / 7 skipped / 0 failed** (three new fixtures: the corrupt-prefix resync, the no-boundary fallback, the direct receipt), **three red-proofs** each restored byte-identically, `teensy41-capture-serial` SUCCESS through the RAM1 budget guard. **Not claimed:** the corrupt head's cause is unidentified (the fix bounds and names the loss; `head_bad` remains the instrument), and the ring-resync notice is deliberately *not* in the host's capture-invalidating set. Evidence: [`evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md).

**2026-10-01 — `runtime-working-set-ownership`: the diagnostic no longer evicts the evidence — the telemetry flood that blocked S4b.9's device leg is fixed, and the host gate names the real cause (firmware + host tooling; image `014af85d…`, RAM1 line unchanged).** The user's requirement was that a device leg must work **while** the device does its large-library work, not only on a quiet device — and the measured competitor was not that work but its telemetry: a workspace save emitted a `PERS,work` row **per slice** (241 per slot body, **5 951 in one boot**, 6 967 in another) and the loop-snapshot tail write one **per step** (2 705). That volume made the capture ring shed rows the acceptance surface fences on (`RING,shed`, once `RING,wiped`); the sheds dropped record/phase rows, `SERIAL_INCOMPLETE` followed, the run aborted, and the successor-start gate reported *"no predecessor stop-completion receipt"* **while the receipt row was in the capture** (2.9 s after the stop, `captures/s4b9-cross-track-10`). **Fixed in two halves.** (1) `include/Utils/TelemetryCadence.h` bounds the cadence — a phase's first slice always logs (an acceptance surface asserts phase presence), the emitted duration is the **running maximum** so `deferred_persist_large_loop_bounds`'s max-over-rows stays a faithful bound, and the per-step tail rows are gated — wired into `PersistenceWorkItemPersist.cpp` (payload/finalize slices, reset per work item), `StorageLoopIoBodyFormatV1.cpp` (the tail emitter no longer treats `sigChanged` as a trigger: its signature includes the write offset, which made its interval irrelevant) and `WorkspaceSave.cpp` (the hand-rolled per-step row). The counters are `DMAMEM`, following the RAM1 budget's own recorded rule that mutable telemetry must not grow DTCM, so RAM1 variables stay **96,192 B** and no budget re-base was needed. (2) `scripts/hitl/legacy_record_baseline.py::_await_successor_start_receipt` now **prints the abort reason** when the run has already ended instead of returning silently into a caller that blames the receipt. **Measured on the same class of work:** `PERS,work` → **382** (from 5 951/6 967), per-slot `payload_slice` 241 → ~18, tail-write rows 2 705 → **29**, `RING,wiped` 1 → **0**, tier-A evictions 0, and run 16's evidence issue list holds only the base recipe's two known items — no lost record rows; the legs themselves now pass their preconditions (`admitted after stop receipt` in runs 13/15/16) and complete (`Selected track 5` mid-finalization, `switch sent: True`). **Verification:** full native **2,663 cases / 2,656 succeeded / 7 skipped / 0 failed** (the cadence fixture is the +1), `test_capture_transitions` **62/62**, two cadence red-proofs and one gate red-proof each restored byte-identically, `teensy41-capture-serial` SUCCESS. **Not claimed:** S4b.9's own clause stays `[~]` — with the destination track not playing, a slot press there is a selection (`Loop 2: Toggle Play/Stop`), so the `Queued playback switch` shape needs that track's loop to be playing; the prelude failure seen once (run 12) is recorded, not diagnosed; and the restore-retry churn is untouched. Evidence: [`evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md).

**2026-10-01 — `runtime-working-set-ownership` S4b.9 RUN: the cross-track switch is accepted while A finalizes, the accepted terminal stays A's, and the leg stays `[~]` because the device resolves the post-switch press by the *new* track's own state (host tooling + docs; firmware unchanged — image `cdab16f4…`, rebuilt byte-identical).** Seven attempts (`captures/s4b9-cross-track-01 … -07`) on the S4b.8 runner with `--switch-track-number 5` (track 4 as A's track, authorized this session; the destination was populated by a seed leg in the same boot, `captures/s4b9-seed-t5s2`). **Observed (attempts 01–04).** `Button press: Track 5` lands at 768.272 s *inside* A's finalization window — A's stop admitted at 762.693 s, released at 768.367 s — and the device answers `Selected track 5` at 768.292 s. **A's occupancy is not released by the switch:** the session has **one** `canonical_release` for A's slot, at A's own reentry-released edge and carrying A's own generation (`canonical_release,0,42,1536,8`). **The display/selection authority moves with the switch** (`LOOP_EDIT session refreshed for focus change`, `Edit state reset for new track`, and the geometry then reported is the new track's stored content — `Sent loop length CC feedback: 2 bars (1536 ticks)` — not A's loop). **The transport stays MIDI-continuous:** `overflow_free`, `call_site_gate_ok`, empty warning list in 01/04 (02/03 add `record_first_note_too_late`, a record-leg latency item). **Why `[~]`, with the cause measured rather than asserted:** the press *after* the track switch is resolved by the **new track's own state**, not as a cross-track switch — an empty destination becomes the **record carrier** (`Loop 2: Start Recording`, attempts 01/02) and the new track's **active** slot with content becomes `Toggle Play/Stop` + `Playback started` (attempts 03/04). So the `Queued playback switch` whose inbound occupancy S4b.7 provisions at a cold boundary needs a **non-active, catalog-filled** destination slot; the three attempts at exactly that (`--switch-loop-slot 3`) each failed **before** their intervention and never on the leg's own assertions — `firmware capture ring discarded unreadable content` (05), `record stop did not reach PLAYING` (06), and `overdub start refused: no predecessor stop-completion receipt` even at the 25 s wait (07). No attempt contains a `LoopEnd playback commit` row. Evidence: [`evidence/2026-10-01-s4b9-cross-track/EVIDENCE.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-s4b9-cross-track/EVIDENCE.md). **Also closed this session, by the requested read-only pass:** the restore-retry churn's *churn* half is confirmed and its mechanism narrowed — the body **does** load (the coordinate reached `terminal,none,committed`, and the boots after that show no `RRQ,4,7` row at all), a turn-budget expiry **cannot** produce the `LSV1 parse failed` warning (both parse layers return `MoreWork` at their budget/grain limits, and the read resumes across turns while a short read is a separate named warning), so the retried attempts were handing the parse bytes that failed validation and the only thing that ever ended it was the request completing when the device went idle — the attempt bound that should have ended it is dead code. **Still owed:** S4b.9's provisioning clause (the non-active, catalog-filled cross-track destination), the `S4 gates` half (S4.5/P3/S4f), and — routed to the restore-liveness area, no fix proposed here — the missing `loopSlotRestoreAttempts_` increment.

**Follow-up attempts 08–11 for S4b.9's clause** established that the destination also needs a *second* filled, non-active slot on track 5 (that boot's only filled slot there was its active one), so the route became seed-then-reboot; those attempts then failed **before** their intervention in two alternating device-state modes — the stop-completion receipt (09/10) and the capture-ring integrity gate (11) — measured against a **64-slot** boot restore batch whose SD traffic delays the receipt and whose log volume fills the ring, not against any assertion of the leg's. The same boots confirm the Loop 8 churn is gone (an ordinary queued/running/terminal `RRQ,4,7`, no parse failures).


**2026-10-01 — `runtime-working-set-ownership` S4b device session: the pool state was recovered, the S4b gate's timing half is now *aimed* on a dedicated three-switch run, the S4b.7 hold terminal has its first device observation, and two device-state findings are recorded (host tooling + docs; firmware unchanged — the image was re-flashed twice and both builds were byte-identical to `cdab16f4…`).** **[Recovery]** The previous session left the device refusing every capture append with `chunk pool exhausted (events=0 chunks=0)` while the census read `chunks_free=320`; a re-flash (fresh PSRAM arena) restored it (`chunks_free` 469–479, `refused=0/0B`, `live` 1.8–4.4 MB through the session). The contradiction is explained in [`evidence/2026-10-01-pool-free-but-append-denied/FINDING.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-pool-free-but-append-denied/FINDING.md): `freeChunkCount()` counts **ids** (`512 − poolUsed_`), while `LoopEventStore::allocChunk()` must **open the id's block** with a ~65.7 KB `extmem_malloc` — `used = 192` = 3 open blocks, so the 320 "free" ids were the 5 blocks that could never be backed. `chunks_free` therefore means "ids not in use", never "ids that can still be backed"; the S1.6b/S2 input is recorded, not a defect claim. **[The aimed run]** `scripts/hitl_s4b8_slot_switch_during_finalization.py` gained a press **sequence** (`--switch-loop-slot 2,3,2`, `--switch-interval-ms`) and an explicit `--state-sync-timeout-ms`, and the run `captures/s4b8-repeated-switches-04` fires three switches inside one finalization window's successor interval: the 37 s `PLAYING` interval contains all three presses **and** the hold report, with `overflow_free: true`, `call_site_gate_ok: true`, `timing_warnings: []` — so "across repeated switches" is now aimed rather than incidental ([`evidence/2026-10-01-s4b-repeated-switches/EVIDENCE.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-s4b-repeated-switches/EVIDENCE.md)). **The S4b.7 terminal fired on device for the first time:** the switches queued against destinations with **no runtime occupancy** (their own press rows read `Sent loop length CC feedback: length=0`) and the device reported the bound **once** — `slot_switch_hold_exceeded track=4 target=1 age=3073 ticks` (4 bars inclusive) — moving nothing, the switch still queued: DEC-104's report-not-decision half on hardware. The committed run contains **no** `LoopEnd playback commit` row, and the evidence says so: the destinations' on-demand restores were queued at the presses (68.2 s / 72.9 s) and served only at 105.2–105.4 s, i.e. after the run's final transport stop (every other coordinate's restore was queued 108.3–108.9 s and committed 110.9–114.8 s; the code names the same condition — `canRunBackgroundLoadLoopNow()` is false while `sequencerRunning` or while a track records/plays audibly). So the *timing* half is aimed and green, the *commit* half stays S4b.8's leg-03 evidence. **[A second device finding, and it blocked a leg]** The same session exposed a restore request that **churns without progress**: `RRQ,4,7` (0-based track 5, so **Loop 8**) 269 transitions in 31 s with 87 `LSV1 parse failed` warnings on this boot, 4 948 transitions over ~265 s on another (which ended in a device reboot), 690 transitions in a 12 s passive read after a re-flash (~427 serial lines/s at its worst). It cannot be bounded: `kMaxLoopSlotRestoreAttempts = 3` is read at `LoadLoopJob.cpp:83` and **nothing ever increments `loopSlotRestoreAttempts_`** (zero-initialised at `:72`; `markLoopSlotRestoreAttempted` records a *terminal*, it does not count), so the "bounded … named refusal" its own comment promises is unreachable. Measured cost to a leg: the same runner missed the harness's stop-completion receipt at the default 8 s state-sync wait (run 02, `overdub start refused: no predecessor stop-completion receipt`) and drove the full leg at 25 s (run 04), and one attach found the device not answering `!STOP_STATUS` while a fresh boot banner followed — the cause of that reboot is **not** attributed. Recorded, with the code path, in [`evidence/2026-10-01-restore-retry-churn/FINDING.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-restore-retry-churn/FINDING.md); it belongs to the restore-liveness area and **no fix is proposed here**. **[Still owed]** S4b.9 needs a second track (track 4 as its source) that this session's authorization did not cover; the `S4 gates` half closes with S4.5's P3/S4f acceptance; and the churn finding is the second of this session's pair (with S1.6b's census question) where a device-surface number and a device-surface admission disagree.

**2026-10-01 — `runtime-working-set-ownership` S4b.4 CLOSED: both open clauses land, and the undo clause's red-proof is explained rather than open (test-only; firmware hex byte-identical).** **(1) Undo, red-proved.** The two mutations that had survived were each only *half* a mutation: `TrackUndo::undoForLoop` gates on the slot **twice** -- the outer `entries[cursor-1].slotIndex != slotIndex` check and, inside the apply loop, the same comparison as a `break` -- so removing either alone leaves the gate intact and the driver still refuses (measured). Removing the gate **as a whole** fails the fixture at *"the retired slot's geometry must not be restored either"* (`Expected 0 Was 4242`). The double gate is itself recorded: a future edit that "simplifies" one of the two would not be caught by behaviour alone. **(2) Display, with its own fixture and red-proof.** The harness already keeps the display boundary observable, so its stub now **records** what it is told (`SlotChangeObservation{count, track, from, to}`) and `test_s4b_4_the_display_authority_moves_to_the_holder_the_switch_moved_to` asserts the identity pair the switch commit names (`invalidateForSlotChange(i, previousPlaying, getPreviewSlotIndex(i))`) after a **production press** -- the gesture is what moves the panel's preview slot, which the first version measured by driving the raw request instead and getting (outgoing, outgoing). Making the commit name the predecessor fails it. Focused suite **45/45**; full native **2,662 cases / 2,655 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with RAM1 unchanged and a byte-identical firmware hex. Record: [ARCHITECTURE-REVIEW.md](../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.4 — the undo route and the display authority on the holder that took over".

**2026-10-01 — `runtime-working-set-ownership` S4b.4's undo clause gains its fixture, and its red-proof is recorded as OPEN (test-only; firmware hex byte-identical).** `test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to` drives the production undo call shape (`TrackUndo::undoForLoop(track, track.getActiveLoop())`) after a switch moved the active slot to the inbound holder, with real stack entries: a **retired-slot** entry must be refused (inbound untouched, entry retained) and an **inbound-slot** entry must be applied **to the inbound loop** and consumed. Landing it required two link-closure symbols (the driver's geometry flush, fail-closed; `hasPendingGeometry`, answer-no) and two observable no-ops (display-cache invalidation, `admitSlotMeta`), plus **reclassifying one stub**: an applied undo restores published geometry and *notifies* the edit manager via `onGlobalGeometryRestored`, a route the earlier slice's fail-closed comment had assumed only a live edit session could reach — DEC-101's argument (an unreachable `OFF_PATH()` cannot have been reached by a passing fixture) applies, and the manager's session entries stay stubbed. **Red-proof: OPEN.** Two mutations of the driver's slot gate (the outer check and the inner one in the apply loop) each survived the fixture, so it pins behaviour but is not yet shown to have teeth; the same path does apply the inbound entry in step (2), so what needs explaining is why removing a gate changes nothing in step (1) here — the named follow-up, recorded rather than claimed. Focused suite **44/44**; full native **2,661 cases / 2,654 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 unchanged (`code 391,416 + padding 1,800`), hex byte-identical `cdab16f4…`.

**2026-10-01 — `runtime-working-set-ownership` S4b.4's matrix is attributed rather than re-asserted, and its two open clauses are named (documentation only).** S4b.4 asks for a fixture proving the **inbound holder** serves record/overdub/passes/undo/redo/display with no stop and no allocation at the switch. Four of the six are already this suite's subjects, so the entry now records *where* each clause lives instead of adding a fixture that re-asserts them under a new name: record (`test_a_cross_track_successor_starts_while_the_predecessor_finalizes`, `test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes`), overdub (`test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes`), passes (`test_the_successor_take_commits_with_content_after_a_transport_stop`), and the two clauses the entry adds -- **no stop** and **no allocation at the switch** (S4b.1's lane fixture; S4b.6/S4b.7's fixtures; S4b.2's ordering fixture). **Open and named:** (a) an **undo-entry round trip** on the slot a switch just moved to (what this suite asserts is the undo *route's* write and the guard's route, not a full entry round trip -- its natural home is the undo suites, which do not switch the active slot mid-sequence); and (b) **display** on the inbound holder (this harness has no assertion surface for it; the display suites do not switch slots). S4b.4 stays `[ ]` with the attribution in `tasks.md` rather than being closed by citation.

# Project state (execution context)

**2026-10-01 — `runtime-working-set-ownership` S4b.3 CHECKED: the switch lands on the quantization's boundary, and that boundary binds a prepared role (test-only; firmware hex byte-identical).** Both halves are measured rather than stated, on the two quantizations the firmware uses: `test_s4b_3_a_loop_end_switch_lands_on_the_loop_boundary` (`LoopEnd`, the playback-switch gesture's own) and `test_s4b_3_a_next_grid_switch_lands_on_the_start_grid` (`NextGrid`, the record carrier's). Each queues the switch **off** the boundary (`loopStart + 211`) and drives the production lane until the active slot changes, recording the committing tick: it is **not** the queue tick (the request is quantized), for `LoopEnd` the outgoing loop's playhead **wrapped** there (phase zero or below the previous tick's, read modulo the loop length because the effective tick is projection-domain), for `NextGrid` the tick is on the start grid (`commitTick % queuedStartGridTicks == 0`), and the role the boundary binds was **prepared before it**. **The cold half is measured at the tick:** the pool is identical immediately before and after the committing `updateAllTracks`, so the boundary binds a prepared role and allocates nothing — the property S4b.2/S4b.6/S4b.7 established by moving provisioning off-clock to the `SlotProvision` turn. **Red-proved twice, each restored byte-identically:** `LoopEnd` made to ignore the boundary fails the `LoopEnd` fixture (and, as a load-bearing control, S4b.1's lane fixture at *"the queued switch must not commit while the predecessor finalizes"*); an allocation added at the boundary fails **both** new fixtures at *"the boundary tick itself may not take or return pool capacity"* (8 chunks taken). Focused suite **43/43**; full native **2,660 cases / 2,653 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with RAM1 unchanged (`code 391,416 + padding 1,800`) and a byte-identical firmware hex (`cdab16f4…`). Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.3 — the boundary the switch lands on, and the cold half of it". **Next in this change:** S4b.4's inbound-role fixture (the inbound holder serving record/overdub/passes/undo/redo/display with no stop and no allocation at the switch), then S4b.1 `(d)`/`(e)`; S4.5 and the S4 gate close with S4f/P3; device legs stay owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.2 CHECKED: the inbound role is provisioned before the outbound retires, the overlap is measured, and two findings are recorded rather than assumed (test-only; firmware hex byte-identical).** The ordering is now asserted at the sequence level: with an **unoccupied** inbound pressed while the outbound holds a working set, the clock lane neither commits nor allocates nor releases (**the outbound's working set survives the wait** — a delayed or refused switch never leaves the track with neither role), the `SlotProvision` turn then takes the inbound occupancy while the outbound is **still held** (the overlap the S2 term accounts for), and only after the commit is the retired slot releasable, with the release touching nothing on the inbound. **Red-proved, restored byte-identically:** provisioning made to release the outbound first fails it at *"the outbound must still be held while the inbound is provisioned"*. The entry's other two clauses are **cited, not duplicated** — allocation-freedom is S4b.6/S4b.7's fixtures, and no-stealing is S4b.1's two "nothing stolen" fixtures. **Finding (a), DEC-106:** a **playback** switch's inbound carries **occupancy but no working-set lease** (`provisionSlotForSwitch` provisions with `prepareCapture = false`; the lease is a capture admission's, S3.1) — every earlier fixture asserting an inbound lease had got one only because the *test* prewarmed with `prepareCapture = true`, so the assumption would have been inherited silently; whether a switch's inbound must also hold a lease (and with which `WorkingSetRole` bits) is recorded for the architect instead of being invented at the last mile. **Finding (b):** the overlap's obvious unit is the wrong one — the outbound's own prewarm cost 14 event-pool chunks (it carries the capture horizon), the switch turn dropped the pool by **1**, and a probe provisioned the same way as the inbound dropped it by **0**, so part of that chunk is the turn's other work (merged-window build / prepared-pair publication); the fixture asserts the bound it can support and records the raw numbers, leaving the split to S2's budget accounting. Focused suite **41/41**; full native **2,658 cases / 2,651 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with the RAM1 line unchanged (`code 391,416 + padding 1,800`) and a byte-identical firmware hex (`cdab16f4…`). Recorded as **DEC-106**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.2 — the inbound is provisioned before the outbound retires, and the overlap is measured". **Next in this change:** S4b.3 (the switch as a MIDI-boundary cold admission boundary) and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; device legs stay owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.5's recorded guard gap is discharged: the accepted-but-unexecuted term now has its own fixture (test-only; firmware hex byte-identical).** The slice recorded, measured, that removing `hasAcceptedUnexecutedInteraction()` from the active-slot guard left the **entire** full native suite green. `test_s4b_5_an_accepted_unexecuted_interaction_routes_the_write_through_the_switch` closes it: the fixture builds the state that term and nothing else covers (a latch alone via `latchAcceptedStopForSlot`, with `isRecording` / `isOverdubbing` / `hasPendingDeferredOverdubStop` all asserted **false**, so no other guard term can route the write) and asserts the **route** through its effect, since both routes move the active slot: the capture-switch commit enables the target slot (and resets its playback state) while a bare write would leave the target disabled on a playing track — the "piano-roll notes with no MO" hazard the switch's own comment names. **Red-proved, restored byte-identically:** with the term removed the fixture fails at *"the accepted interaction must route the write through the switch commit, which enables the target"* — the mutation that once survived every native suite now fails one fixture. Focused suite **40/40**; full native **2,657 cases / 2,650 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with the RAM1 line unchanged (`code 391,416 + padding 1,800`) and the firmware hex byte-identical (`cdab16f4…`). Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.5 — one active-slot write authority, and the cancel that owns the trio"; `tasks.md` S4b.5 carries it too. **Next in this change:** S4b.2/S4b.3 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; device legs stay owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.5 CHECKED: the active slot has one write authority again, and cancelling a queued switch is one call that owns the whole trio (firmware; ITCM +128 B, no RAM1 variable).** Two defects, one stage. The active slot had four writers — the manager entry, two direct `Track::setActiveLoopIndex` sites, and an entry that would **move the slot underneath a queued switch** (two transfers instead of one) — and cancelling a queued switch was open-coded at five sites and done **partially** at several others: clearing only the state machine left the enabled-set replacement armed, and the next switch that armed no replacement inherited it and **wiped the enabled set**. **Built (DEC-105):** `TrackManager::setActiveLoopIndex` is the authority and `applyActiveLoopIndex` (new, private) is the **non-re-entrant commit point** that performs the write and its two side effects — Rev 21.3's split, so the capture-switch commit point can land a write without re-entering the switch it is committing; the guard gains **`hasAcceptedUnexecutedInteraction()`** (the accepted-but-not-executed state a switch's latch sits in before any job exists); a direct write **cancels** a queued switch rather than racing it; and cancelling is **one call** — `cancelPendingSlotSwitch(trackIndex)` owns the trio, and `stopPlayingTrack`, `handleTransportStop`, `queueBarPlaybackStart` (which had *forgotten* the selection hold), `updateAllTracks`' three cancel branches and `MidiButtonActions::discardQueuedSlotActions` all go through it. The playback-switch commit and the transport-stop remap land at the commit point, because both have already decided. **The minimal native harness is part of this contract:** `capture_storage_harness.inc` stubs the surface with a counter a boundary test asserts on, so the two new symbols are defined there *through* its counted stubs — a cancel still counts the same three slot-mutation calls, and the commit point counts as the write `setActiveLoopIndex` used to (two suites had failed to link the new symbols, which is how the harness edit was found). **Fixture** `test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_replacement`: a **replace-enabled-set** switch is queued by the production gesture, the **undo route's own write** lands (plus its playback-state reset, mirrored because without it the new active slot has no anchored playhead), then an **unrelated** switch is queued through a route that arms no replacement — the queued switch is cancelled, the write lands in one transfer, the pool does not grow across the cancel, the unrelated switch commits, and the enabled layers survive. **Red-proved twice, each restored byte-identically:** removing the cancel-on-write fails *"a direct active-slot write must cancel the queued switch, not race it"*; making the cancel **partial** (the pre-fix shape) fails *"no stale enabled-set replacement may reach an unrelated switch"* — the defect demonstrated, not described. **Recorded as a measured coverage gap:** removing the guard's `hasAcceptedUnexecutedInteraction()` term leaves the **entire** full native suite green (measured twice), so that contract-mandated term has no fixture yet; its reproducer is the named next step. Focused suite **39/39**; full native **2,656 cases / 2,649 succeeded / 7 skipped / 0 failed** with no ERRORED suites; `teensy41-capture-serial` SUCCESS, RAM1 `variables 96,192 / code 391,416 + padding 1,800 / locals 34,880` (floor 840), hex `cdab16f4…`, not flashed. Recorded as **DEC-105**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.5 — one active-slot write authority, and the cancel that owns the trio". **Next in this change:** the guard-term fixture above, then S4b.2/S4b.3 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; device legs stay owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.7's third slice: the demoted target commits when its slot has a published root, and a hold that cannot be satisfied is now bounded and reported (firmware; no RAM1 variable, no ITCM).** The two items the previous slice recorded as owed shared one cause: the merged window is gathered from the loop's **published snapshot root**, so a hand-committed fixture slot gathers **empty** (`GatherMissHoldEmpty`). Both are now closed. **(1) A published-root fixture exists:** `publishBarSpacedRootForSlot` installs a *current* root through the existing test seam (`Loop::storePublishedSnapshotForTest`) carrying the same bar-spaced content the slot commits — the state a **restored** slot has — and `test_s4b_7_a_demoted_target_with_a_published_root_commits_through_the_turn` drives a **24-bar** target (above `kMaxDetailedWindowBars` = 16 bars, i.e. the demoted windowed policy): the production press queues the switch with nothing taken inline, the clock lane does not commit, the `SlotProvision` turn takes the occupancy **and** builds the window, readiness becomes **true**, and the switch **commits**. So the entry's "demoted long loop … no dead switch" clause is discharged, and the turn's merged-window half is **red-provable** for the first time (dropping it fails this fixture; the same mutation left all 36 fixtures green before). Two details were measured, not assumed: the gather walks the snapshot's **event** membership, so the change set must carry events and not only notes (a notes-only root probes `gathered=1 events=0`), and the demoted window is two bars centred on the playhead, so root events must be spread across the loop. **(2) The hold carries its age/bound:** `SlotProvisionHold` (`kBoundBars = 4`, converted with `Config::TICKS_PER_BAR`, wrap-safe `holdExceeded`) measured from the pending switch's **own** `pendingQueuedAtTick` (no new timestamp), reported **once per episode** (one packed bit per track, cleared when the switch is gone), checked on the off-clock turn **regardless of whether that turn could make progress** — the case the bound exists for is a switch whose occupancy was taken and which still cannot commit. The terminal (`slot_switch_hold_exceeded`, naming track/target/age) is a **report, not a decision**: the pending switch, the latch, the enabled-set replacement and any retained start stay exactly as they were, so DEC-078 item 1 survives. `test_s4b_7_a_demoted_target_without_a_root_is_reported_once_at_its_bound` asserts no commit, switch retained, exactly one report at/after the bound, no re-report, and a second episode reporting on its own account. **Red-proved three times, each restored byte-identically:** removing the published root, removing the turn's build, disabling the age test. Focused suite **38/38**; `test_midi_service_drain` **37/37** (age-test edges pinned: unset sentinel, one-tick-short, inclusive bound, wrap safety, zero-means-unbounded); full native **2,655 cases / 2,648 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with RAM1 `variables 96,192 / code 391,288 + padding 1,928 / locals 34,880` (floor 840 — the mask fits existing class padding, so this slice costs no RAM1 variable and no ITCM), firmware hex `d3b9d2b1…`, **not flashed**. Recorded as **DEC-104**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.7 — the demoted target, the published root, and the bounded hold". **Next in this change:** S4b.2/S4b.3/S4b.5 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; device legs (S4b.8/S4b.9 among them) stay owed, and the hold terminal has no device observation yet.
**2026-10-01 — `runtime-working-set-ownership` S4b.7's second slice: the provisioning turn the binding corrections named is now the vehicle, replacing the reuse that shared a record with slot restore (firmware; ITCM returns to 391,288, the turn is FLASH, +120 B).** The first S4b.6/S4b.7 slice landed the right *contract* with the wrong *vehicle*: it reused the slot-restore prewarm request, a shared `(track, slot)` record a restore and a switch could consume from each other, and it reached a switch's provisioning only through the restore lane's gating. Rev 20.3/21.7 and Rev 22 §6 had named the vehicle instead — a **`SlotProvision` turn kind** — and the task text did not repeat it, so the correction was found by reading the corrections. **Built:** `DeferredIdleOwner::SlotProvision = 5` with `kDeferredIdleOwnerCount = 6`; the turn body is `TrackManager::servicePendingSlotProvision()`, dispatched from `runDeferredIdleMaintenance` **before** the all-stopped guard (Rev 22 §6's exemption, now structural: the turn returns before the guard), and it runs **both** halves — `provisionSlotForSwitch`, then `ensurePlaybackMergedEventsForSlot(targetSlot)`, the merged-window build that releases the long-loop pending hold. **The eligibility is the pending switch itself** (`MidiServiceDrain::slotProvisionNeeded(hasPendingSlotSwitch, inboundOccupancyPresent)`), so `requestSlotSwitch` marks nothing and no request record is left to be consumed elsewhere — the smallest state, none. The restore lane keeps its own provisioning call (the restore's occupancy need, and what `ensurePlaybackMergedEventsForSlot` used to do implicitly). **Tests re-pinned as the contract requires:** the drain suite's rotation, six-owner cycle, interleaved session-exit sequence and backlog sequence (`two_of_seven`, with the extra-Snapshot rule still riding `LoadAndDisplay`), plus pins that the turn's work is the pending switch and that `SlotProvision` does not depend on the visual cache; the S4b.6 fixtures now drive the real turn and assert it takes the pending switch, then stops being its owner. **Red-proved, restored byte-identically:** removing the turn's provisioning fails three fixtures (*"the retained switch must commit once the predecessor retires"* and both S4b.6 provisioning assertions). **Recorded as a measured coverage gap:** removing the turn's merged-window call leaves all 36 fixtures passing, because that window is gathered from the loop's **published snapshot root** and a hand-committed slot has none (the `GatherMissHoldEmpty` path) — the fixture now pins that harness fact as a tripwire and names the discharge path (the published-root fixture the demoted-target hold also needs, S4c/F). **Still owed:** the contract's age/bound on that hold. Focused suite **36/36**; drain suite **36/36**; full native **2,652 cases / 2,645 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 `code 391,288 + padding 1,928 / locals 34,880` (floor 840), firmware hex sha256 `257adab1…` — **not flashed** (the board runs the earlier `ceb79f0a…` image, flashed with authorization earlier in this session). Recorded as **DEC-103**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.7 — the provisioning turn the contract named". **Next in this change:** S4b.2/S4b.3/S4b.5 and S4b.4's inbound-role fixture; the published-root fixture that discharges both the demoted-target hold and the merged-window gap; S4.5 and the S4 gate close with S4f/P3; device legs stay owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.6 + S4b.7 PARTIAL: the switch's inbound occupancy now has one off-clock owner, the commit binds to it without taking it, and the *demoted* target shape is measured and owed (firmware; RAM1 +16 B ITCM, no new RAM1 state).** S4b.6 asked that the switch bind to the target slot's **occupancy** (its playback runtime) with no allocation on either hot path; S4b.7 named who may take that occupancy instead. **Landed:** `Track::ensurePlaybackMergedEventsForSlot` now reads `playbackRuntime.slotIfAllocated` and is a documented **no-op** when the slot has no runtime (so neither the clock-path commit nor a MIDI edge can take one), with a named predicate beside it, `Track::hasPlaybackOccupancyForSlot`; the commit branch in `TrackManager::updateAllTracks` carries the matching term — an absent inbound occupancy keeps the switch **pending** instead of committing into a slot whose runtime does not exist; and provisioning is a new idempotent off-clock owner, `TrackManager::provisionSlotForSwitch`, reached through the **existing** deferred request owner (`requestSlotSwitch` → `requestDeferredPlaybackPrewarm` → `main.cpp`'s deferred frame provisions then prewarms), refusing with its own `OperationKind::SlotSwitch` (`op=slot_switch`) because the contract on refusal is \"the switch stays pending\", not \"the request is dropped\". So no new lane, no new eligibility state, no new RAM1 state. **The entry's eligibility worry does not materialise, and the reason is not the predicate it named:** the prewarm consumer is **not** gated by `hasRealtimeIdleWork` (that belongs to the maintenance scan); what bounds it is the frame's shape — the prewarm slot sits after the frame's early return while a deferred overdub stop is outstanding, and the mark is cleared **only** by the take — so a switch beside a finalizing predecessor provisions on the first eligible turn *after* that finalization retires: a bounded delay, not a dead end (changing that ordering is S4b.2's own stage). **Fixtures** (both on the production press route): `test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock` (one bar) and `test_s4b_6_a_switch_to_a_multi_bar_target_without_occupancy_also_provisions_off_clock` (8 bars), driving the real lane pair (`takeDeferredPlaybackPrewarm` → `provisionSlotForSwitch` → `ensurePlaybackMergedEventsForSlot`). **Red-proved twice, each restored byte-identically:** replacing the occupancy term with `false` fails both at *\"the switch may not commit before its occupancy exists\"* (`Expected 0 Was 2` — the commit landed early into a runtime-less slot), and reverting the binding to `trySlot` fails both at *\"the MIDI edge must not provision inline\"*. **A measurement that changed the fixture, not the code:** a forced `TRACK_PLAYING` plus `selectSlot` leaves `lastTickInLoop` at `0` across 5,576 ticks, so no loop-end wrap is ever detected and a queued switch can never be *due*; the fixtures therefore reach the production playing state through an overdub **start** on a populated slot (that is what enables, plays and publishes its pair) and retire the predecessor's finalizer **before** the press, because a live finalizer freezes the playhead (the S4b.1 finding) and would hide the term under test. **OWED, and it is not a missing fixture: the demoted target.** Measured with a 24-bar target (`> kMaxDetailedWindowBars × TICKS_PER_BAR` = 12,288 ticks) carrying bar-spread committed content: after the off-clock owner has taken its occupancy the switch is **still pending** (`ready=0 occ=1 pending=1`), because the commit's long-loop hold term (`isPlaybackMergedMidiEventsReadyForSlot`) requires a **non-empty** windowed `mergedMidiEvents` and the windowed gather (`Loop::tryGatherPublishedSnapshotEventsInWindow`) holds the window *empty* (`GatherMissHoldEmpty`) unless a published root supplies it — i.e. the review's \"silent dead switch\" on that shape, as a **readiness** question, not an occupancy one. It needs either a published-root fixture on the windowed publication lane (S4c/F) or an architect decision to relax the hold; both committed fixture shapes stay below the demotion bound and their doc block says so. Focused suite **36/36**; full native **2,651 cases / 2,644 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,192 / code 391,304 + padding 1,912 / locals 34,880` (floor 840; the preceding slice measured code 391,288 / padding 1,928), firmware hex sha256 `ceb79f0a…`. Recorded as **DEC-102**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § \"S4b.6/S4b.7 — the occupancy the switch binds to, and who provides it\". **Next in this change:** S4b.2/S4b.3/S4b.5 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; every device leg (S4b.8/S4b.9 among them) stays owed, and no upload was performed.
**2026-10-01 — `runtime-working-set-ownership` S4b.1's switch lane now has an acceptance surface, and the populated-target queued switch is measured and pinned (test-only; no `src/` change — firmware hex unchanged).** The named prerequisite from the previous slice is discharged: the suite defines `HARNESS_LINKS_PLAYBACK_SWITCH_LANE`, which links the playback hot path, the playback-policy owner and the transport-control owner and moves the **play branch** (the start/stop/toggle owners, the silence owners, the playback-start owners and `setMasterLoopLength`) out of the fail-closed stub surface — because a **populated** target's switch commits inside `TrackManager::updateAllTracks`, which is exactly that branch (four missing symbols; ten stubs collided). The trade is recorded as **DEC-101**: an `OFF_PATH()` abort that is removed cannot have been *reached* by any fixture that was passing, so no coverage was silently weakened, and the loop-edit manager stays stubbed with its callback still failing closed. **Two fixtures that observed through a removed counting stub now assert the real owner** — `trackManager.getMasterLoopLength()` — which is stronger than the counter (stop edge adopts nothing; published geometry adopted; a second dispatch changes nothing; a filtered take adopts nothing). **The lane's acceptance fixture** `test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits` measures: a populated target queues a playback switch without moving the active slot; three full loops of `updateAllTracks` leave it **retained** with the predecessor's finalizer and lease intact; once the finalizer retires it **commits with no further gesture** (active slot moved, pending cleared, enabled set replaced) — so the review's "silent dead switch" does not occur on this shape. **Both named failure modes are red-proved, each restored byte-identically:** committing at any tick fails *"must not commit while the predecessor finalizes"*; never running the commit branch fails *"must commit once the predecessor retires"*. Focused suite **34/34**; full native **2,649 cases / 2,642 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with an unchanged hex and RAM1 line. **Recorded:** the wait is **emergent** (the loop-boundary math with `lastTickInLoop` frozen during finalization), not an explicit guard, so the fixture pins both sides rather than an internal condition; and `Track::ensurePlaybackMergedEventsForSlot` on the commit path is now reachable natively — the surface S4b.6/S4b.7 change next. Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 — the switch lane's acceptance surface, and the populated-target queued switch". **Next in this change:** S4b.6 + S4b.7 (occupancy-aware binding and its off-clock provisioning owner — now assertable), then S4b.2/S4b.3/S4b.5 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; every device leg stays owed.

**2026-10-01 — `runtime-working-set-ownership` S4b.1's finding (a) is discharged: the "press dropped on a denied stop" reading was stale, and the new reproducer covers the denied path (test-only; no `src/` change, firmware hex unchanged).** (a) recorded that "when `Track::stopOverdubbing` denies admission on capacity, the switch returns `false` and the press is **dropped** rather than retained". Reading the gesture shows the carrier exists: `MidiButtonActions::handleToggleRecordForSlot` calls `finalizeCaptureAndSelectSlot`, and on `false` the empty-target branch calls `TrackManager::retainRecordStartForSlot(trackIdx, slotIndex)` and reports "Track %d loop %d: switch and record start retained (stop not yet admitted)" — the retained carrier (a) asked for is already there. New `test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start` is that path's reproducer: another slot's admitted finalization occupies the track's single finalization capacity (DEC-078 item 6), the predecessor then captures live beside it (the required outgoing-finalizer + incoming-capture overlap) and its stop is **denied**; the fixture asserts the active slot does not move, the target's start **is** retained (`isRecordingQueued`), the predecessor's stop **is** accepted-and-latched against its own slot, the occupant's job and lease are untouched and the predecessor keeps capturing — then that retiring the occupant's finalization and turning the lanes makes the retained start **execute with no further gesture** (target active and recording, latch cleared, nothing stolen). **Red-proved twice, each restored byte-identically:** dropping `retainRecordStartForSlot` fails this fixture, the sibling repeated-switch fixture and the two existing retention fixtures (the retention is load-bearing on both the admitted and the denied path); dropping the denied-stop latch fails it at *"Expected 0 Was 1. an uncommitted switch must not move the active slot"* — the **latch** is what holds the active slot in place, so what (a) read as "the press was dropped" was the S4f.5 protection doing its job, and nothing here is a defect. **The real remainder, with its prerequisite measured:** a **populated** target does not take the record carrier at all — the gesture queues a **playback** switch, which commits inside the **multi-slot-commit lane of `TrackManager::updateAllTracks`**; driving that production per-tick entry from this suite **fails to link** for exactly four symbols (`Track::playMidiEvents(uint32_t, bool)`, `Track::playMidiEventsForSlot(uint8_t, uint32_t, bool)`, `TrackManager::isTrackAudible(uint8_t) const`, `Track::shouldCommitQueuedPlaybackStart(uint32_t) const`), so extending the shared harness's linked closure for those four (the `HARNESS_LINKS_*` pattern, without shadowing them in the fail-closed stub surface) is the prerequisite for the S4b.5/S4b.6/S4b.7 fixtures — that lane is where a queued switch commits and where the "silent dead switch" risk lives. Focused suite **33/33**; full native **2,648 cases / 2,641 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 line unchanged. `(d)`/`(e)` stay open as recorded; no new decision (the contract was already logged). Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 (a) — the 'press dropped' reading was stale, and the real remainder has a named prerequisite". **Next in this change:** the harness closure above, then S4b.6/S4b.7 (the occupancy-aware binding and its off-clock provisioning owner), S4b.2/S4b.3/S4b.5 and S4b.4's inbound-role fixture; S4.5 and the S4 gate close with S4f/P3; every device leg stays owed.
**2026-10-01 — `runtime-working-set-ownership` S4b.1's check 7 *repeated* switch is covered, and the request's retention mechanism is now measured rather than described (test-only; no `src/` change — the rebuilt firmware hex is byte-identical to the S4.4 image).** § S4b.1's own next item was "repeated switches B → C while A still finalizes … needs the explicit bounded-overlap policy (`Omax = NUM_TRACKS` inbound holders; an outbound live/finalizing role is already counted and is never released to satisfy O)"; the cross-track case had a fixture, this same-track repeated case did not. New `test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees` (A overdubs on slot 0 → A's stop admitted and paused → switch to slot 1 commits and B records → **switch again to slot 2 while A still finalizes**). **The measured mechanism, which corrects two of my own assumptions:** the repeated switch does **not** move the active slot (S4f.5; the *record* branch of `finalizeCaptureAndSelectSlot` latches and waits — instrumenting the route showed this case takes the record branch, not the overdub one, because the empty target made B a record capture) and does **not** provision its target on the gesture path (the gesture is a MIDI-input edge; provisioning there is the S4b.7 violation); instead B's stop is **accepted and latched against B's own slot** (`hasAcceptedInteractionForSlot(1)` true while `hasLiveCaptureStopJobForSlot(1)` is false) and the target's record start stays **queued** (`isRecordingQueued(0, 2)`), so the S4f.6 **carrier** plus the S4f.1 **latch** is what retains the request — `PendingInteractionLatch` declares `Switch`/`Start` kinds but `latchAcceptedStop` is the only writer, so a switch is never latched as such. Nothing is stolen (A's live finalizer, A's lease and B's lease all survive) and the pool does not grow across the gesture (measured 490 → 490); once A retires and the deferred lane turns the request **executes with no further gesture** — C active, provisioned, B's latch cleared, and the steady state neither drifts nor keeps consuming chunks. **Red-proved twice, each restored byte-identically:** provisioning the target inside the record branch's wait fails the fixture (*"must not provision its target on the gesture path"*); removing that wait fails it (*"Expected 1 Was 2. the repeated switch may not move the active slot"*). **A deliberate control attributes `(b)`'s coverage:** emulating the pre-fix *unscoped* clear leaves this fixture passing and fails `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` and `test_the_successor_take_commits_with_content_after_a_transport_stop`, so `(b)`'s fix (the scoped `clearQueuedRecordingTrack(trackIndex, captureSlot)`) is covered by those two, not by this one. Focused suite **32/32**; full native **2,647 cases / 2,640 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 line unchanged. **Still open in S4b.1:** `(a)`'s uncommitted-switch case and `(d)`/`(e)`; `Omax` is asserted as behaviour (bounded, non-accumulating, nothing stolen) rather than as a new constant, and the design's `TransferOverlap` stays S2's budget term. No new decision: the contract was already logged (S4f.3/S4f.6/DEC-078). Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4b.1 — the repeated switch while a predecessor finalizes". **Next in this change:** S4b.1 `(a)` (an uncommitted switch whose stop is latched), then S4b.2/S4b.3 and S4b.6/S4b.7 (the occupancy-aware binding and its off-clock provisioning owner); S4.5 and the S4 gate close with S4f/P3; every device leg stays owed.
**2026-10-01 — `runtime-working-set-ownership` S4.4 CHECKED: the declared-cap policy is settled as accept-and-schedule, and the breach report now names its holder and is host-assertable (firmware, diagnostic-only; RAM1 byte-identical).** S4.4 asked refuse-vs-queue for an activation beyond the declared envelope, reported and never resolved by allocating — a question its 2026-09-25 slice deliberately left open. **The answer was already logged rather than needing a new decision:** DEC-078 item 1 makes acceptance unconditional and execution scheduled ("the user never re-issues"), so the activation proceeds and the existing accepted-interaction latch / retained requests do the ordering; refusing here would change the admission envelope (`IMPLEMENTATION_CONTRACT.md` §2 returns that to the architect) and would reject supported multi-slot flows, and "resolved by allocating" is excluded by the same §2 because the enforcing caps stay the lease's own. **What this slice adds:** `Track::reportEnvelopeBreachIfAny` now returns the slot holding the class (`kNoWorkingSetClassHolder` = 0xFF when none) and the row names it (`#MEM_DENIED,…,reason=cap_reached,class=<bit>,holder=<slot0|255>,…`), so an anonymous `class=N` can no longer hide the case the contract *requires* — an outgoing finalizer beside an incoming capture (DEC-078 item 5) — which is exactly the mislabel § S4b.1 corrected the record about; and the `PIO_UNIT_TEST_NATIVE` branch records the report (`nativeTestCapDenials()`), making the policy path **host-assertable for the first time** (the previous record's "the emitted line is a device observation" is what this closes). **Fixtures:** the tightened breach fixture asserts the empty census reports nothing, the check allocates no runtime (not even for the slot it inspects) and takes no pool chunks, the breach names slot 0 with class bit 1 (`LiveCapture`), the admission proceeds, and the production admission reports it; new `test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it` drives the required finalizer-plus-incoming pair, asserts the report names the **finalizing** slot, and asserts the incoming activation is admitted with the finalizer still in flight. **Red-proved three times, each restored byte-identically:** dropping the holder fails both fixtures; *excluding* finalizing holders (the candidate correction) fails the finalizing-holder fixture, so the two corrections are discriminated rather than either being blessed; making the census allocate fails the no-allocation assertion. Focused suite **31/31**; full native **2,646 cases / 2,639 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** with the RAM1 line byte-identical (`variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) / locals 34,880`, floor 840). **Recorded, not claimed away:** the census still counts a slot whose capture has stopped as a `LiveCapture` holder (its correction is S4b's role-transfer work), and the new `holder=` field has no device observation yet (nothing under `scripts/` parses `#MEM_DENIED`, so the field is additive). Recorded as **DEC-100**; record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.4 — the declared cap policy: accept-and-schedule, and the report names its holder". **Next in this change:** S4.5 and the S4 gate close with S4f/P3; the remaining native candidates are S4b's own cases (cross-track switch, repeated B→C switches while A finalizes — which need the explicit bounded-overlap policy), and the device legs stay unchanged.
**2026-10-01 — `runtime-working-set-ownership` S4.2 CHECKED: the generation-mismatched release is rejected on the lease path and named as the device row names it (test-only; no `src/` change, rebuilt firmware hex byte-identical).** S4.2 asks for the **A-releases / C-reuses / late-A-cleanup** sequence as a native fixture, and its two 2026-09-25 halves (a generation on `captureStopOwner`; the `(slot, generation)` lease entry whose generation advances on release) had left the *sequence with a live successor in place* and the *naming* of the refusal unproven. Two fixtures close it: `test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set` drives A admitting through the production record-arm admission, A retiring through the track-level sweep (lease dropped, generation advanced), C **reusing** the slot (same runtime address, same role set, its own identity — re-admission bumps nothing), and then A's late release, which must answer `StaleGeneration` — **not** the silent `NotHeld` an identity-free check returns for a slot that looks empty or has been re-admitted — with C's lease, roles and generation untouched and the successor's own identity releasing as the control; `test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them` pins the emitter's outcome→literal mapping (`released`, `refused_busy`, `stale_generation`, `not_held`), the vocabulary `scripts/hitl/lease_release.py` decodes and `test_capture_line_tier` classifies, so "reported" ties to the `DIAG,ws_release` row rather than an enum ordinal. **Red-proved twice, each restored byte-identically:** removing `releaseSlot`'s identity check fails the new fixture (*"Expected 5 Was 1. the retired owner's identity must be rejected as stale, not answered as NotHeld"*) plus the release-entry and two S4.1 fixtures; renaming the `stale_generation` literal fails the vocabulary fixture (*Expected 'stale_generation' Was 'stale'*). Focused suite **30/30**; full native **2,645 cases / 2,638 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS**, RAM1 line unchanged (`padding 1,928`, floor 840). **Recorded, not claimed away:** a device run in which a *stale* release actually fires has never been observed (the corpus carries only `released` rows; the device refusal that has fired is `refused_busy`), so the stale literal is native-pinned and reader-consumed rather than device-observed; the two identities stay unaliased by design; and the `NotHeld` silence is a rule of the *sweep*, not of the label, so it is device-observable only. Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.2 — the generation-mismatched release, rejected and named". **Next in this change:** S4.4 (declared cap policy beyond the envelope) is the next unchecked native candidate; S4.5 and the S4 gate close with S4f/P3, and the remaining device legs are unchanged.
**2026-10-01 — `runtime-working-set-ownership` S4.1's device leg: the working-set lease release row is OBSERVED for the slot under test, and the campaign's route correction names the edge (host tooling + one authorized device campaign; no firmware change).** S4.1 was checked on native evidence with one recorded obligation — a run in which `Track::releaseEligiblePlaybackRuntimeSlots` fires for the slot under test, read as `DIAG,ws_release,<slot>,<generation>,<outcome>` (already emitted, Tier-A since `bbedfeac`) by an oracle rather than inferred. **The row was never unreachable:** a read-only survey of the whole corpus found it **33 times** in ordinary runs of the mandated base preset, so the five 2026-09-27 probe attempts chased the wrong two of the three routes the retention oracle's own limitation text names. The route that fires it is the **playback-reset/undo path** (`TrackUndo.cpp` → `Track::resetPlaybackStateForSlot` → `invalidatePlaybackMergedMidiEvents` → `releaseEligiblePlaybackRuntimeSlots`); a same-track stop cannot fire it because the *retained* route makes `invalidatePlaybackMergedMidiEvents` return early while a deferred stop job is live, and on the capture-switch shape the row that appears names the **incoming** slot. **Observed:** `#CAP,3794271032,DIAG,ws_release,0,3,released` in the **PASS**-verdict base run `captures/session_20260930_142350.log` (hex `577841d6…`), and `#CAP,69967546,DIAG,ws_release,0,0,released` (plus two more) at HEAD in three 2026-10-01 runs, each **1.2 ms after** the device's own `Button press: Record/Overdub (double)` → `MIDI: Undo (entries=3)` edge, for `Loop 1` = firmware slot 0; identity `git_commit f63d5a02` / `firmware_hex_sha256 577841d6…`, uploaded in the campaign's first run. **The oracle half:** `hitl.lease_release` owns the row's wire form and reading (three outcomes, `NotHeld` deliberately silent, one 0-based→1-based conversion), `scripts/hitl_s41_lease_release_read.py` gates on the slot under test and on a required generation (0 observed / 2 not observed / 3 unreadable), and `hitl.retention_probe` now takes the wire form from that owner; `scripts/test_lease_release.py` **13/13** (verbatim rows with provenance, the latch stats read-back proved not to parse as a release, a sibling slot proved not to stand in, absence and refusals proved not to count as success, the edge proved to be selected by device clock). **Found by running and fixed in the same campaign:** every base run at HEAD ended `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on `Record evidence cleanup failed: 'DIAG,latch'` — `drain_record_evidence` reconciled *every* counted family against a row counter that only sees `RECA`/`RECS`/`RID`; it now names the reconciled families and reports the rest instead of raising, `test_managed_record_drain` **23/23** with the mutation red-proved and restored byte-identically. **Not claimed:** no firmware or behaviour change (the native fixtures stay S4.1's behaviour acceptance); today's three runs' *baseline* verdicts were non-PASS for unrelated named reasons (the drain defect; `overdub_stored_span_too_short`; `persistence_active_snapshot_unavailable_during_save`). Evidence and excerpts: [`evidence/2026-10-01-s41-lease-release/EVIDENCE.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-01-s41-lease-release/EVIDENCE.md); record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.1 device leg — the working-set lease release row, observed".
**2026-10-01 — `response-driven-hitl-acceptance` task 5.1's mixed leg gained its registration and stimulus profile: the runner route now reaches the leg the wrapper CLI already accepted, authorized by a profile of its own (host tooling; no firmware, no build, no device action).** Previously the leg was drivable and gradable but reachable only through `--leg mixed` on the wrapper: no registered scenario pointed at it, and its preparation (A verified empty and idle, B seeded and verified) belonged to neither existing leg's profile. Now `LEG_FOR_SCENARIO`/`PROFILE_FOR_SCENARIO` carry `accepted_sequence_mixed → "mixed"` and `→ STIM-BASE-2-MIXED-01`, `hitl.registry` carries the `ScenarioSpec` (the stateless flow + the offline oracle, `owns_capture=True`) with the `accepted_sequence` preset driving three legs in order, and the `STIM-BASE-2-MIXED-01` profile is declared and registered. The profile is its own declaration because the requirement *Scoped same-boot fixture preparation* ends on the constraint this leg is the first to exercise literally — "record and overdub fixtures MUST have different verified content preconditions" — and its two targets are that pair (A the record target, empty *and* idle; B the overdub target, its committed content verified through the seed receipt); the authorization is the same quoted bench scope. The leg still comes from the registration, never from `--leg`. Evidence: scenario suite 40/40 (three legs registered, both mapping tables pinned verbatim, the runner-route pin `--leg mixed` under `STIM-BASE-2-MIXED-01`), a profile pin in `test_hitl_stimulus_contract`, `test_accepted_sequence` 151/151 unchanged, host sweep 1,085 tests / 75 suites with only the six pre-existing failing suites, and 6 registration mutations red-proved with byte-identical, sha256-verified restores. Remaining for the leg: the per-interval content-parity edge on a driven capture, and the device leg with 6.x. [MIXED-LEG-REGISTRATION-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/MIXED-LEG-REGISTRATION-EXECUTION.md).

**2026-09-30 — the mixed leg (record predecessor, seeded overdub successor) landed: plan, preparation, the composed steps and its own grading mode (host tooling; no firmware, no build, no device action).** `leg_gestures("mixed")` is `select_track → a_record_start → b_switch → b_overdub_start → b_stop_request`: A's start is the record capture of 4.2, B's successor the overdub of 4.3, and the difference from the record leg is the firmware's carrier rule — B **has** committed passes, so the S4f.6 carrier does not apply, the switch stops A and selects B, and B's start takes its own record-control press. Preparation is its own branch (`seed_take(B)` → `prepare_empty(A)` → `require_idle(A)`, A last so it is selected). `analyse(leg="mixed")` grades the leg by **mix**: A read with the record vocabulary (its `RECS` stop, `reentry_window_open`, `canonical_release`) beside B with the overdub families (`DIAG,reentry,admit` start, `stop_state` execution, release), the route span running from A's record stop to B's admission with the **capture-switch** as the production row (no carrier — B is populated), and the identity link asking for **B's chain only**, so a conforming mixed run is not failed for lacking an overdub chain on A. Registration (`accepted_sequence_mixed` + preset) and the mixed stimulus profile are still open. Evidence: `test_accepted_sequence` 151/151 (13 new across both halves — 6 flow, 7 grading), corpus 3/3, scenario 38/38, authorization 24/24, host sweep 1,068 tests / 75 suites re-measured 2026-10-01 (six pre-existing failing suites, none importing this change's modules), 8 mutations red-proved with byte-identical, sha256-verified restores. [MIXED-LEG-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/MIXED-LEG-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 5.3 landed: the regression gates are run and recorded, the former queue/timing/attribution bugs' mutations are red-proved, and host success is kept explicitly distinct from hardware qualification (host only: no firmware file touched, no build, no upload, no device action).** Gates: the host sweep **1,067 tests / 75 suites** with only the seven pre-existing unrelated failures, the import gate `test_hitl_import_gate` 5/5 (the new `hitl/acceptance_evidence.py` is reached from the oracle and the oracle from the scenario, so the flow/scenario rules still hold), the architecture/compatibility gates all green (`test_b1_…` 3/3, `test_b2_b5_b6_…` 4/4, `test_b3_b4_…` 5/5, `test_b7_b8_…` 3/3, `test_collapse_legacy_cli_args` 2/2, `test_hitl_actions` 2/2), and the full `pio test -e native` **2,643 cases / 7 skipped / 2,636 succeeded / 0 failed**. Former-bug mutations (`mutate_53.py`, byte-identical restores): the late-Start "confirmed by another press" timing bug and the attribution ledger losing its caller (`test_transport_clock_observation`), the queued-playback-switch double-countdown queue bug (`test_accepted_sequence`), and the record leg's "last candidate" scoring (`test_hitl_serial_timing`). One mutation survived the first run and was a **real coverage gap**: dropping the observation-only branch was invisible because the `allow_transport_press` gate stopped the fall-through, so `test_a_late_ack_for_the_callers_own_start_is_never_re_pressed` now pins the rule. Host success is not device qualification: the native suite is a firmware gate, no host slice drove the board, and 4.1/4.2/4.3's device legs and 6.1–6.4 remain open behind a per-session upload/target authorization. Task 5.3 is `[x]`; with it, everything in 1.x–5.3 is closed except 5.1's mixed record-predecessor/overdub-successor leg. [REGRESSION-GATES-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/REGRESSION-GATES-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 5.2 landed: content parity, the timing gates and the run's own teardown are requirements of the acceptance verdict, each able to veto it, with the evidence read from each existing owner (host tooling; no firmware, no build, no device action).** New `hitl/acceptance_evidence.py` is a reader layer, not a second implementation: content parity comes from `hitl.library_manifest.compare_manifests`' report, the timing half routes through the timing owner's own `midi_boundary_timing_issues(summary)` (so an issue that owner learns to name vetoes without a second edit), and the teardown is `hitl.device_shutdown`'s record — the independent field design decision 6 keeps able to veto. `analyse(..., inputs=…)` grades them as `content_parity`/`timing_gates`/`teardown`: a failing verifier is `ACCEPTED_SEQUENCE_FAIL`, an incomplete drain is a setup failure (not verdict-grade), and **not supplied** is `UNKNOWN` — so a capture can be graded but never passed ("PASS only for the complete conjunction"). The oracle CLI gained `--content-report`/`--timing-report` and the scenario carries all four evidence flags, so both entry points read the same evidence; the retained excerpt is now pinned three ways through both routes (supplied ⇒ PASS, not supplied ⇒ INSUFFICIENT_EVIDENCE, never-staged ⇒ non-pass). Two real defects found and fixed: `ShutdownResult.as_dict()` omitted `ok`, so a real run's sealed bundle could never pass on teardown; and a directly-run oracle shadowed pyserial with its own directory. Evidence: `test_accepted_sequence` 140/140 (8 new `VetoTests`), corpus 3/3 (every retained fixture keeps its verdict), scenario 38/38, neighbours green, host sweep 1,067 tests / 75 suites with only the seven pre-existing failures, plus a CLI check on the retained excerpt. Task 5.2 is `[x]`; owed to 6.3 is a live run whose bundle carries all three reports. [ACCEPTANCE-VETO-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/ACCEPTANCE-VETO-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 4.3 landed: the overdub leg's three clauses are checked on both sides, and a switch answered by the playback queue is refused at the press (host tooling; no firmware, no build, no device action).** The switch step is now a route wait (the `_await_route` 4.2 introduced): the production route is the capture-switch's own row (`Loop B: Switch gesture from slot A`), a playback-queue answer is `CONTRADICTED`, and the leg stops at the press with `PLAYBACK_REAFFIRMED_INSTEAD_OF_SWITCH` sending nothing else — the old wait accepted the playback row and left the refusal to the next gesture's `_switch_pending` guard, by which time the press was spent. "No separate A stop" is read twice (the plan carries none; the oracle counts A's own stop as a second request and fails `L7_one_press_per_stop`), and "no wait for predecessor retirement" is pinned behaviourally (a capture that retires A nowhere still drives to `DROVE_LEG`). The oracle's `L5b_b_start_route` now covers both legs: for the overdub leg the *first* route-family press row decides (capture-switch ⇒ ok; playback first with B's start admitted ⇒ fail; playback with no start ⇒ unknown, which is why the pinned `s4f13-od4-*` fixture keeps `SETUP_INCOMPLETE`), and on `s4f22-dec096-od-13` it reads ok and names the row. Evidence: `test_accepted_sequence` 132/132 (6 new), corpus 3/3, neighbours green, host sweep 1,059 tests / 75 suites with only the seven pre-existing failures, 4 mutations red-proved with byte-identical restores (one attempted mutation recorded as an *equivalence*: the accept set cannot override the conflict stage). Task 4.3 stays `[~]` for its device leg (6.3), as 4.2. [OVERDUB-RECIPE-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/OVERDUB-RECIPE-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 4.2 landed: the record recipe is switch-carried, its targets must be genuinely empty *and idle*, and a playback/overdub answer is a named wrong route rather than a substitute (host tooling; no firmware, no build, no device action).** The record leg no longer stops the global transport as A's stop: the plan is `select_track → a_record_start → b_switch_start → b_stop`, with **one** slot gesture carrying both edges, because `MidiButtonActions::handleToggleRecordForSlot` finalizes A's capture through `finalizeCaptureAndSelectSlot` and — for a target with no committed passes — queues (or, when A's stop is accepted but not yet admitted, retains) B's record start in the same gesture (S4f.6/S4f.3). Preparation is split by leg (`prepare_empty` for both targets with no seeding on the record leg, B first so A is the selected target, plus `require_idle` refusing a target the clear left armed — the firmware's `handleArmedRecordPress` *cancels* an arm under a running transport). The recipe's waits (`_await_record_route`) confirm A's record start incl. its `RECORDING` execution, the switch's carrier or retained twin, and B's single stop, and name a wrong route instead of substituting (`WRONG_MODE_ROUTE`, `SWITCH_NOT_CARRIED`, `B_RECORD_START_UNCONFIRMED`, `A_RECORD_START_UNCONFIRMED`, `B_STOP_UNCONFIRMED`). The oracle reads the same vocabulary as `L5b_b_start_route` over the route decision span (A's accepted stop → B's start), so a later `Playback started` is not a wrong route, and the stale "a record stop publishes inline" note was corrected against this firmware's deferred record-stop obligation. Evidence: `test_accepted_sequence` 126/126 (15 new), corpus 3/3, neighbours green, host sweep 1,053 tests / 75 suites with only the seven pre-existing failures, 8 mutations red-proved with byte-identical restores (one survived the first run — the route span — so the span-boundary test now asserts `unknown` with no carrier). Task 4.2 stays `[~]` for its device leg (6.3), where the vocabulary read from the firmware's emitters is confirmed on real rows. [RECORD-RECIPE-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/RECORD-RECIPE-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 5.1's window invariants landed: the spec's *Continuously playing acceptance window* is two oracle predicates read over the driver's own boundary, so a transport stop or a clock-source change inside a measured window yields the specified non-PASS (host tooling; no firmware, no build, no device action).** The requirement's own sentence — "Preparation, clearing, seeding and final stopped-only verification SHALL have explicit boundaries outside that window" — fixes the boundary: `drive_leg` records `leg_window_end_row` **when the leg's plan returned** (next to `leg_start_row`, taken before the first gesture), and the run scope writes both marks into `accepted-sequence.json`. That position is load-bearing on real data: in `captures/s4f22-dec096-od-13` the leg's last press is at device 59.141 s and the driver's final `Transport stopped` at 67.747 s, so grading that capture with the flow's mark is `ACCEPTED_SEQUENCE_PASS` while a mark taken after the run's own end is `ACCEPTED_SEQUENCE_FAIL` on the run's own teardown. `continuous_transport` reads `ClockManager`'s `Transport stopped` human row and `clock_source_stable` reads `DebugSessionCapture::clockSource`'s `#CAP,<us>,CS,<from>,<to>` one-shot; an unbounded capture reports both `UNKNOWN` (never `OK`), the fallback boundary is the leg's own `Loop N:` stop-request rows *and names itself*, and the shed rule is scoped to the window (a row dropped outside it cannot make the span unmeasurable — the same run shed one at device 51.4 s, before its leg). The record leg's plan still stops the global transport itself (`leg_gestures`' `a_stop`), which the spec's record-successor scenario does not allow, so the oracle records that as a limitation naming the *plan* — making 4.2 measurable rather than prose. Evidence: `test_accepted_sequence` 112/112 (12 new pins), corpus 3/3 (every retained fixture keeps its verdict), neighbours green, host sweep 1,039 tests / 75 suites with only the seven pre-existing unrelated failures, 7 mutations red-proved with byte-identical restores. Task 5.1 stays `[~]`: its predicates are in for both modes, but the record leg cannot pass them until 4.2 makes it switch-carried, and the requirement's mixed record-predecessor/overdub-successor leg is 4.3's. [WINDOW-INVARIANT-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/WINDOW-INVARIANT-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 4.4b landed, closing 4.4: the leg sequence is a stateless flow, the session/report is a host run scope, and the standalone driver is an argument-compatible wrapper both entry points share (host tooling; no firmware, no build, no device action).** `hitl/flows/accepted_sequence.py` now owns the sequence (`drive_leg`: census → scoped stopped preparation → seeded takes → the gesture plan → the final stop and counted read-back), `hitl/accepted_sequence_run.py` owns the session, the authorization record and `accepted-sequence.json` (written at every milestone — `od-08` died with no report at all), and `scripts/hitl_accepted_sequence.py` is parser plus delegation with its flags, exit codes and artifact layout unchanged. **The ownership question 4.4a named is answered:** the stopped-only per-family counter read-back belongs to the session (`ManagedCaptureSession.identity_stat_lines` owns the request, its fence and the decode in `hitl.serial.protocol`), so the flow calls one accessor instead of importing `hitl.serial`, a layer closed to flows and scenarios alike. Both entry points (the runner's registered scenario and the standalone CLI) reach that one implementation, with the scenario owning the single runner→CLI translation. The move was verified *as a move*: 23 moved functions diffed against `git show HEAD:`, 22 byte-identical, and the diff caught two real drifts — the seed's arm gate must accept the printed `Loop N: Start Recording` row and a `RECORDING` state (measured `od-10`), and the census report's keys are the census's own 0-based `(track, slot)` pairs. Evidence: `test_accepted_sequence` 100/100 (two new pins), `test_accepted_sequence_scenario` 38/38 (six flow-layer pins), `test_hitl_stimulus_authorization` 24/24 repointed, 1,025 host tests with only the seven pre-existing failures, native 2,643 cases (7 skipped, 2,636 succeeded), 7 mutations red-proved with byte-identical restores (two survived the first run, which is why the two tests exist). Task 4.4 is `[x]`; the recorded rejection is the literal `runner.main` hop, which would re-parse an argument-compatible CLI and relocate the run's artifacts for no functional gain.

**2026-09-30 — `response-driven-hitl-acceptance` task 4.4's registration half landed: both legs are registered scenarios verified by the offline oracle, the runner translates arguments and refuses unsupported combinations by name, and both entry points reach the same verdict (host tooling; no firmware, no build, no device action).** `hitl/registry.py` gains `accepted_sequence_rec`/`accepted_sequence_od` and an `accepted_sequence` preset, with `hitl.scenarios.accepted_sequence.verify_accepted_sequence` (the offline oracle) as their verifier, so `runner --verify-only` re-derives the acceptance verdict from a log without a device; `ScenarioSpec.owns_capture` is the new declarative field that keeps the runner from wrapping a capture-owning sequence in a second capture on one port, and `--layered` with a classic-registry id is a named refusal instead of a `KeyError`. The new scenario module translates the runner's targets plus an allowlist of wrapper flags into one argv, defaults the run's authorization to the registered profile's own manifest (stricter than the wrapper's `cli` default), and refuses by name — before any device action — a mismatched `--leg`, a missing or ambiguous target, the same target twice, a channel that is not the track's own, an unknown flag or a stray positional. Verdict identity is two-sided on the retained fixtures (the `s4f22-dec096-od-13` excerpt passes through both entry points; `s4f13-od-20260927_015145.log` is `INSUFFICIENT_EVIDENCE` through both). Evidence: new `test_accepted_sequence_scenario` 32/32, `test_accepted_sequence` 98/98 and corpus 3/3, the full host sweep 1,018 tests / 75 suites with only the seven pre-existing unrelated failures (the two layered/base ones re-confirmed identical with this slice's files stashed), `pio test -e native` 2,643 cases (7 skipped, 2,636 succeeded), 10 mutations red-proved with byte-identical restores. Task 4.4 stays `[~]`: its remainder is the leg sequence in a stateless flow with the driver reduced to a wrapper, which first needs an owner for the family-counter read-back under the flow import gate.

**2026-09-30 — `response-driven-hitl-acceptance` task 4.1's host half landed: the run-scoped target authorization manifest is declared with the recipe, enforced before any gesture, and recorded in the run's report and sealed bundle (host tooling; no firmware, no build, no device action).** `hitl/stimuli/spec.py` gains `Authorization` (1-based track, the slots a run may mutate, required provenance, empty scope = read-only, violations naming both the target and the declared scope) and `StimulusSpec.authorization`; `registry.py` declares the scope once for the two profiles these legs drive (track 8, slots 4/5, narrowed from the user's standing authorization of 2026-09-30), exposes `authorization_for`, and `stimulus list` shows `targets=…`/`targets=undeclared` per profile. New `hitl/stimuli/authorization.py` resolves `--authorization <stimulus-id>|cli` into the run's record and refuses — by name, never as a traceback — a target outside the manifest, an unknown id, and a profile that declares nothing; the explicit CLI targets stay the compatibility path and are recorded as such. Both `hitl_accepted_sequence.py` and `hitl_retention_handoff.py` resolve it at the top of `main`, **before the board is reset, before the census and before any gesture**, write `result = "UNAUTHORIZED_TARGET"` into their report and exit 3, and the oracle carries the record into the sealed bundle's `authorization` section (new spec scenario *Target outside the run's authorization*). Evidence: new `test_hitl_stimulus_authorization` 24/24, `test_accepted_sequence` 98/98, the neighbouring set green, the full host sweep 987 tests / 74 suites with only the seven pre-existing unrelated failures, `pio test -e native` 2,643 cases (7 skipped, 2,636 succeeded), six mutations red-proved with byte-identical restores. Firmware untouched. Task 4.1 stays `[~]`: its device legs still need a per-session upload/target authorization.

**2026-09-30 — `response-driven-hitl-acceptance` task 1.1: the change-level G2 execution packet is written, with its one open ownership question recorded as a decision (documentation only; no code, firmware, build or device action).** [G2-EXECUTION-PACKET.md](../../openspec/changes/response-driven-hitl-acceptance/G2-EXECUTION-PACKET.md) pins what every remaining slice shares: the dependency closure (design §Dependency resolved + the runtime change's `SHARED-CAPTURE-FINALIZATION.md`), the observation vocabulary as a table of row families with their emitting owners and the one decoder for each, the budgets (managed ceiling 360 s, cancellation grace 30 s, finite per-action deadlines, RAM1 free-for-locals floor **840** with the bank-arithmetic rule, the fixed-buffer ring whose "loss is detectable" is a gate), the gates (`pio test -e native`, the `teensy41-capture-serial` build, upload only on approval, the mandated HITL entry points) and where each transition oracle is fixed (oracle links, the recipe transitions in design decision 5, the S22 latch transitions, the 3.3 bundle rule) — then audits every required field for an owner and gives each remaining slice (4.1–4.4, 5.1–5.3, 6.1–6.4) a compact section in the template's shape. The audit found one field without an owner and records the decision instead of leaving it open: the run-scoped **authorization manifest** belongs to the stimulus registry, with the drivers' explicit CLI targets kept as the compatibility path and the record of what a run was given; 4.1 implements it. The per-turn observation work number stays deliberately unset — its mechanism owner exists and 6.2 measures it. Landed slices keep their own artifacts, linked rather than restated. `openspec validate --strict` valid; `git diff --check` clean.

**2026-09-30 — `response-driven-hitl-acceptance` task 3.3 closed: a run's verdict is a sealed, versioned, digest-pinned bundle that replays without a device, and the observation wait has one owner (host tooling; no firmware, no build, no device action).** `hitl/reporting.py` owns the **sealed bundle**: `BUNDLE_SCHEMA_VERSION = 2` with a fixed section list, a sha256 digest over everything else, and a verdict derived from the bundle's own evidence by one rule (`derive_verdict`) that the reader re-runs — every recorded predicate ok **and** a teardown result present and ok, with a caller's success claim required to be backed by both, so "shutdown failure SHALL prevent overall success" is enforced rather than restated. `write_report_bundle` seals **once** (atomic rename, refuses to overwrite). Identity comes from the managed path: `hitl/run_metadata.run_identity` is now the one builder behind both `metadata.json` and a bundle's `identity` section. Verify-only replay is `scripts/hitl_report_replay.py --bundle <path>`: it re-derives the verdict with no MIDI or serial port (pinned by patching `mido.open_output`/`serial.Serial` to raise) and exits 0/2/3 (pass / intact non-pass / not trustworthy), stating its own limit honestly (a digest is integrity, not a signature). The oracle seals the acceptance verdict itself: `hitl/accepted_sequence_probe.py --emit-bundle … [--run-report …]` turns its checks into predicates (`UNKNOWN` as not-ok) and carries the run's shutdown result, so a pass needs a proven drain; setup failure, protocol absence, teardown failure and a legitimate non-pass are each their own recorded shape. The observation wait is one owner now — `hitl/actions/observation.py::await_markers`, wrapped by both drivers — and it sits under `hitl.actions`, so the **import gate was resolved without widening it** (pinned by the gate suite). **Evidence:** new `test_hitl_report_bundle` **14/14** and `test_hitl_observation_wait` **7/7**, `test_hitl_import_gate` **5/5**, `test_accepted_sequence` **97/97**, `test_run_metadata` **6/6**, neighbouring suites green together, six mutations red-proved and restored byte-identically. Follow-up, not this task: the layered runner and `--verify-only` still write their own progress report, and the pre-existing `test_foundation_runner`/`test_layered_verifiers` failures (a missing `overdub_step_clocks` attribute in the legacy bridge's verify path) are unrelated and unchanged. See [REPORT-BUNDLE-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/REPORT-BUNDLE-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 3.2 closed: bounded cancellation for a HITL run (host tooling; no firmware, no build, no device action).** The wall-clock watchdog bounded a run but not the scenario inside it: on a ceiling breach it returned, the capture child was released, and the scenario kept working the wire. `scripts/hitl/actions/cancellation.py` owns the three properties — a `GuardedMidiOut` that **refuses every test edge once the run is cancelled** (counting the refusal and raising `RunCancelled`), **releases the notes it was holding** (per note, plus a channel silence per touched channel, idempotently), and permits the **teardown** in an explicitly counted phase (`teardown_call`) so the shutdown's transport stop still goes out; a **registered run scope** (`begin_run`/`active_cancel_scope`/`cancel_active_run`) reachable by the watchdog while the driver owns the scenario; and `run()`, the outer context a driver adopts around its `DeviceSession` (absorb the cancel, release, record, unregister) — with the drain still inner, so both drivers adopted it without re-indenting a body. Cancellation is cooperative and observable: `CancelScope.check` (first reason wins), `cancellable_sleep`, `CorrelatedGesture.wait_for(cancel=…)`. **`managed_capture._run_with_watchdog` is now cancelled-first**: cancel the registered run, wait a bounded grace (`HITL_CANCEL_GRACE_S`, default 30 s) for it to leave the wire, then return — the capture is released after that (no registered run keeps the earlier immediate release). Both drivers register the run, open through the guard, make their waits cancel-aware (raising the run's own reason instead of a false "no receipt") and report `result = "CANCELLED"` + `cancel_reason` + the guard's `midi` record in the same partial report they write on every other exit (nonzero). **Evidence:** new `test_hitl_cancellation` **21/21**, `test_managed_capture` **13/13**, `test_accepted_sequence` **93/93**, `test_retention_handoff_plan` **33/33**, neighbouring suites together **405 tests / 15 suites**, seven mutations red-proved and restored byte-identically, two unrelated pre-existing failures unchanged. Open in this section: 3.3 (managed metadata path, immutable versioned report schema, verify-only replay, the narrow observation-wait facade). See [CANCELLATION-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CANCELLATION-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 3.1 closed: the reusable action layer correlates a gesture's four stages, and both device drivers now wait through it (host tooling; no firmware, no build, no device action).** A press used to be fire-and-forget, so each driver re-derived inline whether the device had answered *that* press and how far it got. `scripts/hitl/actions/correlation.py` owns SEND/RECOGNIZE/ACCEPT/EXECUTE: only the next pending stage advances, and a stage the caller declared no rule for is not awaited at all; every acceptable row must carry the identity **its own wire family** uses (`Loop 4:` label, `DISP,3,` index, `,RECA,3,` field — three spellings of one slot), so a foreign slot cannot substitute; a row at or below the device clock observed before the press is drained history (`hitl.device_time`'s rule, reused for both row families via the new `newest_row_clock_us`); a row answering *differently* is `contradicted` while a missing one stays a gap; `now`/`sleep` are injected; and the wait has no send path at all, so nothing re-presses. The pending-switch clause is `PendingSwitchGate` over the firmware's own rows (`Queued playback switch` incl. the filled-slot form, `Reaffirmed playback switch`, `Switch gesture from slot`), with attribution refusing an unattributed `Playback started`. `hitl/actions/{buttons,slots}.py` gained `correlate=`/`before_device_us=` and record SEND themselves, so no flow declares its own gesture and existing callers are unchanged. **The drivers use it:** `hitl_accepted_sequence.py`'s `_await_slot_disp` is a `CorrelatedGesture` over the caller's vocabulary (`_slot_disp_rows` reduced to a pure row source, `_slot_ready_state` applying the floor as a state read, `_switch_pending` taking its rule from the gate while keeping the leg's window and abort policy); `hitl_retention_handoff.py`'s seed waits became `_await_seed_receipt`, whose `recognize` stage is the device's own `Button press: Loop N (…)` row, whose slot-less `ST,Track` transition no longer counts as this seed's receipt, and whose failure names the blocking stage (`slot N never started recording: blocked at accept (…)`). **Evidence:** new `test_hitl_correlated_actions` **23/23**, `test_retention_handoff_plan` **30/30**, `test_accepted_sequence` **91/91** (corpus **3/3** unchanged), `test_hitl_device_time` **16/16**, neighbouring suites together **244/244** across 9 suites, twelve mutations red-proved and restored byte-identically, two unrelated pre-existing failures unchanged with this slice's files stashed. Open in this section: 3.2 (cancellation) and 3.3 (watchdog, versioned report schema, verify-only replay, the narrow observation-wait facade). See [CORRELATED-ACTIONS-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CORRELATED-ACTIONS-EXECUTION.md).

**2026-09-30 — task 2.2's “loss is detectable” clause became a gate.** The latch family's counters were counted and read back but unused: the managed retrieval's parser matched only `RECA`/`RECS` (so `DIAG,latch` escaped its refusal/displacement gate), its regression check assumed a baseline for every family, and the accepted-sequence driver never asked for the read-back. Now every family the reply carries is reported (trio still required, duplicates and malformed counters still refused, the tag's comma handled), and the oracle gates on the leg's own accounting: `rej`/`evict` rising inside the capture, or `entered != enq + rej`, is a setup failure; a loss already present in both snapshots is not (per-boot counters), and no read-back adds no check. Evidence: `test_accepted_sequence` 90/90, `test_managed_record_drain` 21/21, 15 neighbouring suites green, full native 2,643/2,636/7 skipped/0 failed (firmware unchanged), four mutations red-proved and restored byte-identically. Still owed in 2.2: the latch rows on hardware, gesture/switch rows. See [S22-LATCH-EVIDENCE-PACKET.md §7](../../openspec/changes/response-driven-hitl-acceptance/S22-LATCH-EVIDENCE-PACKET.md).

**2026-09-30 — task 2.2's firmware half: the accepted-stop latch now reports its own sequenced identity evidence (host-verified; device leg owed).** The DEC-078 accepted interaction could be accepted *and* consumed inside one poll gap with neither edge recorded, and the only wait row that existed named no slot. `Track::latchAcceptedStop`, `Track::consumeAcceptedStopObligation` (now taking the reason) and the S2b.6 park emit one compact Tier-A one-shot row per transition (`DIAG,latch,<accept|repeat|wait|park_stall|consume>,<track>,<slot>,<interval>,<destination>,<outcome>`) with the identity the latch holds; the family is counted (`RecordFamilyStats`) and read back by `!CAP_IDENTITY_STATS`. Instrumentation only, no behaviour change. Evidence: `test_continuous_capture` 106/106, `test_capture_ring` 12/12, full `pio test -e native` 2,643 cases / 2,636 succeeded / 7 skipped / 0 failed, `teensy41-capture-serial` SUCCESS with an unchanged RAM1 line (padding 1,928 ≥ 840), five mutations red-proved and restored byte-identically. Device: the image is flashed (sha256 `577841d6…`) and the family is verified **on hardware** through the stopped-only read-back (`RING,record_stats,DIAG,latch,entered=0,…` — present and counted; zero because both overdub stops were admitted immediately). Two attempts at the successor-reentry reproducer aborted at the host's receipt guard with the counters proving the new emitter never ran. Owed: a run with an actually **denied** stop (needs a scratch-slot clear authorization), the gesture/switch rows, and a poll-shaped counter readout. See [S22-LATCH-EVIDENCE-PACKET.md](../../openspec/changes/response-driven-hitl-acceptance/S22-LATCH-EVIDENCE-PACKET.md).

**2026-09-30 — the accepted-sequence oracle reads the overdub leg's own identity rows; `od-13`'s chain now passes with no device action.** `scripts/hitl/accepted_sequence_probe.py` force-unknown'd L1/L5/L6/L7 on an overdub leg because its only stop vocabulary was `RECA`/`RECS` — the *record* lifecycle's rows, which on this leg are the **seeds'** stops (the false acceptance the corpus fixture pins). The intervals do have their own rows: start admission (`DIAG,reentry,admit`), accepted-stop binding (`DIAG,reentry,bound`), execution (`DIAG,stop_state` 6→7 / 7→5|1), retirement (`DIAG,canonical_release`) and the button action's per-interval `Loop N:` **request** rows. Each link is now decided on those, the binding must name the start's interval, A and B may not share an identity, and the new `overdub_interval_identity` is `ok` only when both intervals are identified by their own start, accepted stop and retirement. Absence stays a **gap** (the four pinned `s4f13-od*` corpus fixtures keep their verdicts) while a contradiction is a FAIL; a successor starting after A's release reads `SETUP_INCOMPLETE` "missing overlap is NOT_EXERCISED", not a violation; and the DEC-070 retarget fallback is reported without changing the verdict (its closure condition is `successor-capture-reentry-unguarding` 5.11). **Evidence:** `test_accepted_sequence` **83/83**, corpus **3/3**, 18 neighbouring host suites **361/361**, four mutations red-proved and restored byte-identically, a new device-excerpt fixture pinned line-by-line to its source log. **Re-graded with no device action:** `captures/s4f22-dec096-od-13` `INSUFFICIENT_EVIDENCE`(4) → **`ACCEPTED_SEQUENCE_PASS`(0)** on the run's own rows, `captures/s4f22-dec096-od-01` → `SETUP_INCOMPLETE`(3) with a named cause (B started 136 ms after A's release). No firmware change: the `ODUB,stage`/`ODUB,stop` families still carry no interval identity (the rest of task 2.2), and S4f.22's acceptance still owes a content-parity run and an overlapping leg.

**2026-09-30 — runtime OpenSpec `runtime-working-set-ownership`, S4f.22 acceptance follow-up: the driver's device path is corrected and the OD leg now **drives** on hardware (`captures/s4f22-dec096-od-13`, commit `cf6c1853`, firmware `8d07faec…` = the DEC-096 image).** Host tooling only (`scripts/hitl_accepted_sequence.py`, `scripts/test_accepted_sequence.py`); no firmware change. Ten device attempts each produced one fix, attributed from the device's own rows: the read-only census is retried on a bounded set sized for the **boot restore** (`od-06`/`od-07`: 8, then 9, then 14 of 64 slots known) and an unknown target **refuses before any gesture** (`od-02`: the old fallback long-pressed a filled, non-selected target, which `CLEAR_TRACK_FOR_SLOT` answers by *starting playback*); a decode failure and a crash both stay **named and reported** (`od-08` died on an `LIBOBS,E` row read mid-write and left no report — reports are now written at every milestone); a receipt is qualified by the **gesture's own device clock** rather than file position or a text match for the press row (`od-03`/`od-04`: the ring flushes in batches and the reverse search made the floor *later* than the gesture); receipt waits **flush the capture ring** (`od-05`); the commit mark precedes the commit press (`od-09`: the device emits `RECS,stop,<slot>` at the stop edge, 4.6 s before its own `Transport stopped` row); seed readiness is read from the **live human rows** and an already-armed target is **never pressed again** (`od-10`: the clear's own selection press arms an empty target and the extra press was read as `Stop Recording`); and the leg waits for the predecessor's **publication** (`--predecessor-publication-timeout-s`), reporting the device's own `record admission deferred while a stop obligation is live for slot N` when it arrives (`od-12`, publication 40 s after the stop). **`od-13` result:** `DROVE_LEG` — both clears (B through the documented `post_load_holdoff` retries, A through an `ABORTED` retry), both seeds armed/recorded/committed/settled, the full plan driven, shutdown `complete`, no latch. Oracle: every setup gate **ok** (fresh boot, identity pinned, overflow-free), `L2`, `L3_a_finalization_window` (`DIAG,reentry_window_open,3,1049,1536`) and `L4_a_capacity_released` (`DIAG,canonical_release,3,32,1536,8`) **ok**, with `L1`/`L5`/`L6`/`L7` + `overdub_interval_identity` **unknown** in the oracle's own words (*"RECA/RECS do not identify overdub intervals"* — the sibling change's identity task). Evidence: `test_accepted_sequence` **66/66**, 15 neighbouring host suites **372/372**, four mutations red-proved and restored byte-identically, `git diff --check` clean, `openspec validate runtime-working-set-ownership --strict` valid. One unrelated pre-existing flake recorded: `test_midi_clock`'s pumped-phase test failed 1/8 isolated runs. Commits: `476b7756`, `7e911521`…`cf6c1853` (host path) with the DEC-096 firmware rule as `c1512ab1`. **S4f.22 stays unchecked** — its acceptance still needs the overdub-identity evidence and a content-parity run, not another driver gate.
**2026-09-30 — DEC-097 — HITL musical timing is the device's USB MIDI clock, host-verified.** The recorded premise "this board emits no USB MIDI clock out" is corrected: measured 48.00 Hz = exactly 24 PPQN at 120.00 BPM (384/384 pulses per 8 s, jitter 0.72–1.55 ms stdev), `Start` ~0.31 s after a transport press, and the host can drive it — the device slaves (0 own pulses while driven) and resumes on its own clock 0.53 s after release. Clock exists only while the device's transport runs, and a bare realtime `Start`/`Continue` without clock parks the device in `CLOCK_EXTERNAL` with no clock output (~0.75 Hz measured). Landed `scripts/hitl/midi_clock.py` + `scripts/test_midi_clock.py` (**10/10**) + `scripts/hitl_midi_clock_probe.py` (**device PASS**), guide `docs/Guides/HITL_MIDI_CLOCK.md`, and the premise corrected in `transport_clock.py`, `serial_transport.py`, `edit_controls.py`, both legacy baselines and `test_transport_clock_observation.py` (its 5 tests stay green). Host layer only: no firmware, persistence or wire change. Open: the legacy streamer still paces by the wall-clock proxy whose `#CAP,BPM` source the mandated build never emits.
**2026-09-30 — DEC-097 follow-up — HITL host clock mode passes on device, and `LOOP_EDIT` is proved by causing an ack.** `--clock-source host` now runs the base preset to `Result: PASS` (`captures/session_20260930_142350.log`: record 32/32, loop 1536/1536, stored `REVT` count=32 delta 48–48 bad 0/2, overdub 16/16 + 8/8, first-note 12/2 clocks) and `--clock-source device` re-passes on the same tree (`captures/session_20260930_142518.log`: `REVT` count=31 delta 47–49, first-note 3/3). Two host-side requirements made host mode work: the host drive is now **continuous** (`MidiClockMaster.start_pump` — one 24 PPQN thread on one absolute schedule, because the device's slaving is a 500 ms timeout and it resumes its own internal clock in any gap), and the arm flow is the host's **unconditionally** (realtime `Stop` → Record → `ARMED` → realtime `Start` + pump), since gating the stop on serial evidence of a running transport cannot work while the device is slaved (it emits none of the master-side `BPM`/`BAR` rows) and produced a stray first take plus a missing-`REVT` verdict. The edit-mode precondition now proves `LOOP_EDIT` by holding the edit button long (its `EXIT_EDIT_MODE` binding always lands there) and requiring the ack, because the device announces a mode only when a session opens; unproven refuses by name. Also: the record phase's bar-sync print says `tempo not yet learned` instead of `0.0 BPM`. Host suites: `test_midi_clock` 18/18, `test_edit_mode_precondition` 9/9, `test_clear_before_record_precondition` 39/39, transport-clock/serial-timing/capture-transitions/accepted-sequence green. Device left stopped. Host layer only; no firmware change.
**2026-09-30 — DEC-096 — slot gesture during pending overdub finalization; host verified, device acceptance incomplete.** The user-defined contract is implemented in `MidiButtonActions`; focused runtime tests 28/28, full native passed, `teensy41-capture-serial` build passed (RAM1 padding 1,928 ≥ 840), strict OpenSpec valid. The user authorized track 8 slots 3–5; identity-grade clears were performed only there. Read-only census showed zero committed content after clearing, but valid payload files remain. Uploaded image SHA-256 `8d07faec…e05f` (`git_commit=f0c4b26d`, dirty working tree). OD→OD drove but oracle SETUP_INCOMPLETE (ring shed + missing overdub identity links); R→R aborted before recording; mixed leg seeded slot 4 then failed to start A because startup LOOP_EDIT consumed the slot gesture as geometry editing. Mixed failure leaves test content on authorized scratch slot 4; no content parity or S4f.22 acceptance claimed. Further device retries stopped; see CURRENT_WORK and OpenSpec tasks.

**2026-09-30 — HITL ring classification fixed (host); clear-first device rerun blocked in the driver's census precondition.** `scripts/hitl/accepted_sequence_probe.py` counted a deliberate `RING,shed` row as capture-ring loss, so every fresh-boot leg returned `SETUP_INCOMPLETE` on shed alone (recorded as over-strict 2026-09-27 and re-confirmed for `captures/s4f22-dec096-od-01`); the gate now fails only on `tier_a_evicted`/`overflow`/`critical_rejected`/`wiped` — the same three rows `scripts/hitl/serial_collector.py` marks incomplete — and reports shed non-fatally. A second fix in the same oracle: an absent L3 window/L4 release is a *staging* outcome ("the acceptance was not exercised", the packet's NOT_EXERCISED), not a chain violation. Re-graded with no device action: `s4f22-dec096-od-01` `SETUP_INCOMPLETE`(3) → `INSUFFICIENT_EVIDENCE`(4), `s4f13-od4` keeps `SETUP_INCOMPLETE`, `s4f13-od*`/`carrier` move to `INSUFFICIENT_EVIDENCE`. Host: accepted-sequence 54/54, corpus 3/3, integrity/forwarding/drain/transitions/shutdown green; `git diff --check` clean. The clear-first rerun (`captures/s4f22-dec096-od-02`, same image `8d07faec…e05f`, `git_commit=f0c4b26d`, dirty tree) failed at the clear: the initial census was lost, the filled non-selected target was not pre-selected, its long press became `Loop 4: Queued playback switch (filled slot)` (transport STOPPED→PLAYING), and the follow-up census also went unanswered → `UNCONFIRMED`, fail-closed, `PREPARATION_FAILED`. Its shutdown reached only `incomplete_timeout`, which latched `captures/.device-shutdown-incomplete.json` and refused the next reset; a read-only canonical check (`captures/dec096-recovery-check-01`) then reached verified persistence idle, so the device is known good. No S4f.22 acceptance and no content parity; the latch plus the accepted-sequence driver's census path are the open blockers. See CURRENT_WORK and the OpenSpec tasks.

**2026-09-30 — issue 3 root cause found and fixed: a deferred overdub stop published its pass without its undo entry on the routes that skip `CompanionHistory`/`UndoCollection`.** The sidebar `U:` level (`TrackUndo::undoDepthForLoop`) counted only the record pass while the loop was live (`U:01` after recording plus overdubs against `U:03` after a reboot), and the base check `undo_redo_after_overdub_stop` logged `0 counts` against `expected_min: 1`. The deferred overdub stop drive prepared its `OverdubPassAdded` entry only in those two phases, so three correct production routes committed the pass with no entry: the **clean-stop jump** (`PrepareTransfer` with `SealOutcome::SkippedEmpty` — the interval's take was already committed by its wraps, so nothing is staged at the stop; `stop_phase` trail `1,6,10,11`, the dominant device route), the **STOPPED destination without verified companion preparation** (`TrackPreparedStopCommit`'s transport-Stop fallback, which published no entry by design), and the **superseded causal base** (its variant runs before what was then the audible-bridge handoff). The "3-bar record loses the levels" report is the same defect reached through timing: a late record start is quantized up to 3 bars, which shifts the later steps so the overdub stop lands as a clean stop. Two further defects kept the handoff from working once it existed: `job.undoPublicationReady` held `begin`'s *return* while the transaction only opens into `DetachRedo` (so the gate skipped the prepare turn), and `TrackUndoPublication::canPublish` required a *staged* pass identity a clean stop never has. **Fix (deferred overdub route only; publish at pass publication, once per job; full-route collection machinery untouched):** new `Loop::gatherSessionExitReceiptUndoIdentities` reads the interval's **own** session-exit receipt (DEC-068) in `collectOverdubSessionUndoPasses` shape so the receipt type stays Loop-private; `Track::prepareDeferredOverdubUndoEntryFromReceipt` builds the canonical `OverdubPassAdded` entry from that receipt plus any staged final pass; the handoff sits at the **top of the `AtomicTransfer` phase**, once per job, and drives the protected transaction to `Ready` before any variant runs, so the audible bridge, the stopped recovery, the **no-staged** case and the superseded case each publish what was prepared; `canPublish(…, kInvalidPassId)` means "this turn commits no pass of its own". Fail-open when the interval names no identity, fail-closed on a refused `begin`. **Evidence:** fixture `test_stopped_overdub_stop_publishes_one_undo_entry` (four legs incl. the clean stop), seven mutations red-proved and restored byte-identically; `pio test -e native` **2,637 cases / 2,630 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** (RAM1 padding 1,928 ≥ 840); device A/B on the clean route with an identical command (`captures/s4f20-odundo-07` pre-fix `undo_entries` 5→5 and no short-route row, `captures/s4f20-odundo-10-1` fixed 5→6 with one `src=deferred_short_route` row; full-route control `captures/s4f20-odundo-09` 5→6 and `MIDI: Undo (entries=3)` = the reboot's `U:03`). Decision **DEC-095**, plan [`overdub_stop_undo_level_short_route_bugfix.md`](../Plans/overdub_stop_undo_level_short_route_bugfix.md). Not committed yet.

**2026-09-29 — issue 2 root cause found: the deferred Record publication never queued the record session-exit snapshot.** The production Record stop publishes through the deferred route (**S4f.17 — common interval and stop admission** / **S4f.21 — Record-stop caller migration**) and mirrored the inline stop's undo/display/persistence side effects but not `Loop::queueSessionExitSnapshot(RecordStop, …)`, which the inline `Loop::commitPendingCapturePass` still owns and which no production Record path reaches any more. Without it the committed Record pass is never enumerated into the pending publication box, so the box publishes the **live-record candidate** alone — whose content stops at the completed-bar frontier (`MidiServiceDrain::liveRecordCompletedTicks`, 1536 of 2304 in the reported run) — with its declared length handed off to the sealed length (RC1) and its coverage watermark stamped current by the successor overdub's exit queue. Consumers that serve a current root (visual-cache slice rebuild, overdub source view) then lose the Record pass's last bar; a later pass-stack change (overdub undo, transport stop) makes the root non-current and the passes-authoritative rebuild restores the notes. Evidence `captures/session_20260929_225529.log`: `snapshot_unit,kind=publish,q=0,a=1` @40.135 s; `VCACHE,slice_clean,notes,42` and `DIAG,lcr,src,from=snapshot,notes=42` (26 Record + 16 OD1) against `DIAG,lcr,pairrows,at=passes,count=32`; `notes,48` after the 64.8 s undo. Content, storage and persistence were never wrong. Fixed in `Track::stepDeferredRecordPublication` with fixtures on both routes; decision **DEC-094**, plan [`record_session_exit_snapshot_queue_deferred_publication_bugfix.md`](../Plans/record_session_exit_snapshot_queue_deferred_publication_bugfix.md).

**2026-09-29 — `vch` geometry trace added for issue 2.** The existing session-capture `DIAG,lcr,vch,miss` now reports same-NoteId actual geometry (`sameid_n/sameid_s/sameid_e`) when prepared and native display representations disagree. This directly distinguishes a true Shorten/Hide transform from a prepared-window reconstruction mismatch. Native `test_loop_take_survival`: **141/141 passed**; `teensy41-capture-serial` build: **SUCCESS**, RAM1 padding **1,928 bytes**. No upload or firmware behavior change was performed after this instrumentation.

**2026-09-29 — strict MIDI-clock HITL mode implemented.** `legacy_record_baseline.py` now supports `--require-midi-clock` / `HITL_REQUIRE_MIDI_CLOCK=1`: focused runs refuse serial/wall-clock pacing and require incoming MIDI Clock pulses. Serial RECA/BAR receipts remain validation anchors; no arbitrary sleep is used as musical timing. `TransportTickAnchor` treats the fresh RECA tick as the phase origin, and the host tests cover stale-BAR rejection. Host verification: **88/88 passed** plus syntax validation. A strict invocation reached the device's MIDI-clock-active path; its later failure was the unrelated no-clear slot-observation guard. Device execution with a real clock-feedback path remains required.

**2026-09-29 — refinement of issue 2 classification.** The device observation is split: Record bar-3 notes reappear when transport stops, so their temporary absence is a playback/display-window refresh artifact, not confirmed content loss. The persistent defect is narrower: the Record-pass note crossing the bar-2 boundary is shortened after the first overdub stop and remains wrong. The native 3-bar fixture preserves that crossing note, so the remaining target is the device playback/display handoff path, not Loop canonical storage or the stopped display owner.

**2026-09-29 — issue 2 focused reproduction is green in native.** Added `test_three_bar_record_first_overdub_stop_keeps_record_display_geometry` to `test_loop_take_survival`: a 2304-tick/3-bar Record take with a note crossing the 1536-tick bar boundary, followed by a first Overdub commit/stop and snapshot drain. The Record crossing note, Record bar-3 note, and overdub note all remain present with exact geometry. Focused suite: **141/141 passed**. This does not clear the device observation; it narrows it to device-path/setup state or a path not represented by the Loop/display owner fixture. No firmware edit made.

**2026-09-29 — S4f.22 issue classification from the fresh device run.** Four observations are pinned before diagnosis: **(1) setup timing** — Record started at tick 379 and stopped at tick 2373, so the take is 2304 ticks/3 bars although the fixture expected 2; the note stream was started too late. **(2) candidate firmware defect** — after the first overdub stop (`Reentry window open` → `Reentry released`), the display loses Record-pass material from bar 3 onward and shortens a note crossing bar 2; this requires a focused playback/display reproduction and is not dismissed as setup. **(3) setup sequencing** — after the second overdub, the log has no `MIDI Button A: Stop Overdub`; the next gesture is consumed as `Overdub session undo`. **(4) setup consequence** — the intended global undo/redo never reaches global `U:` history; the subsequent double/triple gestures operate on the overdub session, so the observed `U:02`/missing `U:03` cannot yet diagnose global undo numbering. No firmware change made from these observations.

**2026-09-29 — S4f.22 device baseline and content parity: scenario pass, parity blocker.** Uploaded the approved `teensy41-capture-serial` image and ran the authoritative baseline on track 5 / slot 1 / MIDI channel 1 with a forced fresh boot. Managed baseline: **exit 0 / Result PASS / overall_ok=true**; RECA 2, RECS 8; record grid 32/32; persistence `result=ok`; timing `overflow_free=true`, `call_sites=true`; RAM2 minimum 77,824 B. Read-only content dump/playback verification then **FAILED** inventory parity on every cycle (`inventory_equal=false`), while `storage_matches_effective=true` for 152 effective rows and `max_bar_refs=72` (`capacity_path_exercised=false`). S4f.22 remains open: do not reinterpret this as a firmware contradiction or weaken the content oracle; the next work is to diagnose the playback-capture mismatch.

**2026-09-29 — S4f.22 host integration gate verified.** Focused Python transition/integrity/drain tests: **98/98 passed**. Focused native `test_capture_ring` + `test_shared_capture_finalization`: **62/62 passed**. OpenSpec validation passed. The host-side S4f.22 contract, mutation, capture-accounting, and deferred Record oracle paths are verified; the S4f.22 checkbox remains open only for the separate response-driven device acceptance, which is still parked and must not be substituted by host evidence.

**2026-09-29 — runtime scope reset: S4f implementation is primary; HITL acceptance is parked.** The active OpenSpec work is **S4f.17–S4f.22 — shared capture finalization**: complete the production runtime path and its native/build gates before returning to response-driven HITL/content-evidence acceptance. HITL instability is not a runtime implementation gate and must not drive firmware semantics. No upload or destructive device operation is implied by this reset.
**2026-09-29 — S4f runtime verification resumed after the scope reset.** The existing production implementation was reverified; no firmware edit was needed in this continuation. Focused `test_shared_capture_finalization`: **54/54 passed**. Full `pio test -e native`: **2,625 passed / 7 skipped** on the final run. `pio run -e teensy41-capture-serial`: **SUCCESS**, RAM1 padding **1,928 bytes** (approved floor 840). This verifies the current native/build path for S4f.17–S4f.21; parent task checkboxes remain open where their integration/device acceptance still requires separate evidence. HITL/content-evidence remains parked.


**2026-09-29 — content evidence: the REVT bound regressed on the flush clock; one host fix landed.**
`git log -S` attributes the REVT window `ts > recs_stop_ts → skip` to **`21b8ca8f`**, added against the
function's own docstring ("REVT emission is deferred (idle flush), so lines can appear after RECS,stop",
`373566a7`): a REVT row's `#CAP,<us>` is a **flush** clock, so the bound discarded exactly the deferred
rows. Every run after it reports `record_stored_revt_missing`, so the check has been failing **five**
stored runs, not only the recent ones — a three-extractor replay
replay (`pre-21b8ca8f` unbounded, current bound, the fix) gives 32 / **0** / 32 for
`s4f22-step4-a1`, `s4f22-step5-b1`, `s4f22-step8-e3`, `s4f22-step11-h5`, `s4f22-step11-h6` (their
*recorded* reports predate `21b8ca8f`, which is why a1/b1/e3 read as passing), 0/0/0 for
`s4f22-step3-base-01`, `s4f22-step8-e1`, `s4f22-step11-h3` (nothing delivered), and 4/0/0 for
`s4f22-step8-e2` (the four rows are the *stray* take's). **Fixed** by bounding the take by its **row
span** (`RECA` → next `RECA`, both stop forms). 9 fixtures, including the pre-regression equivalence.
Successor residual accepted: a false negative is evidence loss, never a pass (0 in nine runs).

**Two decisions still open**, because the remaining issues are *not* the same shape:
the SEVT rows that fail the overdub stored-span check sit entirely **outside** the phase windows
(h5: blocks at 9.2–18.1 s and 72.9–73.3 s vs windows 37.8–43.0/51.6–56.8 s; h6: 1792.6–1798.8 s vs
1809–1836 s), i.e. they are pre-run restoration walks — so h6's apparent pass scored restored-library
evidence, and "pass the leg window" cannot fix it; that needs a take/leg id in telemetry (upload) or a
redefined check. `persistence_stage_missing:result` is a deduplicated (`already_pending`) request the
verifier demands a `result` for, and accepting that has a real risk (a save that never settles would
read as passing), so its relaxed form needs its own criteria. Firmware protection for
`CaptureRecordType::Revt`/`SEVT,` (no tier, no counters) is **preventative, not urgent**: these runs
measure no in-run loss — h5's ring is clean (`shed=4, overflow=0, wipe=0`) and h3's pressure
(`shed=73 217`, `tierA_evict=7 966`) accumulated in the long pre-run session with no reader. h6's abort
is the separate intermittent ring wipe. Full proposal, evidence and risks:
[CURRENT_WORK](CURRENT_WORK.md).

**Release state and rollback.** The fix and its docs are **local commits** (`1358a1cd`, `24af74e4`,
`bd86f20a`) on `feature/runtime-working-set-ownership`, which has **no upstream** — nothing is pushed, so
"approve release" means *approval to push the branch and open the PR into `dev`*, not sign-off on an
already-published release. Rollback is host-only and must take every commit of this change **newest
first** (a partial revert conflicts); the tested command is in [CURRENT_WORK](CURRENT_WORK.md).

**2026-09-29 — the transport decision now anchors on device time, not line position.**
Capture rows go through the firmware's fixed evidence ring, which flushes in batches, so a record the
ring drained **late** lands after newer rows and an index window reads it as current. Measured in
`captures/s4f22-step11-h2`: `BAR` rows stamped 850-1864 s interleaved after a 1855 s `state=STOPPED`
reply, 343 `BAR` + 10 453 `DIAG` rows out of device order — and the run concluded "transport running"
from a tail of **40/40 drained rows**, skipped its transport start, then pressed a "stop" onto an
already-stopped device, which *started* it, so the clear could not drain. New
`scripts/hitl/device_time.py` carries the rule (`filter_live_lines`, `device_clock_step`,
`human_line_device_us`): a stamped row older than the newest stamp already seen is drained history,
unstamped directly-printed rows are live by construction, and a boot row restarts the watermark.
`serial_lines_show_transport_activity` filters by it, the arm-time "already playing?" answer compares
a drained `ST,…PLAYING` record with the `Transport stopped` logger line **in device time**, and the
baseline's call site passes its track baseline as the anchor. Replaying h2's prefixes: at the points
where the old index rule said "activity present", the new rule says no, with 40/40 drained rows in the
tail — and where the evidence was genuinely live (h3/h6 tails) both agree. Device: `s4f22-step11-h6`
attached to a device whose log already held 19 026 lines and no longer printed the stale conclusion;
its clear reached `CONFIRMED`/`Clear completed` (its verdict `INCONCLUSIVE_CAPTURE` is the separate
ring-wipe defect below, see the content-evidence entry). Host: new `scripts/test_hitl_device_time.py`
(15 cases). **Residual:** under a large drain lag the newest *transport* row can itself be late, so the
filter can answer "no current activity" for a transport that is running; the authoritative answer (a
fresh fenced `!STOP_STATUS`: a reply *is* proof the transport is stopped) is available but not yet
wired into these phase decisions.

**2026-09-29 — HITL setup: the boot *decision* replaced the boot *wait* (PX1).**
A managed run no longer waits on a fixed post-open window, and never on the device's boot-load steps.
`capture_session.py` asks the device instead (`--boot-reset-mode auto|force|never`): its own read-only
`!STOP_STATUS` probe, and a stopped device is attached **without** a reset — the settle ended on
evidence in **0.1 s** (`captures/s4f22-step11-h3`). Only a device that is not in that state is worth a
fresh boot, and the reset is then confirmed by the firmware's own acknowledgement rather than a blind
10 s window (a refusal is named: `!REBOOT is transport-gated`, `captures/s4f22-step11-h1`). The harness
repairs a transport a killed run left running with **one** idempotent MIDI Stop and then owns a fresh
boot, because that repair makes the stream untrustworthy by position: `captures/s4f22-step11-h2` shows
ring-`drained` `BAR` rows stamped 850–1864 s interleaved *after* a 1855 s `state=STOPPED` reply, the
scenario's "stop" press then **started** the transport, and the clear could not drain. With that rule,
`s4f22-step11-h3` ran clear → record → overdub → second overdub with `precondition_failures: []`, a
**CONFIRMED EMPTY** clear and no readiness refusal; its `INCONCLUSIVE_CAPTURE` verdict is the
pre-existing capture-ring/REVT loss, not setup. Host: new `scripts/test_hitl_boot_settle.py` (15 cases)
green, and `test_managed_capture` is red-free (its metadata test was failing before this slice).
**Open:** an attaching run (`--no-boot-reset`) still decides "is the transport running?" from line
position, so a drained backlog of old `BAR`/`DFRAME` rows can authorize a wrong phase — the h2 shape,
with no reboot to clear it. No firmware change, no upload.

**2026-09-29 — the fresh-boot clear refusal, named and retried (the same slice's second half).**
`--boot-reset` (force) settled in **3.7 s** (`boot_loaded`) and readiness passed on the first probe,
but `s4f22-step11-h4` then died at its *clear* with `clear_not_confirmed` and no cause. The firmware
had said why: `Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left
untouched, save still owed` (`MidiButtonActions.cpp`) — a **temporary** refusal, distinct from
`Clear aborted`, which the transition never retried because only a *disproved* request is. The
harness now reads that refusal by name, waits for the owner of the condition (the save-idle receipt,
30 s bound) and presses once more, bounded to two retries; a refusal that survives them is reported
as `clear_refused_<reason>` instead of a bare `clear_not_confirmed`. `s4f22-step11-h5` (same force
reset) then reported `clear refused by the firmware (waiting_commit_gate)` → waited → **CONFIRMED**
EMPTY → record + two overdubs, exit 0; its `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` verdict is the
pre-existing `record_stored_revt_missing` / persistence-evidence loss, not setup. Host: 4 new cases
in `scripts/test_clear_before_record_precondition.py`.

**2026-09-29 — record-leg misattribution fixed at the phase pairing.**
Not firmware: the pairing accepted only `RECORDING -> STOPPED_RECORDING`, so the intended take
(which resolved `RECORDING -> STOPPED`) was never a pair, a later stray take became the only
candidate, and both the session selection and the length verifier took the last one. Replaying
`s4f22-step8-e2` now selects the intended 65.011 -> 70.204 leg (`length=2304`) rather than
72.697 -> 73.795 (`length=768`). With the successor-start refusal (removes the stray take) and the
clock fix, both record-leg assertions should read the intended take. **Open:** ring reclamation
(e3's zeros) and the bounded same-boot campaign.

**2026-09-29 — correction: the "Record length/span" defect was a misattributed second take.**
`s4f22-step8-e2`: the intended record leg completed correctly (`Recording stopped (to STOPPED) @ tick
2366 (recStart=382 length=2304)`) at 70.20 s. The `overdub start` phase then pressed transport while
the track was STOPPED (71.69), starting an empty-slot capture, and a **second unplanned one-bar
take** appeared (`Recording started` 72.70, stopped 73.80 at `length=768`) -- which the verifier
scored as the record leg. The harness had printed `[warn] overdub start: no stop-completion receipt
within 8.0s; pressing anyway` just before: a press **without** the stop-completion receipt, the same
pattern the clock fix removed. **No Record-length firmware defect.** Open: make a missing successor
receipt a named failure instead of a blind press, and read the record-leg take from the record phase
window rather than the last `RECA`/`RECS` pair. Ring investigation open; the control ledger now
covers the baseline's own presses.

**2026-09-29 — toggle defect verified gone; ring wipe is content loss (zeros), not a stray advance.**
`s4f22-step8-e1/e2/e3`: `transport_press_count` is **0** in all three runs, so the clock helper no
longer toggles transport to confirm a start; the readiness gate passed each attempt. (Ledger caveat:
it covers clock-helper presses only, so the baseline's own presses are not yet attributed.) No
attempt passed: e1 never started an overdub and reported `record_loop_length_mismatch` +
`record_note_span_too_short` + `persistence_loop_persist_not_sliced`; e2 repeated
`ARMED->RECORDING` four times with the same length/span mismatch -- a **second independent defect**
(the Record take's length/span) beside the ring. e3 lost 384 571 bytes to one wipe:
`head_bad op=4 head=724595 tail=60590 used=384571 type=0 plen=0 prefix=0000...`; `(tail-used) mod 1 MiB
= head`, so the bookkeeping is consistent and the head points at **zeros** -- the ring holds 384 KB
that was never written or was zeroed under it, i.e. reclamation/reuse rather than a bad advance. The
ring (1 MiB `extmem_malloc`, freed nowhere) and the event-pool blocks (same heap, zeroed per block on
open) are the next thing to check for overlap. Ring investigation **open**.

**2026-09-29 — the extra transport press, named and removed; readiness proven from a fresh reply.**
`captures/s4f22-step7-d2` carried an unexplained Global Transport press between start and the
intended Record stop (53.527 start, 53.974 recording, 54.526 press, 59.248 Record stop -> armed a
new take). Cause: `ensure_transport_clock` waited for the caller's own start acknowledgement and
then fell through to `ensure_midi_clock`, whose press is a **toggle**, so a late receipt stopped
the transport the start had just begun (same signature as `s4f22-base-device-03`). Fixed:
confirmation after Start is observation-only with a **named failure** instead of a re-press
(regression `scripts/test_transport_clock_observation.py`); attach readiness comes from a **fenced
`!STOP_STATUS`** this run asks for (`state=STOPPED`, idle stop queue), line count diagnostic only;
and every control action is recorded with its caller in `assertions.control_actions`. **Ring-wipe
investigation stays open** -- no `head_bad` in two runs does not clear it. Next: bounded same-boot
campaign preserving every attempt and the content/timing gates.

**2026-09-29 — head-boundary detector armed; the wipe did not reproduce; setup variance dominates.**
`captures/s4f22-step7-d1/d2` (two same-boot attempts, `RING,head_bad` armed): **no `head_bad`
row and no wipe**. d1 attached to a device whose log held 15 lines, aborted with
`!STOP_STATUS: response fence missing` and reported `SPARSE_CAPTURE` with
`clear_to_empty: clear_not_confirmed`; d2 recorded but never entered overdub (`abort: record stop
did not reach PLAYING`) with `record_loop_length_mismatch`, `record_note_span_too_short` and 42
shed rows. The same preset has now produced PASS (`s4f22-step5-b1`), a wipe
(`s4f22-step5-b2`/`b3`, `s4f22-step6-c2`) and a no-record attach (d1): per-run device-state
variance, not one ring defect, blocks repeatability. Next: fail fast when the attach has no
usable stream, and verify the device's settled state before the measured window.

**2026-09-29 — the ring wipe, named: the head lands mid-record, not on a boundary.**
`RING,wipe_detail` from `captures/s4f22-step6-c2`: `head=990370 tail=993518 used=3148
type=50 plen=11317 prefix="295,4294967295,1"`. Those 16 bytes are the middle of a text row,
not a header: the head was advanced by a total that did not match its record, the next flush
read text as a header, saw `total > used`, and reset the ring. Same boot: every append
succeeded (28 472) and tier-A eviction, overflow, critical rejection and shed stayed at 0/4,
so it is a bookkeeping/torn-record defect, not capacity or eviction. Guarding the nested
append removed one way to move the head under a flush; **at least one remains**. Repeatability
is **not proven** (`s4f22-step5-b1` PASS; `b2`/`b3`, `s4f22-step6-c1`/`c2` INCONCLUSIVE).

**2026-09-29 — S4f.22 device legs: the Record leg passes; capture loss is still under attribution.**
Uploaded the candidate (`teensy41-capture-serial`, RAM1 padding 2,072) and ran the
registered base preset. Attempt **a1** (fresh upload + boot reset) is a **PASS**: integrity
COMPLETE, `issues []`, all five transition checks green, drain complete (RECA 1 / RECS 7 /
RID 1 reconciled) and **32 `REVT` rows** present. Fixed from what the runs demonstrated: the
queued stored-note scan borrowed chunk ids the producer then freed (it read nothing, `REVT`
0) and is now pinned for the deferral; the REVT slice drains during playback through the
bounded chunk walk. The boot-restore save completes ~29 s after a real boot, so the
scoped-clear precondition passes and is no longer the blocker. Repeat attempts on the same
boot did **not** pass: **a2** `INCONCLUSIVE_CAPTURE` with `RING,wiped,1,15981` (integrity
INCOMPLETE, now fail-loud) and **a3** `FAIL` (`second_overdub_first_note_missing`). The new
stopped-only `RING,stats` reports attempts == ok, `tx` below it (undrained backlog) and one
wipe; a nested append during a flush can no longer move the head, but the wipe reproduced
anyway, so its remaining cause is **unattributed**. Repeatability is **NOT proven**.

**2026-09-29 — evidence reliability before the next device attempt (step 2).**
The in-flight S4f.21/S4f.22 slices were reviewed and checkpointed in six commits.
Three evidence-reliability defects were then fixed, all host-verified: the Record
REVT scan drains while a loop is playing (bounded chunk walk, 32 events/turn; the
full-merge fallback and stored-MIDI verification still require an idle transport);
the ring reserves 32 KiB that ordinary traffic cannot consume, so a take's
`RECA`/`RECS`/`RID` rows cannot be displaced by another take's; and a missing
telemetry row is no longer reported as a firmware contradiction (`EVIDENCE_MISSING`
/ `INCONCLUSIVE_CAPTURE`, with `assertions.evidence_gap_issues`), while a missing
note *event*, a length mismatch and a short span/count/bar set stay contradictions.
Full native **2,628 cases / 2,621 succeeded / 7 skipped**; `teensy41-capture-serial`
**SUCCESS**, RAM1 padding **2,072** (floor 840). No upload and no device acceptance
is claimed yet.

**2026-09-29 — S4f.21 — Record playback invalidation retarget defect fixed.**
Run 08's two immediate `Record stop retarget` rows led to a native reproduction:
`completeAdmittedRecordStop` launches playback through a cache reset, but
`invalidatePlaybackMergedMidiEvents` treated every pending job as a transport
Stop. The track returned PLAYING while its Record job already targeted STOPPED
and had suppressed MIDI output. The new fixture fails on that destination before
the fix. Ordinary invalidation now preserves a Record job's scratch, destination
and output barrier; explicit Stop and overdub recovery retain their existing paths.
The fixture also asserts an audible barrier state and a live same-slot overdub
successor while Record still finalizes. Focused 156/156; full native 2,619 passed /
7 skipped; capture-serial build PASS, padding 2,072, variables 96,192, locals 34,880.
Not uploaded or device-verified in that slice. Clear authorization for all slots on
all tracks was granted on 2026-09-29; that slice made no device mutation. [Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f21--record-playback-invalidation-is-not-transport-stop-2026-09-29).


**2026-09-29 — S4f.22 — same-session Record evidence drain.**
Run 08 counter evidence reports seven RECS enqueues with zero transmissions,
refusals or evictions. The baseline previously waited only for host serial quiet
before stopping its reader. End-of-leg cleanup now runs before MIDI/reader close:
idempotent MIDI Stop, stopped-only fenced replies, repeated bounded trace drains,
selected finalizer-idle check and transmission-delta reconciliation against this
same log. Counter snapshots use their exact fence endpoint; historical tx counts
do not require reboot. Failed retrieval remains incomplete and preserves the
original leg abort. This does not add Stop within the measured sequence or prove
persistence completion. Device-exercised read-only on 2026-09-29:
`captures/s4f22-base-device-17` reports `record_evidence_drain.complete=true` with
RECA 1 / RECS 7 / RID 1 reconciled against that same log. The device loss mechanism
itself remains unproven, and that follow-up uploaded nothing. [Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--same-session-record-evidence-drain-2026-09-29).


**2026-09-29 — S4f.22 — Record evidence loss accounting, native verified.**
The real capture-ring fixture disproves the previous attribution to critical
reentry/release traffic: those diagnostics are Tier-A, not critical, and cannot
evict a critical Record row. `DebugSessionCapture` now counts RECA/RECS emitter
entries, enqueue successes/refusals, transmissions and evictions alongside RID;
read-only `!CAP_IDENTITY_STATS` returns all three families. Critical append
refusal (including ring allocation failure) latches a safe-flush notice that makes
host `capture_integrity` INCOMPLETE. No retention policy changed. Four real-ring
fixtures pass, including USB backpressure and actual critical-to-critical eviction;
removing the refusal latch fails the allocation-failure fixture. Host integrity
16/16 and forwarding/identity 40/40; full native 2,618 passed / 7 skipped;
capture-serial build PASS, padding 1,992 (floor 840), variables 96,192, locals 34,880.
No upload, SD mutation or device acceptance. The actual device loss mechanism
remains unproven; next compare stopped before/after family snapshots around the
registered baseline. [Execution record](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--record-evidence-loss-accounting-2026-09-29).


**2026-09-29 — §3 S17.5a — transport stop during a Record finalization.** The retarget path the
packet flagged (`runtime->reset`, `retargetFrozenCommandEpoch`, the DEC-070 raw-only fallback
"assume a job exists for overdub") is now mode-aware: `requestDeferredOverdubStopToStopped` returns
after the destination change for a Record job rather than running the overdub recovery against roles
a Record job never populated, and "transport stop wins" is resolved at that edge (`TRACK_STOPPED` +
silence + playback-state reset). Before this a transport stop during a Record finalization left the
track in `TRACK_PLAYING` while the job's destination said STOPPED, because `handleTransportStop`'s
`isPlaying()` branch is unreachable once the pending-job branch has taken the turn. The
playback-runtime reset path deliberately does not force the transport state (its retained window is
still the sounding source, S4f.19), which the playing-inventory fixture pins. Fixture
`test_transport_retarget_of_a_record_job_resolves_stopped`; removing the resolution fails it.

**2026-09-29 — §3 exit breadth.** Transport Stop is covered across the deferred stop's phases by
`test_continuous_capture` (canonical-copy, mid-flight playing retarget, live successor, B03 retarget,
accept-then-transport-stop) plus this change's Record retarget fixture, and clock-source
classification by `test_clock_transport_master`.

**2026-09-29 — §3 S17.5b — load/clear underneath a finalizing stop (two real defects).** An admitted
job whose interval identity was gone parked forever on the deferred-grain prologue's LoopId guard,
holding its raw range, capture-stop owner and ring entry; `supersedeDeferredStopJob` now applies the
S4f.4 transition-3 supersede rule to a job that *did* bind, releasing everything it owns without
touching the live interval. `Track::clear()` now supersedes a job that owns an unpublished take for
the slot rather than letting the worker publish the cleared take back into the slot the user emptied
(explicit supersede — clear preserves the LoopId by design). Fixtures
`test_replaced_loop_during_a_record_finalization_retires_the_job` (load / re-record shape) and
`test_slot_clear_during_a_record_finalization_does_not_republish`, plus the overdub counterpart
renamed to `test_stale_publication_is_superseded_without_persistence` (previously encoded parking as
expected).

**2026-09-29 — §3 S17.5c — the transport-not-running leg of a retained Record stop.** The clock source
reaches this path through `isTransportRunning()` (false both for a stopped internal transport and a
quiet external clock). `test_retained_record_stop_admits_stopped_once_the_transport_is_not_running`
retains a PLAYING-destination Record stop behind another interval's job, stops the transport before
the drive, and pins DEC-078 item 3: the stop admits STOPPED, the take still publishes and the
transport resolves `TRACK_STOPPED`; replacing the re-derivation with the stored destination fails it.
With S17.5a–c, §3's exit list has no unexercised item this environment can reach (the clock
*classification* is covered by `test_clock_transport_master`). No device acceptance is claimed from
host results.

**2026-09-29 — §3 gates (S17.5) — enforced gate, mutation set, accounting.** The finalization suite no
longer reads `S4F16_REQUIRE_CONTRACT`: the R→R/R→OD/OD→R/OD→OD rotation oracle and every other
assertion are unconditional, so ordinary suite success is the strict result and there is no weaker
mode to select. Nine mutations are red-proven — reintroducing the inline Record commit (7 cases),
dropping the stop-edge length stamp (8), closing tails at loop end (3), restoring the old live-length
equality clause (1), retiring during a frozen-window build (1), removing the switch guard (1),
reading the stop-edge prediction instead of the published geometry (2), skipping the deferred REVT
queue (1) and skipping the committed-scratch invalidation (1). Accounting is the compile-time
structure pins (`sizeof(Track) == 384`, `sizeof(CaptureStopJobs) == 28936`, active under
`SESSION_CAPTURE`+`PERF_TELEMETRY`) plus the post-link RAM1 gate:
`variables 96,192 / ITCM 393,216 (code 391,224 + padding 1,992) / locals 34,880`, padding floor 840.
Strict matrix **47/47**; full native **2 617 cases, 2 610 succeeded, 7 skipped**;
`teensy41-capture-serial` **SUCCESS**. S17.1–S17.5 are all landed; §3's own exit list still owes a
transport-stop-in-every-phase sweep, an internal/external clock sweep and load/clear interaction
with a stop in flight, and no device acceptance is claimed from host results.

**2026-09-29 — S4f.20 residuals (S17.4) — deferred REVT queue and `invalidateCaches()` parity.**
The deferred Record publication queues the stored note-on (REVT) scan the inline commit queued, and
the queue is slot-qualified (`deferredRecordRevtsPending` bool → `deferredRecordRevtsSlot`, the same
byte), so it scans the publishing interval rather than whatever owns the active slot; before this
the side effect was absent from the deferred route entirely. Cache-invalidation parity was verified
against the consumer contract instead of assumed: `Loop::notifyCommittedContentChanged` is
`markPassDerivedStale` (playbackOrderDirty, visualCacheDirty, `invalidatePlaybackCaches` — noteCache
and eventIndexValid), a superset of `Loop::invalidateCaches()` for that loop, so the remaining gap
was Track-level — the committed-MIDI scratch, the note-edit selected-slot invalidation and the REVT
queue — now applied by `Track::invalidateCommittedContentForSlot(slot, queueRecordRevts)` for the
job's slot. Fixtures `test_deferred_record_publication_queues_revts_for_its_own_slot` and
`test_deferred_record_publication_invalidates_the_track_committed_scratch`, each with a failing
mutation. The finalization suite is now an **enforced** gate (it no longer reads
`S4F16_REQUIRE_CONTRACT`). Strict matrix **47/47**; full native **2 617 cases, 2 610 succeeded,
7 skipped**; `teensy41-capture-serial` PASS, RAM1 padding **1 992** (unchanged by this slice).
Remaining: S17.5 — enforced-gate documentation, the mutation set, per-track/job/raw/output
RAM1+PSRAM accounting and doc closeout.

**2026-09-29 — S4f.21 S17.3b — job-qualified Record-stop geometry result.** `Track::recordStopGeometryForSlot`
reports `Final` (no stop in flight for that slot; the committed length is authoritative) or
`PendingStop` (an admitted job or a retained obligation still owns the slot's geometry), and is
mode-neutral and slot-scoped. `stopRecordingTrack`, `finalizeCaptureAndSelectSlot`'s Record branch
and `handleTransportStop` now adopt the geometry once, only when final; until then they retain a
per-track obligation consumed by `servicePendingRecordStopGeometry` at the next dispatch boundary,
and auto-align uses the new slot-qualified `Track::setLoopLengthForSlot`. This closes a real defect:
the stop edge stamps the *predicted* length, so the synchronous read adopted a `masterLoopLength`
from a take the minimum-length policy then deleted and auto-aligned the emptied interval with it.
Fixtures `test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction` and
`test_filtered_record_take_publishes_no_master_length`; reverting the transport continuation to the
stop-edge read fails both. `scripts/ram1_budget.json` re-based deliberately (+32 B DTCM for the
obligation, offset by moving the continuation to FLASH: padding 1,976 → 1,992; `itcm_bytes` 12 banks
and `padding_floor` 840 unchanged). Full native **2 613 cases, 2 606 succeeded, 7 skipped**; strict
matrix 45/45; `teensy41-capture-serial` **PASS**. Remaining: S17.4 (the S4f.20 residuals —
deferred revts validation, `invalidateCaches()` parity) and S17.5 (enforced gate, mutations,
RAM1/PSRAM accounting, doc closeout).

**2026-09-29 — S4f.21 S17.3a — switch guard for a retained Record stop.** `finalizeCaptureAndSelectSlot`'s
Record branch now mirrors the overdub branch's slot-scoped rule: an **admitted** Record job carries
its own identity and does not hold the active slot (the switch commits; a different-slot successor
is not serialized behind predecessor cleanup), while an **accepted-but-unbound** Record stop refuses
the switch and leaves the active slot unchanged. This closed a silent content-loss regression
introduced by S17.2: the Record branch switched unconditionally, which advanced `captureInterval_`
past the retained interval, after which `driveAcceptedInteraction` judged the obligation
superseded and dropped the take. Fixture
`test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one` covers the refusal, the
unchanged active slot, the retained obligation, the post-bind commit, the still-finalizing
predecessor and its committed pass; removing the guard fails it. Full native **2 613 cases,
2 606 succeeded, 7 skipped**; strict matrix 45/45; `teensy41-capture-serial` **PASS, RAM1 padding
1 976** (floor 840). **Still open (S17.3b):** the manager continuations still read the
stop-edge-stamped `loopLengthTicks` synchronously.

**2026-09-29 — S4f.17 S17.1 + S17.2 — production Record admission and stop routing landed.**
The production Record stop no longer finalizes inline. `stopRecording` and
`stopRecordingToStopped` admit the interval through `beginDeferredRecordStopForSlot` (final length
from the maintained maximum-tick summary, raw cut under a `canSealCaptureRange` preflight,
job-owned geometry and held-note receipts, declared Record preparation bound, runtime
capture-stop owner) and run their bounded transport edge in `Track::completeAdmittedRecordStop`
(origin alignment, truncation rewind, loop phase, `TRACK_STOPPED_RECORDING` → PLAYING/STOPPED, and
the record-stop playback handoff). A denied stop is retained as an accepted obligation and
admitted later by `driveAcceptedInteraction`, which now dispatches on the retained request's
capture mode. The pass, its `RecordPassAdded` undo entry and its once-only persistence admission
come from `stepDeferredRecordPublication`. Fixtures converted from the inline oracle now assert the
deferred chain's content (three normalization inventories, a 2 056-row take, the filtered-empty
geometry reset, exact held-note closures, the transport-stop successor take) and exposed three
defects the synchronous route had hidden: the deferred normalization closed open tails at loop end
instead of the stop playhead's loop phase; the deferred-grain prologue required the live loop
length to equal the job's, parking a completed job after an all-filtered take or a successor
reset; and the S4f.19 continuity gate waited on a frozen window build only the playback owner can
advance, deadlocking retirement on an empty take. All three are fixed. **Strict matrix green
45/45** with and without `S4F16_REQUIRE_CONTRACT`; full native **2 612 cases, 2 605 succeeded,
7 skipped**; `teensy41-capture-serial` **PASS, RAM1 padding 2 040** (floor 840). Open: S17.3
manager continuations, S17.4 the S4f.20 residuals, S17.5 the enforced gate and mutations.
[Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md).

**2026-09-28 — S4f.19 §2 status after rounds 2-11 (delta work complete; two items depend on a decision/dependency).**
Landed and verified since the G0 packet: **S19.2** job-qualified deferred return-window bind with
content projection (overflow remap, sub-bar clamp, held-note closure, canonical re-order) and an
**atomic refusal** (closure capacity preflighted before any mutation, byte-identical on refusal);
the **native `MidiSendCapture`** wire boundary; the **admission predicate split** (`hasAudiblePlaybackSource`
for audibility, `hasBoundRecordReturnWindowForPendingJob` for a pending owner's ownership, plus
`hasServableDeferredRecordView`); the **hot-path serve** of the bound window and of **onward windows
built from the job's own frozen source** (`serviceFrozenRecordWindow`/`adoptFrozenRecordWindow`,
`RecordCoverage`); the **crossing-note endpoint** (closure gated on `job.pendingNotes`, so a note
whose real Off is further in the take is not cut at the loop end); the **wrap-while-finalizing** fix
(eligibility is ownership, so a later bar is still built after an onward handoff); and the
**continuity-gated retirement** (waits for an in-flight frozen build, then releases the range-derived
window before the first release grain, keeping the copy-owning bar-1 window).
Exit fixtures green: incomplete preparation, sub-bar stop, remapped notes, held notes, traversal
beyond bar one, wrap while finalizing, stale source replacement, plus the production-entry emission
checks (active entry emits the exact sequence; layered emits nothing for an unpublished interval;
publication neither repeats nor omits; retiring ends emission).
**Two items remain, neither inside S4f.19's own implementation scope:** (1) *successor audibility* —
a successor record puts the track in `TRACK_RECORDING`, so `fixedViewEligible`'s `isPlaying()` skips
the adopt paths and the layered entry needs a committed bridge; needs a decision to extend the
layered path or to accept that the predecessor sounds only after the successor's capture ends;
(2) *multi-track audibility* — blocked on the committed-bridge emission path (task 2.4).
Focused **269/269**; full native **2,603 passed / 7 skipped**; capture-serial build PASS with RAM1
padding **888**.
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).


**2026-09-28 — S4f.19 round 2 — refusal atomicity, send-capture boundary, admission predicate.**
The deferred return-window projection now preflights its held-note closure capacity with a
non-mutating pass, so a capacity refusal returns **false with the window byte-identical** for
the worker's retry (capacity-exhaustion fixture compares every row and the coverage before/after,
twice; the preflight-removal mutation fails it). A native-only `MidiSendCapture` boundary records
every event reaching `MidiHandler::sendMidiEvent`, so playback emission can be asserted without a
device. `Track::hasAudiblePlaybackSource(slot)` is the shared admission predicate for both
playback entry points, fixture-verified as not-audible (raw window) → audible (worker bound) →
not-audible (job retired). A hot-path prerequisite is recorded, not yet fixed:
`playCommittedLoopMidi`'s stale-runtime branch calls `reset(true)`, which refuses while a pending
job owns the bound window and then returns, so the interval cannot yet be served. **Next:** the
emission assertions (expected NoteOn/NoteOff sequence, stale/unready emits nothing,
publication/replacement boundary parity, note crossing bar one). Focused 240/240; full native
2,597 passed / 7 skipped; capture-serial build PASS with RAM1 padding **888**.
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).

**2026-09-28 — S4f.19 §2 — G0 complete; S19.2 (deferred return-window bind) landed.**
Read-only trace verdict: PASS with named prerequisites — the MIDI entry early-returns without a
committed pass, so a deferred-only Record interval emits nothing; the retained bar-1 window was
neither bound on the deferred route nor reachable; the projected frozen-source fill had no
production caller; no onward-window builder existed; retirement could invalidate a source
playback would read. **Landed:** `LoopPlaybackRuntime::bindDeferredRecordReturnWindow` binds the
retained bar-1 window to the job's sealed source generation and final geometry, and
`RuntimePlaybackView::projectRawCoverageToLoopLength` **transforms** the raw bar (overflow
NoteOn remap, overflow non-note drop, held-note closure at the loop end, canonical re-order,
coverage clamped to the prepared-bar/final-loop intersection);
`hasBoundRecordReturnWindowForPendingJob` is the pending-owner eligibility predicate. Fixtures
verify transformed content, stale/unsealed refusals and the predicate; two mutations fail
assertions; no new view role or job byte, RAM1 padding 840. **Open:** S19.1 MIDI-entry admission
(held until a native fixture can observe emission), onward windows from the owned frozen source,
and the continuity-gated retirement (matching committed replacement handed off, all builders
detached, playback no longer needs the range). Production Record admission must not be enabled
before those land.
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).

**2026-09-28 — review follow-ups on `2c71b1a3` (one firmware P1, three HITL P1/P2).**
(1) The transfer-row capacity admission grew the prewarmed ownership row with an unguarded
`std::vector::push_back`, so a refused allocation wrote through the null block (ASan null-write);
growth now goes through the new flash-resident `LoopEventStore::prewarmCommittedChunkIdCapacity`,
which probes the exact next growth block first and fails closed — RAM1 padding stays **840**, with
a native refusal hook and a red-proved fixture. (2) The HITL census classified a verified SD
payload with `k=0` as empty, skipping the clear; occupancy is now tri-state (`populated` if either
representation is present, `empty` only when both were observed absent, `None` otherwise) and the
report preserves `None`. (3) The capture-ring drain wrote a bare `!TRACE_DRAIN`, which the capture
owner refuses as malformed and never forwards; it is now a framed request with fresh fence tokens
and the command is allowlisted. (4) `clear_slot` returned `ALREADY_EMPTY` after a recognized long
press plus a settle delay; it now requires an independent confirmed-emptiness reader and otherwise
returns `UNCONFIRMED`. Focused **79/79**; full native **2,592 passed / 7 skipped**;
capture-serial build PASS padding **840**; host Python suites green.
[Closure-boundaries evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).

**2026-09-28 — S4F closure-plan Step 1 — preparation/publication boundaries repaired.**
Six source-verified boundary defects from [S4F-CLOSURE-PLAN](S4F-CLOSURE-PLAN.md) §1 are closed
with production-worker fixtures: (1) the global ephemeral seal is entered and left inside each
private append instead of held across pending turns — proved by interleaving a second track's
256-row capture between private grains (its sealed chunk queues while the private output chunks
stay `NotScheduled` and `isEphemeralSeal()` is false after every grain); (2) staging credit
exhaustion is a resumable `Step::Pressure` with credits topped up per grain and a sticky fatal
deny, so a retry can never duplicate the staged prefix; (3) normalization pressure resumes the
same cursor (the tail cursor repeats its `EventReady`) and a fatal deny keeps the role bound, so
temporary filter markers are never re-read as original events; (4) an all-dropped or
all-filtered Record take reaches the new `RecordEmpty` terminal and retires with the inline
no-content semantics (no pass, no undo, no persistence, geometry reset only with no active
pass), leaving a live successor on another slot untouched; (5) an owned pass transfer may
continue after the source store empties mid-reversal, and `prewarmPreparedPassTransferChunks`
sizes the prewarmed ownership row from the take through the flash-resident `push_back` path, so
a Record take is no longer capped at the fixed 8-chunk overdub row and RAM1 padding stays 840;
(6) publication validates the job-owned geometry receipt and pass capacity **before** the
irreversible commit, so a missing receipt denies with the pass intact and retries exactly once.
Focused 63/63; full native 2,591 passed / 7 skipped; capture-serial build PASS with RAM1 padding
840; six mutations red-prove the fixes. S4f.18/S4f.20 stay open for the production Record
bind/admission (S4f.17/S4f.21).
[Closure-boundaries evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).

**2026-09-28 — closure refinement after `bd7b3bba`.**
Source review found remaining preparation/publication boundary defects beyond the
known production-dispatch gap: finite staging credits with restart over partial
output, global ephemeral-seal scope spanning turns, normalization restart after
mutation, empty-result/transfer continuations, and geometry validation after pass
publication. Strict shared-finalization rerun still fails R→R/R→OD/OD→R (OD→OD passes).
The record HITL recipe still seeds A and sends global Stop, so the recorded missing
successor is not an isolated firmware-cause proof. Close boundaries first, then
playback and common admission/callers; finish live identity, correct recipes and
same-boot provenance before qualification. No firmware changes or new device run.
[Current closure plan](../../openspec/changes/runtime-working-set-ownership/S4F-CLOSURE-PLAN.md).

**2026-09-28 — goal-round closeout (12/12) — objective not complete.**
Whole tree green: focused **57/57**; host HITL suites 46/23/23/15; full native **2,585 passed /
7 skipped**; capture-serial build PASS with RAM1 padding **840**; both OpenSpec changes
strict-valid. Landed this session: the firmware S4f.18 Record preparation chain plus S4f.20
atomic publication/retirement/job-owned geometry; HITL 2.3 command forwarding, 2.1's
device-confirmed observation and 4.1's census-driven preparation (no manual step, `DROVE_LEG`
twice including a fresh-boot run). Open: firmware S4f.19/S4f.17/S4f.21/S4f.22, S4f.20's three
residuals and S4f.13 device acceptance; HITL 2.2, 2.1 coverage, the 4.1 fresh-boot shed-free and
mixed-sequence runs, 5.x and 6.x.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 4.1 — census-driven preparation device-verified.**
The read-only census now drives target preparation: an empty target skips the clear, a filled
target is selected then long-pressed, and the firmware's "deferred save in flight" refusal is
retried after a bounded wait. Device run on track 8 (A=4, B=5, no upload, no manual step):
census 64 slots / 61 occupied; slot 5 cleared on attempt 0; slot 4 aborted once then cleared on
attempt 1; both seeds committed; `DROVE_LEG`. Oracle `SETUP_INCOMPLETE` with named causes (no
fresh boot, 2 ring-shed rows, overdub identity = task 2.2) and L2/L3/L4 green. Host 46/46.
Remaining: a fresh-boot shed-free both-leg run plus the mixed sequence, and 2.2's identity.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.1 — on-demand observation device-confirmed (earlier negative corrected).**
A new stopped-only `!LIB_OBSERVE_STATUS` diagnostic (emitted directly, bypassing the capture ring)
shows `phase=3,ready=1,collected=64,emitted=64` after `!LIB_OBSERVE`, and the managed capture now
returns the complete `LIBOBS,B…E` family — `closed=True, rows=131, slots=64` with real per-slot
occupancy. The earlier "does not serve" result was a ring-flush visibility artifact; the host
drains while it waits. Host **23/23**; full native and capture-serial build re-run; the diagnostic
image is flashed. Remaining for 2.1 is acceptance coverage and 2.2's identity correlation.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.1 investigation — the on-demand observation does not serve.**
Device-reproduced read-only: `!LIB_OBSERVE` reaches the firmware handler (acknowledged, both
transport fences logged) but emits no `LIBOBS` family on a settled, long-running boot across
30 s/60 s windows including repeated drains. The service path should serve, and the one emitting
run had the boot restore active — hypothesis: serving is tied to in-flight restore work. Task 2.1
stays open with the counters-instrumentation next step; task 2.3 remains complete. No SD
mutation, upload or acceptance claim.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.3 — allowlisted command forwarding verified.**
The capture session forwards allowlisted read-only requests on stdin, framed by two distinct
32-char `!TRACE_FENCE` tokens (a device-learned constraint), and `serial/protocol.py` decodes
the receipt and the self-delimiting `LIBOBS,B`…`LIBOBS,E` family once. `ManagedCaptureSession`
owns the stdin pipe and a one-outstanding-request guard. Host **23/23** plus the other HITL
suites green; capture-serial build and full native (2,585 passed / 7 skipped) confirm an
unchanged firmware tree. Device-confirmed fence acknowledgement; the observation family did not
settle on the idle boot, so serving (task 2.1/2.2) is the next HITL slice.
[Command-forwarding evidence](../../openspec/changes/response-driven-hitl-acceptance/COMMAND-FORWARDING-EXECUTION.md).

**2026-09-28 — S4f.20 — job-owned Record geometry receipt verified.**
The interval's before/after `UndoLoopGeometry` and LoopId now live in the job, captured at the
Record bind (consuming the Track's inline baseline) and consumed fail-closed by the deferred
publication, which stamps the after geometry from the job's own loop. The reviewed ring pin is
re-based 28808 -> 28936 B. A fixture seeds a successor baseline after the bind and proves the
published undo entry keeps the job's geometry; two mutations fail assertions. Focused **33/33**;
full native **2,585 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840**.
S4f.20 stays unchecked for the deferred revts validation, `invalidateCaches()` parity and the
production bind site. Next: S4f.21 caller migration.
[Geometry evidence](../../openspec/changes/runtime-working-set-ownership/S4F20-RECORD-GEOMETRY-EXECUTION.md).

**2026-09-28 — S4f.20 — mode-correct Record atomic publication and retirement verified.**
The deferred Record route publishes end to end: the bounded canonical handoff plus the
job-slot-keyed `RecordPassAdded` undo, display staleness and once-only persistence at
`RecordPublished`, then a bounded retirement that returns the ring entry. The inline side-effect
owner was evaluated and rejected. Focused **68/68**; mutations M1/M2/M3 fail assertions; full
native **2,585 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840** and the
ring pin unchanged. S4f.20 stays unchecked: job-owned Record geometry (needs the production bind
site), the deferred revts validation and `invalidateCaches()` parity remain. No dispatch, upload,
device acceptance or checkoff.
[Publication evidence](../../openspec/changes/runtime-working-set-ownership/S4F20-RECORD-PUBLICATION-EXECUTION.md).

**2026-09-28 — S4f.20 — bounded Record pass transfer verified.**
The normalized private output is staged into the job-owned transfer receipt via the existing
mode-neutral overdub mechanism, with a new terminal `RecordPassReady` extending the chain to
`ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady`.
Nothing is published; the atomic publication transaction is S4f.20 completion. Focused
shared-finalization **33/33**; mutations M1 and M3 fail assertions (M2 recorded as individually
unobservable); full native **2,585 passed / 7 skipped**; capture-serial build passes with RAM1
padding **840** and the ring pin unchanged. No dispatch, upload, device acceptance or checkoff.
[Pass-transfer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-RECORD-PASS-TRANSFER-EXECUTION.md).

**2026-09-28 — S4f.18 — per-track Record preparation role and normalization step verified.**
The admission slice landed: `RecordPreparation::Role` (composition cursor + 24,576 B union
scratch, bound by job identity) in the externally allocated per-track ring, and
`stepDeferredRecordNormalization` driving one bounded grain over the merged store to the new
terminal `RecordPrepared`. The reviewed ring size pin is re-based 3072 -> 28,808 B
(205,888 B PSRAM total across 8 tracks; RAM1 padding **840**). Focused shared-finalization
**33/33**, with preparation and staging **57/57**; two mutations fail assertions; full native
**2,585 passed / 7 skipped**; capture-serial build passes. No record-admission dispatch,
upload, device acceptance or task checkoff; S4f.20 publication is next.
[Preparation-role evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-PREPARATION-ROLE-EXECUTION.md).

**2026-09-28 — S4f.18 — Record closure merge and staging phase verified.**
The frozen take is staged after the terminal closure Offs into the same private output, so
the closure consumer is unchanged; a Record-only `RecordSourceStaging` phase and
`stepDeferredRecordSourceStaging` drive the bounded copy to `CanonicalOrder`. Focused
shared-finalization **32/32**, affected suites **166/166**; two mutations fail assertions;
full native **2,584 passed / 7 skipped** (the full-suite gate caught a missing harness include
in three firmware-boundary suites, now fixed). Capture-serial build passes with RAM1 padding
**840**; the reviewed ring size pin is re-based 2984 -> 3072 B (+88 B per track, external).
No dispatch, upload, device acceptance or task checkoff.
[Closure-merge evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-MERGE-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded Record source staging verified; packet corrected.**
The admission packet's premise was falsified by reconnaissance: the closure output holds
only terminal Offs and requires an empty store, so the take must be staged first. The
corrected sequence is source staging -> closure merge -> per-track role/normalization ->
publication. Step 1 landed as `RecordSourceStaging::Cursor` (one bounded row per unit, the
existing `projectRecordStopTick` policy, ephemeral-sealed private output, fail-closed on
stale source or exhausted credits). Focused **7/7**; two mutations fail assertions; full
native **2,583 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840**.
No dispatch, upload or task checkoff.
[Source-staging evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-SOURCE-STAGING-EXECUTION.md).

**2026-09-28 — S4f.18 — preparation workspace unioned; role-admission packet pinned.**
Wrap repair and minimum-length filtering are disjoint phases, so the composition `Workspace`
is now a union scratch role of **24,576 B** (was 40,960), pinned by a `static_assert`, and
`FilterReset` re-discards the tail cursor. Focused composition **17/17**; capture-serial
build passes with RAM1 padding **840** and no linked ITCM growth; full native **2,576 passed / 7 skipped**. The admission slice is pinned in the
[role-admission packet](../../openspec/changes/runtime-working-set-ownership/S4F18-ROLE-ADMISSION-PACKET.md):
one externally allocated 25,696 B preparation role per track (8 total, 205,568 B PSRAM),
bound by job identity and driven one bounded grain at a time. No behavior edit, no
publication, no task checkoff.

**2026-09-28 — S4f.18 — bounded Record identity completion verified.**
The private prepared store gained one fail-closed NoteId write, and the preparation
composition now completes missing NoteOn identities in canonical order between the sort and
wrap repair (matching `sealCapture`) through a borrowed `NoteIdSource`; without a source, or
on an invalid/refused id, it denies `IdentityIncomplete`. Focused composition **17/17**,
with the adjacent primitives **47/47**; full native **2,576 passed / 7 skipped**.
Capture-serial build passes with RAM1 padding **840** and no linked ITCM growth (the role is
not dispatched). ARM cursor 1,104 -> 1,120 B; workspace unchanged. Three mutations fail
assertions, sources restored byte-for-byte. Resource admission and the shared
worker/publication handoff remain open; record stop is still inline. No device acceptance,
upload or parent checkoff.
[Identity execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-IDENTITY-EXECUTION.md).

**2026-09-28 — response-driven HITL 4.1 (partial) — scoped stopped preparation device-exercised.**
The reproducer now clears each authorized target through the production loop-button long
press, correlated only by the firmware's own evidence, and aborts with
`PREPARATION_FAILED` before any record gesture. Host accepted-sequence **42** tests pass.
On the flashed image the record leg cleared slot 4 (`MIDI: Clear selected slot 4`) and
drove; its oracle is `SETUP_INCOMPLETE` (no fresh boot banner) with the known L4/L5
inline-record failures. The overdub leg reported `ALREADY_EMPTY` and `NOT_SELECTED` and
aborted safely. Measured: only the selected slot clears; a short press is a record toggle
(it armed and started the transport); same-button presses inside the tap window degrade a
long press. Target selection, RAM/SD census and repeat qualification remain open; task 4.1
is not checked off, no acceptance criterion relaxed, no upload between legs.
[Execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — S4f.18 — bounded Record preparation composition verified.**
`RecordPreparationCursor` composes the existing private owners over one job-owned
prepared store: canonical (tick, NoteOff < NoteOn < other) order, reserved wrap repair
with a canonical re-order, then minimum-length decisions and private compaction, all in
bounded per-grain units. Identity gaps, stale generations and missing reservations deny
by name; eight randomized differential rounds match the legacy store helpers. Focused
**12/12**, existing primitives **30/30**, full native **2,571 passed / 7 skipped**.
Capture-serial build passes with RAM1 padding **840**, no linked ITCM growth (the role is
not dispatched). ARM `Workspace` 40,960 B / cursor 1,104 B, caller-owned. Four mutations
fail assertions, sources restored byte-for-byte. Bounded missing-identity completion,
resource admission and the shared worker/publication handoff remain open. Record stop is
still inline; no device acceptance, upload or parent checkoff.
[Composition execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-COMPOSITION-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded private canonical filtering verified.**
The store now applies exact rejected-row pairs to private prepared output and
compacts retained rows in bounded turns without a take-sized bitmap or copy.
Ten fixtures cover real minimum-policy parity, long inputs, frozen-source isolation,
late persistence/borrowing, stale receipts and tail retirement; six counterexamples
are rejected. Final focused **24/24**, full native **2,559 passed / 7 skipped**.
Capture-serial build and strict OpenSpec validation pass; RAM1 padding **840 bytes**.
ARM cursor **56 bytes**, no store or resident-role growth. Record ordering/policy
composition, role admission and production integration remain open. No upload,
device acceptance, new decision or parent checkoff.
[Filter execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-FILTER-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded stable private-chunk ordering verified.**
The store owner now sorts private dense prepared chunks in place through bounded
binary comparisons and rotation swaps, preserving equal-tick event identities.
No whole-take event/index scratch; ARM cursor **864 bytes**, no store growth.
Owner/pool checks, exclusive references and non-allocating persistence-state checks
protect the work. Frozen raw input stays untouched beside separate sorted output.
Six fixtures cover long/mixed inputs, metadata and denial; four mutations fail.
Final native **2,549 passed / 7 skipped**; focused ordering/persistence **15/15**. Firmware
build passes with RAM1 padding **840 bytes**, restored after moving the newly used
state accessor to FLASH. OpenSpec validates. No production Record route switch,
upload, acceptance or parent checkoff. Canonical filtering and private-output
publication/persistence integration remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-ORDER-EXECUTION.md).


**2026-09-28 — S4f.18 — canonical compaction retirement bound corrected.**
The existing ordered-source rewrite erased surplus chunk IDs from the middle of
its directory, shifting the remaining suffix in one maintenance grain. It now
releases/pops one tail reference, preserving every retained directory entry.
New fixtures reproduce the old shift and catch wrong-reference retirement while
pinning exact final rows, empty/partial/full chunk cuts and stale-owner denial.
Focused **196/196**; full native **2,543 passed / 7 skipped**. Capture-serial build passes
with RAM1 padding **840 bytes**; strict OpenSpec validation passes. No layout
change, upload, HITL acceptance or parent checkoff. The active finalizer retains
its canonical-copy route. Record ordering, canonical
filter writing and production integration remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-COMPACTION-EXECUTION.md).


**2026-09-28 — S4f.18 — bounded minimum-length decisions verified.**
`CaptureIncrementalSanity::MinimumLengthCursor` consumes one ordered row per call
and retains short-pair source ordinals until acknowledged. Eight fixtures preserve
strict thresholds, retriggers independent of NoteId, channel separation, stale
receipts and retry behavior; 64 differential cases match the real helper's retained
inventory. All 2,048 MIDI keys / 4,096 rows pass, and three policy mutations fail.
ARM cursor **48 bytes**, borrowed workspace **16,384 bytes**; no resident allocation
or production dispatch added. Full native **2,541 passed / 7 skipped**; capture-serial build
and strict OpenSpec validation pass, RAM1 padding **840 bytes**. This is a decision
primitive: sorting, canonical compaction and Record admission/playback integration
remain open. No upload, hardware acceptance, new decision or parent checkoff.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-MINIMUM-EXECUTION.md).


**2026-09-28 — S4f.18 — bounded Record tail-repair prerequisite verified.**
`LoopStopFinalize::TailRepairCursor` now preserves the existing synthetic-Off
policy through one-row input, bounded lane reset/enumeration and acknowledged
output retry. Six fixtures include all 2,048 MIDI keys / 4,096 source rows,
80 real-helper differential cases and three caught policy mutations. ARM cursor
is **48 bytes**; caller-owned scratch is **24,576 bytes**, with no resident field
or production Record dispatch added. Full native **2,533 passed / 7 skipped**; capture-serial
build passes with RAM1 padding **840 bytes** and unchanged firmware footprint.
OpenSpec strict validation passes. Record sorting, minimum-length filtering,
role admission and playback/caller integration remain open. No upload, HITL
acceptance, task checkoff or new decision; connected parent tasks remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-TAIL-EXECUTION.md).


**2026-09-28 — S4f.18 — resumable job-owned pass staging verified.**
The deferred worker now constructs its pass directly in the job, one chunk-reference
move or ordering swap per turn. Partial receipts cannot be borrowed or published;
the Loop producer slot stays available to a successor. A 282-row fixture proves
exact content, live successor survival, one undo/persistence handoff and cursor
retirement. It also exposed unused undo estimates blocking atomic transfer: those
retire only after successful collection and protected publication preparation;
denied collection retains its obligation and refused publication retries its entry.
Focused **31/31**; three mutations rejected; full native **2,527 passed / 7 skipped**.
Capture-serial build passes, RAM1 padding **840 bytes**. The externally allocated
four-job ring grows **2,728 → 2,984 bytes**; Track/Loop/runtime sizes stay unchanged.
Record normalization/admission and connected playback/caller work remain open.
No parent checkoff, upload or device acceptance.
[Staging evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-STAGING-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded prepared-pass chunk transfer verified.**
The existing store-to-committed-directory transition supports resumable reference
moves and order-restoring swaps. Each grain respects its quantum, allocates no
chunks or directory storage, and preserves source/destination ownership on denial.
It requires source/persistence prewarming and rejects stale owner/pool receipts.
Fully prewarmed synchronous callers reuse it; legacy callers retain their path.
Focused **40/40**, five mutations rejected, full native **2,525 passed / 7 skipped**.
Cursor is **64 bytes ARM / 96 bytes native**; no new Track/job/runtime field.
Capture-serial build and layout pins pass; RAM1 padding remains **840 bytes**.
Record normalization and worker integration remain open; no upload, hardware
acceptance or parent task checkoff.
[Transfer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-TRANSFER-EXECUTION.md).

**2026-09-28 — S4f.18 — Record normalization oracle verified.**
Five real-stop fixtures pin overflow NoteOn projection, tail repair, minimum-length
policy, filtered-empty geometry and all 2,056 rows of a Record take beyond overdub
staging. Playing and stopped destinations produce the same expected inventory.
The shared suite now links real playback reanchoring and ledger clearing through
an independent harness opt-in; other suites retain their existing boundaries.
Focused **29/29**; four policy/capacity mutations are rejected; full native
**2,518 passed / 7 skipped**. No production firmware changed, build/upload or
hardware timing claim. Bounded normalization/pass construction and connected
admission/playback integration remain open; no parent task checkoff.
[Normalization evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-NORMALIZATION-EXECUTION.md).

**2026-09-28 — S4f.18 — job-owned Record closure output verified.**
A bounded consumer appends one held-note Off per grain to reserved canonical output,
using the job's saved NoteId and geometry. It preserves a live successor, retains
obligations on denial, and rejects stale raw/output/job identities. Five new fixtures
include 257 held notes and exactly-once retry; four guard-removal mutations fail.
Full native **2,513 passed / 7 skipped**; focused **24/24**; capture-serial build passes.
The owner+generation receipt adds **8 bytes per allocated playback runtime** (6,352 bytes
on ARM); Track/job layouts and static RAM remain unchanged, RAM1 padding **840 bytes**.
Record still finalizes inline. Normalization, pass construction, admission and playback
handoff remain open; no parent checkoff, upload or device acceptance.
[Record output evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-OUTPUT-EXECUTION.md).

**2026-09-27 — S4f.18 — Record held-note identity prerequisite verified.**
Accepted Record NoteOns now retain their NoteId in the existing pending-note map.
Physical and stop closures use that receipt after raw freezing, without searching a reused
producer. Overdub wrap ownership and existing record-stop content remain unchanged.
Full native **2,508 passed / 7 skipped**; focused **19/19**; reverting to raw lookup fails
both identity fixtures. Capture-serial build passed with RAM1 padding **840 bytes**.
ARM held-note entries grow by four bytes; Track/job layouts and static RAM do not grow.
Record still stops inline. Job-owned closure output, bounded pass preparation and playback
handoff remain open; no parent task checkoff or device acceptance.
[Closure prerequisite evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-EXECUTION.md).

**2026-09-27 — S4f.18 — frozen record source consumer implemented.**
The bounded playback fill can read an exclusive CaptureRange without flattening the take.
RecordCoverage shares the existing record tick projection, charges every examined row to
its quantum, and rejects source expiry/read failure or matching-window overflow.
Native regression: **2,504 passed / 7 skipped**. Capture-serial build passed with unchanged
runtime layout pins and RAM1 padding **840 bytes**. This is a preparation prerequisite:
record still stops inline; held-note closure, pass construction, first-bar/onward deadlines
and common admission remain open. No upload or device acceptance.
[Source execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-SOURCE-EXECUTION.md).

**2026-09-27 — S4f.20 — deferred publication persistence handoff implemented.**
Successful deferred transfers now use the existing persistence helper with their validated
job slot, including stopped and superseded-source paths. Cleanup does not replay the request.
A stale Loop object identity also blocks publication. Four transition fixtures cover these
paths with a live successor and a different selected track; the load fixture observes its
legitimate outgoing request separately from real storage admission tests.
Full native: **2,501 passed / 7 skipped**; final publication suite: **15/15**.
Strict contract now has three record-related failures; OD→OD passes with persistence.
Final capture-serial build passed with unchanged RAM1
padding **840 bytes** and no field growth. Full regression/strict contract evidence is recorded in
[the execution packet](../../openspec/changes/runtime-working-set-ownership/S4F20-PERSISTENCE-EXECUTION.md).
No upload or device acceptance. Record still finalizes inline; S4f.17–S4f.21 — connected
shared finalization remain open, including record preparation, playback, undo and callers.

**2026-09-27 — connected S4f.17–S4f.21 implementation in progress; identity/length prerequisites verified.**
Record and overdub starts now share interval stamping; invalid Record starts reject before
slot reset. Accepted stops bind LoopId and Capture mode and supersede changed identities.
Record-length calculation uses a maintained maximum tick, reusing the existing Loop scalar.
Production fixtures cover duplicate/invalid starts, pool-denied preflight/recovery, stale
LoopId/mode, and out-of-order/removal length cases. Record stop still publishes inline;
bounded source preparation, first-bar/onward handoff, publication/undo and caller migration
remain open. Full native: **2,497 passed / 7 skipped**; focused: **11/11**.
Capture-serial build passed, RAM1 padding **840 bytes**. Strict contract retains four
expected implementation-gap failures. No task completion or device acceptance claimed.
[Connected execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F17-EXECUTION.md).

**2026-09-27 — S4f.17 — common admission preflight found a sequencing dependency.**
Record admission cannot be enabled independently of the bounded record worker, source handoff,
record publication and manager continuations assigned to S4f.18–S4f.21. The existing worker
requires overdub staging; managers still assume the void record stop published synchronously.
[Dependency trace and proposed implementation boundary](../../openspec/changes/runtime-working-set-ownership/S4F17-EXECUTION.md).
Recommended: implement the connected firmware path, preserving individual verification gates.
User approved the connected S4f.17–S4f.21 implementation. Work is in progress; each task retains its verification gate and remains open until verified.

**2026-09-27 — S4f.16 — production finalization fixtures complete.**
Real record stop/cold helpers, deferred worker, undo and storage admission now share the drain
suite's production closure. Four record/overdub rotation baselines pass; explicit contract mode
fails on missing record deferred ownership, missing record interval stamp, and missing deferred
worker persistence handoff. These are implementation gaps, not accepted alternative behavior.
Full native **2,490 passed / 7 skipped**; final focused **4/4**; strict OpenSpec valid.
[Fixture evidence and bounds](../../openspec/changes/runtime-working-set-ownership/S4F16-EXECUTION.md).
Next: S4f.17 — common interval and stop admission. S4f.20 — publication/undo/persistence must
also close the exposed persistence gap. No firmware behavior or device changes;
S4f.13 — accepted-sequence acceptance remains open.

**2026-09-27 — shared record/overdub finalization was already intended; firmware contract added.**
Audit confirms runtime proposal role budgeting, accepted-interaction specs and design Revision 23
item 6 already require record A to finalize alongside a live successor. The earlier recommendation
to weaken the record oracle is withdrawn. The implementation gap is record's inline route and
its missing common interval, source, geometry and undo integration.
[Shared capture finalization contract](../../openspec/changes/runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md),
design Revision 25, DEC-093 — shared capture finalization and S4f.15–S4f.22 now define the
firmware work. Existing Track/job/worker owners are reused; first-bar and onward coverage,
mode-correct undo/persistence and all four record/overdub transitions are required. Both
OpenSpec changes strictly validate. Contract only: no firmware/device changes or new test run.
The prior HITL fixture results remain valid; S4f.13 — accepted-sequence acceptance stays open.
At that handoff the next slice was S4f.16 — production finalization fixtures; its completion is recorded above.

**2026-09-27 — response-driven HITL implementation started: production input regressions verified.**
Tasks 1.2 — historical failure corpus and 1.3 — production gesture fixtures are complete
(**2/21**). Four attributed failed-run excerpts remain non-PASS. Real button timing has
12 native cases; USB and DROID dispatch both reach the real long-press clear action in
native storage fixtures. A momentary-dispatch mutation fails 11 assertions. Python accepted
sequence/corpus: **33 passed**; full native: **2,486 passed / 7 skipped**.
Task 1.1 — instrumentation contract is blocked: record-stop seals/publishes inline; the
planned predecessor capture-stop window belongs to overdub. The user has been asked whether
to separate the record/overdub oracles or retain record overlap with firmware scope. No
acceptance criterion changed pending that choice. No firmware edit or device action.
Evidence: [execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).
S4f.13 — accepted-sequence acceptance remains open.

**2026-09-27 — response-driven HITL proposal for S4f.13 — accepted-sequence acceptance.**
The user specifies DROID MIDI controls, continuously playing acceptance windows, no manual
reference pass, and scoped clearing/repetition without reboot. Code and existing failed
captures ground the new [response-driven HITL change](../../openspec/changes/response-driven-hitl-acceptance/proposal.md).
Its design separates gesture recognition, accepted intent and execution; adds bounded
identity observations; verifies RAM/SD preparation and persistence; and requires production
button-timing regressions plus three consecutive device passes per leg on one boot.
All planning artifacts validate; implementation and device qualification remain open.
Earlier host-only driver edits are preserved. No device access, clear or upload in this
planning slice. DEC-092 — response-driven HITL records the user constraints.

**2026-09-27 — S4f.13 — accepted-sequence driver corrected, host verified only.**
The overdub leg now reuses the proven retention prefix: seed B then A, start A’s overdub,
press B once to stop A and switch, start B’s overdub, press B once to stop it. The former
separate A stop removed the capturing-state precondition and entered the SD-aware playback
queue. Slot 8 is a separate record successor in the retention run, not a substitute for an
overdub target. Unexpected queued/reaffirmed playback aborts before the next gesture;
unattributed Playback-started rows cannot discharge that guard. Abort results survive managed
shutdown into the JSON report and nonzero exit. Both intervals receive notes; starts and seed
commits are confirmed. Host tests: accepted sequence 30, retention plan 23, shutdown 15;
old guard mutation fails the new driver regression. OpenSpec strict validation passes.
The oracle no longer treats wrong-slot/late starts or window-open-as-release as acceptance;
record-only RECA/RECS cannot establish overdub identity, and successful stop counts cannot
prove input press counts. Those evidence links explicitly remain unknown. No firmware,
upload, device run, SD clear, push or PR. S4f.13 — accepted-sequence acceptance remains open.
Record: active change `ARCHITECTURE-REVIEW.md`, “S4f.13 — accepted-sequence driver correction”.

**Current handoff — S4f.3's no-concession device gate CLOSED (`RETENTION_PASS`); S4.1's lease row still owed
with its route corrected (2026-09-27).** The re-run the first campaign called for, on the same authorized
targets (**track 8, A=4, B=5, C=8**) with both fixes committed (`675a115a`) so the flashed image is pinned to
them. **Result: `RETENTION_PASS`, exit 0, 20/20 checks `ok`, `concessions: []`**, identity `git_commit
675a115a8225` / `firmware_hex_sha256 e76e1818e069`. Every link is on its own evidence — L1
`DIAG,reentry_window_open,3,1117,2304`; the exposure gate **bounded at 531.1 ms** (the old release-turn
formula would have said 34.8 ms, so the two quantities are now separate report fields and only the bounded one
gates); L2 the **device-timestamped** `DIAG,reentry,deny,1321,2,4,1` (slot 4, interval 2) instead of
`UNKNOWN`; L3 the retained row between denial and release; L4 `canonical_release,3,40,2304,8`; L5
`DIAG,reentry,bound,1717,2,4,0` on the denied interval; L6 exactly one `RECA,7` after the release; L7 note 67
in storage **and** effective agreeing on the full MIDI fields; L8 a clean bounded follow-up — and the content
pipeline reports `storage_matches_effective: true`. **Both fixes are what made it pass:** the
`stop_job_pending` branch now names its slot and interval (FLASH-resident, so `code 392,360 / padding 856` is
byte-identical), and the width gate reads a bounded, directly-observed interval. **Three host defects were
found by running and fixed:** `exit_code` was a computed property never serialized into the review file; the
oracle's CLI is **0-based** for slots *and* tracks while the driver's is 1-based (a real trap that makes a
correct run look like it had no denial — now documented and converted explicitly); and the first cut of the
exposure fix produced a *negative* window by subtracting host seconds from device micros. **S4.1's
lease-release device row remains owed, but its cross-track route is retired as structurally impossible:** a
lease is acquired only when `prewarmPlaybackForSlot` receives `prepareCapture == true` (the record arm and the
boot-cold overdub start; boot's pass uses `false`), and the cross-track sweep excludes the capturing track — so
a **same-track switch after retirement** is the reachable protocol. Oracle tests 43/43; full native **2,469
passed / 7 skipped**; capture-serial SUCCESS; device shutdown `complete`, no latch; no user content touched by
the re-run (preparation cleared campaign-authored test data only). Records:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4f.3 no-concession device gate — CLOSED" and § "S4.1's lease-release device row: the cross-track route is
a structural dead end". Recovery hardware content/Undo/Redo acceptance and the parked DMA repair remain open
and unchanged.


**Previous handoff — first authorized device campaign for the two owed device legs: executed, not accepted
(2026-09-27).** The session committed the verified checkpoint `5849d620` (F4/DEC-091 plus the two S4.1
verification slices), then ran the first authorized attempt at S4.1's lease-release row and S4f.3's
no-concession retention run. A **read-only library census came first and changed the plan**: the device holds
**60/64 committed slots** and **no track has three empty slots** — including track 5, the documented scratch
track — so the user authorized clearing exactly one slot, and the run used **track 8, A=4, B=5, C=8** with
identity-grade clears and a census re-verification of all three targets before any gesture. Identity
`git_commit 5849d620ebc5` / `firmware_hex_sha256 adbb079fd552`; fresh boot, pinned identity, no ring eviction,
tempo verified. **The whole chain ran and all eight links are present in order, but the oracle returns
`SETUP_INCOMPLETE` (exit 3) — no acceptance is claimed, and both device legs stay open.** Three findings are
recorded: the window-width check still reads the **deferred `#CAP` rows** (34.8 ms reported against a 150 ms
floor, while the timely human rows put the window opens 1.56 s apart, so it cannot distinguish "too narrow"
from "flushed late"); the denial row for `stop_job_pending` carries **no slot/interval** while the sibling
branch does, which is the identity link L2 needs; and **zero `DIAG,ws_release` rows** appeared in a completed
A/B/C chain, so S4.1's row is still owed with its triggering route now named rather than assumed. A **log
reading trap** is recorded too: `Selected track 8` (1-based) sits beside `[TRACK] Track 7 armed` (0-based
`trackIndex`), and only the census diff proves the writes landed on the authorized track. Host tests green
(`test_retention_handoff_plan` 23/23, `test_retention_probe` 42/42); device shutdown `complete`, no latch; the
authorized clear of **track 8 slot 5** is the only content this session removed. Record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 device leg + S4f.3 no-concession run". Recovery hardware content/Undo/Redo acceptance and the parked
DMA repair remain open and unchanged.


**Previous handoff — S4.1 lease release verified and checked; F4 completion-failure policy fixed (2026-09-26).**
Two slices since the last checkpoint. **S4.1's lease leg** is verified natively with no firmware change
(three fixtures; the retired slot is released by the next same-track playback reset, refused while a live
finalization owns it, and the protection is triple-layered and red-proved by staged guard removal) and **S4.1
is checked**, its device leg recorded as an evidence obligation because the `DIAG,ws_release,…` emission and
its Tier-A promotion already exist. **F4 / DEC-091** is a firmware change: a persistently failing workspace
completion is now **discharged and reported** instead of re-queued at the queue head, where it blocked every
other persistence item and kept `hasDeferredSaveWork()` true for the rest of the boot (refusing the save
pre-flight and the read-only slot-payload observation). Full native **2,469 passed / 7 skipped**;
capture-serial SUCCESS with `code 392,360 / padding 856` (byte-identical; the named queue API the first cut
added cost 256 B of ITCM and was withdrawn rather than widening the floor); `include/` untouched. Device
evidence for the failure path is owed — it is an SD fault path with no fault injection. Records:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — working-set lease release on a same-track slot switch" and § "F4 — a failed workspace completion is
discharged"; decisions `DEC-090`, `DEC-091`. Recovery hardware content/Undo/Redo acceptance and the parked
DMA repair remain open and unchanged.


**Previous handoff — S4.1 lease release verified; S4.1 checked; DMA still deferred (2026-09-26).**
S4.1's last behavior leg is closed with **no firmware change**: the working-set lease of a retired slot is
released by the **next same-track playback reset/switch** (not by retirement itself), refused with
`RefusedBusy` while a live finalization owns the slot, and the live-finalization protection is
**triple-layered** (track-level early return → per-slot predicate → the runtime's own `reset()` refusal),
each layer red-proved by staged guard removal. Three new fixtures in `test_runtime_working_set_ownership`
(23/23); full native **2,468 passed / 7 skipped**; capture-serial SUCCESS with `padding 856` (floor 840);
`src/` untouched. **S4.1 is checked**, its device leg recorded as an evidence obligation only — the
`DIAG,ws_release,<slot>,<generation>,<outcome>` emission already exists and is Tier-A, so a run plus an
oracle that reads the row is what is owed. Record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — working-set lease release on a same-track slot switch". **Next recommended native slice:**
`FinalizeWorkspace` retry policy (a persistent completion failure retries at the queue head and blocks the
queue behind it; owes a bounded-failure `DEC` before code) — see
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md). Recovery
hardware content/Undo/Redo acceptance and the parked DMA repair are unchanged and still open.


**Previous handoff — S4.1 persistence identity closed as verified; DMA still deferred (2026-09-26).**
S4.1's persistence-identity remainder is settled with **no firmware change**: the deferred save's footer
records the workspace focus at **footer-write time** (content identity stays the interval's slot), verified
by four new cases in `test_storage_drain_restore` that pause the real worker with the footer due, hand off
A→B through the production selection route, and reload — plus a no-switch control, a post-footer handoff and
an SD write fault; red-proved. Full native **2,465 passed / 7 skipped**, capture-serial SUCCESS with
`padding 856` (floor 840), `src/` untouched. **DEC-090**; record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — persistence identity during handoff". Next recommended native slice: S4.1's remaining
working-set **lease** release on a same-track slot switch, and then the `FinalizeWorkspace` retry-policy
question found while building the fixture — see
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md). Recovery
hardware content/Undo/Redo acceptance and the parked DMA repair are unchanged and still open.


**Previous handoff — recovery committed; DMA deferred (2026-09-26).**
Recovery/loaded undo is committed in `f29539e3`. Automatic repair and healthy boot
completed on hardware; full device content/Undo/Redo acceptance remains open.
Blocking-SPI display update is user-confirmed. **User parks DMA repair until after
`runtime-working-set-ownership` completes.** Default firmware still enables DMA;
use the preserved comparator profile when an explicitly approved upload must retain
the working display. No timing gate is waived. Next recommended native work:
**S4.1 — persistence identity during handoff**, starting with a paused-worker
contract fixture, not an assumed active-slot replacement. See
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).

## Earlier session checkpoints


**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.


**2026-09-26 — blocking-SPI diagnostic uploaded with explicit user approval.**
Persistence idle verified before reset. Capture-serial upload SUCCESS using
`captures/display-blocking-diagnostic-20260926/platformio.ini`; actual device
`#DMA_XFER` shows init=0, dma_frames=0, increasing blocking_frames. Serial capture
remains open. Visual outcome awaiting user; no display fix or timing gate claimed.
Recovery source unchanged. Image/config identity is pinned in that capture directory.

**2026-09-26 — display issue after recovery upload.** User sees a frozen first
piano-roll frame while MIDI and renderer/DMA counters progress. Cause unproven.
Existing blocking-SPI diagnostic variant builds; not uploaded. Capture stays open.
Evidence/config: `captures/display-blocking-diagnostic-20260926/`.

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.


**2026-09-26 — automatic boot metadata repair implemented and host-verified.**
DEC-089 — boot metadata repair validates the complete workspace before generating
runtime metadata; preserves source files and corrupt originals; blocks saves after
refusal. Actual boot owner restores the copied 64-slot library and derives 122 undo
actions; healthy reboot retains them. Six isolated cases include 276 interruption
cuts. Full native 2,461 passed / 7 skipped; Python 16; capture-serial SUCCESS with
RAM1 padding 856 (floor 840). No upload/device action; hardware acceptance and the
incident hold remain open. Evidence: active-change
`evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.


**2026-09-26 — approved load-time undo implemented; host recovery passes.**
DEC-088 — load-time undo reconstruction supersedes the earlier zero-after-load
policy. All 122 actions derive from saved passes on load/reload with empty undo
headers; all 60 nonempty slots pass Undo/Redo, and background commits preserve
runtime Redo. Redo starts empty and is created by Undo. The isolated runner verifies
hydration before/during/after restore. Candidate reconstructed-undo-04 preserves all
65 original files plus a 628-byte bundle (443,431 bytes total). Capture-serial build
passes with RAM1 padding 840, unchanged. No upload/device/card action; standalone
reader and incident hold remain. Hardware recovery is still unverified. Evidence:
active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.

**Verification complete:** full native 2,461 passed / 5 skipped; focused native 68
passed; recovery load/reload 122 actions and all 60 nonempty slot roundtrips;
Python 16 passed; OpenSpec validation passes. Capture-serial firmware SUCCESS,
RAM1 padding 840. No device actions. See UNDO-LOAD-FIX.md and preserved logs.


**2026-09-26 — correction: reconstructed undo after reload needs a policy change.**
The prior “next fix” diagnosis missed DEC-057 — snapshot body format and the active
working-set spec: LSV1 load deliberately starts with undo depth zero. The older
content-history reconstruction spec has a newer snapshot delta that changes it.
The 122-action host recovery remains valid; it is not authorization to reverse the
load contract. Proposed change: derive per-slot undo from sufficient saved passes,
redo empty, saved snapshot authoritative, no persisted undo stack. Approval is
required before that normal-load behavior changes. No firmware/device action.
See active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`.


**2026-09-26 — undo reconstructed and host-tested; reload blocks installation.**
Existing C++ content-history owner derives 122 actions (60 record, 11 overdub,
51 boundary changes) across all 60 nonempty loops. Full Undo → Redo preserves
materialized MIDI bytes, loop lengths and published note identities/content on all
60; four empty slots have no actions. Diagnostic bundle: 8,846 bytes. Production
load/hydration fixture retains only 9 actions after reload, so the wrapper exits 2
and the candidate is not safe to install. Original files byte-identical; firmware
unchanged; no device access; hold remains. Original cross-slot chronology, clear
snapshots and old redo branch remain unrecovered. Evidence: active-change
`evidence/2026-09-26-workspace-loss/UNDO-RECONSTRUCTION.md`.


**2026-09-26 — workspace reconstruction passes host load/restore acceptance.**
Candidate `captures/midilooper-folder-copy-20260926-195228/reconstructed-03/candidate/`
preserves 65 original files and adds a 628-byte runtime bundle (total 443,431 bytes).
All 64 slots restore; 7,840 capture-event records match source byte-for-byte, 57 snapshot
inventories match. Explicit replacement settings: stopped/120 BPM, first nonempty
slot per track selected/enabled, unmuted, fresh global undo. Source decode 3/3;
reconstruction 2/2; affected ordinary-native regression 12 passed/2 explicit skips;
Python 16 passed. Reusable C++ writer is host-tested only; automatic device recovery
is not implemented. No SD-card writes/upload/device access. Hold remains latched.
Evidence: active-change `evidence/2026-09-26-workspace-loss/WORKSPACE-RECONSTRUCTION.md`.
S1.6c.2 — capacity revalidation remains blocked on actual hardware recovery/measurement.


**2026-09-26 — host production decoder audit complete.** All 64 copied current
slots decode using an isolated Teensy-compatible event-layout adapter: 7,840 raw
MIDI events; 57 effective snapshots with 2,766 note identities. Three nonempty slots
use raw pass format; four slots empty. Direct Mac decoding was invalid because
MidiEvent is 32 bytes on Mac versus 20 on Teensy. Firmware unchanged. Three focused
native tests pass through the repeatable host wrapper; no device access. Evidence:
active change `evidence/2026-09-26-workspace-loss/PRODUCTION-DECODE-AUDIT.md`.
Runtime metadata reconstruction/load validation remains next; no restore candidate
approved, normal firmware hold unchanged, S1.6c.2 — capacity revalidation blocked.


**2026-09-26 — offline recovery audit complete; no device access.** All 64 current
slot files pass the firmware checksum/footer checks; 60 have nonzero loop lengths.
60/64 lengths match the preserved pre-loss census; differences are track 8 slots
4–7. Saved revisions hold at most 22 nonzero-length slots, so current files remain
the primary recovery candidate. Full note decoding and compatible workspace metadata
are not yet validated; no restore candidate is approved. S1.6c.2 — capacity
revalidation remains blocked. Evidence: active change's
`evidence/2026-09-26-workspace-loss/OFFLINE-RECOVERY-AUDIT.md`. 16 host tests pass.
Normal firmware/recovery hold stays latched; standalone reader remains installed.


**2026-09-26 — USB folder copy COMPLETE, authorized reader installed.**
Copied only `/MidiLooper` through the standalone read-only reader with the card left in place:
**1,007 file entries / 6,837,184 bytes**, transfer CRC32 and independent local SHA-256/length/CRC
verification all pass. Output: `captures/midilooper-folder-copy-20260926-195228/download-05/`;
report: `captures/midilooper-folder-copy-20260926-195228/verification.json`.
Contains 65 current files (64 slots plus workspace metadata), 73 saved-set files, and historical
`current.bad.*` trees. This proves surviving files, not their semantic validity or recoverability.
One duplicate path, `current.bad.211210/workspace.bin`, has distinct 154-byte and 77-byte entries;
both are preserved (second under `duplicate_entries/000854.bin`), not overwritten. Initial transfers
are retained as incomplete evidence; USB per-line acknowledgements fixed unreliable bulk sending.
Reader firmware SHA256 `a915b18c5a12175ef7c25dddcc8932840418fd61018ca4201ade467e78cacce2`;
9 host tests pass. No SD writes, normal-looper boot, recovery or restore occurred. Reader remains
installed; normal-looper upload/recovery hold stays latched. User's explicit approval covered this
reader installation and folder copy only. Recovery analysis can now use the host copy.

**2026-09-26 — standalone USB reader installed with explicit user approval; copy pending USB port.**
User approved replacing/resetting the running firmware solely to read `/MidiLooper`. Standalone
`pio run -d tools/midilooper-reader -e teensy41-capture-serial -t upload` succeeded. macOS
sees USB Serial `16c0:0483`, serial `15494480`, but has not exposed a `/dev/cu.usbmodem*`
port. No export command was sent and no SD access occurred through the reader. Asked the user
to allow the accessory if macOS prompts. Installation evidence:
`captures/midilooper-folder-copy-20260926-195228/installation.json`. The normal-looper
upload/recovery hold remains latched; do not restore the normal firmware automatically.

**2026-09-26 — USB folder reader prepared locally; no device access.** User requests only
`/MidiLooper` over USB, leaving the card in place and unrelated data excluded. Added
`tools/midilooper-reader/`: separate read-only utility firmware (no looper boot recovery/save
code) plus a streaming Mac downloader with file CRC32, SHA-256 manifest, path checks and
incomplete-transfer handling. Seven host tests and the standalone Teensy build pass.
NOT uploaded or device-verified; incident hold remains set. Installing the reader replaces
and resets the running firmware and needs explicit approval. Folder-only copying supersedes
full-card imaging as the requested approach, without authorizing upload, repair or device access.
Contract and commands: `tools/midilooper-reader/README.md`.

**2026-09-26 — INCIDENT: the device workspace no longer loads; host shutdown defect fixed (host-only; no device access).** The device comes up empty: `CurrentSet meta missing .../current/temp/runtime.bundle.bin`, `Current workspace load failed`, `Boot recovery chain exhausted; starting empty`, `BOOT,load,fail`, `free_chunks=480/512`, all 64 slots empty — where the same library read **58 committed / ~386-403 free** minutes earlier, and the post-loss observation is *complete* (64/64 slots, `incomplete_window: false`), so it is not an artifact. **Cause, and it is this session's tooling:** the deliberate clear of the authorized target admitted a deferred workspace save, and `--prepare-only` returned immediately, closing the serial port mid-write (that session's last line is `PERS,work,...,TrackMeta,track:7,start,ok` with no completion); the next run's upload then reset the board on top of it. **Established:** the load failure and the empty runtime. **NOT established:** that the slot payloads are erased, or the exact failure chain — a load failure is not an erasure, and the card may still hold every payload and saved set. **Fixed (host-only):** `scripts/hitl/device_shutdown.py` — canonical stop → drain → stop → drain, completion **verified** through the existing idle owner (never slept), judged from the log **tail** (anchoring at "now" reads an empty suffix as idle and reported success before the save was admitted — a real defect in the first cut, caught by its own test), disconnection/timeout named apart, incomplete shutdowns **latched**, and `DeviceSession` draining on *every* exit (normal, early return, exception) rather than per-`return`. The driver and `hitl_boot_census` both refuse an upload/reset while latched; the hold is currently set with the incident context. 15 host tests use the incident's own preserved logs as the decisive fixture (clear-run reads active, clean run reads idle — a fixed sleep could not tell them apart); a false green in the structural test was found by red-proving it and fixed. Evidence preserved with per-file sha256, commands and build identities under `openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/` (byte-exact via `.gitattributes`), and the read-only SD imaging procedure awaits authorization. **Device validation of the shutdown fix is PENDING** — it is host-tested only and cannot be device-validated while the hold is latched; its tests support the correction, they do not demonstrate it on hardware. **S1.6c.2 — capacity revalidation is BLOCKED (hard):** it needs the loaded N=56 library; an empty-library run cannot replace that experiment. No boot, no card access, no quarantine, no load, no rebuild.


**2026-09-26 — S4.1 reconciliation: the A/B/C run is the release acceptance, and the mid-pass journal resolver is fixed.** Owner correction, verified against the code: the accepted-switch retention run is not merely *related to* S4.1 — it is S4.1's device acceptance. "The slot leaves the active set" is A switching to B while its deferred finalization holds the occupancy; "the guard reports no live owner" is the release marker's `captureStopOwner == nullptr` clause, which is the `ownsLiveFinalization` term of `noLiveOwnerForSlot` (S3.2); it is identity-qualified (`canonical_release,4,40,6144,8` names the retiring owner's slot **and generation**); and **C's start is what performs the guarded working-set release** — `startRecordingTrack` → `releaseBackgroundPlaybackMergedMidiEventsMemory` → `releaseEligiblePlaybackRuntimeSlots` → `releaseSlot(slot, generation)` — followed by C's reuse, which is S4.3 verbatim. **S4.1's "rerun blocker" text was stale:** the per-interval ledger clear is converted (`clearStoppingIntervalLedger()` clears its own slot; the comment names the old global sweep as the blocker), `resetAll` is deleted and replaced, the only remaining `clearAllLedgers()` caller is the deliberate `sendAllNotesOff` sweep, and the slot-threaded persistence reads are converted. **Genuinely still wrong and now fixed:** the mid-pass journal resolver read `getActiveLoopIndex()` for a **stop in flight**, so an interval pinned to slot 5 with the active slot moved to 7 was journalled under 7; the selected-track fallback keyed a queued chunk to whatever slot was selected when the drain ran. The rule moved to its owner as `Track::captureWorkSlot()` (with `Track::pinnedStopSlot()`), refuses rather than guesses, and the resolver is now a thin loop over it. Fixture `test_s4_1_capture_work_slot_uses_the_pinned_interval_slot` asserts 1 while the active index is asserted to be 3 (so a rule that read it cannot pass), plus a refusal control and a live-capture control; **red-proved** (*Expected 1 Was 3*). Full native **2,459 passed / 1 skipped**; firmware SUCCESS, `code 392,376 / padding 840` (floor; the rule is FLASH-resident). **Recorded, not quietly dropped:** the working-set lease release is currently *inferred* from the sweep mechanism rather than asserted by name (the oracle asserts L4's finalization-capacity release); and `DeferredSaveJobStages`' footer read is a **design question** — the deferred save walks *live* state per stage, so capturing only the footer at admission would make the saved image internally inconsistent, and a snapshot-at-admission save owes its own `DEC`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.1 reconciliation".


**2026-09-26 — S4f.3 device acceptance re-run after the Tier-A fix: `RETENTION_PASS`, exit 0, zero concessions.** Run `captures/s4f3-retention-20260926_181732`, identity **`git_commit 5629e7e4f96d` / `firmware_hex_sha256 da59a7eeb3e0`** (a firmware hash distinct from every earlier run, so this is the promoted image). **20/20 checks `ok`, `concessions` empty.** The point of the run is that the rows whose absence forced `INSUFFICIENT_EVIDENCE` at 17:56 are now present in a completed run: `DIAG,reentry_window_open,4,1121,6144`, `DIAG,canonical_release,4,40,6144,8`, `DIAG,reentry,deny,1311,2,5,1` and `DIAG,reentry,bound,1706,2,5,0` — while `capture_integrity` again reports no ring eviction, the signature that the evidence was being *shed*, not evicted. **The corrected L5 ordering holds:** the bind (59,698,903 us) precedes the release record (59,723,633 us) by 24.7 ms, and the release is now read as an informational maintenance observation rather than the availability edge. Window **1550.3 ms**; C's retained request was **staged** inside it with no start executed before the release, and exactly one `RECA,6` follows the release. **L7 is loop-scoped and the run shows why:** the whole-playback review returns FAIL/`inventory_equal=False` on all 36 cycles because track 8 slots 1–4 still hold committed content whose notes sound alongside the loop under test (reported as `EXTRA`); the deciding evidence is the dump's own completeness (`declared counts {'effective#0': 2, 'record#0': 2}` match parsed) with note 67 in **both** storage and effective rows agreeing on tick/type/channel/note/velocity, `storage_matches_effective=True`. **Staging was verified, and the clear is attributable:** targets cleared as a separate deliberate step (never silently), then a read-only census re-verified eligibility — committed count **61 → 58**, exactly the three slots — and the run was gated on that census with `--no-clear`. **Driver bug found by running and fixed (`5fc74bf9`):** the prepare reported `clear_5_wrong_slot` for all three targets while the device had cleared exactly the right slots (target 5 → `active_slot=4`, 6 → 5, 7 → 6) — the firmware identity fields are 0-based and the driver's `slot_number` is 1-based, compared raw; the presses were always correct and only the verification was wrong. Invisible before the identity existed, since any marker was accepted — which is how a preparation could report success on a slot it never cleared. Also recorded: the driver's live `transport_stopped` returned false while the take committed and L7 read a complete dump, so the live check is a convenience, not a gate. **Scope closed: retention plus successor capture at that precise scope.** **Still open and not claimed:** A/B content preservation (the dump covers the active slot only), the `companion path stalled; raw pass only` fallback's content consequences, and MIDI-output playback parity, which is separate from this loop-scoped parity. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The rerun after the Tier-A fix".


**2026-09-26 — the identity-grade evidence was being *shed* by design; logging fixed, no device run.** The unexplained `INSUFFICIENT_EVIDENCE` on the verified-empty rerun is explained: **it was never a deferral or flush problem.** `CaptureLineTier::isTierALine` decides what the timing-critical shed may drop, and it drops non-Tier-A diagnostics **silently, by design, without reporting a loss** — and none of the three identity-grade tags were in its allowlist (`grep` count 0 for `DIAG,reentry,`, `DIAG,reentry_window_open,`, `DIAG,canonical_release,`). So the shed removed exactly the rows the acceptance is computed from, while `capture_integrity` correctly reported no *eviction*: shedding is not eviction. **The three tags are now Tier-A** (Tier-A is displaced only by Tier-A; these are a handful of one-shot edge rows per run), costing **no RAM1** — the predicate is called by a `FLASHMEM` function, so `code 392,376 / padding 840` is unchanged at the approved floor. **A latent bug in that very fix was caught before shipping:** the comparison was first written with a hand-counted length of 24 for `"DIAG,canonical_release,"`, whose `strlen` is **23**; including the NUL makes `strncmp` match only a tag that ends there, so the real row would never have matched and the evidence would have been shed again with the same silent signature. It now uses `strlen(...)`, pinned by a boundary test and red-proved (restoring 24 fails it). **The timely edges now carry identity as well**, so attribution no longer depends on the ring: `Overdub stop refused: <reason> slot=<n> interval=<n>`, `Track cleared @ tick <n> (active_slot=<n>)` (appended through `logTrackEvent`'s existing format parameter, so the established prefix survives every existing substring matcher), and both `Clear ignored — … (track <t> slot <n>)` forms. **Consequence — preparation is auditable:** `_clear_slot` used to accept *any* `Track cleared`/`Clear ignored` marker, which is exactly how a preparation reported success on a slot it never cleared; it now parses the identity and returns pass / fail (a clear naming a **different** slot → retry, not success) / unknown, recorded as `identity_grade: false` rather than silently counted as proof. Bases are explicit because mixing them is the trap: `active_slot=` and the `Clear ignored` field are 0-based (matching `#CAP` identity and the census), while `MIDI: Clear selected slot N` is 1-based and deliberately not used for identity. **Also fixed:** the oracle's wire-form docstring documented `DIAG,canonical_release` with 3 fields where the emitter and regex use 4, and a **stale host test** (`..._blocks_clear_without_retry`) supplied two sequencer probes for a retry path that makes three, erroring on `StopIteration` regardless of this change (confirmed pre-existing by stashing); it is replaced by two cases asserting the current, stricter behaviour. **Verification:** firmware SUCCESS (`padding 840`), all four new strings in the fresh ELF; native **2,457 succeeded / 1 skipped** (up 2); oracle 39 green (up 3), gesture plan 22 (up 7), clear precondition 28, capture transitions 48. Both new guards red-proved by mutation. **The device gate does not move:** retention plus successor capture still needs a rerun reaching `RETENTION_PASS` with zero concessions; what changed is that the previously-unexplained `INSUFFICIENT_EVIDENCE` should no longer be reachable for this reason. A/B preservation, the raw-only companion fallback and playback-output parity remain open. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The evidence edges are Tier-A now".


**2026-09-26 — S4f.3: the concession loophole closed and the evidence gaps filled (no device run).** Conceded links no longer yield a pass: **`RETENTION_PARTIAL` (exit 5)** is its own verdict and `RETENTION_PASS` (**exit 0**) is reserved for the complete, unconceded chain — `--allow-setup-issue` now counts as a concession too, since allowlisting a report issue is an operator claim rather than evidence (a partial result must not close the device gate). **L5 closed:** `beginDeferredOverdubStopForSlot` now emits `reentryDecision("bound", tick, interval, slot, depth)` on the admission-committed edge, reusing the existing emitter and wire format, so a denial and its later binding correlate on the **same interval identity** instead of being inferred; a `bound` record for the denied interval *before* the release is a `RETENTION_FAIL`. **RAM1 floor preserved and measured:** `padding` byte-identical at **840** with FLASH `+104 bytes`, because the function is `TRACK_COLD_MEM` (FLASH-resident) and never enters the ITCM segment the floor measures — no recovery was needed. **L3:** the retained row now names track *and* slot, and the oracle requires both plus positional ordering (recorded as positional, since a human log row has no timestamp). **L7:** now requires the **existing content pipeline** — the `!CAPTURE_CONTENT` dump for the successor slot's real pitches plus `content-review.json` parity; a review alone is parity-only and insufficient. **36 host tests green**, including the complete unconceded chain reaching `RETENTION_PASS`/exit 0 with zero concessions; firmware SUCCESS. **Step 3, the first authorized no-concession device run, was NOT performed:** it needs a host sequence for the real gestures, and that flow does not exist (`queued_switch.py` is a Phase-1 skeleton; the action layer has no per-slot record press or slot/track select). Building it is host work with device round-trips, so it is recorded as the next step rather than rushed. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (the concession loophole closed).


**2026-09-26 — S4f.3 retention oracle tightened (still no device run).** The first oracle could falsely report `RETENTION_PASS`; the owner reproduced two cases and both are now refused: C starting **before** A releases capacity (was a pass → now `RETENTION_FAIL`, exit 2, failing L3 and L6) and removal of B's **identity-specific** denial with the generic `successor_stop_denied_capacity` row left in place (was a pass → now `INSUFFICIENT_EVIDENCE`, exit 4). The oracle now demands an **ordered chain** — L1 A's finalizer outstanding → L2 B's stop denied **with slot and interval** → L3 C's request retained → L4 capacity released → L5 that same B stop bound → L6 C starts exactly once **after the release** → L7 C recorded the expected MIDI → L8 bounded follow-up with no duplicates — where any missing or reordered link is never a pass. New verdict `INSUFFICIENT_EVIDENCE` (exit 4) separates "staged but a link lacks telemetry" from `SETUP_INCOMPLETE` (exit 3, "this run is not evidence"), and integrity gates now block a pass on missing build identity, ring overflow, a truncated log, unconfirmed setup transitions, and any base-report issue that is not explicitly allowlisted. **Telemetry gaps are now reported rather than inferred:** L5's identity link is unavailable (`captureInterval_` is emitted only by `reentryDecision`, while stop-tier records carry `frozenGeneration` = `staging.captureGeneration`, a different identity space), L7 is count-level because no per-note capture record has a caller, and L3 rests on a human log line — each passes only via an explicit, recorded **concession**. **Corrections:** exit-code separation does not establish that out-of-range notes were generator artefacts (an allowlisting is recorded as an operator claim), and the earlier "no finalizer-pause control is required" is **withdrawn** — the 1.335 s window is a timing observation from a session with no boot banner, so the hook stays a contingency. Evidence: 29 host tests (one negative per way to lose a link), CLI reproductions at exit 2 and 4, and the real log returning `SETUP_INCOMPLETE` (exit 3) while still measuring the window; three oracle bugs found by its own tests were fixed. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (the retention oracle tightened).


**2026-09-26 — S4f.3 device-retention stimulus and oracle prepared (NOT run).** Per owner direction, prepared only: no device run, no target slot assumed authorized. **Assessment — telemetry and exposure already exist, so no new instrumentation and no pause hook is needed.** The requirement is decidable from existing records keyed on slot and interval identity: `DIAG,reentry_window_open,<slot>,…` (DEC-067 `successor_start_window_open` = the predecessor's finalizer admitted and outstanding, named by slot), `DIAG,canonical_release,<slot>,…` (the denied window closes), `DIAG,reentry,deny/deny_resource,<tick>,<interval>,<slot>,<depth>` plus `ODUB,stop,admission,…,successor_stop_denied_capacity`, and `RECA,<slot>,<tick>` (the start, so exactly-once is a count) — never the 21 µs `OVERDUB_STOPPING` state that defeated the interruption fixture. **Measured window:** real device records `reentry_window_open,0,16387,1536` (136,629,769 µs) → `canonical_release,0,1536,8` (137,965,158 µs) = **1,335,389 µs (1.335 s)** for a 2-bar loop, comfortably wider than two host gestures; that session has no boot banner, so the figure is a timing observation to reconfirm under a fresh boot. **Oracle:** `scripts/hitl/retention_probe.py` → `retention-review.json`, verdicts and exit codes keeping setup distinct from retention (`SETUP_INCOMPLETE` 3, `RETENTION_NOT_EXERCISED` 2, `RETENTION_FAIL` 2, `RETENTION_PASS` 0), with `fresh_boot`, `tempo_verified` (read-back required), `seed_content_on_a`, `finalizer_admitted_on_a`, `capacity_released_after_a`, `capacity_denied_window_wide_enough`, `successor_stop_admission_denied` (inside A's window), `successor_started_exactly_once`, `activation_invariant_held`, `no_stop_obligation_replay`. Validated without a device: **11/11 host tests** (`scripts/test_retention_probe.py`) and a real-log run that returns `SETUP_INCOMPLETE` while still measuring the window. **Stimulus:** `STIM-BASE-2-ONE-OD-01-TEMP-RETENTION-A` authored via the sanctioned `stimulus temp` fork, not registered, sidecar under the change directory; pass criteria live in the oracle. **Setup gap:** the three targets (A/B/C on one track) are **not yet authorized** — the documented scratch slot is a single slot (track 5 / slot 1). **Adjacent defect, reported not fixed:** `scripts/hitl/stimuli/yaml_view.py` `dump_yaml` emits invalid YAML (dict branch tests `isinstance(item, list)` while `StimulusSpec` fields are tuples). Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (device-retention stimulus and oracle).


**2026-09-26 — S4f.3 HITL attempt — no pass, and the new path is uncovered.** Flashed the verified image (`firmware_hex_sha256 14a1aea7…`, `git_commit e743b97c…`) and ran the registered entry points per the hitl-content-verification skill (fresh upload; documented scratch slot **track 5 / slot 1**). The stop-interruption wrapper reached the fixture (`PLAYING->OVERDUBBING: 1`, record `32/32`, persist `ok=True`, `OVERDUB_STOPPING: 1`, intervention sent) but returned `INCONCLUSIVE_CAPTURE` / `no_device_stop_inside_stopping`; the log shows the `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` window is **21 µs** because the admitted stop defers finalization (DEC-078), so the fixture's premise is architecturally mismatched rather than flaky, and no delay value can hit it. The base leg's `overdub_unmatched_open_notes` / `overdub_out_of_range_notes` are the skill's setup verdicts. The baseline preset (`STIM-BASE-2-2-01`) returned `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` with `RECORDING: 0` — the known base-preset setup race already attributed to the still-open selection fixture — at a correct tempo (`record loop length 1536/1536`). **Tempo must be re-pinned per run**: the device was found at 160.0 BPM and the DEC-086 fader pinned it back to 120.0. **No registered stimulus covers this slice's behaviour**, so these runs are regression context only, and the owner declined a temporary variant this session — the retention path is **host-verified only**. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (HITL attempt).


**2026-09-25 — S4f.3 (PARTIAL 3) — accepted-switch retention under unavailable stop admission.** A switch/start requested while the predecessor's stop **cannot be admitted** is no longer dropped: `TrackManager::retainRecordStartForSlot` records it in the **existing** manager pending-start owner (no new owner, no new storage) without moving the audible set, and `handleToggleRecordForSlot` calls it on the uncommitted path. `finalizeCaptureAndSelectSlot` now performs **explicit supersession scoped to the finalized slot** (`clearQueuedRecordingTrack(trackIndex, captureSlot)`, plus dropping the retained request's phase reference when it is that slot) instead of clearing the whole track's pending-start state before it knows the switch will commit. Three corrections came with it: the overdub guard is **slot-scoped** (`hasLiveCaptureStopJobForSlot` / `hasAcceptedInteractionForSlot`) because `hasPendingDeferredOverdubStop()` is **track-wide** and would let another slot's job stand in for this interval's unadmitted stop; re-entering `stopOverdubbing()` on an already-admitted stop is avoided because it **re-latches** (its guard fires when a job is already pending) and would block the very switch being made; and **the activation hazard was found and fixed** — `handlePendingRecordStart` activated with the unconditional `Track::setActiveLoopIndex`, moving the active slot underneath an unadmitted predecessor stop, so it now uses `TrackManager::setActiveLoopIndex` and **explicitly asserts** that a failed activation leaves the slot unchanged with the predecessor's stop in flight. Evidence: `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` (A's stop held, B overdubbing with its stop capacity-denied, C's switch/start retained through 2,304 dispatches, capacity released, B's stop binds, C starts and records, no double execution), **red-proved three ways** with distinct failure lines, and a **false green found and removed** (the replaced fixture returned early from a characterization branch in the default gate while holding nothing). Full native `2,448 passed / 1 skipped`; firmware SUCCESS. **RAM1 is now exactly on its approved floor** (`code 392,376 / padding 840`; 272 bytes spent, gating the diagnostic recovered none because the strings are FLASH-resident) — the next slice here must free RAM1 or revisit the floor. **Immediate repeated handoffs (B→C while A finalizes) remain explicitly open**: retention is proven, immediate execution is the separate finalizer-capacity question. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3.


**2026-09-25 — S4f.3 (PARTIAL 2) — the switch carries the start.** `finalizeCaptureAndSelectSlot` returns whether it committed, and the capture-switch branch of `handleToggleRecordForSlot` queues the record start for a record target at that commit point with **no** phase reference, so an empty/cleared target starts on the transport's bar grid and the predecessor's loop timing never becomes an accidental dependency of the target's start. Fixture `test_the_switch_gesture_carries_the_start_without_a_second_press` asserts the request after one gesture and drives the dispatcher with no further press; red-proved by disabling the carrier (`1 failed, 11 succeeded`) while the two-gesture acceptance fixture still passed, so it discriminates. Green both gate modes; full native `2,448 passed / 1 skipped`; firmware SUCCESS, `code +64 / padding -64`, above the 840-byte floor. Still open: the **uncommitted** switch — reachable when `stopOverdubbing` denies admission on capacity (DEC-067), where the press is dropped rather than retained — plus the unconditional pending-record clear at the top of the switch path. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3.

**2026-09-25 — S4f.6 — accepted-start carrier; S4b.1's acceptance passes end-to-end.** `TrackManager::startRecordingTrack` returns `bool` and refuses **per slot** (`Track::hasStopInFlightForSlot`), so a stop bound to A no longer blocks a start on B; `handlePendingRecordStart` activates the target first and consumes the request only on success, so a refusal retains it. **Retraction:** the refusal did not "strand" a retained request — the dispatcher consumed **unconditionally**, so it **discarded** it; the earlier path review's mechanism sentence is corrected in the packet. The acceptance fixture's characterization branches are deleted (claims unconditional, fallback press removed) and it is **red-proved** against the pre-S4f.6 guard (`2 failed, 10 succeeded`); the unadmitted-stop fixture is renamed because its old name overclaimed (switch only) and now also drives the start. Full native `2,448 passed / 1 skipped`; firmware SUCCESS, RAM1 byte-identical (`padding 1,176`). Open: S4f.3's carrier (the switch's own gesture still does not carry the start), the newly found unconditional pending-record clear at the top of `finalizeCaptureAndSelectSlot`, check 7's cross-track and cleared/populated coverage, and repeated B→C as a finalizer-**capacity** case. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.6.

**2026-09-25 — S4b.1 (second pass) — fixture tightened; unadmitted-stop retention already holds.** The acceptance fixture now requires an **admitted** finalizer and drives B through the production per-slot gesture and real MIDI handler against an empty target. The admitted-and-paused case remains blocked on **S4f.3**, but a separate fixture proves the **unadmitted-stop** case already retains the switch and start and executes them once admission allows, without another gesture. Storage/display boundaries crossed by the gesture are observable for switch suites; the real edit-depart closure was rejected as too heavy. Open: S4f.3, cross-track switch, and repeated B→C (finalizer capacity, not `Omax` alone). Full native `2,448 passed / 1 skipped`. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4b.1.

**2026-09-25 — S4b.1 — switch acceptance executable; blocked on S4f.3.** The exact handoff gesture (A overdubbing → stop admitted, finalizer paused → switch to B → B records MIDI while A finalizes) is a fixture in `test_runtime_working_set_ownership`, asserting B's recorded events through the production switch and MIDI route. It fails at the switch: the path holds the active slot while the stop is unadmitted/in flight, which S4f.5 requires and which S4f.3's accepted-interaction carrier must replace. Removing the guard passes the acceptance but breaks `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`, so it was reverted; the fixture asserts the current behavior by default and the requirement (red) under `S4B_ACCEPTANCE=1`. Cross-track and repeated B→C switches remain unproven and need the bounded overlap policy. S4.4's "envelope stricter than the model" claim is retracted: the census mislabeled an outgoing finalizer as `LiveCapture`. Full native `2,447 passed / 1 skipped`. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4b.1.

**2026-09-25 — S4.4 — declared cap policy reports, and does not refuse.** `Track::reportEnvelopeBreachIfAny` reports a declared-envelope breach as `reason=cap_reached` (distinct from an allocation failure) via the new `MemoryMonitor::reportDeclaredCapDenied`, with no new storage, while letting the activation proceed. The first cut enforced the caps at the activation entry and broke the suite: the declared envelope is stricter than the resource model (per-slot capture roles are supported; 85 fixtures arm a second slot), so enforcement would reject supported flows. Enforcement reverted, observation kept; enforcing caps remain the lease's own. The refuse-vs-queue decision is deliberately left open — it needs a cap the model also enforces or an architecture decision. Full native `2,446 passed / 1 skipped`; RAM1 byte-identical. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.4.

**2026-09-25 — S4.3 — reactivation reuses released capacity.** Release retires a slot's working set but keeps its `LoopPlaybackRuntime` in `runtimeBySlot_`, so re-activation re-admits into the same object at a stable address with its generation advancing rather than accumulating. A four-cycle fixture with a baseline control asserts address identity, role-set stability and a stable chunk pool, and is red-proven with the sweep disabled. The entry has a production caller already (`startRecordingOnTrack` sweeps every other track, plus the memory-pressure path). Native has no PSRAM, so the byte-level device claim stays open with S2.2/S2.3. Full native `2,445 passed / 1 skipped`, RAM1 byte-identical. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.3.

**2026-09-25 — S4.1/S4.2 — the `(slot, generation)` release entry.** `TrackPlaybackRuntime::releaseSlot` is the single, identity-qualified release entry (`NotHeld` / `StaleGeneration` / `RefusedBusy` / `Released`), applied per slot behind `noLiveOwnerForSlot` by `Track::releaseEligiblePlaybackRuntimeSlots`; the predicate-free `resetAll` is deleted. The slot's runtime generation now advances on release, so the S3.1 lease release path is live. No new state (identity is the lease generation), `sizeof(TrackPlaybackRuntime)` stays 32, RAM1 byte-identical. Both fixtures red-proven. Full native `2,444 passed / 1 skipped`. Open: the persistence footer read and the role-transition sweep callers. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.1/S4.2.

**2026-09-25 — S4.2 — generation-qualified finalization identity.** `captureStopOwner` is now qualified by the owner's generation (new `captureStopOwnerGeneration`, with `set`/`clear` as the only writers), so a predecessor's late cleanup on a reused runtime object is a rejected no-op instead of nulling a successor's marker. Fixture covers A-releases / C-reuses / late-A with the matching-identity control and is red-proven. The field fit existing padding: `sizeof(LoopPlaybackRuntime)` stays 6,344 and RAM1 is byte-identical. Full native `2,442 passed / 1 skipped`. Open: the per-slot runtime generation is not bumped on release, so the lease's release methods remain uncalled. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.2.

**2026-09-25 — S4.1 (first slice) — per-interval ledger scoping.** A stop used to clear every allocated slot's ledger, so a sibling slot's stop erased a finalizing predecessor's open-note suppression state. `Track::clearStoppingIntervalLedger()` now scopes the clear to the stopping interval, both converted stop routes call it, and the deliberate global sweep (`sendAllNotesOff`) is unchanged. The fixture is red-proven against the pre-fix behavior; an earlier version of it was a false green and that is recorded. Full native `2,441 passed / 1 skipped`, capture-serial builds, RAM1 `variables` unchanged. Open remainder: the memory-pressure `resetAll` conversions, the `DeferredSaveJobStages` footer read, and the `(slot, generation)` release surface (S4.2). Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.1.

**2026-09-25 — S3 gate — acceptance suite landed (`test_runtime_working_set_ownership`).** Boot/load as an admission boundary, the record arm's role establishment, liveness-guarded release per owner kind (with retirement controls), and selection acquiring nothing — asserted over the production admission paths, linked as a firmware-boundary suite. 4 cases green; full native `2,440 passed / 1 skipped` (98 suites); `test_playback_prewarm` unchanged; firmware RAM1 byte-identical. **Gate stays open** on the S2b device gates and the S2.2/S2.3 current-root budget, which are device measurements. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3 gate.

**2026-09-25 — S3.3 — selection acquires nothing.** A fixture browses three committed slots through the production selection mechanism and proves each slot's lease stays unheld (`None`, generation 0) with no runtime admitted, so selection cannot depend on working-set availability; the record arm is the positive control. No production line changed and firmware RAM1 is byte-identical — the churn counters a first cut added were rejected by the `LoopPlaybackRuntime` size pin and removed rather than re-pinning. `test_continuous_capture` 101/101; full native `2,436 passed / 1 skipped` (clean build, 97 suites). Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.3.

**2026-09-25 — S3.2 — per-owner-kind liveness predicates and their discriminating fixture.** `noLiveOwnerForSlot` is now a list of named terms (`hasLiveCaptureStopJobForSlot`, `hasAcceptedInteractionForSlot`, `ownsLiveFinalization`, `ownsInFlightPreparedHandover`) with unchanged meaning and callers; the job-owned undo root (S5.2a) and deferred reconstruction (S5.2e) remain inexpressible and are recorded with their stages, and the published pair stays deliberately not a term. `test_continuous_capture` gains a fixture holding one owner kind at a time with two inverse controls; suite 101/101, full native **2,520 passed / 1 skipped**, RAM1 flat, capture-serial builds. Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.2.

**2026-09-25 — S3.1 — working-set lease vocabulary landed and routed.** `WorkingSetLeaseSlot` (per-slot, on the externally allocated `LoopPlaybackRuntime`) plus `WorkingSetLeaseTable<MaxSlots>` give the runtime a generation-qualified identity over a declared cap; operations are O(1) and allocation-free and report `CapReached` / `InvalidRole` / `NotHeld` / `StaleGeneration`. The record arm (`prewarmPlaybackForSlot`) and the boot-cold overdub start (`admitOverdubStartWorkingSet`) both record `PlaybackWindow | LiveCapture`; the boot pass moved into `Track::prewarmBootPlaybackRuntimeSlots()` so the admission predicate and the lease share one owner. Behavior unchanged: `test_playback_prewarm` and full native **2,434 passed / 1 skipped**; capture-serial builds with `variables` flat (`code 391,912 / padding 1,304`) and both struct pins intact. Release is S4.1's; S4.2 reads the generation for `captureStopOwner`. Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.1.

**2026-09-25 — S1.6c.2c — loop-edit tempo fader (DEC-086), device-verified.** Fader 4 on the loop-edit control channel now sets the global tempo: **ch16 CC102, one BPM per step over 60–187 BPM**, applied immediately through `ClockManager::setBpmFloat`; entering loop edit sends the current tempo back so the motor parks at the whole-BPM value. The law is the dependency-free `TempoMapping` header, so firmware and host tests cannot drift (`test/test_tempo_mapping`, 10/10). Device-verified via `scripts/hitl_tempo_fader_probe.py`: CC60 → **120.0 BPM exactly** (`captures/tempo-pin-120b/`); under the first 72–200 BPM law, CC32/100/64 → 104.3/172.8/136.5 (`captures/tempo-fader-01/`). RAM1 unchanged. Added at the owner's request as fixture support for the 24-bar capacity revalidation, which previously could not pin the device tempo.

**2026-09-25 — S1.6c.2b — no-clear selection fixture repaired (host-only).** The no-clear record path skipped the clear *and* the slot selection, so the recorded `snapshot-reuse-24bar` run armed track8/slot1 against a configured slot 4. Selection is now a pre-arm step that stops the transport, taps the Loops row and retires the ghost capture, with slot identity proven by a bounded wait on the target's DISP row (`_await_observed_record_slot`) before the arm press; `select_loop_slot=False` keeps the edit path from a second tap. 17 focused host tests green; full host script suite unchanged at its 8 pre-existing failures. **The device capacity gate remains open:** the corrected 24-bar run reached RECORDING but the fixture note stream did not start (1 note-on, 0.6 s take, `record stop did not reach PLAYING`), and the failed runs left the authorized witness slot 4 populated, so the guard now refuses. Resume after the owner re-verifies an empty target, with the tempo pinned via DEC-086.

**2026-09-25 — display transfer is DMA-backed and recoverable (`recoverable-async-display-dma`).** The SSD1322 framebuffer now leaves the CPU over SPI DMA, with blocking SPI retained as both the post-failure fallback and the rollback. Activation is the default for every build (`MIDILOOPER_DISPLAY_DMA`; `=0` compiles the CPU-driven baseline). Device: `init=1` with `dma_frames` climbing and `blocking_frames=0`, and per-frame `DFRAME` transfer time **44–53 µs against the 4,495–4,670 µs baseline** (~100x); the `=0` build records `init=0, dma_frames=0, blocking_frames=900` and 4,496–4,802 µs, so the census is the control that makes the comparison trustworthy. The timeout recovery re-arms the GRAM window before its resynchronisation frame — the pre-fix write did not start at row 0 after a partially shifted aborted transfer. Recovery is device-verified with injected faults (`MIDILOOPER_DISPLAY_DMA_FAULT=1` reject, `=2` timeout, with a fired-event count): both transient variants reach `DMA recovered` after the cooldown with one blocking frame in the window, and the permanent variants degrade at the third consecutive failure and then serve every later frame over blocking SPI. A dependency-free policy object (`SSD1322_DMA_HEALTH`) carries the bounds, cooldown and census so the host test drives the production transitions (9/9); `DisplayManager` publishes a bounded on-change `#DMA_XFER` census under `SESSION_CAPTURE`. RAM1 is byte-identical to the pre-change build (`96,160 / 391,448 / 1,768 / 34,912`). **Complete and archived 2026-09-25 (14/14):** `recoverable-display-dma-transport` is now the normative spec. Task 4.1 added deferred-transfer admission so a frame that the busy transport refuses stays pending instead of being acknowledged untransferred (`admitDisplayTransfer`, `advanceDisplayFramePhase(ran, transferDeferred)`, device-smoked at `dma_frames` 1 → 136 with `blocking_frames=0`); 2.1/2.2 verified `abortAndReset()` on hardware — two injected timeouts then `DMA recovered` with `dma_frames` 13 → 119 on the same channels, i.e. the wrapper is left idle and reusable with no channel re-initialisation. The managed baseline-vs-DMA pair returned `INCONCLUSIVE_CAPTURE` on the base preset's setup-race class, present with DMA off too and owned by the still-open `runtime-working-set-ownership` selection fixture — not by this change. Not yet on `dev`: it rides `feature/runtime-working-set-ownership`, so `DELIVERABLE_TRACKING` updates at the PR.

**2026-09-25 — S1.6c.2 — snapshot construction reuse implemented, device gate open.** Candidate-private pitch/page/membership edits now persist across one construction (DEC-085), with cached tail/count updates and cached-event sort-state reset. Native **2,402 passed / 1 skipped**, focused snapshot **42/42**, target build green; RAM1 unchanged (code391,368 + padding1,848; variables96,160; locals34,912). Image `94b09b51…`. Fresh N=56 census: external payload **7,006,576 → 3,238,256 bytes**, snapshot slabs **151 → 36**; all 64 observed slot records match the earlier census (both lack consistent-view attestation). This saves **3,768,320 bytes (~3.59 MiB)** without deleting content or shrinking the capture ring.
Stopped-only follow-up (`captures/snapshot-reuse-24bar/stopped-heap.log`): internal arena356,800B, allocated chunk bytes199,392B, free chunks157,408B, largest free chunk122,768B, unused tail12,288B; external payload5,246,288B, free3,100,380B, largest request3,046,620B. Both scans complete. This disproves interpreting the *stopped* 12KiB tail as total available heap. It does not retroactively measure free lists at the earlier live refusals; tail-based snapshot admission remains the next hypothesis to verify without live scans. No leak/fragmentation verdict from this one stopped reading.


The stress rerun is **INVALID_TARGET_INTERRUPTED**, not acceptance: configured track8/slot4 but the no-clear harness armed selected **track8/slot1**, recorded/overdubbed and persisted `slot:7:0`. Interrupted and explicitly stopped; `STOP_STATUS` confirms STOPPED/pending=0. No clear/delete/rename or cleanup. User reports device85.4BPM, explaining host24-bar timing yielding17devicebars (13,056ticks). No fatal/external refusal, but seven late `snapshot_reserve/heap_low` rows and minimum heap tail12KiB mean overall heap correctness remains open. Host precondition now fails before gestures unless the latest device-time DISP since track selection verifies the configured slot; host tests56/56. No further content runs until the selection fixture is corrected. Role-specific reservations remain unchanged pending their transition/admission contract. Evidence: `captures/snapshot-reuse-preflight/`, `captures/snapshot-reuse-24bar/`, `captures/snapshot-reuse-image/`. Recorded in local runtime checkpoints; capacity acceptance remains open.


**2026-09-25 — S1.6c.1 — known owner lifetime fixes delivered in the working tree:** whole-take release now frees origin/rank side arrays (24,576 bytes of previously retained payload); draw-source compose reuses pending-paint backing, filters slide inclusion in place, and retires obsolete paint before allocating a larger copy. Selected-bridge authority and compose/paint separation preserved. Red→green regressions; focused 301/301, full native **2,399 passed / 1 skipped**; capture-serial SUCCESS, RAM budgets unchanged. Final image `10695e07…`. **Capacity is not fixed:** the same N=56 24-bar HITL (`captures/heap-ownerfix-24bar-02`) still fatals ~47.62 s into RECORD on a 6,096-byte DisplayNote vector reserve, before overdub. S1.6c — owner correction/capacity revalidation remains open. No live heap scan or content-clearing workaround. Board recovered: N=56, chunks403, target empty, zero differences in compared content fields across 64 slots. Edits uncommitted.


**2026-09-25 — S1.6b — original workload rerun:** same effective N=56 24-bar record/24-bar overdub configuration, user track 8 slot 4, no CLEAR, image `a71b50eb…`. `captures/heap-rerun-24bar-01/` reproduces fatal exhaustion ~32.08 s into RECORD: 8192-byte DisplayNote vector growth request, caller `0x6008c919`, external live 8144 KiB / 1176 blocks, reported internal tail 8 KiB. No overdub or record stop reached; harness SERIAL_INCOMPLETE. No heap scan during the workload. Stopped preflight scan complete; no same-boot after-sample because firmware aborted. Recovery returns N=56 / 403 free chunks, target empty, zero differences in 10 compared content fields across 64 slots (not a byte/write guarantee; observation consistent_view=false is not a new stability proof). Exact display container and leak/fragmentation remain unresolved. Evidence: active runtime review packet and `captures/heap-rerun-24bar-01/result-summary.json`. No firmware/script changes in this rerun; OpenSpec gates remain unchanged.


**2026-09-25 — S1.6d — stopped heap diagnostics:** added capture-only `!HEAP_STATS` under MemoryMonitor. No periodic/live allocator inspection or allocation probes. Refuses transport, active tracks/capture/stop work, deferred storage/prewarm, selected external clock, and pulses received within the last second. Raw allocator chunk/gap metrics, unused internal tail, completeness and scan duration are separate. DEC-084 records the explicit stopped/clock-quiet exception. Native **2,394 passed / 1 skipped**, focused diagnostic **5/5**, capture-serial build green (RAM1 variables 96,160; code 391,368; padding 1,848; locals 34,912). Initial stopped readings complete in ~49 ms; final stopped/clock-refusal device gate passed in `captures/stopped-heap-03/` (exact refusal, no scan). S1.6d is checked. Work remains uncommitted. This adds evidence, not a malloc leak verdict or the S1.6c — owner correction; capacity gates remain open.


**2026-09-25 — S1.6 heap correctness:** physical external accounting now excludes internal
fallback, balances tags on address reuse, and reuses saturated-table tombstones. Three native
red regressions → 5/5; full native 2,389 passed / 1 skipped; capture-serial green and flashed,
RAM1 unchanged. Capacity sweeps remain paused. The N=56 canonical 24-bar run still fails in
recording: 5,216-byte request from `DisplayManager::resolveDisplayNotesByDrawSource`, resolved
against pinned ELF, external live8,142KB with no accounting misses. This is not attribution of
the older 8KB request or proof of a leak. S1.6a is complete; S1.6b allocation/lifetime attribution
and S1.6c owner fix remain open, as do S1.5 overdub refusal and S2b.3 workload-peak budget.
See active runtime-working-set-ownership ARCHITECTURE-REVIEW.md § S1.6 and
`captures/heap-attribution-24bar-01/allocation-attribution.json`. Earlier lower-N/reclassify
recommendations are superseded by this heap-correctness priority. Work is uncommitted.
Recovery confirmed N=56 / 403 free chunks, zero observed content-field differences across
64 slots, and the failed target still empty. Board recovered; no clear/delete/rename.


**2026-09-25 — S1.5 device refusal-point attempted on the N=56 library: REFUSED IN THE RECORD PHASE, so the overdub reserve admission was never reached. S1.5 stays open.**
Pinned capture-serial image `b289af53…`, git `1ccff16a`; target **user track 8 slot 4**
(verified empty, authorized this session); fixture `STIM-BASE-2-2-01-TEMP-S15-24BAR` — the RWSO-24CAP
canonical shape with `--record-bars 24 --overdub-bars 24` and `--no-clear-before-record`. Pre-flight
(read-only): complete 64/64 observation, payload 59, initial playback 8, committed 56
(pass-backed 56 / root-backed 53), `chunks_free 403`, `live 6846 KB`, `heap_free 84 KB`, target empty.
The device entered RECORDING at 89.169 s; from 91.961 s `snapshot_reserve` denied every ~1 s
(`need=40960B`, `heap_free` 68 → 0 KB over 39 denials); at 130.538 s
`MEM_EXT_REFUSED bytes=8192 tag=0 heap_free=0KB live=8527KB chunks_free=391` plus the fatal exhaustion
line, then no response (no crash report, no reboot). `Overdubbing started` = **0** and
`op=overdub_start` denials = **0**, so the **overdub reserve admission was never reached** — this is the
S1.3 record-phase hard-failure class (internal heap, not chunks: 391 of 512 free; `snap` unchanged),
not the S1.5 overdub-refusal clause. Bracket **(2, 56]**: admitted at the 2-slot RWSO-24CAP library
(2026-09-23), refused here; S1.3's 2-bar slope recorded its first named denial at N = 55.

Recovery boot is identical: banner, `reconcile=0`, all readiness fields, complete observation,
payload 59, initial playback 8, committed 56 (56/53), **0 content-field differences over all 64 slots**,
target (8,4) still empty, `chunks_free 403`, `live 6842 KB`. No clear/delete/rename; only the authorized
target was addressed.

**S1.5 remains open.** The next bounded step is a target that lets the 24-bar loop exist at an N with
internal-heap headroom (record it, then overdub it), or an explicit decision to treat the N = 56
record-phase refusal as the S1.5 / S2b.3 capacity boundary. No firmware change and no `DEC-###` owed.
Evidence `captures/s15-24bar-t8s4-01/refusal-analysis.json`, `captures/s15-preflight-01/`,
`captures/s15-recovery-02/`; full record
[`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ S1.5 — device refusal-point result. Work remains uncommitted on `feature/runtime-working-set-ownership`
(HEAD `1ccff16a`).

**2026-09-25 — S1.3 residency slope COMPLETE: dense probe reached hard allocation failure.**
The fixed-role two-bar slope remains N=10..56 (46 verified additions). A separate dense
four-bar probe at N=56, user track 8 slot 4, supplied a one-clock note cadence across pitches
0–127. Recording started; at 41.448245 s device time, before record stop, the allocator refused
8,192 bytes with 4 KB internal heap free (`tag=0`) and emitted the fatal exhaustion line.
This is the measured failure point for this library/workload, not a universal smallest N or a
64-bar capacity claim. The untagged allocating caller is not identified. The last sampled free
chunk count was 393: do not classify this as chunk-count exhaustion.

The failed take had no verified persisted payload; the fixture withheld its survival reboot.
Separate crash recovery and a final read-only boot confirm the original 56 loaded slots,
unchanged other observed content fields, matching boot roles, and an empty failed target.
Board is stopped at track 6 slot 3. No clear/delete/rename or existing-slot overwrite.

S1.3 is checked: measured slope plus hard failure/error, alongside the previously completed
synthetic 60-descriptor characterization. S1.5 — 24-bar overdub refusal, S1.4 — undo/pin baseline,
S2b.3 — workload-peak budget and the other device gates remain open. No S3 — lease ownership start.
Host tests **42/42**, OpenSpec/diff checks pass. No firmware edits/build this slice; pinned image
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208` reused.
Evidence: `captures/s13-dense-evidence/pressure-result.json`, `captures/s13-dense-t8s4-01/`,
`captures/s13-dense-recovery-01/result.json`; existing slope in `captures/s13-slope-evidence/extended-slope.json`.
Work remains uncommitted on `feature/runtime-working-set-ownership` (HEAD `1ccff16a`).

<!-- S13_EXTENDED_PROGRESS_BEGIN -->
**Historical checkpoint — superseded by dense-probe completion above: S1.3 extended sweep N=10..56.**
46 two-bar additions passed exact 62-event / 1,536-tick SD/RAM survival before and after reboot.
All boot roles stayed fixed; no other observed content fields changed; 45 repeated intermediate
owner baselines matched exactly. Free chunks 449->403; snapshot telemetry 2,187->4,196 KB;
slab-used 2,491->4,774 KB; tracked live 4,221->6,846 KB. Tracked live/high includes the core
`extmem_malloc` internal fallback and is NOT a physical PSRAM-free oracle.

First named denial occurred at N=55 while recording the 56th slot (user track 8 slot 3):
`snapshot_reserve / heap_low`, 40,960-byte margin + 32,768-byte floor, heap 48 then 44 KB;
17 denial lines, zero wrapper allocation refusals. Publication deferred; the take persisted in
legacy format and survived reboot exactly. This is a temporary publication-admission boundary,
not a hard residency/canonical failure. S1.3 remained open at that earlier checkpoint;
S1.5 — 24-bar overdub refusal and S2b.3 — workload-peak reserve remain OPEN. S3 is not started.

Growth stopped at that boundary. The supervisor initially advanced because survival passed;
the next target (track 8 slot 4) was interrupted during preflight before recording. The host
runner now returns a stop status on named memory denials even if survival succeeds. Final fresh
read-only boot confirms 56 loaded, same observed content/roles, stopped focus track 6 slot 3.
No clearing/deleting/renaming or populated-slot overwrite. User authorization for empty slots
was sufficient; this stop is a measured boundary, not exhausted authorization.

Evidence: `captures/s13-slope-evidence/extended-slope.json`, `extended-episode-peaks.json`,
`captures/s13-empty-anchor-t8s3-01/`, `captures/s13-sweep-closeout-01/`. Host tests 40/40;
OpenSpec validates. Firmware unchanged this slice (SHA256 `b289af53…`, code 391,304 / padding
1,912); prior native evidence 2,384 passed / 1 skipped remains applicable, not newly rerun.
Changes remain uncommitted on `feature/runtime-working-set-ownership`.
<!-- S13_EXTENDED_PROGRESS_END -->

**2026-09-25 — S1.3 fixed-role growth MEASURED, range 10–15 loaded slots.**
Five two-bar takes into user track 6 slots 4–8 passed exact SD/RAM event-and-length parity
before and after reboot (62 rows / 1,536 ticks each). All ten boot checkpoints used the same
stopped transport and boot playback footer; focus was restored to track 6 slot 3. Each loaded-set
comparison added only its intended target and lost none; existing observed content fields stayed
unchanged. Four repeated intermediate baselines reproduced every reported owner total exactly.
Payload inventory stayed **36**; content union/pass count **10 -> 15**, root count **8 -> 13**.

Post-restore free chunks: **449, 448, 447, 446, 445, 444** at N=10..15. Cold reservation walk
remained **512 -> 480**, all walked credits zero (not workload peak). Snapshot telemetry grew
**2,187 -> 2,410 KB**, slab-used **2,491 -> 2,745 KB**, live **4,221 -> 4,799 KB**; heap_free
stayed 84 KB. Live increments are **65, 352, 64, 33, 64 KB**: retain the N=11->12 jump,
not a single linear per-slot attribution. Subsequent code/capture attribution identifies the 64->65 canonical-chunk backing-block boundary
(328,448 bytes); see the extended review entry.
No observed MEM_EXT_REFUSED, MEM_DENIED, FATAL or RING overflow in the ten measurement logs.

**Stopped because all approved empty scratch slots are now populated, not because of capacity.**
S1.3 and S1.5 remain unchecked: failure point not reached, two-bar workload only. S2b.3 remains
open because workload peak reservation is unmeasured; S3 does not start. Next device growth needs
explicit authorization for additional verified-empty slots outside the prior scratch range (the final
manifest lists candidates), or permission to recycle a named scratch subset. No clear/delete/rename
or recording outside track 6 slots 4–8 occurred this slice. Board left stopped, focus track 6 slot 3.

Evidence: `captures/s13-slope-evidence/slope.json`; detailed runs `s13-slope-slot4-04` and
`s13-slope-slot5-01` through `s13-slope-slot8-01`. Full native **2,384 passed / 1 skipped / 0 failures**;
decoder 51/51; host 35/35; OpenSpec/diff checks pass. Firmware unchanged from the prior green
capture-serial image (SHA256 `b289af53…`, code 391,304 / padding 1,912); only host tools/tests/docs
changed this slice. Work remains uncommitted on `feature/runtime-working-set-ownership`.


**2026-09-25 — S1.3 controlled one-slot survival PASS (uncommitted branch work).**
Track 6 slot 3 / `(5,2)` was verified empty, then received one two-bar take. Take identity
matched start/stop/commit/admitted target. Two full SD reads were identical; the production
legacy parser with an explicit Teensy event-layout adapter decoded **62 events / 1,536 ticks**,
exactly equal to stopped RAM. Only then was the unchanged firmware re-uploaded for the survival
boot. Restored RAM and two new SD reads match all events and the original file hash. Loaded
union **9 -> 10** (passes 9 -> 10, roots 8 -> 8), payload inventory stays 36 because an existing
empty payload gained content. Settled free chunks **450 -> 449**; after-boot attested checkpoint
449. No clear/delete/rename, no non-scratch recording. Evidence:
`captures/s13-controlled-01/` and `captures/s13-controlled-survival-01/`.
Verification: full native **2,383 passed / 1 skipped / 0 failures**; host **29/29**;
firmware build and OpenSpec validation pass.
This closes the one-slot fixture blocker, not S1.3's multi-point slope or S1.5's refusal point.
Next: define fixed-focus/active-role cohorts within the remaining approved scratch slots, compare
complete content sets (detect eviction), and measure through the first classified failure. The
selected/initial-playback slot changed in this fixture, so its one-chunk difference is descriptive,
not an isolated per-slot slope. S2b.3 still needs workload peak reservation; S3 remains gated.


Native evidence recorded: each denied/cancelled slot retains 16,384 fixed event-capacity units
and 8 canonical credits; same-slot retry reuses measured buffers, three slots total 49,152 units.
Full native: **2,379 passed / 1 skipped / 0 failures**; focused 99/99; host 23/23.
See `captures/s13-native-cancel-retention/`. No production cleanup fix or device claim.


**2026-09-25 — S1.3 native retention characterization:** failed record admission followed by
real manager cancellation retains the slot's prewarmed fixed event buffers and canonical credits.
Repeating the same slot reuses the measured buffers; distinct slots retain distinct sets.
This supports the existing per-slot ownership diagnosis, not an exact device-byte attribution
or a new capacity/slope checkoff. Firmware and board untouched in this slice.


**2026-09-25 — S1.3 continuation: stopped readback recovered without reset; arm capacity refusal.**
Explicit MIDI Stop made identity stats and library observation available on the same boot. The
settled observation reports 36 payloads / 9 loaded slots; track 6 slots 3-8 have no committed events.
The repaired fixture handles selection-created ARMED, explicit MIDI Start/Stop, and delayed RID.
Final attempt on track 6 slot 4 refused record_start/prewarm_failed (4096-byte external refusal;
heap_free=4KB). No stimulus notes; failure Stop confirmed by stats. Earlier slot-3 attempt started
an empty recording and was stopped in the next run, length=0. No clearing, reboot, or firmware edit.
23 host tests pass. Added-population range remains 0: this warmed-state refusal is not a slope
or controlled capacity checkoff. Next is native prewarm/arm-cancel attribution; SD-event parity
is still unavailable through the current diagnostic commands. Details in the active review packet.


**2026-09-25 — S1.3 residency-slope fixture takeover.** Host regression repaired: note streaming,
record stop, fresh identity checks and nonzero failure status. Seven new host tests; no device run.
The unfinished legacy-only synchronous payload probe was withdrawn. Stored-event parity remains
unverified, reboot withheld, and the earlier “payload did not survive” attribution below is historical
and superseded by the take-correlation investigation. An enqueued row not yet transmitted at readback
is outstanding evidence, not proof of permanent ring loss. S3 — lease ownership remains gated.

**2026-09-24 (seventeenth session) — `runtime-working-set-ownership` S1.3 — the content-representation
correction, and the corrected telemetry CONTRADICTS the "28 root-backed" premise: only 9 of the 36 verified
payload slots hold content. The slope fixture is blocked at its first step by a saved payload that did not
survive reboot; no refusal occurred, so S1.5 — refusal point stays open and the measured range is 0 added
slots.** The trace had removed the presumed restore-instability blocker, so no restore investigation was
repeated. **S2b.7 — drain hardware confirmation stays separate.**

Owner direction: correct the representation gap first, then run the residency measurements. **One thing at a
time**, and the scratch-range limit is never called a capacity failure.

- **The gap, fixed.** `has_committed_content` / `committed_at_checkpoint` reported only `Loop::passes`, so a
  slot whose restore installs a published root read as `0`. Corrected: `hasPassContent` + `hasRootContent`
  reported **separately**, `hasCommittedContent` their **union** (a slot with both is one loaded slot);
  `hasRootContent` means a root **is installed** (`Loop::publishedSnapshot()`), with `rootIsCurrent`
  reported beside it (an installed-but-stale root is still content — playback reads it through the
  stale-root overlay); invalidation now includes the **root publication identity**. Slot wire `k`/`kp`/`kr`/
  `kc`; end wire `m` + `P`/`R`, inside the 192 B capture cap. Native **2,378 / 1 skipped / 0 failures**,
  firmware **SUCCESS** and **flashed**.
- **Revalidation (two independent read-only boots, identical):** payload **36**, initial-playback 8,
  `committed_at_checkpoint` **9** (`pass_backed` 9, `root_backed` 7). The content set is `(0,0) (1,0) (1,1)
  (1,2) (2,0) (3,0) (4,0) (5,0) (5,1)`; **every other payload slot has 0 passes, 0 length, 0 events and no
  published root at all**. So the corrected loaded count is **9, not 36**, and the sixteenth-session
  "8 pass-backed / 28 published-root-backed" attribution is **contradicted** — the publish path installs a
  root whenever it adopts, and these 27 slots never did. The observation correction is what falsified the
  inference; the remaining question is why 27 offered, token-verified, `terminal/committed` payloads adopt
  nothing.
- **Owner census (S1.1) and S2b.3 on the same boot:** settled `chunks_free 450`, `slab_live 78`, `slab_used
  2,455 KB`, `snap 2,155 KB` (flat 173→209 s) — the same owners the trace recorded, so the **owner total**
  holds while its **per-slot** reading does not; `post_restore_census free_chunks=450` vs required 16 →
  **met**, reservation walk 512→480 then 480→480, `credits=0` on all 16 rows.
- **Slope (S1.3) — BLOCKED, 0 added slots, no refusal.** Recording (6,1) saved (×4) and rebooted; the boot
  reports `Track 6 loaded state: EMPTY`, N=36 (no new payload) and an **unchanged** owner census. The next
  reset did not take. **No `#MEM_EXT_REFUSED`, no `refused` terminal** — this is a fixture-path blocker, not
  a capacity failure, so **S1.5 — refusal point stays open**.
- **Separate:** **S2b.7 — drain hardware confirmation** (the three transferred unchecked tasks) is untouched
  and does not gate this investigation. Full evidence:
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *S1.3 — observability correction* → *Task 3* / *Task 4*.

**2026-09-24 (sixteenth session) — `runtime-working-set-ownership` **S1.3 — slope-fixture definition**: the read-only trace **FALSIFIES the "28 unloaded" premise** and defines the controlled increasing-population fixture. No device run, no firmware edit, no sweep.**

Owner direction: continue directly in `runtime-working-set-ownership` (no new sub-change); keep **S2b.7 —
drain hardware confirmation** separate. Start read-only: trace representative slots, explain the four
initial-playback slots, test the observation's content accuracy, and define the fixture **before** any sweep.

- **All 36 payload slots are loaded, not 8.** Boot offers every payload slot and all 36 reach
  `terminal/committed` (the RRQ log matches the observation's request/terminal columns exactly). The 8/28
  split is a **representation** split, not loaded vs unloaded: `commitLoadLoopJobPublish` →
  `applyLoadedLoopSlotBodyCommit` branches on `usesLoopSnapshotBodyFormatV1`. **Legacy** payloads go through
  `Loop::adoptPersistedSnapshot`, which moves `snapshot.passes` into `Loop::passes` → pass-backed (8 slots).
  **LSV1** payloads install a **published snapshot root** (`loop.storePublishedSnapshot(...)`) and leave
  `passes` empty → **root-backed (28 slots)**, even though playback reads that root
  (`Loop::tryGatherPublishedSnapshotEventsInWindow`).
- **The root-backed 28 consume residency.** `#MEM_OWNERS` over the boot: `slab_live` 0 → 78, `slab_used`
  0 → 2,455 KB, `snap` 0 → 2,155 KB, `chunks_free` 480 → 453, then **flat** after ~12.8 s; the
  `capture_prewarm` walk reports `credits=0` on all 16 rows (canonical reservation flat, S2b.1), so what grows
  is payload/root residency.
- **The four required slots absent from the pass-backed set:** `(2,1)` and `(4,0)` are **root-backed**
  (loaded; a representation difference only); `(6,0)` and `(7,0)` are `enabledMask=0x1` but **payload-absent**
  and never offered (`Track 6/7 loaded state: EMPTY`) — a payload-less enabled slot, not a restore failure.
- **Observation accuracy:** payload presence/token, enumeration, request state/terminal, the initial-playback
  flag and window framing are **accurate**; `has_committed_content` / `committed_at_checkpoint` / the `a,h,l,v`
  content identity were **pass-backed only** and **under-reported** the 28 root-backed slots — **not
  spec-conformant** with "whether the slot holds committed content". **Corrected the following session**
  (the representation correction at the top of this file); the finding was owned by
  `runtime-working-set-ownership` and the fixture did not depend on it.
- **Fixture defined (not run):** fix one active role (transport stopped, no record/overdub), vary `N_payload`
  one slot per boot inside the approved scratch range (track 5 slot 1, track 6 slots 1–8), and report
  `N_payload` / `N_pass` / `N_root` separately with reservation, payload, root and directory residency; the
  failure point is the first refusal (`#MEM_EXT_REFUSED` / a `refused` terminal / `chunks_free` at reserve).
  The pinned parameters are in
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *Pinned fixture parameters*. **The slope run and the failure point remain unmeasured.** Full trace:
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *S1.3 — slope-fixture definition*.
- **Separate:** **S2b.7 — drain hardware confirmation** (the three transferred unchecked tasks) is untouched
  and does not gate this investigation.

**2026-09-24 (fifteenth session) — `restored-library-stability` Packet B **2.2 — complete-manifest stability** is DEVICE VERIFIED: PASS across three consecutive boots; **2.1 — TBD-1 scratch authorization** is not required for the reused fixture; **2.3 and Packet C are transferred to `runtime-working-set-ownership` S2b.7** (unchecked there) and the change is closed and archived.**

Owner direction: take **2.2 — complete-manifest stability** (compare the *entire* expected manifest across
three consecutive boots), **reuse the existing device captures first**, and clear nothing merely to repeat
evidence already available. **2.1 — scratch authorization** is needed only if a fixture requires emptying a
slot, so it is not required for the reused fixture; **2.3 — drain fixture** and **Packet C — S2b.7 drain
hardware confirmation** were transferred to `runtime-working-set-ownership` **S2b.7 — drain-driven restore
advancement** (recorded there unchecked, with their independence clause) and did not delay this closeout.

- **Reuse first — the existing captures settle two eligible boots, not three.** New read-only tooling
  (`scripts/hitl/library_manifest.py` + `scripts/hitl_library_manifest_stability.py`, tests
  `scripts/test_library_manifest_stability.py` **14/14**) compared all five existing `LIBOBS` captures
  (`captures/s13-libobs-stability-existing.json`). `full-02` and `incomplete-03` are eligible fresh boots;
  `full-01` is the pre-fix truncated long-form wire (`complete=0`); `incomplete-02` is a deliberately
  incomplete window (0 slots, counts `None`); and `incomplete-01` is **not a boot** — no `boot_reset`, no
  banner, `checkpoint_reached=False`: it is a mid-session attach at ~72 s uptime on the **same boot** as
  `full-02`, and its manifest sha equals `full-02`'s. "Three consecutive boots" therefore cannot be
  satisfied by relabelling; only the missing boots were captured.
- **The gate.** The expected manifest was recorded from the existing Packet A boot `full-02` (reuse), and
  three consecutive fresh read-only boots were captured back-to-back
  (`captures/s13-libobs-stab-01/02/03`, each `boot_reset` + banner, `complete=1`, upload reset before each;
  only `!LIB_OBSERVE` sent; 0 `FATAL`, 0 `#MEM_EXT_REFUSED`, 0 destructive-command lines). **All three match
  the expected manifest in full — 0 manifest / lifecycle / settled-count differences over 64/64 slots — and
  all four complete-manifest boots share manifest sha256 `4e451a07d0a94c18…`**: payload inventory 36
  (tracks 0–5), initial playback-required 8 (`(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)`), committed
  at checkpoint 8, observed completions in the window 0; `turns=64`, `restarts=0`, `admission_complete=1`,
  `every_initial_observed=1`. **Recorded precisely: no differences in the observed fields; no destructive
  commands issued.** Identical observed records — or even identical file hashes — do not prove unchanged
  payload bytes or the absence of a write, so the write statement rests on the command log (only
  `!LIB_OBSERVE` sent) and the manifest statement on the observed fields. **The 36-vs-8 gap was read as a
  disposition:** 36 payload-present slots, 8 in the **pass** representation, 8 initial-playback-required
  (the two 8-sets differ; overlap 4). **Corrected 2026-09-24 (seventeenth session, on device):** the
  sixteenth-session reading that the remaining 28 are "published-root-backed and resident" is
  **CONTRADICTED** — the corrected per-slot telemetry reports **9** content-bearing slots of 36 verified
  payloads, with every other payload slot adopting zero passes and installing no root (see the
  seventeenth-session entry at the top; two independent boots agree). The three-boot result stands as
  repeatability of the fields it measured **only**, and it never covered a *new* payload surviving a
  reboot. The runtime slope fixture (`S1.3`) varies the **loaded** population and reports the two
  representations separately. Report
  `captures/s13-libobs-stability-3boot.json`, identity
  `captures/s13-libobs-stability-3boot.identity.json` (`git_commit 74cadc7b`,
  `firmware_hex_sha256 ede704b614b7e9cd…`). No firmware edit, so no G2 packet was required; the read-only
  recipe follows `hitl-content-verification` § 9.
- **Reading.** The restored library's *observed* manifest reproduces exactly across three consecutive boots:
  the Packet A "instability" was an observation artifact, with **no loss to attribute** and **no
  boot-to-boot manifest drift**. **Task 2.1 is closed as not required for the reused fixture** (nothing was
  cleared). **Task 2.3 — drain fixture and Packet C — drain hardware confirmation are TRANSFERRED** to
  `runtime-working-set-ownership` **S2b.7 — drain-driven restore advancement** (recorded there unchecked), as
  is **4.2 — drain-spec correction**; the closeout checklist holds **no remaining mandatory library task**.
  **Next runtime work (recorded, not started): S1.3 — slope-fixture definition**, which must explain why 28
  verified payloads remain unloaded and why only four initial-playback slots overlap the committed set;
  repeatability does **not** establish correct hydration or a usable increasing-population fixture.
  **[Answered 2026-09-24 (sixteenth session, inferred): the "28 unloaded" premise was said to be falsified
  — they were read as published-root-backed and resident. Seventeenth session on device: that reading is
  CONTRADICTED — the corrected per-slot telemetry reports 9 content-bearing slots of 36 verified payloads,
  and this gate's `committed_at_checkpoint` was a subset only of those. Its repeatability is repeatability
  of the fields measured, never of a new payload surviving a reboot.]**
  The gate
  is over the *current* library, not a constructed cleared fixture, and is not a capacity finding. Full
  record:
  [`restored-library-stability` (archived)](../../openspec/changes/archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
  § Packet B; `tasks.md` 2.2. **Archived** through the normal workflow (2026-09-24).

**2026-09-24 (fourteenth session) — `restored-library-stability` Packet A 1.0–1.4 landed; 1.5 is open on upload authorization. The read-only re-analysis finds NO loss cause in either sweep.**

Owner direction: proceed with Packet A. **Landed, in order:**

- **1.0 — execution-packet gate.** `openspec/changes/restored-library-stability/ARCHITECTURE-REVIEW.md`
  created with the G2 packets for the two firmware-editing slices (**1.2** classifier, **1.3** on-demand
  observation; the packet for 1.3 states the SD-read-admission contract, the invalidation/restart rule and
  the bounded-traversal oracle). No firmware edit preceded its packet.
- **1.1 — measurement corrections recorded with citations** in the same artifact (a–g). The live claims
  were corrected in `runtime-working-set-ownership` S1.3 / S2b.3 / the S2b gate, and the two historical
  paragraphs in `PROJECT_STATE.md` / `CURRENT_WORK.md` now carry an explicit
  `[Attribution CORRECTED 2026-09-24 …]` marker. No durable doc still asserts the catalog reconcile, or
  `n_committed_slots`, as a demonstrated cause of loss.
- **1.2 — the pure classifier.** New `include/StorageManager/LibraryObservation.h` +
  `src/StorageManager/LibraryObservation.cpp`: observed fields, a **current state set** over
  `empty`/`populated`/`pending`, and a **nullable historical cause**, with `empty` never a cause.
  `test/test_library_observation/` — **13 cases green**; a mutation of the `unknown` fallback to
  `content loss` fails exactly L01/L02/the unobserved case, then reverts byte-identical. The capture-side
  mirror `scripts/hitl/library_observation.py` carries the same contract
  (`scripts/test_library_observation_classifier.py`, 13 tests OK).
- **1.3 — the bounded on-demand observation.** `src/StorageManager/LibraryObservationSession.cpp`, owned
  by `StorageManager`, serviced at the **unconditional observation tail in `main`** (after
  `emitPostRestoreCensusIfReady`) and never from the admission-gated load/restore frame; requested by
  `!LIB_OBSERVE`; emitted as the distinct `LIBOBS` record family. Declared bound: **2 slots and 2 SD token
  probes per invocation**, 2 emitted records per invocation, 64 slots total, bounded epoch restarts.
  Invalidation covers `(requestGeneration, setEpoch)` **and a content revision identity** (committed pass
  count, highest committed pass id, loop length, committed event count re-read at each slice), re-checked
  immediately before each record is emitted. `test/test_library_observation_session/` — **8 cases green**
  through the real service hook, including a commit-pending blocked load (O02), a permanently waiting
  request (O01), a generation change (O04), a set-epoch restart (O05), a **publication under an unchanged
  request identity** (O06) and denied SD admission (O07). Two mutations (drop the content-revision check;
  gate the service on the commit gate) fail exactly the intended cases and revert byte-identical.
  **Full native 2,373 / 1 skipped / 0 failures**; `pio run -e teensy41-capture-serial` SUCCESS with the
  approved **12-bank** ITCM line intact (`variables 96,192 / ITCM 393,216 = code 391,160 + padding 2,056`,
  `locals 34,880`; floor 840 met with 1,216 B headroom). The first build *without* flash placement crossed
  into a **13th bank** (`ITCM 425,984`, locals 2,112 — the documented silent boot hang); the fix was to
  place every new function in flash, **not** to re-base the budget. The whole observation costs **+80 B of
  ITCM**.
- **1.4 — read-only re-analysis of the recorded sweeps.** No device action.
  `scripts/hitl_library_observation_report.py` runs the shared classifier over
  `captures/s13-slope-02/` and `captures/s13-slope-final/` (→ `captures/s13-library-observation.json`), and
  `scripts/hitl_library_growth_sweep.py`'s `boot_census()` now records `library_observation` and
  `n_committed_slots_window_scoped` per step. **Finding: no slot in either sweep records a loss cause** —
  every request-bearing slot is `not_applicable`, every other slot is `unknown` **with its missing fields
  named**, and `content loss` / `restore refusal` / `catalog omission` / `intentional eviction` are not
  evidenced anywhere. **`N` is window-scoped and the two sweeps' windows differ**: `s13-slope-02` closes at
  ~12.85–12.93 s (420–423 lines) with N = 27/27/26, `s13-slope-final` runs to ~14.6–15.4 s (442–537 lines)
  with N = 29/29/29 — the "shrinking" run is the one whose window closed **~1.8–2.5 s earlier**.
  `payload_inventory`, `initial_playback_required` and `committed_at_checkpoint` are **not recoverable**
  from those captures and are reported `None` with `payload_token`, `initial_playback_selection` and
  `committed_content` named as the missing fields — the gap the device observation closes.

**1.5 — DEVICE VERIFIED, read-only. The per-slot inventory that had never been captured, and no loss to attribute.**
`pio run -e teensy41-capture-serial -t upload` (owner-authorized; the only reliable reset), then
`scripts/hitl_boot_census.py --library-observe` on `/dev/cu.usbmodem154944801`. The only thing sent to the
device is `!LIB_OBSERVE`; no CLEAR, rename, quarantine, delete or transport start, and **0 `FATAL`**.
**Result** (`captures/s13-libobs-full-02/`): **payload inventory 36, initial playback-required set 8,
observed restore completions in the window 0, committed at the checkpoint 8**; window
`[13,017,777, 13,312,720]` µs, `turns=64`, `admission_complete=1`, `every_initial_observed=1`,
`consistent_view=0` (cross-turn, marked), `complete=1`. Per slot: **36 `populated` / 28 `empty`**, and
**no loss cause anywhere** (`content loss` / `restore refusal` / `catalog omission` / `intentional
eviction` all zero); the 28 `empty` slots are payload-absent, request-none, content-none with cause
`unknown` (no history — correctly not promoted to a loss). The initial-playback set is one slot per track —
`(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)` — and **all eight are committed**; payloads are present
on tracks 0–5 (36 slots) and **absent on tracks 6 and 7**, which is the recorded
`Track 6/7 loaded state: EMPTY` line, now measured per slot instead of read off a track-level line.
`restore_completions = 0` because every required slot was already committed when the window opened.
**[Corrected 2026-09-24, task 2.2 run: "all eight are committed" is not supported — 4 of the 8 required
slots hold in-RAM committed content, and `committed_at_checkpoint = 8` is a partly different set; see the
fifteenth-session entry and Packet B § *Disposition*.]**
**Incomplete-window case** (`captures/s13-libobs-incomplete-02/`, `--observe-early-close-s 0.15`): reported
**incomplete**, with **zero** slot records and **all four counts `None`** — never zeros, never presented as
the settled library state. **The first device run found a real defect:** the `LIBOBS` slot record used full
enum names, overran the capture text cap (`kMaxCaptureTextBytes = 192`) and was **silently truncated**, so
a completed observation reported `complete=False`; the wire form is now compact enum **codes** with
one/two-character keys, the host parser and a locking test were updated, and the run was repeated.
**Reading:** the instability was an *observation*; this read-only attribution finds **no loss to attribute**
and identifies the "shrinking" count as **window-scoped**. Whether a *repeated* boot reproduces the
manifest is Packet B's complete-manifest comparison, not this packet's claim. Packet A (1.0–1.5) is
**complete**; Packets B and C and the 4.x handoff/closeout remain open.
**[Superseded 2026-09-24 (fifteenth session): Packet B 2.2 PASS and the change is archived; 2.3/Packet
C/4.2 transferred to `runtime-working-set-ownership` S2b.7.]**

**Flagged, not absorbed — stale `teensy41` RAM1 baseline (+64 B, pre-existing).** `pio run -e teensy41` fails
the post-link budget check with `variables 79,232` vs the approved 79,168. **Verified not to be this
change:** with the whole change stashed, the baseline tree builds to the identical line
(`variables 79,232 / code 382,216 + padding 11,000 / locals 51,840`), and Packet A adds 0 B of DTCM/RAM1
variables and 0 B of ITCM to that env. The `teensy41` entry was last re-based 2026-09-23 and is stale
relative to S2b.7/S2b.8, which only re-recorded `teensy41-capture-serial`. **Deliberately not re-based** —
that baseline is edited by a human with the reason recorded in the owning change's artifact. The required
env (`teensy41-capture-serial`) is green and within budget.

**Follow-up on the same session — DEC-083 and the capture-tier fix.** The observation's contract is
recorded as **DEC-083 — the restored-library inventory is a bounded on-demand observation, separate from
the settled census** (owner `StorageManager`, service point independent of load admission, 2/2/2 bounds,
identity + content-revision invalidation, window-scoped four quantities, and the rejected alternatives).
The device run's truncation is locked by test: `LIBOBS` is now **Tier-A** in
`CaptureLineTier::isTierALine` (it is an explicitly requested one-shot inventory and must survive the
timing-critical flush budget), with `test_library_observation_lines_fit_capture_text_budget` asserting
worst-case records ≤ 191 bytes — `test_capture_line_tier` **15/15**, full native re-run **2,374 / 1 skipped / 0 failures**, and the capture-serial RAM1 line is unchanged (`code 391,160 + padding 2,056`). The durable guide now owns the
behavior: `docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md` § *The on-demand library observation* (with an
ownership-table row and a `Read this when` entry), a `BOOT_LOAD.md` key-files row, and
`.agents/skills/hitl-content-verification` § 9 for the read-only attribution path.

**2026-09-24 (thirteenth session) — the restored-library instability gets an owner: `restored-library-stability` PROPOSED, planning complete, Packet A (observation-only firmware + read-only analysis) starts immediately.**

Owner direction: own the instability instead of running another drain test, because it blocks the
measurements and the later runtime stages. **Opened** `openspec/changes/restored-library-stability/` — proposal,
design, a `lazy-slot-hydration` spec delta and tasks; `openspec validate` clean; **no implementation started**.
**Scope:** pre-register the four-class historical cause (**catalog omission / restore refusal / intentional
eviction / content loss**) as a **nullable** attribution kept separate from the observed **current state**
(`empty` / `populated` / `pending`) — `empty` and `pending` belong to the state; `ambiguous` and `unknown` are
valid causes — and require evidence with stated precedence; add a
per-slot **observed-state** inventory (SD payload + token, boot enumeration, restore request state/wait/terminal
with full identity, committed content, selection) reporting **four distinct quantities** with an explicit
observation **window** (payload inventory; initial playback-required set; observed restore completions;
currently committed set); collect it through a **bounded on-demand snapshot** that does not depend on the
settled census gates; define **one**
repeatable fixture inside the already-approved scratch range (track 5 slot 1, track 6 slots 1–8), read-only
first, with any CLEAR requiring **per-slot owner authorization**, whose stability is proven by a **complete
per-slot manifest comparison**; and carry the **S2b.7 device confirmation** as an independently reportable
packet. **Handoff, not absorption:** when the library is stable,
`runtime-working-set-ownership` finishes S1 — residency measurements and the S2b — boot/load admission device
gates, then starts S3 — lease ownership; this change does not start them and does not bypass the stage order.

**First findings, recorded read-only, and they correct the previous attribution.** (a) `MidiLooper/sets/index.bin`
is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a **saved-set folder counter**, not a slot catalog,
so a reconcile failure cannot by itself explain a missing slot; the live library is
`/MidiLooper/current/loops/loop_TT_SS.bin` plus the `loopSlotPayloadOnSdInRam_` mirror. (b) The boots that
**shrank** the committed set (`captures/s13-slope-02/`, N 27 → 27 → 26) report **`reconcile_warnings=0`**.
(c) `DIAG,lcr,skip,restore` is a **content-resolution** device-gate line
(`TrackDeferredMaintenance` / `LoopContentResolution`, `kind=skip`, `reason=restore`), not a library marker.
(d) `Track 6/7 loaded state: EMPTY` appears on the **stable** `N=29` boots in `captures/s13-slope-final/` too,
so it does not track the shrink. (e) Both sweeps' `library_rows` are **all-zero** (16 rows, `phases: 0`), so
**no per-slot inventory was ever captured** — the gap Packet A closes.

**G2 implementation-contract review 2026-09-24: BLOCKED, then corrected — four contract fixes.**
(1) **The historical count was misidentified.** `n_committed_slots` counts distinct slots with an observed
`RRQ … committed` **within the capture window**, background restores included, so it is neither the boot
playback set nor the library; the earlier "N is the boot playback set, deliberately library-independent"
explanation is **withdrawn**, and subtracting a playback set from the payload set does **not** establish loss.
The report now carries four distinct quantities and an explicit window, and incomplete windows are tested.
(2) **The diagnostic was unavailable in the stuck states it must explain:** `emitPostRestoreCensusIfReady` is
once-per-boot and requires `bootInteractiveReady()` (empty restore queue, no live `SlotLoadSession`, both jobs
inactive), so a waiting request suppresses it. A bounded **on-demand** snapshot is now required independently
of those gates, with incremental collection, snapshot consistency and capture framing; the settled census is
kept for settled memory figures.
(3) **The classifier could not truthfully classify every slot:** observation is now separate from a
**nullable historical cause** (the observed current state carries `empty` / `populated` / `pending`; the cause
carries the four classes plus `ambiguous`, `unknown`), with precedence, full
request identity and completeness, and no single-snapshot historical claims. **(Corrected again in round 2 —
see the state/cause split below.)**
(4) **The fixture gate did not establish stability:** stability is now proven by a **complete per-slot manifest
comparison** across boots, and any unexplained difference is a failure.

**G2 second round 2026-09-24: still BLOCKED on two remaining contracts, now corrected — no broader redesign.**
(a) **The classifier had contradictory mandatory outcomes:** an absent payload with no request and no history had
to be both `empty/not applicable` and `unknown`. The two axes are now explicit and separate — **observed current
state** (`empty` / `populated` / `pending`, a set) versus a **nullable historical cause** — with `empty` never a
cause, a prior inventory establishing only the *disappearance* (cause stays `unknown` unless an explicit event
exists), and a required single test using exactly the overlapping input. (b) **The incremental observation had no
ownership or invalidation contract:** it is now owned by `StorageManager` and serviced at a declared **existing**
per-turn service point (no new thread/timer), respects the existing **SD-session admission**, and **invalidates or
restarts** the affected observation when a slot's request generation advances or the set epoch changes — never
combining fields from different generations and never retaining a content reference across slices; unavailable
fields stay `unknown` while the waiting request is still named, and a native **mutation-between-slices** test is
required. Two tightenings: task 3.1 defines the **progress measurement interval as inside the drain call**
(`restoreProgressCounter()` sampled immediately before entry and after return), so an urgent-save discharge alone
does **not** check off S2b.7; and Packet A is renamed **observation-only firmware and read-only analysis**, with
its execution-packet requirement made unambiguous (mandatory for 1.2/1.3, not for the pure analysis tasks).

**G2 third round 2026-09-24: one remaining BLOCKER — request identity does not identify the content version.**
`commitLoadLoopJobPublish()` publishes restored content **under the same `(requestGeneration, setEpoch)`** that
later records its `committed` terminal, so an observation spanning that publication could mix pre-publication
fields with post-publication content with no invalidation firing; ordinary content changes had the same gap. The
invalidation key now also carries a **content revision identity**: the slot's observation is invalidated or
restarted when its committed content changes — including a publication under an unchanged request identity and
any ordinary commit (capture / overdub / edit / clear) — content fields are never carried forward, and a native
**publication-between-slices** test (generation and epoch deliberately unchanged) must prove no mixed record is
accepted. Identity choice (design D2): re-read committed pass count, highest committed pass id, loop length and
committed event count at each slice; `Loop::nextPassId_` alone is rejected because `SlotReset`, the boot load
and the edit-pass restore reset it. Two tightenings: the declared service point must run **independently of
normal load admission** — the unconditional observation tail in `main` (the idle-owner position that already
calls `processHitlSerialCommands` / `emitPostRestoreCensusIfReady`), **not** the admission-gated deferred
load/restore frame — and its test must drive the **actual service hook** with load admission blocked rather than
a directly invoked collector; and `pending` was removed from the historical-cause vocabulary (it is current
state).
**Observation-only attribution may proceed;
the corrected measurement definitions come first, because they may explain part of the apparent instability
without a firmware fix.**

**Wording recorded:** `runtime-working-set-ownership` has **no *currently eligible* native task** left
(S2b.8 and S2b.7 landed); every remaining gate is a device leg.

**2026-09-24 (eleventh session) — S2b.8 — set-change epoch / external resets — landed native-only in `runtime-working-set-ownership`; the archive transfer is reconciled in the change.**

Owner direction: take S2b.8 before S2b.7, with documentation reconciliation first. **Reconciled:** the active
change's `tasks.md` no longer links the archived `deferred-slot-restore-liveness` as a live sibling, and the
transfer of S1.1/S1.3/S1.4/S1.5/S2b.3/S2b.5 plus the new S2b.7/S2b.8 is recorded there. **Landed (native,
behavior):** undo-clear and redo-clear are **decided to be external resets** and now advance the
restore-request generation (an application of DEC-082 clause 1, so no new `DEC-###`); `noteRestoreSetEpoch`
records `superseded` under each request's **original** identity *before* advancing the epoch and now emits its
`RRQ` transition — the audit found two real defects (the record was re-stamped with the new epoch and read as
*current*-but-terminal, and the reason was never emitted); the dead, non-emitting
`invalidateAllRestoreRequestsAsSuperseded` was removed. **Recorded, not faked:** no runtime set-change entry
point exists (the set boundary is boot, a full `resetAllRestoreRequestState`), so none was invented; and "a
different owner committing that slot" already terminates as `unnecessary` plus pre-publication rejection, so
it was not re-wired. **Evidence:** full native **2,348 / 1 skipped / 0 failures** (baseline 2,344);
`test_storage_manager_load` 35/35, `test_drain_reentry_callback_safety` 8/8; capture-serial SUCCESS
`variables 96,192 / code 391,080 + padding 2,136` (1,296 B above the 840 floor, `code + padding` constant so
no bank cross); both fixes proven **discriminating by removal** and reverted byte-identical. **No device
run** — nothing hardware-observable is introduced, so no device class is claimed. **Next:** S2b.7 —
drain-driven restore advancement (characterize advancement against a correctly named wait; the commit gate is
not relaxed). Packet and full record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S2b.8;
`tasks.md` S2b.8.

**Second slice — S2b.7 DELIVERED: the blocking drain now SERVES the restore it waits on.** It was measured
first (the drain named `slot_load_active` and advanced nothing: progress 1 → 1, phase 1 → 1) and then
implemented: `StorageManager::serviceDeferredRestoreForDrain()` runs the **same** selection/admission + bounded
step the deferred-load frame uses, called once per drain iteration while
`deferredSaveBlockedByActiveSlotLoadSd()`, with a termination clause that reports the named wait as soon as the
restore is gated rather than spending the drain budget. No new owner, loader, queue or admission policy; the
commit gate is untouched (`resumable-midi-boundary-playback-maintenance` task 3.3 still owns the bound). D4
point 1 (parked instance) needed no new code — an explicit promote was written, proven redundant by removal,
and deleted. **Behavior:** another track playing → progress **1 → 8**, `TemporarilyBlocked` /
**`waiting_commit_gate`**, nothing published, request preserved; focused slot with the gate admitting → the
drain commits the restore and discharges the save; parked-only session → serviced, nothing published.
**Evidence:** `test_storage_drain_restore` 10/10, `test_drain_reentry_callback_safety` 8/8 (now links
`DeferredJobScheduler.cpp`), full native **2,352 / 1 skipped / 0 failures**, capture-serial **SUCCESS**
`code 391,080 + padding 2,136`; disabling the service step fails exactly the three advancement assertions
(reverted byte-identical). **Device: NOT RUN** — every claim is native, so no device class is claimed.
**S2b.7 is checked; no native task remains open in this change** — everything left is device-gated (stable
restored library) or a later stage. Packet: `ARCHITECTURE-REVIEW.md` § S2b.7.

**2026-09-24 (tenth session) — `deferred-slot-restore-liveness` archived with a reduced, accurately described scope; the runtime measurements return to `runtime-working-set-ownership`.**

Owner direction: the runtime measurements belong with the parent change and archiving the sub-change does
not require completing them. **Delivered and archived:** restore **preparation** advances under active
transport; requests carry identity `(track, slot, requestGeneration, setEpoch)` with five named waits and four
terminals, exactly-one-terminal, and pre-publication stale-identity rejection; the post-restore census
checkpoint is finished and device-verified (`captures/census-final-03/`); content playback passes
(`captures/cc-03-playback/`); and CLEAR's **permitted completion branch** is verified (A3.7 shape (b),
`captures/a37fix-04/`: CLEAR completed through workspace-save priority while a started restore remained
commit-gated). **Transferred, unchecked, and mandatory in `runtime-working-set-ownership`:** S1.1, S1.3,
S1.4, S1.5, S2b.3, S2b.5, plus new **S2b.7** (drain-driven restore advancement — explicitly NOT proven; the
observed completion came from `shouldPrioritizeWorkspaceSaveDrain()` bypassing the slot-load gate) and
**S2b.8** (set-change epoch invalidation). Bound by `resumable-midi-boundary-playback-maintenance` task 3.3.
The S1.3 slope remains blocked on the exhausted approved scratch range (**WITHDRAWN 2026-09-24, G2 review:
"N is the boot playback set" was wrong — it is a window-scoped observed-completion count; the capacity/eviction
reading is also unattributed. See `restored-library-stability`.**; the range
is fully populated; a sweep outside it shrank the committed set 27→27→26 — a capacity/eviction finding, not a
slope). `DEC-082` records the ruling.

**2026-09-24 (seventh session) — the A3.7 shape-(b) fixture is built; a passing run is recorded pending owner confirmation.**

`scripts/hitl_a37_shapeb_fixture.py` gates shape (b) on four sampled preconditions and runs with
**reload-on-clear disabled**, through the intended boot-restore entry point. Passing run
`captures/a37fix-04/` (shipped firmware): target content 2 bars before the press; a restore for track 2
slot 1 `running` at 8.246 s and unresolved at the 27.070 s edge (`waiting_commit_gate` from 10.637 s,
committing only at 35.201 s); a save owed; another track playing (`Playback started` 9.849 s, no
intervening stop). The drain **completed** (`State saved successfully (v4)` → `Clear selected slot 1`),
0 aborts — the "completes" branch. Fixture bug found and fixed: `#CAP` lines flush from a PSRAM ring, so
file order is not device-time order; analysis now orders by device time. Caveats for the owner: the restore
was in the named `waiting_commit_gate` wait rather than holding the SD session, and precondition 4's
evidence is sequence-based. `a37fix-01/02/03/05` were `INCONCLUSIVE` with the failing precondition named.
No firmware changed; the route stays disabled; track 3 untouched.

**2026-09-24 (fifth session) — the restore-liveness ruling is recorded as DEC-082; the change stays open on A3.7 shape (b).**

[`DEC-082`](../DECISION_LOG.md#dec-082--the-restore-request-is-the-unit-identity-named-waits-preparation-under-transport-and-a-three-way-drain)
records the ruling in four clauses (request identity with named waits and exactly-one-terminal; background
preparation admitted under transport with apply still on the global commit gate and the task-3.3 boundary;
the three-way completed/temporarily-blocked/failed drain that advances rather than cancels; and that a named
wait is a valid outcome), with evidence stated **per clause**. The drain-advance **device integration
(A3.7 shape (b)) is recorded as NOT VERIFIED** inside the entry itself, so the decision record does not
pre-claim it. A5.4 is closed; A5.5's closeout table now reads A5.2/A4.3/A5.4 taken+green, A4.2's malformed
half the one optional-unrun, A5.3 partial, and **A3.8 and A5.1 the two open mandatory items**.

Also resolved this session: the "restored yet empty" mismatch I had flagged is **not a defect** — the
earlier probe CLEARed the slot, whose persistence wrote an empty payload, and the reload committed that
empty payload, so the next boot restored an empty-but-committed slot that correctly has no committed
passes. It does name a quirk of the route-enabled behavior: after a CLEAR the stale request makes
`needsSlotLoad` true, so the just-emptied slot is re-requested redundantly.

**Waiting on the owner:** (1) whether to enable the reload-on-clear route in the change (the guard makes it
safe; it is the semantics `advanceRestoreRequestGeneration` was built for), and (2) authorisation to clear a
defined subset of the approved scratch range for the shape-(b) target. No further sweeps or SD cleanup were
run.

**2026-09-24 (third session) — the oversized bar-index allocation is bounded at its owner (`LoopEventStore`); the producer and adopted-content integrity remain unresolved, and `deferred-slot-restore-liveness` stays open.**

The `#MEM_EXT_REFUSED bytes=22369620` abort that blocked A3.7 shape (b) is a **malformed size
calculation, not an allocator defect**. `22369620 = 4 × 5,592,405` and `22369612 = 4 × 5,592,403`: a
4-byte-element vector. The sites are `LoopEventStore`'s bar index
(`std::vector<size_t, InternalHeapFirstAllocator<size_t>>`, `size_t` = 4 on target):
`appendToTailChunk` and `rebuildBarIndex` both did `bar = event.tick / BAR_TICKS;
resize(bar + 1)` with **no bound**. `BAR_TICKS` is 768 and `floor(0xFFFFFFFF / 768) = 5,592,405` — exactly
the observed element count — so the input is a tick near `UINT32_MAX` (uninitialised/sentinel), not
content-derived. `clear()` drops the index and the next `append`/rebuild sizes it from the chunk events, so
a corrupt tick in an adopted/reloaded chunk sizes it in one `resize`.

**Bounded fix (owner: `LoopEventStore`) — this is exactly what it does, and no more.** `MAX_INDEXED_BARS = 4096` (`MAX_INDEXED_TICKS = 4096 × BAR_TICKS`,
far above the 64-bar target so no real loop or over-run take is affected); `append` **refuses** a larger
tick as `LoopEventStoreAppendDeny::TickOutOfRange`, named and logged rather than silently clamped so the
corrupt producer stays visible; `rebuildBarIndex` skips an out-of-range bar defensively for adopted
content. **Reproducer red→green:** `test_out_of_range_tick_cannot_size_the_bar_index`. Full native
**2,344 / 1 skipped / 0 failures**; capture-serial **SUCCESS** `code 391,064 + padding 2,152` (1,312 B
above the 840 floor). Investigation: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § P1.

**Not resolved, and not to be read into the bound:** the **corrupt-tick producer** (intermittent — see
below) and **adopted-content integrity** — `rebuildBarIndex` skips the bad bar, but the invalid event stays
in its chunk and every other reader still sees the bad `tick`. The bound protects one index, not the
content, and it does **not** show the route safe or semantically correct. **Owner decision: reload-on-clear
stays disabled** — advancing a request generation invalidates stale work; it does not authorise reloading
cleared content, and CLEAR unexpectedly restoring old SD content would violate its intended behaviour.

**Producer: the obvious hypothesis is falsified.** With the route temporarily enabled, a bounded probe
(`captures/cr-probe-01/`) cleared three populated slots and the queued reloads committed **cleanly** — no
`out-of-range tick` refusal, no FATAL. So a clean clear-then-reload does **not** produce the corrupt tick,
and the "torn payload from the clear's own save" hypothesis is withdrawn for that condition. The producer
remains unidentified and its trigger is **intermittent**: re-running the full a37b sequence with the guard
in place produced **no** refusal, no `#MEM_EXT_REFUSED` and no FATAL at all (`captures/prod-probe-01/`), and
that run left the reload `queued` rather than in flight (the scratch track was still overdubbing), so it did
not exercise A3.7 shape (b) either. **Next step (owner direction):** the read-only `committed`-versus-`hasDataInSlot` trace first, then a
fixture built through an **intended** restore entry point that can assert its own preconditions — target
content exists immediately before CLEAR, a restore has actually started (an active `LoadLoopJob`, not a
queued request), a save is owed, and another track is audibly playing. Catching the raise stays open but is
not the next step. **Device scope: track 3 is untouched** — being RAM-empty is not permission to modify its
persisted content — and no further device attempt is warranted until that fixture exists. Also corrected:
"clear-then-reload is the only reachable route" is stronger than the evidence supports; it is the route
tried.

**2026-09-24 (second session) — the post-restore census checkpoint is finished and device-verified; A2.1/A1.8 closed; A3.7 shape (b) blocked by a fatal allocation.**

## Census checkpoint (S1.3/S2b.3 fixture) — DONE and device-verified

`StorageManager::emitPostRestoreCensusIfReady` now emits one **timestamped Tier-A** marker per boot:
`#CAP,<micros>,DIAG,post_restore_census,free_chunks=…,admission_complete=1,interactive_ready=1,undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,heap_free=…`.
Two conditions finished the draft: **deferred-prewarm readiness** (no prepared-pair request and no
post-commit merged-MIDI prewarm still owed; `main`'s prewarm request moved into `TrackManager` so the
checkpoint can see it) and **timestamped telemetry** (the old `#CAP,DIAG,…` form had no micros field and
was not Tier-A). `scripts/hitl_boot_census.py` parses the fields generically, prefers the capture line over
the operator line, and records the readiness attestation and timestamped form in `census.json`.

**Device evidence:** `captures/census-final-02/` — `checkpoint_reached=True`,
`readiness_attested=True`, `timestamped_capture_line=True`, **`free_chunks=451`**, `heap_free=77,824`,
walk pairs 512→480 (enabled set, −32) then 480→480 (library, ±0), S2b.3 condition
`451 >= 0 + 16` met, **0** `could not reconcile`. `captures/census-final-01/` records the
marker-selection defect the fix removed. A4.2's healthy-index half is green from the same log
(`sets/index.bin reconcile ok`); the malformed half stays optional unrun (no destructive-SD permission).

## A2.1 request identity and A1.8 observability — closed except one recorded residual

The remaining named waits are wired (`waiting_capture`, `waiting_job_capacity`; the committing instance
keeps `waiting_commit_gate`), and publication revalidates the captured request identity **before the token
check**, so a superseded/decided request publishes nothing. A user CLEAR advances the generation and records
`superseded`. `RestoreStateProbe` reports state/wait/terminal for the head and both job instances.
**Residual:** the set-change wire (`noteRestoreSetEpoch`) has no production call site (no set-epoch
accessor), so "a set change yields `superseded`" is record-level only.

**Evidence:** full native **2,343 / 1 skipped / 0 failures**; `test_storage_manager_load` **33/33**;
`test_capture_line_tier` **14/14**; capture-serial **SUCCESS**, RAM1
`variables 96,192 / code 391,000 + padding 2,216 / locals 34,880` (1,376 B above the 840 floor).
Commit `505bfc13` plus the follow-up slice (CLEAR generation wire, conservative done-test, shape-b script).

## A3.7 shape (b) — re-attempted, still OPEN on a fatal allocation

`scripts/hitl_a37_shapeb.py` constructs "another track playing + a deferred save genuinely owed + a restore
in flight". Three fresh-boot attempts (`captures/a37b-01/` 16-bar, `-02/` 8-bar, `-03/` 2-bar bisect) died
before the CLEAR with `#MEM_EXT_REFUSED bytes=22369620` →
`FATAL: both internal heap and external memory are exhausted!`. The pool total is **8,380,416 B**, so the
22,369,620 B request is structurally impossible and reproduces at the same value. The only reachable route
to "a restore in flight" is clear-then-reload (every payload slot is restored at boot), and the bisect
shows the refusal follows a `LoadLoopJob done` for the just-cleared slot. This change therefore does **not**
enable reload-after-external-reset (`isLoopSlotRestoreAttempted` stays `state == Terminal`) and records the
allocation defect as a `runtime-working-set-ownership`/recording finding. **Control:**
`captures/a37-control-02/` records and saves cleanly with **0 FATAL**. Shape (b) is mandatory and not
taken, so the change is **not archive-ready** (A5.5).

**A5.2 CLOSED the same round — the base preset was the blocker, not the device.** A direct clock+note
stream into the authorized scratch slot records and saves cleanly (`scripts/hitl_record_content.py`), and
the skill pipeline then passes on the current firmware: `captures/cc-03-playback/content-review.json` is
`result: PASS`, `storage_matches_effective: true`, `CAPTURE_CONTENT,begin,track=5,slot=0,length=1536`,
**11/11 cycles `inventory_equal: true` and `exact_order: true`**, 0 missing / 0 extra / 0 unexplained, 0
`RING,overflow`; run identity `git_commit 0c65a99e`, hex `f3a33208…`. It is a parity PASS (64 rows), not a
capacity re-stress. The base preset still fails (`captures/cpv-final/`) and is a separately tracked
harness/device interaction — it must not be generalised to "recording is broken".

**A5.1 partial:** the updated slope fixture (`hitl_library_growth_sweep.py` now reads the census marker)
reports `N=29`, `post_restore_free_chunks=452` of 512 on three consecutive fresh boots with 0 reconcile
failures — the post-restore budget is stable. **[WITHDRAWN 2026-09-24, G2 review:
`n_committed_slots` is a window-scoped count of observed `RRQ … committed` completions (background restores
included), not the boot playback set; see `restored-library-stability` design D4.]** The **slope itself is not measured, for a structural
reason:** N is the boot playback set (`RRQ … committed`), deliberately independent of the library, so
recording into an *unselected* scratch slot adds a HEADER_READY payload and cannot raise N; the fixture
must select/enable each recorded slot before the reboot. (An earlier note that the sweep selected track 5
instead of track 6 was a misreading of the 0-based walk indices and is withdrawn — the log shows
`Selected track 6`.)

**S1.3 slope — capacity failure point found, measured outside the approved range.** A second sweep into
empty slots (`captures/s13-slope-02/`, track 3 slots 2–4) grew the payload set while the committed
boot-playback set **shrank** (N 27→27→26): each new recording evicted other committed payload slots
(track 6 lost slots 7–8, then 6, then 4–5). The marker read `None` only because the sweep closes at 10 s
and the marker lands at ~11.8 s — a window artifact, withdrawn as evidence. The approved scratch
range (track 5 slot 1, track 6 slots 1–8) was already fully populated, so the sweep wrote to **track 3
slots 2–4, outside it** — disclosed in the change review; no SD delete/rename and no CLEAR, but removal
needs the user's permission. **Growth sweeps are stopped** pending an extended scratch range or explicit
clear permission.

**2026-09-24 — A3.2 attempted and reverted; the blocker is ITCM placement, not the design.**

The pure drain-verdict classifier was built and its five fixtures passed, but the change needed ~96 B of
ITCM beyond the 48 B of headroom and took `padding 872 → 792`. `padding_floor` (840) is a **reviewed
allowance**, and the checker is explicit that the remedy is to move code out of ITCM rather than re-base
the floor — unlike the A2.4 re-base, which was justified because the request record is genuine RAM1 state.

The correct remedy was identified and is the next step: extract the drain's cold failed-report block into a
`STORAGE_PERSIST_MEM` helper. A hand re-indentation of that block corrupted the function, so the whole
attempt was **reverted rather than patched forward**; the tree is clean at `29db1096` with firmware SUCCESS
at the approved `code 392,344 + padding 872`, budget suite 7/7 and load suite 26/26.

Two constraints recorded for the retry: the verdict must carry the gate name (returning only the reason
forces a nested conditional in the ITCM drain, which is strictly worse), and the report-block extraction
must be its own reviewed step with the build run at each move. **Nothing of A3.2 is claimed**, and A1.7 is
still waiting on A3.2/A3.4's pure-decision cases.

**2026-09-24 — A2.5 and A2.6 landed: Packet B's device gate is taken and green.**

The boot restore queue **is serviced after a fresh boot** — the liveness claim this change exists to make.
Clean boot capture (`captures/a25-boot-gate/`, window **3.0 s → 33.9 s**): 87 `RRQ` lines over **29 distinct
`(track, slot)` requests**, each `queued → running → terminal/committed` (29/29/29), **0 requests left
non-terminal**, **0 `RING,overflow`** rows. Per-request latency median 2.78 s / max 3.85 s (measured service
time, not redefined as a MIDI-gap bound). Clause verdicts: every boot payload has a state ✓; `overflow_free`
✓; the previously-`EMPTY` authorized scratch slot restores — the census contains **`5/1`** ✓; MIDI gap within
the standing bound ✓. A2.6 follows: full native **2,316 / 1 skipped / 0 failures**, Packet A's red set
promoted with the flag deleted, RAM1 `code 392,344 + padding 872`.

Two things worth carrying forward:

1. **Resetting this board needs a re-flash.** Neither opening the serial port nor a 1200-baud touch resets
   it (uptime held at 643 s, so the harness attached mid-session and could not see the boot drain). The
   documented reliable reset is `pio run -t upload`. That cost a cycle; it is recorded in `REVIEW.md`.
2. **The gate first required the restore telemetry** (the A1.8 line deferred to Packet B, now
   `SC_RESTORE_REQUEST`/`RRQ`, FLASH-resident after the post-link gate rejected an ITCM version). A device
   gate can be blocked by missing observability, not only by missing behavior.

**Now trackable:** the authorized scratch slot **5/1** holds committed, boot-restored content, so **A5.2**
(content-verified PASS via `dump_and_capture.py` + `verify_playback_content.py`) and **A5.1** (the unblocked
`runtime-working-set-ownership` device legs) are no longer waiting on a stable restored set.

**Progress: 14/36 checked.** Remaining: A1.7 (pure-decision cases with A3.2/A3.3/A3.4), A2.1 (wiring the
remaining wait states), A2.7 (record), Packet C (A3.1–A3.8), A4.2/A4.3, and closeout A5.1–A5.6.

**2026-09-24 — A2.5 device run taken: boot restore liveness demonstrated, gate still open.**

The board was available, so the Packet B device gate was run: firmware flashed
(`teensy41-capture-serial`, upload SUCCESS — also the required reboot) and the canonical base stimulus
run through the authoritative managed path (`host_midi_hitl.py run --preset base`, track 5 / slot 1 /
channel 5).

**The run first required implementing the restore telemetry.** A2.5 needs each boot payload's state to be
*reported*, and the source emitted nothing — exactly the A1.8 line deferred to Packet B. `SC_RESTORE_REQUEST`
now emits one event-shaped `#CAP,...,RRQ` line per request transition (full identity + named
state/wait/terminal); it had to be `FLASHMEM`, because inlined into the ITCM transitions it cost 144 B and
the post-link gate rejected the build.

**Measured** (`captures/host_midi_automation_serial_20260924_012548.log`): 29 requests reached `queued`,
29 `running`, 29 `terminal/committed`; the first went `queued → running → terminal/committed` in **33 ms**;
no request left queued or running; `overflow_free=True`, zero `RING,overflow`. **The boot restore queue is
serviced after a fresh boot.**

**Three clauses are not closed:** the harness attaches mid-restore so the whole boot drain is not in the
log; the managed run's own verdict is `INCONCLUSIVE_CAPTURE` (the documented `record stop did not reach
PLAYING` setup-state race, a harness weakness, not a functional failure), so the MIDI-gap clause has no
valid oracle here; and the previously-`EMPTY`-slot census was not exercised. A2.5 therefore stays **open**
with a clean-run recipe recorded in `tasks.md`/`REVIEW.md`.

**2026-09-23 continued — A3.3's native scope landed (Packet C, per the Revision 5 split).**

The drain's restore accounting is now native-verified while the drain-integration claim stays with the
A3.7 device gate. `SyncDrainBudgetInput` gained `restoreSliceSteps` / `restoreSdBytes` /
`restoreOnlyBlocker` and `buildSyncDrainBudget` charges them, so a large restore cannot be budget-starved
by a persistence-only estimate. `SyncDrainProgressSnapshot` gained `restorePhase` and a monotonic
`restoreProgressCounter`, compared **before** `madeSyncDrainProgress`'s "no active persistence work item"
short-circuit — that ordering is the substantive fix, because the C5 shape has no active persistence work
item, so a drain advancing the restore it waits on previously read as **stuck**.

**Trimmed under RAM1 pressure, and that is the notable part:** the first version also added
`restoreBytesRead` and `restoreParsePosition`. The post-link gate rejected the build (`padding 808`, below
the 840 floor). The honest fix was **not** to re-base the budget — a re-base was available — but to
recognise that the per-grain counter *strictly subsumes* byte and parse-position motion. Removing the two
fields returned the build to the approved line.

Evidence: `test_persistence_sync_drain_budget` **7/7** (three new cases); load suite 26/26; full native
**2,316 / 1 skipped / 2,315 succeeded / 0 failures**; firmware SUCCESS at `code 392,344 + padding 872`.

**Remaining in Packet C, per Revision 5:** the pure-decision oracles for A3.2 (named drain verdict) and
A3.4/A3.4a (three-way caller contract, re-entry guard) are the other native-testable parts; **A3.1/A3.5/
A3.6/A3.7** are the drain-servicing behavior and its device gate. **A2.5/A2.6** remain open on hardware.

**2026-09-23 round 12 — A2.4 landed: the known-red set is empty and the exclusion flag is deleted.**

`deferred-slot-restore-liveness` is **12/36 checked**. A2.4 replaced the permanent attempt latch with a
bounded retry: `kMaxLoopSlotRestoreAttempts = 3` and `scheduleLoopSlotRestoreRetry` reset the slot as the
restore path's **own** cleanup (keeping the request identity and recording a `waiting_retry` wait, per the
A2.1/G2 tightening) and re-queue while under the bound; at the bound the request reaches the **`refused`**
terminal. Retryable now covers the token/epoch/header/size/seek/open failures and the mid-job
reopen/seek/read/token/parse/body-probe failures; an **absent** file stays `unnecessary` with no retry.

**C6 turned green and was promoted into the default gate, so the known-red set is empty and
`RESTORE_LIVENESS_RED` is deleted.** No red fixture was deleted, generalized or threshold-relaxed.
**`loopSlotRestoreAttempted_` is subsumed by the request record** (A2.1's retirement clause), removing a
second 64-byte array and leaving one source of truth for exactly-once.

**Two things worth reading in `REVIEW.md` § A2.4:**
1. The C6 fixture's phase-1 oracle had to be **corrected**. It asserted the old latch (`attempted=1`) as
   proof the injected failure was reached — under the fix that is false, so the assertion would have
   **re-encoded the defect as the oracle**. It now asserts the request is still **owed** and not committed.
2. **RAM1 was re-based deliberately, in the same commit, with the reason recorded** in
   `ram1_budget.json` and `REVIEW.md`: the record stores identity + state + terminal per `(track, slot)`
   where the retired flag stored one bit. `teensy41-capture-serial` → `variables 96,192 / locals free
   34,880 / padding 872` (above the 840 floor, still 12 ITCM banks). Changed accessors were made `FLASHMEM`
   first, which kept the bank count at 12.

Evidence: suite **26/26** default (identical with the now-inert flag set); full native **2,313 / 1 skipped
/ 2,312 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**Open and hardware-dependent, deliberately not claimed:** **A2.5** (Packet B device gate — needs the
board, owner permission to flash, the canonical stimulus and a capture), and therefore **A2.6**. **A2.7**
is a recording task and is now recorded in `tasks.md`. **Packet C (A3.1–A3.8), A4.2/A4.3 and closeout
(A5.1–A5.6) remain.**

**2026-09-23 round 11 — A2.3 landed: the queue serves the first startable request, not the priority head.**

`deferred-slot-restore-liveness` is **11/36 checked** (A2.3 added). `popNextStartableDeferredLoopSlotRestore`
walks the priority order and takes the first **startable** request, leaving a blocked entry queued with its
reason; done entries are still removed as queue hygiene. `queuedRestoreMayBegin` moved into the queue TU so
the selection path and the startable pop share one liveness predicate. Both `BeginBackgroundFromQueue` sites
use it instead of a head-only pre-check.

**C4 turned green and was promoted into the default gate in this same slice** (A1.11). **C6 is now the only
known-red fixture**, and its slice is A2.4 (bounded retry).

Two new cases: priority order preserved among startable entries while the blocked head stays queued and is
never taken; and the A2.2/A2.3 **non-claim made observable** — with one request commit-pending the gate is
global, so a second request is tracked as `waiting_job_capacity` rather than served, and queue ordering
cannot fix it.

**RAM1 enforced twice now:** the first build failed at `padding 808` (below the 840 floor) until the new
helpers were marked `STORAGE_PERSIST_MEM`; the line is back to `code 392,328 / padding 888`. The
`tasks.md` placement rule is a gate, not a note.

Evidence: suite **25/25** default; `RESTORE_LIVENESS_RED=1` → 27 cases / 1 failed (C6 alone); full native
**2,312 / 1 skipped / 2,311 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**2026-09-23 round 10 — A2.2 landed: bounded background restore preparation under transport.**

`deferred-slot-restore-liveness` is **10/36 checked** (A2.2 added). `canPrepareBackgroundLoadLoopNow()` +
`backgroundJobMayBeServicedNow()` admit a bounded background **preparation** slice under active transport,
so a queued request's open/read/parse advances while playback continues. **Commit and cleanup keep the
pre-existing full admission** — `canRunBackgroundLoadLoopNow()` still governs applying, the commit
stop-gate in `main.cpp` is untouched, and an unbounded commit under transport is never entered. Capture
keeps its existing deferral (the preparation predicate returns false when a track is recording), so no
recording behavior changed. A prepare-complete request now reports **`waiting_commit_gate`** by name.

**C1 and C2 turned green and were promoted into the default gate in the same slice** (A1.11). The
known-red set is now **C4 (A2.3) and C6 (A2.4) only**.

Evidence: suite **22/22** default; `RESTORE_LIVENESS_RED=1` → 25 cases / 2 failed (C4, C6 exactly); full
native **2,309 / 1 skipped / 2,308 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1
`code 392,328 / padding 888`.

**Not claimed:** a restore *applying* under active transport (runtime-maintenance task 3.3), or that
queue-head skipping resolves the global `waiting_job_capacity` blockage — that is A2.3's explicit
non-claim and A2.2's `waiting_job_capacity` case.

**2026-09-23 round 9 — A2.1's named-wait transition landed.**

A request blocked by a live owner now reports **`waiting_slot_owned`** instead of sitting `queued` with no
reason — the observable form of C4, and the state the A1.8 probe's `queueHeadStartable` and the drain's
verdict will report. Fixture-first: `test_restore_request_slot_owned_denial_names_the_wait` failed
(*Expected 2 Was 0*) before the wiring, then passed. The transition fires at all three selection denials
(empty-idle priority-head check, focus branch early return, parked/focus branch).

Evidence: suite **19/19**; `RESTORE_LIVENESS_RED=1` → 24 cases / 4 failed (the unchanged Packet A set);
full native **2,306 / 1 skipped / 2,305 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS with
RAM1 `code 392,328 / padding 888`. (One anomalous 2,236-case reading appeared mid-round; two consecutive
re-runs both reported 2,306, so it is recorded as a measurement artifact rather than a lost suite.)

**A2.1 remaining:** `waiting_capture` / `waiting_commit_gate` / `waiting_job_capacity` (the gates A2.2
introduces), the `refused`/`unnecessary` terminals (A2.4), pre-publication identity validation, and
`loopSlotRestoreAttempted_` retirement.

**2026-09-23 round 8 — Packet B started: A2.1's request record and its birth/running/committed wiring.**

`deferred-slot-restore-liveness` remains **9/36 checked**; A2.1 is now **in progress** with its first
slice landed. New TU `src/StorageManager/RestoreRequestState.cpp` gives the restore request an identity
`(track, slot, requestGeneration, setEpoch)`, the queued/running/waiting/terminal state split, five named
waits and four named terminals, exactly-one-terminal-per-identity, validated generation advance, and
set-epoch supersede. The production path now drives it: born `queued` at both queue-entry sites,
`running` in `beginLoadLoopJob`, `committed` terminal in `commitLoadLoopJobPublish`.

Two defects were caught by the new cases rather than by review, and both are fixed: the identity check was
**self-satisfying** (the generation advance wrote the new counter into the record, so a stale record always
compared equal) — now guarded by an explicit `identityValid` flag; and `noteRestoreSetEpoch` recorded each
request's `superseded` terminal and then **cleared the records**, wiping the outcome it had just written.

**RAM1 was enforced, not advisory:** the first version cost 64 B of ITCM (`padding 888 → 824`, below the
approved floor of 840) and the post-link gate failed the build; every helper is now `FLASHMEM` and the line
is back to `code 392,328 / padding 888` with 48 B of headroom.

Evidence: suite 18/18 by default; `RESTORE_LIVENESS_RED=1` → 23 cases / 4 failed (C1, C2, C4, C6 exactly);
full native **2,305 / 1 skipped / 2,304 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**A2.1 still open:** the wait-state transitions at the denying gates, the `refused`/`unnecessary`
terminals, pre-publication identity validation, and `loopSlotRestoreAttempted_` retirement.

**2026-09-23 round 7 — Packet A closed; A4.1 landed; the A1.7/A3.1 oracle split is now the contract.**

`deferred-slot-restore-liveness` **9/36 tasks** complete (A1.1, A1.2, A1.3, A1.5, A1.6, A1.9, A1.10,
A1.11, A4.1). Packet A's gate is **taken and green**: default suite 10/10, `RESTORE_LIVENESS_RED=1` →
15 cases / 4 failed (exactly C1, C2, C4, C6), full native **2,297 / 1 skipped / 0 failures**,
`teensy41-capture-serial` SUCCESS at RAM1 `code 392,328 / padding 888`.

Two durable outcomes this round, both evidence-driven:

1. **A4.1 — named boot reconcile outcome.** `SetIndexReconcileOutcome { Ok, Repaired, Failed }` with
   `reconcileSetIndexOnSd` kept as the `Ok || Repaired` wrapper; the boot site logs the named outcome
   instead of only warning on failure. New suite `test_storage_reconcile_index` 4/4 green.
2. **Revision 5 (A1.7/A3.1 oracle split).** Measurement showed a native red fixture against the **real**
   blocking drain needs most of the persistence stack **plus** the display stack and the cold
   edit/track-manager owners — close to the whole firmware, so not a test-only closure. The oracle now
   sits where it is tight: **native** for the drain's pure decisions (budget, progress, wait-reason
   classification, caller-contract mapping) and the **A3.7 device gate** for the integration claim that
   the drain advances a restore. The spec delta is unchanged; only the evidence route is. A1.7 stays open
   for its pure-decision cases, which land with A3.2/A3.3/A3.4.

A1.8 is partial by design: the queue/state probe and the reconcile mapping are landed and default-gate;
the capture-tier transition lines and the `terminal` / `prepare-complete-waiting` states land with Packet
B (A2.1), since emitting them now would invent the state vocabulary A2.1 defines. **Next: Packet B —
background restore liveness (A2.1 identity/state separation first).**


**2026-09-23 — `deferred-slot-restore-liveness` Packet A started: A1.1/A1.2/A1.3/A1.11 landed, no behavior change.**

Implementation has begun. A1.1 built the controllable restore harness in
`test/test_storage_manager_load` (`load_and_restore_harness.h`): payloads written with the production
body writer through the production write boundary, explicit runtime-eligibility controls, and a bounded
turn driver over `selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`. A1.11 wired the known-red
acceptance set behind the test-read `RESTORE_LIVENESS_RED` flag; A1.2 (C1) and A1.3 (C2) are the first
two red desired-behavior fixtures, each asserting the **desired** behavior so it is red on the current
tree and green only once Packet B lands.

Measured harness facts now recorded: this suite's default track state is `TRACK_PLAYING`, so every
fixture must set runtime eligibility explicitly; and `probeActiveLoadLoopJob` cannot see a **parked**
job, although a parked instance keeps the `SlotLoadSession` that `deferredSaveBlockedByActiveSlotLoadSd()`
reports — A1.3 adds the observation-only `probeParkedLoadLoopJob`. Packet A's oracle is structural
request ownership (`served || owned`), not byte progress: `probeLoadLoopJobProgress` can read a previous
request's leftover `bytesRead` and made C1 look green on the untouched tree.

**Update (same day, round 2).** A1.5 (C4) and A1.6 (C6) are landed and red with deterministic oracles;
A1.7 (C5) has its real-drain link closure measured and a new hardware-boundary suite
(`test_storage_drain_restore`) proving it links, with the remaining undefined tail recorded as a scope
decision. Two mechanism corrections are recorded in `REVIEW.md`: C4's stall needs a head that fails the
pop's done-test **and** `noLiveOwnerForSlot` (a committed head is skipped, not blocking), and the suite's
`setUp` never cleared the `LoadLoopJob` instances, so an earlier fixture's parked job could own the next
fixture's request.

Evidence: suite default gate 8/8 green; `RESTORE_LIVENESS_RED=1` → **13 cases / 4 failed** (exactly C1, C2,
C4, C6); full native **2,291 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS at
`code 392,328 / padding 888`. Details and packet evidence:
[`REVIEW.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/REVIEW.md). **No `src/` behavior has
changed**; A1.7 (C5) fixture, A1.8 observability and the A1.9/A1.10 Packet A gate remain.

**Round 3 addition (same day).** A1.7's native closure is now **fully measured**: the drain TU plus a
19-TU import set links in the new `test_storage_drain_restore` suite, and exactly three **boundary**
items remain — a directory view for the in-memory SD card (`open(dir)`/`openNextFile`/`isDirectory`/
`name`), a native wall clock in place of `RtcTime` (whose TU includes `core_pins.h`), and a public
overlay setter in place of `src/LooperState.cpp` (which redefines the harness's `looperState` global).
A first directory-view attempt corrupted the shared `SD.h` mid-edit and was **reverted in full**; that
stub is critical-path for every native suite, so it needs its own reviewed edit with the load suite run
immediately after. **No fixture, red evidence or diagnostic count is claimed for A1.7 yet.**

**Round 4 addition.** The in-memory SD card now models **directory handles** (`open(dir)` +
`openNextFile()` + `isDirectory()` + `name()`), which the saved-set scan needs; the load suite, the
critical path for every native suite, stayed 8/8 green immediately after. Retaining the **real** drain
call then corrected the closure estimate: it is not three boundary items but most of the persistence
stack plus cold display/edit/track-manager owners, and it exposes a genuine ambiguity in
`TrackManagerCaptureQueue.cpp`'s `requestLoopSlotPersistAndSaveState` call (an anonymous-namespace
`(const Track&, uint8_t, uint8_t)` overload competing with `TrackStopTelemetryColdHelpers.cpp`'s
`(Track&, uint8_t, uint32_t)`), which prevents that TU from compiling natively at all. `REVIEW.md` § A1.7
records the options; the recommendation is a deliberate "drain native-link expansion" slice, since
A3.3's budget accounting and A3.4a's re-entry guard are logic a device-only gate cannot falsify cheaply.
That ambiguity is now **fixed** (`TrackManagerCaptureQueue.cpp`, one-line explicit qualification with a
comment): the firmware never saw it because it compiles TUs separately, but an include-based harness that
joins them could not compile that TU at all. The fix unblocked the capture-queue TU natively and both the
firmware build and the native suites were re-run after it. The remaining closure is now a precise list of
cold UI/manager symbols (`DisplayManager`, `EditManager`, `TrackManager` LED/memory-pressure,
`Track::silenceSlotMidiOutput`, the content-undo builder), whose TUs additionally need the OLED library's
include paths in the native env.


**2026-09-24 — A3.5 restructured for the MIDI topology; A3.7 device shape (a) PASSED.**
A3.5's first form serviced MIDI from inside the drain's holdoff wait, which the HITL timing verifier
rejects: every `midiHandler.handleMidiInput()` call belongs in `src/main.cpp` (exactly nine) because the
drain runs on the interval path (`ARCHITECTURE_RULES.md`, `NAMING.md`). The drain now **reports the named
`post_load_holdoff` immediately** instead of waiting it out, preserving the restore and the owed save; the
holdoff expires on its own 100 ms deadline while `main` services MIDI, and the next attempt completes the
save. Topology gate **9/9 no extras**; `test_storage_drain_restore` 6/6; full native **2,337 / 0 failures**;
capture-serial **SUCCESS** `code 390,936 + padding 2,280`. **A3.7 device gate, shape (a) "nothing else
sounding": PASSED** on track 5/slot 1 with a fresh boot and a 24-bar take — `Clear waiting for deferred save
completion` → `State saved successfully (v4)` → `Clear selected slot 1` in <243 ms, with **0** `Clear
aborted`, **0** `Persistence drain stuck`, **0** `drain blocked`, and the timing verifier clean
(`captures/a37-clear-05-fresh/`). Shape (b) completed but did not exercise the save-owed-while-playing case
(partial, recorded). Driver: `scripts/hitl_clear_drain_a37.py`.

**2026-09-24 — A3.6 DEC-081/capture preservation LANDED; Packet C native work complete.**
Three regression cases in `test_storage_drain_restore` (real deferred-save scheduler): a `TRACK_PLAYING`
track leaves `isCaptureRecordingForPersistence()` false and the classifier names no denial (DEC-081 holds);
a recording track parks the multi-frame writers (nothing queued, no write started, the owed save stays
owed); and the mid-pass one-chunk flush still services a real sealed chunk during a take while the
multi-frame writers stay at zero. `isCaptureRecordingForPersistence` semantics unchanged.
**Harness defect found and fixed by the third case:** the in-memory SD stub classified the **raw** mode
argument, so `MidPassChunkPersist`'s seal journal (`FILE_WRITE | O_CREAT`) matched neither branch, fell
through to the existing-file path, and got an invalid handle — the journal had never opened natively and
the whole mid-pass flush was unobservable. The stub now classifies by the meaning-carrying flags
(`O_AT_END` = append, `O_CREAT` = create), because its own `FILE_WRITE`/`FILE_WRITE_BEGIN` fallbacks
disagree with the native core's values. Full native **2,340 / 1 skipped / 0 failures**; capture-serial
**SUCCESS** `code 391,080 + padding 2,136`. **Only the A3.7/A3.8 device gate remains open in Packet C.**

**2026-09-24 — A3.5 holdoff MIDI servicing LANDED and native-verified.**
The finite post-load commit holdoff (100 ms) is now **waited out instead of aborting**: the interval is
declared once (`LoadLoopBudget::PostLoadCommitSaveHoldoffMs`), `postLoadCommitSaveHoldoffRemainingMs()`
reads the remainder **without clearing** it, and `serviceMidiWhilePostLoadCommitHoldoffBlocks(maxWaitMs)`
waits while **servicing MIDI** (`midiHandler.handleMidiInput()` + `yield()`), bounded by the caller's
remaining wall clock. The drain charges real elapsed time against `maxWallClockUs`, enters the wait only
when the pure `holdoffWaitFitsInDrainBudget(remaining, budget)` holds, and otherwise leaves A3.2's
classifier to report the named `post_load_holdoff`. Evidence: `test_persistence_sync_drain_budget`
**18/18** (the fit/does-not-fit bound); `test_storage_drain_restore` **4/4** against the **real** drain — a
fitting holdoff is waited out and an injected DIN message is **consumed during the wait**, while the
production 100 ms holdoff against the minimum drain wall clock (~99 ms) is **not** entered (message
unserviced, holdoff still armed). Red-proven by disabling the wait (`inbox=1`). Full native
**2,337 / 1 skipped / 0 failures**; capture-serial **SUCCESS** `code 391,080 + padding 2,136`. Two native
seams were added for this and are documented in `REVIEW.md` § A3.5: a controllable clock and an injectable
MIDI inbox (shared across `NativeMidiPort` instances, because `MIDI_CREATE_INSTANCE` defines a copy).
**Remaining in Packet C: A3.6 (preserve DEC-081/capture behavior) and the A3.7/A3.8 device gate.**

**2026-09-24 — A3.4a drain re-entry and callback safety LANDED and native-verified.**
The prerequisite for **A3.5 — holdoff MIDI servicing** is in: one scoped owner
(`StorageManagerInternal::PersistenceDrainScope` + `isPersistenceDrainInProgress()`, state = one `bool` in
the measured `StorageSession`), acquired by the detailed drain before any shared mutation. A **nested**
drain returns `TemporarilyBlocked("drain_in_progress")` and the `bool` wrapper returns false, without
changing the outer owner. **Audited and guarded before side effects:** `handleClearTrack` (first statement,
before the stop), `handleUndo`/`handleRedo`, `handleUndoClearTrack`/`handleRedoClearTrack`,
`saveNewSetInternal`, `copySavedSetIntoCurrent`. **Deliberately unguarded:** transport, record, note-edit and
load/save-mode toggle — none resets content or persistence state. New suite
`test_drain_reentry_callback_safety` **6/6** injects a CLEAR through the **real** MIDI route
(`midiButtonManager.handleMidiNote` → processor → callback → `executeAction` → `handleClearTrack`), with a
free-guard control that reaches the slot mutations and a held-guard refusal with **zero** mutations and
zero silences; red-proven by forcing the predicate false. `test_storage_manager_load` **29/29**;
`test_persistence_sync_drain_budget` 15/15; full native **2,332 / 1 skipped / 0 failures**; capture-serial
build **SUCCESS** `code 391,896 + padding 1,320` (the first build failed the 840 floor at 728, so
`handleClearTrack` moved to FLASH under the change's placement rule — 480 B better than the A3.4 line, no
re-base). `restorePlaybackAfterSlotClear` now has one owner
(`src/Track/SlotClearPlaybackRestore.cpp`). Packets A–D tasks and review: `REVIEW.md` § A3.4a.

**2026-09-23 — restore-liveness plan correction (documentation only).**
The final review's callback re-entry finding is addressed in
[`2026-09-24-deferred-slot-restore-liveness/tasks.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/tasks.md)
**A3.4a — drain re-entry and callback safety**, required before **A3.5 — holdoff MIDI servicing**.
The contract adds a scoped owner guard, pre-side-effect refusal of conflicting callbacks, live eligibility
and target revalidation, and production-dispatch fixtures. CLEAR gates consistently permit named refusals
and reject unexplained aborts. The final falsification tightenings are also incorporated: explicit
queued/running/waiting/terminal states and the actual `saveNewSetInternal()` caller, with corresponding
acceptance fixtures. Firmware and verification tasks remain unimplemented/unrun.

**2026-09-23 DEC-081 LANDED: audible playback no longer gates persistence and never stops for it.**

The owner ruled that the "all tracks silent" persistence gate is **superseded** by *never require stopping,
always keep playing*: a slot may be stopped on the selected track as it is being emptied, but every other
playing track keeps playing. Implemented: `isTransportActiveForPersistence()` →
**`isCaptureRecordingForPersistence()`** (recording only; all four call sites updated), `BlockedByTransport` →
**`BlockedByCapture`**, and — deliberately — **the bounded active slice budget is retained during playback**
(`resolvePersistenceSliceBudgetUs` still gets "anything sounding or recording", keeping
`maxPersistenceMicrosActive` **300 us** instead of the idle fallback **3,000 us**; without that, SD bursts
during playback would be ten times longer). Build SUCCESS at `code 392,328 / padding 888`; full native
**2,289 / 1 skipped / 0 failures**; harness suites OK.

**Device-verified on the discriminating case.** With **five tracks audibly playing** and a deferred save
genuinely owed, the clear's drain **completed**: `Clear waiting for deferred save completion` →
`Draining persistence work queue to SD card...` → **`State saved successfully (v4).`** →
`Clear ignored — track is empty`, in **<833 ms**, with **0** `Clear aborted` / `drain stuck` / `drain blocked`
where the old gate gave 33 stuck iterations then an abort. The other tracks kept playing (musical note-ons
sampled before and after on 1-based channels 1, 2, 3, 4, 6), and the same build reports
`overflow_free=True`. The earlier "not reachable" note is superseded: the two failed attempts had faulty
setup (a transport tap that stopped an already-running transport; a sample with nothing sounding), fixed by
toggling transport until serial confirms `Transport started`. **Standing observation, not a DEC-081
regression:** `DIAG,persist_save` still reaches ~40 ms maxima (39.6 ms before the change, 43.3 ms after) —
a single persist *grain* is not bounded by the slice budget, only the loop is. Full entry: `DEC-081` in
[`docs/DECISION_LOG.md`](../DECISION_LOG.md).

**Earlier 2026-09-23 owner asks (persist-as-you-record, content pass): the content pass PASSES and
persist-as-you-record is device-verified.**

Full findings and the exact remaining gates:
[`docs/Plans/persistence_immediacy_clear_latency_refinement.md`](../Plans/persistence_immediacy_clear_latency_refinement.md).

**Content pass: PASS.** `captures/cp02-playback/content-review.json` → `result: PASS`,
`storage_matches_effective: true`, 3 cycles each `inventory_equal: true` / `exact_order: true` with 0
missing and 0 extra (260 rows = 188 record + 48 + 24 overdub). **The owner's read was right:** the base run's
`stored overdub span count=0/15 … count_ok=False` is a **telemetry false negative**, not a storage failure —
its `stored_overdub_span` carries `sevt_missing: true`, and the harness itself documents the SEVT dump as
temporarily unarmed (`sevt_gate_required = False`, "a missing SEVT dump must not by itself turn an otherwise
valid run into INCONCLUSIVE_CAPTURE"), with a matching temporary note in `TrackDeferredMaintenance.cpp`. That
run's actual issues were `record_stored_revt_missing` and
`persistence_active_snapshot_unavailable_during_save` — also telemetry/observation classes.

**Persist-as-you-record: VERIFIED on device.** With the full shape (`captures/content-pass-02/`) there are
**13 `PERS,mid_pass` rows**, including inside live `OVERDUBBING` (584,590,680 · 584,617,581 us) and `PLAYING`
(598,393,317 · 598,402,101 us) windows, each `ok:t4:s0:cNN:n256` for the slot under test, with
`overflow_free=True`. The earlier "0 mid_pass" was a `--record-only` fixture artifact — that mode never
commits a pass, so its chunks are released and never admitted. No further diagnostic is needed:
`PERS,backlog`'s 3rd field is the chunk queue depth.

**Persist-as-you-record (mechanism).** The incremental mechanism already exists — `LoopEventStore::sealChunk` admits each
sealed capture chunk via `PersistenceQueue::admitSealedChunk`, and `stepMidPassChunkPersist` writes one and
emits `PERS,mid_pass` — but it sat behind `processDeferredSaveState`'s transport gate, so a take lived only
in RAM until the stop. **Landed:** a deliberate one-chunk-per-turn exception in
`DeferredSaveScheduler.cpp` (FLASH-resident helper; guard kept for an active `SlotLoadSession`; not starved
by a parked workspace save; the bundle writers stay parked because they are what caused the 90-598 ms
display gaps). Firmware SUCCESS at `padding 888` (48 B above the floor, i.e. 64 B of ITCM spent). Device: the
risk check passes — a 24-bar take reports `overflow_free=True call_sites=True`. **NOT yet effective:**
`PERS,mid_pass` never appeared, because `main.cpp`'s `runDeferredSaveState` requires
`hasDeferredSaveWork()` *and* the persistence idle-owner allowance, so the scheduler is not invoked during a
take. Next: give the queued-capture-chunk flush its own invocation reason, then verify `PERS,mid_pass`
appears between `EMPTY->RECORDING` and `RECORDING->STOPPED_RECORDING`. No native suite links
`DeferredSaveScheduler.cpp`, so this is device-verified only.

**CLEAR latency — diagnosed.** The clear stops only the **selected** track (`MidiButtonActions`,
`track.stopPlaying()`), while the blocking drain's first gate `isTransportActiveForPersistence()` is true
while **any** track is audible or recording. With other tracks sounding the gate can never clear, the drain
spins its whole stuck budget and CLEAR aborts (`Persistence drain stuck ... Clear aborted: could not
complete deferred save first`) — which also explains the intermittency. Preferred fix: **retire the guard per
slot** (supersede the pending work for the slot being reset and clear immediately), preserving the guard's
stated purpose — keeping the writer off a slot mid-reset — as per-slot supersession instead of a global
wait. Silencing every track would be smaller but trades a slow CLEAR for an audible gap, against the
never-stop-playing rule; a bigger slice budget cannot help while the gate blocks.

**Content pass (owner ask):** `captures/content-pass-01/` -> `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)`.
Healthy: `record loop length 1536/1536`, `persistence handoff result=ok duration_us=74940`
(`loop_persist=691 sliced=True ok=True`). Not healthy: `stored overdub span count=0/15 ... count_ok=False` —
the overdub phase did not register. Per the standing rule that class is **retried, never reinterpreted**, so
no content verdict is claimed.

**2026-09-23 SD temp-write truncation FIXED (a pre-existing defect found by the rwso device session) — the boot catalog reconcile is repaired; a separate capture-loss problem still blocks the content PASS and S1.3's slope sweep.**

Root cause, confirmed in the vendor headers: Teensy's `SD.open(path, FILE_WRITE)` is
`O_RDWR|O_CREAT|O_AT_END` — it **appends** — and `FILE_WRITE_BEGIN` is `O_RDWR|O_CREAT` with **no
`O_TRUNC`**, so it only starts at offset 0. Every "write from scratch" in the persistence layer used
`FILE_WRITE`, and `sets/index.bin.tmp` was never cleaned, so one failed write left a partial temp that
every later write appended to. `index.bin` is `[nextSequence:u32][token:u32]` with the token verified
**from the end**, so a 5-byte leftover plus one 8-byte record puts the token at the wrong offset and
`readSetIndexFromSd` fails — and because the write path only runs after a successful read, the state was
permanent. This is the same class DEC-022 fixed locally at one site.

**Fix.** `CurrentSetStorage::openForRewrite` (remove, then open; FLASH-resident) applied to the **16**
from-scratch sites (temps + the binary-copy destination) across `CurrentWorkspaceStorage.cpp`,
`SavedSetIo.cpp`, `RevisionLoad.cpp`, `WorkspaceSave.cpp`, `RevisionCommit.cpp`; **kept** on the
in-place patches (`patchLastActiveUnix`, `patchAnchorFields`, `writeEpochFileHeaderCrc`), the mid-pass
seal journal and the crash log, with a test pinning that. `reconcileSetIndexOnSd` is now
**self-healing** — an unreadable index is recoverable metadata (`nextSequence` is re-derived from the
folder scan), which is what repairs an already-malformed card. **The native SD stub was wrong** —
create-or-truncate "as the write path expects" rather than as the device behaves — so it now models
append/offset-0 and the class is visible natively.

**Evidence.** New `test_storage_manager_load` cases, **red-proven**: with the remove disabled the
stale-temp test fails `Expected 8 Was 13`. Full native **2,289 / 1 skipped / 2,288 succeeded /
0 failures**; `teensy41-capture-serial` SUCCESS at `code 392,200 / padding 1,016` — **112 B better** than
the pre-fix `392,312 / 904`. Device (`52d5a805…`): **two genuine post-fix boots (only two of the four
post-fix logs contain a boot sequence), 0 `could not reconcile`**, against a warning on every pre-fix
boot, with the census unchanged (enabled set 32 chunks, restore walk
480 → 480 at `credits=0`, payload to 441). Plan:
[`docs/Plans/sd_temp_write_truncation_refinement.md`](../Plans/sd_temp_write_truncation_refinement.md).

**The catalog is NOT yet faithful — the fix's stability clause is unmet.** A save now completes
(`CurrentSet saved successfully (v6 deferred slices)`, where the pre-fix failure was
`first_missing=request` with no save markers), and the post-fix boot still shows 0 reconcile warnings.
But an **attributed slot read** (tap track, tap slot, take the CC feedback in that window) gives track 5
slot 1 = **24 bars before the reboot and 3 bars after it**, while track 2 slot 3 stays 18 bars both
times. So reconcile and the save path are repaired, but the restored set is not yet faithful across a
reboot — which is exactly S1.3's prerequisite. Two method corrections are recorded with it: my earlier
"track 5 reads 3 bars" datapoint was a **read-attribution** artifact (a naive reader spanning taps
reports `EMPTY` for a slot holding 24 bars), and **raw-MIDI record driving must not be used** — a direct
attempt started an *overdub* instead of stopping the record and had to be halted (it wrote only into the
approved scratch slot).

**Round 5 localized the operative blocker for the content run, and corrected round 4's conclusion.**
The run aborts at the clear: on device `Persistence drain stuck after 33 iterations` then
`Clear aborted: could not complete deferred save first`, and in the harness narration
`ACTION RECOVERY_FAILED requested=EMPTY … Clear aborted in firmware (deferred save)`. Cause: the blocking
clear-drain cannot advance while the transport is active, and it has no transport-gate classification (generic
`Stuck`); the harness never printed "transport stop before clear long press" because its transport detector
does not see a start preceding its anchor. **It is intermittent** — of the three latest runs, one cleared and
(earlier) one full run recorded 18432 ticks + an overdub + a save, one cleared without recording, and one
aborted at the clear. A suspected serial-open **reset** explanation is **withdrawn**: all three logs have
`boot_rows = 0`, so none reset. The transport-stop prep fixes capture sparsity (561 → 25,164 lines) but not
the clear, because the harness starts the transport itself before pressing clear. **Round 4's "restored
content is not faithful" is therefore not established either** — that 24-bar reading was taken from an
uncommitted, mis-driven session state. What *is* established is restore **determinism** (two consecutive
attributed reads both gave track 5 slot 1 = 3 bars, track 2 slot 3 = 18 bars); write→restore fidelity stays
**untested** because nothing has recorded since. Next, in owners other than the SD fix: (1) firmware —
classify the transport gate instead of `Stuck`; (2) host — make the pre-clear step actually stop the
transport, which is the effective unblock. **That host fix is now landed** (whole-log-first transport check,
matching the precedent in `_ensure_transport_stopped_before_record_arm`; host tests 48/48, 19/19, 30/30) —
but it did **not** resolve the clear failure, and the failing run's log has **no transport start at all**, so
the transport was never the gate. The drain is blocked by one of the two **load-owner** gates
(`deferredSaveBlockedByActiveSlotLoadSd` = `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`, or
`deferredSaveBlockedByPostLoadCommitHoldoff`), with a real 512-byte payload owed, and it reports only `Stuck`
because neither gate has a classification. A boot log shows the restore queued then skipped
(`Queuing boot playback loop slot restore 6 pending; first 5/0`, `LoadLoopJob done 5/0`,
`DIAG,lcr,skip,restore`), but the failing run's log covers only the clear window, so the specific gate cannot
be named from existing telemetry — which is the actionable gap: **classify the gates** first, then choose the
behavioural fix on evidence.

**Round 7 MET the catalog-stability clause, and corrected round 6.** With the authoritative full shape
(`--record-bars 4 --overdub-bars 2`) a recorded loop's **body** persists — `loop_persist=4252 sliced=True
ok=True`, `record loop length 3072/3072` — and after a genuine reboot (`captures/sd-fix-full01-dump/`,
`BOOT,load,start` present, uptime 31 s) its slot dumps **`track=5,slot=0,length=3072` with 190 event rows**,
with **0** reconcile warnings; a pre-existing slot stays faithful at `length=13824` / 1,964 rows. **So the
catalog IS stable**, and round 6's "newly recorded content does not restore" is **withdrawn**: that loop was
recorded with the fixture's `--record-only` mode, which never queued a loop-body persist (`loop_persist=0`,
`first_missing=request`, metadata bundles only), so there was nothing on SD to restore. Whether
`--record-only` should queue a loop-body persist is a fixture question, not a catalog defect. **Still open
under `runtime-working-set-ownership`:** the content PASS (needs a clean managed capture) and the S1.3 slope
sweep (now constructible, since persistence works with the full shape).

**Round 6 found the root, and corrected two of my readings.** `SyncDrainFailureReason` now classifies the
drain gate (`BlockedByTransport` / `BlockedBySlotLoad` / `BlockedByPostLoadHoldoff`) and prints
`Persistence drain blocked by <gate>` instead of the generic `Stuck`; it is built and unexercised because
that verification run's clear happened to pass (padding 952). Two corrections: the per-slot
**CC-feedback probe is not a content instrument** — it reported `EMPTY` for slots holding content and
disagreed with itself across a reboot, so rounds 4 and 6's probe-based fidelity claims are **withdrawn**;
the authoritative read is `!CAPTURE_CONTENT`. And `DIAG,lcr,skip,restore` means **a restore is pending, so
content resolution is deferred** — not "the restore was skipped" — so it is evidence *for* the slot-load
gate. **Authoritative fidelity:** a pre-existing committed slot restores faithfully
(`track=2,slot=2, length=13824`, 1,964 event rows), while a loop recorded this session does **not**
(`track=5,slot=0, length=0`, 0 rows) although the harness reported `CurrentSet saved successfully`
**twice**. **One coherent root:** a queued boot restore (`Queuing boot playback loop slot restore 6
pending`) keeps `SlotLoadSession::isActive()` true, which is exactly the gate
`deferredSaveBlockedByActiveSlotLoadSd()` tests — so it explains both the unrestored new content and the
refused CLEAR drain. Next: why the queued boot restore stays pending.

**Also still blocked, and it is a different defect:** the managed capture truncates mid-run (561 and 1,732
lines against a good run's 45,909) with **0** ring-overflow rows, so the firmware's ring did not shed —
the capture stopped. The harness is capture-driven, so it never reaches the overdub and reports
`INCONCLUSIVE_CAPTURE (SPARSE_CAPTURE)` with a measured `record loop length None`. Documented remedy: a
retry. **The content PASS and S1.3's slope sweep stay open until that capture is clean.**

**2026-09-23 `runtime-working-set-ownership` DEVICE SESSION on `feature/runtime-working-set-ownership` (HEAD `26e96539`, `teensy41-capture-serial`, hex `673e20ca…`): the S2b/S1.5 hardware gate is taken, the restored library survives a reboot, and a clear-drain blocker was diagnosed and worked around.**

Flashed HEAD and ran the new temporary stimulus **`STIM-BASE-2-2-01-TEMP-RWSO-24CAP`** (canonical base
recipe with `--record-bars 24 --overdub-bars 24`) on the authorized scratch slot track 5 / slot 1.

**Leg 1 — the per-committed-slot reservation is gone at boot (device).** `free_chunks` pre-restore is **480 of
512** on HEAD against the baseline's **400**; the default enabled set costs **32 chunks** against **96** (12 per
track); and the library restore walk goes `phase=before 480 → phase=after 480` across all eight `(track,slot)`
rows with `full=0,ready=0,credits=0` on every one — **0 chunks per committed slot**, including the two committed
library slots the walk visits. That is the S2b.3/S1.5/S2b-gate claim on hardware.

**Leg 2 — a 24-bar overdub is admitted.** `record loop length 18432/18432` (24 bars exactly),
**`Overdubbing started` twice**, **`op=overdub_start` denials 0**, 0 `#MEM_DENIED` / `#MEM_EXT_REFUSED` /
`FATAL`, SEVT `bars=24/24 count_ok/span_ok/bars_ok`, display `overdub_ok`/`transport_ok` true. The pre-change
RC-MEM-5 leg recorded zero starts against 4 denials, so the capacity path this change must not regress does not.

**Leg 3 — the restore works once but is NOT stable across boots, and my "library can grow" claim is withdrawn.**
`dump_and_capture.py` resets the board on serial open, and its dump showed `CAPTURE_CONTENT,begin,track=5,slot=0,
length=18432` — so a slot *can* come back byte-exact right after its own save+reset. I over-generalised that to
"the library can grow"; a growth attempt falsified it. After a second forced reboot
(`captures/rwso-growth/boot-n2/`) the recorded obstacle reproduces at **catalog** level:
`could not reconcile MidiLooper/sets/index.bin`, `Queuing boot playback loop slot restore 6 pending; first 4/0`,
`Track 6 loaded state: EMPTY` (and track 7), `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set
**changed between boots** (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`), and track 6 emits no length
feedback at all. **The obstacle is the SD catalog reconcile, not scratch-slot approval, and S1.3's slope sweep
stays blocked** — the restored *set* is unstable across boots. **[Attribution CORRECTED 2026-09-24: this
paragraph's stated obstacle is withdrawn. `MidiLooper/sets/index.bin` is
`SavedSetCatalog::SetIndex{ uint32_t nextSequence; }`, a saved-set folder counter, so a reconcile failure
cannot by itself omit a slot; the shrinking boots report `reconcile_warnings=0`; `DIAG,lcr,skip,restore` is
a content-resolution line; and `Track 6 loaded state: EMPTY` appears on stable boots too. The instability is
real and now owned, unattributed, by `restored-library-stability` Packet A — see the 2026-09-24 session entry
at the top of this file and that change's `ARCHITECTURE-REVIEW.md` § Task 1.1.]** **One datapoint of mine is withdrawn:** the
"track 5 reads 3 bars" reading was a **wrong-slot read** (I pressed only the track button); track 5 **and slot 1**
reads `24 bars (18432 ticks)`, and a further dump again reports `length=18432`, so this slot does survive the
hard reset. Also recorded: a boot census needs a **forced** reboot (a serial open only sometimes resets), and
repeated runs without a reboot accumulate residency — `live` 4,658 KB → **8,170 KB** with `heap_free` pinned at
its 52 KB minimum.

**Leg 4 — content NOT a formal PASS, but the two "missing" rows are proven present.** `storage_matches_effective:
true` with all 24 bars dense (`per_bar_effective` 70–84; `effective_rows_all = expected_note_cc_rows = 1,746`),
but the verifier's single inter-anchor cycle reports `inventory_equal: false` with **2 unexplained missing rows in
bar 0** (`[128,5,76,0]`, `[176,5,74,101]`). Neither is a documented residual class, so the guide's bar is **not
met**. However, locating each row against the two anchor occurrences (48.001 s apart) finds **one matching
occurrence per cycle for each** — `note76` Off near `anchor − 80 ms` in both cycles, `note74` On near
`anchor + 1 ms` and `+ 44 ms` — so playback carries both; the verifier's cycle-local inventory just does not bind
them (exact tick attribution not established). A third, longer capture (160 s) went
`INCONCLUSIVE_CAPTURE: no unique effective event recurs` because the restored set — and hence the playback mix —
drifts between boots, so **content verification of this slot is confounded by the same catalog defect**.
`capacity_path_exercised: false` (densest bar 84 refs vs the old 256 ceiling): a parity run, not a re-stress.

**Blocker: CLEAR cannot proceed while transport is active.** Two attempts died at
`Persistence drain stuck after 33 iterations … Clear aborted`. `StorageManager::processDeferredSaveState`
returns immediately while `isTransportActiveForPersistence()`, so the blocking clear-drain can never advance; and
the drain has **no transport-gate classification** — `maybeAdmitFinalizeWorkspaceAfterDrain()` returns `true` for
merely-queued work, so it never takes the `AdmissionRefused` fast path and reports the generic `Stuck`. The host
did not step in because the failed log carries **zero** `PERS,*`/`SAVE,*` rows (only `DIAG,persist_save`/
`hostdrain` timing telemetry, which is not a pending marker), and its transport-running check cannot see a
transport start preceding its anchor. **Workaround (device prep): stop the global transport and idle until the
owed workspace save drains, then run.** This defect pair is pre-existing and outside this change; it blocks any
content run on a device that boots with a pending workspace save.

**Also observed (not a measured slope):** repeated runs **without** a reboot accumulate residency — the same
device read `live=8,170KB`, `chunks_free=431`, `heap_free/heap_min=52KB` before the re-flash, against
`live=4,658KB`, `chunks_free=441` at the fresh boot of Leg 1, with the S1.1 per-owner vector
(`tag_total=1776KB`) unchanged in shape. Recorded as an observation; no per-N census exists to attribute a slope.

**A later round tried the non-destructive lever for a formal content PASS and it does not work.** Muting the
other seven tracks (double press on the track button, `Track 1..4, 6, 7, 8 mute toggled` confirmed) silences
**everything**: the 160 s capture carries only LED feedback and clocks, and the verifier returns
`INCONCLUSIVE_CAPTURE` again. Muted tracks still run playback (six `Playback started` rows), so this is
suppression, not a stopped transport. Measuring played channels directly: all unmuted → 1-based channels
1, 2, 3, 4, 6; others muted → **none**. So the slot under test contributes no notes now, although its content is
dumped as present (`track=5,slot=0,length=18432`, 1,746 rows, all 24 bars dense) and channel 5 **did** play in
the earlier 2-cycle capture. **The set of tracks that actually play drifts across resets, exactly as the restored
set does**, so a formal PASS needs the persistence/catalog defect fixed first — it is not reachable by capture
configuration. Mute state was restored and the transport stopped.

**Round 3 closed the last route to a formal content PASS.** Re-recording the 24-bar slot and capturing
immediately — the configuration of the near-parity run — came back **degraded, not faithful**:
`record loop length 19200/18432 (raw 19200)` (a 25-bar over-run where the earlier run sealed at exactly
18,432), `stored overdub span count=0/191` with `overdub_ok=False transport_ok=False` (though the firmware log
does hold two `Overdubbing started` rows, 0 `FATAL`, 0 `#MEM_EXT_REFUSED`), `persistence handoff
first_missing=request` with **no** `PERS,request`/`SAVE,*` markers so the re-record was **not persisted**, and a
**1,574-line** sparse session log against the faithful run's **45,909**. No firmware or SD content was changed
by that round; the transport was left stopped. **Across rounds 1–3 the same condition held:** the persisted
library neither stabilizes nor restores faithfully, so no deterministic single-slot capture can be produced.

**Still open:** S1.3's slope sweep and failure point, the content-verified PASS, S1.4's pass→chunk conversion,
S1.5's device refusal point, and phases B–F. **All of the device items are gated on the persistence/catalog
defect**, which is a StorageManager/load owner outside `runtime-working-set-ownership` and should be its own
change. Full evidence and the exact citations:
[ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § Device
session — RWSO-24CAP. **Note:** the pre-change baseline revision remains `83be2e19`; no baseline firmware was
flashed in this session (its census is already on record).

**2026-09-23 `runtime-working-set-ownership` S1.5 clause 3 answered natively on `feature/runtime-working-set-ownership`: the dense-24-bar seal is diagnosed, and the refusal point is outside the supported envelope — test-only, no firmware change.**

S1.5's third clause ("the smallest N at which a 24-bar overdub is refused") had been blocked for three rounds
by an upstream puzzle: a dense 24-bar construction did not seal at all, and two findings about why were
withdrawn. **The branch is now identified.** `Loop::sealCapture` runs
`CaptureIncrementalSanity::removePairsShorterThanNoteMinLength` **before** its empty check, so note pairs
closer together than `Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` (**8**) are all removed, the live store empties,
and the seal returns `FailedValidation` from the **`capture.store.empty()` branch** — not from the transfer
precondition that both withdrawn findings had inferred from reading the code after seeing a failure their own
construction produced. The signature is exact and asserted: the events really are in the store before the seal,
`beginCapture` holds one tail chunk (free 511), and the failed seal **releases** it (free **512**, nothing
retained), while the positive control at exactly 8 ticks seals `Ok` and keeps its chunk. The withdrawable
"staging-order" rule is also withdrawn: all three append/prewarm orders seal with pairs >= 8 ticks apart.

**The answer.** With a valid construction — 864 pairs = 1,728 events = **7 chunks** per dense 24-bar loop at
the task's recorded ~72 events/bar, committed across the 8 x 8 matrix — boot costs **32 chunks**, and at
**N = 63** the probe's free chunks are **39 before and 31 after**: the 24-bar reserve admission is
**ADMITTED at every N up to 63**, so **the smallest refusing N lies outside the supported envelope**. The
library at N = 63 holds 108,864 events (83 % of the 131,072-event ceiling) and still keeps 31 chunks free, 7
above `CHUNK_RESERVE + ACTIVE_OVERDUB_TRANSFER_CHUNKS` (24).

**Red-proven.** With the pre-S2b.1 per-slot reservation restored, the same construction refuses at **N = 30**
(free = 6 chunks, below the 8-chunk reserve) and the fixture fails on its admission assertion at `free before
probe=0`. So the HEAD answer is a **capacity result of the S2b.1 removal**, not a property of the pool. The
mutation was reverted; `src/` is byte-identical.

**What this does not establish.** The device's refusal point is **byte**-limited (PSRAM), not id-limited — that
is S1.3's device leg — and the ~191-chunk figure the task cites for a 24-bar overdub's peak reservation is
**not reproducible natively**, because those byte terms (views, companion rows, snapshot inputs) are
heap-backed off-device. No device capacity number is claimed.

**Gates.** `test_continuous_capture` **97/97** (+2 cases); full native **2,287 / 1 skipped / 2,286 succeeded /
0 failures** (was 2,285/1/2,284); `teensy41-capture-serial` **SUCCESS** with the post-link RAM1 budget at
`code 392,312 / padding 904 / locals 34,944` — unchanged, as expected with no `src/` change. Full packet and
evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ P1 — S1.5 clause 3. **Still open:** the device legs (S1.3's slope, S1.5's device refusal point, S1.4's
pass->chunk conversion) and phases B–F.

**2026-09-23 `runtime-working-set-ownership` S1.3 clause 2 landed on `feature/runtime-working-set-ownership`: the synthetic 60-descriptor characterization is green and red-proven — test-only, no firmware change.**

S1.3's **device** slope stays blocked (the restored scratch slot does not survive a reboot — `Track 6 loaded
state: EMPTY`, `index.bin` does not reconcile), so its **native** clause was the unblocked step, and
`S2b gates` requires it. New `measureWorkingSetResidenceAcrossTracks(N)` in `test/test_continuous_capture`
commits one-note descriptors across the 8x8 slot matrix — the existing single-track helper could not express
N > `MAX_LOOPS_PER_TRACK` = 8 — and two default-gate cases now characterize the law at the contract's envelope:
`test_s1_3_sixty_descriptor_characterization_is_well_formed` and
`test_s1_3_residence_is_flat_across_sixty_descriptors`.

**Numbers (exactly one active role in both variants).** Canonical **reservation flat at 40 chunks (205,280 B
device)** at N = 8 and N = 60; the constant decomposes as boot's stop-descriptor tail reserve (32 chunks =
8 tracks x 4 `CaptureStopJobs` x 1 chunk, bounded by the enabled set) plus the one active role's own canonical
copy (8 chunks) — so it is not a claim of zero reservation, but of nothing reserved **per committed
descriptor**. Payload is reported separately and grows 8 -> 60 chunks (41,056 -> 307,920 B); the runtime
**directory** grows 15 -> 60 slots (95,160 -> 380,640 B at the S3.0 `LoopPlaybackRuntime` 6,344 B pin) and is
reported, **not** asserted flat. Inactive descriptors hold **0** canonical credits and **0** transfer horizons
(0 over 59); resident working sets stay **1**; 100 of 512 chunks used, so the reserve is intact and the flat
result is not a capacity artifact. Native byte figures were **rejected as device numbers**: native
`sizeof(EventChunk)` is 8,208 B against the device's 5,132 B, because `MidiEvent`'s `sysexData` union holds a
64-bit pointer. Chunks are the device-identical unit and are what the law is asserted on.

**Red-proven, not assumed.** Re-introducing the state S2b.1 removed (making `prewarmCanonicalCaptureOutput()`
unconditional in `Track::prewarmPlaybackForSlot`) fails **both** cases — `reserved(8)=152` vs
`reserved(60)=452` (~8 chunks per descriptor, the RC-MEM-5 slope) with the well-formedness case catching the
pool exhausting at `free=0` — and seven suite cases go red, which is the expected cross-check against the
1-vs-4 law cases and B01. The mutation was reverted immediately and `src/` is byte-identical (`git diff` on
`src/` is empty).

**Gates.** Full native **2,285 / 1 skipped / 2,284 succeeded / 0 failures** (was 2,283 / 1 / 2,282 — exactly
the two cases added); `teensy41-capture-serial` **SUCCESS** with the post-link RAM1 budget enforced at
`variables 96,128 / code 392,312 / padding 904 / locals 34,944`, byte-identical to the recorded line as
expected with no `src/` change. Full packet and evidence:
[ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § P1 —
S1.3 clause 2. **Still open:** every device leg (S1.3's slope to the failure point, S1.5's refusal-N, S1.4's
pass->chunk conversion) and phases B–F.

**2026-09-23 SESSION CLOSEOUT — `runtime-working-set-ownership` P0/P1 native scope complete on `feature/runtime-working-set-ownership`; the remaining gates need hardware.** Read this instead of the per-round entries below it.

**What landed and is verified.** S0's decisions verified against their landing sites (no re-decision). S1.2's red scaling characterization, now **green**: the per-committed-slot boot reservation is gone, so reserved chunks and resident working sets are flat in recorded slots (they were 96→120 chunks and 8→11 bundles). The accepted-stop latch (S4f.0/S4f.1/S4f.2/S2b.6) with its deferred drive, canonical park and retire-path drain; the three boot/load admission tasks (S2b.1–S2b.3); the S4.0 per-slot liveness predicate with the S2b.4 release; S4f.5's stop-related in-flight consumers; S4f.4's four paused-worker transitions; B03 and B04 in the new `test_storage_manager_load` harness; S3.0's placement pins with a **per-env post-link RAM1 budget check**.

**Numbers.** Full native **2,281 cases / 1 skipped / 2,280 succeeded / 0 failures**; `test_continuous_capture` 91/91; the load harness 5/5. All four firmware envs build and pass their per-env budgets. **RAM1 `padding` improved 872 → 1,096**: the FLASH extractions of `needsSlotLoad`, `resetLoopSlotToEmpty` and `hasRealtimeDeferredIdleWork` more than repaid the 32 B the latch cost, and the post-link check that enforces the approved budget caught two real defects in itself on the way (a configuration-specific pin and a global-vs-per-env baseline).

**Four defects found by fixtures, not by review** — the argument for the fixture-first rule: `startRecordingTrack` ignored the latch and admitted a record start on top of an unaccepted-invisible stop; `finalizeCaptureAndSelectSlot` moved the active slot underneath an unadmitted stop; `LoadLoopJob`'s callers popped a restore request before checking liveness and the queue would not take it back; and `driveAcceptedInteraction` parked a request whose interval was gone, which would have wedged the track forever.

**Two corrections to my own record, kept visible.** An "N=2 refusal" measurement was a misread `SealOutcome::FailedValidation` (an enum read as a count), and a follow-up "seal needs the transfer horizon" finding was **retracted** after its own bisect showed the horizon was not the discriminator — the real lesson is that `loop.appendCaptureEvent` is not the production ingress once the arm has admitted staging.

**Open.** (a) Every **device leg**: S1.1's per-owner census, S1.3's slope to the current failure point, S1.4's undo/canonical-pin baseline, S1.5's smallest-refusal N, and the S2b device gate — all needing the owner's go-ahead to flash. The **baseline revision is pinned and proven** (`83be2e19`, firmware-identical to the branch base, with the reproduction recipe and per-leg measurement list in `ARCHITECTURE-REVIEW.md`). (b) S3.0's pins for members the later stages introduce. (c) Phases B–F of the change itself.

**Recommended next step:** a device session — baseline `83be2e19` first, then HEAD — using the documented recipe and per-leg list.

**2026-09-23 `runtime-working-set-ownership` P0 started on `feature/runtime-working-set-ownership`: S0 verified, S1.2 red characterization landed — no firmware change.**
First slice through the DEC-080 execution-packet workflow, on a branch cut from the planning line (the
change does not exist on `dev`, so committing the handoff docs as the branch base was the only base that
preserves it). Packet and full evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § P0.

- **S0 (decisions-only) verified, not re-decided:** every item checked against its landing site — DEC-076
  points 1/2/8, DEC-078, `design.md` Revisions 2/3/4/7/12/16/18, `tasks.md` S0.5. No code change follows
  from any of them. One observation recorded rather than skipped: S0.6 asks for the resumable work-item
  contract "as a convention plus `DEC`", and there is **no dedicated `DEC-###` id** — DEC-076 point 8 with
  Revision 16 and `NAMING.md` § Pipeline of role-owned stages bind it.
- **S1.2 (red scaling fixture) implemented in `test/test_continuous_capture`**, driving the production
  boot/load admission and the record-arm admission (no copied lifecycle): two law cases behind
  `WORKING_SET_RED=1` plus a default-gate well-formedness case.
- **Measured, at a constant active set:** boot admission reserves **96 chunks with 1 recorded slot and 120
  with 4** — a slope of exactly **8 chunks = `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` per admitted boot
  slot** — and resident working sets go **8 → 11**. Canonical **payload** is recorded separately from the
  **reservation** (payload 1 → 4 chunks), so the slope is not confused with content. Both law assertions
  fail as designed: `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` → **2 failed,
  78 succeeded** (`Expected 96 Was 120`, `Expected 8 Was 11`); the default gate stays green at 78/78.
- **Verification:** full native **2,263 cases / 1 skipped / 2,262 succeeded / 0 failures** (baseline
  `7f90d996`: 2,262 / 1 / 2,261); `teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,128 /
  code 392,344 / padding 872 / locals free 34,944`. Only a test file changed, so the firmware artifact is
  unaffected and `padding 872` is the recorded line.
- **D0.2/D0.3 started (partial, same session):** the P1 harness controls that S2b's fixtures need are in
  place — test-controlled transport state (the harness's hardcoded `isTransportRunning()` is now a
  variable reset in `setUp`), reservation denial/recovery through **real pool-credit exhaustion**
  (`LoopEventStore::ChunkReservation`), and a read-only ownership census on
  `TrackCaptureStopAccess` (`canonicalReservedChunks`, `canonicalOutputAdmitted`, `liveRingEntrySlot`,
  `transferHorizonAdmitted`, …). **B01 is now red as designed:** with four committed slots and one
  active role, the three inactive slots hold **8 canonical-copy credits each (24 total)** where the law
  requires zero. Outstanding and named rather than implied: the dispatch pause-and-step control, the
  linked load / transport-retarget paths, and B07's allocation census — the last is **not measurable
  natively** because the pool wrappers are `teensy41-capture-serial` link flags only. **No production
  behavior has changed; only a test file.**
- **First production behavior landed (same session): the accepted-stop latch.** DEC-078's core — an
  accepted stop is retained and executes once when the canonical-copy target returns, instead of being
  dropped and needing a second press — is implemented and native-verified. `beginDeferredOverdubStopForSlot`
  (S4f.0) parameterises admission by the interval's slot; `Track::PendingInteractionLatch` (S4f.1, Stop
  variant) holds `{kind, destination, slot, switchTarget, interval, phaseTick, transportEpoch}` with no
  ring capacity; the dispatch branch sits in `processDeferredIdleMaintenance` before the owner-specific
  branches (S4f.2, via the named entry `serviceAcceptedInteractionTurn`); and
  `parkAcceptedStopForCanonicalOutput` re-runs the slot's reservation and reports
  `accepted_stop_finalizer_wait` as a wait (S2b.6, partial). New fixture **B02** is green in the default
  gate: one press under a denied canonical target → retained → exactly one job → exactly one completion.
  **The S3.0 placement pin earned its keep:** the `unique_ptr` form of the latch cost 64 B of RAM1 and
  took padding **872 → 856, below the documented floor**; storing it in the already-external ring object
  restored the line to `variables 96,128 / code 392,344 / padding 872 / locals free 34,944`, identical to
  the pre-change build. Full native **2,264 / 1 skipped / 0 failures**. **S2b.1 (boot/load reservation
  removal) is deliberately not in this step**, so phase A is not complete; S4f.1/S4f.2/S2b.6 are recorded
  as partial and S4f.5 (the 49 in-flight consumers), S4f.3, S4f.4's epoch counter, S4.0, S2b.2-S2b.5 and
  B03-B07 remain open.
- **Next slice pinned by reading the tree, not assumed.** Two hazards must ship with S2b.1 (boot/load
  reservation removal): (1) `Track::startOverdubbing` never admits the transfer horizon, so with the boot
  reservation gone a loaded slot's stop would fail `transfer_denied` forever — that admission belongs at
  the overdub start's cold point, **not** in the park, because
  `prewarmActiveOverdubTransferHorizon` relocates `passes.overdubPasses` and must not run mid-interval;
  (2) `prewarmCanonicalCaptureOutput` requires an empty `canonicalCaptureOutput.store`, which the retire
  block never touches, so S2b.1 must ship with S2b.6's retire-path invariant or a loaded slot's second
  overdub can wait forever. Recorded in `ARCHITECTURE-REVIEW.md` § P1 rather than implemented in a hurry.
- **S2b.1/S2b.2 landed: boot/load canonical admission is gone, and the residency law is green.** The
  three red characterizations written hours earlier now **pass in the default gate** and the temporary
  `WORKING_SET_RED` gate is deleted: reserved chunks are flat in recorded slots (was 96 vs 120), resident
  working sets are flat (was 8 vs 11), and inactive slots hold zero canonical credits and zero transfer
  horizons (was 24 credits over 3 slots). `Track::startOverdubbing` now admits its own reserve set for a
  slot boot left cold via the FLASH-resident `admitOverdubStartWorkingSet()` — the view/bridge/companion
  scratch (without it the stop's estimates are `UINT64_MAX` and admission denies as
  `preparation_service_unavailable`), the transfer horizon, and the prepared-companion-history horizon
  (without it the job admitted and never retired) — while START stays **not** gated on the canonical copy.
  S2b.6's retire-path invariant drains `canonicalCaptureOutput.store` at retirement, which is what makes
  the removal safe. Two implementation findings worth keeping: granting the companion history
  unconditionally broke the DEC-072/5.12 adversarial reentry case (`prewarmPreparedCompanionHistory`
  resets the Loop's prepared history), so it is gated on no job being in flight; and the whole admission
  body had to move out of ITCM-resident `startOverdubbing` into FLASH (568 → 856 padding recovered).
  **Reported deviation, not silently accepted:** RAM1 `padding` is **856 against the recorded 872 line** —
  16 B of linker alignment, `code + padding` constant at 393,216, **no bank cross**, `locals free`
  unchanged at 34,944. It needs the architect's call (re-base the line with that arithmetic, or find the
  16 B elsewhere) before S3.0's static_assert work. Full native **2,268 / 1 skipped / 0 failures**.
- **S4.0/S2b.4 core landed: the per-slot liveness predicate and the canonical-reservation release.**
  `Track::noLiveOwnerForSlot(slot)` scans the **live ring window only** (a retired entry still carries
  `slot == 0`, so a whole-array scan would block release forever — the B6 stale positive), plus the slot's
  `captureStopOwner`, an in-flight prepared handover, and the S4f.1 latch. The published pair is
  deliberately not a term (it stays valid while the slot sounds). `Track::tryReleaseCanonicalCaptureReservation`
  enforces the predicate itself so no caller can bypass it, refuses a non-empty store, and is idempotent.
  Fixture `test_s4_0_release_respects_the_live_ring_window_and_is_idempotent` is green; full native
  **2,269 / 1 skipped / 0 failures**; RAM1 unchanged at `padding 856` (every new function is FLASH-resident,
  so the reported deviation did not grow). S4.0 and S2b.4 stay **open** with their remaining terms recorded:
  the job-owned-undo-root term (needs S5.2a's per-slot descriptor), the deferred-reconstruction term (S5.2e),
  and the mid-copy release fixture.
- **S4f.5's stop-related consumers landed, and the audit found two real holes.** Most start/switch
  guards already held transitively because a latched stop leaves the interval in `OVERDUBBING`, but two
  did not: `TrackManager::startRecordingTrack` checked only `hasPendingDeferredOverdubStop()`, so it
  admitted a record start on top of an unaccepted-invisible stop; and `finalizeCaptureAndSelectSlot`'s
  switch path fell through to `setActiveLoopIndex` after a *non-admitted* `stopOverdubbing()`, moving the
  active slot underneath the accepted stop — which the contract forbids in this intermediate slice. Both
  fixed via the new `Track::hasStopInFlight()` / `hasLiveCaptureInterval()`, also adopted by the three
  persistence predicates and `resolveActiveCaptureTrackSlot`. Fixture
  `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` is green; full native
  **2,271 / 1 skipped / 0 failures**. **The RAM1 deviation grew from 16 B to 32 B** (`padding` **840** vs
  the recorded 872; `code + padding` still constant at 393,216, still no bank cross, `locals free`
  unchanged) — the two new ITCM call sites to the FLASH-resident predicate. Making the manager entries
  FLASH-resident is a placement decision for the architect.
- **S3.0 placement pins landed** (the pulled-forward prerequisite for the fields this slice added).
  `Track::placementPins()` pins the latch layout platform-independently and, device-only, `Track` and the
  ring object; a separate block pins `LoopPlaybackRuntime` 6,344 / `Loop` 984 / `TrackPlaybackRuntime` 32.
  Device sizes were **measured, not guessed** (template probe), and the pins are **effective** — a
  deliberately wrong latch pin failed the device build in all 8 TUs that include `Track.h`. The pins emit
  no code (`code` 392,376 / `padding` 840 unchanged). **S3.0's own RAM1 clause is still not met:** padding
  **840** vs the recorded 872 (`code + padding` constant at 393,216, no bank cross, `locals free`
  unchanged at 34,944). That re-base-or-move-ITCM decision is the one open item blocking a clean S3.0.
- **RAM1 decision taken and enforced (architect, 2026-09-23).** The 32-byte ITCM increase is **accepted**;
  the padding floor is **re-based 872 -> 840** for this slice and the manager entries stay in ITCM (moving
  them to FLASH would be a separate execution-placement change needing its own latency verification). The
  correction that mattered: **size pins do not close the placement gate** — `static_assert` cannot see
  instructions, linker veneers or alignment, so the earlier "the pins make any further drift a build error"
  claim is withdrawn. Enforcement is now **post-link**: `scripts/ram1_budget.json` holds the approved budget
  and the floor's history (edited deliberately, never auto-updated) and `scripts/check_ram1_budget.py` runs
  the platform's own `teensy_size` on the linked ELF via `AddPostAction` from `[env:teensy41]`, failing the
  build when the ITCM region, RAM1 variables, the local allowance or the floor change. Verified both ways:
  the normal build prints `variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944`
  and passes, and temporarily raising the floor to 900 **fails the build** with an actionable message.
  **840 B is still tight** — the next bank cross would drop the local allowance to ~2,176 B — so a
  deliberate cold-code placement improvement is planned as its own change, kept out of this
  runtime-correctness slice.
- **S2b.5 split into a done half and a harness-blocked half.** The predicate S2b.5 needs **is**
  `Track::noLiveOwnerForSlot(slot)` (S4.0), and its per-slot scope is now asserted in the S4.0 fixture: with
  a live job on slot 0, `noLiveOwnerForSlot(1)` is **true** -- a finalizing predecessor blocks a load into
  *its* slot and not another slot of the same track, which a track-level "capture active" flag would fail.
  The remaining work is local (`LoadLoopJob.cpp`'s callers pop the restore entry *before* `beginLoadLoopJob`,
  and re-queueing is inert, so a load can destroy a live owner) but it is **not implemented**: that path has
  **zero** native coverage -- the native `SD.h` stub is two lines and no suite links `LoadLoopJob.cpp` or
  `LoopSlotRestoreQueue.cpp` -- so shipping it would be an unverified change to load behavior. The harness
  gap and the full B04 plan (files to link, in-memory SD/File emulation, stub boundaries, assertion
  sequence, and the ordering against B06 and the mid-copy fixture) are pinned in
  `ARCHITECTURE-REVIEW.md` § P1 -- S2b.5. Full native **2,272 / 1 skipped / 0 failures**.
- **Two decisions recorded for the architect (2026-09-23), both about not inventing on the implementer's
  authority.** (1) **S4f.4's transport-epoch counter**: the drive already satisfies the epoch's behavioural
  requirement -- it re-derives the destination from live transport state and the boundary tick from
  `getCurrentTick()`, never trusting the stored absolute tick -- and no existing tick-domain epoch can be
  reused, so the contract's new `TrackManager` counter is new RAM1 state (`Track`'s `.bss` additions are
  documented as costing ~4 KB of locals each) with only a diagnostic use. The latch currently stores a
  `transportEpoch` nothing consumes, which is worse than wiring or removing it; **recommended: drop the field
  (latch 40 -> 32 bytes, update its pin) and record the epoch as satisfied by live re-derivation**.
  (2) **The load harness is a multi-slice build**, with increment-1 findings from a one-build probe: the
  native link script allow-lists only `test_continuous_capture` for the Arduino/SD stub path; the `SD.h` stub
  declares only `struct File {};` with **no `SD` object**, so an in-memory SD/File emulation is needed rather
  than a header tweak; and `CurrentSetStorage::verifySaveFileTokenAtPath` is not visible, so those units must
  be linked. Nothing was committed from the probe and the tree is unchanged.
- **Shared load harness increment 1 landed: the load path now links natively.** New suite
  `test/test_storage_manager_load` compiles, links and runs `StorageManager/LoopSlotRestoreQueue.cpp` --
  the path had **zero** native presence before. Blockers removed at the right boundary: the firmware
  `SD.h` stub (two lines, no `SD` object) is now an in-memory card; the harness defines `ARDUINO 1` because
  `CurrentSetStorage`'s SD declarations -- including `verifySaveFileTokenAtPath` -- sit behind
  `#if defined(ARDUINO)`, **which is precisely why this path was never natively testable**; the native
  serial stub gained `println`/`flush` (activated by that define); and `native_test_link.py`'s hardcoded
  suite name is now an allow-list. Two facts now asserted that shape the S2b.5 fix: the queue orders
  **ascending** by `restorePriority` (the first test asserted descending and failed, which is how the real
  contract got pinned), `peekFirstPendingLoopSlotRestore` has **no done-test**, and a **forced enqueue
  already exists** (`appendBootLoopSlotRestore`) -- the contract's alternative -- so S2b.5 need not invent
  one. Next increment: link the `TrackManager`/`Track`/`Loop` set so `needsSlotLoad` and the pop done-test
  go live, then drive `beginLoadLoopJob` for B04, and only then make the S2b.5 call-site fix. Full native
  **2,275 / 1 skipped / 0 failures**; all firmware envs pass their per-env RAM1 budgets.
- **Load harness increment 2 pinned to a measured structural choice.** Increment 2 needs
  `Track`/`TrackManager`/`Loop` because `StorageManager::needsSlotLoad` ends in
  `track.getLoop(slot).hasCommittedPasses()`. Probing the obvious route -- adding the queue to
  `test_continuous_capture`, which already links those -- produced exactly three error classes and was
  **rejected**: it would force `ARDUINO 1` for that whole TU (changing what *every* included production TU
  compiles, since the tree's SD declarations sit behind `#if defined(ARDUINO)`, and the suite's 87 tests
  currently depend on those guards being off), delete two now-duplicate stubs from `off_path_deps.h`, and
  expose an unknown cascade through 87 unrelated tests. **Recommended next increment:** extract the shared
  production include block into `test/test_support/track_harness.inc` and include it from both suites --
  the load path gets its Track objects, the existing suite's boundary surface is untouched, and the include
  set stops being duplicated. The probe was fully reverted; tree clean and `test_continuous_capture` 87/87.
- **Load harness increment 2 landed: the load path links with the full Track harness.** The earlier
  rejected route's problem was dissolved rather than worked around. `test/test_support/track_harness.inc`
  now holds the shared production include block **plus** the harness globals and native clock boundary,
  extracted verbatim from `test_continuous_capture` (include order is significant, so it moved as a block)
  and included by both suites -- so the include set stops being duplicated instead of gaining a second copy.
  `off_path_deps.h` gained one explicit seam, `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`, so a suite linking
  the real loop-slot-payload functions opts out of those two stubs while every other suite keeps the
  fail-closed route; and the load suite links `MidiHandler.cpp` directly because `TrackManager`'s
  constructor references the global handler and this suite has no second TU to carry it. Verified at each
  move: `test_continuous_capture` **87/87** after both extractions, load suite **3/3**, full native
  **2,275 / 1 skipped / 0 failures**, firmware + per-env budget SUCCESS. **Next: B04 itself** -- build a
  finalizing interval on a slot, queue a restore for it, run the real selection/admission and assert the
  entry is still queued, the Loop was not reset and the finalizer is intact; only then the S2b.5 call-site
  fix against that red fixture.
- **B04 narrowed to a one-step-away, fully specified increment.** With the harness in place the defect
  mechanism was written as a fixture, which sharpened B04's trigger rather than just its shape: the caller's
  **pop destroys the request** (`queueDeferredLoopSlotRestore` is inert for a slot with committed passes or
  one already attempted), and the slot's **destruction** (`resetLoopSlotToEmpty`) happens later and only when
  the loader runs for a slot whose payload is absent or invalid. So B04's discriminating state is **a live
  finalizing owner on a slot whose queued restore has no valid payload** -- which also means the
  request-loss half is observable with no SD at all. The fixture named exactly three missing symbols:
  `StorageManager::needsSlotLoad` (live in the `src/StorageManager.cpp` monolith -- **recommended: extract it
  into its own small TU**, a ~25-line predicate over Config, the attempted flag, `SlotLoadSession::isActiveFor`,
  the active/parked job queries and the committed-passes check), plus `CurrentSetStorage::formatLoopSlotPath`
  and `verifySaveFileTokenAtPath` (link `CurrentSetStorage.cpp`). Nothing was committed from the fixture; the
  load suite stays 3/3 and the tree is clean.
- **Load harness increment 3: the load decision is extracted and the SD card is complete.** Three verified
  steps: (1) `StorageManager::needsSlotLoad` moved out of the `StorageManager.cpp` monolith into
  `src/StorageManager/SlotLoadAdmission.cpp` so the load *decision* links without the persistence layer --
  pure predicate, **FLASHMEM**, and the firmware's RAM1 line is byte-identical (`code 392,376 / padding 840`)
  which the post-link budget check confirms; (2) the load suite links `CurrentSetStorage.cpp` and
  `SlotLoadAdmission.cpp`, so `formatLoopSlotPath`/`verifySaveFileTokenAtPath` are real production
  definitions; (3) the in-memory SD card gained `rename`, `FILE_WRITE`, `File::write` and create-or-truncate
  `open` (additive; `test_continuous_capture` still 87/87). **B04 is now one triple of symbols away:**
  `SlotLoadSession::isActiveFor` (`SlotLoadSession.cpp`) and `isActiveLoadLoopJobFor` /
  `isParkedLoadLoopJobFor` (`LoadLoopJob.cpp`) -- so the next increment links those two TUs, re-adds the
  fixture, watches it go red, and only then makes the S2b.5 call-site fix. The fixture was reverted again to
  keep the suite green at 3/3; full native **2,275 / 1 skipped / 0 failures**.
- **B04 is reproducible, and the S2b.5 defect is worse than "a lost request".** The last linkage step landed
  (SlotLoadSession, LoadLoopJob, LoopPendingNoteChange, TrackManagerSlotMatrix; plus two documented seams --
  `HARNESS_ARDUINO_STUB` for a duplicate native `micros()`, and the earlier
  `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`). The fixture shows that for **a slot with committed passes** --
  exactly a finalizing predecessor's state -- the pop's done-test treats the request as already satisfied and
  **silently discards the entry, returning false**, and `queueDeferredLoopSlotRestore` then refuses to take it
  back, so the obligation is **unrecoverable with no signal at the pop**. Re-queueing is inert for committed
  slots; the Loop's destruction is the later consequence, when the loader runs for a slot it was told to load
  and the payload is absent or invalid. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**,
  firmware + per-env budget SUCCESS. **Next:** the bounded S2b.5 call-site fix (peek/pre-check or the forced
  enqueue), then re-point the fixture at the fixed behaviour.
- **B04's live-owner fixture attempted; the goal narrowed and the fix is fully specified.** Two clarifications
  worth keeping: the owning predicate is a live **job**, not committed content (`noLiveOwnerForSlot`
  correctly ignores committed passes, so the fixture must build a real admitted stop job), and that state can
  be built **without** the MIDI/note-edit layer -- open the interval on the Loop directly and admit through
  `Track::beginDeferredOverdubStop` via the existing `TrackCaptureStopTestAccess` friend contract, whereas
  routing through `midiButtonActions` drags in `analyzeEditSessionInteractions` and
  `resolveConstrainedGeometry`. Remaining linkage, in the order it appeared: `StorageLoopIo.cpp` +
  `StorageLoopIoBodyFormatV1.cpp` (they define `releasePersistedLoopSnapshotChunks`, despite
  `PersistedLoopSnapshotRelease.cpp`'s name), then `LoadLoopBudget.cpp`. **The fix is designed and local:**
  guard `LoadLoopJob.cpp`'s four pop sites with `noLiveOwnerForSlot(entry.track, entry.slot)` before the pop
  (peeking first at the two `popNext` sites, which have no filter), leaving the entry queued and returning
  when a live owner holds the slot. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**.
- **`resetLoopSlotToEmpty` extracted -- and it gave RAM1 headroom back.** The destruction the load path
  performs on a slot whose payload is absent or invalid (the function B04 exists to prove is *not* called)
  moved from the `StorageManager.cpp` monolith into `src/StorageManager/SlotReset.cpp` and is now **FLASHMEM**.
  The enforcement caught both halves: the first cut, left without the attribute, **failed the post-link
  budget check** at `code 392,440 / padding 776` (the split had pushed 64 B into ITCM); with `FLASHMEM` the
  line is `code 392,184 / padding **1,032**` -- **+192 B of headroom** against the line this change had
  spent, reported as headroom rather than absorbed, and the floor stays 840 as instructed. A side effect is
  that a suite can now link the destructive function without the monolith. **The live-owner B04 fixture is
  one symbol from linking**: `StorageManagerInternal::storageIoFromFileRead(File&)`, which lives in the
  heavyweight boot-load TUs and would pull much of the persistence layer -- a deliberate stopping point, with
  the fixture's design already recorded. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**.
- **S2b.5's retirement half is blocked by a harness artifact, now named.** The fixture's second half ("after
  retirement, restoration happens exactly once") needs the job drained to retirement, which makes
  `TrackUndo::prepareOverdubSessionEntry` live, which needs `src/TrackUndo.cpp`. Linking it collides with the
  already-linked `MidiButtonActions.cpp`: **both define `restorePlaybackAfterSlotClear`** -- `TrackUndo.cpp` at
  namespace scope, `MidiButtonActions.cpp:118` inside an **anonymous namespace**. Separate translation units
  never collide; an include-based harness concatenates them into one. The obvious seam
  (`HARNESS_OMITS_MIDI_BUTTON_ACTIONS`, left guarded in `track_harness.inc` for the next attempt) then breaks
  differently, because `off_path_deps.h` stubs `ControlSurfaceManager`/`MidiFaderActions` whose **headers**
  arrive only via `MidiButtonActions.cpp`. So the obstacle is the harness's single-TU assumption, not the load
  path. Two routes: give the load suite its own minimal stub surface instead of reusing `off_path_deps.h`, or
  split the shared harness into "button headers" vs "button TU". Load suite **5/5**, full native
  **2,277 / 1 skipped / 0 failures**, firmware + per-env budget SUCCESS.
- **S4f.4 transition 3 was a defect, and it is fixed.** Writing the load/replacement fixture showed the drive
  sent *every* refusal to the park -- including a request whose interval is gone because a load or replacement
  left the same slot number. Parking waits for a canonical reservation unrelated to the failure, so the
  obligation was retained forever and `hasStopInFlight()` stayed true, blocking every later start, switch and
  release for that track. The drive now recognises a stale interval **before any admission attempt**, reports
  it, consumes the obligation exactly once and admits nothing. Fixture
  `test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound` is discriminating. S4f.4 transitions
  **1, 3 and 4** are green natively; transition 2 (seek/reset/MIDI Start) has no native entry because those
  `ClockManager` entries are harness stubs. Full native **2,279 / 1 skipped / 0 failures**; firmware + per-env
  budget SUCCESS at padding 1,096.
- **S4f.4's four paused-worker transitions are all green natively.** Transition 2 (seek/reset/MIDI Start) was
  the last: `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution` latches an accepted stop at
  tick 96, moves the tick domain before execution, drives, and asserts the admitted boundary is **not** the
  stored tick and lies in the new domain -- discriminating, since a drive that used `request.phaseTick` would
  make the two equal. **Coverage limit stated in the fixture:** the `ClockManager` seek/reset/MIDI-Start entries
  are harness stubs, so the domain is moved with the controlled clock; that proves the decision, not the
  production entry that moves it (device territory). Full native **2,280 / 1 skipped / 0 failures**; firmware +
  per-env budget SUCCESS at padding 1,096.
- **Still open:** S1.1's per-owner byte census, S1.3's device slope to the current failure point, S1.4's
  undo/canonical-pin baseline, S1.5's smallest refusal N, and the whole S1 device gate — **no hardware run
  in this session**. Next: the remaining D0.2 controls, then S2b.0/S2b.1, which is expected to turn the
  three red cases green and to retire the `WORKING_SET_RED` gate.

**2026-09-23 — chat reference readability.** Agent replies now pair every stage/task/gate/decision ID
with a short descriptive name, including repeats and link labels. Canonical rule:
[NAMING — short names beside IDs](../Authority/NAMING.md#short-names-beside-ids-in-chat), linked from
AGENTS.md and the always-applied naming rule. Presentation only; no runtime or task-status change.


**2026-09-23 — deterministic implementation workflow strengthened (DEC-080).**
[Implementation readiness](../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
now requires a proportional execution packet for every future slice, including steps of existing changes:
fixed transition oracle, controlled production-path fixture, repeatable recipe and actual evidence before
checkoff. AGENTS.md, Cursor rule, OpenSpec config and agent roles share that authority.
The current plan's [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
adds P0–P8 and S2b cases B01–B07; D0 tasks prepare controls/fixtures before behavior changes. Fixtures are
**specified only**, not implemented or run. No firmware or hardware action; no implementation task checked off.


**2026-09-23 — runtime-working-set-ownership implementation handoff corrected (DEC-079).**
The five targeted review findings are resolved in the active OpenSpec's
[IMPLEMENTATION_CONTRACT.md](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md)
and task/spec text: self-contained S2b with pulled-forward Stop/liveness prerequisites, per-holder
cardinality budget, job-retaining regeneration, S4c contract-only with phase F explicitly uncreated,
and stop/switch/start composition with explicit supersession. S0/S1 → S2b remains the first slice;
its gate uses current resident content and a synthetic descriptor fixture, not a 60-long-slot library.
Current whole-loop playback roots stay accounted until F. No firmware, build, upload or device run;
all implementation checkboxes remain open. This supersedes earlier handoff wording, not device evidence.


**2026-09-19 implementation readiness is now a gate, and the OpenSpec config no longer restates volatile state
(DEC-077).** Workflow/documentation only — **no firmware change**. New:
[`docs/Authority/IMPLEMENTATION_READINESS.md`](../Authority/IMPLEMENTATION_READINESS.md) (G0 evidence → G1
architecture → G2 contract → G3 apply, with the STOP conditions for Apply), three roles in
[`docs/Agents/`](../Agents/README.md) (`implementation-investigator`, `implementation-architect`,
`implementation-red-team`), and matching loadable skills in `.agents/skills/`. `openspec/config.yaml`
`rules.design` / `rules.tasks` now require G0–G2 and forbid architecture-discovery tasks; its dated
milestone narrative is replaced by a pointer here and to
[`CURRENT_WORK.md`](CURRENT_WORK.md).

Two things this explicitly is **not**: it is not a second stop gate (the
[formal trigger list](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) still decides reassessment, and the
one-per-session limit is unchanged — see DEC-077 § 1), and it does not change ownership or bounds. The
structural constants stay in the config because they are current: `NUM_TRACKS = 8`,
`MAX_LOOPS_PER_TRACK = 8` (`include/Globals.h`); what was stale was the milestone narrative, not the counts.

Filed while writing it: `ClockManager::setBpm(uint16_t)` (`src/ClockManager.cpp`) assigns `bpm` and reprograms
the tick timer with **no clamp**, while `setBpmFloat` clamps 20–300 and both the boot-load and legacy-validation
paths require 20–300. Two entry paths to one owner state with different invariants; not fixed here.

**2026-09-19 `runtime-working-set-ownership` ran G0 → G1 → G2, and the G2 red team returned BLOCKED — design
input landed, no firmware changed.** G0 (six read-only traces) verified the law violation (per-slot
`runtimeBySlot_` never released; a sounding slot requires a resident whole-loop root; a busy-owner stop is
**dropped** and must be re-pressed; undo pins snapshot residency with no demotion; no residency-vs-library test
exists). G1 produced the architecture; the red team then falsified **eleven reachable paths** — the worst being
that `finalizeCaptureAndSelectSlot` ignores a stop denial and moves `activeLoopIndex` anyway, that boot reserves
8 pool chunks (41,056 B) **per committed slot** (480/512 at the 60-slot target, reintroducing the RC-MEM-5
state), and that the contract's own fields add ~1.4 KB of DTCM against **872 B** of usable padding.

Landed as design input: **DEC-078** (accepted — every interaction accepted; finalization is **per interval**;
amends DEC-067 and DEC-076 point 2, withdrawing DEC-076's "finalization is a queue" bullet);
`openspec/changes/runtime-working-set-ownership/design.md` **Revision 18** (findings, latch, cardinality and
liveness, invariants 32–36, placement, cold-undo pinning, caps); `tasks.md` restructured (**S2.9 extracted** to
the sibling change `windowed-playback-generation`, new **S2b** boot/load stage, per-slot liveness, slot-
parameterised acceptance); and `CURRENT_WORK.md` § Now implementing's stale 32-lease envelope corrected. The
change stays **Design (S0 pending)**; the revised contract must be re-attacked before any task is executed.

**2026-09-18 the overdub live-capture one-frame paint flicker is CLOSED WITHOUT A FIX and folded into the
memory-pressure work (user decision).** No firmware change was made and no GitHub Bug was opened. Two
measurements from the reference capture
([`captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log`](../../captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log))
made the fold the honest close rather than a punt, and correct one assumption in the OPEN plan:

- **The dip is real, already measured, and in the compose - not the draw.** The on-change `DISP` family is
  emitted at `Transfer` from `resolvedPaintNotes()`, the same vector `Draw` read, so a lost row shows up as
  a decrease. The whole capture has exactly two decreases: the `23.001 s` clear (`RECORDING`, 32 -> 0) and
  **`49.972 s`: `OVERDUBBING`, `frameNotes` 75 -> 74, restored to 75 at `50.019 s`** - one painted frame
  with one row fewer, right after the `49.946 s` MIDI event. The OPEN plan asserted the existing telemetry
  could not catch a one-frame dip (true of `DFRAME` at every 30th frame) and made denser compose
  instrumentation its first step; the on-change `DISP` family already catches it, so that step is
  withdrawn.
- **The paint's own source state cannot advance during the window.** The same capture carries **52**
  `#MEM_DENIED,op=snapshot_reserve,reason=heap_low,need=40960B,heap_free=68KB,pool_refused=0/0B` rows,
  continuously reported (1 s report window) from `27.64 s` to `79.06 s` - across the entire overdub.
  `kSnapshotPublicationReserveBytes` (40,960) plus the 32 KB `kOperationHeapFloorBytes` needs 73,728 B
  against 68 KB free. DEC-075 makes a denial deliberately non-fatal (previous root stays selected), so the
  published root the paint composes from is held at the arm's heap state.

Handed to `runtime-working-set-ownership` (now the ACTIVE scope in
[`CURRENT_WORK.md`](CURRENT_WORK.md) § Now implementing), with the invariant *a note present in the
composed paint set before and after a note-on/note-off must be painted in every frame in between* and a
cheap re-measure recipe: after the lease/publication repair, scan the same fixture for `DISP` **decreases**
inside the overdub window (no new firmware telemetry required); if the dip survives a healthy gate, re-open
[`overdub_live_capture_one_frame_paint_flicker_bugfix.md`](../Plans/overdub_live_capture_one_frame_paint_flicker_bugfix.md)
as a paint-path RC owning `DisplayManager::resolveDisplayNotesByDrawSource`. The tidy-up exoneration
(P1-P6 behavior-preserving; base `4aacd18e` reproduced the blink) stands, so `runtime-code-tidyup` remains
out of scope.

**2026-09-18 `DIAG,droid_queue` is off in the default capture env, and that also removed the 4 KB RAM1 snap.**
`SESSION_CAPTURE_DROID_QUEUE=0` now sits in `[env:teensy41-capture-serial]`, with
`teensy41-capture-serial-investigation-diag` setting `=1` as the explicit debug gate and
`teensy41-capture-serial-no-grain` keeping its own explicit `=0` (its contract is the acceptance contract). The family
fires on every enqueue/coalesce of the DROID control-surface feedback path, was 5,636 rows (~310 KB) on the 2026-09-18
eight-bar record+overdub fixture, and with the per-slice `PERS,work` trace shed the 1 MiB ring, evicting the
`snapshot_unit`/`ST` rows the base-run oracle is read for; the counters and the `logger.log` overflow warning are
unaffected. Two measured effects. (1) Ring budget: with only `droid_queue` off, the fixture reached **17,412** `#CAP`
rows against 3,287 in the shed runs and got past the phase-confirmation abort, but `DIAG,lcr` was then 12,664 rows /
~966 KB and still overflowed the ring -- so the acceptance env (which also drops `DIAG,stop_grain` and `DIAG,lcr,*`)
is still required; there the same fixture logs **2,410 rows with no `capture_ring_overflow`**. (2) RAM1: the emitter's
own format string was the RAM1 `.rodata` literal that triggered the 4 KB `.data`/`.bss` alignment snap chased in the
entry below -- with the family dropped, the build reads `variables 91,968` / `code 392,152` / `padding 1,064` /
`free for local variables` **39,104** instead of 96,064 / 35,008. So the drain fix that had to be reverted at 2,240
now lands with the largest allowance this tree has had. This is the documented "a new capture format string can cost
4 KB" hazard in [`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc), and it makes
dropping a high-volume *emitter* both a ring-budget and a RAM1 lever. The base preset's remaining misses on this
fixture are its setup-oracle class (a 2x-tempo run: 780 ticks/s against 384 nominal, sealing a 17-bar loop for
`--record-bars 8`, the same artifact already recorded for the DEC-074 session), not content.

**2026-09-18 the blocking `saveState` drain is fixed and device-verified, and the RAM1 tree gained a DTCM bank
(`immutable-loop-snapshot-passes` 5.7.6; `12fdf38d` -> reverted `d3290020` -> re-landed `0a036c63` on linker change
`ea9918ac`).** The RC recorded below this entry was a **monotonic persistence work-entry table**: no path ever returns
an entry to `NotScheduled`, and `LoopPersist` is keyed by `LoopId`, which is fresh for every new loop, so each
clear/record cycle permanently consumes one of the 256 slots. Once exhausted, `admitWork` refuses **every** key --
`FinalizeWorkspace` included -- while `currentWorkspaceSave.pending` stays set, so `hasPersistenceWorkPending()` is
true on `pending` alone with nothing to step, `stepPersistenceWorkItem` returns success on that state, and the
blocking drain spins its stuck budget and fails (`Persistence drain stuck after 33 iterations (max 33, expected ~1
slice steps, ~0 SD bytes)`), refusing CLEAR for the rest of that device generation. The state was pinned by
arithmetic before any code change (`SyncDrainBudget.cpp:66` sets `finalizeSliceSteps` only when `pending &&
queueDepth == 0 && writingWorkItemCount == 0 && !itemActive`, so `expectedSliceSteps = 1` with 0 SD bytes means
`pending` alone; `maxSliceSteps = 1*3/2 + 32 = 33` and `maxStuckIterations = min(64, 33) = 33` match the log exactly).
Fix: `admitWork` reclaims a `Persisted` tombstone when the table is full (lossless: nothing reads `workState`,
`visitScheduledWorkItems` skips tombstones, and a fresh admission reproduces the re-queue shortcut), never touching
`Queued`/`Writing`; `maybeAdmitFinalizeWorkspaceAfterDrain` reports whether work is available; the drain fails fast
with `SyncDrainFailureReason::AdmissionRefused` and a PROGMEM diagnostic. Two native fixtures fail on the pre-fix
behaviour (verified by disabling the reclaim) and pass after; suite 15/15, full native **2252 / 1 skipped / 0
failures**. **The fix first had to be reverted**: it costs 4 KB of DTCM, and at 13 ITCM banks that left `locals free`
2,240, which hung the boot load silently (`BOOT,scan,start`, no crash record). Controls proved it was the 4 KB and not
the logic -- the pre-fix firmware booted and streamed 1136 rows with the same SD state, and a **semantically neutral
edit** to either touched TU reproduced the same 96,064 / 2,240 and the same hang. With no movable DTCM left, the
lever was ITCM: `linker/imxrt1062_t41_lcr.ld` now routes ten cold TUs (boot/load, SD-bundle serialization,
telemetry, note-edit undo, set-browser read -- none in the MIDI tick, capture append or playback serving) to FLASH,
taking `code + padding` 425,984 -> 393,216 = **13 -> 12 ITCM banks** and `free for local variables` 6,336 -> 39,104
(with the fix re-landed, **35,008**). Device (`captures/drain-fix-03`, git `0a036c63`): clear precondition
**satisfied**, 0 `Persistence drain stuck`, 0 `admission_refused`, 0 `FATAL`, 0 `#MEM_EXT_REFUSED`, 3,287 `#CAP` rows
and **12 `PERS,work`** rows where the wedged runs had 0; boot probe 1,023 rows, `BOOT,load,ok`. **Caveat:** `padding`
is now 1,256 B, so the next ~1.2 KB of new ITCM code crosses back to 13 banks (~7 KB allowance); taking the next bank
means moving another ~32 KB of cold code. Method and failure signature recorded in
[`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc).

**2026-09-18 the snapshot publication boundary now admits memory per construction turn and parks on allocation failure
(DEC-075, `immutable-loop-snapshot-passes` 5.9).** The reset recorded below is fixed at its boundary rather than by
shrinking the footprint. **RC-PUB-1:** `Loop::stepPendingSnapshotPublish` admits every turn that could start, step or
freeze a candidate, through `OperationKind::SnapshotReserve` -- which existed with an 8 KB margin and had **zero call
sites** -- with `kSnapshotPublicationReserveBytes = kSnapshotSlabBytes + 8 KB`, on top of the gate's 32 KB heap floor
and its pool-refusal recency window. A denial writes nothing: the publication stays pending and the previous root
stays selected, which is the state DEC-074's `hasSettledPublishedSnapshotForSave` already handles. Publishing an
already-frozen candidate is deliberately not gated (it allocates no new content memory). The gate's only per-turn
caller now bounds its repeat `#MEM_DENIED` *report* to one per second per shape while the decision stays exact on
every call, the way `recordPoolRefusal` bounds its own storm. **RC-PUB-2:** `noteFragmentIncorporationFailure` parked
only `MembershipCapacity`, so an `Allocation` failure rebuilt the fragment every turn -- copying its
`LoopSnapshotChangeSet` onto the internal heap and retrying the slab -- and a freeze failure was not counted at all;
`Allocation` now parks transiently and the freeze branch counts through the same path. The park is released only
after admission has denied while parked and then admitted again, so the same admission state that let the fragment
fail cannot re-arm it. `MembershipCapacity` keeps its permanent park and `Injected` keeps retrying, so the DEC-047
transaction fixture is unaffected. **RC-PUB-3 corrected:** routing `SnapshotSlabAllocator::allocate`'s `abort()` to
the non-fatal path is neither expressible (device `-fno-exceptions` leaves `allocate_shared` no failure channel, and
a custom deleter would put a control block per node on the internal heap) nor reachable
(`allocateSnapshotShared` pre-reserves `128 + align(sizeof(Type))`, so pressure surfaces as the already-non-fatal
`snapshotSlabOpen` failure); the abort stays as the invariant check, tagged `site=slab_allocate` against the
reachable `site=slab_open`, documented in place. The allocator that actually aborted is the shared
`ExternalMemoryFirstAllocator`, whose abort-instead-of-deny semantics remain the wider defect recorded in
[CURRENT_WORK.md](CURRENT_WORK.md); this fix stops the publication from driving it there. Evidence:
`test_publication_turn_waits_for_admitted_headroom` (a denied turn leaves the wrap journal, the published root, its
revision, the in-flight-construction flag and the failure count all unchanged, and the same publication then
completes) and `test_allocation_failure_parks_publication_until_admission_recovers` (two injected `Allocation`
failures park, an admitted turn makes no attempt, a denial records the pressure, the next admission releases it and
the root becomes current), both in `test_loop_take_survival`; full native **2249 / 1 skipped / 0 failures**;
`teensy41-capture-serial` SUCCESS with RAM1 `variables 91,968 / code 425,560 / padding 424 / locals free 6,336` --
byte-identical to DEC-074, i.e. no FlexRAM bank cost. **Device evidence 2026-09-18 (parked path,
[`rcpub-gate-05`](../../captures/rcpub-gate-05/session_20260918_114025.log), git `dc9a1107` clean, hex `7f6bd8e3…`):
0 `FATAL`, 0 `#MEM_EXT_REFUSED`, device alive, and 49 `#MEM_DENIED` -- every one `op=snapshot_reserve`,
`reason=heap_low`, `need=40960B`, `heap_free=68KB`, `pool_refused=0/0B` -- while `snapshot_unit`,
`snapshot_fragment`, `snapshot_slab_fail` and `snapshot_alloc` are all 0: the boundary parked with nothing written,
and `heap_free` held at 68 KB (min = median = max over 73 samples) with `snap` unchanged at 2214 KB and pool `live`
6507 -> 7881 KB then flat.** The crash log is the counterfactual (same boundary, no gate: heap 108 -> 0 KB, `snap`
2409 -> 2543 KB, then `abort()`), and the denial report is confirmed rate-bounded on device (~1/s across the 50 s
window). **Owed:** the wrap case specifically (that run's denials landed during `RECORDING` and the harness aborted
before the wrap on a 1 MiB `#CAP` ring shed, `Result: INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`) and the
resume-and-complete path once the heap recovers (the run ended while still denied, so resume is native-proven only).
`--record-bars` accepts only `[1, 2, 4, 8]` while the crashing loop was 24 bars (`TICKS_PER_BAR = 768`), so a
faithful repro also needs that cap raised. `kSnapshotPublicationReserveBytes` remains the tunable: the margin requires
72 KB of internal heap, the DEC-073 RC1 control settles at 104 KB, and this run was denied at 68 KB -- the intended
discriminator.

**2026-09-18 RC recorded: the snapshot publication boundary has no memory admission, so a full PSRAM pool resets the
device instead of parking the publication (`immutable-loop-snapshot-passes` 5.9, RC-PUB-1..3).** User report: the
stress run crashed a few bars past the overdub wrap, overdubbing an existing overdub. Device
[`session_20260918_101615`](../../captures/session_20260918_101615.log) is not a fault -- there is no crash record
(`boot_reset,crash=0` on the next boot, and `abort()` raises no `CrashReport`); the reset follows
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and
`#CAPTURE_RECONNECT`. `Overdub wrap committed @ tick 18432` at 114.519 s, and then for six seconds the pool's `<=32K`
class gains **one 32768-byte snapshot slab per second** (96 -> 102 blocks) while the internal heap falls
**108 -> 72 -> 36 -> 0 KB**; the pool then refuses `bytes=32768` (119.465 s, the exact `snapshotSlabOpen` request) and
`bytes=8224`, and `ExternalMemoryFirstAllocator::allocate` calls `abort()`. The overdub session itself did not leak:
pool live is pinned at 8201-8203 KB for the whole 45 s before the wrap against the boot's reported `pool=8182 KB`,
because a 12-bar record sits on top of the loaded workspace's reference root (~1.5-1.7 MB of slabs installed by the
boot load) plus the prewarmed view sets (~0.9 MB, 715 KB of it `TagViewPrewarm`). The control is DEC-073's own RC1
device run ([`rc1-coalescing-device`](../../captures/rc1-coalescing-device/session_20260918_100646.log)), which shows
the same ~8-slab post-wrap burst and then settles with 0 `FATAL` -- so the ~256 KB cost is by design and the defect is
that this boundary neither admits nor parks. No persistence ran in the crashing boot (15 `PERS` lines, all boot-time,
none after the wrap; the deferred save needs an overdub stop that never came), so the in-tree RC2/DEC-074 gate is not
involved. Three RCs are recorded in the owning change and cross-referenced from
`successor-capture-reentry-unguarding` 5.9: **RC-PUB-1** wire the dead `OperationKind::SnapshotReserve` admission into
the publication start (point-of-use probe; on denial start nothing and leave the work pending with the previous root
selected); **RC-PUB-2** park `LoopSnapshotFailureReason::Allocation` like `MembershipCapacity` instead of retrying the
fragment forever (each retry copies the fragment's `LoopSnapshotChangeSet` onto the internal heap, and the
freeze-failure branch is not even counted); **RC-PUB-3** remove `SnapshotSlabAllocator`'s fatal `abort()` backstop.
The steady-state footprint half stays with the memory-pressure / DEC-065 track ("idle occupancy leaves no headroom").
No code changed in this entry.

**2026-09-18 RC1 LANDED: a published snapshot root carries its committed-pass coverage, and "current" can no longer
lie (DEC-073, `successor-capture-reentry-unguarding` 5.9).** The post-reboot overdub loss was the publisher stamping a
candidate built for an older committed-pass state with the *current* `playbackRevision`: the session-exit publish and
the undo/redo captured-root selection both did it, so `publishedSnapshotIsCurrent()` was exactly
`box.revision == playbackRevision` and the LSV1 save could gather an undo-shaped 48-row root while `PassesResolve` was
57. Fix: `PublishedSnapshotBox`/`PendingSnapshotPublishBox` carry `coveredPassRevision`;
`publishedSnapshotIsCurrent()` is `coveredPassRevision == committedPassRevision()` (the enumerable pass/companion
inventory revision, an audited alias of `playbackRevision` today); the box sets its watermark only where it actually
enumerates (`queueSessionExitSnapshot`, both `queueAllCommittedPassesInto` callers, `enqueueLiveOverdubPasses` when it
queues, wrap journals via their own `targetRevision`), the session-exit publish passes that watermark, and undo/redo
coverage travels with the retained root (`UndoEntry::before/afterPublishedCoverage`, the job- and session-owned
before-roots, `TrackUndo::selectPublishedSnapshotForEntry(loop, ref, coverage)`). No pending-request clause sits in
the predicate: a later request can only invalidate a root by moving the inventory, which the comparison already reads.
Evidence: the two DEC-073 reproducers in `test_loop_take_survival`
(`test_coalesced_snapshot_request_before_construction_is_not_satisfied_early`,
`test_coalesced_snapshot_request_mid_construction_is_not_satisfied_early`) fail on the pre-fix tree on the same
clause -- a root captured before the live committed-pass state claiming current -- and pass after; suite 137/137,
full native **2246 / 1 skipped / 0 failures**, `teensy41-capture-serial` SUCCESS (RAM1 variables 91,968 / code
425,560 / padding 424 / locals free 6,336; FLASH code 1,007,080). **Device-verified 2026-09-18:** DEC-071 multi-pass
shape with the recipe's undo/redo on `teensy41-capture-serial-no-grain` (hex
`f2f1d8bdfc85b30727fc667e1c548066d1debf4854fa5acae8cdae645c735f1e`, tree `d9eb419a` + this change) --
`captures/rc1-coalescing-device` ran two overdub sessions with undo=1/redo=1 and the post-redo republish
(`q=0,a=4` at 95.474 s) is the root the LSV1 `tail_gather_begin` at 99.640 s gathers; the base report's only issue
is `persistence_stage_missing:result` because the now-complete 600-row tail was still writing when the run closed
serial (the device's own log has `PERS,result,…ok` at 111.14 s). `captures/rc1-coalescing-reboot-playback` is a
fresh boot (`boot_reset,crash=0` at 3.0 s) and reads **`result: PASS`, `storage_matches_effective: true`,
384 + 154 + 38 + 24 = 600 effective, all five cycles `inventory_equal: true` / `exact_order: true`, 0 missing,
0 extra, `max_bar_refs=84`, 0 `RING,overflow`** -- the 24-row second-overdub pass that `content-verify-58` lost is
audible after reboot. `captures/rc1-coalescing-device-playback` (live) is also 5/5 clean. **RC2 landed (DEC-074):** the
LSV1 body admission now requires a *settled* root (`hasSettledPublishedSnapshotForSave()`:
coverage-current **and** no publication pending), the per-loop format is resolved once at `Header` and pinned
(`loopWriteUsesLsv1`), and an unsettled loop writes the passes-authoritative **legacy** body -- LSV1's readers reject
a missing `LoopSnapshot` section, so LSV1 cannot express a rootless body. A bounded settle re-admission
(`kLoopPersistSettleDeferralBound = 128`, nothing written while waiting) lets a transient pending republish finish
before falling back. Native: `test_storage_loop_io` 47/47 (two new fixtures, one red on the pre-RC2 predicate); full
native green; `teensy41-capture-serial` SUCCESS with RAM1 variables 91,968 / padding 424 / **locals free 6,336**
(the first implementation's new telemetry tag cost 4 KB of DTCM allowance via `ALIGN(4096)` churn; dropping it
restored the figure). **Device read closed 2026-09-18** (firmware `86fd8f07…`): the save took the settled LSV1
path (`tail_gather_begin`, `LoopSnapshotTail` 1389 steps, `q=0` publications), 0 `RING,overflow`, 0 `FATAL`, and
post-reboot `storage_matches_effective: true`. The verify's 4 unexplained wrap rows are a setup artifact -- that
session recorded at 240 BPM instead of 120, so its 8-bar request became a 17-bar
`record_loop_length_mismatch`; the intended shape's post-reboot parity is proven clean under DEC-073. The two
earlier attempts' ring overflow is fixed by `SESSION_CAPTURE_LCR=0` in the no-grain acceptance env. Remaining:
6.1/6.2/6.3, and the owning change's RC-PUB-1..3 publication-boundary work (`immutable-loop-snapshot-passes` 5.9).

**2026-09-18 RC recorded for the post-reboot overdub loss (`successor-capture-reentry-unguarding` 5.9).** User report:
after reboot the second overdub isn't loaded. Device `captures/gus-sanity-01` + `captures/reboot-01-playback`: the
committed passes are complete (effective 142 rows; both overdub passes present after load), but the deferred save's
LSV1 tail gathered a coalesced published root that encodes the **undo** state — the loaded body root resolves to 48
display rows vs 57 before the reboot, and playback misses the 24-39 overdub range. The save requires neither
`publishedSnapshotIsCurrent()` nor `!hasPendingSnapshotPublish()`, and the loader installs the body root as current
with no reconcile (the boot hydration lane is gated off). The session-exit publish also stamps the candidate with
the **current** `playbackRevision` regardless of the committed-pass state it was built from, so "current" cannot
prove content coverage. **Fix call: (b) publisher coalescing/coverage correctness first, then (a) a persistence
safety gate; load-time rebuilds are rejected by the owner.** The RC in 5.9 pins the invariant (queue consumption
and candidate coverage advance together; carry a `coveredPassRevision`) and the acceptance (root == `PassesResolve`
immediately before save admission). The stale uncommitted edit that had marked 5.9 closed / ticked 6.1 was reverted.

**2026-09-18 the undo stack now keeps its protected publication capacity (`successor-capture-reentry-unguarding` 8.3).**
User report: the undo stayed at 2 instead of 3 after two overdub sessions, and the next undo fell through to the
first overdub. `TrackUndo::eraseUndoEntriesForSlot` move-assigns a filtered rebuild, which took the smaller
local buffer and dropped the `prewarmProtectedUndo` reserve; `TrackUndoPublication::begin` then rejected at
`cursor >= capacity` and the stop silently committed without a GUS entry (device diagnostic before the fix:
`cap=4`). Fix `433fea68` re-reserves `kUndoStackProtectedCapacity` (constant moved to `GlobalUndoStack.h`);
`DIAG,gus,publish_begin` / `DIAG,gus,drain` added. New native fixture fails without the fix; full native
**2,244 / 1 skipped / 0 failures**, RAM1 locals 6,400. Device gate via the skill's canonical `base` preset,
`captures/gus-sanity-01`, **`Result: PASS`**: `MIDI: Undo (entries=3)`, `publish_begin cap=32` for both
sessions, `passIds_n=2,src=deferred` per entry, `DISP across undo/redo: 56 → 48 → 57`.

**2026-09-18 the deferred stop now marks the committed pass's display bars (`successor-capture-reentry-unguarding`
8.2).** User report: the second overdub vanished from the OLED after its stop and came back at the loop wrap. The
deferred terminal commit only called `notifyCommittedContentChanged` → `markPassDerivedStale`, which leaves
`dirtyBars` empty after a `slice_clean`, so `rebuildVisualCacheIdleSlice` cleared the stale flag as
`slice_nodirty` without rebuilding and the cache kept the pre-commit notes; the wrap's
`markAffectedDisplayCacheRanges` was what brought them back (`gus-undo-04`: 176 → 144 at 74.3 s). Fix
(`a91cb719`): mark the committed pass + prepared companion ids in every deferred terminal commit. New native
fixture fails without it; full native **2,242 / 1 skipped / 0 failures**, RAM1 locals 6,400 unchanged. Device
`captures/gus-undo-06`: frame holds 176 from the second stop through the undo, `before=176 after_undo=144
after_redo=176`. The undo/redo display clause also bounds the post-redo sample at the next undo for
`undo_redo_cycles > 1`; `scripts/test_capture_transitions.py` 48/48.

**2026-09-18 the GUS now records every pass of an overdub session (`successor-capture-reentry-unguarding` 8.1, DEC-068
follow-up).** The reported defect: one undo removed only the last wrapped pass, not the whole overdub. Cause: the
deferred stop admission swaps the interval's wrap identities out of the shared loop into its own session-exit
receipt (DEC-068), but the deferred undo collection read the live session lists at its later `UndoCollection`
phase, when they are empty — so the entry carried only the final pass. Fix: the deferred Pass/Companions phases
read the interval's own session-exit receipt (`SessionUndoCollection` receipt pointers + a bound
`receiptIdentity` that replaces the live generation guard; `beginCollectOverdubStopUndoPasses` takes the receipt
ring; `stepDeferredUndoCollection` passes `job.admission.sessionExitReceiptRing`). The deferred route also emits
the `#CAP,DIAG,gus` row now (`src=deferred`). New native fixture
`test_deferred_stop_undo_entry_carries_every_session_pass` fails without the fix (`Expected 2 Was 0`);
`test_continuous_capture` 75/75, full native **2,241 / 1 skipped / 0 failures**, RAM1 locals 6,400 unchanged.
**Device gate closed:** `captures/gus-undo-01` (managed `base`, firmware from `7a4c8bfc`) logs
`#CAP,66172706,DIAG,gus,slot,kind=1,passId=3,passIds_n=2,src=deferred` (one entry, both passes) and
`DISP across undo/redo: before=189 after_undo=124 after_redo=188` — one undo removes the whole overdub, against the
partial `194 -> 179 -> 193` before the fix. The run's base report is `FAIL` only on the known record
stop-quantisation grid artifact and the redo re-verification span window, not the GUS entry; a second attempt
(`gus-undo-02`) was `INCONCLUSIVE_CAPTURE` (setup).
**Follow-up fixed in `c380c030`:** the deferred admission took `captureSessionExitTail` before the checks that
can still deny it, so a denial sent the stop to the synchronous fallback with an already-emptied live session --
one final pass in the entry. The tail capture now runs after admission is committed. Device `captures/gus-undo-03`
(firmware `7708d605…`, git clean) is a clean **`Result: PASS`**: **`MIDI: Undo (entries=2)`**,
`DIAG,gus,…passIds_n=2,src=deferred`, `DISP across undo/redo: 194 -> 128 -> 192`, `stored overdub span 64/63
count_ok/span_ok/bars_ok`, 0 `RING,overflow`.

**2026-09-18 the 5.8 undo/redo fix's display half LANDED (`immutable-loop-snapshot-passes` 5.8a).** After 5.8
made playback stop emitting a disabled pass, the OLED still painted the undone overdub: the sealed capture
handoff (`retainCapturePreviewAtSeal`) is released only when `sealedCapturePreviewIsCovered()` sees each row in
the committed `visualCache`, so disabling the pass on undo removed the rows from the committed set and coverage
could never hold again; `applyDrawSourceOverlays` then unioned the stale rows into every frame. Device
telemetry (`content-verify-65`): `DFRAME` 194 before `Undo` → **193** after, `DISP frame=193` against
`visual=180` for the whole window, redo unchanged; the extra 13 rows are exactly the sealed handoff
(`194 = 181 + 13`, `193 = 180 + 13`). `resetPassTimeline` already cleared the handoff for this hazard; the fix
makes `Loop::setCapturePassState` do the same when a pass becomes `Disabled`. New native fixture
`test_disabling_a_capture_pass_releases_the_sealed_draw_source_handoff` fails without the fix; full native
**2,240 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 locals 6,400 unchanged. The base
runner also gained `_verify_undo_redo_display`, which rejects a frame that keeps the undone overdub (it flags the
real buggy log `194 -> 193 -> 193`; `scripts/test_capture_transitions.py` 46/46). **Device gate CLOSED:**
`captures/display-undo-03` (managed `base`, git `9eda0984` clean, firmware `47e05029…`, DEC-071 shape + recipe
undo/redo) is **`Result: PASS`** with `undo=1 redo=1`, 0 `RING,overflow`, `overflow_free=True`, persistence
`result=ok`, display `overdub_ok/transport_ok=True`, and **`DISP across undo/redo: before=194 after_undo=179
after_redo=193`** — the frame drops the overdub on `Undo` and restores it on `Redo`.

**2026-09-18 successor 5.9 + 5.11 CLOSED and the defect fixed (`immutable-loop-snapshot-passes` 5.8).** The
user's report — the overdub wrap's second pass shown on the display but not audible — had two independent
causes, both leaving the published root not representing the committed passes: (1) `TrackUndo::
applyPublishedSnapshotSelect` was a no-op when the asynchronous session-exit after-attach had not landed, so a
redo kept the pre-undo root and the entry's pass restore made it stale (live-capture telemetry: window 384 → 576
→ 384 after `Undo` → still 384 after `Redo` against 576 effective); and (2) `Track::processDeferredIdleMaintenance`
admitted snapshot idle work only for `liveRecordPastFirstBar || inFlightConstruction || hasWrapJournal`, so a
publication queued with none of those never started (the plain stop → play-again case). Fix (`31933183`):
`Loop::requestCommittedPassesRepublish()` republishes every active committed pass on the normal lane without
clearing the live root, called after both undo and redo of a record/overdub entry; the eligibility clause also
admits `pendingSnapshotSessionExitQueued()`. Device gate: `content-verify-65` clean base PASS and
`content-verify-65-playback` **`result: PASS`** with `storage_matches_effective: true` (384 + 157 + 35 == 576),
**all five cycles inventory-equal, exact order, 0 missing, 0 extra**, `max_bar_refs: 72`; same run 0
`companion path stalled`, 0 `RING,overflow`, 0 `FATAL`, 0 `MEM_DENIED`, save `ok`; firmware `f82ee9b9…`, git
`31933183` clean. Full native 2,239 / 1 skipped / 0 failures; RAM1 locals 6,400. Owed: the native redo-requeue
fixture and a load-time reconcile for already-saved stale roots (the fix applies to new activity).

**2026-09-18 successor 5.9 device run FAILS: a committed overdub pass never reaches the MIDI output
(OpenSpec `successor-capture-reentry-unguarding`).** The DEC-071 multi-pass shape was created on the
flashed tree (`captures/content-verify-58`, git `4fde5c03` clean, firmware `d298f9ea…`, `teensy41-capture-serial`): the base run
is a clean **PASS** — 9-bar seal for the 8-bar request, `ST PLAYING->OVERDUBBING: 2`,
`OVERDUBBING->PLAYING: 2`, `stored overdub span count=64/63 count_ok=True`, display `overdub_ok=True
transport_ok=True`, `persistence handoff result=ok`, undo=1 redo=1, `overflow_free=True` — so four of
5.9's five checks hold in one clean run. The fifth fails:
`captures/content-verify-58-playback/content-review.json` reports `storage_matches_effective: true`
(record 384 + overdub 157 + 35 + 24 = effective 600) with **every cycle `inventory_equal: false`,
`rows=572`, 28 missing (27 unexplained + 1 residual)**. The loss is deterministic and device-side: the
**whole second-overdub pass `overdub,2`** (24 rows: NoteOn/NoteOff notes 12–19 at ticks 4975–6503, CC74
0–63) and **3 rows of the first overdub's wrap continuation** (`(144,4,28,98)`@6911, `(128,4,28,0)`@95,
`(176,4,74,84)`) are in `passes` and in `effective` but never emitted. This is the same class as DEC-071 —
a committed pass missing from the playback source — with a different pass; DEC-071's own pre-wrap pass is
audible here. The same 24-row loss reproduced on `content-verify-53-playback`. **5.9 and 5.11 stay open**;
the next step is a fix plus a re-verification, not a tick.

**Root cause localised 2026-09-18 (two independent device resets): the persisted representation can lag
the persisted passes, and playback follows it.** `content-verify-59` (acceptance build, single-overdub
DEC-071 shape) is a clean base PASS, yet playback emits only the record pass: `raw_counts` record 384 +
overdub 157 + 35 = effective 576, `rows=384`, **192 missing every cycle**, reproduced identically by a
second reset+dump (`content-verify-59b-playback`). `src/StorageLoopIo.cpp` `writeLoopPersisted` serializes
the loop body's note/event snapshot from `loop.publishedSnapshot()` without requiring it to be current
with the committed passes, and `applyLoadedLoopSlotBodyCommit` installs it as the published root
(`storePublishedSnapshot(loadedLoopSnapshot, 0)`) with no reconcile against the restored passes; after a
reboot there are no unpublished passes for the stale-root overlay to fold in, so the committed overdub
stays silent while present in storage/effective. **Correction from the healthy-ring telemetry
(`content-verify-59`):** `PERS,request` at 78.6 s is ~10 s after the last publish (68.6 s), so the save did
not overtake the publication; and a stale root would have written a legacy body (no root on load ⇒
silence), not record-only playback. The persisted body was LSV1 with a root that was current at save but
already missing the overdub, so the prime suspect moved to the **publisher's candidate completeness**; all
guards in that area (`f139b2a7`, `e87272a8`, `4fcfbcb3`, `2b948d2a`) are still present, and nothing in the
save/snapshot/playback path changed since the last known-good device run except 2.4 slices 1–2. Fix
options (owner decision — persistence/publication contract): gate the persist admission on the root being
current (with a bounded re-admit), serialize the body from the passes, reconcile at load, or harden the
publication candidate's coverage.

**ROOT CAUSE LOCALISED 2026-09-18 (live capture, no reset): the global REDO of an overdub entry restores the passes but not the published root, so the redone overdub is permanently inaudible.** A MIDI-only capture (no serial open, so no reset: `scripts/hitl/live_playback_capture.py`) proves the loss is *live*, not save/load -- the live capture has 0 rows for the overdub notes (24-39) exactly like the post-reset one. The live gather telemetry pins the moment: `playback_build` eventCount 384 (rev 1/2) -> 576 (rev 3, after the session-exit publish at 68.85 s) -> **384 (rev 4) after the `Undo` at 78.53 s** -> **still 384 (overlay, rev 5) after the `Redo` at 81.67 s**, while the dump's `passes`/`effective` keep 576 the whole time. Code: `TrackUndo::applyPublishedSnapshotSelect` (`src/TrackUndo.cpp:148`) selects `undo ? beforePublishedSnapshot : afterPublishedSnapshot` and is a **no-op when the redo's `afterPublishedSnapshot` is empty**; the redo's pass restore bumps `playbackRevision` afterwards, which makes the pre-undo root stale, and the stale-root overlay has no unpublished receipt left to fold the redone passes in. The application order is correct (root selected after the restore) and the attach machinery from `8aa29307` is all present -- registration (`ensureSessionExitPublishedUndoAttachRegistered`), handler (`TrackUndo::attachAfterPublishedSnapshot`, which only accepts `RecordPassAdded`/`OverdubPassAdded` with a matching id and loopId) and the publisher's call at `LoopSnapshotPublish.cpp:2194`. Nothing guarantees the after-root is captured before an undo can consume the entry, and nothing republishes on a redo that lacks it -- that hole is the defect. Next: decide the fix (capture/attach the after-root before the entry can be undone, or republish on a redo whose after-root is missing) and re-verify the DEC-071 shape with undo/redo in the recipe.

**Observability fix landed alongside:** `SESSION_CAPTURE_DROID_QUEUE=0` folded into the acceptance env
`teensy41-capture-serial-no-grain`. `DIAG,droid_queue` was ~5.6k rows/run and, with the per-slice
`PERS,work` trace, overflowed the 1 MiB ring (four runs came back `INCONCLUSIVE_CAPTURE` after the
harness failed to deliver the overdub-stop press). With it dropped, the DEC-071 run fell from 20,800 to
**9,237** `#CAP` lines and became a clean PASS, so the base-run oracle is trustworthy again. RAM1 locals
unchanged at 6,400 for both envs.

**2026-09-17 successor change task 5.5 CLOSED: the adversarial mid-grain reentry checkpoint set is
complete and in the default gate (OpenSpec `successor-capture-reentry-unguarding`).** 5.5 was the only
remaining host-only item of that change, the one DEC-072 / D8 directly unblocked, and the one still
hiding coverage behind `REENTRY_RED=1`. The missing "after the raw-range stage" checkpoint is added at
`CanonicalRewrite` (raw range sealed, canonical order built, canonical copy being rewritten, no staged
pass yet), and one further checkpoint is added beyond the named set because the phase enum is **not**
execution order: `PrepareTransfer` dispatches before `CanonicalTail` and its grain also stages the pass
and takes the receipt, so that state is expressed as *the turn the job first owns its staged pass*
(`test_adversarial_reentry_at_the_staged_pass_ownership_checkpoint`) — exactly the state a successor's
`beginCapture` used to destroy. The `CompanionHistory` case is promoted into the default suite, so
`REENTRY_RED` is gone (`ONLY_COMPANION=1` remains the focused single-case runner). The checkpoints are
tight: each phase checkpoint is asserted by **equality**, and the ownership checkpoint asserts it pauses
on `CanonicalTail`. Five checkpoints in total — `CanonicalOrder`, `CanonicalRewrite`,
`CompanionHistory`, staged-pass ownership, `AtomicTransfer`. Evidence: `test_continuous_capture` 74/74,
`ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native **2,239 / 1 skipped / 0 failures**,
`teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / **locals
6,400** (test-only increment), both `openspec validate --strict` checks valid. Remaining in that change:
5.9 (the `hitl-content-verification` audibility proof), 5.11 on that run, 6.2 (a device session), then
the 6.1 re-run and 6.3 closeout.

**2026-09-17 task 2.5's real-gather parity fixtures landed (OpenSpec
`resumable-midi-boundary-playback-maintenance`).** The 2.5a harness could compare the immutable
`PreparedPlaybackPair` against the synchronous merged/order path only over a synthetic window, so the
2.4 slice-2 conversion was not yet validated through the production publication pipeline. It is now:
`buildFixtureRoot` in `test_continuous_capture` builds a published snapshot root carrying both
independent snapshot roots — note membership and the MIDI-event membership the playback gather reads —
mirroring the publisher (an Off at or beyond the loop length clamps to `loopLength - 1`; a wrap note
splits into head+tail with the same `NoteId`), and `assertPairMatchesSynchronousPathFromAnchor` anchors
both consumers at the pair's own `cursorSeed`, so the comparison is about the pair rather than about
where each cursor started. Four owner-priority fixtures landed: wrap + loop-edge note, source change
mid-window (hold, 8-tick publish latency, the added row audible), the long-loop window (the documented
`pairApplicable == false` boundary, plus the windowed stream against an independently phase-sorted
full-gather window), and the record-stop publication gap (an empty gather publishes no pair; once the
session-exit root lands the pair matches the synchronous path). Evidence: `test_continuous_capture`
71/71, `REENTRY_RED=1` 72/72, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native
**2,236 / 1 skipped / 0 failures**, `teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 /
code 425,496 / padding 488 / **locals 6,400** (test-only increment; no firmware change). What remains of
2.5 is the view/bridge-path matrix (record stop, phase-zero wrap admission, pre-seal invalidation,
rejected-On/later-Off, accepted-On/matching-Off, wrap-held closure, append-and-stop, the explicit
deferred-stop trace, promotion before wrap, max phase advance, denied refill, three-buffer rotation,
source-change preemption near handoff, source prep midway through two windows, second-source bridge
replacement, coverage fault, layered slots, slot switch, first/second revision changes). 2.4 is
code-complete and open only for the 5.3 device read of the pair counters.

**2026-09-17 task 2.4 slice 2 landed: an immutable `PreparedPlaybackPair` with atomic publication, and
the measurement decides against slice 3 (OpenSpec `resumable-midi-boundary-playback-maintenance`).**
The pinned invariant — *playback never observes `mergedEvents` and `playbackOrder` from different
generations; a pair becomes consumable only when both members and their cursor/reanchor state are
complete for the same source generation* — is now structural: `PreparedPlaybackPair`
(`include/TrackPlaybackRuntime.h`) carries the generation, the live source identity, the full-loop
coverage, a pointer to a dedicated prewarmed `FixedPlaybackEventStorage`, the event count and the
`cursorSeed` resolved against the publication phase, and `LoopPlaybackRuntime::publishPreparedPlaybackPair`
replaces the whole descriptor in one assignment inside a non-yielding turn. The off-clock owner
(`Track::publishPreparedPlaybackPairFromWorkingWindow` / `Track::servicePendingPreparedPlaybackPair`,
serviced one preparation per turn from `runDeferredLoadAndDisplayFrame`) is the only publisher;
`resolvePreparedPairConsume` is the clock path's entire decision, so `playCommittedLoopMidi` no longer
runs `::rebuildPlaybackOrder` / `reanchorPlaybackIndex` for the full-loop merged path. A source change
(new `playbackRevision`, new snapshot identity, or the one-shot overdub-seal receipt exposed through
`Loop::hasPendingOverdubSealAdmission()`) records exactly one request and the tick keeps consuming the
published P0. Measurement (native): `prepared_pair_hold_ticks` **8** and
`prepared_pair_max_latency_ticks` **7** — one MIDI clock interval, ~1% of the 768-tick one-bar budget
and far inside the next 16th-note (48-tick) audibility edge — with `playbackOrderHotRebuildCount` 0
across the decision loop and 1 for the off-clock owner's replacement order. **Slice 3 (b), a bounded
incremental order update, is therefore not required for 2.4**; `prepared_pair_rebuild_units` records the
one per-source-change build as its own follow-up. Evidence: `test_continuous_capture` 67/67 (four new
slice-2 fixtures), `REENTRY_RED=1` 68/68, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native
**2,232 / 1 skipped / 0 failures**, RAM1 variables 91,904 / code 425,496
/ padding 488 / **locals 6,400** (the new off-clock helpers are FLASH-resident; ITCM is one 32 KB
FlexRAM bank from the edge). 2.4 ticks on the 5.3 device measurement of the same counters; 5.4 stays
blocked on 2.5/5.3.

**2026-09-17 interval-owned staged capture pass (DEC-072; OpenSpec 5.12 / D8 landed and
device-verified).** The 5.5 `CompanionHistory` blocker was one `Loop` scalar
(`pendingCapturePass_`) serving two capture intervals: a deferred predecessor staged its pass at
`PrepareTransfer`, a successor's `beginCapture` discarded it unconditionally, the predecessor's
`beginPreparedCompanionHistory` was refused forever by its own precondition, and its job held
`captureStopOwner` and the frozen role for the rest of the session. Guarding the discard only
preserved the successor's `AlreadyPending` collision, so ownership moved instead: the job stages into
the Loop slot and immediately takes the receipt into `CaptureStopJob::pendingPass` in the same
non-yielding grain, the companion history binds that receipt by address, and the job's
`AtomicTransfer` consumes it directly through `Loop::transferCapturePass`; every deferred reader
(undo identity, bridge-refresh, companion verification, stop telemetry/accounting) resolves the
receipt through the job with no fallback. `beginCapture` keeps its unconditional producer-local
clear. Evidence: `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture` green (the former
red reproducer), `REENTRY_RED=1` 57/57, full native 2,221 / 1 skipped / 0 failures, RAM1 locals
6,400 (the ring constructor had to become FLASH-resident: `CaptureStopJob` now owns a
`PendingCapturePass`, and the implicit aggregate constructor cost one 32 KB FlexRAM bank in ITCM);
device [`captures/successor-ownership-06`](../captures/successor-ownership-06/session_20260917_230432.log)
managed HITL `base`, `Result: PASS`, `overlap=True denied_stop=True retirement_ok=true` (A retires
inside B's live interval; B then stages and transfers its own pass). The host probe oracle now also
accepts the current `Overdub stop refused: stop_job_pending` denial wording. OpenSpec 5.5 remains
open for its own parameterized checkpoint coverage and gate promotion; the change's remaining pick-up order (5.5 first, then one 5.9/5.11/6.2 device session, then 6.1 and 6.3) is recorded at the top of its `tasks.md`.

**2026-09-17 transport-stop retarget re-route (DEC-070).** The post-HITL stuck state machine was a
deferred stop job admitted for PLAYING, retargeted to Stopped and left on the PLAYING phase path; its
companion work is bound to the transport epoch the stop invalidates, so it parked in
`CompanionHistory` and held the slot's finalization resources, refusing every later stop until a
reboot. The retarget now re-routes the job onto the STOPPED continuation
(`retireSkippedCompanionPreparation` -> `AtomicTransfer` -> deferred cleanup). Native case
`test_transport_stop_retarget_retires_a_midflight_playing_job` fails without it (full native 2,213 /
1 skipped / 0 failures; RAM1 locals 6,400); device `captures/successor-wrap-40` shows the retargeted
job retiring 2.2 s after the teardown transport stop and the slot staying usable across repeated
enter/stop cycles. openspec 5.3/5.7/5.8 closed. DEC-070 follow-up: a retargeted job now keeps and publishes its companion (note-edit) horizon instead of dropping it (bounded 512-grain per-phase fallback to the raw pass if it cannot complete), which restores the overdubbed notes to the display (`captures/successor-wrap-42` reports `overdub_ok=True`; `transport_ok=False` there is inconclusive because that run overflowed the capture ring — openspec 5.9). Diagnostic instrumentation for stall reasons stays in place.


**2026-09-17 the wrap owns the slot's canonical revision too (DEC-069; hardening landed, gate
open).** A manual three-overdub device session
([`captures/session_20260917_172651.log`](../../captures/session_20260917_172651.log)) shows the
second interval's deferred job never retiring once the third interval's **wrap** commits a pass:
the commit advances `Loop::playbackRevision`, which invalidates the pending job's
`RuntimeCanonicalPassInputs` frozen source (pinned to its admission revision), so its
`CanonicalTail` phase denies forever, its finalization resources are never released, and the third
interval's own stop is refused as occupied for the rest of the session; the third interval's queued
wrap publication stays behind the unresolved predecessor publication, so its notes neither show nor
record at the wrap. DEC-067's rule — a resource blocks the transition that first requires it —
therefore applies to the wrap as it already does to start and stop. Landed: the wrap no longer
resets a pending predecessor's frozen canonical source or its frozen order/rewrite builders (two
new native cases; full native 2211 / 1 skipped / 0 failures; RAM1 locals 6464; firmware builds).
DEC-069 Option A is now implemented (owner's choice): the crossing is recorded as an owed commit
and sealed on the first turn after the job retires, so the revision the job pinned never moves; the
held note's reservation is never reset, which removes the live interval's `unmatched NoteOff`
rejections as well. Three native cases (two fail without the gate); full native 2,212 / 1 skipped /
0 failures; RAM1 locals 6,464. Device acceptance closed (`captures/successor-wrap-36`): the successor's deferred crossing
committed after the predecessor retired, with no `unmatched NoteOff`; `captures/successor-wrap-33` is
the matching DEC-068 regression PASS. The reproducer's temporary successor-stream flag is not yet
promoted to a registered stimulus variant.


**2026-09-17 reentry session ownership (DEC-068).** Tasks 5.1(d)/(e) landed and immediately
exposed a job-locality defect: the retiring predecessor's cleanup built its session-exit receipt
from the shared session state (the successor's), closed the live successor's session, and left the
successor's own job stuck in `DeferredCleanup` with its publication unqueued. Each interval now
owns its session-exit tail — captured at its own stop admission (swap, not copy), released with its
own pass id at its own commit, consumed ready-FIFO by the publisher — a successor opens a fresh
session instead of joining the draining one, and the cleanup ends the live session only when the
retiring job still owns it. Device `captures/successor-reentry-27` is the first clean reentry run:
B START 1,915 ms before A's release, B's stop refused, `Reentry released`, B's stop admitted, **B's
own retirement** at 52.992 s, run result **PASS** (`overlap_ok`/`denied_stop_ok`/`retirement_ok`).
Six native cases cover the gates; full native 2209, 1 skipped, 0 failures; RAM1 locals 6464.
Next in the change: 4.2/4.3 three-route agreement, then 5.3-5.6, then the shed-capture
observability item before final 6.2 closeout.

**2026-09-17 first-record prepared bar-1 launch + successor-reentry finalization admission
(DEC-067).** Two user-directed RCs landed. (1) The record stop now *uses* the prepared bar-1
window: a take that ended without the handoff no longer blocks the next take's preparation
(`b6dd67bf`), the bound window is preserved across the launch's playback-cache resets and
adopted as the audible source while the first root publishes (`a49bb01a`), and the adoption
selects the launch cursor so the already-played prefix is not replayed as a catch-up burst
(`a20eba50`). Device `captures/first-record-launch-04` (canonical `base`, PASS): first note
93 ms after the stop/PLAYING edge and the following note-ons on the 16th grid
(125.0/125.0/125.0/122.4 ms) where `deadline-pacing-01` measured 620 ms and a burst. Still open
there: S2b (the window's retained extent can lag the last accepted input, and the bar-1
closure obligations are not carried yet) and S3 (continuation past bar 1). (2) The
successor-reentry device case exposed that `Track::startOverdubbing` gated on three
*finalization* resources; per the user's decision the gates were moved to the transition that
first needs them — STOP — under DEC-067 (`758c89b0`), the successor stop was kept out of the
legacy fallback when those resources are busy (`d9d641da`), and two deterministic edges
(`Reentry window open slot=<n>`, `Reentry released`) now drive the HITL probe (`8dc92a5e`,
`7c3e1c20`, `96752f86`). Device `captures/successor-reentry-25`: B START 583 ms after A's stop
and 2,209 ms before A's finalization released, B's stop refused while A owned those resources,
then admitted after the release — the first time the declared depth-2 capacity is observable on
hardware. Gates: full native 2203 cases, 1 skipped, 0 failures; RAM1 code 425576 / padding 408
/ locals 6464.

**2026-09-16 task 2.3 causal predecessor/source identity — increment B, approved
superseded rule (resumable-midi-boundary-playback-maintenance).** Option (A) is
implemented and verified. A deferred stop whose recorded `causalBase` is no longer the
live published root retires without publishing a successor window: the staged accepted
capture pass is committed through
`TrackPreparedStopCommit::commitSupersededCausalBase`, no bridge is selected or
published (`causal_base_superseded`), a prepared protected undo entry is published in
the same turn, and the unpublished companion horizon returns to `Prewarmed` through the
new allocation-free `Loop::restoreSupersededPreparedCompanionHistory(owner)` (with
discard-and-reprewarm as fallback). The checks sit at the start of `CompanionBridge`
and `AtomicTransfer`, with `PendingStopAdmission::retireSupersededPreparation()`
retiring the ranked/canonical/undo-collection credits so the job retires instead of
stalling. Implementation finding recorded: resolution must stay live — the canonical
owner identity is compared by address against the live Loop scalars, so the recorded
identity is the supersession reference, not the resolver, and the live revision/root
were measured unchanged during a normal finalization. Full native 2,191 cases — 2,186
passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Recorded deviation: the
superseded route publishes the capture pass only, relying on the newer root to already
carry the companion rows (to be re-verified when successor capture is unguarded).
Remaining in 2.3: successor-capture unguarding, in a separate change. Task 2.3 remains
unchecked.

**2026-09-16 task 2.3 causal predecessor/source identity — increment A
(resumable-midi-boundary-playback-maintenance).** Auditing task 2.3(f) found that a
deferred stop re-derives its predecessor source live at every consumer turn: admission
computes a `PlaybackViewSourceIdentity baseSource` for credit sizing and discards it, the
worker recomputes `baseSource`/`targetSource`/`frozenSource`/`currentSource` when the grain
runs, and the atomic commit recomputes `base` from the live `playbackRevision` and
`publishedSnapshotIdentity()`. A successor publication during finalization could therefore
change what the predecessor resolved against. `CaptureStopJob` now records `causalBase`,
pins its root with `causalBaseSnapshot` (`ImmutableLoopSnapshotRef`) and sets
`causalBaseRecorded`; admission resolves and pins it before the raw-range ownership transfer
and denies `causal_base_unavailable` if a non-null published identity cannot be pinned, and
retirement clears all three. Increment A records only, so behavior is unchanged; increment B
(next) makes the worker and the atomic commit resolve against the recorded identity and
decides the epoch-qualified publication rule including STOPPED recovery. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191 cases
— 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 prepared companion history interval qualification
(resumable-midi-boundary-playback-maintenance).** The Loop-owned prepared companion
history (`preparedCompanionHistory_`) is borrowed worker scratch that outlives one
grain, so a successor interval could previously observe, advance, commit, recycle or
discard a predecessor's in-flight preparation. Every consumer now requires the owning
interval identity, `PendingStopAdmission::intervalToken()` (frozenGeneration +
stopTick) — a value, not an object address, so a copy of an admission made for credit
probing or retargeting still identifies the same interval while a reused ring
descriptor gets a fresh token; zero means the live session path.
`preparedCompanionHistoryOwnedBy()` keeps Cold/Prewarmed receipt-free and
owner-agnostic; `beginPreparedCompanionHistory` records the token and recycle
completion plus `discardPreparedCompanionHistory` clear it. The deferred stop worker,
the stop-commit helpers and `beginCollectOverdubStopUndoPasses` pass
`stop.intervalToken()`; default arguments keep the live path source-compatible. New
native `test_prepared_companion_history_is_job_qualified` covers successor and
live-session refusals, stale discard, receipt ownership and reuse after recycling;
the two Track-level `test_playback_prewarm` fixtures now begin the preparation with
the admission that consumes it, and their stale unqualified consumers were qualified
(rather than deleted) because the qualification is what makes those calls correctly
answer "not mine". This closes the session/preview/undo/snapshot receipt slice. Full
native 2,190 cases — 2,185 passed, 1 skipped, the same three intentional
public-reentry failures; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom. Remaining in 2.3: causal predecessor/source identity per job, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 session-exit handoff receipt ring
(resumable-midi-boundary-playback-maintenance).** The session-exit
`SessionExitSnapshotHandoff` receipt is now a Loop-owned FIFO ring of
`kSessionExitReceiptCapacity = 4` (external-memory-first, cold-prewarmed by
`Loop::prewarmSessionExitSnapshotHandoff()`), replacing the single-slot unique
pointer. This closes "Finding 2" of the deferred-stop receipt audit: the receipt
outlives the stop job, so it cannot be job-owned. `canHandoffSessionExitSnapshot()`
is now a **free-slot** gate; "one receipt per open session" is enforced inside
`handoffSessionExitSnapshot()` by `sessionExitHandedOff_` (set on handoff, cleared by
`openOverdubSession()`), so a successor session is never serialized behind an
unconsumed publisher turn while a repeat handoff for the same session is refused. The
idle publisher consumes the oldest receipt one per turn (retaining it when
construction is disallowed), and playback's stale-root overlay folds every pending
receipt (passes and companion ids) rather than one. New native
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the stale
"unconsumed receipt closes the gate" assertions in `test_playback_prewarm` and
`test_loop_take_survival` were rewritten to the ring contract. Native:
`test_playback_prewarm` 165/165, full suite 2,189 cases — 2,184 passed, 1 skipped, the
same three intentional public-reentry failures; capture-serial builds with 6,752
bytes RAM1 headroom (13 banks, code 425,208). No new boot allocation, so the increment
A/B device memory check (build `91e6b38c…`) still covers this slice. Remaining in 2.3:
prepared-companion history qualification, causal predecessor/source identity, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 increments A/B boot abort — ROOT-CAUSED, FIXED, device-verified.**
The memory check found the increment A/B firmware never reached a running sketch
(`a7f10733…` emitted only `boot_reset,crash=1` before the USB port dropped; the
32-entry-prewarm variant `8c03dcfc…` behaved the same, so the undo-stack prewarm was
not the cause). A hardware bisect isolated it to **`0f398715` (increment A)**:
`7a275d1f` boots (21386 bytes, crash=0), `0f398715` and `162df28f` do not (0 bytes).
Root cause: `prewarmCaptureStopJobs` reserved `kRecordsPerBridge` (2048) rows for each
job's `undoPassIds`/`undoCompanionIds`, and both are
`InternalHeapFirstAllocator` vectors — 4 ring entries × 8 tracks × 2 lists × 2048 × 4 B
= **512 KB demanded from a ~340 KB internal heap at boot**. Fixed by bounding that
reserve to `kUndoCollectionReserveRows = 64` (16 KB total); a session longer than that
publishes no prepared undo entry and takes the plain commit. The fixed tip is flashed
and verified: build `91e6b38c…`, 22091 bytes streamed, 0 warnings, prewarm samples
`PSRAM 5,141,952 -> 3,775,872`, `heap 352,256 -> 290,816`, chunks `512 -> 416` — about
79 KB PSRAM and 20 KB internal heap over the reference build
(`captures/task23-undo-prewarm-fix/result.json`). Native: 2188 cases, 2183 passed, one
skipped, the same three reentry failures; build 6752 bytes RAM1 headroom.

**2026-09-16 task 2.3 deferred protected undo publication wired
(resumable-midi-boundary-playback-maintenance):** the normal deferred overdub stop
now records its `OverdubPassAdded` entry. `TrackUndoPublication` is externally
allocated and cold-prewarmed by `prewarmProtectedUndo()`, which also reserves the
undo stack's entry capacity (`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) — a
publication precondition whose device PSRAM admission must be measured.
`stepDeferredUndoPublication` runs `begin` + bounded `stepPrepare` inside the
existing `UndoCollection` phase; the atomic transfer proves `canPublish(...)` then
calls `advanceAtomicTransferWithUndo`, else plain-commits with the transaction
restored. `stepProtectedUndoDrain` drains one bounded quantum per DeferredCleanup
turn (`ReleaseRedo`/`Trim`/`Complete`, plus restore + `recoverPreparedEntry` for an
unpublished transaction), so `protectedState()` is Idle before the job retires.
Tests assert one published `OverdubPassAdded` and an Idle transaction; full native
2,188 cases — 2,183 passed, one skipped, the same three known reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom. Known deviations: the DEC-063
disabled-pass reclaim continuation is unwired, and STOPPED/empty routes publish no
protected entry. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 deferred undo collection, job-owned prepared entry
(resumable-midi-boundary-playback-maintenance):** a new
`PendingStopPhase::UndoCollection` on the prepared PLAYING route collects the
session's undo identities in bounded grains and builds the `OverdubPassAdded` entry
from the job's own slot and baseline via `TrackUndo::prepareOverdubSessionEntry`.
The collection has its own admission grain class
(`remainingUndoCollectionGrains`/`undoCollectionQuantum`, sized from
`Loop::requiredSessionStopUndoCollectionGrains`, included in
`finalizationComplete()`), so saturation denies admission instead of stalling. The
STOPPED/empty routes skip companion preparation and never enter the phase. Private
outputs are pre-reserved in `prewarmCaptureStopJobs`; `Loop::reclaimUnreferencedDisabledEditPasses`
moved to flash to hold 13 ITCM banks. New native
`test_deferred_stop_prepares_job_owned_undo_entry`; full native 2,188 cases — 2,183
passed, one skipped, the same three known reentry failures; capture-serial builds
with 6,752 bytes RAM1 headroom. Increment B remains: publish through
`TrackUndoPublication` + `commitWithUndo`/`commitStoppedWithUndo` and drain its
`ReleaseRedo → AwaitReclaim → Trim → Complete` phases in DeferredCleanup. Task 2.3
remains unchecked.

**2026-09-16 task 2.3 deferred-stop receipt audit — design decision required
(resumable-midi-boundary-playback-maintenance):** the last two receipt items need a
decision before implementation. The normal deferred overdub stop publishes **no**
`OverdubPassAdded` entry — `pushOverdubPassAdded` runs only from the fault-fallback
`commitCaptureForStop` via `finalizeCommitSideEffects`, while the deferred path calls
`TrackPreparedStopCommit::commit` with `undo = nullptr` — so the job-owned undo
baseline is correct preparation but currently unreachable in production; wiring the
protected undo publication (the prewarmed-but-unused resumable
`*CollectOverdubSessionUndoPasses` machinery plus `TrackUndoPublication`) is what
makes it load-bearing. The session-exit receipt cannot be job-owned because it is
retained past the job and consumed later by the idle publisher while playback reads
it for the stale-root overlay; it needs a Loop-owned multi-slot ring with the gate
testing a free slot. Ring size and multi-pending overlay behaviour are the open
questions. No code changed for these two items. See CONTINUOUS-CAPTURE-OWNERSHIP.md.

**2026-09-16 task 2.3 job-owned overdub-session undo baseline (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns the overdub-session undo baseline
(`ImmutableLoopSnapshotRef beforePublished` / `hasBeforePublished`). Admission moves
the live `Track::overdubSessionBeforePublished_` into the job and clears the live
member, so a successor session's `beginOverdubSession` records its own baseline
instead of overwriting the one this interval's `OverdubPassAdded` entry must record;
`TrackUndo::pushOverdubPassAdded` prefers the head job's baseline and an unconsumed
baseline is retired with the job. New native
`test_stop_job_owns_overdub_session_undo_baseline`; `test_continuous_capture` 29 pass
with the same three intentional reentry failures; full native 2,187 cases — 2,182
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: the session-exit snapshot handoff receipt and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned capture preview (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `CapturePreview preview`, transferred from the live session
at admission by constant work (`CapturePreview::swapWith`, per-vector swap, no
copy/allocation) with a job-identified read borrow, so the deferred cleanup seals
this interval's preview rather than a successor's. `Loop::capturePreviewView()` is
the single readable accessor and is out of line in flash (inlining it at 40+ display
call sites cost a RAM1 bank); 45 read-only display/Edit sites were migrated and
writers keep the member. `TrackPreparedStopCommit::completeDeferredCleanup` takes the
job's owned preview. New native `test_stop_job_owns_capture_preview` and
`test_capture_preview_borrow_release_is_owner_identified`; `test_continuous_capture`
28 pass with the same three intentional reentry failures; full native 2,186 cases —
2,181 passed, one skipped, the same three; capture-serial builds with 6,752 bytes
RAM1 headroom (13 ITCM banks). Still open in this slice: the overdub session/undo
before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned pending note changes (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `PendingNoteChangeVec pendingChanges`; admission swaps the
live Add/Shorten/Hide set into the job in constant work and leaves the live session a
prewarmed empty set, so the worker no longer resolves the canonical tail against the
live session's intents and a successor interval is independent. `Loop::pendingNoteChanges()`
returns the job's owned set while a job-identified borrow is bound (writers use the live
member), and the borrow is released with the job. New native
`test_stop_job_owns_pending_note_changes` and
`test_pending_change_borrow_release_is_owner_identified`; `test_continuous_capture` 26
pass with the same three intentional reentry failures; full native 2,184 cases — 2,179
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: `capturePreview` ownership across the stop, the
overdub session/undo before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 sealed raw-range ownership wired (resumable-midi-boundary-playback-maintenance):**
The approved ownership transfer is implemented. `CaptureStopJob` owns
`LoopEventStore::CaptureRange rawRange`; `beginDeferredOverdubStop` proves
`canSealCaptureRange` before any mutation, seals the producer extent at punch-out
and binds a job-identified read borrow on `Loop`. A denied admission performs no
seal and no transfer; an empty capture skips the seal. `Loop::captureReadSource()`
exposes producer and finalizing extents separately and ordering is scratch-only, so
the borrowed range is never mutated; closure NoteId resolves from the finalizing
extent first. `Track::retireJobCaptureSourceGrain` retires the owned extent in
grains, drops the borrow only while it still identifies that job, and re-arms the
descriptor's reserved destination. DisplayManager is unchanged; the
successor-capture guard stays deliberately in place. Five new native regressions
plus three migrated ownership assertions; `test_continuous_capture` 24 pass with the
same three intentional reentry failures; full native 2,182 cases — 2,177 passed,
one skipped, the same three; capture-serial builds with 6,752 bytes RAM1 headroom
(new functions are FLASHMEM and `Loop::beginCapture`/`discardCapture` moved to flash
to hold 13 ITCM banks). Device boot prewarm admission **PASS** on this build
(`41b30d18…`, `captures/task23-sealed-range-prewarm/result.json`): PSRAM free
5,142,912 -> 3,856,320 B (160,824 B/slot, unchanged), internal heap 352,256 ->
311,296 B, pool chunks 512 -> 416 (+4/slot = the four range destinations per track),
all slots `ready=1` with 8 credits and zero warnings. Task 2.3 remains
unchecked; per-job session/preview/undo/snapshot receipts, causal predecessor/source
identity and unguarding successor capture remain open.

**2026-09-16 task 2.3 FIFO/queue-head binding (resumable-midi-boundary-playback-maintenance):**
The stop-descriptor ring is now the explicit admission authority —
`Track::captureStopJobTail()` admits (null when full, `queue_full` stop stage),
`captureStopJobHead()` is the worker's stable per-grain descriptor, and
`!STOP_STATUS` reports `queue=depth/capacity`. Behaviour is unchanged at one job
(tail == head). Two new native regressions; full native 2,178 cases — 2,173
passed, one skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom.

**Sealed raw-range wiring is audited but not wired:** during the deferred stop
`Loop::capture.store` is still read (and sorted in place) by the Loop
display-window merge helpers and scanned by `appendCaptureNoteOffAtPhase` for
closure note identity, and `Loop::beginCapture` frees a pre-reserved tail. Sealing
at punch-out therefore needs a Loop-internal producer-or-sealed read fallback; the
recommended design and the rejected lazy-seal alternative are recorded in
CONTINUOUS-CAPTURE-OWNERSHIP.md. It moves capture-interval ownership between `Loop`
and the admitted Track job, so the architecture ownership-transfer protocol
applies — approval required before implementation. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 ingress/staging isolation (resumable-midi-boundary-playback-maintenance):**
`RuntimePlaybackViewSet::captureStaging` is split into two independently prewarmed
roles in the same owner: a *live* ingress staging and a *frozen* stop-job staging.
Live ingress writes only the live role; the one admitted stop job borrows the frozen
role. Punch-out freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its own
retired frozen role, so old-job cleanup cannot clear or alias a successor interval's
ingress rows. No new owner/module, no canonical/persistence/SD change, no action-edge
copy. New native regressions in `test_playback_prewarm` and `test_continuous_capture`
(production Button A route) pass; full native 2,176 cases — 2,171 passed, one skipped,
the same three intentional public-reentry failures; `teensy41-capture-serial` builds
with 6,752 bytes RAM1 local headroom, RAM2 free 472,928. Added cost is one
2,048-event staging buffer per fully-prewarmed runtime slot (now counted by
`fixedExternalBytes`); boot prewarm admission was measured on device and passes —
build `fb4ab3a9…` reports PSRAM free 5,143,968 → 3,859,680 B across the 8 boot
slots (160,536 B/slot, two stagings per slot, no internal-heap fallback), all
slots `ready=1` with 8 credits, zero warnings, and 46.1 % of the 8.38 MB pool still
free (`captures/task23-staging-prewarm/result.json`; the run used a capture-only
SESSION_CAPTURE boot line). The selected-slot full-prewarm plus dense loaded
content still needs its own footprint check before the guards come off. FIFO/queue-head
binding, sealed raw-range wiring, causal predecessor/source, per-job
session/preview/undo/snapshot receipts and the three reentry cases stay open; task
2.3 remains unchecked. DEC-064 follow-up recorded.

**2026-09-16 dense snapshot representability + fragment transaction (openspec 2.3 / immutable-loop-snapshot-passes 3.7):**
The dense record/overdub pair (`task23-dense-snapshot-cc-playback`, 734 effective
rows) was lossy in publication: bar 2 needs 260 event references, above the fixed
16-page (256-reference) per-bar membership ceiling. Construction failed at
`appendCurrentReference`, and because pass-change progress had already advanced the
fragment was discarded, so a 710-row truncated candidate was published. Two bugs:
representability (valid content exceeds the ceiling) and atomicity (failure consumed
progress despite DEC-047). Fix: explicit `kSnapshotMembershipMaxRefsPerBar = 4096`
design envelope with derived 256-page limit applied to event and note membership,
`uint16_t` page counters, and transactional fragment incorporation (authoritative
progress advances only after successful incorporation; partial candidate discarded on
failure; representability failure parks publication after two consecutive failures
with `DIAG,snapshot_fragment`). DEC-047 amended + ExecutionBounds rows 21/22 updated
2026-09-16. Focused `test_loop_take_survival` 132/132; full native 2166 passed / one
skipped / three known task-2.3 reentry failures; `teensy41-capture-serial` SUCCESS
with RAM1 locals 6752. **Hardware verification PASS for representability:** uploaded
build `5bb90b47bdae299d630be640…`, dense recipe stored 766 effective rows with bar 2
at 270 refs (>256) and four restart cycles played 765 rows with 0 duplicates; the
only absent row is the pre-existing terminal loop-boundary Off (`NoteId=0`). Ordering
parity and that terminal-Off ledger behavior remain separate open items.

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**2026-09-16 automated dense rerun:** same track-5/slot-1 recipe reproduced the
reboot; corrected boot marker is crash=1. Serial missed the early fault text;
SD crashlog has not been read. Runner/capture finished. The dense gate remains
open; no manual reproduction is required. See task23-dense-crash-report-rerun
and active FOCUSED-STOP-HITL.md.

**2026-09-16 overlap reboot follow-up:** user confirms spontaneous reboot during
record-pass/overdub overlap. Native Shorten/stop regression passes; root cause
remains unresolved. Fixed destructive double-read of Teensy CrashReport: startup
now preserves the crash flag and renders once to Serial and SD. Build/upload pass,
with unchanged 5,600-byte RAM1 headroom. Human overlap reproduction on track 5 /
slot 1 is the next checkpoint; serial capture is in task23-overlap-crash-report.
See active FOCUSED-STOP-HITL.md for firmware hash and evidence limits.


**2026-09-16 device checkpoint (track 5 / slot 1, authorized):** verified firmware
uploaded. Two normal stop cleanups, focused transport-Stop silence, and three
restart content cycles pass. The denser run disconnected/rebooted before its stop
trace drained and is INCONCLUSIVE_CAPTURE; reset cause is unknown. No dense or
full task-5.3 acceptance. Updated host verifier recognizes pending-job completion
after visible PLAYING and passes 24 regression tests. Original reports and a
separate reevaluation are preserved under captures/task23-reset-*. See the active
FOCUSED-STOP-HITL.md checkpoint for exact build identity and limits. FIFO/reentry
implementation remains unfinished; no task checkbox closes.


**2026-09-16 pending-job reset protection / intermediate HITL checkpoint:**
An admitted stop job now retains its runtime staging and canonical-copy output
across cache reset. Reset retargets STOPPED and yields; Track invalidation preserves
the borrowed generation until terminal recovery. Native coverage exercises partial
copy and every stop phase, including held-note closure and receipt release.
A Tier-A prewarm record exposes output credits and pool/heap headroom. Verify
boot/load memory and single-job copy/Stop timing on hardware before expanding the
FIFO reservations: prior eager prewarm exhausted device PSRAM. This checkpoint is
not task-2.3 acceptance; the same three reentry fixtures remain red, and FIFO,
causal-source and preview/session/undo/snapshot migration remain unfinished.
No upload has been performed. Exact results and build identity are in the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md; the procedure is in FOCUSED-STOP-HITL.md.

**2026-09-16 task 2.3 worker-owner integration:** The existing stop worker now
uses a stopped-interval receipt for Loop/slot/phase/geometry and its own pending
notes. Closures target the saved owner; one-node retirement never clears live
notes, and completion cannot transition a newer Track interval. Production
empty, paired and held-note stops complete in native tests. Public A/B/C reentry
still fails: raw capture, staging, preview and undo/snapshot session migration
and the fixed FIFO remain open. Affected tests: 266 pass, the three known reentry
cases fail. Capture-serial builds with 5,600 bytes RAM1 local headroom; no upload.
Task 2.3 is not complete. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).



**2026-09-16 task 2.3: public-route reproducer and raw range foundation:**
The actual Button A → TrackManager → Track route and MidiHandler musical input
now run in a native fixture. First-interval capture/stop passes; A/B/C reentry,
manager same-pitch held-note reentry and direct Track reentry remain intentional
failing requirements. None of the three admission guards has been removed.

LoopEventStore now supports pre-reserved exclusive capture-range rotation,
read receipts, incremental release and stale rewrite/pool-lifetime rejection.
The focused range/persistence/event-store/playback-prewarm run passes 209 tests.
The full native run had 2,136 passes, one skip and only the three expected reentry
failures (before the final persistence-prewarm correction). This API
is not yet wired to Track; it does not fix immediate reentry. Job-owned staging,
closure credits, pending changes, undo/snapshot receipts, causal FIFO resolution,
cleanup isolation and device verification remain open. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
Capture-serial build passes with 6,176 bytes RAM1 local-variable space after
placing the bounded rewrite maintenance worker in flash. Device timing remains
unverified. Task 2.3 stays unchecked; no firmware upload or archive was performed.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.



Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.



Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Full native validation
reports 2,124 passed and one skipped; final focused validation passes 153 tests.
Firmware build and upload retry succeeded with 6,176 bytes RAM1 local headroom;
serial returned. Task 2.3 and hardware acceptance remain open.


**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Full native validation: 2,123 passed, one skipped. Build/upload succeeded
with 6,176 bytes RAM1 local headroom; serial returned. Device reentry remains
unverified; task 2.3 and HITL/deadline gates remain open.


**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Full native suite: 2,123 passed, one skipped. Firmware build/upload pass with
6,176 bytes RAM1 local headroom; serial port re-enumerated. Manual device
confirmation and task 2.3 remain open.


**Current DRY direction:** first recording reuses the existing playback view,
fill/order, coverage lifecycle, bridges and MIDI cursor. The separate recording
window/store/stream prototype was removed. Shared coverage-scoped fill is tested
(122 focused tests); source/lifecycle integration and original-boundary readiness
remain open. Earlier prototype test counts are historical, not acceptance of the
shared recording path. No delayed launch or new recording rejection is authorized.

**2026-09-15 first-record shared-window implementation:** bar 1 is installed as
raw playback coverage before RECORDING, then refreshed from the canonical
chunked capture store by the existing resumable fill/order owner. Source-count
and capture-display generation checks discard stale unpublished work; the
retained view survives a cache reset and binds final length at record stop.
Native focused coverage/prewarm tests pass 132/132 and both firmware builds
pass. Deferred main-loop maintenance is routed to FLASH, and the capture-serial
instrumentation target now passes its RAM1 local gate with 6,496 bytes of local
headroom. No upload or HITL verdict is claimed yet.

The recovered capture image has since been uploaded. Track 5 / slot 1 transport
Stop interruption passed focused precedence and silence verification; the clean
normal-stop rerun was inconclusive because the overdub-stop transition was not
confirmed. Broader HITL and reservation gates remain open.


**2026-09-15 first-record direction corrected by user:** no delayed launch is
acceptable. Prepare rolling one-bar playback windows during recording, with
bar 1 established at record start and retained for the existing final-bar return
boundary. Extend the currently overdub-only staged input path; prove late input,
held-note closure, final-length binding, and subsequent window handoffs without
rewinding transport or replaying historical events. The verified first-record
snapshot delay/catch-up defect remains unfixed. The deferred-launch proposal is
rejected history. See the active `resumable-midi-boundary-playback-maintenance`
design and specs; task and HITL gates remain open.


**2026-09-15 device-clock stop timeline ready:** capture-only telemetry now
retains every stopping grain's frozen identity, requested destination, actual
preceding MIDI service/due/deadline and span, followed by queued service and
NoteOn/Off emission records. Loss is explicit and invalidates the host timing
report. Focused native telemetry/tier: 43 passed; host timing/stop tests: 40
passed. Capture build remains within the RAM1 local gate (6,496 bytes). The
next device checkpoint is normal stop plus timed transport Stop after the
deferred-save setup precondition is confirmed. Task 4.1 remains open.

**2026-09-15 forced wrap Stop PASS:** the approved track 5/slot 1 four-bar
fixture reached `OVERDUB_STOPPING`; MIDI Stop followed 8.689 ms later. A
volatile per-track output barrier now blocks loop output before the Stop
handler's all-track CC123 fan-out and at the final Track emission call. The
rerun sent All Notes Off after 83 us, reached STOPPED after 173.607 ms, and
observed no target NoteOn for 18.9 s. Evidence:
`captures/resumable-handoff-wrap-forced4-barrier-hitl/`. This is focused
single-track silence evidence only; Task 2.3, layered playback and deadline
acceptance remain open. Native focused suites: 242 passed; host tests: 38
passed; capture firmware RAM1 locals: 6,496 bytes.

**2026-09-14 restart omission repaired and HITL PASS:** the prepared-stop path
now hands prewarmed session identities to the idle snapshot owner before close;
the stale-root overlay reads that receipt immediately. Companion publication
updates both note and MIDI-event membership. Three complete restart cycles each
played all 49 expected notes exactly once with matching NoteOffs, after a focused
Stop-interruption PASS. Evidence: `captures/resumable-handoff-admission-hitl/`
and `captures/resumable-handoff-restart-hitl/`. Firmware uploaded; transport stopped.
Native full suite 2,083 passed/1 skipped, final focused suite 124 passed, host tests
38 passed; RAM1 locals 6,496 bytes. Tasks remain 11/19. An earlier wrap-adjacent
run took legacy stop and is inconclusive; reproduce that admission fallback next
with the new capture-only denial diagnostics. Protected cleanup/undo, fully
bounded publisher/overlay work, layered parity and timing acceptance remain open.
See `FOCUSED-STOP-HITL.md` for scope and separate baseline verdicts.

**2026-09-14 restart survival FAIL:** after the focused interruption PASS, three
complete restart cycles emitted only base pitches 48–79; overdub pitches 24–39
and held 83 were absent. Canonical/display evidence still reports 49 notes while
playback's overlay has 32. Prioritize task 2.3's reserved bounded session-exit
snapshot handoff before clearing session identity; the prepared-stop production
path currently omits it. Do not insert the unbounded legacy queue call into an
active grain. Layered HITL is deferred until single-slot survival passes.
Evidence: `captures/resumable-stop-restart-survival/restart-result.json` and
`FOCUSED-STOP-HITL.md`. Transport stopped; firmware unchanged; tasks 11/19.

**2026-09-14 focused interruption PASS:** repaired concrete-output-port fixture
and stale-PLAYING skip; actual MIDI Stop observed 9.253 ms into OVERDUB_STOPPING,
All Notes Off after 70 us, then STOPPED and 9.49 s without resumed target notes.
Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`.
This closes only the single-track interruption/silence checkpoint. Restart
survival, layered playback, broader baseline and full timing acceptance remain
open. Tasks remain 11/19; see `FOCUSED-STOP-HITL.md` for scope and next gates.

**2026-09-14 MIDI deadline correlation checkpoint:** uploaded capture-only retained
send/due/deadline timestamps and per-stop-phase summaries. Native 2,080 passed,
1 skipped. A normal stopping interval measured 97.760 ms; one canonical-order
turn crossed its entry service deadline (248 us duration, 33 us slack). Another
run retained 24.678 ms clock and 26.049 ms NoteOff lateness. This is partial
hardware evidence, not deadline acceptance. Scratch clears succeeded with the
revised host precondition; the timed interruption fixture was subsequently repaired and its focused
checkpoint passed (see the newer entry above). See `FOCUSED-STOP-HITL.md`; tasks remain 11/19 complete.

**2026-09-14 protected stop continuation:** bounded undo publication/restore,
STOPPED-only joint recovery, disabled-pass reclamation and session-close helpers
now have native coverage. Production playback honors the latched STOPPED
emission policy; full joint stop integration remains open (11/19 tasks).
See the change's `BRIDGE-PROGRESS.md` and `FOCUSED-STOP-HITL.md` for the focused
Stop regression checkpoint and explicit remaining integration. Upload and a focused HITL attempt were approved and performed on 2026-09-14.
The initial boot exhaustion is fixed: boot now reserves staging only; full
bridge readiness remains separately guarded. Device startup and normal UI are
confirmed, with 4,401 KB free PSRAM. Native: 2,077 passed, 1 skipped. The exact
managed-log binding is fixed. Normal stopping was observed; the interruption
retest remains INCONCLUSIVE_CAPTURE after a deferred-save clear refusal. See
`FOCUSED-STOP-HITL.md` for evidence and remaining gates.

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-09-12 (OpenSpec 6.4 SHIPPED; next 6.5 archive)

---

**2026-09-13 active OpenSpec continuation:**
`resumable-midi-boundary-playback-maintenance` is **11/19 tasks complete**.
DEC-060 records user approval of canonical per-pass companion application for
combined capture preparation. Preserve immutable pass membership and apply rows
independently before the canonical merge; flattened admission is not the oracle.
The bounded per-pass owner now applies and merges immutable input descriptors
with pass/event origins and generation/capacity guards. Per-pass mapping now
validates an explicit published-base provenance table and feeds the ranked bridge
compiler/cursor. Full-loop view fill now constructs and owns the table alongside
events from a completed effective-pass baseline. A generation-checked sealed-pass
reader now copies raw chunks in bounded event/chunk visits without allocation
or partial-result exposure (2026-09-14; event-store tests 34/34). Production canonical pass
lifetime integration remains open. Bounded history preparation
now covers Create/Delete/Length/Pitch/Velocity/NoteRange, including contiguous row
collapse and Create identity reactivation. DEC-061 records the approved stable
Create sort correction. A runtime-owned continuation now connects frozen
sealed-pass descriptors through history application to separate effective-pass
inputs, with a generation-bound companion consumer. A direct LoopPasses overload
now selects canonical passes and filters active history in bounded grains;
a frozen-source entry point now checks live revision/generation/epoch, loop
length and capture state even on completed-result access. The typed companion
consumer now also binds the prepared row owner, rejecting same-generation row
reuse and mismatched capture owners. A frozen-overdub entry now orders capture
with private indexes and normalizes it as the final pass without changing the
shared audible arrays. Companion continuation now preserves historical Delete/
Create membership across the stage boundary through a bounded prefix scan.
End-to-end native bridge traces now cover 24 canonical history/capture variants,
a four-cycle open-note capture trace, and epoch cancellation/rebuild in mapping
and compilation. Ranked mapping/compilation now also feeds runtime admission
with live owner/generation checks retained through final selection preflight.
Executable uint64 bounds now compose ranked preparation, mapping and compilation;
the conservative full-horizon result exceeds the existing stop budget, so
completed sealed prefixes can now be reused without selection/read/history replay.
A runtime-owned prefix continuation now binds live revision/generation/length,
latches denial until the source changes, separates cleanup from restart, and
yields ownership to a frozen tail. The VisualCache owner now schedules at most
one prefix child across all tracks, with Q=32 and a global early return. Stop,
recording and existing active visual work retain priority; eligible overdub
tracks rotate without allocating missing runtime storage. A runtime coordinator now extends the owned prefix
through frozen-tail normalization, row preparation and canonical per-pass
companions in separate grains. It binds all child generations and retains
live source/epoch checks through ranked bridge preflight. Production stop routing,
atomic admission, admitted-tail/service proof and HITL remain open. Ranked-tail
bounds now accept an explicit maximum pass count and published-base event count;
C=2048/P=2/B=2048 with no history/companions still needs 7648 additional grains
at Q=32. Invalid quantum, out-of-staging event counts and overflowing legacy
contract totals now deny before stop admission. The order builder now checks
retained adjacency during fill and bypasses merge/copy only when the exact
comparator proves the input already ordered (64 grains for 2048 rows at Q=32).
Unordered inputs retain the merge path; worst-case bounds and production stop
limits are unchanged. Frozen-tail normalization now imports completed canonical
capture indexes into its private array in Q-bounded steps. Shared-order reuse
invalidates an in-flight import; after copying, audible ordering is independent.
Canonical and audible order bindings now also record that storage generation,
so discarding a later audible order cannot revive an older canonical completion.
Stale bindings deny resume/rewrite/readiness; tail admission uses private sorting
until a current canonical order is explicitly rebuilt.
When canonical order is already complete/current, capture normalization has a
196-grain bound at C=2048,H=0,Q=32 instead of 1604. The initial canonical sort is
still charged. The executable prepared-order aggregate now gives 6240 additional
grains for C=2048/P=2/B=2048,H=R=0,Q=32. The general 7648-grain aggregate and
legacy order/rewrite credits are unchanged. DEC-062 now approves a separate
provisional 6240-grain ranked-preparation allowance for continued integration.
Determine realtime reservations and elapsed stop latency with HITL once the
complete path can run; the old 3072 is not a product latency ceiling.
`PendingStopAdmission` now accepts an optional uint64 ranked-work bound at begin,
validates the 6240 allowance and combined counter capacity before mutation,
and tracks ranked credits separately. Finalization requires all three counters
empty; transport Stop preserves credits and changes epoch/destination. Existing
production callers still request zero ranked credits pending typed tail/bridge
routing; exhausting credits does not replace prepared-source readiness checks.
Reserved tail/bridge step entry points now bind the pending stop's frozen
generation, epoch and admitted quantum, charge one credit before one child,
and deny exhausted/stale reservations without advancing it. Completed readiness
queries consume no credit. Native end-to-end coverage uses these entries;
production stage dispatch, setup charging and atomic publication remain next.
A STOPPED-only typed oracle publisher now owns canonical baseline events and
pass origins together, upgrading flat same-identity views before bridge mapping.
It preserves the existing view on invalid baseline/coverage preflight and needs
no allocation. Track's all-stopped merged-event preparation now invokes canonical
baseline construction using idle prewarmed workers. It publishes typed origins
only when the complete canonical event sequence matches the existing oracle;
otherwise it retains flat publication. Temporary workers are released on success
or denial, without cancelling an already-owned prefix. This is synchronous
STOPPED throughput, never an active grain. Production ranked-stop stage dispatch
and atomic canonical/audible handoff remain open. A joint audible preflight now
requires AtomicTransfer phase, exhausted stop counters, matching command epoch,
PLAYING destination, explicit base/target identities and ready bridge/ledger/
cursor state. It is read-only and does not establish canonical companion-row
publication. Final bridge selection also rejects phase >= loop length instead
of accepting it through modulo coverage. Canonical companion staging is still
required before enabling the combined production commit. Loop-owned companion
history staging now prewarms a separate EditPass vector while STOPPED and copies
history metadata, Create payload events and prepared companion rows in Q-bounded
units. Prospective IDs do not advance nextPassId; canonical rows/revisions stay
unchanged. Source/storage/row-owner/ID changes hide the candidate. Logical discard
retains storage. The history handoff now swaps prebuilt canonical rows and the
snapshot companion-ID list, advances IDs/revision, and retains an immutable ID
receipt for the existing undo owner. Snapshot clear/replacement and append guards
reject stale candidates. Old history stays allocated until STOPPED prewarm.
A joint canonical helper now preflights the reserved capture pass and history,
transfers capture, then publishes history without a stale revision recheck.
It retires capture using scalar writes and preserves the one-revision-per-seal
convention. Track's prepared-stop entry now preflights exact canonical/companion owners,
source identities, stop epoch/credits/destination, cursor and ledger, then commits
canonical data and publishes the bridge without a second fallible selection.
Native fixtures verify matching identities and retained selected-bridge storage.
Loop's prepared-history continuation now verifies every staged sealed capture
event against the raw frozen canonical order in bounded event/chunk visits.
The joint handoff requires this proof; same-count payload/order mismatches and
missing/extra events deny publication. The rewrite cursor also binds its original
store. Stop admission now reserves canonical preparation separately from the 6240
ranked grains, rejects combined counter overflow before mutation, and requires
both counters exhausted for commit. An admitted step checks Q, frozen epoch and
owners before charging exactly one canonical grain; completed queries are free.
STOPPED prewarm now reserves both swap sides. After receipt consumption, a
bounded recycler retains allocations and restores preparation within the remaining
row/snapshot horizon. Two consecutive commits reuse the same buffers; exhausted
capacity or intervening canonical/mailbox changes deny reuse. Stop admission now
reserves post-commit history recycling separately, checks the combined credit
limit, and permits consumption only in DeferredCleanup after preparation finishes.
The joint commit requires and preserves that reservation; transport Stop also
preserves it. The native joint fixture charges setup and recycling while retaining
canonical revision and selected-bridge data. Track's recycling entries now bind
that committed Loop and preparation generation, charge receipt-consumed setup
separately, and enforce the admitted Q for each later grain. Wrong owner or
replacement preparation cannot consume the old reservation. Completed queries
remain free only while canonical/mailbox identity stays current. Loop now offers
bounded session undo-ID collection into caller-reserved private lists, preserving
the legacy cursor prefix and duplicate/order semantics. Session mutations invalidate
paused and completed collection. The maximum 2048-row/2048-companion collection
fits 193 grains at Q=32, separate from ranked preparation and recycling. The
collector now also appends the final capture and prepared companion receipt in
bounded stages, preserving the legacy final-pass inclusion and duplicate rules.
Receipt generation/lifetime and canonical identity guard paused and completed
collection. TrackUndo now prepares a private UndoEntry by setting metadata,
retaining the supplied before-snapshot and swapping the bounded ID outputs;
single-pass entries retain the legacy scalar representation. Preparation consumes
the collection without releasing vector buffers or existing snapshot roots.
Global publication and redo/trim retirement still need bounded integration.
DEC-063's accepted lifetime helpers now drain immutable snapshot graphs, persisted
snapshot/chunk payloads and complete UndoEntry resources in bounded units. A
private-stack trim continuation releases each entry before rechecking physical
pressure and shifts surviving entries one per unit. Shared snapshot readers remain
unchanged in per-grain native checks. Focused suites pass 37/37 and 43/43; full native passes 2069 with 1 skipped.
Firmware passes with RAM1 code 425736; strict OpenSpec and whitespace checks pass. These helpers require exclusive unpublished ownership
beyond a no-fail point; production locking, attachments, commands, reservations and
joint dispatch remain open. See the current BRIDGE-PROGRESS lifetime section.
A native pressure-feedback reproducer now proves that retaining removed roots
can change the surviving undo prefix (10 versus 8 entries in the controlled case).
The [undo publication reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md)
records the accepted DEC-063 protected transaction with bounded reclamation.
The latest deferred Undo/Redo request wins per affected Track and executes once
after finalization under the destination policy; Transport Stop stays immediate.
Feedback trimming remains unchanged. Implementation and native proof precede
runnable HITL. Focused pool-budget validation passes **18/18**; this review changed no
firmware behavior. Production dispatch,
receipt/undo sequencing and active snapshot-box refill remain open. This canonical staging work is separate from the
6240 ranked-preparation allowance. A required live storage-generation guard now rejects timeline
reset/revision reuse, disabled-history reclamation and deferred edit appends
that can relocate rows without a playback revision change. The marker advances
before reset releases chunks, reclamation moves rows, or append reallocates
history, and remains bound across frozen-tail handoff. Native lifetime and
scheduler policy coverage do not establish an on-device latency bound.
Empty-history normalization and empty-companion merging now bypass
the apply worker and its extra copies, with corresponding tighter bounds.
Per-pass bridge mapping now uses a bounded sorted origin index and binary
search instead of quadratic scans; its maximum Q=32 bound is 2564 grains.
Production atomic transfer/routing, remaining combined-history/companion parity, duplicate
mergeSequence parity, production lifetime enforcement, capture freezing,
and admission remain open. See the
[Create reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md).
See
[scope reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md).
Task 3.5 STOPPING MIDI-poll/save policy is verified. Bounded bridge compilation
and the production selected-bridge consumer retain cross-wrap open-note
closures in native tests; capture-side creation and the canonical/audible bridge
handoff remain incomplete. Private UndoEntry preparation validation and capture-serial build pass
(RAM1 code 425736, locals 6496). Final full native run: **2061 passed, 1 skipped**
(`/tmp/resumable-undo-entry-native.log`), including combined receipt handoff,
single/multiple-pass representation, buffer/snapshot retention and denial guards;
bounded
Delete and wrap-head selection now match the actual resolver in 1,000
generated differential fixtures, including retention masks and inferred loop
length. Maximum-size completion, cancellation, and restart/capacity guards pass.
DEC-059's revised pairing reference remains the oracle. Length overlap traversal now preserves shortening-before-deletion order in
bounded quanta with held actions; 500 generated cases match the actual Length
resolver when using its existing reconstruction and mutation/order primitives.
Reconstruction wrap queries now match the legacy helpers in bounded two-pass
lookups. Complete bounded pitch reconstruction now matches canonical spans and
projected/deduped note output, using prewarmed storage with stale-source and
partial-output guards. Bounded Delete/Length application and frozen batches now match sequential
reference application while retaining base provenance. Bounded bridge mapping
now preserves final canonical ranks separately from that provenance. The ranked
compiler and cursor consume them with a bounded immutable-base index sort;
combined capture/production integration remains open. Aggregate stop-grain/
latency proof, bridge mapping/publication, and production transfer remain open;
host timing fixtures **22/22**;
capture-serial RAM1 locals **6720**. No upload/HITL. Details and next integration
work: [BRIDGE-PROGRESS.md](../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md).

## Current branch

**`feature/overdub-wrap-snapshot-publication`** — 3.5 wrap-journal publication
forked from `feature/immutable-loop-snapshot-passes`. Parent:
**`feature/immutable-loop-snapshot-passes`** — forked from
`546169f66658b87c54e6166b79eba74628aa3d1e` for the immutable loop snapshot
architecture proposal. The maintained-LCR continuation from
`feature/overdub-stop-lag-stage-c` is not pursued on this branch. See
[`CURRENT_WORK.md`](CURRENT_WORK.md). Record and overdub `LoopSnapshot`
publication queue at stop and step from idle (`DEC-045`) in the current
firmware. Task `3.5` now queues bounded wrap journals and steps them during
PLAYING/OVERDUBBING after the first root (`DEC-047`). Playback still reads a
stale published window plus unpublished session passes until consumer
migration. OVERDUBBING idle does not run visual reconstruct. NOTE_EDIT close
stays synchronous.
Device PASS [`233308`](../../captures/session_20260825_233308.log): no
`snapshot_idle` after first publish until STOPPED; PLAY4 BPM 119.966–120.049.
Capture-only `snapshot_idle` telemetry exposed the root/queue boundary, and
device capture [`232633`](../../captures/session_20260826_232633.log) verified
that the PLAYING/OVERDUBBING rows before root publication were one in-flight
record construction with no queued successor passes. The final `pending=0`
row published the root; no later PLAYING/OVERDUBBING idle rows occurred.
4b.2c is closed without amending `DEC-045`.
Tasks `3.5`/`3.6` and 4.3 are closed. Remaining-interval yield proof is
parked (`kSnapshotIdleReserveYieldEnabled` is false). OpenSpec 4.4a–4.4c
are closed. 4.5a is closed: overdub stop holds the composed frame and
elevates session-exit Snapshot idle until the successor root is current
([`145633`](../../captures/session_20260908_145633.log)). 4.4d is closed:
NOTE_EDIT dirty paint/select uses the current snapshot as committed base.
4.4e Stage 1 is in tree: `resolveDisplayNotes(*snapshot)` copies
note-span membership. Device
Device PASS [`162207`](../../captures/session_20260908_162207.log)
questions 1–2 (open visual **98**).
Q3 [`170211`](../../captures/session_20260908_170211.log) earned Stage 2.
Stage 2 device PASS [`171444`](../../captures/session_20260908_171444.log):
current-root opens share the span NoteId set. Stage 3 device PASS
[`173531`](../../captures/session_20260908_173531.log): commit rematerialize
uses `copyNoteEditCommitBase`. Close bake PASS
[`174646`](../../captures/session_20260908_174646.log): empty-id exit does
not invent `NoteEditPassClosed`.
4.4f leftover NOTE_EDIT oracles are closed: current-root open skips
`assignMissingNoteIds(loop.midiEvents())`; E: rows undo/redo fallback
copies the opened snapshot root (`tryCopyNoteEditOpenedSnapshotEvents`).
RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Wrap-head vs last-Off pairing RC1–RC3 is accepted (native shipped;
[`185428`](../../captures/session_20260908_185428.log) RC3 analog PASS;
174646 recapture waived).
Close publish: `NoteRange` reuses the current snapshot pitch
([`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md)).
4.4g is closed: `validateAndCleanupMidiEvents` copies
`copyIdleValidateEvents` (current-root span-filtered snapshot events).
Overdub analysis current-root gather already skips pass flatten (4.4a).
RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` at `root_rev=7` `play_rev=7`.
[`194039`](../../captures/session_20260908_194039.log): NOTE_EDIT open
`current=1` `root_rev=8` **177** / `only_*=0`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1`; both overdub opens
`from=snapshot`.
4.4h Stage 1 shipped: `rebuildDisplayNotesInWindow` copies
`tryCopyCurrentSnapshotDisplayNotesInWindow` on a current root.
Device [`201106`](../../captures/session_20260908_201106.log): second
overdub open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`.
NOTE_EDIT open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME`
**137** after second stop through session end.
DEC-055 Stage 3 device PASS
[`221906`](../../captures/session_20260908_221906.log): first Move
`push,e00` then `resolve,9669,1,3,0`; `phase,setup` 303 µs. Stage 2 device PASS
[`220445`](../../captures/session_20260908_220445.log): first Move
`UNDO_WARM,push,e00` (1358 µs, 79 notes, kind=Move); `focus_snap` = 0.
Stage 1 device PASS
[`212850`](../../captures/session_20260908_212850.log). Option B rejected.
Hydrate 1 device functional PASS
[`224533`](../../captures/session_20260908_224533.log): select neighborhood
and first `push,e00` hold. Fast fader-2 / bar-sweep sluggishness is stacked
Move `GEOM_APPLY` on `hostdrain`, not a neighborhood fail. Hydrate 2–3
device functional PASS [`231008`](../../captures/session_20260908_231008.log):
`candidates_hit` then `candidates_miss`; first `push,e00` holds.
Hydrate 4a mover-only seed is in tree
([`232826`](../../captures/session_20260908_232826.log) /
[`233644`](../../captures/session_20260908_233644.log) Move/Pitch).
DEC-056 miss-path device PASS
[`000740`](../../captures/session_20260909_000740.log): close skips
stale-parent publish; overdub source shows `id=93 p=85`. Current-root
close [`000237`](../../captures/session_20260909_000237.log) `root_rev`
4→6. Hydrate 4a empty apply scratch made no-edit close rematerialize
and bake Deletes; [`001857`](../../captures/session_20260909_001857.log)
hung at `EditSelectNoteState::onExit`. Empty-store bake device PASS
[`002450`](../../captures/session_20260909_002450.log)
`NoteEditPass close skip empty-store bake`. Same capture left NOTE_EDIT
open into OVERDUBBING. Compose Stage 1 device PASS
[`112612`](../../captures/session_20260909_112612.log): capture-active
NOTE_EDIT paint always gathers (`includeActiveCapture`) and keys the
paint cache on `captureDisplayRevision`. Open visual **48**; in-edit
`DFRAME` **53 → 63**. Native **1795 succeeded, 1 skipped**. RAM1 locals
**7008**. HITL `--preset note_edit_overdub_compose` / stimulus
`STIM-NE-OD-COMPOSE-02` v1 (promoted TEMP-A). `01` remains paint-only.
Compose Stage 1+2 device PASS. Exit persist parked mover shipped
([`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json)
**13/13**; [`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md)).
First-note loop-end close commit is RC1
([`192140`](../../captures/session_20260909_192140.log) `ChangeLength` 80–2303;
watch HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce the store-pair form;
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md)).
`STIM-NE-OD-COMPOSE-03` last-bar beat-4 Shorten HITL device PASS
[`133714`](../../captures/session_20260909_133714.log). After that recipe,
[`134549`](../../captures/session_20260909_134549.log) Shorten `58` `2102–2111`
then LCR `e=2212` / reopen `empty_step`
([`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md)).
([`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md)).
([`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md)).
Do not start hydrate 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).
DEC-054 Stage 1 admission remains in tree; [`204458`](../../captures/session_20260908_204458.log)
failed the select-cadence gate. Parent 4.4 closed (inventory
2026-09-09). **4.5 SHIPPED** (2026-09-10): `base` / occupy / compose `02`
PASS. **4.6** Stages 1–6 **SHIPPED** / task checked off (derived-rep identity
freshness; §5 undo/load excluded; §6 materialize retirement remains). Stage 6
HITL `base` [`141547`](../../captures/session_20260911_141547.log).
(A→B@R native; RAM1 **7008**; native **1821/1**). Stage 2+ remain.
VisualCache stamp). Persistence and snapshot-identity undo (§5) remain
later. Hydrate 4b–5 and raise `N` stay parked.
The following publication-admission paragraphs retain historical intermediate
captures; their earlier “recapture remains open” wording is superseded by the
closed 3.5/3.6 evidence above.
Latest `teensy41-capture-serial` build restored RAM1 locals to **7008**
without USB-host ownership changes, so task `3.6` recapture is unblocked.
The follow-up admission fix allows queued session-exit snapshot work after
wrap-journal drain and permits the bounded Snapshot owner while tracks are
stopped but the transport clock remains running. Focused native coverage
passes; full native and device recapture remain open. Construction now
persists membership-copy, mutation, append, and event-sort progress at one
fixed-capacity membership page per admitted unit. The capture-serial build
remains at **7008** RAM1 locals; device `T_RT_reserve` recapture remains open.
Capture [`202635`](../../captures/session_20260827_202635.log) confirms journal
drain and eventual `pending=0`, but still records 79.169 ms and 74.453 ms
slices from whole-pass session-exit start. That start now uses a persistent
`CommittedEventRange` cursor and the existing journal caps so one admitted
unit builds at most one bounded pass-change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) published `root_rev=8`
when wrap journals drained while session-exit passes were still queued;
playback then dropped the first record pass. Wrap completion now waits for
queued passes, construction progress, and companion changes before
`storePublishedSnapshot`. Stage 1 STOPPED admission is closed on
[`221054`](../../captures/session_20260827_221054.log): Snapshot idle ran
after stop, the candidate stayed private, and publication occurred only at
`pending=0` / `root=0x70400ac8` / `root_rev=10`.
Stage 2 child timing is in tree on `snapshot_idle`
(`start`, `pass_change`, `step`, `finish`, `publish`, `probe`, `enqueue`,
`discard`, `emit`). Capture-serial RAM1 locals **6976**; native **1491/1491**.
Capture [`225659`](../../captures/session_20260827_225659.log) names the
224842 record-stop leftover as `discardPendingIfLoopLengthChanged`
(`discard=15262` on the 20.984 ms PLAYING idle). Exclusive leftover after
that probe is ≤ 39 µs. STOPPED session-exit `start`/`pass_change` reaches
19.996 ms. Publication closed at `pending=0` / `root=0x70431844` /
`root_rev=8`. The Stage 2 falsification review is recorded in
[`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md).
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): record-stop
PLAYING `discard=47`, first root `pending=0` / `root=0x703248cc` /
`root_rev=1`. Non-fit discard remains a content-sized destructor. Device recapture [`004119`](../../captures/session_20260828_004119.log)
names the 21 ms session-exit as `note_lookup` (86.9% of
`pass_change=21050`). Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max 857 µs, `pass_change` max 3.219 ms.
Wrap-tick derived reconstruction is removed from the wrap clock interval
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)).
Musical wrap stays on the clock. Rank 6 next-tick full-loop compatibility
gather is replaced by incremental overdub-seal admission when the one-shot
receipt proves completeness
([`playback_merged_compatibility_rebuild_clock_interval_bugfix.md`](../Plans/playback_merged_compatibility_rebuild_clock_interval_bugfix.md)).
`DEC-049` preserves that merged stream across `stopOverdubbing` so the
same incremental admission can run after stop, and admits Snapshot on 2
of every 6 deferred turns while a publication backlog is present. Device
Stage 2 PASS [`152513`](../../captures/session_20260831_152513.log);
Stage 3 drain PASS [`153113`](../../captures/session_20260831_153113.log)
(`root_rev=7` meets `playback_rev=7`). Live-record snapshot start walks
`capture.store` from a persisted index
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)).
Device [`190654`](../../captures/session_20260831_190654.log) closed the
copy/scan class; remaining `start` growth was `note_lookup`. That path now
skips `findNoteAtPitch` (`openNotes` only)
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)).
Device [`192422`](../../captures/session_20260831_192422.log) closed the
lookup class: `note_lookup=0` on all 32 live-record starts; after-first
`start` 167–233 µs. Remaining `idle_maint` spikes are display VisualCache
(wrap-stale + `from=passes`). [#41](https://github.com/Lytrix/MidiLooper/issues/41)
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log)
([`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
no wrap `dsz=0` while notes exist; no wrap `from=passes`; first overdub pass
stays on the paint frame (`183,111` → `184,180`; second wrap `249,246`).
Stage 1b device FAIL [`204132`](../../captures/session_20260831_204132.log):
STOPPED DNTE is short; NOTE_EDIT open rebuild then 291 notes / `presence=0`.
Stage 1c device FAIL [`205138`](../../captures/session_20260831_205138.log)
wrap 2 `205→271` then slice `260`. Stage 1d shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): idle Overlay fill
left longs after wrap 2. Stage 1e (in tree): idle rejects Overlay and
fills from current committed
([`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md)).
Native **1518/1518**. RAM1 locals **6944**. Device recapture required.
Stage 1f (in tree): last-resort STOPPED paint rejects Overlay
([`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md)).
Device gate [`213448`](../../captures/session_20260831_213448.log): first
STOPPED `DISP` must equal visualCache. Stage 1g (in tree): wrap-held
spans dirty On/Off bars only
([`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md)).
Device [`221925`](../../captures/session_20260831_221925.log): wrap `dcnt`
inconclusive on a 4-bar all-bar overdub; wrap 3 idle did not fill; STOPPED
**128 / 137** with `DNTE` 39 **144+256**.
Device [`222349`](../../captures/session_20260831_222349.log): wrap first
`DISP` drops then `slice_clean` restores; first STOPPED **159 / 31** then
`OverlayRejected` **159 / 166**; B3 held across stop; no per-note dump.
Stage 1h shipped `4238b422`: `Off@0` does not linear-steal a mid-loop On
([`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md)).
Stage 1i (native **1525/1525**, RAM1 **6944**): wrap/stop first paint
holds dirty cache
([`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md)).
Stage 1j (native **1526/1526**, RAM1 **6944**): playhead tails are
paint-only
([`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md)).
Device [`232910`](../../captures/session_20260831_232910.log): 1k wraps
2–8 hold; wrap 1 `104→92` after idle `append_resume` 89 is the grow-vs-prefix
hole (fix: compare to composed hold). Dump `reconstruct` 327 / `frame` 412.
First STOPPED **412 / 208**. 1k/1m device PASS [`002145`](../../captures/session_20260901_002145.log).
Occupy same-start hide is in tree
([`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md)):
reconstruct keeps both `noteId`s; display adopt/idle keep the shortest end.
Native **1533/1533**. RAM1 locals **6848**. Device recapture required
(`002145` `norow` 75/76; pitch 63 frame 96+288). 1l not in that log.
([`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md),
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md)).
Stage 2.1 (idle copies current `LoopSnapshotNote` membership) is in tree.
Stage 2.0 load enqueue is disabled after restore/adopt
([`023222`](../../captures/session_20260908_023222.log) rebooted on a loaded
`start` with `ld=1`). Title still exits on restore completion only
([`020111`](../../captures/session_20260908_020111.log)). After `boot_commit`,
VisualCache Stage 2.1 fallback fills from passes until a current root
exists. Session-exit snapshot publication is unchanged. Device stability
PASS [`024731`](../../captures/session_20260908_024731.log)
([`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md)).
Do not freeze an observed unit or the minimum observed `reserve_us` as the
`T_RT_reserve` guarantee. [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
records the 10,000 µs policy ceiling. Remaining-interval yield proof is
parked after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `kSnapshotIdleReserveYieldEnabled` is false;
yield helpers stay compiled. OpenSpec `3.5`/`3.6` are closed. OpenSpec `4.3`
is also closed: `LoopContentResolution` now exposes explicit stateless
`LoopSnapshot` query overloads; compatibility pass/index/checkpoint paths
remain until caller migration. Next is `4.4`, which requires a new phase
selection and gate. Construction bounds stay unchanged.
For open 4b.3 attribution, `TrackManager::updateAllTracks` now emits
capture-only per-track `track_update` maxima and over-budget counts alongside
the aggregate `tracks` timing; device recapture is required.
Display paint fallback and visual-cache fill read published snapshot events
when a root exists; compatibility gather remains the no-snapshot fallback.
A nonempty dirty visual-cache prefix does not authorize the paint window
([`235954`](../../captures/session_20260825_235954.log)). Live overdub keeps
the PLAYING committed handoff only while the paint window stays inside
the last gather ([`001048`](../../captures/session_20260826_001048.log),
[`001907`](../../captures/session_20260826_001907.log)). First gather
includes a short loop tail so a 20-bar loop does not omit the last 2 bars
([`002634`](../../captures/session_20260826_002634.log)). Overdub-stop paint
holds the composed frame — device PASS [`004934`](../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms). 4b.0 wrap/hold device PASS
[`092755`](../../captures/session_20260826_092755.log)
(`ODUB,stop,display` 21.4 / **11.1 ms** after wrap). **Now:** prepare
decomposition D1 **device PASS**
[`221655`](../../captures/session_20260826_221655.log) after 1A measure FAIL.
Next is D2 design session on
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)). Ring B/RC2 is
**closed.** Residual **4b.1b** lives on
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
§ *4b.1b* and is parked. Ring A firmware is not authorized. Ring B/RC2 shipped
the five-owner cursor (snapshot, visual-cache, wrap-append, persistence,
load/display) and RC2 coverage: refuse uncovered visual-cache reads; fall back
to committed-pass reconstruction. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings stay on the owner-boundary plan.
OpenSpec `4b.2` first-take length updates during RECORDING remain in tree;
live-record snapshot append starts now admit at most one completed bar of
new range per start so setup cost stays smaller in realtime windows;
first-take display keeps continuous playhead length (host implement; device
verify open). Ring B now owns the five-way deferred idle turn; `4b.3`
post-stop `usbdisp` /
`load_frame` / `late_on` is now in tree (device verify open);
`4b.4` overdub `DisplayResolveLiveCapture` owner refinement is now in tree
(device verify open). `DEC-046` accepts the round-robin admission and
representation-coverage direction. Ring B/RC2 firmware is **closed**.
RC4 repair and Ring A remain separate.
Slice 1 telemetry is in tree and device-validated by
[`161834`](../../captures/session_20260826_161834.log) and
[`162522`](../../captures/session_20260826_162522.log): the dominant
long-loop rebuild destination is `T0/S6`, with `T4/S0`, `T6/S0`, and `T2/S2`
also observed; `162522` directly pairs the repeated compatibility gathers with
`T0/S6`. The playback rebuild `<count>` is rebuild-call count before one-shot
emission, not distinct-destination multiplicity or an eligible-set count.
RC4 question 3 is closed from code: `adoptPersistedSnapshot` /
`restorePassesSnapshot` do not publish a `LoopSnapshot`. Question 1 is
closed by [`165024`](../../captures/session_20260826_165024.log) and
[`171159`](../../captures/session_20260826_171159.log):
all 16 `cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`.
The measured internal-first spill and nested-clock mechanisms are ruled out.
`cer_recon` is in tree to split internal-heap `filt` from PSRAM-first
`span`/`proj` and to record IntervalTimer ISR `itickus`.
[`170149`](../../captures/session_20260826_170149.log) rebooted at first
clock after Record; probe counters are now file-scope volatiles.
[`171159`](../../captures/session_20260826_171159.log) closes question 1:
`filt` stays flat, `span`/`proj` take the 7.9x, `itickus=0` on every row.
The follow-up `cer_snapshot` formatting probe was removed after
[`172305`](../../captures/session_20260826_172305.log) rebooted at record start.
`playback_build` now carries a deferred `path` field (`snapshot`,
`compatibility`, or `note_edit`) so later investigation captures can identify
the representation behind each emitted maximum sample without formatting on the
clock/interval path. The existing one-shot remains an aggregate: `count` is all
rebuild calls before emission, not distinct destinations.
Device capture [`202654`](../../captures/session_20260826_202654.log) validates
the split: 82 emitted samples contain 74 `compatibility` and 8 `snapshot`
paths, with no `note_edit` samples. `T0/S6` remains compatibility-backed
(maximum 325.541 ms), while `T4/S0` uses the published snapshot
(maximum 358 us). RC4 question 5 is closed as that `T0/S6` compatibility
replay; ignore it for snapshot-path sizing. **G1 is satisfied:**
`representativeRebuildWall` = 358 µs; `prefetchTicks` = 272; horizon
unchanged. Ring A firmware is not authorized.
[`205447`](../../captures/session_20260826_205447.log) `T0/S5` rebuild is
334 µs; the 377 ms gap at that switch is `idle_maint`, not the new-slot
rebuild.
[`172802`](../../captures/session_20260826_172802.log) confirms first-clock
recording and continued overdub/stop operation after removal.
Historical checkpoint: task 4.3 was still skipped at this capture.
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING/PLAYING with repeated
`snapshot_idle` slices and `midi_gap` spikes before cadence alternation.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long visual idle reconstruct bursts; host
refinement now constrains/gates that visual fallback during contention.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms, 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows; host refinement now broadens unpublished visual constraints to
all pending unpublished work and caps each live-record append-start range.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
issues remain: same-turn `snapshot_idle` + `PERS,work` overlap is still
visible (`snapshot_idle us=26823` then `PERS payload_slice=20806`), large
OVERDUBBING windows remain (`midi_gap=236090`, `usbdisp=143649`,
`load_frame=25638`), STOPPED still hits `midi_gap=430552`
(`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`), and overdub
display still spikes (`timing_max,DisplayResolveLiveCapture,215619`) while
`ODUB,stop,display` remains 21.9-50.7 ms. In tree: same-turn lane now blocks
deferred save after snapshot on the same loop turn; recapture pending and
4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer showed
`PERS,work` while `snapshot_idle` was active (same-turn overlap not observed
in that run), but 4b.3 and 4b.4 owners remained:
post-overdub PLAYING `midi_gap=175230`
(`usbdisp=121603`, `load_frame=14728`, `late_on=184364`), STOPPED
`midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`, `late_on=30491`),
and `timing_max,DisplayResolveLiveCapture,157754`. In tree for 4b.3:
post-overdub drain now polls MIDI during `runDeferredLoadAndDisplayFrame`
between display/load slices; recapture pending.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (example:
`snapshot_idle us=7360` then `PERS,work 86`), and OVERDUBBING spikes remained
(`midi_gap=200549` with `usbdisp=129828`, `load_frame=23600`, `late_on=26036`;
`midi_gap=196173` with `usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were in the
26-52 ms class after stop-display lines (`ODUB,stop,display` 19.6-51.4 ms).
In tree for 4b.1: lane arbitration now includes RECORDING pending snapshot
work (not PLAYING/OVERDUBBING only), and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection; recapture pending.
[`134438`](../../captures/session_20260826_134438.log) showed no `PERS,work`
entries (`0` matches) while `snapshot_idle` remained active, so same-turn
snapshot/persist overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now reduces per-call dispatch batch size from
`128` to `32` while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns bound `usbdisp` burst size before the next poll.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; recapture
pending.
[`135317`](../../captures/session_20260826_135317.log) again showed
`PERS,work=0` while `snapshot_idle` remained active (4b.1 still open pending
repeated recaptures). 4b.3 remains open: first stop cycle held
`ODUB,stop,display=15428`, but a later `midi_gap=181392` was logged after
PLAYING re-entered OVERDUBBING; later cycles were `midi_gap=42033` and
`midi_gap=22132`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips overdub paint
snapshot gather+reconstruct when `visualCache` already has notes, preferring
held-layer/window-filter branches to bound resolve cost. Verification:
`pio run -e teensy41-capture-serial` PASS with RAM1 locals `4608`, and
`pio test -e native` PASS (`1472/1472`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but first-bars OVERDUBBING still reached `midi_gap=133468`. 4b.4 improved
strongly in this run with `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` versus nearby PLAYING `DISP`
totals `240`/`253`/`308`) and restamp miss evidence
(`seal_publish_span_cache_restamp ... invalidated_missing_id` together with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS;
recapture pending.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and wrap
reopen still showed `DIAG,lcr,src,why=open ... notes=0` after a restamp miss.
In tree: source-cache fallback now also seeds from non-empty dirty
`visualCache` when prepared spans miss during an overdub session.

**Queued-dest visual cache:** `invalidateForSlotChange` leaves a PLAYING queued
destination `visualCache` in place. Firmware in tree; HITL open. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).

**Boot title RAM1:** `cccbeaa` margin regression fixed — DMAMEM cold globals + `assignMissingNoteIdsToNoteOnsInChunkIds` in `.cpp`. Device PASS [`161855`](../../captures/session_20260816_161855.log). Investigation frozen: [`boot_title_loadloopjob_idle_slice_investigation.md`](../Plans/boot_title_loadloopjob_idle_slice_investigation.md).

**NOTE_EDIT entry while overdubbing:** `openNoteEditSession` calls `stopOverdubbing()` first — live capture commits before rematerialize (quick fix).

**NOTE_EDIT lengthened loop select:** committed-content span for bracket/nav; skip detailed-window filter on selectable inventory. Fixture [`015731`](../../captures/session_20260814_015731.log).

**Overdub + NOTE_EDIT exit bake:** device PASS [`025322`](../../captures/session_20260814_025322.log) — `rows=1 saved=1`, session flats 234 held. Native `dropUnrequestedSessionStoreDeletes`. Stage 3 unblocked.

**OLED first-frame mismatch:** RC3 device PASS [`014553`](../../captures/session_20260814_014553.log). Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

**Loop content-only history (DEC-035):** **Archived** 2026-08-14 — OpenSpec `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log).

**NOTE_EDIT leave-restore painted span:** RC1 native shipped; device gate open — [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md). [`200154`](../../captures/session_20260813_200154.log) Restore 5 `720–2255` / select `DNTE` 1535; same-loop [`193838`](../../captures/session_20260813_193838.log) first-select paints length **47**.

**NOTE_EDIT mover wrap-length jump:** RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log) — [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md). No `2351`/`2975`. Note 14 `2256–2304` stays on the wrap-stub plan.

**NOTE_EDIT note-off pairing LIFO:** native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS — [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md). @45.971 seals five overlap rows plus the mover (`canonical=7 pre_commit=7`).

**NOTE_EDIT edit-pass replay row payload:** native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS — [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md). `Length 115 48→287` and `Length 115 1008→1103` seal and hold. Rows replay as stored.

**NOTE_EDIT overlap shorten commit seal:** native shipped; device gate open — [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md). Deselect commit dropped the overlap `Length` row on a parked mover; [`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` then @166.848 seals `Length 10 960–1247` against note 14's focus. Slice B of the resolver contracts plan **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was the correct truncation.

**NOTE_EDIT Length replay loop-boundary:** native shipped; device gate open — [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Persisted `ChangeLength` 14 `2256–2304` replayed as a wrap; same-pitch notes shortened to 2255. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log).

**NOTE_EDIT wrap-stub commit:** RC3 native shipped; device gate open — [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). RC2 device FAIL: 14 still `2256–2304` when cache paints a non-zero span. RC1 withdrawn (67.824 min-length stay-hidden).

**NOTE_EDIT display unification:** Stages 1–2 and 5–9 shipped — [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md). Device [`192007`](../../captures/session_20260813_192007.log): 45 Hide/Restore `1440–1511` (not 2160); 76 as target still `840–2160`; first NOTE_EDIT `DISP` 59/60; resolve 16.8–52.3 ms. Paint gap and undo-warm stay open.

**Active work:** Occupy clock same-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log) ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)) — `n=0 a=1` = 0; `n=1 a=0` = 0. Parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4. Pin [`233247`](../../captures/session_20260818_233247.log) was 11. Interval catch-up in tree. Merged-capture gather **FROZEN**. USB occupy `playMidiEvents` catch-up HITL **FAIL** [`214856`](../../captures/session_20260818_214856.log) **reverted**. Loop-head storage-0 occupy HITL **PASS** [`203948`](../../captures/session_20260818_203948.log). Wrap-S catch-up HITL **PASS**. Occupy reads `ActiveNoteLedger` at `currentTick` ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Occupy lookup: `ledger.noteId(midiChannel, pitch)`. Phase 2 **PASS** [`040236`](../captures/session_20260818_040236.log). PLAYING/STOPPED/MUTED HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)). **Phase 4 in tree**. Phase 3 1-bar occupy **PASS** [`121933`](../captures/session_20260818_121933.log). Stage 1 `from=span` HITL **parked**. Do not shrink `kOverdubSourceWindowBars`. Queued (no firmware): [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md). **NOTE_EDIT hydrate** queued ([`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)). **Playback gather** Stage 1 hooks landed. Wrap-move persist parked [`201446`](../captures/session_20260816_201446.log).

**Merged to `dev`:** PR [#35](https://github.com/Lytrix/MidiLooper/pull/35) LoopContentResolution prototype (`be0e05f`); PR [#30](https://github.com/Lytrix/MidiLooper/pull/30) overdub overlap; PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) Stage 5a-1/5a-2 + display RC4–RC5.

**Stage 5a-3:** **`pool_alloc` proof abandoned** (2026-08-12) — see [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](../Plans/long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

**`chore/firmware-ownership-lifetime-review`** — P0/P1 review **closed** (manual MT gates PASS); Phase 5 + layered **`base`** **parked**.

**`feature/deferred-lazy-load`** — Phase B archived `openspec/changes/archive/2026-07-19-deferred-job-scheduler/`. Specs: `openspec/specs/deferred-job-scheduler/`.

*(Parallel: **`feature/persistence-work-queue`** @ `8b3f93e` — B1–B5 on `dev`.)*

### Firmware lines

| Line | Branch | Role |
|------|--------|------|
| v1 | `main` | Frozen — 16×2, 2-button looper |
| v2 | `midi-faders` | Frozen — iPad/DROID, no PSRAM required |
| v3 | `dev` | Active integration |

See [`docs/BRANCHING.md`](../BRANCHING.md).

## In flight

- **§36 companion admission + STOPPED first-paint:** A+B+C closed. Device PASS [`165107`](../../captures/session_20260821_165107.log) / [`183229`](../../captures/session_20260821_183229.log). Later (parked): bound `applyDeferredVisualCacheWrapAppend` (1.13 s), attribute 5 s STOPPED idle. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Boot title `boot_commit` display stall:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — USB 11.484 s, first `DISP` visual=2638, `boot_commit=288557` (289 ms). Prior gather was `161148` / `154353` (~70–75 s, visual=0) from `onBootSlotLoadComplete` wiping the idle-built cache. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).
- **Boot title startup stall hardening:** capture [`161410`](../../captures/session_20260820_161410.log) stops after `DisplayManager: Display buffer displayed` and never reaches boot load logs. `Looper::setup` now uses bounded SD bootstrap retries (`3` attempts, `50 ms` delay) and an explicit empty-runtime fallback when SD stays unavailable, preventing title-screen startup stalls from requiring repeated manual reboots. Native verification pending.
- **Overdub-stop seal lag optimization:** captures [`163904`](../../captures/session_20260820_163904.log), [`164944`](../../captures/session_20260820_164944.log), [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`171246`](../../captures/session_20260820_171246.log), [`171909`](../../captures/session_20260820_171909.log), [`172553`](../../captures/session_20260820_172553.log), [`173206`](../../captures/session_20260820_173206.log), and [`174941`](../../captures/session_20260820_174941.log) keep the stop owner on `companion_seal` while `mark_range` remains low. Deterministic follow-up landed: overdub-stop path uses `SC_REC_FLUSH_PENDING_REVTS(8)` in `Track::stopOverdubbing` and in-edit `handleNoteEditFold`, removing variable USB serial flush work from the critical path before display refresh. Telemetry-visibility follow-up landed: `ODUB,stop` lines are Tier-A in `CaptureLineTier` so stop-stage lines remain visible under ring pressure with `flush(8)`. Display-specific follow-ups are reverted per user request. Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425084 / locals 4768`); HITL re-measure pending — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Idle-gather edit apply Phase A:** `applyNoteEditPassSequence` now performs strict contiguous `Velocity`/`Pitch` update collapse with barrier-preserving execution; non-collapsing baseline parity is covered by dedicated deterministic and randomized equivalence tests in `test_edit_apply`. Validation gates passed (`pio test -e native`, `pio run -e teensy41-capture-serial`) with RAM1 still within budget (`RAM1 code 424668 / locals 4640`) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay:** captures [`165534`](../../captures/session_20260820_165534.log) and [`165956`](../../captures/session_20260820_165956.log) show early-start delays (`Live Overdub -> Overdubbing started` ~2102-2249 ms) and `ODUB,stage,begin_capture` spikes (`2194189 us`, `2248843 us`). Root cause is start-path fallback to full-loop materialize in `Loop::rebuildOverdubSourceSpanCache` when prepared spans are unavailable. Stage now restores overdub-session deterministic fallback (reuse prior cache notes during active sessions; full materialize kept for non-session paths). Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425020 / locals 4768`); waiting on HITL re-measure — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay (closed):** HITL [`170724`](../../captures/session_20260820_170724.log) confirms the restoration removed the 2.2 s start stalls: `Live Overdub -> Overdubbing started` is now 2-8 ms and `ODUB,stage,begin_capture` is 8198 us / 2150 us. Same run exposes a remaining stop long-tail cycle (`ODUB,stop,seal=310980 us`, `flush=312270 us`, `display=320406 us`, `overlap_hold note_offs=100`), while `DIAG,seal_companion_batch` remains bounded (`rows=5 sealed=5 us=84`). Start regression is fixed; stop-path lag is still open — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start reboot (fault now read; repro conditions narrowed; hang reproduced):** USB drops right after `[TRACK] Overdubbing started`. Present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), i.e. before the 2026-08-20 removals, so commits `4af158a`…`6ff4142` are reverted and the tree is byte-identical to `28cf6e3`. **Two failure modes under one symptom, both memory exhaustion.** (a) *Fault:* the 2026-09-17 00:39 session's SD crashlog reads `DACCVIOL` at `Accessed Address: 0xE00` with `Code was executing from 0x6002C97C` = `Loop::tryGatherPublishedSnapshotEventsInWindow`. **The `0xE00 == 128 * sizeof(MidiEvent)` stride argument is withdrawn (2026-09-17, step B):** `sizeof(MidiEvent)` is **20 bytes** on device, measured with the `teensy41-capture-serial` toolchain, so the address does not decode to element 128 of a `MidiEvent` store; 28 is `sizeof(PlaybackBridgeRecord)`, and the faulting object is unidentified. The "snapshot-gather fill through a lost destination base" attribution is therefore **unverified** (the landed gather guard remains useful defence in depth). `ExternalMemoryFirstAllocator` aborts rather than returning null, which is the part that still stands. (b) *Hang — reproduced:* an 8-bar record plus overdub on track 4 slot 1 reached `[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and went permanently silent with USB still enumerated, leaving **no crash record** (`crashlog.txt` unchanged, next boot `crash=0`) — `abort()` raises no `CrashReport`, which is why this has only ever shown up as a silent hang ([`hang-8bar-track4-slot1-01`](../../captures/hang-8bar-track4-slot1-01/session_20260917_094002.log)). Internal heap was already `free=68 used=393 of 461 KB` at 127.7 s uptime, ~212 KB down from boot, before that record began. User's repro conditions: several tracks already playing loops, then an 8+ bar pass with overdub. Diagnostic landed SESSION_CAPTURE-gated (gather destination guard + `displayManager` RAM2 tail canary, `free for local variables` 6752 → 6720). Native **2192 cases: 2188 passed, 1 skipped, 2 known unguarded reentry failures**; RAM1 locals **6720**; instrumented firmware flashed. **Both boundaries now deny instead of aborting** (overdub via `admitOperationMemory`, record arm via the required prewarm's own result - `Track::startRecording` returns before any mutation, and a missing runtime slot is a denial too). Device [`arm-admission-01`](../../captures/arm-admission-01/session_20260917_113447.log): one `op=companion_history_reserve`, one `op=record_start`, zero `Recording started`, zero `FATAL`, device alive. Native **2193 cases: 2189 passed, 1 skipped, 2 known unguarded reentry failures**. **All five are now investigated, and step E is implemented (DEC-065).** Loaded-set 8-bar rerun [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log) closed the exhaustion RC: zero `FATAL`, zero `Recording started`, device alive, playback continuing, one `op=companion_history_reserve` + one `op=record_start` denial, **arm delta 1,240 KB**, heap 64-76 KB at the refusal. The audit shows the seven prewarms are one concurrently-live working set (not shareable) and that `sizeof(MidiEvent)` is **20**, not 28, which withdraws the `0xE00 == 128 * sizeof(MidiEvent)` crashlog attribution. Step E then made staging admission-provisioned: boot PSRAM headroom **+1,189 KB** (`tag 3` = 0 at boot), arm delta 1,469 KB including both staging roles, and the point-of-use gate now treats the probe as the authority for the `InternalHeapFirstAllocator` history reserve instead of vetoing on the heap floor while ~1.27 MB of pool was free. Result on the same recipe ([`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log)): **0 denial, 0 refusal, 0 FATAL**, 128 record notes, 16 overdub events, and the deferred stop completes `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` at 50.667 s. Next priority (user-directed): **HITL oracle repair, then the bridge tail.** C3/DEC-066 is closed as validated. The oracle repair is landed: the deferred stop (`OVERDUBBING -> OVERDUB_STOPPING -> PLAYING`) is first-class in the transition counts and the phase pairing, duplicate observer `ST,Track` edges are collapsed (they doubled every count and mis-paired a successor interval to the predecessor's stop), and a wall-clock-proxied stop reports its length instead of failing it. Device [`oracle-fixed-01`](../../captures/oracle-fixed-01/session_20260917_125305.log) reports **PASS**: deferred hop counted, overdub phase 16/16 notes (was 0/0), proxy-paced 9,216-tick length a report, SEVT/cleanup/display green, only an advisory `overdub_first_note_too_late`. Host tests 26/26, 30/30, 25/25 green. OPEN: the **successor-reentry device case** is scoped but not built - with the repaired oracle the canonical second-overdub run now PASSES ([`successor-reentry-06`](../../captures/successor-reentry-06/session_20260917_130431.log), two overdub intervals, both stops counted through the deferred hop, SEVT/display green) but it is sequential: measured OD1 stop 41,064.8 ms -> OD2 start 49,713.4 ms, an 8,648 ms gap against the predecessor's ~1.2 s deferred window, and removing the post-stop undo/redo does not change it. Needs a millisecond-scheduled successor press plus a verifier predicate that the successor starts before the predecessor's `bridge_ready`. Also found: an aborted run can wedge the deferred workspace save (`Persistence drain stuck after 256 iterations`, 158 KB pending) and then **every** later run fails its clear precondition until the device is re-flashed - the device does not reset on serial open and a 1200-baud touch did not reset it; recovery is `pio run -t upload`. The bridge tail is **profiled**: 490 deferred-stop grains, 68.0 ms of total grain work in a 1,068 ms window, 466 of the grains and 65.4 ms of the work in `CompanionBridge`, issued at a 1.2-3.2 ms cadence - so it is **cadence-bound, not work-bound**, and any optimization DEC would be about the deferred-stop owner's turn cadence/per-turn grain budget argued against the MIDI-boundary budget, not about the bridge algorithm. `CompanionHistory` is down to 4 grains (was ~2,030 Trim units), so C3's work shaping shows in the profile. Not pursuing C4, event-horizon ceiling reduction or seal-phase reserve. Previous C3 summary:  Per-owner capacity derivation replaced "2048 of everything": the companion-row reserve is derived from the loop's canonical payloads (clamped to the existing 2,048 ceiling), `RuntimeCaptureCompanions` now admits against the rows a frozen change set can produce rather than its whole vector, and the prepared history sizes its working rows to the interval's actual demand, removing the `Phase::Trim` walk (~2,030 units, ~64 prep turns per stop). Measured against the step-E baseline on the same loaded-set 8-bar recipe: arm delta **1,469 -> 1,315 KB**, `tag 2` **790 -> 715 KB**, stop press -> `bridge_base` **220 -> 112 ms**, stop-window allocations **0**, SEVT/canonical unchanged and correct, **0 FATAL / 0 denial / 0 refusal**. `bridge_ready` is unchanged at ~1.18 s because the bridge map/compile dominates it, exactly as the per-owner analysis predicted. Native **2196 cases: 2192 passed, 1 skipped, 2 pre-existing failures**. The event-horizon owners remain ceiling-bound by design (their interval term is the staging admission bound and their reserve is taken at the arm); the maximum-envelope case is fixture-covered on a dense loop rather than device-covered. **C4 is not being taken** - after reclaiming 1.19 MB at boot, giving up cross-track deferred-stop concurrency for ~228 KB is no longer attractive. Separately: the host-side HITL edges (`OVERDUBBING->PLAYING` vs the deferred `OVERDUB_STOPPING->PLAYING` path, the wall-clock bar-sync proxy and `overdub_first_note_missing`) need repair against the deferred state machine, independent of the memory work. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub session index reboot undo:** HITL **PASS** [`003854`](../../captures/session_20260820_003854.log) — reboot `entries=2` `kind=1`; one undo 112→14 — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Cleanup branch telemetry/fader compile-gating:** `chore/cleanup-codebase-tidiness` stage shipped — telemetry-only timing work gated by owning feature macros; `MidiHandler` motor-fader USB-host service helper now feature-gated to `MIDI_USB_FADER_PROBE_PASSTHROUGH`; verification passed (`pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial`) with zero warnings.
- **Overdub-stop handoff flash:** dirty `visualCache` must not overwrite the RC5a composed frame after overdub stop. HITL **PASS** [`001925`](../../captures/session_20260820_001925.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Display window follow readiness:** committed paint filters `visualCache.notes` for the moving 16-bar window plus lookahead; source-view is not display authority. Native **1390/1390**; RAM1 code **425276** / locals **4768**. HITL **PASS** [`000553`](../../captures/session_20260820_000553.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Companion-only projection fast path:** Full companion projection cache is admitted only while `capture.phase == CapturePhase::Overdub`. STOPPED/boot gather uses windowed apply. Mixed companion+non-companion rows keep legacy ordered replay fallback. Stage A on [`165107`](../../captures/session_20260821_165107.log) keeps Overdub `tracks` at 190 ms (was 136 s on [`162928`](../../captures/session_20260821_162928.log)).
- **Overdub participant discovery:** `feature/overdub-participant-discovery` — occupy clock same-tick Off-before-On HITL PASS [`001021`](../../captures/session_20260819_001021.log); parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4 `n=0 a=1`; USB `playMidiEvents` catch-up HITL FAIL [`214856`](../../captures/session_20260818_214856.log) reverted; wrap-S HITL PASS; loop-head storage-0 HITL PASS [`203948`](../../captures/session_20260818_203948.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Codebase consistency:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) **NOW** — Phase 4 TrackManager **shipped** PR #22; next extractions in [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)
- **StorageManager TU remaining:** **Merged** PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08); closeout [#16](https://github.com/Lytrix/MidiLooper/issues/16)
- **Firmware ownership lifetime review:** **Closed** 2026-08-06 — [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md); layered **`base`** HITL **parked** for dedicated refactor
- **HITL CLI rebuild:** Phase 3 — layered presets locked to `base` + `edit_full`; layered **`base`** device PASS deferred to dedicated HITL refactor
- **Edit-session-action-geometry:** **Archived** 2026-08-05 → `openspec/specs/edit-session-action-geometry/`; Phase 5 HITL matrix parked (`m8_edit_note_edit_hitl_automation_refinement.md`)
- **Note-edit control-surface split:** **complete** on `chore/note-edit-control-surface-split` — Phases 0–8; see [note_edit_control_surface_split_refinement.md](../Plans/note_edit_control_surface_split_refinement.md)
- **Long record display freeze:** queued — [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md); evidence [`012342`](../../captures/session_20260812_012342.log)
- **Next product slice:** loop length during overdub queued — [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)
- **Archived this branch:** OpenSpec [`deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/) Phase B — gates B.1 [`231510`](../../captures/session_20260718_231510.log), B.3/B.4 [`022107`](../../captures/session_20260719_022107.log); specs in `openspec/specs/deferred-job-scheduler/`
- Memory reclaim + boot/display stack — **merged to `dev`** (PR #4)
- **Hygiene (`chore/codebase-hygiene-sprint1`):** safe debt **complete** — see [`codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md); sprint plans Status Done + [README index](../Plans/README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| `3e9253e` | DEC-029 archive + spec sync + Phase 8 closeout | DELIVERABLE_TRACKING, PHASE8_CLOSEOUT |
| `15c5750` | Playing move/length `refreshPlaybackPreview` | note_edit_playing_move_audition_bugfix |
| `7af8671` | DEC-030 §12 R1–R5 orthogonal state | note_edit_resolver_authority_contracts_refinement |
| `5af41c7` | P1 fold wrap unify; NOTELEN exit bracket; HITL serial proxy | firmware_ownership_lifetime_review |
| `85ae7d6` | P1 undo routing docs (session-gated **E:**) | LOOP_MIDI, ARCHITECTURE_RULES |
| `d87d0c6` + [`010126`](../../captures/session_20260718_010126.log) | Windowed display + queued countdown; device gate PASS | boot_load_windowed plan |
| `8b3f93e` | Sync-drain budget, clear-slot restore, post-reboot undo save | persist queue plan |
| `7a89f03` | Retire monolithic deferred save; work queue (B4) | persist queue plan |
| `b3d066f` | `stepPersistenceWorkItem` scheduler hook (B3) | persist queue plan |
| `3143ef0` | `StorageManager::admit*` API (B2) | persist queue plan |
| `736fa33` | NOTE_EDIT move/pitch bracket — mover NoteId lookup | CURRENT_WORK |
| `0f086f4` | Split-focus slot switching + loop-end commit (DEC-025) | Loops.md |
| `f6b496a` | Slot short-press deferred-restore fix | — |
| `346f7ce` | Playhead projection + undo geometry restore | — |
| `b1d3259` | Loop-scoped undo display (DEC-024 Phase 1) | loop_undo plan |
| `50ad01b` | Cold boot + USB Host defer + slot scan hardening | DEC-021/022 |
| `e40f26c` | DEC-020 Phase 4 mid-pass persistence writer | DEC-020 |
| `f0ee520` | DEC-020 Phase 3 cooperative scheduler HITL | DEC-020 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

**2026-09-24 — the current working state.** Two changes carry this branch:

1. **`runtime-working-set-ownership`** — its native scope (P0/P1) plus **S2b.8** and **S2b.7** is complete;
   **every remaining gate is a device leg**, and all of them are blocked by the restored library being unstable
   across boots. Reported native line: full native **2,352 / 1 skipped / 0 failures** (2026-09-24, after
   S2b.7); RAM1 `variables 96,192 / code 391,080 + padding 2,136` (`padding 840` is the enforced
   floor). **No *currently eligible* native task remains open in this change** — the stage order is not
   bypassed merely because hardware evidence is blocked. (The current tree, which also carries Packet A's
   cases, reports full native **2,374 / 1 skipped / 0 failures**.)
2. **`restored-library-stability`** — **PROPOSED 2026-09-24; Packet A (observation-only firmware plus
   read-only analysis, 1.0–1.5) is COMPLETE, native- and device-verified read-only 2026-09-24; Packets B/C
   and the 4.x handoff/closeout remain open.** It owns the instability that blocks (1):
   the set of slots observed committed after a reboot is not reproducible **as measured**. It reports a
   per-slot observed state (SD payload + token, boot enumeration, request state/wait/terminal with full
   identity, committed content, selection) with **four distinct quantities** and an explicit observation
   **window**, through a **bounded on-demand observation** with a declared owner and a service point that runs
   **independently of normal load admission**, SD-read
   admission and an **invalidation/restart** rule covering request-generation change, set-epoch change **and
   content revision change** (a publication installs content under the same request identity, so it changes
   neither; content fields are never carried forward), independent of the
   settled census gates; the observed **current state** (`empty` / `populated` / `pending`) is reported
   **separately** from a **nullable historical cause** (`catalog omission`, `restore
   refusal`, `intentional eviction`, `content loss`, `ambiguous`, `unknown`) with evidence and precedence —
   `empty` is never a cause.
   One repeatable fixture inside the already-approved scratch range (track 5 slot 1, track 6 slots 1–8),
   stability proven by a **complete per-slot manifest comparison**; observation-only firmware first; any CLEAR needs **per-slot
   owner authorization**. It also carries the **S2b.7 device confirmation**, independently reportable. When
   the library is stable it **hands back** to (1) for S1 and the S2b device gates, then S3 — it does not
   absorb them.
3. **`deferred-slot-restore-liveness`** — **ARCHIVED 2026-09-24** as
   `2026-09-24-deferred-slot-restore-liveness`, with a reduced, accurately described scope. It delivered
   restore **preparation** under transport, request identity with named waits and four terminals,
   pre-publication stale-identity rejection, the post-restore census checkpoint, a content PASS, and CLEAR's
   permitted completion branch. It did **not** deliver drain-driven restore advancement (now
   `runtime-working-set-ownership` **S2b.7**, bound by
   `resumable-midi-boundary-playback-maintenance` **task 3.3**) or a runtime set change (now **S2b.8**,
   landed as external resets), and it did **not** by itself unblock CLEAR while another track keeps playing.
   Planning artifacts: the archived change's `proposal.md` / `design.md`.

**2026-10-02 — `windowed-playback-generation` CREATED, planning artifacts only; not implementation scope here.**
The sibling this branch's `runtime-working-set-ownership` extracted at `design.md:812` (**phase F**, S2.9) now
exists: [`openspec/changes/windowed-playback-generation/`](../../openspec/changes/windowed-playback-generation/proposal.md)
carries `proposal.md`, `design.md`, `tasks.md` and a new-capability delta (`windowed-playback-generation`,
`openspec validate --strict` valid). It takes over the acceptance criteria written for it (`S4C1` AC-1…AC-9 and
`S4C45` F-1…F-8) and the four interfaces S2.1 needs. **Its own contract and trigger record landed 2026-10-02** —
**DEC-108** plus the change dir's `ARCHITECTURE-REASSESSMENT.md`, which is what RWO's
`IMPLEMENTATION_CONTRACT.md` §4 requires of the sibling before Apply — and **nothing is implemented**: no firmware, no
`src/`/`include/` change, no build, no device action, with DEC-108's capacity, scheduling and retention bounds recorded
as **unproven**. **Three review passes are applied (2026-10-02):** DEC-108 §13 records the lifecycle and recovery
policies, and **§14** decides the third pass's three implementation choices — capacity accounting is a **sliding
window-span invariant** (window storage capacity is exactly the provisional 2,048-event limit, the closure reserve counts
inside it, no resolver on the MIDI edge), legacy dense content is served through a **bounded segmented page path** rather
than the removed mutable fallback, and the effective-content owner to extend is **`LoopContentResolution`** over pinned
bodies — with the artifacts re-validated `--strict` (21 requirements / 67 scenarios) and everything still **uncommitted**.
**Nothing here is superseded:** RWO's S2.9/S5/S7 keep their status, and `S2.3`'s `N` stays
**provisional** until the sibling's gates pass. It is therefore **not** in this branch's implementation scope —
`CURRENT_WORK.md` § Now implementing is unchanged.

**Resolved by the owner as path A** — the owner is restated in NAMING vocabulary, and DEC-108 §14(c), `CURRENT_WORK.md` and the change's spec/design/proposal/tasks wording were corrected with every behavioural clause kept; `NAMING.md` and DEC-037 are untouched. Task 1.7 is checked; the naming gate is discharged for tasks 3.2/3.6/3.12, and task 3.1 was never gated.
**Slice 3.1 (the window-build owner) is implemented, natively green, and builds through the RAM1 guard with the approved line unchanged** — branch `feature/windowed-playback-generation`, task 3.1 checked. Host: `test_midi_service_drain` **38/38**, `test_runtime_working_set_ownership` **48/48**, `test_playback_prewarm` **190/190** (unchanged), full native **2679 / 2672 succeeded / 7 skipped / 0 failed**; four mutations red-proved with byte-identical restores. **Its first build failed the guard** (`+4,096 B DTCM` with no per-object `.data`/`.bss` change) — three `switch` statements emitted 16–24 B jump tables into `.data`, and a few added `.data` bytes snapped the following `ALIGN(1024)`/`ALIGN(4096)` (the RAM1 rule's capture-tag finding, reproduced). Fixed by adding **zero `.data`**; the build now reports `variables 96,192 / padding 1,208 / locals 34,880`, so **no re-base was made**. Evidence: [`evidence/2026-10-02-s31-window-owner/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s31-window-owner/EVIDENCE.md).
**Slice 3.2 (publication half) landed:** the owner publishes immutable interval windows atomically (`RuntimePlaybackViewLifecycle::publishCompletedBuildAsCurrentWindow`, called in the turn that completes the build), the window carries its identity and coverage, and a second window is refused by name with the published window unchanged; no consumer changed. Green: 49/49 + 38/38 + 190/190, full native 2680/2673/7 skipped/0 failed, `teensy41-capture-serial` SUCCESS with RAM1 `variables 96,192` unchanged. **Task 3.2 is complete** (2026-10-02): its enumeration clause is `Track::playbackWindowInterval` + `Track::requestPlaybackWindow(slot, phase, scratch)` — the selected `LoopSnapshot` enumerated through `Loop::tryGatherPublishedSnapshotEventsInWindow`, rootless and non-current roots refused by name — and fixture **2.10** is asserted through the gather's new inspected-work counter (identical work for the same window on a 4-bar and a 64-bar loop; 0 for a window that intersects nothing). Full native 2681/2674/7 skipped/0 failed and the build keeps RAM1 `variables 96,192` unchanged. **Task 3.3 (representation budget) closed:** [`S33-REPRESENTATION-BUDGET.md`](../../openspec/changes/windowed-playback-generation/S33-REPRESENTATION-BUDGET.md) shows the five lifecycle role names map onto three buffers (`retained` aliases `current`/`build`; `idle` is a sentinel), that this change's publisher uses `current + build` only, and that **no buffer is added**: retention is the DEC-108 item 4 pinned source (3.6), not a fourth window, and the windowed lane reserves no bridge/base so it has no transition overlap. Holder cost unchanged at "three views + two stagings" (333,824 B ≈ 326 KiB on the target ABI); `test_playback_prewarm` 190/190 unchanged and cited, no new fixture. Evidence: [`evidence/2026-10-02-s32-interval-publication/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s32-interval-publication/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.4 (F-1): the window identity is carried, and a replacement cannot be straddled (firmware; branch `feature/windowed-playback-generation`; tasks 3.4 and 2.1 checked).** The published window now carries `PlaybackWindowIdentity` `{slot, playbackGeneration, playbackRevision, interval}` — equality over exactly those fields, with the snapshot identity left as provenance inside `PlaybackViewSourceIdentity` and the generation guarding **ownership** rather than acting as a second currency (`playbackRevision` stays the sole content currency). The lifecycle gained the **successor lane** (`publishCompletedBuildAsReadySuccessor`): `publishCompletedBuildAsReady` treats the prewarmed shell at `ready` as an existing successor and refuses, so a lane without a record-return shell could never publish a replacement at all; the variant accepts an *incomplete* shell while keeping "one **real** successor at a time" and "only a complete view is ever installed". The owner's completed build now **routes** — no window → `current` (3.2's primitive), a window being served → the `ready` successor, leaving the served window byte-for-byte untouched — and the request bound follows with it: `WindowAlreadyPublished` → **`SuccessorAlreadyPrepared`**. **This resolves 3.2's measured finding rather than working around it:** the F-1 fixture asserts the served window identical field-for-field before every preparation turn, that a prepared successor does not become the served window until adopted, and that the adoption (`adoptPreparedPlaybackWindow`, one atomic `handoffReadyCoverage(Preparation)`) swaps it in; reverting the routing reproduces the old symptom, which is how the fix is held in place. F-1's third clause is by construction as well as by assertion — the only window accessors on `Track` are const and return copies, and the view set is private. Verification: focused **50/50**, `test_midi_service_drain` **38/38**, `test_playback_prewarm` **190/190** (unchanged; cited, not restated), full native **2681 / 2674 succeeded / 7 skipped / 0 failed**, and `teensy41-capture-serial` **SUCCESS** on a **clean rebuild** (70.8 s, every TU recompiled) with the approved RAM1 line unchanged (`variables 96,192`, `code 392,200 + padding 1,016`, locals 34,880, floor 840) — the new code is `TRACK_COLD_MEM` (FLASH), so no `.data` and no ITCM moved, and **no re-base** was needed. Two mutations red-proved with byte-identical restores (sha256 verified). **Not claimed:** the adoption edge is **not wired into production** (3.7 decides when a boundary is eligible), readiness is still **3.5**'s predicate, nothing a consumer reads changed, and there is no device evidence. Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md); evidence: [`evidence/2026-10-02-s34-window-identity/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s34-window-identity/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.5 (AC-3): readiness is the completed publication, with a named holder (firmware; branch `feature/windowed-playback-generation`; task 3.5 checked).** `Track::playbackWindowReadinessForSlot` answers readiness from the window this change **published** — not the live merged buffer — so a published, complete, current window is `Ready` **even when its interval holds no events**, and every non-ready value **names the holder**: `OwesBuild` (the window-build owner, when a current published root exists), `OwesSource` (the *source* owes the root), `OwesCurrency` (a rebuild is owed), `NotServable`, `NoRuntime`. `playbackWindowReadinessHolder` renders the name in an if/else chain (no name table, no `switch`); currency is `playbackRevision` and exactly it — the generation stays an ownership guard. **RWO's two demoted-target fixtures are re-pointed** to ask the predicate and name the holder (`published_source` in the S4b.6 helper; `OwesBuild`/`OwesSource` per shape in the S4b.7 helper) instead of depending on empty ⇒ not-ready; their *commit* expectations stay on the **legacy** term because **nothing in production requests a window yet**, and red-proof (b) measures that flipping the commit's hold term now leaves the root-backed demoted switch pending forever — the requester and the flip are 3.7's. Verification: focused **51/51**, `test_midi_service_drain` **38/38**, `test_playback_prewarm` **190/190** (unchanged; cited), full native **2682 / 2675 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a **clean rebuild** with the approved RAM1 line **byte-identical** (`variables 96,192`, `code 392,200 + padding 1,016`, locals 34,880, floor 840) — **no re-base**. Two mutations red-proved with byte-identical restores (sha256 verified). **A RAM1 regression was produced and fixed by name, not by re-basing:** the slice's first build failed the guard (`variables 100,288`, +4 KB, with ITCM code unchanged) because this script places `.rodata` in **DTCM**, so the six holder-name literals crossed the `_VectorsRam`/periodictable `ALIGN(1024)` edge; the natural fix (`TRACK_COLD_MEM` on the literals) is rejected by the compiler with a section-type conflict — `.flashmem` is a **code** section in that TU — so the working fix is the carve-out the script already carries for two other TUs, `*TrackPlaybackWindowBuild.cpp.o(.rodata.str* .rodata.*.str*)` in `.text.progmem`. [`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc) now carries the instance and the rejected first guess. **Not claimed:** F-2 (task 2.2) stays open; the one-pass-late allowance is 3.6's; `isPlaybackMergedMidiEventsReadyForSlot` remains the switch commit's term until 3.7 removes the live build; no retention/index/adoption wiring; no device evidence. Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.5; evidence: [`evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s35-readiness-holder/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.6b: the lag rule (F-3, AC-5) — one pass late and only that (firmware; task 3.6 stays open for the interval index).** `PlaybackLagDecision { Current, PreviousOnePass, BreachSecondMiss }` is **total by construction** — no waiting outcome, so a consumer is never blocked by a missed deadline — with `PlaybackLagState` holding the per-interval accounting (traversed interval, served and desired revisions, consecutive misses, the once-per-episode breach) on the per-slot runtime; `Track::resolvePlaybackLag` is the rule as one pure resolution. At a **traversal boundary** a miss leaves the **previous** revision authoritative for that traversal with the served window byte-for-byte unchanged; the **second** consecutive miss is the **named** breach, reported **once per episode** (`lag_breach` through the existing row emitter) while the previous revision keeps being served; a **further edit does not reset the allowance**; a published replacement or a changed interval ends the episode cleanly. Verification: focused **53/53**, full native **2684 / 2677 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with RAM1 `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` / locals 34,880 (floor 840, 448 B headroom) and **no re-base**; three mutations red-proved with byte-identical restores (the AC-5 allowance reset; a published replacement ignored; the once-per-episode guard dropped) — the first AC-5 mutation attempt was a no-op and was re-placed before it counted, which the record keeps. The S3.0 pin moved per ABI again (target 6,464 → **6,488**, measured with a temporary compile-time probe; the host figure is unchanged because the state fitted existing padding). **OWED for 3.6:** the **interval index over pinned bodies**. Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.6; evidence: [`evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s36b-lag-rule/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.6c: the interval index over pinned bodies, and **task 3.6 checked** (firmware).** `RetainedIntervalIndex` is one **bit per bar** — a **descriptor**, never an event copy (`TickIndex`'s whole-history storage is not inherited; `CommittedEventRange` stays the body-reading primitive) — whose lookup is **interval-directed** (a query counts the bars the pinned bodies cover and the bars **skipped without touching a chunk**) and which folds the **edit destinations** in, so a bar whose only content is a moved note is not skipped. It publishes **atomically with the source revision** (candidate built, swapped by one assignment, revision stamped inside), its **update strategy is a full rebuild per admitted revision** (both affected ranges by construction, no reader holding a mutated index), its **resident bound** is `kRetainedIndexMaxBars = 512` bars with a **named refusal** beyond it, and its **lifetime** is the revision it describes (`RevisionMismatch`, never a stale answer). The fixture also builds an interval from the **pinned chunk lists alone**, demonstrating "no resident whole-loop root". Verification: focused **54/54**, full native **2685 / 2678 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with RAM1 `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` / locals 34,880 (floor 840, 448 B headroom) and **no re-base**; three mutations red-proved with byte-identical restores (edit destinations not folded; the revision guard dropped; every bar reported covered). The S3.0 pin moved per ABI again (target 6,488 → **6,512**: +96 B sources, +24 B lag, +24 B index; host 6,544). **Task 3.6 is checked** — all four clauses green, with two limits on the record: the **consumer path stays 3.7's** (no production requester yet) and the **envelope roll-up** of these figures into RWO's budget artifact is that change's own term. Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.6; evidence: [`evidence/2026-10-02-s36c-interval-index/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s36c-interval-index/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.8 checked (AC-7, reuse before invention); slice 3.7 not started because its own gate blocks it (documentation only; no firmware, no build, no device action).** [`REUSE-BEFORE-INVENTION.md`](../../openspec/changes/windowed-playback-generation/REUSE-BEFORE-INVENTION.md) names, with citations read from the code, the four entry points AC-7 requires — the declared build contract (`CoverageServiceContract` + `Track::playbackViewCoverageContract`), the body format's bounded write/parse (`StorageIo`'s `write`/`read`/`peek` + `getLastPersistedCapturePassWriteMaxBatchEvents()`), the resume-arming cursor pattern (`PersistedLoopSnapshotReleaseCursor`), and the single persistence drain budget (`SyncDrainBudget` + `buildSyncDrainBudget(input, sliceBudgetUs)`), which this change neither consumes nor shares — and records the audit: **one** deferred-service enumeration (`DeferredIdleOwner`), to which this change added a *value* (task 3.1), with dispatch on the existing frame and state-driven eligibility, so **no second publication scheduler exists**. Its two INTERPRETATION rows are marked as mappings a reader may dispute, and its stated limit is that the behavioural half (fair service among holders, no starvation) belongs to **task 3.10**. **3.7 carries a gate note rather than a start:** it requires 3.11, 3.12, 3.13, 3.14 and 4.5 implemented and verified before the live mutable merged-window build is removed, and none exists yet — the ladder's stage (4) is unreachable, and the next slices are those dependencies, **3.11 first**. No code changed, so the verification record is the last full-suite/build state (focused 54/54, full native 2685 / 2678 succeeded / 7 skipped / 0 failed, `teensy41-capture-serial` SUCCESS). Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.8.

**2026-10-02 — `windowed-playback-generation` slice 3.11 checked: shared-view ownership and the transition table (DEC-108 §13d); 3.7's first dependency is green (firmware).** The ownership rules are **one table** — `PlaybackViewTransition` (the spec's seven transitions), `PlaybackViewTransitionEffect` (one field per clause the spec names) and `resolvePlaybackViewTransition` (the pure resolution) — with the served window's readability asserted as the invariant of **all seven** rows. `Track::applyPlaybackViewTransition` applies each row (cancel the candidate, retire the prepared successor, invalidate the identity), `Track::playbackViewRoleStateForSlot` exposes the role state, and the **publication guard** uses the build's recorded **operation generation** so a completion arriving after the identity moved is refused and releases only its own candidate (a late completion publishes nothing); clear/load bumps the generation, so returning to a slot revives nothing. Verification: focused **55/55**, full native **2686 / 2679 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with RAM1 `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` (448 B headroom) and **no re-base** — the S3.0 pin is unchanged at 6,512 (the 4-byte field fitted existing padding). Three mutations red-proved with byte-identical restores; **one attempted mutation did not discriminate and is recorded as such** (the explicit prepared-successor retirement branch, because `discard()` already clears the view the successor publication leaves in `ready`), with the uncovered case named as a gap deferred to 3.7 and the packet's mutation cell corrected. **Still owed before 3.7:** 3.12, 3.13, 3.14 and 4.5. Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.11; evidence: [`evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s311-shared-view-transitions/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.6a (DEC-108 items 4/12(a)/13(a)): the retained source revision, its roles, and the one release predicate (firmware; task 3.6 stays open).** The first clause of 3.6 is green. `RetainedPlaybackSource` — identity `(revision, generation)`, descriptor **pins** and **owned bodies** — lives in explicit **serving / candidate / retiring** roles on the per-slot runtime, admitted by `Track::retainPlaybackSourceForSlot` with typed outcomes (`Admitted`, `AlreadyRetained`, `NoRuntime`, `NotServable`, `RoleCapacityReached` — the bound refuses **by name** rather than discarding a still-needed source). `tryReleaseRetainedPlaybackSource` is the **one release predicate** (the revision advanced past it; no window serves it; no builder holds a frozen source; no closure obligation; or the content it described is gone), and retention is a **named term of `Track::noLiveOwnerForSlot`**. Both destruction paths are covered by the mechanism that decides each: the **reclamation consumer** reads the retained pins (the manager merges them into the set the loop consumes, since "a pin registered only alongside undo references is not sufficient protection"), and the **direct reset/replacement path** is safe by **body ownership** — `LoopEventStore::retainChunkRefs` means all eleven `Loop::resetPassTimeline` call sites stay **ungated**: gestures keep working, the bodies survive, and only the release predicate returns them. That ownership **superseded** the approved veto for this slice and is the spec's own "actual body ownership" clause, not a relaxation. Verification: focused **52/52**, full native **2683 / 2676 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with RAM1 `variables 96,192` **unchanged** / `code 391,864 + padding 1,352` / locals 34,880 (floor 840, **336 B more headroom than the 3.5 baseline**) and **no re-base**; three mutations red-proved with byte-identical restores (sha256 verified). Two findings: the **S3.0 size pin is now per ABI** (6,368 → **6,464** target / 6,544 host, because the holder carries pointer-sized pin lists — the first build failed on the target only), and the enlarged cold reclamation pass moved to FLASH (`PRESSURE_RECLAIM_MEM`, the TU's existing macro), **returning 336 B of ITCM**. **Still open in 3.6, named not implied:** the **interval index over pinned bodies** (later intervals from the retained revision, no resident whole-loop root, both affected ranges, atomic publication with the source revision, inspected-work budgeting) and the **lag rule** (F-3: one pass late and only that, the named second-miss breach, the allowance not reset by a further edit, the consumer never blocked). Packet: [`G2-EXECUTION-PACKET.md`](../../openspec/changes/windowed-playback-generation/G2-EXECUTION-PACKET.md) § Slice 3.6; evidence: [`evidence/2026-10-02-s36a-retained-source/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s36a-retained-source/EVIDENCE.md).

The maintained-LCR and phase-3 changes below are retained as historical base context and are not
implementation scope here.

| Change | Focus |
|--------|--------|
| **`deferred-slot-restore-liveness`** | **ARCHIVED 2026-09-24 as `2026-09-24-deferred-slot-restore-liveness` with a reduced, accurately described scope — see the Active OpenSpec section above.** The text below is the pre-implementation intent, retained as history; the delivered scope is narrower (preparation under transport, request identity with named waits, pre-publication stale-identity rejection, the census checkpoint, a content PASS, and CLEAR's permitted completion branch — **not** drain-driven advancement, which is `runtime-working-set-ownership` S2b.7, and **not** a runtime set change).** Restore-liveness change: a queued slot restore becomes a liveness obligation with an explicit identity `(track, slot, requestGeneration, setEpoch)`, separate **named wait** states (`waiting_slot_owned` / `waiting_capture` / `waiting_commit_gate` / `waiting_job_capacity` / `waiting_retry`) versus exactly one **terminal** outcome (`committed` / `unnecessary` / `refused` / `superseded`), and validated rather than blind generation advance. **Scope, stated plainly: it delivers diagnosable preparation and named waits plus drain advancement where the commit gate admits — it does NOT by itself unblock CLEAR during another track's playback.** Two global gates bound it: `MidiServiceDrain::admitLoadJobChild` (commit/cleanup need a content-independent bound) and `hasLoadLoopJobCommitPending()` (true for *either* job instance, so the denial is global, not per job); both are the open clause of `resumable-midi-boundary-playback-maintenance` **task 3.3**. **The drain's caller contract is a three-way outcome** — completed / temporarily blocked / failed — with `saveState` kept as a `Completed`-only wrapper and both callers (`handleClearTrack`, `SavedSetIo`) using the detailed entry point: a blocked CLEAR **refuses by name, leaves the slot untouched, and leaves the save owed**; retention as an accepted interaction is design O7. The finite **100 ms post-load holdoff** (`LoadLoopJob.cpp`) is waited out within a bounded, MIDI-serviced budget or reported by name. The blocking drain must **advance** the restore (active and parked instances) using the **same all-tracks runtime eligibility** as the normal path — `handleClearTrack` stops only the selected track — and restore work must be charged in the drain's budget and progress detection. Modifies the `lazy-slot-hydration` capability, whose current text permits the stall. Four G2 packets (A desired-behavior fixtures → B liveness → C drain advancement + caller contract → D reconcile outcome) + closeout; Packet A's red set is excluded from the initial green gate behind a named flag. Owner: `StorageManager`; no ownership move. Evidence and non-goals: [`proposal.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/proposal.md), [`design.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/design.md) |
| **`restored-library-stability`** | **[Packet A COMPLETE and device-verified read-only 2026-09-24 — see the Active OpenSpec section above and the fourteenth-session entry; Packets B/C open, contract DEC-083.]** **PROPOSED 2026-09-24 — planning complete, `openspec validate` clean, Packet A (observation-only firmware + read-only analysis) can start immediately; nothing implemented; G2 review two rounds — BLOCKED then corrected (first four contract fixes, then two: the state-vs-cause split and the observation's ownership/admission/invalidation contract).** Owns the instability that blocks `runtime-working-set-ownership`'s device legs: the committed set is not reproducible **as measured**. Reports a per-slot **observed current state** (`empty` / `populated` / `pending`) **separately** from a **nullable historical cause** (`catalog omission` / `restore refusal` / `intentional eviction` / `content loss` / `ambiguous` / `unknown`) with evidence and precedence — `empty` is a state, never a cause; four distinct quantities with an explicit observation **window**; through a **bounded on-demand observation** with a declared owner and a service point that runs **independently of normal load admission**, SD-read admission, and an **invalidation/restart** rule covering request-generation change, set-epoch change **and content revision change** (a publication installs content under the same request identity, so it changes neither) — content fields are never carried forward; one repeatable fixture inside the approved scratch range (track 5 slot 1, track 6 slots 1–8) with any CLEAR needing per-slot owner authorization, stability proven by a **complete per-slot manifest comparison**; carries the **S2b.7 device confirmation** as an independently reportable packet; hands back for S1/S2b/S3. **Corrected findings:** `n_committed_slots` is a **window-scoped observed-completion count** (background restores included) — not the boot playback set, so the earlier explanation is withdrawn; `sets/index.bin` is `SetIndex{nextSequence}` (a saved-set folder counter, not a slot catalog); the shrinking boots report `reconcile_warnings=0`; `DIAG,lcr,skip,restore` is content-resolution; `Track N loaded state: EMPTY` appears on stable boots too; and both sweeps' `library_rows` are all-zero. |
| **`runtime-working-set-ownership`** | **Native scope (P0/P1) plus S2b.8 and S2b.7 COMPLETE 2026-09-24 — full native 2,352 / 1 skipped / 0 failures; every remaining gate is a device leg (no native task open).** The scaling-law change: runtime working sets become active-role owned, window-sized and generation-leased, so RAM scales with active roles rather than recorded slots. Landed and green: S0 verification, S1.2/S1.3-clause-2/S1.5-clause-3 fixtures, the accepted-stop latch (S4f.0/S4f.1/S4f.2/S2b.6), boot/load admission removal (S2b.1–S2b.3), S4.0/S2b.4 per-slot liveness and release, S4f.5's stop consumers, S3.0 placement pins + the post-link RAM1 budget check, and B01–B06. **Blocked on an unstable restored library — now owned by [`restored-library-stability`](../../openspec/changes/restored-library-stability/proposal.md) — for:** S1.1's per-owner census, S1.3's device slope, S1.4's undo/root baseline, S1.5's device refusal point, S2b.3's post-restore baseline, S2b.5's device gate, and the content PASS — the restored set is unstable across boots, so no deterministic single-slot capture can be produced. Envelope: 8 tracks x 1 sounding slot today (N derived, **provisional** until the sibling change lands); 16-channel banks and multi-slot-per-track are targets the law must not preclude. Governed by **Playhead-scheduled MVCC** ([NAMING.md](../Authority/NAMING.md)); invariants 1–31 plus **32–36**; `design.md` **Revisions 18–24**; **DEC-078** (every interaction accepted; finalization **per interval**; amends DEC-067/DEC-076 point 2; counter renamed **`accepted_stop_finalizer_wait`**), **DEC-079** (handoff corrections, [`IMPLEMENTATION_CONTRACT.md`](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md) is binding), **DEC-080** (execution packets); stages S0–S7 + **S2b** (boot/load admission), windowed playback **extracted** to `windowed-playback-generation`. **Execution order: S0/S1 → S2b → S3/S4 → S4f → S5 → decoupling**; design revisions stop unless implementation contradicts the contract. RAM1 floor currently **840**, most recent build `variables 96,192 / code 391,080 + padding 2,136`, enforced by `scripts/check_ram1_budget.py` against `scripts/ram1_budget.json` |
| **`capture-tier-a-budget-profiles`** | **Design (B1 pending)** — profile-selected, per-family **byte-budgeted** Tier-A capture with bounded bursts and edge/sample/stream semantics, so a 15–20 minute session fits the 1 MiB ring intact (measured Tier-A ~30–35 KB/s covers ~30 s). Critical reserve, per-family coverage, and a host verdict that cannot report an unconditional PASS over degraded evidence. Builds on the landed RC-MEM-7 degradation |
| **`immutable-loop-snapshot-passes`** | Phase 2 publication complete; 3.5/3.6 and 4.1–**4.6** closed; **5.1**–**5.7** SHIPPED; **6.1**–**6.4** SHIPPED (**6.4** device [`164947`](../captures/session_20260912_164947.log) 2026-09-12); next open: **4b.3**, **6.5**; §**6.6** post-closeout polish non-blocking; [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write); boot sync load still legacy; snapshot nodes pack into 32 KB slabs (`DEC-048`) |
| `loop-content-resolution` | Historical base prototype; maintained-LCR continuation not pursued on this branch |
| `loop-effective-event-source` | Historical base context; no new LCR work on this branch |
| **`hitl-cli-rebuild`** | **Phase 3 in progress** — layered `base` + `edit_full`; 3.2 bridge done; next: 3.3 device PASS |
| **`continuous-runtime-persistence`** | Phase 0–4 shipped; Phase 4 HITL **passed**; Phase 5 recovery **parked** (see firmware ownership review) |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending |
| **`unified-interval-projection`** | Phases 1–5 code shipped; native PASS; **5.5 HITL deferred** (DEC-017); Phase **6.1–6.2** synced with geometry (2026-08-04) |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

## Archived OpenSpec (normative specs in `openspec/specs/`)

| Change | Archived | Normative spec |
|--------|----------|----------------|
| **`recoverable-async-display-dma`** | 2026-09-25 | [`recoverable-display-dma-transport/`](../../openspec/specs/recoverable-display-dma-transport/) — 14/14; evidence in the archived [`EXECUTION_PACKET.md`](../../openspec/changes/archive/2026-09-25-recoverable-async-display-dma/EXECUTION_PACKET.md) |
| **`loop-content-history-persistence`** | 2026-08-14 | [`loop-content-history/`](../../openspec/specs/loop-content-history/) |
| **`note-edit-current-state`** | 2026-08-08 | [`note-edit-current-state/`](../../openspec/specs/note-edit-current-state/) — [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md) |
| **`edit-session-action-geometry`** | 2026-08-05 | [`edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| **`deferred-job-scheduler`** | 2026-07-19 | [`deferred-job-scheduler/`](../../openspec/specs/deferred-job-scheduler/) |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../Plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../Plans/archive/handoff/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../Plans/archive/handoff/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry`** — **archived** 2026-08-05; normative `openspec/specs/edit-session-action-geometry/`
- **`loop-content-history-persistence`** — **archived** 2026-08-14; normative `openspec/specs/loop-content-history/`

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/Authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
- **Runtime scheduling:** contract [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md); roadmap [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Interval reservation is **not** authorized. Persist `admit*` and `DeferredJobScheduler` (DEC-027) remain separate.
- **Active linkage:** [`CURRENT_WORK.md`](CURRENT_WORK.md) overdub-lag stages `P1`–`P4` feed the roadmap as pre-contract owner-boundary work (causality + arbitration ordering), not as interval-reservation implementation.


2026-09-16 task-2.3 integration checkpoint: production canonical stop preparation
now uses a separately reserved output Capture instead of mutating raw capture
chunks. Explicit-source transfer/preview APIs and full-pool admission checks are
covered by native tests. The service-paused Button A/manager/Track reentry tests
remain red: FIFO, live/head staging, preview/session/undo/snapshot ownership,
causal sources and generic-reset lifetime isolation are not implemented. Keep
2.3 and HITL open; see the active change's CONTINUOUS-CAPTURE-OWNERSHIP.md for
bounds, evidence and limitations. No firmware upload or device action occurred.

**2026-10-02 — `windowed-playback-generation` slice 3.12a: the admission limit on the decided accounting unit (DEC-108 §14); task 3.12 stays open.** `WindowAdmissionAccounting` (in `RuntimePlaybackViewConfig`) accounts the capture interval's **effective** events plus outstanding reservations plus the **closure reserve inside L**, with `fits(...)` as the one predicate, an `admit(...)` returning a **named** `Outcome` that never half-accepts, and per-bar/span bounds that are conservative and **clamped by L**. Two invariants are pinned (`static_assert`): `kWindowAdmissionLimit == kEventsPerView` — **L is the window's storage capacity**, so no partition multiplier can drift — and `kWindowClosureReserve < L`, with 128 a declared **provisional** value rather than a measurement. Verification: focused **56/56**, full native **2687 / 2680 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` unchanged / `code 391,928 + padding 1,288` (448 B headroom), no re-base, S3.0 pin unchanged; two mutations red-proved with byte-identical restores. **OWED:** the bounded legacy paged path (fixture 5.5) and the MIDI-edge wiring of the limit into capture acceptance. Evidence: [`evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s312a-admission-limit/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.12b: the bounded legacy paged path; task 3.12 stays open.** `include/WindowPagedStream.h` (new) serves content beyond `L` — and any interval that outgrows one page — through a **ring of three fixed-capacity immutable 128-event pages** with a **resumable cursor** `(sourceRevision, nextSequence, offsetInPage)`: `produce(...)` reuses the **existing** `CommittedEventRange::appendWindowBudgeted` + `WindowTraversalCursor` for one bounded slice per call (no second enumerator, no inline rebuild) and `consume(...)` continues where it left off, so ordering and **exactly-once** emission hold across page boundaries from positions alone while a drained page returns to the ring (**bounded memory, unbounded content**). There is no blocking state (an unpublished next sequence answers `NeedsPage`) and a foreign revision is refused by name (`StaleRevision`) on both sides. Verification: focused **57/57**, full native **2688 / 2681 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` (448 B headroom), no re-base and the S3.0 pin untouched; two mutations red-proved with byte-identical restores (the first M15 attempt did not discriminate, so the fixture was enlarged to 700 events — recorded). **OWED:** the MIDI-edge wiring of the limit (3.12c) and fixture 5.5's timing envelope. Evidence: [`evidence/2026-10-02-s312b-paged-path/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s312b-paged-path/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.12c: the MIDI-edge wiring of the admission limit; task 3.12 stays open.** The gate sits in `Track::recordMidiEvents` **before** the canonical append, with a **derived** accounting (`Track::captureIntervalAdmissionForSlot` = the serving window's content + this interval's accepted rows + the closures they owe + the reserve): arithmetic over existing state, so nothing is stored (no counter can drift, no `sizeof` pin moves), nothing allocates, nothing is scanned and the resolver is never invoked on the edge. `admitsNewEvent` is the one decision (`kUnitsForNoteOn` = 2: a note-on owes its closing row too); a **closure is never refused** (its row is already inside `L`, so it moves a unit — the closure guarantee clause); a refusal is **named** (`CaptureAppendDenyReason::WindowAdmission` / `"window_admission"`), gives a staged reservation back and leaves nothing owed. **Scope, decided by the first full-suite run:** `L` is what a **window must hold**, so the gate is consulted exactly where a window serves the slot — the first wiring bounded every interval and broke the sibling suite's `test_record_normalization_preserves_take_larger_than_overdub_staging`, because a whole-take record take is not served by the 2048-event window. Interaction acceptance (stop/switch/undo) stays outside by construction (DEC-078). Verification: focused **59/59**, sibling suite **60/60**, full native **2690 / 2683 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,072 + padding 1,144` (304 B headroom), no re-base; five mutations red-proved with byte-identical restores (and the harness's own non-unique-anchor mis-edit was caught by the shasum oracle and repaired byte-identically before the proofs were re-run). **Not claimed:** the gate is inert in today's running system until a window lane serves a slot (3.7's requester), and the plain-event reservation release on refusal is executed but not asserted. **OWED:** fixture 5.5's timing envelope (over-capacity same-tick/wrap/cross-page closures) and the paged path's production consumer. Evidence: [`evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s312c-midi-edge-admission/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.13: the preview source contract; task 3.13 stays open.** `PlaybackWindowOrigin { Committed, Preview }` is carried in `PlaybackViewSourceIdentity` **and** `PlaybackWindowIdentity`, both **part of equality**, so a preview window is not equal to the committed window it stands in front of at the same slot/generation/revision/interval — "preview is identified as preview, never committed currency" as an identity answer. `Track::requestPreviewWindow` publishes preview through the **same deferred lane** as committed content (whole-loop coverage, the session's preview revision as the freeze token and currency, `origin == Preview`); `Track::cancelPreviewWindowForSlot` serves the three lifecycle reasons with a **named** `PreviewCancelOutcome` and reuses the shared-view `ClearOrLoad` row (candidate released, successor retired, identity invalidated) so the served window is never torn and a committed build is never cancelled; the exit handoff keeps the preview window serving until a committed successor is **adopted** (a boundary, not a gap), with a repeated exit answering `AlreadyHandedOff`. Verification: focused **61/61**, full native **2692 / 2685 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor); the target size pin was re-based with measurement and the reason at the assert (6,512 → **6,624**, +112 B, host unchanged), read from a temporary compile-time probe that was removed. Five mutations red-proved with byte-identical restores. **Not claimed:** no production requester for either lane yet (so the lane is inert in the running system), and fixture 2.12's integration half needs a suite linking both the window lane and the real edit stack. Evidence: [`evidence/2026-10-02-s313-preview-contract/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s313-preview-contract/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.5 (containment half): second-miss fault containment and the approved last resort; task 4.5 stays open.** `ContinuityContainment { Nominal, ServeLastCoherentRevision, LastResortTargetedSilence }` plus the pure `resolveContinuityContainment(faultLatched, validCoverageRemains)` is total — no waiting, no rebuilding — so blocking the clock and unbounded rebuilds are unrepresentable. The **second consecutive miss latches** `deadlineContractFaultLatched` (3.6b's once-per-episode breach report keeps its shape; this latch is what containment reads), cleared at exactly **one** place: the eligible boundary (AC-5: a further edit never forgives it). `Track::containmentForSlot` answers containment as a **coverage** question (an event-empty window still covers its interval — the structural half of "an event-empty window means no new events"); `Track::applyLastResortForSlot` applies the approved last resort — **targeted note closure plus affected-slot silence** via the existing slot-scoped owner, never a global transport stop, never a rebuild — and answers `NotNeeded` with **zero** output when coverage remains. Verification: focused **62/62**, full native **2693 / 2686 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the previous slice: FLASH-placed additions, no ITCM growth, and the new `bool` fitted padding so **no size pin moved**). Six mutations red-proved with byte-identical restores; the first "track-wide silence" mutation did **not** discriminate and the fixture was sharpened to assert the real MIDI output through `MidiSendCapture`, while an assertion that was **vacuous in this suite** (`g_nativeSilenceTrackCalls`) was removed rather than kept as evidence. **Not claimed:** the cross-generation closure half (§13g) is the consumption path's (3.7/3.14), and no production caller applies containment yet. Evidence: [`evidence/2026-10-02-s45-containment/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s45-containment/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.5b: the cross-generation closure reconciliation (§13g); task 4.5 stays open.** `ClosureSettlement` renders one version handoff's plan as a **value** — the soundings the replacement removes (closed at handoff), the ones it re-issues (their note-ons **suppressed**: an unchanged note is never retriggered), and the ones it leaves alone — bounded by `ActiveNoteLedger::kMaxOpenNotes`, allocation-free. `Track::planClosureSettlementForSlot` computes it **off-clock** (a bounded presence set over the incoming note-ons, then a ledger walk) and `Track::applyClosureSettlementForSlot` emits the closures **first** and only closures (the replacement's note-ons are the caller's, the spec's ordering as calling order), discharging exactly those ledger obligations. The interpretive rule — absence is removal evidence **only** when the incoming content is authoritative for the whole loop, and never for an empty window — is what makes "an event-empty window means *no new events*" a consequence rather than a special case. Verification: focused **63/63**, full native **2694 / 2687 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom; the line is byte-identical to the two previous slices — FLASH-placed additions, no ITCM growth, no size pin move); four mutations red-proved with byte-identical restores, with one non-unique-anchor repair caught by the shasum oracle. **Not claimed:** the emitted order at the real handoff, the moving-closure case and displayed provenance are the serving path's (3.7). Evidence: [`evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s45-closure-reconciliation/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.14: the undo/redo audible handoff; task 3.14 stays open.** The blanket slot silence is removed from the undo/redo playback refresh: the four edit-batch sites reconcile instead, through `Track::reconcileAfterEditPassStateChange` (the affected edit passes' own added identities against the ledger: **closed** if removed, **suppressed** if re-established while still sounding, **preserved** otherwise), fed by the bounded allocation-free `NotePresenceSet`. The refresh's **other** blanket half — clearing the ledger — is also gone: `LoopPlaybackRuntime::reset` clears the caches unconditionally while `preserveLedger` governs only the ledger (the notes sounding on the wire, which *are* the closure obligations the handoff reconciles). Undo semantics, ownership and the stack discipline are unchanged. Verification: focused **64/64**, full native **2695 / 2688 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the three previous slices, no size pin move); six mutations red-proved with byte-identical restores including the task's own blanket-silence restoration on both refresh paths (the first attempt did not discriminate and the fixture was extended to cover the generic path). **Not claimed:** the overdub-session sites do not yet close their removed soundings (needing a committed-pass identity source, with the stuck-note risk stated), and the refresh is not yet a request through the immutable publication owner; the between-grains matrix, retained-source lifetime and preview-session undo/redo remain. Evidence: [`evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.14b: the committed-pass identity source; task 3.14 stays open.** The overdub-session undo/redo sites now settle rather than merely not-silence: `Loop::overdubSessionPassIdAt`/`overdubSessionCompanionIdsAt` name the step's pass and companion edit batches, and `Track::reconcileAfterCommittedPassStateChange` takes the identities from that pass's **own bodies** (a bounded, allocation-free row walk), so a note the undo removed **closes** and the stuck-note risk is gone. The classification is now one shared core (`settleLedgerAgainstPresence`: closed / suppressed / preserved over a single ledger walk) fed by two sources — an edit batch's added events and a committed pass's bodies — and the generic refresh is replaced by `refreshPlaybackAfterCommittedPassStateChange`. Verification: focused **64/64** (the fixture extended, not added), full native **2695 / 2688 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the four previous slices, no size pin move — the `Loop` accessors add no state, proven by the build's own `static_assert(sizeof(Loop) == 984)`); six mutations red-proved with byte-identical restores, with two bookkeeping facts recorded (M38/M39 anchors moved into the shared core, M36b's target deleted, so M42/M43/M44 succeed them; M44's first attempt passed only because its anchor was in the wrong file). **Not claimed:** the refresh is not yet a request through the immutable publication owner (3.14c — the owner's companion passes are a no-allocation candidate but unverified as authoritative at undo time, versus the existing request path's caller-owned scratch), and the between-grains matrix, retained-source lifetime, displayed provenance and preview-session undo/redo remain owed. Evidence: [`evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s314b-committed-pass-source/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.14c: the refresh's source question, verified (no production code changed); task 3.14 stays open.** Both candidate sources for an eager refresh request at the undo edge are rejected on their own documentation: `RuntimeCompanionPasses` is an operation-scoped preparation whose `ready(source)` gate binds it to the exact frozen identity it was prepared against, and `RuntimeCanonicalPassInputs` says to *invalidate before pass/history mutation or release* — and the undo edge's act is precisely that mutation. The remaining option (the existing request path's enumerated scratch) is **measurably gated**: with a served window and a real `TrackUndo::undoForLoop`, readiness stops being `Ready` (a rebuild is owed, named in the owner's vocabulary) and `requestPlaybackWindow` answers **`SourceNotCurrent`** — the lagging root is named, never served as current (DEC-073's protection = task 4.1's outcome). So the refresh's requester belongs to **4.1 → 3.7**, while the edge already advances the currency, settles the closures, keeps the ledger, silences nothing and enumerates nothing. Verification: focused **64/64** (fixture extended again), full native **2695 / 2688 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the five previous slices); three mutations red-proved with byte-identical restores, two of them deliberately against other owners' code (the not-current guard and the revision bump) so the fixture tests the claim it states. **Not claimed:** the refresh is not performed at the edge (now with the measurement for why), and the between-grains matrix, retained-source lifetime, displayed provenance and preview-session undo/redo remain. Evidence: [`evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s314c-refresh-source-verification/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.1: checkpoint parity is *named* (F-5); the task stays open on the content-level comparison.** The parity rule already existed as a bool (`publishedSnapshotIsCurrent()`), but the requirement demands the lagging case be **named** in the readiness/lag vocabulary and never presented as current, and nothing named it. Landed: `CheckpointParity { NoCheckpoint, Current, OwesReconciliation, CoverageUnrecorded }` + the pure `resolveCheckpointParity` (`include/LoopSnapshot.h`), and `Loop::checkpointParity()` (`src/Loop/LoopSnapshotLifetime.cpp`) reading only the loop's own figures — no new state (`sizeof(Loop) == 984` still holds by the build's own assert) and no size pin moved. It cannot drift from the predicate it names: `storePublishedSnapshot` resolves the `kSnapshotCoverageCurrent` sentinel into a real revision as it stores it, so `parity == Current` iff `publishedSnapshotIsCurrent()` for every reachable state, and the fixture asserts that agreement. Through the **production** undo path a pushed entry's root is restored **with the coverage it carried** (DEC-073) while the committed-pass revision advances — the answer is `OwesReconciliation` with `publishedSnapshotIdentity()` **null**: selected, never presented as current; DEC-073's demotion is separately named `CoverageUnrecorded`. Neither undo nor save consults the window's lag (a not-ready window lane leaves a `Current` checkpoint `Current`), which is what makes this consistent with 3.14c's `SourceNotCurrent` refusal. Verification: focused **65/65** (fixture added), full native **2696 / 2689 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** on a true clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the six previous slices); four mutations red-proved with byte-identical restores. **Two harness facts recorded rather than glossed:** the first build attempt was a 2.8 s cached link (deleting `.pio/build/…` deletes nothing — `build_dir` is `~/Library/Caches/pio-build-250513-215524-teensy41`), and M50's first attempt reported "green" with its anchor in the wrong **file**, the same class of error as 3.14b's M44. **Not claimed:** no production caller consumes the name yet (4.2 and the readiness/lag reporting will), and 4.1 stays open on the content-level comparison — its instrument is 4.3. Save parity is 4.2. Evidence: [`evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s41-checkpoint-parity/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.2: save parity is judged on the serialized root (F-6); the task stays open on the repair its own falsification demands.** Asserted through the **production** writer (`writeLoopPersisted`) and the **production** restore edge (`stepLoopSnapshotBodyFormatV1LoadParse` → `applyLoadedLoopSlotBodyCommit`): the installed root is **equivalent** to the root that was serialized (content-for-content, `assertSnapshotsEquivalent`; pointer identity was my mistake and failed), the body's committed state arrives with it, and the restore's coverage outcome is **named** — `CheckpointParity::Current` with `publishedSnapshotIdentity()` non-null, 4.1's name applied to the save consumer. The only production edit is a **comment**: the restore edge's over-claiming DEC-073 note is replaced by the boundary statement, since that claim rests on the **writer's** admission (RC2), not on a derivation. The gap was **verified**: the LSV1 root section persists no provenance figure, the reader validates presence and per-section structure but never the root's correspondence to the passes, and the pre-existing restore-edge fixture asserted only presence. **The falsification is measured, not pinned:** a temporary probe (removed before commit) built a body whose metadata and root sections cannot come from one committed state; today's path installs that root and reports it **current** — the "silently current" outcome the requirement forbids. Recorded: the reader merges the metadata **into** the oracle, so the divergence must be metadata-vs-root to survive (the first probe attempt measured nothing). Repairs: persisting the root's coverage = a **format decision** (owed); a cheap cross-check of the root's figures vs the committed state's = **refuted** (`EditSelectNoteState.cpp:188` → `Loop::allocateNoteId()` can advance `nextNoteId_` with the committed state — and a settled root — unchanged); reconciling root vs the body's own passes at load = owed, a design task. Verification: focused 52/52 (+1 fixture), 65/65, 190/190 (cited pin), 38/38; full native **2697 / 2690 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** on a true clean rebuild with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the seven previous slices); four mutations red-proved with named-file, count-asserted anchors and byte-identical restores, their bound stated (a positive-outcome fixture cannot falsify an over-claiming parity). **Not claimed:** no behaviour change; the hole is not closed or narrowed; **4.2 stays open on the repair.** Evidence: [`evidence/2026-10-02-s42-save-parity/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s42-save-parity/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.3: the served window's stream is readable and content parity across a window boundary is proven natively (F-7); the task stays open on its device leg and on the obligation the measurement names.** `Track::playbackWindowEventAtForSlot(slot, cursor, out)` exposes the served window's ordered events exactly as the emission path renders them (`storage.events[storage.order[cursor]]`) — read-only, O(1), no new state, served window only (never a prepared successor). The gap was **verified**: the lane had identity/coverage/count/presence and no way to read content, nothing in production consumes a window's events (`stepPlaybackViewMaintenance` returns `Idle`; row 27 denies active windowed playback), and no fixture read a built window's content. The proof: **32** successive two-bar windows over a 64-bar demoted loop, each built and served interval asserted, concatenated and compared **row for row** with the whole-loop stream from the **existing** resolver (`LoopContentResolution::resolveWindow`) — nothing lost, duplicated or reordered across 31 boundaries. The boundary note's ON is served by tile 0 and its OFF by tile 1 (**not** synthesized at the edge), so the concatenation balances while either window alone does not: the transition's closure is the **consumer's** obligation (3.7 with 4.5's ledger set). Also measured: the centred policy makes arbitrary-phase consecutive windows **overlap** (a consumer must not double-serve; 3.7), and the first window installs as served while replacements publish as the ready successor for adoption. Verification: focused 66/66 (+1 fixture), 52/52, 190/190 (cited pin), 38/38; full native **2698 / 2691 succeeded / 7 skipped / 0 failed**; clean rebuild **SUCCESS** with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, byte-identical to the eight previous slices); four mutations red-proved with byte-identical restores, their bound stated (the row comparison is protected by the interval/coverage pins the mutations trip first). **Not claimed:** no production consumer, no device/HITL evidence (6.4's windowed profile and 6.5's split are owed), loop-wrap content not exercised; **4.3 stays open** on the device leg, the closure obligation and the overlap rule. Evidence: [`evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s43-window-boundary-parity/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 4.4: the residency statement per slot is length-independent, swept over loop lengths (F-8); test-only.** The sweep covers **16, 17, 24, 30, 32 and 64 bars** through the production policy and admission (`fullTakePlaybackViewAdmissible` + `prewarmPlaybackForSlot`, which derives the representation itself), read back from the slot's accounting. Verified: the boundary is the declared bar cap; the whole-take representation is reserved **exactly while admissible and absent at every demoted length**; **every demoted length reports the same windowed reservation** (strictly smaller than the admissible figure, so it is absent rather than unused), views at `kEventsPerView`, stagings resident; at/above the declaration's floor the slot is **served from a window while its root is demoted**, and below it the refusal is **named**. **The floor as a number:** the declaration's own arithmetic gives a lead of 22,272 ticks ≈ 29 bars, so the floor is **exactly 30 bars**, and loops between the bar cap and it have neither representation admissible — the number 6.3/3.10 need, pinned from the contract's fields rather than hardcoded, and recorded as deliberately conservative. Verification: focused 67/67 (+1 fixture), 52/52, 190/190 (cited pin), 38/38; full native **2699 / 2692 succeeded / 7 skipped / 0 failed**; build **SUCCESS** and a no-op (2.7 s, no source changed) with the RAM1 line unchanged (48 B headroom); three mutations red-proved with byte-identical restores. **Two limits stated:** the admissibility oracle is the policy function itself (the contract's declared numbers are 3.10/6.3's to own), and the absolute reservation figure is cited from `test_playback_prewarm`. **Not claimed:** the parity half (4.3's, and the spec names the content-verification pipeline: 6.4/6.5 owed), the retained-source term (3.6a), no device evidence; row 27 still denies active windowed playback. **4.4 stays open** on those. Evidence: [`evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s44-length-independent-residency/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.10, first increment: fair, bounded, deadline-ordered service at the holder level (DEC-108 §13b); the task stays open on the wiring and the starvation/envelope halves.** The owner rotation already existed and was already proved (the unconditional 7-owner cycle, one turn per owner per cycle); the gap was the level below it — which **holder** (one slot's owed build) an admitted `PlaybackViewMaintenance` turn serves, where the scan was ascending first-match with **no cursor** and **no deadline was stored at all** (`beginCoverageBuild` drops `handoffPhase`). `include/Utils/MidiServiceDrain.h` gains the pure `selectPlaybackHolder(cursor, holders, count, out)` with `PlaybackHolder` (eligible / can progress / outstanding deadline) and the named `PlaybackHolderSelection { None, Service, AllBlocked }` — header-only, no allocation, **no new state** (the deadline is the within-interval phase `requestPlaybackWindow` already passes as `handoffPhase`). Proved as five bounds: each able holder served exactly once per rotation with the next rotation continuing from the cursor; earliest outstanding deadline first (deadlines deliberately reversed against the holder order); a deadline outranks none while deadline-less holders still rotate evenly; a **blocked holder is never chosen** and is served within one rotation of becoming able; and "nothing owed" is distinct from "all blocked", with the rotation advancing in both (never a latch). Earliest-deadline-first is global over the eligible set — the tighter reading, from which the per-holder one follows. Verification: focused `test_midi_service_drain` **39/39** (+1 fixture), 67/67, 52/52, 190/190 (cited pin); full native **2700 / 2693 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** with `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B headroom, the policy inline and unconsumed); four mutations red-proved with byte-identical restores. **Recorded:** the fixture's outcome block was wrong on its first attempt, not the code (no eligible holder is `None`; `canProgress` defaults true). **Not claimed:** no production caller yet (the holder scan and the `main.cpp` track scan stay ascending first-match; nothing carries the handoff phase per holder); the starvation half is structural only; the envelope is bounded in turns with the grain-cost/worst-gap derivation owed; and "one declared grain per turn" is not yet a uniform invariant. **3.10 stays open** on all four; row 27 still denies active windowed playback. Evidence: [`evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310-holder-service-fairness/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.10, increment 2: the urgency figure is named once and shared; a fidelity gap in increment 1 is corrected (DEC-108 §13b).** Increment 1 ordered holders by a raw phase, meaningless across holders whose loops differ in length. `handoffLeadTicks(currentPhase, handoffPhase, loopLengthTicks)` now lands beside `CoverageServiceContract` in `include/RuntimePlaybackViewBuild.h` — the tick distance along the loop to a handoff, **named once** as the contract's own `available` sub-term — and `canCompleteBeforeHandoff` calls it instead of computing it inline, so the admission and the scheduler cannot diverge; the holder policy orders by those ticks. The fixture pins the pair that shows why: a holder 1,400 ticks out on a 2,000-tick loop versus one 18,200 ticks out (a whole pass) on a 20,000-tick loop, where the **more urgent holder carries the larger phase**, plus the wrap case (a handoff behind the playhead is a full pass away) and the refusals for an undeclared loop and an out-of-range phase (`0`, never a wrap). Verification: `test_midi_service_drain` **39/39** (fixture extended), `test_runtime_working_set_ownership` **67/67** (the admission side), full native **2700 / 2693 succeeded / 7 skipped / 0 failed**, build **SUCCESS** with the RAM1 line unchanged (48 B headroom); three mutations red-proved with byte-identical restores — the wrap branch dropped (also failing **11 cases in the sibling suite**, the point of one definition), the out-of-range refusal dropped, and "a deadline beats none" inverted. **Recorded:** two of this task's own fixture attempts were wrong, not the code. **3.10 stays open** on the wiring (no production caller yet; nothing computes the lead for a live holder), the starvation half, the envelope derivation, and one declared grain per turn as a uniform invariant. Evidence: [`evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.10, increment 3: the wiring (the holder scan routes through the policy); its discriminating fixture is owed and named.** `Track::stepPlaybackViewMaintenance(currentPhase = 0, cursor = nullptr)` takes the playhead's phase and the caller's rotation cursor, builds the holder set from live state (each owing slot's urgency from its **in-flight build's interval**, which `RuntimePlaybackViewFill::begin` already sets — **no new field, no pin move**) and lets `selectPlaybackHolder` choose; the grain served is unchanged; `src/main.cpp`'s single production call site passes the phase and one cursor. `canProgress` is structurally true for an owing holder today, stated rather than faked. **The RAM1 guard rejected the wiring twice** — an 8-cursor array and then a single DTCM byte each cost +32 B of `variables` against the approved 96,192 — so the cursor is **`DMAMEM`** (DEC-108 §13b's own placement rule; RAM2 has 362 KB free) and the approved line is **untouched, not re-based**. Verification: `test_runtime_working_set_ownership` **67/67**, `test_midi_service_drain` **39/39**, full native **2700 / 2693 succeeded / 7 skipped / 0 failed**; build **SUCCESS** with `variables 96,192` unchanged (48 B headroom). **Not claimed — the increment's own gate:** the wiring has **no discriminating fixture**; its new behaviour needs **more than one owing holder on one track** (every existing fixture has exactly one), so those fixtures cannot distinguish the routed scan from the old ascending one. **OWED, specified:** two slots on one track, both owing, the urgent one at the higher slot index and one grain from publication, asserting the single turn publishes the **urgent** slot rather than slot 0, plus a persistent cursor interleaving them. Until then the rotation and deadline order are **proved as a policy** (increments 1–2) and **wired but not proved end-to-end**. Nothing in production owes a window today, so no reachable behaviour changed. Evidence: [`evidence/2026-10-02-s310c-routed-scan/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310c-routed-scan/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.10, increment 4: the owed fixture lands and the wiring's gate is met (DEC-108 §13b; no production change).** `test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them` makes the routed scan's behaviour observable through each holder's **own next grain** ("progressed" past `Fill` — a small window finishes outright as `Complete`, the state my first version wrongly expected as `Order`). Three cases on their **own slot pairs** (a slot that already owes answers `BuildInFlight`, and that check is **per slot**, so two slots on one track can owe simultaneously — measured): (A) far vs 32-ticks-away → the **urgent** holder progresses and the far one does not, where the pre-3.10 ascending scan served slot 0; (B) the roles swapped → slot 0 progresses, the control that (A) is not "the higher index always wins"; (C) equal urgency → one persistent cursor progresses **both** across two turns. Verification: `test_runtime_working_set_ownership` **68/68** (+1 fixture), `test_midi_service_drain` **39/39**, `test_storage_loop_io` **52/52**, `test_playback_prewarm` **190/190** (cited pin); full native **2701 / 2694 succeeded / 7 skipped / 0 failed**; build **SUCCESS**, RAM1 line unchanged (48 B headroom). Two mutations red-proved with byte-identical restores — **M76 (the decisive one): reverting the scan to the pre-3.10 ascending order fails case A**, so the fixture does distinguish the routed scan from the old one (the RWO "every fixture green" trap closed here); M77 (urgency ignored) also fails case A. **3.10 stays open** on the starvation half, the envelope derivation and one declared grain per turn, plus the new question the fixture raises: it exercises holders within a track, while the same starvation argument applies to the `main.cpp` loop that stops at the first *track* with work. Evidence: [`evidence/2026-10-02-s310d-routed-scan-fixture/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310d-routed-scan-fixture/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.10, increment 5: the cross-track offer and selection; the question increment 4 raised is measured (DEC-108 §13b).** `Track::playbackWindowServiceOffer(currentPhase)` reads a track's **most urgent owing holder without serving**, with `fillPlaybackHolders` as the one holder description shared by the offer and the turn; `src/main.cpp` builds **every track's offer** and lets `selectPlaybackHolder` choose **one track** per turn, replacing the loop that stopped at the first track with work. The *track* level is where the starvation argument bites (a track almost always owes one holder), and the tick lead is what makes different tracks comparable. Fixture on two **real** tracks: offers answer read-only and both still owe; the policy picks the **urgent track though it is the higher index**; equal urgency rotates; the choice is executable; and a track with two owing holders offers its **most urgent** — **that last block exists because a red-proof found the gap** (M79 passed before it, fails after). Verification: `test_runtime_working_set_ownership` **69/69** (+1 fixture), `test_midi_service_drain` **39/39**, `test_storage_loop_io` **52/52**, `test_playback_prewarm` **190/190** (cited pin); full native **2702 / 2695 succeeded / 7 skipped / 0 failed**; build **SUCCESS** with `variables 96,192` **unchanged** (48 B headroom). **Not claimed:** the caller's own shape is **build-verified, not fixture-proved** (`src/main.cpp` is not native-linkable), nothing requests windows in production yet, and the **starvation half**, the **envelope derivation** and **one declared grain per turn** remain open. Evidence: [`evidence/2026-10-02-s310e-cross-track-offer/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s310e-cross-track-offer/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.9, increment 1: the readiness receipt and its two delivery rules (DEC-108 §13b / D10); the task stays open on the emission site.** `DebugSessionCapture::readinessReceiptImmediate(track, slot, readiness, holder, revision, leadTicks)` is implemented exactly as the precedent is — `serialWriteRoom(96)` → a direct `Serial.printf`, otherwise `emitCapPrintf` → the ring — with the `#ifndef`-guarded `SC_WINDOW_READINESS` macro in both regions so a native suite can substitute a recorder; row `#CAP,<micros>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`. The direct path is the point: a window is prepared **under transport** and the ring is retained by design while a transport runs, so an enqueued receipt arrives after the transport stops — after the readiness it explains. The row carries readiness **and its holder** (3.5's vocabulary), the slot that scopes it, and the revision and lead it explains; the field list beyond the holder is a stated choice (the design names the delivery shape only). Fixture in `test_capture_ring`: with room the row reaches the stream and **nothing is enqueued**; with no room it is **delayed rather than lost** — **the second half closes a gap the precedent's own fixture never covered**. Verification: `test_capture_ring` **16/16** (+1 fixture), `test_runtime_working_set_ownership` **69/69**, `test_midi_service_drain` **39/39**, `test_playback_prewarm` **190/190** (cited pin); full native **2703 / 2696 succeeded / 7 skipped / 0 failed**; build **SUCCESS** with `variables 96,192` **unchanged** (48 B headroom). Two mutations red-proved with byte-identical restores. **Not claimed:** nothing in the lane **emits** it yet — the emitting call site (the owner turn's terminal outcomes and the refusal path, silent by row today) and the cadence bound (`TelemetryCadence::shouldLogOccurrence` with a `DMAMEM` counter; no `src/Track/*` consumer exists) are the next increment, so task 3.9's wording is **half met**; no device evidence. Evidence: [`evidence/2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md).

**2026-10-02 — `windowed-playback-generation` slice 3.9, increment 2: the receipt's emission site and its cadence; and the RAM1 edge (DEC-108 §13b / D10).** The lane now emits what increment 1 learned to deliver: `playbackWindowReadinessLabel` names the outcome and `reportPlaybackWindowReadinessReceipt(slot, currentPhase)` checks **cadence first** (`TelemetryCadence::shouldLogOccurrence` with a `DMAMEM` counter — that header's first `src/Track/*` consumer) then reports readiness + holder + the revision it judged + the lead from the served window, called on the owner turn's two **terminal outcomes** (`GrainDenied`, `BuildComplete`) and never per grain. **The RAM1 guard caught a second +4096 and the precedent's fix covered it:** the wiring first FAILED the approved budget (`variables 100,288`), and the cause was the compiler's own **jump tables** (`.rodata.CSWTCH.*`, unmatched by the linker script's patterns — an if/else chain over a dense enum does not stop the optimizer tabulating it), so the script's rule for this TU was widened to `.rodata.CSWTCH*`, restoring the line **exactly** (`variables 96,192`, ITCM `code` byte-identical, **no re-base**). Fixture: the receipt's content (slot, readiness `ready`, holder `ready`, revision) via the `#ifndef`-guarded `SC_WINDOW_READINESS` recorder — the production call site — plus the **cadence bound**. Verification: `test_runtime_working_set_ownership` **70/70** (+1 fixture), `test_capture_ring` **16/16**, `test_midi_service_drain` **39/39**, `test_playback_prewarm` **190/190** (cited pin); full native **2704 / 2697 succeeded / 7 skipped / 0 failed**; build **SUCCESS** with the approved RAM1 line; **M82** (the cadence gate dropped) fails the fixture, so the bound is load-bearing. **Recorded, not smoothed:** the first cadence assertion expected 2 rows for 17 completions and measured **1**, and why only one outcome reached the reporter is **not established here** — restated as the bound, question named. **Not claimed:** the **refusal paths remain silent by row**; "one pass late but served" has no row; the interval (16) is a declared choice; no hardware observation; and fixture isolation was **repaired, not designed**. Evidence: [`evidence/2026-10-02-s39b-receipt-emission-site/EVIDENCE.md`](../../openspec/changes/windowed-playback-generation/evidence/2026-10-02-s39b-receipt-emission-site/EVIDENCE.md).
