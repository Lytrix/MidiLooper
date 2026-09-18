# Decision log

Persistent record of **accepted architectural and implementation decisions**. Not a changelog, roadmap, or meeting notes.

**Agents:** run [DECISION_REVIEW.md](Templates/DECISION_REVIEW.md) before implementation; append new decisions at [session closeout](Templates/SESSION_CLOSEOUT.md).

| Rule | Meaning |
|------|---------|
| Append only | Never delete; supersede with a new entry (`Status: Superseded`) |
| Search before design | `rg` this file + active OpenSpec + relevant `docs/Plans/` |
| Challenge via reassessment | Redesign is allowed through [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) + new log entry |

## Index

| ID | Date | Topic | Status |
|----|------|-------|--------|
| [DEC-076](#dec-076--runtime-working-sets-are-role-owned-and-memory-scales-with-active-roles) | 2026-09-18 | Runtime working sets are role-owned and memory scales with active roles (**Playhead-scheduled MVCC**) | **Accepted — design authority recorded; implementation staged (S0-S7), nothing built yet.** Governs `runtime-working-set-ownership` |
| [DEC-075](#dec-075--the-snapshot-publication-boundary-admits-memory-per-construction-turn-and-parks-on-allocation-failure) | 2026-09-18 | The snapshot publication boundary admits memory per construction turn and parks on allocation failure | Accepted — implemented; native 2249 / 1 skipped / 0 failures, RAM1 unchanged; device gate owed |
| [DEC-074](#dec-074--the-lsv1-body-admits-only-a-settled-root-the-format-is-pinned-once-and-the-fallback-is-the-passes-authoritative-legacy-body) | 2026-09-18 | The LSV1 body admits only a settled root; the per-loop format is pinned once and the fallback is the passes-authoritative legacy body | Accepted — implemented and device-verified (settled LSV1 save path, `q=0` publishes, post-reboot `storage_matches_effective`); native reproducer red→green; RAM1 allowance restored |
| [DEC-073](#dec-073--a-published-snapshot-root-carries-the-committed-pass-coverage-it-was-built-from-current-is-coverage-not-a-revision-stamp) | 2026-09-18 | A published snapshot root carries the committed-pass coverage it was built from; "current" is coverage, not a revision stamp | Accepted — RC1 implemented and device-verified (post-reboot playback parity, 5/5 cycles, 0 missing); full native 2246 / 1 skipped / 0 failures; RC2 landed as DEC-074 |
| [DEC-072](#dec-072--a-staged-capture-pass-is-owned-by-its-capture-interval-not-by-the-loop-producer-slot) | 2026-09-17 | A staged capture pass is owned by its capture interval, not by the Loop producer slot | Accepted — implemented; former `REENTRY_RED` reproducer green; device PASS [`successor-ownership-06`](../captures/successor-ownership-06/session_20260917_230432.log) |
| [DEC-070](#dec-070--a-transport-stop-retarget-must-re-route-a-mid-flight-deferred-stop-job-not-only-re-label-its-destination) | 2026-09-17 | Transport-stop retarget must re-route a mid-flight deferred stop job, not only re-label it | Accepted — implemented; native case fails without it; device PASS [`successor-wrap-40`](../captures/successor-wrap-40/session_20260917_195608.log) |
| [DEC-069](#dec-069--the-overdub-wrap-is-a-canonical-revision-transition-and-must-not-advance-the-revision-a-pending-stop-job-owns) | 2026-09-17 | The overdub wrap must not advance the loop revision a pending deferred stop job owns | Accepted — Option A implemented; native green; device PASS [`successor-wrap-36`](../captures/successor-wrap-36/session_20260917_193344.log) |
| [DEC-068](#dec-068--an-overdub-session-belongs-to-one-capture-interval) | 2026-09-17 | An overdub session belongs to one capture interval; a deferred job carries its own session-exit receipt | Accepted — implemented; six native cases pass, device retirement proof PASS [`successor-reentry-27`](../captures/successor-reentry-27/session_20260917_163416.log) |
| [DEC-067](#dec-067--finalization-resource-admission-is-stop-directed) | 2026-09-17 | Overdub START admits on the live role; canonical/transfer/session-exit capacity is checked at STOP | Accepted — device START/STOP proof PASS [`successor-reentry-26`](../captures/successor-reentry-26/session_20260917_161119.log) |
| [DEC-066](#dec-066--per-owner-capacity-derivation-for-the-stop-working-set-under-the-existing-ceilings) | 2026-09-17 | Stop working-set capacity derived per owner under the existing 2,048 ceilings | Accepted — implemented and device-verified |
| [DEC-065](#dec-065--capture-staging-is-provisioned-by-capture-admission-not-by-boot) | 2026-09-17 | Capture staging is reserved by capture admission (record arm, overdub start), not at boot | Accepted — validated on device |
| [DEC-063](#dec-063--protected-stop-undo-publication-and-bounded-reclamation) | 2026-09-14 | Protected stop transaction preserves feedback trimming; latest deferred Undo/Redo wins per Track | Accepted — implementation authorized; validation gates remain open |
| [DEC-062](#dec-062--provisional-ranked-preparation-allowance) | 2026-09-14 | Allow 6240 ranked-preparation grains for integration; realtime reservations determined with HITL | Accepted — provisional; validation/HITL gates remain open |
| [DEC-061](#dec-061--stable-raw-tick-ordering-after-create) | 2026-09-14 | Create appends then stably sorts raw ticks, including empty payloads | Accepted — explicit semantic correction; task 2.3 and validation/HITL gates remain open |
| [DEC-060](#dec-060--canonical-per-pass-companion-application) | 2026-09-13 | Combined capture preparation applies companions per canonical pass before merging | Accepted — user endorsed; task 2.3 and validation/HITL gates remain open |
| [DEC-059](#dec-059--deterministic-companion-pairing-lane-runs) | 2026-09-13 | Deterministic filtered-lane runs replace the invalid companion-pair comparator | Accepted — semantic correction; task 2.3 and validation/HITL gates remain open |
| [DEC-058](#dec-058--runtime-owned-derived-playback-views-and-bounded-stop-admission) | 2026-09-13 | Runtime-owned derived playback views and bounded deferred overdub stop; canonical content remains in `Loop` | Accepted — implementation authorized; capacity, grain, coverage, stop-latency, and MIDI-trace gates remain required |
| [DEC-057](#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write) | 2026-09-11 | LoopSnapshot body format version 1 (`LSV1` wire discriminator); dual-write `PersistedLoopSnapshot` + `LoopSnapshot`; load undo depth 0 | Accepted — OpenSpec task 5.2 docs/decision only; firmware is 5.3 |
| [DEC-056](#dec-056--note-edit-rows-enter-the-existing-pending-companion-set-when-the-published-root-is-stale) | 2026-09-08 | NOTE_EDIT `saveNoteEditPass` admits the row into `pendingSnapshotPublishBox.companionIds` when the published root is already stale; not a new publication owner | Accepted — miss-path device PASS [`000740`](../captures/session_20260909_000740.log); current-root close [`000237`](../captures/session_20260909_000237.log) |
| [DEC-055](#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot) | 2026-09-08 | NOTE_EDIT working set is overlay delta on the opened LoopSnapshot; no second full-loop representation for edit or undo | Accepted — Stage 3 device PASS [`221906`](../captures/session_20260908_221906.log); Stage 2 device PASS [`220445`](../captures/session_20260908_220445.log); Stage 1 device PASS [`212850`](../captures/session_20260908_212850.log) |
| [DEC-054](#dec-054--note-edit-playing-display-stall-remainder-units) | 2026-09-08 | NOTE_EDIT playing display stall: coalesce/post-display undo warm and deferred open/close | Accepted — Stage 1 admission in tree; Option B rejected by DEC-055; Stage 2 still separate |
| [DEC-053](#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary) | 2026-09-08 | `T_RT_reserve` is a 10 ms policy ceiling plus per-turn MIDI-boundary `reserve_us`; not 60% of `T_clock` and not an observed unit | Accepted — yield device proof parked; 3.5/3.6 closed |
| [DEC-052](#dec-052--occupy-consumes-the-draw-source-predicate) | 2026-09-02 | Occupy committed source uses the paint draw-source predicate; exclusive snapshot fill when true | Accepted |
| [DEC-051](#dec-051--overdub-wrap-seals-at-loop-end-not-session-start) | 2026-09-01 | Overdub wrap persist seals at loop end (phase 0); S remains session identity | Accepted |
| [DEC-050](#dec-050--occupancy-at-noteon-and-per-bar-draw-routing) | 2026-09-01 | Occupancy resolves at NoteOn into pending effective transforms; draw source is per-bar routing only | Accepted |
| [DEC-049](#dec-049--overdub-stop-admits-the-seal-and-accelerates-snapshot-publication) | 2026-08-31 | Overdub stop preserves the merged stream for incremental seal admission; Snapshot idle uses a fixed 2-of-6 ratio while a publication backlog is present | Accepted |
| [DEC-047](#dec-047--bounded-overdub-wrap-snapshot-publication) | 2026-08-27 | Bounded overdub-wrap snapshot publication through mutation journals and fixed-capacity membership pages | Accepted — Phase 3 tasks 3.5–3.6 |
| [DEC-046](#dec-046--round-robin-admission-over-playback-relevant-destinations-representation-coverage-is-a-prerequisite) | 2026-08-26 | Round-robin admission over playback-relevant destinations; representation coverage is a prerequisite; `RECORDING` included; G1 consumes RC4's post-resolution cost | Accepted — architecture direction; firmware Slices 1–4 not authorized |
| [DEC-045](#dec-045-overdub-snapshot-publish-is-idle-stepped) | 2026-08-25 | Overdub `LoopSnapshot` publication queues at stop and steps from idle | Accepted |
| [DEC-044](#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction) | 2026-08-25 | Immutable loop snapshots replace maintained LCR as the effective-state direction on the feature branch | Accepted — design phase; amended for LoopPass vs LoopSnapshot, persistent reference root, provisional/global snapshots, and latest-saved reboot |
| [DEC-043](#dec-043-execution-bound-class-is-a-gated-architectural-property) | 2026-08-22 | Execution bound class is a gated architectural property; `admit(WorkClass)` stays rejected | Accepted |
| [DEC-042](#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem) | 2026-08-19 | Same-pitch active-note identity is cardinality, not Off identity; open-NoteOn ledger | Implemented; nested HITL MET [`095902`](../captures/session_20260819_095902.log); extra-open is catch-up stack RC |
| [DEC-041](#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) | 2026-08-18 | Occupy present-at-S is JIT; full-loop `lcr,mat` is not occupy readiness | Accepted |
| [DEC-040](#dec-040-skip-playingstoppedmuted-overdub-participant-hitl) | 2026-08-18 | Skip PLAYING/STOPPED/MUTED overdub participant HITL | Accepted |
| [DEC-039](#dec-039-persist-noteid-reconciled-at-note-edit-commit-boundary) | 2026-08-16 | Persist NoteId reconciled at NOTE_EDIT commit boundary | Accepted |
| [DEC-038](#dec-038-overdub-wrap-commit-and-session-undo) | 2026-08-15 | Overdub wrap commit at start-tick S; session-gated undo; one U: on stop | Accepted |
| [DEC-037](#dec-037-loop-content-resolution-parallel-prototype) | 2026-08-14 | LoopContentResolution parallel prototype; NOTE_EDIT hydrate and playback gather are separate work paths (amended 2026-08-16/17) | Accepted |
| [DEC-036](#dec-036-runtime-effective-event-source-for-overdub) | 2026-08-14 | Runtime effective event source; overdub entry without display reconstruction | Accepted |
| [DEC-035](#dec-035-loop-persists-content-only) | 2026-08-14 | Loop persists content only; undo/redo is derived and not persisted | Accepted |
| [DEC-034](#dec-034-overlap-shorten-seals-at-the-user-triggered-commit) | 2026-08-13 | Overlap shorten seals at the user-triggered commit; closure defers leave-restore only | Accepted |
| [DEC-033](#dec-033-overdub-overlap-ignores-per-note-channel) | 2026-08-12 | Overdub overlap uses loop-scoped notes; no per-note channel filter | Accepted |
| [DEC-032](#dec-032-overdub-editpass-unification-reassessment) | 2026-08-12 | G2: unify resolution; dual storage transitional | Accepted |
| [DEC-031](#dec-031-overdub-overlap-encode-pending-buffer-to-editpass) | 2026-08-12 | Transitional dual-seal encode + undo/restore pins | Accepted (encoding) |
| [DEC-030](#dec-030-sticky-overlap-end-of-participation-on-current-state) | 2026-08-08 | Sticky overlap end-of-participation on NoteEditCurrentState | Accepted |
| [DEC-029](#dec-029-noteeditcurrentstate-owns-note-edit-editable-note-state) | 2026-08-07 | NoteEditCurrentState owns NOTE_EDIT editable note state | Accepted |
| [DEC-028](#dec-028-editsessionaction-geometry-pipeline-phase-1-native) | 2026-08-04 | EditSessionAction geometry pipeline Phase 1 native | Accepted |
| [DEC-026](#dec-026-commit-centered-lazy-slot-load) | 2026-07-18 | Commit-centered lazy slot load (audible boot + on-demand hydrate) | Accepted |
| [DEC-025](#dec-025-split-focus-playing-preview-pending) | 2026-07-09 | Split focus: playing / preview / pending; committed-transition invariant | Accepted |
| [DEC-024](#dec-024-loop-owned-undo-ownership-direction) | 2026-07-08 | Loop-owned undo ownership direction (Phase 1 filter, Phase 2 migrate stack) | Accepted |
| [DEC-022](#dec-022-runtime-bundle-save-tail-integrity) | 2026-07-07 | Runtime bundle save tail integrity (meta temp truncate + append cursor) | Accepted |
| [DEC-021](#dec-021-defer-inactive-loop-slot-restore-at-boot) | 2026-07-07 | Defer inactive loop slot restore + undo bodies at current set restore | Accepted; amended 2026-07-09 |
| [DEC-020](#dec-020-continuous-runtime-persistence-architecture) | 2026-07-07 | Continuous runtime persistence — invariant-driven capture-chunk persistence | Accepted |
| [DEC-019](#dec-019-sd-load-path-extmem-routing-m5-spike) | 2026-07-07 | SD load path extmem routing (M5 spike) | Accepted (spike) |
| [DEC-018](#dec-018-admission-current-heap-derived-rep-consolidation) | 2026-07-07 | Admission uses current heap; derived-rep OpenSpec consolidation | Accepted |
| [DEC-017](#dec-017-skip-long-hitl-gates-implement-runtime-redesign) | 2026-07-07 | Skip long HITL/capture gates; implement runtime redesign | Accepted |
| [DEC-016](#dec-016-runtime-architecture-four-layer-model) | 2026-07-07 | Runtime architecture four-layer model | Accepted |
| [DEC-015](#dec-015-interval-projection-stage-1-stage-2-split) | 2026-07-05 | IntervalProjection module + Stage 1/2 split | Accepted |
| [DEC-014](#dec-014-dual-normalization-boundaries-micro-vs-macro) | 2026-07-03 | Dual normalize micro/macro | Accepted |
| [DEC-013](#dec-013-linear-loop-tick-validate-vs-normalize) | 2026-07-03 | Linear loop tick validate vs normalize | Accepted |
| [DEC-012](#dec-012-storagesession-persistence-state-model) | 2026-06-29 | StorageSession persistence state | Accepted |
| [DEC-011](#dec-011-bias-toward-progress) | 2026-06-29 | Bias toward progress | Accepted |
| [DEC-010](#dec-010-ownership-evolution-protocol) | 2026-06-29 | Ownership evolution protocol | Accepted |
| [DEC-009](#dec-009-runtime-state-vs-roadmap-split) | 2026-06-29 | Runtime state vs roadmap | Accepted |
| [DEC-008](#dec-008-authority-conflict-resolution) | 2026-06-29 | Authority conflict resolution | Accepted |
| [DEC-007](#dec-007-historical-decision-reuse-enforcement) | 2026-06-29 | Historical decision reuse | Accepted |
| [DEC-006](#dec-006-agent-context-harness-vs-chat-history) | 2026-06-29 | Agent context / chat history | Accepted |
| [DEC-005](#dec-005-gpio-base-module-vs-droid-only-actions) | 2026-06-29 | GPIO base vs DROID actions | Accepted |
| [DEC-004](#dec-004-recordoverdub-stop-validation) | 2026-06-29 | Record/overdub stop validation | Accepted |
| [DEC-003](#dec-003-d13-jam-recording-ordering) | 2026-06-29 | D13 jam recording order | Accepted |
| [DEC-002](#dec-002-set-revision-vs-flat-savedset-snapshot) | 2026-06-29 | Set revision vs flat SavedSet | Accepted |
| [DEC-001](#dec-001-loadsave-overlay-confirm-control) | 2026-06-29 | Load/save overlay confirm | Accepted |

---

<!-- Append new entries below (newest first). Next ID: DEC-077 -->

## DEC-076 — Runtime working sets are role-owned and memory scales with active roles

**Date:** 2026-09-18
**Status:** **Accepted — design authority recorded; implementation not started** (staged S0–S7). This entry fixes
the law and the vocabulary; it does not claim shipped code.
**Owner:** `TrackPlaybackRuntime` / `LoopPlaybackRuntime` working sets, `Loop` derived state, the deferred
publication owners, `DebugSessionCapture` evidence, and the display owners named in the migration stages.
**Follows:** DEC-073 (coverage-based currency), DEC-074 (settled-root LSV1 admission), DEC-075 (publication
admission + parking), DEC-067/DEC-068/DEC-072 (successor reentry and interval-owned staged passes — the
concurrency this decision must not design away). Supersedes the *strategy framing* of
`immutable-loop-snapshot-passes` RC-MEM-2 (its landed part stands) and reclassifies its §5.13 arm failure as a
symptom.

**Question (user, 2026-09-18).** The target is roughly **60 recorded slots at up to 64 bars**. After the memory
fixes, arming a track still exits arm on a loaded workspace. Is the arm failure the problem, or is something
structural?

**Diagnosis.** The arm failure is a symptom. The scaling law is wrong: runtime working sets and whole-loop
derived state are owned **per recorded slot, for the slot's lifetime**. Measured: 64 slots exist (8 tracks x 8);
one admitted slot's tagged runtime total is **~1.47 MB** and its arm prewarm chain **~845 KB**; whole-loop derived
state is **~325 KB per loop** (83 snapshot slabs, ~2.6 MB, for a loaded workspace); the resident event store holds
**~28** sixty-four-bar loops at the observed density (131,072-event ceiling / ~4,600 events). Against an **~8 MB**
pool that supports on the order of **four** resident bundles — and the arm chain fails at
`#MEM_EXT_REFUSED,bytes=65536,tag=12` (`TagCompanionReconstruction`) once earlier arms have accumulated.

**Decision.**

1. **Working sets are role-owned, not slot-owned.** Derived runtime views, capture companion storage and bounded
   canonical working content belong to an **active runtime role**. Inactive slots retain descriptors, bounded
   metadata and a capped envelope only. Working sets are **window-sized** so a slot's resident cost does not
   depend on its loop length, leased on activation/admission under identity `owner + generation + role`, reused
   across slots, and released when no live owner remains. **Capacity is derived from measurement, never
   asserted.**
2. **Role classes and cardinalities.** `PlaybackWindowLease` **N, derived** (8 today: 8 tracks x 1 sounding slot;
   must not preclude 16/32/48/64), `LiveCaptureLease` 1, `DeferredFinalizationLease` **1 admission with a
   serialized queue** (`CaptureStopJobs::kCapacity` = queue depth; a full queue is the denial case),
   `InteractiveLease` 1, and an explicitly budgeted transfer overlap. The budget is
   `fixed + N x P + C + D + I + overlap + reserve <= budget`, so only the term multiplied by N must be small.
3. **Immutable readiness is owed per affected tick.** After a recording, overdub or edit changes content at a
   tick, an updated **immutable** state must be ready by the time the playhead moves over that tick again,
   because playback may only consume immutable events. A **missed deadline keeps the previous version
   authoritative for that pass** — never torn, never held silent, never deferred in a way that disturbs timing.
   This changes the publication **unit and deadline**, and it **reuses** the resumable deferred publication
   machinery already built for the purpose rather than adding a second incremental-publication system.
4. **A published root is the checkpoint authority, not the playback authority.** Undo and save require an
   immutable checkpoint; playback resolves windows and must not require a resident whole-loop root.
5. **Durability gates eviction.** Content is evictable only after persistence durably acknowledges **that exact
   canonical generation**. Dirty content pins residency; persistence failure or delay reduces capacity and never
   discards accepted input.
6. **The looper never stops playing.** Switching track/slot, record/overdub and undo/redo happen while playing,
   with the inbound role provisioned before the outbound is released and no allocation at the switch.
7. **Vocabulary is part of the decision.** The whole setup is **Playhead-scheduled MVCC** — multi-version
   immutable state, resolved by interval projection, materialized incrementally against a playhead deadline,
   with epoch-leased working sets — and its stage shape is a **Pipeline of role-owned stages** with declared cost
   and deadline ([NAMING.md](Authority/NAMING.md)).
8. **Thirty invariants are binding** on the owning change (scaling, ownership, immutability, continuity, failure,
   evidence, method), each with its enforcement point; a task that violates one is not complete and an invariant
   with no enforcement point is recorded as a gap.

**Consequences.**

- `ARCHITECTURE_RULES.md` § Ownership rules states the law and its two corollaries: a capacity claim that scales
  with recorded-slot count is a **defect**, and a second authority for "which slot is active" is a **defect**
  (`activeLoopIndex` owns it).
- `Architecture/IntervalProjection.md` carries the readiness contract and the **window-size tunable** (bar-based
  today; 1/16 beat = 12 ticks, 1/32 beat = 6 ticks at 192 PPQN, i.e. 31 ms and 16 ms at 120 BPM), with the three
  coupled effects: resident bytes, achievable switch granularity, publication load.
- New change `openspec/changes/runtime-working-set-ownership/` (stages S0–S7) with a
  [`FALSIFICATION.md`](../openspec/changes/runtime-working-set-ownership/FALSIFICATION.md) review: the envelope
  is **N derived** (not 2 per track), the unbudgeted per-sounding-slot root term is removed rather than budgeted,
  finalization is a queue, and 8 tracks today with bank switching to 16 MIDI channels as the target.
- Instrumentation change `openspec/changes/capture-tier-a-budget-profiles/` keeps a 15–20 minute session inside
  the 1 MiB capture ring; the measurements above depend on it.
- The arm-exit fix (`immutable-loop-snapshot-passes` §5.13) stays as a safeguard only.

**Naming of the owning change.** It keeps the id `runtime-working-set-ownership` (decided 2026-09-18) even
though it also carries readiness, standby, visual and canonical-residency stages: the id names the stable
**thesis** that justifies the change, while **Playhead-scheduled MVCC** names the architecture the stages build.
Its `proposal.md` § Name and scope states this, so the id is not re-derived when a stage is added.

**Not decided here.** The 16-track / bank-switching scope; the transfer-overlap depth; the **cold undo policy**
(required before any flat-cold-memory claim); the canonical residency policy; the default window size; whether a
second finalization lease is ever warranted (evidence path: a `finalization_lease_stop_denied` counter); and
which existing owners are retrofitted to the uniform work-item contract (opportunistic, its own change —
a big-bang retrofit of nine timing-path owners is explicitly rejected).

---


## DEC-075 — The snapshot publication boundary admits memory per construction turn and parks on allocation failure

**Date:** 2026-09-18
**Status:** Accepted — implemented; native and firmware green; **device-verified for the parked path**. Full native
**2249 / 1 skipped / 0 failures** (two new fixtures, below); `teensy41-capture-serial` SUCCESS with the RAM1 line
byte-identical to DEC-074 (`variables 91,968 / code 425,560 / padding 424 / locals free 6,336`), i.e. no FlexRAM bank
cost. Device (`captures/rcpub-gate-05`, git `dc9a1107` clean, hex `7f6bd8e3…`): **0 `FATAL`, 0 `#MEM_EXT_REFUSED`**,
device alive, and **49 `#MEM_DENIED` all `op=snapshot_reserve`, `reason=heap_low`, `need=40960B`, `heap_free=68KB`**
with `snapshot_unit`/`snapshot_fragment`/`snapshot_slab_fail`/`snapshot_alloc` all **0** — the boundary parked with
nothing written and held `heap_free` at 68 KB min=median=max, `snap` unchanged, pool `live` flat, where the crash log
went heap 108 -> 0 KB and `abort()`. The denial report rate-bound is confirmed on device (~1/s over 50 s). Remaining:
the wrap case specifically and the resume-on-recovery path (both see "Owed" in the OpenSpec task).
**Owner:** `Loop::stepPendingSnapshotPublish` + the cold helper `admitSnapshotPublicationTurn`
(`src/Loop/LoopSnapshotPublish.cpp`), `PendingSnapshotPublishBox::fragmentBlockTransient` /
`fragmentParkSawDenial` and `noteFragmentIncorporationFailure` (`src/Loop/LoopSnapshotBoxes.h`,
`src/Loop/LoopSnapshotPublish.cpp`), and the repeat-denial report bound in
`MemoryMonitor::admitOperationMemory` (`src/Utils/MemoryMonitor.cpp`).
**Follows:** DEC-065 (the same admission pattern at the record-arm and overdub-start boundaries), DEC-047 (the wrap
publication is bounded per fragment), DEC-073 (coverage-based "current" is what makes a *parked* root a defined
degraded state -- the previous root stays selected, `publishedSnapshotIsCurrent()` stays false, and the save falls to
the passes-authoritative body instead of serialising a wrong root).

**Report (user, 2026-09-18).** The device reset a few bars past the overdub wrap while overdubbing an existing
overdub. `captures/session_20260918_101615`: **not a fault** -- no crash record (`boot_reset,crash=0` on the next
boot, and `abort()` raises no `CrashReport`); the reset follows
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and
`#CAPTURE_RECONNECT`. `Overdub wrap committed @ tick 18432` at 114.519 s, and then for six seconds the pool's `<=32K`
class gained **one 32768-byte snapshot slab per second** (96 -> 102 blocks) while the internal heap fell
**108 -> 72 -> 36 -> 0 KB**; the pool then refused `bytes=32768` (119.465 s, exactly `snapshotSlabOpen`'s
`extmem_malloc` request) and `bytes=8224`, and `abort()` fired.

**Diagnosis.** The wrap publication costs ~8 slabs (~256 KB) plus an internal-heap working set *by design*: the
DEC-073 RC1 control (`captures/rc1-coalescing-device`, wrap at 61.070 s) shows the identical post-wrap burst, then
settles, 0 `FATAL`, and completes its LSV1 `loop_snapshot_tail`. The crashing session reached that window with no
headroom (pool live 8201-8203 KB against a reported `pool=8182 KB`, from a 12-bar record on top of the loaded
workspace root), because this boundary had neither of the two things every other content-sized boundary has: a
memory admission, and a park for an allocation failure.

**Decision.**

1. **Admit every content-sized publication turn.** `OperationKind::SnapshotReserve` existed with an 8 KB margin and
   was **dead code** (zero call sites; that boot logged exactly one `#MEM_ADMIT` -- the boot probe -- and zero
   `#MEM_DENIED`). It is now called at the top of the construction loop in `stepPendingSnapshotPublish`, with
   `kSnapshotPublicationReserveBytes = kSnapshotSlabBytes + 8 KB`, which the gate combines with its 32 KB heap floor
   and the pool-refusal recency window. The freeze follows an admitted turn; publishing an already-frozen candidate
   is deliberately **not** gated because it allocates no new content memory. A denial breaks out with nothing
   written, so the publication stays pending and the previous root stays selected -- the state DEC-074's
   `hasSettledPublishedSnapshotForSave` already handles.
2. **Park on allocation failure (transient), release only after observed pressure.**
   `noteFragmentIncorporationFailure` parked only `MembershipCapacity`; `Allocation` retried the exact fragment
   forever, copying the fragment's `LoopSnapshotChangeSet` (a default-allocator vector, i.e. internal heap) into a
   fresh `LoopSnapshotConstruction` and re-attempting the slab every turn -- which is what drained the heap. A freeze
   failure was not counted at all. `Allocation` now parks after `kSnapshotFragmentMaxConsecutiveFailures`, and the
   freeze branch counts through the same path. The park is **transient** and is released only once admission has
   denied at least once and then admits again, so the same admission state that let the fragment fail cannot
   immediately re-arm it. `Injected` (test) and representability failures keep their existing semantics.
3. **Bound the denial diagnostic, not the decision.** This is the gate's only per-turn caller and it can stay parked
   for as long as the pool is full, so a repeat of the same denial shape is reported at most once per second, exactly
   as `recordPoolRefusal` already bounds its own storm. The decision stays exact on every call.

**RC-PUB-3 correction (recorded, not implemented as originally worded).** The RC as first recorded said to remove
`SnapshotSlabAllocator::allocate`'s `abort()` and route it to the non-fatal path. That is **not expressible**: the
device builds `-fno-exceptions`, so `std::allocate_shared` has no failure channel, and a custom `shared_ptr` deleter
would move every control block onto the internal heap (~450 KB at the observed 14k-node peaks). It is also
**unreachable**: every snapshot allocation goes through `allocateSnapshotShared`, which pre-reserves
`128 + align(sizeof(Type))` before `std::allocate_shared` asks for one object including its control block, so
`Allocation` failures surface as `snapshotSlabOpen` failing *before* the allocator, which is already non-fatal
(`allocateSnapshotShared` returns an empty ref). The abort is therefore kept as the invariant check, tagged
distinctly (`site=slab_allocate` vs the reachable pressure site `site=slab_open`), and documented in place.

**Scope boundary.** The allocator that actually aborted is the shared `ExternalMemoryFirstAllocator`, whose
`abort()`-instead-of-deny semantics are the wider defect already recorded in `docs/Runtime/CURRENT_WORK.md`
("`abort()` turns a transient allocation failure into an unrecoverable silent hang"). This entry stops the
publication from *driving* it there; it does not change that shared allocator, which other owners still reach.

**Evidence.** `test_publication_turn_waits_for_admitted_headroom` pins the admission: under a forced denial a
step returns pending with the wrap journal, `publishedSnapshot()` pointer and `publishedSnapshotRevision()` all
unchanged, no construction in flight and no failure recorded, and the same publication then completes unchanged.
`test_allocation_failure_parks_publication_until_admission_recovers` pins the park: two injected `Allocation`
failures park, a further admitted turn makes no attempt (the failure count cannot advance), a denial then records the
pressure, and the next admission releases the park so the publication completes and the root becomes current.
Both are in `test/test_loop_take_survival`. **Device run 2026-09-18 (`captures/rcpub-gate-05`)** took the parked
branch: 0 `FATAL` / 0 `#MEM_EXT_REFUSED`, device alive, 49 `op=snapshot_reserve` denials at `heap_free=68KB`, and
`snapshot_unit` 0 -- nothing started, stepped, frozen or published while denied -- with `heap_free` flat at 68 KB and
`snap` unchanged. **Owed:** the wrap case specifically (that run's denials landed during `RECORDING` and the harness
aborted before the wrap on a 1 MiB `#CAP` ring shed), and the resume-and-complete path once the heap recovers (the run
ended while still denied; resume is native-proven only). The 24-bar repro is not reachable with `--record-bars`
`[1, 2, 4, 8]`, so a faithful re-run needs that cap raised. `kSnapshotPublicationReserveBytes` remains the tunable if
a healthy device parks: the margin requires 72 KB of internal heap, the DEC-073 RC1 control settles at 104 KB, and the
device run above was denied at 68 KB -- the intended discriminator.

## DEC-074 — The LSV1 body admits only a settled root; the format is pinned once and the fallback is the passes-authoritative legacy body

**Date:** 2026-09-18
**Status:** Accepted — implemented and **device-verified**. Native reproducer
`test_write_loop_persisted_requires_a_settled_published_snapshot` fails on the pre-RC2 predicate and passes after;
`test_storage_loop_io` 47/47; full native 2248 / 1 skipped / 0 failures; firmware builds with the RAM1 local
allowance restored to RC1's value. Device (`captures/rc2-settle-device-c` + `captures/rc2-settle-reboot-playback`,
firmware `86fd8f07…`): the save took the settled LSV1 path (`tail_gather_begin`, `LoopSnapshotTail`, `q=0`
publications), 0 `RING,overflow`, 0 `FATAL`, post-reboot `storage_matches_effective: true`. That session was
accidentally recorded at **240 BPM instead of 120**, so its 17-bar `record_loop_length_mismatch` and the verify's 4
unexplained wrap rows are a setup artifact, not a content finding; the intended DEC-071 shape's post-reboot parity is
proven clean under DEC-073 (`captures/rc1-coalescing-reboot-playback`, 5/5 `inventory_equal`, 0 missing), and this
entry changes only the admission, not the published content.
**Owner:** `Loop::hasSettledPublishedSnapshotForSave` (`src/Loop/LoopSnapshotLifetime.cpp`),
`loopUsesLoopSnapshotBodyFormatV1ForSave` / `decideLoopPersistBodyFormat` (`src/StorageLoopIo.cpp`,
`include/StorageLoopIo.h`), `stepDeferredLoopPersist` / `deferUnsettledLoopPersist` and
`CurrentWorkspaceSaveJob::loopWriteUsesLsv1` / `loopSettleDeferrals` (`src/StorageManager/WorkspaceSave.cpp`,
`include/StorageSession.h`).
**Follows:** DEC-073 RC1. That entry's `publishedSnapshotIsCurrent()` is coverage-based, which makes the *live*
publisher truthful; this entry makes the *persistence* admission require a settled representation and removes the
mid-write format flip.

**Report.** DEC-073 left two persistence holes. (1) `stepDeferredLoopPersist` decided the body format **twice** — at
`Header` and again at `EditTail` — so a committed-state change between the stages (a pass commit, or an undo/redo
restore while the transport is idle) could flip LSV1 -> passes-authoritative *after* the LSV1 preamble was already on
disk, leaving a body with an LSV1 preamble and no `LoopSnapshot` tail. (2) The admission did not require the
publisher to be settled, so a coverage-current root with a publication still in flight was embedded without evidence
that the representation was final.

**Format decision (the one DEC-073 asked for).** LSV1 is **root-authoritative by contract**: both readers reject a
body without the `LoopSnapshot` section -- `stepLoopSnapshotBodyFormatV1LoadParseImpl` fails when
`!state.sawLoopSnapshot` (and when the `PersistedLoopSnapshot` oracle is missing while its header flag is set), and
`test_lsv1_rejects_missing_loop_snapshot_section` pins the buffer reader's identical rejection. LSV1 therefore
**cannot** express a "body without a selected snapshot/root". The passes-authoritative representation is the
**legacy** body: it streams the passes oracle and the edit tail, and `applyLoadedLoopSlotBodyCommit` runs
`TrackUndo::rebuildSlotFromLoopContent` on load, so the loop is rebuilt from its passes with no embedded root.
Adopting a rootless LSV1 body would mean changing the loader's contract (a format/semantics change) for no functional
gain over the rebuild the legacy body already performs. **Chosen: legacy is the passes-authoritative body.**

**Decision.**

1. `Loop::hasSettledPublishedSnapshotForSave()` = `publishedSnapshotIsCurrent() && !hasPendingSnapshotPublish()`:
   the root must be coverage-current **and** the publisher must have nothing in flight.
   `loopUsesLoopSnapshotBodyFormatV1ForSave()` (the save admission predicate, used by both the immediate and the
   deferred writer) is exactly this.
2. `decideLoopPersistBodyFormat(loop, settleBudgetRemaining)` returns one of three outcomes:
   - `Lsv1SelectedRoot` when the root is settled;
   - `AwaitSettled` when the root is coverage-current but a publication is pending **and** the caller still has
     settle budget -- the pending work is a rebuild of the same committed state, so it is transient;
   - `PassesAuthoritative` otherwise. A stale/uncovered root is never transient and never waits.
3. The deferred loop write resolves the decision **once**, at `Header`, before a single byte is written, and pins it
   in `loopWriteUsesLsv1`. `EditTail` and `LoopSnapshotTail` read the pin and never re-evaluate, so a preamble can
   never be left without its tail. The pin and the settle counter reset with `resetDeferredLoopWriteState()`.
4. **Bounded re-admission.** `kLoopPersistSettleDeferralBound = 128` writer turns per loop write. A deferral writes
   nothing and leaves the stage at `Header`, so the work item stays active and retries on a later persistence slice
   (the three `stepDeferredLoopPersist` callers already treat `!loopDone` as "keep the item"). After the bound the
   decision becomes `PassesAuthoritative`, so a blocked publisher cannot wedge the save.

**RAM1 note (measured).** The first implementation emitted a new `await_settled` persistence telemetry tag and passed
the deferral back through an extra `bool*` parameter; the build then reported `free for local variables: 2240`
against RC1's `6336` (RAM1 variables 91,968 -> 96,064). A symbol-size diff found no meaningful growth -- the +4 KB
was DTCM `.data` alignment: the new string literal and the modified switch tables moved a following `ALIGN(4096)`
boundary (the exact failure described in
[`Teensy-RAM1-Bank-Overflow.mdc`](../../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc)). Dropping the new tag and
the extra parameter, and moving the resolution out of the stage switch, restored
`variables: 91968 / free for local variables: 6336` exactly. The deferral is deliberately **silent** for this
reason; it stays bounded and transient, and the policy is covered natively.

**Observability (acceptance env).** `[env:teensy41-capture-serial-no-grain]` also sets
`-D SESSION_CAPTURE_LCR=0`, gating the `DIAG,lcr,*` family (`SESSION_CAPTURE_LCR`, default 1) at its
lcr-only emitter blocks. On the 2026-09-18 device state (a loaded 20-bar loop) that family logged 12,733
`DIAG,lcr,srcspan` rows of 13,173 `#CAP` lines and overflowed the 1 MiB ring in two attempts; with it gated the
same recipe ran `overflow_free=True` (`captures/rc2-settle-device-c`, firmware `86fd8f07…`).

**Verification.** `test_write_loop_persisted_requires_a_settled_published_snapshot` asserts the pre-RC2 predicate is
still true at that point (`snapshot && publishedSnapshotIsCurrent()`) and that the new admission is false, then that
`writeLoopPersisted` emits a **legacy** body while unsettled and LSV1 once the republish settles.
`test_loop_persist_body_decision_awaits_settlement_within_its_budget` pins `AwaitSettled` / `PassesAuthoritative` /
`Lsv1SelectedRoot` for the budget, stale-root and settled cases. `test_storage_loop_io` 47/47; full native
**2248 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 variables 91,968 / code 425,560 /
padding 424 / **locals free 6,336** (FLASH code 1,007,072) -- exactly the RC1 figures. Device: the save took the
settled LSV1 path and reloaded with `storage_matches_effective: true`; see Status for why that run's 240 BPM
mis-setup is not a content finding.

## DEC-073 — A published snapshot root carries the committed-pass coverage it was built from; "current" is coverage, not a revision stamp

**Date:** 2026-09-18
**Status:** Accepted — RC1 (publisher correctness) implemented and **device-verified**
(`captures/rc1-coalescing-reboot-playback`: fresh boot `boot_reset,crash=0` at 3.0 s uptime, then
`result: PASS`, `storage_matches_effective: true`, `384 + 154 + 38 + 24 = 600` effective rows and **all five
playback cycles `inventory_equal: true`, `exact_order: true`, 0 missing, 0 extra** — including the 24-row second
overdub pass that was the reported loss). RC2 (the LSV1 persistence admission gate) is a separate follow-up and is
**not** part of this entry's code scope.
**Owner:** `Loop::storePublishedSnapshot` / `PublishedSnapshotBox` (`src/Loop/LoopSnapshotLifetime.cpp`,
`src/Loop/LoopSnapshotBoxes.h`), `Loop::stepPendingSnapshotPublish` and the pending box
(`src/Loop/LoopSnapshotPublish.cpp`), `Loop::publishedSnapshotIsCurrent`. The requesters that must supply coverage:
`Loop::queueSessionExitSnapshot`, `Loop::requestCommittedPassesRepublish`, `Loop::queueOverdubWrapSnapshotJournal`
(which already passes an explicit `targetRevision`), and the undo/redo captured-root selection
(`TrackUndo::applyPublishedSnapshotSelect` -> `TrackUndo::selectPublishedSnapshotForEntry`).
**Supersedes:** the implicit contract that a root is current when `PublishedSnapshotBox::revision == playbackRevision`
(introduced with DEC-047/DEC-049 and relied on by the LSV1 body save since DEC-057). That equality stays necessary but
is no longer sufficient.

**Report (user, 2026-09-18).** After reboot the second overdub was not loaded: the committed passes are complete
(`!CAPTURE_CONTENT` effective 142 rows, both overdub passes present) but the deferred save's LSV1 tail gathered a
coalesced published root that encodes the **undo** state. `captures/gus-sanity-01` + `captures/reboot-01-playback`:
`PassesResolve` 55 -> **48 after `Undo`** (149.33 s) -> **57 after `Redo`** (152.27 s); the save request was pending
from 116.97 s, its tail gather fires at 154.269 s, and post-reboot the loaded body root resolves to **48** display
rows vs 57 before the reboot with the 24-39 overdub range silent. The same class of loss is recorded for
`content-verify-58`/`-59`/`-59b` and `content-verify-60`.

**Mechanism ("current can lie").** Two independent holes in the same contract:

- `Loop::stepPendingSnapshotPublish` publishes the session-exit candidate with
  `storePublishedSnapshot(box.unpublishedCandidate)` and the default revision, which
  `Loop::storePublishedSnapshot` fills with the **current** `playbackRevision` no matter which committed-pass state
  the candidate was actually built from. Only the wrap branch passes an explicit revision
  (`box.queuedWrapJournals.front().targetRevision`, DEC-047/DEC-051). So a candidate enumerated before the redo is
  stamped current after it: the machinery cannot distinguish "this revision was reached" from "this candidate covers
  that state". `Loop::publishedSnapshotIsCurrent()` is exactly `box.revision == playbackRevision`.
- The undo/redo captured-root selection has the same shape: `TrackUndo::selectPublishedSnapshotForEntry` calls
  `loop.storePublishedSnapshot(ref)` with the default revision, so a root captured at an older committed-pass state
  is re-stamped with the current one. With the redo's `afterPublishedSnapshot` empty (the Pin A hole that DEC-071 /
  `immutable-loop-snapshot-passes` 5.8 closed for the live path), the pre-undo root stayed selected and was stamped
  current.

In the same window the queue accounting (`pendingSnapshotQueuedPassCount` `q` / `pendingSnapshotAppliedPassCount`
`a`) advanced `q=4 -> 0`, `a=0 -> 5` while the published candidate still resolved to the **undo-shaped 48-row** set
against `PassesResolve` 57: **queue consumption and candidate coverage were not tied together**.

**Decision.** Coverage is data carried with the root, not an inference from a revision that was reached.

1. `Loop` keeps a **committed-pass revision** (`committedPassRevision`) that advances exactly when the publisher's
   enumerable pass/companion inventory changes -- a record/overdub/wrap pass commit, a pass disable or reclaim, an
   undo/redo pass restore, a note-edit row save/delete, a pending-change seal, or a clear. Audited 2026-09-18: every
   site that bumps `playbackRevision` is one of those inventory changes, so `committedPassRevision()` is an alias for
   `playbackRevision` today; it is named as its own contract because the coverage comparison is about the enumerable
   pass inventory, and a future content-only revision bump must be excluded from it deliberately rather than by
   accident.
2. A candidate is published with `coveredPassRevision`: the committed-pass revision its content was **enumerated and
   applied from**. `PendingSnapshotPublishBox` carries the watermark; it advances only when a request actually
   enumerates passes into the box (an enumerated pass is always applied before the publish, because the publish
   requires the queue to drain), so the watermark at publish time describes exactly the content the candidate
   incorporated. A wrap candidate keeps its explicit `targetRevision` as both stamp and coverage.
3. A published root carries that `coveredPassRevision`. **`publishedSnapshotIsCurrent()` means exactly
   `root.coveredPassRevision == loop.committedPassRevision`.** No pending-request clause is needed at this level: a
   later request cannot invalidate the root without moving the committed-pass inventory, which advances the revision
   the comparison reads. A selected captured root is current only for the committed-pass state it was captured at,
   never for one it was not built from; a captured ref supplied without coverage is conservatively not current. The
   persistence gate (RC2) adds the separate "publisher is settled" requirement (`!hasPendingSnapshotPublish()`) at
   its own admission point, because a save must not race an in-flight publication even when the live root is
   coverage-current.
4. **Coalescing rule.** When several requests are coalesced into one box, the candidate ultimately published must
   represent the latest committed-pass state covered by **all** requests consumed by that publication. A candidate
   derived from an earlier request may not become current merely because later requests were coalesced into its queue
   entry. If a started candidate cannot safely retarget, the valid policy is to finish/publish it only for its own
   covered revision, leave the newer request pending, and then construct and publish the latest state: a temporary
   intermediate root is allowed, silently declaring the newer request satisfied is not.

**RC1 -- publisher correctness (this entry's code scope).** Add the coverage field and the committed-pass revision,
stamp the session-exit and republish candidates at enumeration, propagate coverage through `finishPendingConstruction`
and every `storePublishedSnapshot` call, and make the currency predicate coverage-based. Reproduce
`OD2 -> Undo -> Redo -> coalesced snapshot requests` and prove `PassesResolve == published root` **before any
persistence runs**. Cover both a later request arriving before construction begins and one arriving while an earlier
candidate is mid-construction (the second is the reported case).

**RC2 -- persistence guard.** Landed as its own decision, DEC-074: the LSV1 body admission now requires a settled
root, the per-loop format is resolved once and pinned, and the passes-authoritative fallback is the legacy body
(LSV1 is root-authoritative by its reader contract, so it cannot express a rootless body).

**Rejected alternatives.**

- *Load-time reconcile in `applyLoadedLoopSlotBodyCommit`* (owner call 2026-09-18): treats the symptom at the wrong
  layer -- the bad root exists before the save, which only serializes it faithfully.
- *Persist from `passes` first*: makes the persisted representation diverge from what playback consumes, so a
  publisher defect becomes invisible instead of impossible.
- *Keeping "revision reached" as proof*: measured to lie; the revision is a stamp, not evidence about a candidate's
  content.
- *Publishing only the newest state and never an intermediate root*: not required, and it would either block the
  older candidate past its own coverage or drop its request; the intermediate root is allowed and must simply not
  claim the newer coverage.

**Consequences.** The LSV1 save admission, the playback stale-root overlay, the undo after-attach and the display
cache revision keys keep reading `publishedSnapshotIsCurrent()`; the predicate becomes strictly stronger, so a caller
that relied on the weaker form may now see "not current" and must take its pending/rebuild path. `playbackRevision`
semantics are unchanged; the two revisions are compared only inside the publication contract.

**Implemented (2026-09-18, RC1).** `PublishedSnapshotBox` and `PendingSnapshotPublishBox` carry
`coveredPassRevision` (`src/Loop/LoopSnapshotBoxes.h`); `Loop::storePublishedSnapshot(snapshot, revision,
coveredPassRevision)` never infers coverage from the revision stamp and resolves `kSnapshotCoverageCurrent` to the
live committed-pass revision; `Loop::publishedSnapshotIsCurrent()` compares coverage only; new accessors
`publishedSnapshotCoveredRevision()` / `committedPassRevision()`. Watermarks are set where the box actually
enumerates (`queueSessionExitSnapshot`, `queueAllCommittedPassesInto` from both `queueLoadedSnapshotPublish` and
`requestCommittedPassesRepublish`, `enqueueLiveOverdubPasses` when it queues a pass, wrap journals via their own
`targetRevision` at publish), and the session-exit publish now passes `box.coveredPassRevision`. Undo/redo coverage
travels with the retained root: `UndoEntry::before/afterPublishedCoverage` (`include/GlobalUndoStack.h`), captured at
every `capturePublishedSnapshot` site, attached in `TrackUndo::attachAfterPublishedSnapshot`, plumbed through
`TrackUndo::selectPublishedSnapshotForEntry(loop, ref, coverage)`, and carried by the job-owned and live
session-before roots (`Track::CaptureStopJob::beforePublishedCoverage`,
`Track::overdubSessionBeforePublishedCoverage_`, `TrackPreparedUndo::prepareOverdubSessionEntry`). Synchronous
publication (note-edit close, loaded hydration) stamps `kSnapshotCoverageCurrent`; the native test hook defaults to
the same but treats an explicit older revision stamp as uncovered.

**Verification.** Native reproducers in `test/test_loop_take_survival`:
`test_coalesced_snapshot_request_before_construction_is_not_satisfied_early` and
`test_coalesced_snapshot_request_mid_construction_is_not_satisfied_early` (the reported timing). Both were red on the
pre-fix tree on the same clause -- the selected root claimed current although its content predated the committed
overdub pass -- and are green after the fix; `test_loop_take_survival` is 137/137, full native 2246 / 1 skipped /
0 failures. Device (firmware `teensy41-capture-serial-no-grain`, hex
`f2f1d8bdfc85b30727fc667e1c548066d1debf4854fa5acae8cdae645c735f1e`, working tree at `d9eb419a` + this change),
the DEC-071 multi-pass shape (`--record-bars 8 --overdub-bars 8 --overdub-start-delay-bars 2`, track 4 slot 1) with
the recipe's undo/redo:

- `captures/rc1-coalescing-device` (managed `base`): `PLAYING->OVERDUBBING: 2`, `OVERDUBBING->PLAYING: 2`, undo=1
  redo=1, `stored overdub span count=64/63` + `second overdub count=56/7`, display `overdub_ok/transport_ok=True`,
  `overflow_free=True`. The run's own report is FAIL only on `persistence_stage_missing:result`: the post-redo
  republish (`snapshot_unit,kind=publish,q=0,a=4` at 95.474 s) lands, the last `OverlayRejected`/`PassesResolve` row
  precedes it (root_rev 5 vs playback_rev 6), the LSV1 `tail_gather_begin` at 99.640 s gathers that root, and the
  now-complete 600-row tail was still being written when the base run closed serial -- the device's own log then
  records `PERS,result,...,ok` at 111.14 s. The oracle miss is the run window, not the content.
- `captures/rc1-coalescing-device-playback` (live): `result: PASS`, all five cycles `inventory_equal: true`,
  `exact_order: true`, 0 missing/extra, `storage_matches_effective: true`.
- `captures/rc1-coalescing-reboot-playback` (**fresh boot**, `boot_reset,crash=0` at 3.0 s uptime, load
  `start`/`ok`): `result: PASS`, `storage_matches_effective: true`, `record 384 + overdub 154 + 38 + 24 = 600`
  effective rows, **all five cycles `inventory_equal: true`, `exact_order: true`, 0 missing, 0 extra**,
  `max_bar_refs: 84`, 0 `RING,overflow`. The previously silent 24-row second-overdub pass is audible after reboot.

## DEC-072 — A staged capture pass is owned by its capture interval, not by the Loop producer slot

**Date:** 2026-09-17
**Status:** Accepted — implemented and device-verified (`captures/successor-ownership-06`: the predecessor retired inside the successor's live interval and the successor then staged and transferred its own pass; `Result: PASS`, `overlap=True denied_stop=True`).
**Owner:** `Track::CaptureStopJob::pendingPass` / `ownsPendingPass` (the receipt), `Loop::takePendingCapturePass` / `transferCapturePass` (the move and the terminal transfer), `Loop::beginPreparedCompanionHistory` (the bound verification source), `Track::ownedPendingCapturePass` (the deferred reader)
**Supersedes:** the implicit "one staged pass lives in `Loop::pendingCapturePass_` for whoever staged it last" ownership recorded by DEC-067/DEC-068 prose. This entry is the structured record of OpenSpec task 5.12, design decision D8 in [`successor-capture-reentry-unguarding/design.md`](changes/successor-capture-reentry-unguarding/design.md).

**Report.** Same wedge family as DEC-070, different trigger: a predecessor's deferred stop was
admitted and had already staged its capture pass at `PrepareTransfer` when a successor interval
started. `Loop::beginCapture` discards the pending pass unconditionally, so the successor's
`Track::startOverdubbing` destroyed the predecessor's receipt; the predecessor's
`beginPreparedCompanionHistory` was then refused forever by its own `!hasPendingCapturePass_`
precondition, its companion work never left `Prewarmed`, and its job held `captureStopOwner` and the
frozen role for the rest of the session — the device's
`Overdub stop refused: stop_job_pending phase=7 dest=1`. The native red reproducer is
`test_adversarial_reentry_at_a_companion_checkpoint` (paused at `CompanionHistory`), gated behind
`REENTRY_RED=1`.

**Why the obvious fix was rejected.** Guarding the discard (`beginCapture` clears the loop's slot only
when the staged pass retires the live capture) does make that checkpoint retire, because the shared
scalar keeps the predecessor's receipt. But `pendingCapturePass_` is then still one object serving two
intervals: the successor's own staging at its `PrepareTransfer` returns `AlreadyPending`, so the
`AtomicTransfer` checkpoint goes red. "Protect the shared pass" only moves the collision in time.

**Decision.** The staged pass is a `PrepareTransfer`-time object owned by the interval that produced
it:

```text
A live                         -> A owns its pass
A STOP admitted                -> job A owns its frozen/raw/canonical inputs; no pass yet
B may start                    -> the Loop pending slot belongs to the live producer only
A reaches PrepareTransfer      -> stage into the Loop slot, then immediately take it into job A
B start / wrap / staging       -> may use the Loop pending slot independently
A AtomicTransfer               -> consumes job A's pass directly; never restages
```

Invariant: **at most one owner holds a given staged pass, and multiple intervals may each hold their
own staged pass concurrently.** Corollaries, all enforced in the implementation:

- The Loop pending slot is producer-local scratch. A deferred job may use it only as a transient
  construction point inside its own `PrepareTransfer` turn (stage then take, no yield between), so a
  job never depends on the slot's contents after stop admission and `beginCapture` may keep clearing
  it unconditionally.
- The terminal transfer reads the job-owned pass and has **no fallback** to `Loop::pendingCapturePass_`;
  a fallback would silently read a successor's receipt. `restagePendingCapturePass`-style helpers are
  not on the normal deferred path.
- Deferred readers resolve the receipt through the job: undo identity (`canPublish`, the collected
  final pass id), the companion-history verification source, `requiresBridgeRefresh`, the stop
  telemetry/accounting counts, and the transfer itself.
- A stop-admission placeholder was rejected: it would imply ownership of an object that does not exist
  yet and re-introduce a shared-slot step. The staged-pass lifecycle deliberately does not mirror
  `pendingNotes` / `preview` / `beforePublished`, which do exist at admission.

**Implemented (2026-09-17).** `CaptureStopJob::pendingPass` + `ownsPendingPass`; `Loop::takePendingCapturePass`
(move by chunk-list swap, non-allocating) called in the same grain as the staging;
`Loop::transferCapturePass` / `canTransferCapturePassWithoutAllocation` /
`commitPreparedCapturePassAtomic(transferred, pass)` for a specified receipt;
`beginPreparedCompanionHistory` binds `PreparedCompanionHistory::capturePass` by address so a
successor's capture start can neither destroy nor rebind the predecessor's verification source;
every deferred reader above takes the job pass explicitly; the job releases and clears its receipt at
retirement. The implicit `Track::CaptureStopJobs::CaptureStopJobs()` became non-trivial, so it is now
defined out-of-line and FLASH-resident — as an implicit aggregate constructor it landed in ITCM and
pushed RAM1 across a 32 KB FlexRAM bank (`locals 6,400 -> -26,368`); the build is back to
`RAM1 variables 91,904 / code 425,816 / padding 168 / locals 6,400` and `FLASH code 1,003,112`.

**Native evidence.** `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture` green (the
former red reproducer); `REENTRY_RED=1` full `test_continuous_capture` 57/57; new
`test_simultaneous_interval_owned_staged_passes` asserts the invariant directly (job A owns pass A with
the Loop slot empty, B stages its own distinct pass with different identity/chunk storage/session
index, A's transfer appends exactly one canonical pass by pointer-move with the canonical row vector
unchanged in capacity and address, B's receipt is untouched, and B then consumes B exactly once).

**Device evidence (`captures/successor-ownership-06`, managed HITL, `base`, 8-bar record / 2-bar
overdub, temporary `d8-ownership` stimulus variant).** 50.903 s A's stop admitted (`Reentry window
open`, job phase 9); 51.392 s B starts, 1,122 ms before A's release (`overlap=True`); 51.935 s B's quick
stop refused (`Overdub stop refused: stop_job_pending phase=9 dest=0`) with B still recording
(`denied_stop=True`); 52.514 s A `Reentry released` while B is live — A's companion preparation
completed and A's own pass transferred; 52.979 s B's own stop admitted and 52.996 s its job retired
(`successor_retirement_ts`, `retirement_ok=true`), i.e. **B staged and transferred its own pass with
no `AlreadyPending`**. Oracle `Result: PASS` (`overall_ok=true`), `overflow_free=true`, display
`overdub_ok/transport_ok` true, 0 `FATAL` / `#MEM_DENIED` / `#MEM_EXT_REFUSED`, no
`stop_job_pending phase=7`, no `companion path stalled`. The run's identity is `firmware_hex_sha256=24b86a9736b2a3d882b0c179bd9d8efda2b386a44894178cef202ffd42e1c65b` (`build_env=teensy41-capture-serial`, `preset=base`, temporary stimulus sidecar in the run directory).

**Harness correction in the same change.** `_verify_successor_reentry` accepted only the DEC-067-era
denial wording (`Overdub stop deferred: deferred/frozen admission role is occupied`), so every
current-firmware run reported `successor_reentry_stop_not_denied` while the device log carried the
refusal (runs `successor-ownership-01`/`-03`). It now accepts both wordings
(`REENTRY_DENIAL_MARKERS`), with a regression test
(`test_current_firmware_denial_wording_counts_as_the_denial`); `scripts/test_capture_transitions.py`
42/42.

**Deliberately out of scope.** OpenSpec 5.5 stays open: its parameterized checkpoint coverage and the
promotion of `test_adversarial_reentry_at_a_companion_checkpoint` into the default gate belong to 5.5,
which this decision unblocks.

## DEC-071 — The audible stale-root view must enumerate wrap-journaled passes, not only queued ones

**Date:** 2026-09-17
**Status:** Accepted — implemented; regression fails without it; **device-verified** with the content-verification pipeline on the reported multi-pass shape (`captures/content-verify-52-playback`: 8-bar record + 8-bar cross-wrap overdub, storage 541 == effective 541, and **all five playback cycles `inventory_equal: true`, `exact_order: true`, 0 missing, 0 extra**).
**Owner:** `Loop::tryGatherPublishedSnapshotEventsInWindow` (`src/Loop/LoopSnapshotPlayback.cpp`), the unpublished-pass allow-list

**Report (user, 2026-09-17).** Record 8 bars, then an 8-bar overdub that starts at bar 3. The
overdub crosses loop end once, so the wrap commits **pass 1** (bars 3..8) and the stop commits
**pass 2** (the wrap tail, bars 1..2). Afterwards pass 1 was never audible again -- every cycle
played only the tail -- while pass 1 stayed on the display. The user's words: "I only hear the
first 2 bars which are overdub pass 2".

**Mechanism.** Playback and the display read different sources. The display resolves every Active
overdub pass from `loop.passes` (`Loop::copyEffectiveCommittedEventsInRange` ->
`collectActiveCommittedChunkLists`, and the LCR prepared index). The audible reader never
enumerates passes: it reads the published snapshot root plus, while that root is stale, a curated
overlay assembled by `Loop::tryGatherPublishedSnapshotEventsInWindow`. The overlay's
unpublished-pass allow-list enumerated `overdubSessionPassIds_`, `lastCommittedPassId_`,
`box.queuedPassIds`, `box.appliedPassIds`, `box.constructionPassId` and the session-exit receipts.

A wrapped pass belongs to none of those. `Track::commitOverdubWrap` journals it
(`Loop::queueOverdubWrapSnapshotJournal`), which records it in `box.journaledPassIds` and pushes its
change fragments onto `box.queuedWrapJournals`; `Loop::queueSessionExitSnapshot` then refuses to
queue it again because `passAlreadyTracked` sees `journaledPassIds`. So once the session closed --
which clears `overdubSessionPassIds_` -- pass 1 was in neither the root (the root predates the wrap
and the journal's own publish is deferred behind the session exit) nor the overlay. When it was the
only unpublished candidate the chunk list was empty, the gather returned false, and the merged
view held empty; otherwise the overlay answered with the stop's pass alone. Either way pass 1 was
silent for as long as the root lagged, without limit when the wrap publication parks.

**Fix.** The overlay also enumerates `box.journaledPassIds` and each `queuedWrapJournals` entry's
`wrapPassId`, so every pass the root does not carry is audible whether it is tracked as a queued
pass, a construction, or a wrap journal. `journaledPassIds` alone would leave the fragments of a
journal that has not started construction invisible; the queued-journal scan alone would miss a
journal whose fragments were already consumed. Both are needed and both are deduplicated by
`appendUniquePassId`.

**Evidence.** Regression
`test_stale_root_overlay_keeps_a_journaled_wrap_pass_after_session_close` (`test_loop_take_survival`)
fails without the fix (`Expected 1 Was 0` for the wrapped pass in the window gather) and passes with
it; full native 2,215 cases / 1 skipped / 0 failures at the fix commit, RAM1 locals 6,400. Device
`captures/successor-wrap-46`: `Overdub wrap committed @ tick 6611` at 59.640 s, successor start
62.142 s, the transport-stop retarget at 75.384 s with the successor live, and the retarget's
`companion path stalled; raw pass only` at 76.217 s -- the overlay fix is what keeps the wrap pass
audible across that window; the companion stall itself is DEC-070 task 5.11.

**Multi-pass device verification (2026-09-17, the reported shape).** Content-creation HITL
(`captures/content-verify-52`) with `--record-bars 8 --overdub-bars 8 --overdub-start-delay-bars 2`:
the overdub starts at bar 3 and runs past loop end, so the wrap commits one pass and the stop a
second -- the shape where the pre-wrap pass was inaudible. `dump_and_capture.py` +
`verify_playback_content.py` (`captures/content-verify-52-playback`):

- storage `record 384 + overdub 157 == effective 541` (`storage_matches_effective: true`);
- **every one of five playback cycles reports `inventory_equal: true`, `exact_order: true`,
  `missing: 0`, `extra: 0`**;
- per-bar effective counts carry the overdub across the whole loop, including the pre-wrap bars:
  `{0:38, 1:48, 2:62, 3:72, 4:72, 5:72, 6:72, 7:72, 8:33}`.

So the pre-wrap pass reaches the MIDI output row for row, not just the display. Note the record
sealed at 6,912 ticks (9 bars) against the requested 6,144 -- the known one-to-two-bar stop-time
quantisation slop, unrelated to this decision; the overdub still spanned the wrap.

**Related open finding (this session).** The same class of conflict appears without a transport
stop: pausing a deferred predecessor in `CompanionHistory` and starting a successor leaves
`preparedCompanionHistoryCanStep == false` while the companion work phase cycles
Prewarmed<->Committed (frozen binding and command epoch both still match), so
`beginPreparedCompanionHistory` never re-admits it and the job never retires. Reproducer:
`test_adversarial_reentry_at_a_companion_checkpoint` (OpenSpec 5.5). This is the successor-start
versus companion-preparation conflict, and it is the resource wedge the device reports as
`Overdub stop refused: stop_job_pending phase=7 dest=1`.

## DEC-070 — A transport-stop retarget must re-route a mid-flight deferred stop job, not only re-label its destination

**Date:** 2026-09-17
**Status:** Accepted — implemented and device-verified (`captures/successor-wrap-40`: the retargeted job retired 2.2 s after the transport stop; the slot stays usable across repeated enter/stop cycles).
**Owner:** `Track::requestDeferredOverdubStopToStopped` / `Track::stopOverdubbingToStopped` (the retarget), `Track::processDeferredOverdubStopGrain` (the phase routing), `TrackPreparedStopCommit::stepPreparation` / `beginPreparedCompanionHistory` (the parked phase)

**Report.** After the HITL runs the track never returns to STOPPED: the display shows PLAYING with the cursor stopped, a record/overdub press drops it to STOPPED, and starting playback enters OVERDUBBING that can no longer be exited. Reproduced on the device by pressing record/overdub (`Overdub stop refused: stop_job_pending phase=7 dest=1 closure=0 recycle_owner=0 recycle_started=0 undo_ready=0`), and by the pre-diagnostic build (`Overdub stop deferred: deferred/frozen admission role is occupied` on every press). The slot only recovers with a reboot.

**Mechanism.** A deferred job admitted for the PLAYING destination runs
`PrepareTransfer -> CanonicalTail -> CompanionHistory`. The transport-stop retarget
(`stopOverdubbingToStopped` -> `requestDeferredOverdubStopToStopped`) re-labels that same job's
`destination` to Stopped but leaves it in the PLAYING phase path. The PLAYING companion-history work
is bound to the pre-stop transport command epoch and its frozen-source generation, so after the
transport stop invalidates that epoch every grain denies (`preparedCompanionHistoryCanStep` /
`stepPreparation` -> `Denied`, both silent), the job parks in `CompanionHistory` forever, and it keeps
`captureStopOwner`, the frozen staging role and the session-exit receipt. Every later stop is then
refused -- the wedge the user sees. A job admitted *for* Stopped never enters this path at all (it
goes `PrepareTransfer -> AtomicTransfer`), which is why the defect only appears on the retarget.

Both stall sites used to return without telemetry, so neither the `#CAP` tiers nor the human channel
named anything; the capture tiers were also shed during the runs, which is why this survived several
sessions.

**Implemented (2026-09-17).** `Track::requestDeferredOverdubStopToStopped` now re-routes on the same
edge: when a job retargeted to Stopped has passed the transfer decision point (phase
`CanonicalTail`..`CompanionBridge`) it retires the companion/undo estimates a STOPPED-admitted job
never contracts, drops the half-prepared companion horizon (so the atomic transfer takes the STOPPED
commit path instead of consulting a verification that can no longer hold) and continues at
`AtomicTransfer`, which commits the already-staged prepared pass and retires the job through deferred
cleanup. Native: `test_transport_stop_retarget_retires_a_midflight_playing_job` (fails without the
re-route, `Expected 10 Was 5`); full native 2,213 cases / 1 skipped / 0 failures; RAM1 locals 6,400.
Device: `captures/successor-wrap-40` — `Transport stopped` 74.873 s with the successor's job pending,
`Reentry released` 77.053 s (pre-fix builds parked it and needed a reboot), and repeated
enter/stop cycles afterwards with no refusal.

**Display regression reproduced and attributed (2026-09-17, `captures/successor-wrap-41`).** Run with
the same acceptance recipe plus a play-from-stopped step (`--post-save-play-wait-ms`, temporary
harness variant). The display verifier reports `overdub_ok=False` with
`DISP after transport stop: frame=178 epoch=178 visual=124 buffer=178` -- the committed frame and the
audible buffer keep 178 notes while the resolved visual window keeps 124 -- and the retarget line reads
`Overdub stop retarget: companions=raw_only phase=6`. So the reported case is the sub-case the guard
cannot keep: the job was still at `PrepareTransfer` when the transport stopped, its companion
(note-edit) horizon had not been prepared yet, and the re-route's raw-only commit published the capture
rows without the transforms the display resolves the overdubbed notes from. Audible content is intact
because the raw rows and the bridge window carry it.

**Probe result (2026-09-17, `captures/successor-wrap-43`, read-only).** The retarget logs the companion
predicates before and after the command-epoch retarget: `probe_before=73`, `probe_after=205`
(bit0 prefix Ready, bit1 inputs-generation match, bit2 `canonicalPassInputs.ready()`, bit3 capture
attached, bit4 `frozenCaptureCompanions.idle()`, bit5 `frozenCompanionPasses.idle()`, bit6 companion
apply available, bit7 frozen source current for the post-stop epoch, bit8 `canonicalTailPhase == Idle`,
bit9 companions verified). The epoch retarget works (bit7 and bit2 flip to set). What blocks the
companion path is bits 4/5/8: the slot's canonical tail phase and companion workers are **not idle** --
they are the *retained* tail cache the predecessor's cleanup keeps across punch-out
(`retainCommittedCanonicalTail` leaves `canonicalTailPhase == Complete` with the companion workers
owned). So the conflict is cache capacity, not a stale epoch: a retargeted successor cannot begin its
own companion preparation while the retained cache owns those workers.

**Consequent fix (not yet implemented).** At the retarget: (1) release the retained cache
(`discardFrozenCanonicalTail`, which drops the companion rows/passes/inputs for the completed phase);
(2) do **not** call `retireSkippedCompanionPreparation`, because that is what zeroed the undo-collection
estimate and left the interval without a protected undo entry; (3) let the job continue
`CanonicalTail -> CompanionHistory -> UndoCollection -> CompanionBridge -> AtomicTransfer` so
`commitStoppedWithUndo` publishes both the companion representation and the undo; (4) keep the bounded
per-phase fallback purely as a safety net, with its raw-only commit documented as losing the display
representation and the undo. The cached tail is recomputable, so releasing it costs work, not content.

**Attempted fix implemented, then reverted (2026-09-17, `baafd68a` reverts the attempt).** Keeping the
companion preparation on the retarget (preference order plus a bounded 512-grain per-phase fallback)
restored the display -- `captures/successor-wrap-42` reports `overdub_ok=True`, up from `False` in the
reproduction -- but broke three things the user relies on: only the **first overdubbed bar** stayed
audible, the deferred **save never finalised**, and **no global undo entry was created**. Holding the
job in the companion phases keeps `captureStopOwner` and the frozen role owned for as long as they run
(the save queues behind them) and the raw-only recovery commit `commitPreparedCapturePassAtomic`
deliberately publishes no protected undo entry while the Playing route's `UndoCollection` phase is
skipped by the re-route. The correct fix therefore has **four** obligations at once: keep the note-edit
display representation, publish the protected undo entry, let the deferred save complete, and keep full
audio. It must let the retargeted job finish its companion **and** undo-collection preparation promptly
(retarget the companion-side bindings, not only `canonicalPassInputs`'s command epoch) and publish
through the undo-carrying stopped path -- neither skipping that work nor holding the slot while it runs
(openspec 5.9/5.10).

**Fix direction (not implemented).** The retarget must *complete and publish* the interrupted companion
preparation on the STOPPED route instead of dropping it: extend `retargetStoppedRecovery` beyond
`canonicalPassInputs`'s command epoch to the companion-side bindings (`canonicalTailInputGeneration`,
`frozenCaptureCompanions`, `frozenCompanionPasses`), let the job continue `CanonicalTail` ->
`CompanionHistory` with the Stopped destination, and publish through `commitStopped` once
`preparedCompanionCaptureVerified` holds; the raw-only fallback stays only for a horizon that truly
cannot be prepared. Note the diagnostic logs the phase after it is overwritten (`phase=6` is this
entry's own `setPhase`), so it should log the original phase.

**Display regression follow-up (2026-09-17).** The user reports that after the re-route the *first*
overdubbed notes disappear from the display when playback resumes from STOPPED while they stay
audible, and only the wrapped notes remain visible. The discarded horizon is the recorded note-edit
(companion) representation the display resolves those notes from; the raw capture pass still plays.
The re-route now keeps a horizon whose preparation completed and verified (`commitStopped` publishes
it on the STOPPED route) and drops only an incomplete one, logging
`Overdub stop retarget: companions=<published|raw_only> phase=<n>` so the next reproduction names
which sub-case it hit. The incomplete case still commits the raw pass alone; if the reported case is
that one, the follow-up is to make the interrupted companion preparation completable after the
retarget instead of dropping it.

**Implemented (2026-09-17, probe-driven).** `requestDeferredOverdubStopToStopped` now releases the
slot's retained canonical tail cache (`discardFrozenCanonicalTail`), rebinds the canonical command
epoch, leaves the job's phase untouched and does **not** retire the companion/undo estimates or drop
the horizon. The job walks `CanonicalTail -> CompanionHistory -> UndoCollection -> CompanionBridge ->
AtomicTransfer`, so `commitStoppedWithUndo` publishes the note-edit representation **and** the
protected undo entry, and it retires promptly instead of holding `captureStopOwner`/the frozen role for
the fallback window. `PrepareTransfer` keeps the companion path for a retargeted job
(`PendingStopAdmission::retargetedFromPlaying`), and the grain keeps a bounded per-phase fallback (128
grains, reset on progress) purely as a safety net -- its raw-only commit is documented as losing the
display representation and the undo.

Evidence: native `test_transport_stop_retarget_retires_a_midflight_playing_job` (companion path first,
then the bounded fallback in the fixture, then retirement); full native 2,213 cases / 1 skipped /
0 failures; RAM1 locals 6,400. Device `captures/successor-wrap-44` is **inconclusive**: the capture ring
overflowed (`capture_ring_overflow`, `SERIAL_INCOMPLETE`), which shed the rows the display, save and
undo verifiers read, so 5.9 still owes one clean run.

**Second attempt falls short, denial now precise (2026-09-17, `captures/successor-wrap-45`).**
Releasing both retained caches at the retarget (runtime `discardFrozenCanonicalTail` **and** the Loop's
prepared companion horizon via `restoreSupersededPreparedCompanionHistory`/`discardPreparedCompanionHistory`)
still stalls: the log shows `Overdub stop retarget: phase=7 dest=1 companions=kept` then, 0.76 s later,
`Overdub stop retarget: companion path stalled; raw pass only`. Combined with the probe bits, the
remaining denier is `LoopPlaybackRuntime::beginFrozenCanonicalTail`, which refuses when
`canonicalPassInputs.captureAttached()` -- and probe bit 3 (attached) was set. That attachment is the
predecessor's retained canonical inputs. Clearing it is not a retarget-time option: `discard()`
invalidates the canonical prefix (`beginFrozenCanonicalTail` also requires
`canonicalPassInputs.generation() == canonicalPrefixGeneration && ready()`), and rebuilding the prefix
needs playback turns the stopped transport does not provide. So the fix belongs at **retention time**:
`TrackPreparedStopCommit::completeDeferredCleanup` should not retain the committed canonical tail when a
successor interval is already live (`job.interval != captureInterval_`), because that cache is what
blocks the successor's own companion preparation -- it exists to save the *next* interval work, and here
it prevents it. Until that lands, the retarget keeps falling back to the raw pass: audio, save and undo
behave as before, the note-edit display representation is still lost in this edge case, and 5.9 remains
open.

**Fix, as specified before implementation.****Fix, as specified before implementation.** Make the retarget re-route as well as re-label: when a job that has already
passed the `PrepareTransfer` decision point is retargeted to Stopped, it must skip the remaining
PLAYING-only preparation (companion history, canonical tail, companion bridge) and continue on the
Stopped route's own path (`retireSkippedCompanionPreparation` -> `AtomicTransfer` ->
`DeferredCleanup`), which is exactly what a job admitted for Stopped does. The already-staged prepared
pass is still committed; the PLAYING-only companion publication for that interval is dropped, which is
the documented STOPPED semantics. Cover it with a native case that admits a populated job for Playing,
drives it into `CompanionHistory`, then retargets to Stopped and asserts it retires exactly once and
releases `captureStopOwner`, the frozen role and the receipt; then verify on device with the sequence
that reproduced the wedge (openspec task 5.3).

**Diagnostics landed (this entry).** One-shot `Deferred stop stalled: <reason>` notes for the grain's
owner/geometry and frozen-generation predicates, the cleanup and history-recycle prerequisites, and
`Overdub stop refused: <resource>` naming which of the five owned resources refuses the stop plus the
head job's phase/destination/closure/recycle/undo state. No behavior change; RAM1 locals 6,400.


**Retention guard implemented; premise corrected (2026-09-17, `captures/successor-wrap-46`).**
`TrackPreparedStopCommit::completeDeferredCleanup` now retains the committed canonical tail only while the
retiring job is still the live interval (`ownsLiveSession`), so a retiring predecessor no longer leaves an
attachment a live successor would trip over. Native capture suite 50/50 and the full gate stay green, and the
device save now completes (`persistence handoff: result=ok`, 77 ms). It does **not** unblock the retarget:
`-46` still logs `Overdub stop retarget: phase=7 dest=1 companions=kept` then
`companion path stalled; raw pass only` 0.69 s later, with `overdub_ok=False`. So the `captureAttached()` the
probe saw is **not** the predecessor's retained tail -- it is already present on the live interval's own
canonical inputs at the retarget, which is why a retention-time guard cannot help. Next discriminating step:
instrument where that attachment is set for the live interval (the `beginFrozenOverdub` /
`appendFrozenOverdub` callers on the wrap/normalization path) and whether the retarget may clear it together
with a prefix rebuild. Until then the retarget keeps falling back to the raw pass: audio, save and undo behave
normally, the note-edit display representation is still lost in this edge case, and 5.9 stays open.

## DEC-069 — The overdub wrap is a canonical-revision transition and must not advance the revision a pending stop job owns

**Date:** 2026-09-17
**Status:** Accepted — Option A implemented, native-tested and **device-verified** (`captures/successor-wrap-36`: the successor's crossing was deferred, the predecessor retired, the deferred commit then sealed the pass; `captures/successor-wrap-33` is the matching DEC-068 regression PASS).
**Owner:** `Track::commitOverdubWrap` (the live interval's loop-crossing transition), `LoopPlaybackRuntime::canonicalPassInputs` / `canonicalPrefix*` / `frozenCaptureOrder*` (the slot's single canonical preparation), `Track::beginDeferredOverdubStop` (the deferred stop job that owns that preparation)

**Context.** DEC-067 fixed *which transition owns the finalization resources* and moved the
canonical copy target, the transfer admission and the session-exit receipt to the STOP admission.
It fixed the live interval's **start**. It did not cover the live interval's **wrap**, which is the
third transition that reaches into the same slot-shared canonical preparation: `commitOverdubWrap`
commits the live interval's next capture pass, and a pass commit advances `Loop::playbackRevision`.

**Evidence.** `captures/session_20260917_172651.log` (manual 6-bar record, three sequential
overdubs on the DEC-068 firmware):

| time | event |
|------|-------|
| 570.855 | OD1 starts; wraps 581.454; `Reentry released` 587.875 (first interval, no job pending — healthy) |
| 586.564 | OD2 starts while OD1's job is pending |
| 590.687 | OD2 stop admitted → `Reentry window open` |
| 591.518 | OD3 starts (DEC-067 start gate: admitted) |
| 593.456 | OD3 `Overdub wrap committed` |
| 594.434+ | `Capture admission rejected: unmatched NoteOff ch=4 note=72` (and 95) repeatedly |
| 598.245 | OD3 stop refused: `Overdub stop deferred: deferred/frozen admission role is occupied` |
| — | OD2's job never emits `Reentry released`; its publication never resolves, so the wrap pass OD3 committed at 593.456 never becomes visible |

Native confirmation (`test_successor_wrap_keeps_the_predecessors_frozen_builders`, instrumented):
with a pending job whose frozen source is bound, the successor's wrap gives
`revBefore=1 revAfter=2`, the frozen binding is still present, but `canonicalPassInputs.ready()`
is **false** and the job sits in `CanonicalTail` for 20,000 grains without retiring.

**Mechanism.** `RuntimeCanonicalPassInputs::valid()` compares the live `playbackRevision` against
the revision the binding was made at (`expected_`). The successor's pass commit moves the live
scalar, so the pending job's frozen canonical source can never validate again:
`stepReservedFrozenCanonicalTail` returns `Denied` forever, `CanonicalTail` never completes,
`TrackPreparedStopCommit::completeDeferredCleanup` never runs, `runtime.captureStopOwner` and the
canonical/transfer/session-exit resources stay owned, and every later stop of the live interval is
refused. The queued wrap publication is also stuck behind the unresolved predecessor publication
(the journal is FIFO), which is why the third overdub "does not show or record at loop wrap" and
its notes sound lengthened into the previous overdub's content. Per DEC-067's own rule — *a
resource blocks the transition that first requires ownership of that resource* — the wrap is that
transition for the loop's canonical revision.

**Already landed hardening (part of this entry).** `commitOverdubWrap` used to (a) call
`resumeCanonicalAfterAppend`, whose `resumeCurrentAfterAppend` resets the frozen capture binding,
and (b) discard the frozen order/rewrite builders unconditionally. Both are owned by a pending
job (`beginDeferredOverdubStop` already refuses to touch them); the wrap now does neither while
`hasPendingDeferredOverdubStop()`. That removes a second way the wrap clobbered its predecessor,
but it does not remove the revision advance, which is the remaining cause.

**Options.**

- **Option A (recommended) — gate the wrap's pass commit on the job's retirement.** While a
  deferred stop job is pending, the wrap performs only its row-level obligations (straddling held
  notes, preview `wrapHeld`, wrap bookkeeping) and arms a deferred commit; the maintenance turn
  performs that commit as soon as the stop ring is empty, or the interval's own stop does it. The
  revision therefore advances only when no job pins it. Consistency: the live interval's *stop* is
  already refused while a job is pending (DEC-067), so the wrap becoming pending is the same
  capacity answer one boundary earlier. Cost: during an overlap the live interval's wrap pass
  becomes audible one cycle later than today, and an interval that crosses twice while deferred
  merges those rows into one pass (already legal — multi-wrap same-phase appends are supported).
- **Option B — make the predecessor's tail restartable across the append** (a frozen analogue of
  `resumeCurrentAfterAppend`) so the job re-normalizes against the advanced pass list instead of
  being invalidated. Cost: the predecessor's prepared publication and the live interval's queued
  wrap publication would then both describe the same advanced revision, so the snapshot journal's
  revision deltas must absorb a re-target; that is a publication-ordering change, not a gate.

**Decision.** Option A is preferred and is the smaller change; Option B is retained as the
fallback if the one-cycle audibility delay of A is unacceptable. No further code is written for
the revision gate until the owner picks one.

**Implemented (2026-09-17, owner chose Option A).** `Track::maybeCommitOverdubWrap` detects the
crossing, records the owed commit in `Track::overdubWrapCommitPending_`, and returns without
sealing; the normal per-tick playback turn retries the commit as soon as the stop ring is empty, and
`beginDeferredOverdubStop`/`startOverdubbing` consume the flag at the interval's own boundaries. The
straddling rows stay in the live producer (wrapped ticks are already legal there), so the held
note's reservation is never reset -- which is also what removed the live interval's
`unmatched NoteOff` rejections. Three native cases in `test_continuous_capture`
(`test_successor_wrap_defers_its_pass_commit_while_a_stop_job_is_pending`,
`test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off`,
`test_successor_wrap_keeps_the_predecessors_frozen_builders`); the first two fail without the gate.
Full native 2,212 cases / 1 skipped / 0 failures; `pio run -e teensy41-capture-serial` succeeds with
RAM1 locals 6,464. No new human-log literal was added: on this platform one added string in that
translation unit snaps 4,096 bytes of DTCM with no symbol size change, so the deferral is observed
through marker *ordering* (`Reentry released` before the successor's `Overdub wrap committed`).

**Device acceptance (2026-09-17, `captures/successor-wrap-36`).** The successor has to own rows
*before* its loop crossing or the deferred commit has nothing to seal, and the canonical preset
stops streaming when the predecessor's pass ends — so the registered entry point was run with a
temporary variant flag (`--successor-reentry-stream-bars`, default 0, canonical behavior unchanged)
that streams bars into the live successor interval, plus a stop phase placed 0.5 bar before loop
end (`A_stop_phase ~= (A_start_phase + delay_bars + overdub_bars) mod record_bars`; A starts ~1 bar
after the record stop, so an 8-bar record needs ~7.5 bars). Recorded sequence: A stop 57.987 s
(`Reentry window open`), B start 58.605 s at tick 6075, B's crossing of tick 6144 **deferred**,
`Overdub wrap committed @ tick 6460` at 59.615 s -- reachable only through the deferred retry, since
an immediate commit would have logged the crossing tick and the next crossing is a full loop away --
then `Reentry released` at 60.534 s (the predecessor retired: the outcome the old path made
impossible), B's own `Reentry window open` at 62.736 s, and no `unmatched NoteOff` or
`Capture admission rejected` row anywhere in the run. The harness's own verdict for that capture is
INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED) only because the `#CAP` tiers its record-phase verifiers
read were shed; the device human channel shows the 6144/6144 record and the complete sequence. The
matching regression run `captures/successor-wrap-33` (DEC-068 flags, same firmware) is a clean
harness PASS: `overlap=True`, `denied_stop=True`, A retired, B's stop refused then admitted, B
retired, `bars=8/4 count_ok span_ok`.

**Not claimed.** No task-4.2/4.3/5.3–5.6 checkbox is closed by this entry. `#CAP` tier shedding
still makes unrelated baseline assertions inconclusive, and the temporary successor-stream flag is
not yet promoted to a registered stimulus variant (that promotion is the owner's call).

## DEC-068 — An overdub session belongs to one capture interval

**Date:** 2026-09-17
**Status:** Accepted — implemented and device-verified (`captures/successor-reentry-27`, first clean `PASS` of the reentry stretch: `overlap_ok`, `denied_stop_ok`, `retirement_ok` all true).
**Owner:** `Loop` overdub-session state (`openOverdubSession` / `closeOverdubSession` / `overdubSessionPassIds_` / `sessionExitHandedOff_`), the session-exit receipt ring (`SessionExitSnapshotHandoff`), `Track::beginDeferredOverdubStop`, `TrackPreparedStopCommit::completeDeferredCleanup`

**Context:** the task 5.1(d) case (`test_successor_same_interval_transfers_exactly_once`) proved the
capture-transfer half of exact-once and then exposed the publication half. With A's deferred job
pending and B live, `completeDeferredCleanup` unconditionally called
`loop.handoffSessionExitSnapshot(...)` and `loop.closeOverdubSession()`:

1. the receipt was built from the **current shared session state**, which under reentry belongs to
   B, so A's session-exit publication carried the successor's pass ids;
2. `closeOverdubSession()` closed **B's** session while B was still recording
   (`loop.hasOverdubSession()` false for a live interval);
3. B's own `DeferredCleanup` then could not hand off (`!hasOverdubSession()`), so B's job reached
   phase 11 and never retired — its descriptor leaked and its session-exit publication never
   queued. Reachable on device in the verified DEC-067 sequence (`captures/successor-reentry-25`:
   A released at 52.195 s with B live since 49.986 s).

Root cause: `Loop::openOverdubSession` early-returns while `hasOverdubSession()`, so a successor
interval **joins the predecessor's session**, while `sessionExitHandedOff_`, the receipt ring and
`lastCommittedPassId_` are written as if a session belonged to one interval.

**Decision:** an overdub session belongs to one capture interval. A deferred stop job carries the
session-exit receipt for **its own** interval from stop admission onward. Retirement consumes only
that job-owned receipt. A retiring predecessor may close shared live session state only if it still
owns the live interval; otherwise the successor's session remains open and independent.

```text
A live                      → session A
A STOP admitted             → A's session tail captured into the job-owned receipt slot
                              (live session state keeps serving paint/overlay until retirement)
B START                     → opens session B (its own index/ids), does not join A's tail
A cleanup                   → fills its receipt's final pass id, marks it ready, consumes only
                              that receipt, retires; must not close session B
B STOP later                → captures receipt B, publishes B, retires B
```

Call-site audit — who consumes the shared session state, and what each needs:

| consumer (current) | what it actually needs | after DEC-068 |
|---|---|---|
| `handoffSessionExitSnapshot` (deferred cleanup) | the retiring interval's own pass/session ids | **replaced**: the job's receipt was captured at its stop admission; `lastPassId` is filled at its commit |
| `closeOverdubSession` (deferred cleanup) | end *this* interval's session | **gated**: only when the retiring job still owns the live session (`job.interval == captureInterval_`, the current implementation predicate for session ownership) |
| `stepPendingSnapshotPublish` (`oldestPendingSessionExitReceipt`) | one interval-level receipt per turn, FIFO | unchanged, plus: a receipt whose final pass id is not yet committed is **not ready** and is skipped in place (FIFO preserved) |
| `queueSessionExitSnapshot` (box assembly) | the receipt's ids, not the live session | unchanged (it already prefers `oldestPendingSessionExitReceipt()`) |
| display / paint / pending-changes / overlay (`hasOverdubSession()`) | "a session is being recorded or finalized here" | unchanged: the live session state stays populated until the retiring job owns the last interval |
| `TrackUndo` session queries | the live session's undo state | unchanged (the job already owns `beforePublished`) |
| legacy synchronous stop (`finalizeCommitSideEffects`) | one-shot handoff with no job | unchanged (`handoffSessionExitSnapshot`) |

**Implementation shape (pinned):**

1. `beginDeferredOverdubStop` (which has already proved no job owns the finalization resources)
   reserves a receipt slot and copies the live session tail into it — pass ids, companion ids,
   cursor, session index — leaving it **not ready**. A full ring is the documented stop denial it
   already is (`canHandoffSessionExitSnapshot()`); no capacity is added.
2. `completeDeferredCleanup` fills that receipt's final pass id from its own commit, marks it
   ready, and closes the live session only when the job still owns it. It never touches another
   interval's session and never builds a receipt from live state.
3. The publisher consumes only ready receipts, oldest first, exactly one per turn.
4. A successor start opens a **fresh** session even while a predecessor's tail is still draining;
   the predecessor's tail lives in its own receipt.
5. Session ownership is the architectural rule; `job.interval == captureInterval_` is only the
   current predicate for it and may be replaced without reopening this decision.

**Required native gates (six cases):** (1) A receipt isolation — A's publication carries A's
pass/session ids even after B started; (2) B session survival — A's cleanup leaves
`hasOverdubSession()` true for live B; (3) A retirement — A's descriptor retires exactly once while
B remains live; (4) B retirement — after B stops, B queues its own session-exit publication and its
descriptor retires; (5) no receipt reuse — A's and B's receipts have distinct interval/session
identity; (6) supersession — A retiring through the causal-base-superseded route consumes only its
own receipt and cannot close B.

**Device acceptance:** the DEC-067 overlap sequence extended past B's admitted stop until **both**
descriptors are retired and **both** session-exit publications are resolved (a second
`Reentry released` for B, no leaked descriptor, no stop denied for an occupied finalizer).

**Not in this decision:** per-interval receipt capacity (the ring stays at
`kSessionExitReceiptCapacity = 4`), display/paint routing changes, DEC-067's admission direction,
and DEC-045's idle publication scheduling.

**References:** `openspec/changes/successor-capture-reentry-unguarding/` tasks 2.3/5.1/5.4/6.2,
[`CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) § Now implementing, native
`test_successor_same_interval_transfers_exactly_once`.

## DEC-067 — Finalization-resource admission is stop-directed

**Date:** 2026-09-17
**Status:** Accepted — implementation authorized by the user; device acceptance (the successor START/STOP proof) remains open.
**Owner:** `Track::startOverdubbing` (start admission), `Track::beginDeferredOverdubStop` (stop admission), `LoopPlaybackRuntime` canonical output / transfer admission, `Loop` session-exit receipts

**Context:** the successor-reentry device case could not be produced. With the probe's presses
finally landing inside the predecessor's deferred window (`hitl.probe_press`, `90342d7c`;
`captures/successor-reentry-19` / `-20`), the device refused every press with
`Overdub start rejected: canonical copy chunks are not reserved` — press actions +500 ms and
+909 ms (`-19`), +1116 ms and +1515 ms (`-20`), against `bridge_ready` +1458 ms / +1681 ms —
and the successor only opened once its action landed after the predecessor had fully finalized
(`-13` +1738 vs +1424, `-14` +1906 vs +1444, `-17` +1530 vs +1008; the one early start, `-16`
+637 ms, is a stop that never deferred). The declared capacity —
`successor-capture-reentry-unguarding` design task 1.2, *"START requires the live role to be
free; STOP requires the deferred/frozen role to be free"* — was therefore unreachable, because
`Track::startOverdubbing` also gated on three **finalization** resources.

**Decision:** a resource blocks the transition that first requires ownership of that resource.
Overdub START admission is governed only by the resources required to establish and sustain
the live capture interval; the deferred-finalization resources — canonical capture output,
active overdub transfer admission, and session-exit snapshot handoff capacity — are checked
when that interval requests STOP. Call-site audit (each verdict is "what object would B
actually use before B's stop edge?"):

| start gate | resource, and who uses it | first required at | verdict |
|---|---|---|---|
| `liveCaptureStagingIfPrewarmed() == nullptr \|\| !captureStopJobs_` | B's live ingress for the whole interval (`viewSet->reset()`, append path); the descriptor ring is structural | START | **keep** |
| `canonicalCaptureOutputAdmitted()` | written only by `stepFrozenCaptureCanonicalCopy` while B finalizes | STOP | **move** — added to `beginDeferredOverdubStop` |
| `hasActiveOverdubTransferAdmission()` | consumed by `stagePreparedCapturePassFromActiveAdmission` at B's stop, and already checked by `beginDeferredOverdubStop` | STOP | **remove from START** (already at STOP) |
| `canHandoffSessionExitSnapshot()` | `queueSessionExitSnapshot` at B's stop | STOP | **move** — added to `beginDeferredOverdubStop` |
| `MemoryMonitor::admitOperationMemory(OverdubStart)` | the interval's own live working set | START | **keep** |
| `runtime->captureStopOwner != nullptr` (already at STOP) | the single finalization working set | STOP | **keep** |

The start path additionally discarded the *frozen* order builders
(`discardFrozenCaptureOrder` / `discardFrozenCaptureCanonicalOrder`) unconditionally, which
would clobber a pending predecessor's in-flight order/rewrite. Those discards are now
conditional on no deferred stop being pending and no live stop owner.

**Architectural property (pinned):** depth-2 *capture* concurrency does not imply depth-2
*finalization* working sets. At most one interval may own the bounded canonical/transfer
finalization resources; another interval may coexist only in the live capture role. That is
why B START is legal while B STOP can be denied, and why this decision does **not** make the
canonical working set concurrent (option B below).

**Device acceptance this authorizes (task 6.2):** two deterministic capture edges replace
timing guesses — `successor_start_window_open` (the predecessor has frozen its interval and
relinquished the live role while its stop is still pending), emitted as
`DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>`, and
`canonical_finalization_capacity_released` (the finalization resources are free again),
emitted as `DIAG,canonical_release,<slot>,<loopLengthTicks>,<availableChunks>`. The wire tags
are short on purpose: longer ones tipped a `.data`/`.bss` alignment snap that cost 4 KB of the
RAM1 local allowance (`.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`).
Required sequence: A OVERDUBBING → stop A → A deferred; B START succeeds **while A is still
pending, asserted at the instant of B's start**; distinctive B input accepted at its exact
ticks; B STOP denied non-destructively while A owns finalization capacity (B stays live; rows,
pending notes, reservations and interval identity unchanged); A releases; B STOP admitted and
B finalizes exactly once; A and B content exact; no stale A cleanup mutates B.

**Consequences:** a successor interval can record while a predecessor finalizes, and its stop
carries the explicit denial when finalization capacity is still owned — the outcome task 2.2
already specifies. Design task 1.2 becomes the operative rule; tasks.md 2.1's start-side
canonical/transfer/handoff requirements are superseded by the stop-side checks above.

**Not in this decision:** giving each interval its own canonical output (option B — grows the
arm-time envelope and makes two finalization working sets concurrent), narrowing the declared
capacity to "reentry after finalization" (option A), and any change to the staging ceiling,
the deferred-stop grain budget, or the event-horizon ceiling.

**References:** `captures/successor-reentry-19` / `-20` (device denials),
`captures/successor-reentry-13` / `-14` / `-17` (starts only after `bridge_ready`),
[`successor-capture-reentry-unguarding`](../openspec/changes/successor-capture-reentry-unguarding/),
[`CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) § Now implementing item 2.

## DEC-066 — Per-owner capacity derivation for the stop working set, under the existing ceilings

**Date:** 2026-09-17
**Status:** Accepted — implemented and device-verified (work shaping + companion-row reserve shaping). The event-horizon owners remain ceiling-bound by design; see the derivation table.
**Owner:** `LoopPlaybackRuntime` playback/capture working set; `Loop` prepared companion history and prepared companion rows

**Context:** Six owners were reserving the *same* 2,048-row/event horizon although
their worst-case demands have six different determinants. That both over-reserves
(the arm's ~1.47 MB) and over-processes (`Phase::Trim` popped one unused row per
quantum unit, ~2,030 units for a full horizon with a sparse overdub). The
determinants were measured from the code before this decision (each citation below
is the enforcing check, not a comment).

**Decision:** each owner's capacity is derived from *its own* worst-case bound,
clamped to the existing architectural ceiling; no owner gets a new fixed cap below
its ceiling.

| owner | ceiling | derived worst-case bound | enforced at | known |
|---|---|---|---|---|
| `RuntimeCaptureCompanions` rows | 2048 rows | eligible frozen transforms (Delete / Update-Length only) ≤ distinct occupied source note identities | `RuntimeCaptureCompanions.cpp` begin/step | punch-out |
| `Loop::preparedCompanionHistory_` | cumulative `editPasses + published companions` ≤ 2048 rows | per interval: the same transform bound plus the canonical history rows; per interval the **size** is the actual demand | `LoopPreparedCompanionHistory.cpp` begin guards | history at arm, increment at punch-out |
| `RuntimeCanonicalPassInputs` | 2048 events | `max(max_i raw_i, F, Σ effective_i + e_F)`, and `C >= P` for the descriptor arrays, `R <= 2048` for the history pointers | `RuntimeCanonicalPassInputs.cpp` step/append | Σ effective and P at arm; `F`/`e_F` only at punch-out |
| `RuntimeCompanionPasses` | 2048 events | `Σ effective_i + e_F` (the frozen capture is appended as a final pass) | `RuntimeCompanionPasses.cpp` aggregate check | per pass during preparation; total at punch-out |
| `RuntimeCompanionApply` | 2048 events | **per pass** `max_i (e_i + created_i)`, never the aggregate | `RuntimeCompanionApply.cpp` begin/canAppend | per pass during preparation |
| `Reconstruction` / `SpanWalk` / `NotePairLookup` / `BridgeMap` | `2C` notes / `C` flags / `C` lane / 2048 records | per pass, driven by one apply candidate; spans/notes ≤ the pass's events, map records = applied rows | each owner's `count > capacity` check | per pass during preparation |
| playback views / bridges / staging | 2048 events | loop content **plus one frozen interval inside the same horizon**, not additive; staging = one interval ≤2048 events and ≤128 open reservations | `RuntimePlaybackView.cpp`, `RuntimePlaybackBridge.cpp` | arm for content, punch-out for the interval |

A single shared "interval horizon" constant is therefore wrong, and the
event-horizon owners cannot shrink below their ceiling soundly, because their
interval term *is* the staging admission bound and their reserve is taken at the
arm - before the interval exists.

**Pinned work rule (landed):** preparation must never spend units proportional to
the *granted* horizon. `Loop::beginPreparedCompanionHistory` sizes the working row
vector to this interval's actual demand (allocation-free, inside the grant) so
`Phase::Trim` has nothing to pop, and `Phase::RecycleGrow` restores the horizon in
one batched resize instead of one row per quantum unit. Fit is checked against
**capacity**, never the current size, so a later larger interval is still
admissible against the same reserve. Guarded by
`test_companion_history_prep_units_scale_with_demand_not_reserve`, which prepares one
companion row over a 512-row grant and requires Ready inside 32 quantum units (the
old walk needed ~511).

**Pinned reserve rule (landed):** the per-interval increment of the
row-horizon owners may be sized from the *actual* sealed demand where it is known
before the timing-sensitive work; the canonical/cumulative side keeps its ceiling.
Two consequences the implementation must respect:
1. `RuntimeCaptureCompanions`' admission check must bound the **eligible** rows it
   will produce (Delete/Length transforms), not the whole frozen change vector -
   the vector also carries the interval's `Add` entries, which become the capture
   pass and never become companion rows. Until that check is fixed, a smaller
   granted capacity could deny a stop whose captured engine is dense but whose
   transform set is small.
2. Sizing a *reserve* at the seal (punch-out) makes that reserve fallible at the
   stop. A reserve failure must therefore degrade the companion path, never refuse
   the audible stop; the audible state transition is already independent of the
   deferred work.

**Not in this decision:** reducing the staging admission bound itself (a
user-visible interval ceiling), and C4's device-wide deferred-stop working set
(explicitly not taken).

**References:** [CURRENT_WORK C3 baseline and per-owner analysis](Runtime/CURRENT_WORK.md),
[ExecutionBounds — record-arm reservation](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17),
DEC-065 (admission-provisioned staging).

## DEC-065 — Capture staging is provisioned by capture admission, not by boot

**Date:** 2026-09-17
**Status:** Accepted — **validated on device 2026-09-17** (user-accepted; harness-gate repair deferred as separate work)
**Owner:** `LoopPlaybackRuntime` capture-staging owner; `Track` capture admission (record arm and overdub start)

**Context:** The two capture-admission staging roles per slot are reserved by
`TrackManager::prewarmPlaybackRuntime` at boot for every enabled or non-empty slot
(`Track::prewarmPlaybackForSlot(s, prepareCapture=false)`). Measured on the loaded device
([`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log)):
phase 1 prewarms 8 slots for 1,336 KB of PSRAM and phase 2 adds one slot for 156 KB
(~157 KB per slot), i.e. `tag 3 = 1334 KB` is paid for slots that may never record. The
record arm's own measured increment is 1,240 KB (views/bridges 790 + the seven
companion/lookup prewarms 450) and it fails on exactly that device because the *last*
reserve lacks 80 KB. Staging is only needed by a slot that records, and the record arm
already reserves it (idempotently) through `prewarmViews` → `prewarmCaptureStaging`.

**Decision:** Capture staging is provisioned by **capture admission**, not by slot
existence.

1. **Boot establishes only metadata/order needed to make admission deterministic:** the
   runtime slot, the per-track stop-descriptor ring, the transfer horizon and the
   canonical copy directory. It reserves **neither** staging role.
2. **A capture operation is admitted only after its required staging capacity has been
   reserved successfully.** Order is always *reserve → require → commit*: the record arm
   reserves both roles through its existing admission; `Track::startOverdubbing` reserves
   both roles on its own cold button turn before any mutation, and its
   "staging must already exist" rejection becomes an explicit denial **after** that
   reserve, never an implicit dependence on boot.
3. **Both roles are reserved by the same admission.** The live ingress role and the frozen
   stop-job role keep their DEC-058 split and constant-work
   `exchangeCaptureStagingRoles`; punch-out therefore still never allocates, and the
   successor interval gets a physically separate, already-reserved live role.
4. **Ownership transfer and release follow the data, not UI state.** Live capture owns the
   live role first; a stop job that freezes it owns the frozen role until its own cleanup.
   Staging storage is released only by the **last owner** (runtime teardown), never because
   live recording ended while an admitted deferred stop still owns backed rows.
5. **Capacity denial is explicit and atomic.** If the reserve fails, no interval identity,
   pending note, preview, stop ownership, reservation or track-state transition becomes
   authoritative; the operation is denied with an explicit `#MEM_DENIED` reason and prior
   playback/capture state is intact.
6. **No allocation in a deferred grain.** Moving the reserve to admission changes *when*
   memory is taken, never permitting allocation inside deferred-stop processing.

**Scope boundary:** this decision covers staging only. The seven companion/lookup
prewarms and the prepared-companion-history rows stay owned by the record arm, because
`Loop::prewarmPreparedCompanionHistory` is documented STOPPED-only (it reshapes canonical
vectors under an active read borrow). A loaded-slot overdub therefore keeps today's
degraded canonical-prefix/companion path; upgrading it would be a separate decision.

**Gates before this is closed:** boot ordering (capture-only structures still initialized
in the documented order); admission atomicity; ownership transfer; last-owner release;
explicit capacity denial; zero allocations in deferred grains; two-role reentry
consistency with predecessor-pending successor capture. Device measurement must record
boot free headroom, arm allocation delta, post-arm headroom, stop-grain allocation count,
deferred completion and successor reentry, and - once headroom allows - one end-to-end
successful 8-bar record + overdub run.

**Supersedes:** the "boot prewarm admission" provisioning half of the DEC-058
dual-staging follow-up (`docs/DECISION_LOG.md:117-138`). The dual-role split itself, the
per-slot cost (160,536 B/slot for two 2,048-event roles) and all other DEC-058 gates stand.

**References:** [ExecutionBounds — record-arm reservation](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17),
[CURRENT_WORK steps A–F](Runtime/CURRENT_WORK.md).

**2026-09-17 implementation amendment — the point-of-use gate must match the allocator.**
Moving staging off boot was not sufficient to admit the loaded-set 8-bar arm: the remaining
denial was `op=companion_history_reserve, reason=heap_low, need=81920B, heap_free=76KB` with
`pool_refused=0/0B` and ~1.27 MB of pool free, because the 80 KB `EditPassVec` reserve is
`InternalHeapFirstAllocator` (malloc, then extmem) while the gate vetoed on the heap floor
alone. `admitOperationMemory` now treats the point-of-use probe as the **authority** for
`CompanionHistoryReserve` - the probe tests exactly the malloc-then-extmem pair the real
allocator will use - so the heap floor no longer refuses a request the pool can serve.
Denial remains explicit when the probe fails or the pool refused inside its window. A gate
that denies a succeeding reserve violates clause 2 (*reserve -> all succeed -> commit*), so
this is part of the decision, not an exception to it. Device evidence:
[`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log) - 0 denial,
0 refusal, 0 FATAL, record + overdub + deferred stop to PLAYING.

## DEC-064 — Exclusive raw capture ranges for queued overdub preparation

**Date:** 2026-09-16
**Status:** Accepted direction; storage foundation only, integration gates open
**Owner:** LoopEventStore storage; Track/Loop capture lifecycle

**Context:** The user-approved continuous-capture plan requires immediate reentry
with earlier stop jobs pending. The real Button A, manager and Track routes all
reject reentry because live capture, staging and cleanup still share singletons.
Existing chunk detachment walks the whole directory and cannot be the action edge.

**Decision:** Extend LoopEventStore with an exclusive read-only CaptureRange and
pre-reserved tail rotation. Swap ownership directories, retain the exact end,
validate pool/range/rewrite generations, and reclaim in bounded chunk units.
Do not share writable prefixes, clone Loop/Track, add a worker graph, remove
reentry guards prematurely or reinterpret faster cleanup as immediate capture.

**Constraints:** Range tests do not prove interval queue, note-lifetime credits,
fallible cold heap admission, causal companion resolution, undo/snapshot ownership,
old-job cleanup isolation or audible readiness. Keep task 2.3 and HITL open.
Canonical/persistence ownership and SD format are unchanged. No production Track
caller uses the range cut in this checkpoint.

**References:** [ownership audit and migration](../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md),
[authorized plan](../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md).

---

**2026-09-16 integration follow-up:** Extend Track's existing worker with a
stopped-interval receipt rather than introduce another worker. Save owner/slot,
closure phase, geometry and interval identity; transfer pending notes by swap.
Reclaim only job-owned notes and qualify Track-state completion by interval.
This implements part of the approved migration; shared Loop/runtime capture
owners and immediate reentry remain guarded and incomplete.

**2026-09-16 integration follow-up — dual capture-admission staging roles:**
Split the runtime's single `RuntimePlaybackViewSet::captureStaging` into two
independently prewarmed roles inside the same owner: a *live* ingress staging and
a *frozen* stop-job staging. Live ingress (`TrackCaptureInput`, overdub wrap,
`startOverdubbing`) always writes the live role; the one admitted stop job
borrows the frozen role. Punch-out freezes the live role and exchanges the two
indexes in constant work (`LoopPlaybackRuntime::exchangeCaptureStagingRoles`),
so the job keeps its immutable rows while a successor interval owns a physically
separate buffer. Job cleanup resets only its own retired frozen role. No staging
is copied at the action edge, no new owner/module is introduced, and no canonical,
persistence or SD ownership moves. This closes the "staging is shared by live
input and the frozen stop" gap named in the ownership audit. Cost is one extra
2,048-event staging buffer per fully-prewarmed runtime slot (external-first).
Boot prewarm admission was measured on device and passes: PSRAM free
5,143,968 -> 3,859,680 B across the 8 boot slots (160,536 B/slot, matching two
stagings per slot with no internal-heap fallback), all slots ready with 8 credits,
and 46.1 % of the 8.38 MB pool still free after prewarm
(`captures/task23-staging-prewarm/result.json`, build `fb4ab3a9…`). The
selected-slot full view/bridge prewarm plus loaded dense content still needs its
own footprint check before the reentry guards are removed. FIFO, causal
predecessor/source, session/preview/undo receipts and the three public reentry
cases remain open.

**2026-09-16 integration follow-up — sealed capture ownership transfer (APPROVED):**
On successful stop-job admission, ownership of the stopped raw capture extent
transfers from `Loop`'s producer to the admitted `CaptureStopJob::rawRange`. `Loop`
retains only a **job-identified** non-owning borrow for consumers whose semantics
span deferred finalization: capture-overlay materialization and pending NoteOff
identity resolution. Borrowed ranges are immutable to Loop consumers — ordering
operations use scratch storage, never an in-place sort of the range. Producer
reset/reuse (`resetCaptureExtentPreservingReservedStorage`) must not alter
transferred storage. Release is conditional on matching job identity, so job A's
cleanup cannot clear job B's binding. Queue-full or otherwise denied admission
performs no seal and no transfer. Consumers resolve capture material through one
internal Loop read source that exposes the producer and finalizing sources
separately, so mutual exclusivity is not a permanent architectural truth.
DisplayManager is unchanged. Wire this before removing the successor-capture
guard, and do not unguard continuous successor capture in the same change.
**Status 2026-09-16: wired.** `CaptureStopJob::rawRange` + job-identified Loop
borrow, a single read source exposing producer and finalizing extents separately,
scratch-only ordering, closure identity resolved from the finalizing extent,
`canSealCaptureRange` precheck so denial performs no transfer, and
`resetCaptureExtentPreservingReservedStorage` for the producer. New functions are
FLASHMEM; `Loop::beginCapture`/`discardCapture` moved to flash to hold 13 ITCM
banks (RAM1 headroom 6,752). Device boot-prewarm admission was re-measured and
passes: PSRAM free 5,142,912 -> 3,856,320 B (unchanged from the dual-staging
build), internal heap 352,256 -> 311,296 B, pool chunks 512 -> 416 (+4/slot = the
four range destinations per track), all slots ready with 8 credits
(`captures/task23-sealed-range-prewarm/result.json`, build `41b30d18…`).

## DEC-063 — Protected stop undo publication and bounded reclamation

**Date:** 2026-09-14

**Status:** Accepted — user: “accept recommended solution”

### Decision

Implement the [recommended undo publication transaction](../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md).
Preserve feedback-based oldest-entry trimming: complete each entry's bounded
final-reference release before rechecking actual memory pressure. Prewarm private
entry, stack bookkeeping and retirement capacity before accepting capture; charge
this work separately from DEC-062's 6240 ranked grains.

Protect the affected Track from competing mutations and hide intermediate stack
state. Reserve deferred snapshot attachment storage. Preserve one final
canonical/audible/undo publication boundary and defer disabled-pass reclaim until
it cannot invalidate prepared proofs. Physical retirement must begin only when
recovery no longer requires the removed state.

While protected, keep the latest pending Undo or Redo request per Track and
report deferral. Execute that request exactly once after finalization using the
latched destination's normal command policy. Repeated/conflicting presses
coalesce. Transport Stop remains immediate and cannot restore PLAYING; other
Tracks keep independent command state.

### Reuse, alternatives and verification

Extend TrackUndo, Track/GlobalUndoStack, LoopSnapshot lifetime, PassReclaim and
TrackManager. No new Manager, undo kind or SD schema. Do not adopt fixed trim
counts, latched pressure, restricted redo/pressure eligibility or synchronous
unbounded release at the handoff. The controlled 11-entry counterexample proves
retaining roots can change survivors from 10 to 8; it is not hardware timing.
Native lifetime, survivor parity, command, attachment, recovery and quantum-bound
gates precede active integration. Task 2.3 and remaining validation/HITL gates
stay open. Approval does not authorize firmware upload or claim completion.

---

## DEC-062 — Provisional ranked-preparation allowance

**Date:** 2026-09-14

**Status:** Accepted — user explicitly approved provisional allowance

### Decision

Allow **6240 grains** for ranked preparation while integration proceeds. Determine
realtime reservations, service rate and acceptable elapsed stop latency when
HITL can exercise the complete path. Lack of a measured realtime reservation is
not a blocker to implementing and testing this provisional path.

This allowance covers the prepared-order ranked-tail aggregate, whose current
C=2048,P=2,B=2048,H=R=0,Q=32 bound is 6240. It is separate from the legacy
3072 order/rewrite credits and excludes closures, initial canonical ordering,
canonical rewrite/transfer, cleanup and denied service turns. It is not a
6240-grain total-stop claim or an exemption from source/capacity/epoch checks.
Other horizons must fit the allowance or remain outside this provisional case;
accepted capture must never be silently omitted when a bound is exceeded.

### Rationale and validation

The old 3072 was an implementation work reservation, not an established product
latency limit. Comparing the new aggregate against it must not block continued
integration. Preserve explicit bounded grains and native parity; use overflow-free
HITL to measure costs, scheduling gaps, total stopping latency and MIDI deadlines
before finalizing realtime reservations. Task 2.3 and the listed validation/HITL
gates remain open. No upload is authorized by this allowance alone.

### Related OpenSpec

`resumable-midi-boundary-playback-maintenance`; supersedes treating the old
3072 allowance or missing HITL-derived realtime sizing as prerequisites to
implementing the provisional ranked path. Final realtime qualification remains required.

---

## DEC-061 — Stable raw-tick ordering after Create

**Date:** 2026-09-14. **Status:** Accepted; user explicitly approved the Create
reference correction and continued implementation.

Each canonical per-pass Create appends its payload in stored order, then stably
sorts the complete pass by ascending raw tick. Existing events precede appended
events at equal ticks; each group retains its internal order. Empty Create
payloads still sort. No Off-before-On tie-break is added to stored Create order;
DEC-059 pairing and DEC-060 per-pass application/merge remain unchanged.

This explicitly corrects the previous tick-only `std::sort` reference. Existing
histories may consequently materialize equal-tick identities in a different
order. Parity is measured against the revised reference. Create's existing
present-ID bookkeeping remains: added nonzero NoteOn identities re-enter the
sequence's present-ID set after earlier Delete rows.

The [Create reassessment](../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md)
contains a native 25-event counterexample and the approved reference patch.
Task 2.3, production integration, aggregate bounds, and validation/HITL gates
remain open; semantic approval does not close them.

## DEC-060 — Canonical per-pass companion application

**Date:** 2026-09-13. **Status:** Accepted; user explicitly endorsed canonical
per-pass application as the reference for combined capture preparation.

Apply the frozen companion sequence independently within each participating
canonical pass, including frozen capture, then merge pass results in canonical
pass order with the existing stable raw-tick merge. Repeated NoteIds in separate
passes are resolved independently. Pairing within each pass retains DEC-059.
The flattened incremental seal path is not the reference for this preparation.

The approved [scope reassessment](../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md)
records Length-overlap and repeated-NoteId Delete counterexamples. Input must
retain immutable pass membership/order and provenance through application and
merge; flattened base events alone cannot prove canonical parity. This preserves
canonical materialization semantics. Production transfer, bounded admission,
MIDI trace validation, and HITL remain open; approval is not evidence of completion.

## DEC-059 — Deterministic companion-pairing lane runs

**Status:** Accepted — explicit user authorization, 2026-09-13. Task 2.3 and
listed validation/HITL gates remain open.

**OpenSpec:** `resumable-midi-boundary-playback-maintenance`.
**Evidence:** [companion pairing reassessment](../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-PAIRING-REASSESSMENT.md).

### Decision

Runs are contiguous within the filtered channel/pitch lane of note events.
Equal ticks separated by a different tick remain separate runs. Each run
processes Offs before Ons, preserving order within each kind. Existing LIFO
pairing and wrap fallback remain unchanged. In the counterexample
`On@96, On@0, Off@96, Off@96, Off@0`, target 0 pairs with Off index 3 at tick 96.

This is an explicit semantic correction to the companion-pairing reference.
Parity must be measured against the revised reference. The former comparator
was non-transitive and could produce different pairings with different sorting
algorithms. This decision authorizes applying the reviewed patch and matching
bounded lookup; it does not change playback's global phase ordering or waive
DEC-058 capacity, grain, coverage, latency, MIDI-trace, and recovery gates.
The approving review itself changed no repository files or firmware behavior.

---

## DEC-058 — Runtime-owned derived playback views and bounded stop admission

| Field | Value |
|-------|-------|
| **Status** | Accepted — implementation authorized; evidence gates remain open |
| **Date** | 2026-09-13 |
| **OpenSpec** | `resumable-midi-boundary-playback-maintenance` |
| **Reassessment** | [`ARCHITECTURE-REASSESSMENT.md`](../openspec/changes/resumable-midi-boundary-playback-maintenance/ARCHITECTURE-REASSESSMENT.md) |
| **Transfer** | [`OWNERSHIP_TRANSFER.md`](../openspec/changes/resumable-midi-boundary-playback-maintenance/OWNERSHIP_TRANSFER.md) |

### Decision

Move only the **derived active playback-view lifecycle** — ordered event view,
coverage, cursor, bridge, and continuation — from `Loop`’s mutable
`playbackOrder_` path to per-slot `LoopPlaybackRuntime`. `Loop` retains
canonical passes, capture, revision, persistence, undo/redo, and invalidation
ownership.

`OVERDUB_STOPPING` is an active transport state. It continues normal playback
and MIDI priority while bounded reserved finalization completes. Transport Stop
has precedence over a pending PLAYING transition and forces STOPPED recovery.

`DeferredJobScheduler::runFrame()` remains the existing load-job child. No
`RuntimeWorkBudget`, second scheduler, extra MIDI service call site, persistent
schema change, or partial publication is authorized.

### Implementation correction (2026-09-16)

A moving ledger must not restart stable canonical bridge sorting/copying.
Boundary reconciliation reuses the same compiler, owned records and sorted
indexes; affected-note summaries conservatively narrow admission validation.
Original closure and command-epoch protection remain mandatory. This changes
neither audible timing semantics nor the provisional reservation policy;
consecutive-overdub HITL and real-time service bounds remain acceptance gates.

### Rationale

The DMA baseline still attributes large post-overdub MIDI gaps to synchronous
`track_order` and `load_frame` work. A caller-side budget cannot bound their
content-sized operations. The existing runtime per-slot owner already owns the
ledger and merged-event lifetime required to bind an immutable playback view;
moving canonical content there would duplicate `Loop`’s established authority.

### Required evidence before closeout

- Fixed external-memory/RAM1 capacity for three views, two bridges, and capture
  reservations.
- Structural or empirical-plus-policy bounds for each active grain and eligible
  load Commit phase.
- Coverage and deferred-stop contracts under maximum phase advance and denied
  turns.
- MIDI-trace parity and overflow-free HITL 4b.3 evidence.

### Related decisions

- [DEC-043](#dec-043-execution-bound-class-is-a-gated-architectural-property)
- [DEC-027](#dec-027-deferred-job-scheduler-north-star)
- [DEC-038](#dec-038-overdub-wrap-commit-and-session-undo)
- [DEC-049](#dec-049-overdub-stop-admits-the-seal-and-accelerates-snapshot-publication)
- [DEC-051](#dec-051-overdub-wrap-seals-at-loop-end-not-session-start)

---

## DEC-057 — LoopSnapshot body format version 1 (LSV1) dual-write

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-09-11 |
| **OpenSpec** | `immutable-loop-snapshot-passes` task **5.2** (approval only). Firmware reader/writer + deferred migration = task **5.3**. |
| **Plan** | [`immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md) |

### Decision

Approve the physical CurrentSet **loop slot body** layout for content-only persistence of the latest complete immutable `LoopSnapshot`, with migration dual-write of the compatibility `PersistedLoopSnapshot` stream.

| Role | Name |
|------|------|
| Human / DEC name | **LoopSnapshot body format version 1** |
| Wire discriminator | **`LSV1`** — 4-byte ASCII, encoded using the existing fixed-token wire convention (`EPT3` / `GEO1` / `OSI1` in [`StorageLoopIo.cpp`](../src/StorageLoopIo.cpp)) |
| Body format version field | **`uint16_t formatVersion = 1`** immediately after `LSV1` |
| Flags | **`uint16_t flags`** after `formatVersion` (bit0 = `hasPersistedLoopSnapshot`; must be 1 during migration dual-write) |

Vocabulary: **format identifier** / **wire discriminator** — not “magic.” Rejected draft tags (not on wire): `LSP1`, `LoopSnap01`, `META`, `PASS`, `SNAP`.

### File shape

```text
[optional EpochFileHeader 10 B]     # unchanged
LSV1 + uint16 formatVersion(=1) + uint16 flags
section "LoopMetadata"
section "PersistedLoopSnapshot"     # dual-write migration material
section "LoopSnapshot"              # sole persisted complete/root snapshot
[SAVE 4 B]                           # unchanged
```

Sections use length-framed names (`uint8 nameLength` + name + `uint32 payloadByteLength` + payload). Unknown names skip by length. Nested `EPT3` / `GEO1` / `OSI1` inside the `PersistedLoopSnapshot` payload stay unchanged. Outer CurrentSet / epoch / `SAVE` / `REVPK02` / `CONTAINER_VERSION` are unchanged.

### Authority and load contract (binds 5.3)

- **`LoopSnapshot` section** is the sole persisted complete snapshot/root and load authority.
- **`PersistedLoopSnapshot` section** is compatibility / migration material only — not authority when an `LSV1` + `formatVersion == 1` body is present.
- **Absence of `LSV1`** ⇒ **legacy** bare `PersistedLoopSnapshot` body; deferred migration path.
- **`LSV1` present and `formatVersion != 1`** ⇒ **unsupported / reject** slot load — never treat as legacy.
- Load rebuilds loop-local handles from logical refs; selects the loaded `LoopSnapshot` as published root; **runtime undo depth = 0**, redo empty. Do not invent `U:` depth from passes.
- Missing or unparseable required `LoopSnapshot` section on a valid `LSV1` + `formatVersion == 1` body ⇒ **load failure** for that slot. **Must not** fall back to `PersistedLoopSnapshot` as authority.
- Failed/partial write retains the previously valid card file.
- On disk: logical ids/refs only; no RAM page packing, runtime addresses, VisualCache, ledgers, cursors, GUS / undo lineage, or `beforePublishedSnapshot` / `afterPublishedSnapshot`.

### What this does not change

- Ownership: `StorageManager` / `StorageLoopIo` / `Loop` remain owners (no new persistence owner).
- State transitions / execution bounds in 5.2 (docs only). 5.3 classifies deferred units in `ExecutionBounds.md`.
- No pre-approval of `formatVersion >= 2` body semantics.
- Dropping the `PersistedLoopSnapshot` section / retiring `LoopPasses` remains §6.

### Related

- [DEC-035](#dec-035-loop-persists-content-only) — content-only persist; undo not persisted
- [DEC-044](#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction) — immutable snapshot direction
- OpenSpec 5.1 — runtime published-identity select on GUS (RAM-only; not this wire format)

---

## DEC-056 — NOTE_EDIT rows enter the existing pending companion set when the published root is stale

**Date:** 2026-09-08

**Status:** Accepted

**Owner:** `Loop::saveNoteEditPass` admits the EditPassId.
`pendingSnapshotPublishBox.companionIds` remains the unpublished-content
list. Stale-root `tryGatherPublishedSnapshotEventsInWindow` and
`rebuildOverdubSourceSpanCache` consume that list.

**Authority:**
[`note_edit_close_stale_snapshot_publish_bugfix.md`](Plans/note_edit_close_stale_snapshot_publish_bugfix.md),
[DEC-045](#dec-045-overdub-snapshot-publish-is-idle-stepped),
[DEC-055](#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot).

**Context:** [`233644`](../captures/session_20260908_233644.log) saved six
NOTE_EDIT rows (`NoteEditPassClosed edits=6`) while `root_rev=1` and
`playback_rev=11`. Those ids were not on the pending companion set.
Stale gather and overdub source (`from=cache`) showed the pre-edit grid.
`tryPublishNoteEditSnapshot` on close uses the published parent; applying
the six rows onto rev 1 would drop later overdub content.

**Decision:**

1. This is unpublished-content admission. It is not a new snapshot-publication
   owner, idle step, or rematerialize.
2. `saveNoteEditPass` is the admission point. If
   `publishedSnapshotIsCurrent()` is already false before
   `playbackRevision++`, append the saved EditPassId to
   `pendingSnapshotPublishBox.companionIds` (create the box if missing; do
   not set `sessionExitQueued`).
3. If the published root is current at save, do not admit. Close keeps
   `tryPublishNoteEditSnapshot` when `openedOnCurrentSnapshot` or the root
   is current.
4. If the root is stale at close, do not publish the edit rows onto that
   parent.
5. `queueSessionExitSnapshot` unions incoming companion ids with ids already
   on the box. It does not replace the list.
6. Overdub-while-NOTE_EDIT session close is a separate transition. Not this
   decision.

**Does not authorize:** hydrate 4b–5, scan-all-Active-editPasses gather,
synchronous construction on close for a stale parent, STOPPED idle
`pending=1` repair, `beginOverdubSession` closing NOTE_EDIT.

**Consequences:** Stale-root consumers see committed NOTE_EDIT rows through
the same companion overlay overdub already uses. Gather applies
`applyNoteEditPassSequence` on snapshot MIDI events. Overdub source
applies the same ids in place on DisplayNotes (pending Hide/Shorten
style); it does not reconstruct the source cache. Later session-exit
publication that applies `companionIds` incorporates them. DEC-045
synchronous NOTE_EDIT close remains for an opened-current or now-current
root.

## DEC-055 — NOTE_EDIT working set is overlay delta on opened LoopSnapshot

**Date:** 2026-09-08

**Status:** Accepted — Stage 3 device PASS
[`221906`](../captures/session_20260908_221906.log)
(analyze fill removed; first-Move `phase,setup` 303 µs). Stage 2 device
PASS [`220445`](../captures/session_20260908_220445.log). Stage 1 device
PASS [`212850`](../captures/session_20260908_212850.log).

**Owner:** `EditManager` (session, `NoteEditCurrentState`, **E:**).
`Loop` owns the published snapshot. `NoteGeometryResolver` owns overlap
Resolution. `DisplayManager` paints.

**Authority:**
[`note_edit_overlay_delta_architecture.md`](Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](Plans/note_edit_overlay_delta_refinement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_hydrate_enhancement.md`](Plans/note_edit_hydrate_enhancement.md),
[DEC-029](#dec-029-noteeditcurrentstate-owns-note-edit-editable-note-state),
[DEC-044](#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction),
[DEC-054](#dec-054--note-edit-playing-display-stall-remainder-units).

**Does not authorize:** hydrate 4b–5, DEC-054 Stage 2, Option B
`clone()` slice, rename `NoteEditCurrentState`, unify live-capture fold,
raise `N`, OpenSpec 4.5 / 4.6, `RuntimeWorkBudget`.

### Problem

DEC-054 relocated kind-boundary undo warm. [`204458`](../captures/session_20260908_204458.log)
still pays 34–60 ms `focus_snap` per settled select because
`NoteEditCurrentState::clone()` copies every row. [`201106`](../captures/session_20260908_201106.log)
was 91–133 ms for 137 rows. The stall’s cause is a second full-loop
representation, not an unsliced copy.

### Decision

1. **North star.** A NOTE_EDIT session never creates a second full-loop
   representation merely to support editing or undo.
2. **Representations.** Opened `LoopSnapshot` is committed content and
   **E:00**. `NoteEditCurrentState` is this-session overlay (mover +
   participating notes). Keep the name (DEC-029). **E:01…** are lossless
   edit-scope deltas plus selection/focus.
3. **Select does not clone.** Select updates selection/focus only. No **E:**
   push, no current-state clone, no full-row warm. Stage 1 is this only.
4. **First kind-boundary at undoCount 0** is a later slice: restore from
   opened snapshot (`hasUndoCurrentState == false`). Do not couple it to
   Stage 1 so the first device capture isolates the select hitch.
5. Later **E:** entries scale with what changed, not with loop size.
   Live-capture fold stays its existing path.
6. Overlap/select query shapes stay the 2026-08-17 sibling amendment.
   Do not map `currentTick` → `selectedTick`. Do not reuse
   `ActiveNoteLedger` at the bracket.
7. **Reject DEC-054 Option B.** Do not slice `clone()`. Remove the full
   copy from the working set.
8. Consume already-prepared snapshot/index on open and fader. Miss =
   legacy only. No new Manager.

### Rationale

DEC-054 identified and relocated the stall. DEC-055 removes its cause.
Undo storage and analyze cost must scale with edit scope, not note count.
`applySessionUndoEntry` already has the no-current-state snapshot copy
path; later slices change which representation the first kind-boundary
is allowed to publish.

### Alternatives considered

| Alternative | Rejected because |
|---|---|
| DEC-054 Option B (slice `clone()`) | Still O(loop size); elaborate machinery |
| Couple E:00 first-kind-boundary into Stage 1 | Two correctness changes; capture cannot isolate |
| `ActiveNoteLedger` at `selectedTick` | Playhead runtime; DEC-041/042 |
| Rename `NoteEditCurrentState` | Churn; DEC-029 already names the overlay owner |
| Unify live-capture fold into E:00 | Those notes are not in the opened snapshot |

### Affected modules

`EditManager` select / session undo, `NoteEditCurrentState` row scope
(later slices), opened-snapshot restore, hydrate consume stages.

### Constraints created

- Working-set invariant (architecture doc) is the regression bar.
- Stage 1 firmware must not clone or warm on select.
- Stage 2+ must prove lossless E:00 + overlay-delta undo before claiming
  the clone is gone from kind-boundary.
- Option B is not an authorized fallback.

### Related OpenSpec

[`note-edit-session-undo`](../../openspec/specs/note-edit-session-undo/spec.md)
(delta-sized entries). Not an `immutable-loop-snapshot-passes` 4.4/4.5/4.6
slice.

### Migration notes

No SD schema. First Move after Stage 1 still cache-miss clones until the
E:00 kind-boundary slice.

---

## DEC-054 — NOTE_EDIT playing display stall remainder units

**Date:** 2026-09-08

**Status:** Accepted — Stage 1 admission firmware in tree. Device
[`204458`](../captures/session_20260908_204458.log) failed the 30 ms
cadence during back-and-forth select. **Option B (`clone()` slice)
rejected** — [DEC-055](#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot)
removes the full copy. Stage 2 (deferred open/close) stays separate.

**Owner:** `EditManager` (pending state, execution, publication).
`ControlSurfaceManager` only requests / marks pending. `DisplayManager`
paints the last composed frame.

**Authority:**
[`note_edit_display_playing_stall_architecture.md`](Plans/note_edit_display_playing_stall_architecture.md),
[`ExecutionBounds.md`](Authority/Architecture/ExecutionBounds.md),
[DEC-043](#dec-043-execution-bound-class-is-a-gated-architectural-property).

**Does not authorize:** hydrate Stage 4a, raise snapshot idle `N`,
`T_RT_reserve` / DEC-053 yield, OpenSpec 4.5 / 4.6, interval reservation,
`RuntimeWorkBudget::admit`. Slicing `NoteEditCurrentState::clone()`
(Option B) is **rejected** by DEC-055.

### Problem

[`201106`](../captures/session_20260908_201106.log) display frames are
18–25 ms. Select-fader `UNDO_WARM` is 91.7–133.2 ms (`focus_snap` includes
`NoteEditCurrentState::clone()` / `assignFrom` for 137 rows). Twelve stacked
warms produce a 7.37 s `DFRAME` gap. `openNoteEditSession` is 82 ms on the
button turn. This is admission/ownership, not OLED performance.

### Decision

1. Do not make `DisplayManager` faster. Remove content-sized `EditManager`
   work from the display-admission turn.
2. No new Manager and no `RuntimeWorkBudget`.
3. Coalesce select kind-boundary undo warm. Reuse
   `kDeferredNoteEditPlaybackRefreshIdleMs` (80 ms). Process after that
   turn’s display admission.
4. **Stage 1 bound gate.** Relocating `processKindBoundaryUndoWarm` after
   display does not make it a bounded unit. The Stage 1 commit must
   register the existing whole-warm as an empirically bounded remainder
   unit with a measured worst case (`201106` 91.7–133.2 ms) and this
   overrun policy: remain pending; do not publish partial undo state.
   If Stage 1 device capture shows that bound is unacceptable for the
   30 ms display cadence, do **not** slice `clone()` — DEC-055 removes
   the full copy from select / the working set.
5. Stage 2 (after Stage 1 capture): queue NOTE_EDIT open/close. Old
   session remains authoritative until the unit commits, then
   `EditSession.active` and LOOP_EDIT↔NOTE_EDIT flip together.
   `EditSession.active = false` while bake is pending is forbidden.
   Immediate UI/session flip is rejected.
6. Playing move/length/pitch stay on `PlayingEditGeometryDefer`.

### Rationale

Coalesce first: twelve 90–133 ms warms are the multi-second stall.
A single whole-warm after settle still needs an inventory class and
overrun policy (DEC-043). Close must not drop the authoritative session
before bake commits.

### Alternatives considered

| Alternative | Rejected because |
|---|---|
| Faster display interval / forced NOTE_EDIT paints | `201106` paint is already 18–25 ms; cannot preempt warm |
| Treat post-display whole-warm as bounded without a gate | Relocates a 91–133 ms unit; does not classify it |
| Slice `clone()` (Option B) | Rejected 2026-09-08 — DEC-055 removes the full copy |
| Optimistic session flip | Half-open session on faders |
| `RuntimeWorkBudget` / interval reservation | DEC-043 / scheduling contract |

### Affected modules

`EditManager`, `ControlSurfaceManager` (pending only), `main.cpp` display
admission order, `ExecutionBounds.md`.

### Constraints created

- Stage 1 firmware must include the inventory row and overrun policy.
- Stage 2 waits as a separate admission change. Option B is not authorized.
- Close may not clear `EditSession.active` before bake commits.

### Related OpenSpec

None. Not an `immutable-loop-snapshot-passes` 4.4/4.5/4.6 slice.

### Migration notes

None. No SD schema. Undo entry shape unchanged.

## DEC-053 — T_RT_reserve is ceiling plus per-turn MIDI boundary

**Date:** 2026-09-08

**Status:** Accepted — policy for OpenSpec tasks `3.5`/`3.6`. Remaining-interval
yield device proof is **parked** (no testable live yield on firmware;
[`115043`](../captures/session_20260908_115043.log) `remaining=0` starved
construction until STOPPED). `kSnapshotIdleReserveYieldEnabled` is false;
`allowSnapshotIdleReserve` and `DIAG,snapshot_idle,yield` stay compiled.
Tasks `3.5`/`3.6` are closed on wrap-journal publication plus
[`101815`](../captures/session_20260908_101815.log) /
[`104353`](../captures/session_20260908_104353.log). `T_RT_safety_margin`
stays unfrozen.

**Scope:** `feature/overdub-wrap-snapshot-publication` / OpenSpec
`immutable-loop-snapshot-passes` task `3.6`

**Authority:** [`ExecutionBounds.md`](Authority/Architecture/ExecutionBounds.md),
[`NAMING.md`](Authority/NAMING.md) § Timing-critical MIDI Input,
[`overdub_wrap_source_snapshot_migration_refinement.md`](Plans/overdub_wrap_source_snapshot_migration_refinement.md),
[`snapshot_publication_timing_bugfix.md`](Plans/snapshot_publication_timing_bugfix.md).

**Amends:** [DEC-047](#dec-047--bounded-overdub-wrap-snapshot-publication) item 9
(how the `T_RT_reserve` device gate is defined). Does not supersede DEC-047
wrap-journal publication, DEC-043 bound classification, or the rejection of
`RuntimeWorkBudget::admit(WorkClass)`.

**Does not freeze:** an observed construction-unit duration (including 8.86 ms),
the minimum observed `reserve_us`, `T_RT_safety_margin`, or OpenSpec `3.5`/`3.6`.

### Decision

`T_RT_reserve` is the maximum time a snapshot-idle construction turn may consume
before the next timing-critical MIDI Input entry, measured from that idle
turn’s start. The deadline is the actual next `handleMidiInput()` entry, not
the next MIDI-clock timestamp and not `60% × T_clock`.

Three numbers:

1. **`T_RT_reserve_ceiling`** — fixed policy upper bound on
   construction-attributable work per snapshot-idle turn. Initial value:
   **10,000 µs**. Independent of BPM. Not justified by an observed unit. Not
   replaced by the minimum observed reserve.
2. **`reserve_us`** — per-turn measured availability: snapshot-idle start to
   the next timing-critical `handleMidiInput()` entry. Capture-gated field on
   the existing `DIAG,snapshot_idle,child` line. When that enter has not been
   seen yet, omit the field or emit `0` and pair on the following MIDI Input
   enter sample. Do not add a second telemetry owner. `DIAG,midi_gap` remains
   a five-second maximum since the previous input **exit** and is not this
   value.
3. **`T_RT_safety_margin`** — explicit margin for timestamp uncertainty,
   MIDI-entry/dispatch overhead, and uninstrumented work. **Not frozen.**
   1,000 µs is a working hypothesis only.

Admitted construction interval:

```text
min(T_RT_reserve_ceiling, reserve_us - T_RT_safety_margin)
```

with a floor of zero. A turn whose next MIDI boundary is 7.2 ms away does not
receive 10 ms.

Admission at turn start cannot wait for this turn’s retrospective `reserve_us`.
Remaining time is the interval until the next already-knowable timing-critical
MIDI Input opportunity. Recapture scores the **actual** next enter. A local
elapsed-time timer is not the bound. A Snapshot idle turn admits two
construction units (`kSnapshotIdleConstructionUnitsPerTurn` = 2). The turn
starts only when the classified worst-case for those two units fits the
admitted interval; otherwise the turn yields and retries. No incomplete
candidate is published or selected.

**STOPPED is excluded.** `T_RT_reserve`, remaining-interval yield, `reserve_us`
ceiling, construction-attributable `late_*`, and construction-attributable MIDI
Input Gap apply only while the track is `RECORDING`, `PLAYING`, or
`OVERDUBBING` (and `ARMED` / `STOPPED_RECORDING` on the way into those). While
the track is `STOPPED`, Snapshot idle, persist, and other deferred work may
take as long as they need. Those samples are not an overrun and must not be
scored against the 10,000 µs ceiling.

`60% × T_clock` (12.5 ms at 120 BPM) stays an operating/diagnostic proxy only.
The construction budget is not a function of BPM. Measured `reserve_us` is
the adaptation mechanism when the MIDI boundary moves.

### Acceptance (3.6 closed 2026-09-08; yield device proof parked)

Pass requires a capture-gated recapture with `SESSION_CAPTURE_SNAPSHOT_IDLE=1`
covering record and overdub through `pending=0` **while timing-critical**.
STOPPED-hold samples are excluded from the ceiling and lateness gates:

- `reserve_us` varies with actual MIDI boundaries
- construction-attributable work per turn stays ≤ `T_RT_reserve_ceiling`
- no construction-attributable `late_clk` / `late_on` / `late_off`
- construction-attributable MIDI Input Gap stays inside this contract
- the ~79 ms parent/child class is either non-construction or a gate failure
- insufficient-reserve turns yield, retry, and then publish only a complete
  candidate; the previous complete root stays authoritative
- `pending=0` converges without prefix publication

The `snapshot_idle` parent wall time is not an acceptance metric.

**Amendment (2026-09-08):** Snapshot idle cardinality is **N = 2**, not 1.
[`104353`](../captures/session_20260908_104353.log) construction stayed well
under the 10,000 µs ceiling with `stepPendingSnapshotPublish(2)` already in
firmware. Do not revert `kSnapshotIdleConstructionUnitsPerTurn` to 1 for the
3.6 gate. Remaining-interval admission compares the two-unit worst-case to
`min(T_RT_reserve_ceiling, remaining − T_RT_safety_margin)`.

**Amendment (2026-09-08):** STOPPED timings are excluded from the 3.6 gate.
[`110034`](../captures/session_20260908_110034.log) `reserve_us` 36 / 42 /
95 ms after `PLAYING → STOPPED` are not overruns. Remaining-interval admission
does not yield on `STOPPED`.

**Amendment (2026-09-08):** Device remaining-interval yield proof is parked.
No testable live yield exists on firmware
([`115043`](../captures/session_20260908_115043.log) `remaining=0` until
STOPPED). `kSnapshotIdleReserveYieldEnabled` is false. Yield helpers and
`DIAG,snapshot_idle,yield` stay compiled. `3.5`/`3.6` close on wrap-journal
publication plus [`101815`](../captures/session_20260908_101815.log) /
[`104353`](../captures/session_20260908_104353.log). Re-enable the reject
only when remaining-interval admission can admit live construction and still
reject a constrained turn.

**Consequences:** add capture-gated `reserve_us` on the existing child line;
extend the existing Snapshot idle owner for remaining-interval admission; do
not introduce `RuntimeWorkBudget::admit`.

## DEC-052 — Occupy consumes the draw-source predicate

**Date:** 2026-09-02  
**Status:** Accepted  
**Owner:** occupy source-span cache — `Loop::rebuildOverdubSourceSpanCache`  
**Plan:** [`overdub_occupy_snapshot_source_refinement.md`](Plans/overdub_occupy_snapshot_source_refinement.md)  
**Evidence:** [`134815`](../captures/session_20260902_134815.log) paint `snap=1` vs occupy `from=visual`  
**Does not supersede:** [DEC-050](#dec-050--occupancy-at-noteon-and-per-bar-draw-routing) occupancy-at-NoteOn or per-bar paint routing

### Problem

Paint and occupy independently selected committed sources. `gatherDrawSourceBars` uses `shouldGatherPublishedSnapshotForDrawSource`. `rebuildOverdubSourceSpanCache` ignored that predicate and filled from prepared LCR, prior cache, covering `visualCache`, or pass reconstruction. [`134815`](../captures/session_20260902_134815.log) OD2 open: paint `snap=1`, occupy `from=visual,notes=19`.

### Decision

1. **Single predicate.** Occupy evaluates the same `shouldGatherPublishedSnapshotForDrawSource` call as paint (`publishedSnapshot()`, `captureActive()`, `hasOverdubSession()`, visual nonempty/dirty, `publishedSnapshotIsCurrent()`). No occupy-specific approximation.
2. **Snapshot exclusivity.** When the predicate is true, committed occupy membership and geometry come only from the published `LoopSnapshot` (unique by `noteId` across every bar). `visualCache`, prepared LCR, prior cache, and reconstructed passes do not participate in that fill.
3. **Fallback preservation.** When the predicate is false, the existing prepared → prior cache → covering visual → pass reconstruction order is unchanged.
4. **Stage boundary.** This is committed-source selection consistency only. Snapshot publication, visual-cache population, `#41` Stage 2, LCR scheduling, and `resolveOccupancyAtNoteOn` stay out of this decision.

### Constraints created

- A current published snapshot wins over prepared LCR. [`113651`](../captures/session_20260902_113651.log) `from=prepared,notes=8` is leftover LCR, not occupy-paint authority.
- `from=snapshot` is not a pass by itself. Occupy geometry must change when the snapshot changes even if `visualCache` is stale or unchanged.
- Do not add `Loop::displayNoteFromSnapshotNote()` or a DisplayManager occupy API. Paint and occupy share one snapshot→`DisplayNote` conversion in a neutral utility.

---

## DEC-051 — Overdub wrap seals at loop end, not session start

**Date:** 2026-09-01  
**Status:** Accepted  
**Owner:** overdub wrap persist — `Loop::shouldCommitOverdubWrap` / `Track::commitOverdubWrap`  
**Plan:** [`overdub_wrap_loop_end_commit_refinement.md`](Plans/overdub_wrap_loop_end_commit_refinement.md); evidence [`overdub_wrap_bar4_note_off_session_seal_bugfix.md`](Plans/overdub_wrap_bar4_note_off_session_seal_bugfix.md)  
**Amends:** [DEC-038](#dec-038-overdub-wrap-commit-and-session-undo) wrap-persist sentence 1 only  
**Does not supersede:** DEC-038 session identity **S**, session-gated **O:** undo, one **U:** after stop, empty-wrap no-op, no `finalizePendingNotes` at wrap, 6E.1b consume rotation proof

### Problem

DEC-038 sealed when the playhead returned to **S**. When S is mid-loop, one wrap pass spanned `[S, L) ∪ [0, S)`. Reconstruct then paired tail ons with unrelated head offs (`2304→96` in [`193232`](../captures/session_20260901_193232.log)). 6E.1b proved consume rotation around S, not that the seal clock must be S.

### Decision

1. **Wrap persist clock.** When the playhead crosses **loop end (phase 0)**, seal the current wrap as an `OverdubPass` + DEC-031 companions, push a session-stack entry, `beginCapture(Overdub, S)` again, stay OVERDUBBING. `sealedAtTick = 0`. First wrap is the next loop end after leaving S; later wraps every loop end. **S** (`playheadPhaseTick`) remains session identity: source view, occupy present-at-hold, and wrap `beginCapture` still use S. Do not add a second session-start noun.
2. **Undo suppress.** `consumeSuppressedOverdubWrapCrossing` consumes the next **0** crossing, not S.
3. Unchanged DEC-038 sentences: still an `OverdubPass`; no `finalizePendingNotes` at wrap; empty wrap is a no-op; undo while OVERDUBBING is session-gated **O:**; after stop one **U:** `OverdubPassAdded`; crash mid-session out of scope.

### Constraints created

- Do not rebuild source view at 0 on wrap `beginCapture`.
- Do not invent an Off at 0 for notes held across the musical wrap — extract and re-append only.
- Wrap-shaped consume uses the musical loop `[start, L) ∪ [0, E)`, not S.

---

## DEC-050 — Occupancy at NoteOn and per-bar draw routing

**Date:** 2026-09-01

**Status:** Accepted

**Scope:** `feature/overdub-wrap-snapshot-publication`

**Authority:** snapshot-first bar painter plan (design-session artifact).

**Decision:**

1. Occupancy resolution is an authoritative Loop-state transition initiated at
   NoteOn. Display drawing consumes the resulting effective state and does not
   determine musical occupancy semantics.
2. Draw source selection is per-bar routing only; it does not arbitrate between
   competing note representations.
3. Occupancy is represented as a pending effective transform over committed
   geometry, never as ledger state. All candidates have final `startTick` and
   `endTick`; the ledger supplies identity only.
4. Occupancy transforms compose monotonically per identity. No resolution may
   make a note more audible than a previous resolution left it.

**Consequences:**

- `resolveOccupancyAtNoteOn` writes Shorten/Hide into `pendingNoteChanges_`.
  `pendingPaintHides_` is removed.
- `upsertSourceTransform` is monotonically restrictive: Hide absorbs Shorten;
  Shorten takes `min` end tick; sub-min-length degrades to Hide.
- NoteOff skips NoteOn-resolved identities and only resolves notes that start
  strictly inside the incoming hold.
- Draw path (Stage B) selects Capture / Snapshot / VisualCache per bar.

## DEC-049 — Overdub stop admits the seal and accelerates snapshot publication

**Date:** 2026-08-31

**Status:** Accepted

**Scope:** `feature/overdub-wrap-snapshot-publication`

**Authority:** [`overdub_stop_incremental_admission_bugfix.md`](Plans/overdub_stop_incremental_admission_bugfix.md),
[`DEC-047`](#dec-047--bounded-overdub-wrap-snapshot-publication).

**Amends:** `DEC-047` audible-admission consequence only. Publication owner
remains `Loop::stepPendingSnapshotPublish`. The `DEC-047` OVERDUBBING
construction-start rule is unchanged.

**Decision:**

1. Stopping overdubbing (`Track::stopOverdubbing`) reanchors playback
   traversal and does not invalidate a still-valid full-loop merged
   representation solely because overdubbing ended.
2. The next `playCommittedLoopMidi` may incrementally admit the one-shot
   overdub-seal receipt into that preserved stream. Any mismatch keeps the
   existing full-gather fallback.
3. `playback_build` labels the branch actually taken: `snapshot` (current
   root membership), `overlay` (stale-root overlay), or `compatibility`
   only when `gatherCommittedEventsForDerivedView` runs.
4. While a publication backlog is present (published root revision lags
   `playbackRevision` and `hasPendingSnapshotPublish()`), deferred idle
   admits Snapshot on 2 of every 6 turns. Every other deferred owner still
   receives 1 of 6. The per-unit bound stays `stepPendingSnapshotPublish(1)`.
5. The ratio does not depend on backlog size, pass count, loop length,
   publication cost, queue depth, or elapsed CPU.

**Consequences:** Audible playback after an eligible overdub stop no longer
requires compatibility gather until idle finishes the successor root.
Stale-root overlay remains available for ineligible rebuilds and shrinks
as the published root catches `playbackRevision`.


## DEC-048 — Snapshot nodes pack into 32 KB slabs

**Date:** 2026-08-27

**Status:** Accepted

**Scope:** `feature/immutable-loop-snapshot-passes`

**Authority:** [`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/),
[`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md).

**Decision:**

1. `Loop` owns immutable snapshot object allocation. `allocateSnapshotShared`
   bump-allocates into 32 KB slabs (`kSnapshotSlabBytes`).
2. A slab is one `extmem_malloc` on device. Native builds use one 32 KB
   `malloc` because PSRAM is unavailable. Do not spray per-node `malloc`s
   when a slab is denied.
3. Each slab holds a live-object count and is freed when the count reaches 0.
   Copy-on-write sharing stays pointer-based; unchanged membership may live
   on an older slab.
4. One mutable `SnapshotSpanRoot` and one mutable `SnapshotEventRoot` are
   reused for the life of a `LoopSnapshotConstruction`.
5. If a new slab cannot be allocated, construction `failed_` stays as today:
   discard the candidate and keep the previous published snapshot.
6. This is not a general arena under `ExternalMemoryFirstAllocator`. Directory
   `pages` vectors and publish scratch stay on their existing allocators.

**Consequences:** smalloc block count for snapshot nodes grows with payload
slabs, not with note count × node type. Capture-serial emits
`DIAG,snapshot_alloc,nodes,bytes,slabs` on `freeze()`.

## DEC-047 — Bounded overdub-wrap snapshot publication

**Date:** 2026-08-27

**Status:** Accepted — Phase 3 tasks `3.5`–`3.6`; production firmware remains
gated by the phase architecture and device verification gates.

**Scope:** `feature/immutable-loop-snapshot-passes`

**Authority:** [`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/),
[`overdub_wrap_source_snapshot_migration_refinement.md`](Plans/overdub_wrap_source_snapshot_migration_refinement.md),
and [`ExecutionBounds.md`](Authority/Architecture/ExecutionBounds.md).

**Supersedes:** `DEC-045` only for the activated Phase 3 wrap-publication
slice. `DEC-045` remains historical evidence for the prior stop-queued,
post-root successor policy outside this slice.

**Decision:**

1. A settled overdub wrap queues an explicit transient mutation journal through
   `Loop`; `Track::commitOverdubWrapAtSessionStart` does not construct or
   publish a snapshot synchronously.
2. Capture/pairing state assigns paired NoteOn/NoteOff event identities before
   pass promotion. Promotion does not scan capture content for event identity.
   These identities are runtime-only until the later persistence phase.
3. The journal has structural caps for changes, affected bars, membership
   entries, and bytes. Oversized wraps are represented as ordered bounded
   fragments. Fragments retain the exact parent identity and never publish an
   incomplete candidate.
4. Snapshot construction uses fixed-capacity immutable membership pages and
   persistent bar-cursor progress. It does not enumerate all changed bars in
   `begin()` or copy an unbounded membership vector in one unit.
5. Ordered successor construction may step during `PLAYING` and `OVERDUBBING`
   after the first root exists under this phase’s replacement policy. The old
   complete root remains selected until a complete successor is frozen and
   published.
6. A failed or over-budget fragment remains queued for retry. It is not
   discarded, and the settled compatibility mutation is not silently lost.
7. The event index is effective content for note mutations: Hide removes paired
   note events, while geometry replacement creates new immutable event objects
   with the same logical `EventId` and canonical order.
8. The global overdub undo entry remains pass-based while construction is
   pending. Snapshot-identity undo attachment belongs to the later undo phase.
9. The device gate uses the `T_RT_reserve` contract for construction-
   attributable MIDI gap and deadline lateness. Total construction wall time
   alone is insufficient. **Amended 2026-09-08:** definition, ceiling, and
   acceptance are [DEC-053](#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary).

**Consequences:** OpenSpec tasks `3.5` and `3.6` require updates to the phase
gate, runtime scope, construction inventory, and parity fixtures before
implementation. Direct consumer migration, persistence, and snapshot-identity
undo remain later tasks.

**Amendment (2026-08-27):** “Paired event identity” in items 2 and 7 means
the stable `NoteId` shared by a logical NoteOn/NoteOff pair. Note-event
replacement and removal are keyed by that `NoteId`. `EventId` remains only for
non-note event objects. A provisional `O:` snapshot is private and unselected;
the prior complete root remains active until atomic publication.

**Amendment (2026-09-16) — membership design envelope and fragment transaction.**
Two defects were reproduced from the device record/overdub pair preserved as
`test_dense_device_capture_snapshot_preserves_every_committed_row`
(`task23-dense-snapshot-cc-playback`, 734 committed effective rows, bar 2 = 260
event references). Construction failed at the fixed 16-page
(256-reference) per-bar ceiling, and the failed fragment was then dropped
because pass-change progress had already advanced. This amendment fixes the
representation contract; it does not change item 4's bounded-unit rule.

1. **Bar membership capacity is an explicit design-envelope bound.** The
   representable reference count per bar is expressed as
   `kSnapshotMembershipMaxRefsPerBar` and derived page count, not as an
   accidental side effect of a page constant. The same envelope applies to
   event-bar membership and note-bar membership. Membership pages remain
   lazily allocated; raising the envelope preallocates nothing.
2. **Valid committed content must be representable.** Exceeding the bound must
   never result in publication of a partial candidate. The envelope is sized
   to the documented design envelope with measured headroom over the observed
   dense fixture, not to the whole-store theoretical maximum.
3. **Fragment incorporation is transactional.** Pass-change traversal and
   progress (`batch`, `batchIndex`, cursor, open notes, next `EventId` /
   canonical order) become authoritative only after the corresponding fragment
   has been successfully incorporated into the candidate. Failed incorporation
   leaves both the selected published root and the authoritative pass-change
   progress unchanged. Any partially constructed candidate is discarded before
   retry, so a retried fragment cannot double-apply content.
4. **Failure classes are distinguished for diagnostics.** A transient or
   budget failure leaves the exact fragment pending for retry. A persistent
   representability or invariant failure keeps the previous root selected,
   blocks publication, and emits a diagnostic; it must not become a silent
   infinite reconstruction loop.

Item 6 is reaffirmed: a failed fragment is retained for retry, never
discarded, and the settled compatibility mutation is never silently lost.

## DEC-046 — Round-robin admission over playback-relevant destinations; representation coverage is a prerequisite

**Date:** 2026-08-26
**Status:** Accepted — architecture direction. Firmware Slices 1–4 **not** authorized by this entry.
**Scope:** `feature/immutable-loop-snapshot-passes`
**Authority:** [`ExecutionBounds.md`](Authority/Architecture/ExecutionBounds.md) rows 1, 2 and 7,
[`RuntimeArchitecture.md`](Authority/Architecture/RuntimeArchitecture.md),
[`IntervalProjection.md`](Authority/Architecture/IntervalProjection.md),
[`playback_window_round_robin_admission_refinement.md`](Plans/playback_window_round_robin_admission_refinement.md)

**Context:** [`session_20260826_141838`](../captures/session_20260826_141838.log) showed
`TrackManager::updateAllTracks` at 344 ms with `midi_gap` 26 ms, and overdubbed notes vanishing from
the display at wrap. Four distinct problems were being conflated. They are now separated:

| RC | Question | Owner |
|---|---|---|
| RC1 | Why are multiple content-sized rebuilds admitted into one clock dispatch? | Ring A admission |
| RC2 | Why is a nonempty but incomplete derived representation treated as authoritative? | Invariant R |
| RC3 | Why does snapshot construction consume CPU without converging? | Separate plan |
| RC4 | Why does one rebuild cost hundreds of milliseconds? | Separate change — [investigation](Plans/playback_window_edit_projection_unit_cost_investigation.md) |

**Decision:**

1. **Ring A admits one eligible content-sized playback-window rebuild per clock dispatch.** Its
   population is the **dynamic set of playback-relevant destinations that need preparation**, not all
   enabled slots. Admission does **not** reclassify the rebuild: it remains **Unbounded** in
   `ExecutionBounds.md`, and no fourth bound class is introduced.
2. **`TRACK_RECORDING` counts as playback-engine-running for other enabled slots.** Layered-slot
   playback during recording is intentional, so those slots retain playback-window obligation. The
   recorded slot itself is excluded by lack of committed content, not by state.
3. **Invariant R — representation coverage is a prerequisite, not a freshness hint.** A derived
   representation may satisfy a consumer query only when its coverage corresponds to the requested
   interval at the requested revision. This is what the entry records; the three enforcement points
   (display must not paint uncovered cache data, wrap append must eventually produce coverage,
   overlap resolution must not seed from uncovered cache data) follow from it.
4. **Invariant A1 and its failure policy are separate statements.** A1: within the design envelope a
   playback-relevant destination has its replacement ready before its current window expires. Policy
   on violation: **serve the old immutable window and raise a visible starvation diagnostic. No
   synchronous rescue rebuild on the clock path** — that path is RC1 itself.
5. **Invariant A2 — bounded waiting of persistent eligible members.** No playback-relevant
   destination that needs a refresh may be indefinitely displaced. Verification must prove
   **dynamic-set semantics**, not cursor arithmetic: members enter on eligibility, leave after
   successful preparation, the cursor keeps advancing, newly eligible members cannot reset the
   rotation, and a persistently eligible member eventually gets a turn.
6. **Ring B rotates idle-path owners**, replacing the three pairwise gates. Owner-3
   (`applyDeferredVisualCacheWrapAppend`) has an **eligibility** rule, not a priority: its cursor
   position is consumed even when the owner is skipped.
7. **Prefetch lead is derived, not chosen.** `(N-1) x T_clock` is a **conservative
   continuously-due bound**, not a steady-state prediction. Gate **G1** sizes the lead for the
   representative envelope of **16 tracks x 2 slots x 240 BPM**. Extreme capacity (64 slots, the full
   `[20, 300]` clamp) is a documented capacity limit, not a blocking gate.
8. **G1 consumes RC4's post-resolution cost.** [`160244`](../captures/session_20260826_160244.log)
   proved unit cost is environment-dependent, not input-dependent: five byte-identical input
   signatures cost **7.3x–9.7x** more under contention, so `cer_range` splits into an uncontended
   band of 29.5–98.9 ms and a contended band of 306–363 ms. **The 367 ms observation is pathological
   and must not size the prefetch lead.** RC4 may be investigated now; its firmware fix is a separate
   change and may not enter the Ring A implementation.
9. **Sequencing:** Slice 1 slot attribution → RC4 investigation and resolution measurement → G1
   sizing → Ring B / RC2 → Ring A.

**Consequences:** Ring A's expected benefit is restated honestly. In `160244` a single 362.9 ms
rebuild accounts for nearly all of the 391.1 ms `tracks` maximum, and observed multiplicity is
**pairs** (median `cer_range` spacing 1998 ms; only 8 gaps under 50 ms, all 11.9–16.8 ms). Ring A is
worth roughly 2x there while RC4 is roughly 9x. Ring A remains architecturally correct — it bounds an
admission multiplier that nothing bounds today — but it is **not** the fix for the observed stall.

**Open, not blocking:** window **bias** (backward margin for LoopEnd launch and clock catch-up) is to
be measured rather than guessed. It affects the eventual value of `H_fwd` and therefore G1, and does
not reopen the admission design.

## DEC-045 — Overdub snapshot publish is idle-stepped

**Date:** 2026-08-25
**Status:** Accepted
**Scope:** `feature/immutable-loop-snapshot-passes`
**Authority:** [`ExecutionBounds.md`](Authority/Architecture/ExecutionBounds.md) `LoopSnapshotConstruction::step` and session-exit publish,
[`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/)

**Context:** After Phase 3 playback began reading the published `LoopSnapshot`,
overdub stop ran `tryPublishSessionExitSnapshot` on the button path.
[`session_20260825_214529.log`](../captures/session_20260825_214529.log) spent
11.67 s then 2.37 s in `seal_fx_push_overdub_undo` (undo push + full candidate
construction). MIDI and display were blocked until that call returned. Playback
already treats a published root behind `playbackRevision` as stale and uses
compatibility gather.

**Decision:**

1. Overdub session-exit copies pass ids and companions in
   `Loop::queueSessionExitSnapshot` on the stop path. It does not
   `step(totalChangeCount)`.
2. `Track::processDeferredIdleMaintenance` calls
   `Loop::stepPendingSnapshotPublish(1)` before visual-cache slices. Idle runs
   while PLAYING / OVERDUBBING / STOPPED_RECORDING and when STOPPED tracks are
   serviced. That is the interval-path owner; the unit remains one
   `LoopSnapshotConstruction::step`, not a local timer.
3. During OVERDUBBING idle, only an already in-flight
   `LoopSnapshotConstruction` may `step(1)`. Do not
   `startNextPendingConstruction` or `enqueueLiveOverdubPasses` on that turn.
   PLAYING / STOPPED_RECORDING / STOPPED may start the next pass change-set.
   `storePublishedSnapshot` waits until session-exit is queued and the
   candidate is complete.
4. Record and overdub session-exit queue at stop. NOTE_EDIT close stays
   synchronous. Native tests keep `tryPublishSessionExitSnapshot` as
   queue-then-drain.
5. Failure discards the candidate and keeps the previous published root. Do not
   publish a prefix.

**Consequences:** Audible playback after overdub stop uses compatibility gather
until idle finishes the successor root. Stop no longer completes the candidate
before returning to MIDI service.

**Amendment (2026-08-25):** The idle unit is one dirty bar of the in-flight
change, not one settled note. Remaining bars are applied on later idle turns,
ordered by soonest playhead arrival from `playheadPhaseTick`. Starting the next
pass change-set is unchanged.

**Amendment (2026-08-25):** Playback no longer walks every committed chunk list
while a published root exists but is behind `playbackRevision`.
`tryGatherPublishedSnapshotEventsInWindow` reads that root's window and
overlays unpublished session / pending-publish passes plus their companions.
`queueSessionExitSnapshot` copies wrap companions with the stop companions.
Compatibility gather remains only when no published root exists.
[`225025`](../captures/session_20260825_225025.log) `cer_win` `lists=18`.

**Amendment (2026-08-25):** OVERDUBBING idle does not run visual-cache
reconstruct. When snapshot step has no in-flight construction, that turn ends.
[`230556`](../captures/session_20260825_230556.log) `idle_reconstruct`
241–278 ms. Visual slices resume on PLAYING / STOPPED_RECORDING.

**Amendment (2026-09-01):** Stage 3 one-phase `load_frame` left remainder
for VisualCache idle during OVERDUBBING. [`233642`](../captures/session_20260901_233642.log)
OD2 reconstructed mid-pass (`OverlayRejected` / `slice_clean` 102→99) and
only `adopt_partial` 146 at stop. Stage 2 [`232353`](../captures/session_20260901_232353.log)
held visual 125 until stop. Gate is
`MidiServiceDrain::allowVisualCacheReconstructSlice`. Overdub source-view
idle is unchanged.

**Amendment (2026-08-25):** PLAYING / STOPPED_RECORDING idle gives an empty
dirty visual cache the turn before `stepPendingSnapshotPublish`. Snapshot
publish after record stop must not starve first paint.
[`231632`](../captures/session_20260825_231632.log) RECORDING 653 events,
first PLAYING 288 notes, `visualCache` notes=0.

**Amendment (2026-08-25):** Once a published root exists, PLAYING and
OVERDUBBING idle do not `step` successor construction, including an
in-flight candidate. PLAYING visual slices also stay off unless the visual
cache is empty. Successor publish resumes on STOPPED / STOPPED_RECORDING.
Playback keeps the stale-window overlay.
[`232329`](../captures/session_20260825_232329.log) one `pending=0` at 80.27 s
during OD1; 3631 later `pending=1` steps; OD2 BPM 57; OD4 BPM 46.
Device PASS [`233308`](../captures/session_20260825_233308.log): no
`snapshot_idle` after first publish until STOPPED; PLAY4 BPM 119.966–120.049.

**Amendment (2026-08-26):** Persist and first-record snapshot stay on PLAYING
and OVERDUBBING idle, but not on the same main-loop turn
(`MidiServiceDrain` post-stop lane). [`010505`](../captures/session_20260826_010505.log)
stacked `LoopPersist` `done=46697` with `snapshot_idle` on first OD
(`midi_gap` 32–51 ms).

**Amendment (2026-08-26):** First-record uses the overdub live-enqueue setup.
`stepPendingSnapshotPublish` starts construction from `capture.store` during
RECORDING after the playhead has left bar 1, and starts on PLAYING
(`allowStartConstruction = !isOverdubbing()`). OVERDUBBING only advances an
in-flight candidate. After a root exists, PLAYING and OVERDUBBING still do
not step successor construction. First-take `loopLengthTicks` updates during
RECORDING with `RecordStopLength::computeRecordStopLengthTicks` (same bar
quantize as `prepareRecordStop`) so idle can start after the playhead has
left bar 1. Growing record does not clamp capture ticks to that working
length. Live RECORDING construction builds only that completed first bar
and does not restart when later `loopLengthTicks` steps grow. Session-exit
discards a first-bar unpublished candidate when the sealed length differs.
No µs budget. Off-PLAYING first-record was withdrawn.
[`095720`](../captures/session_20260826_095720.log) had no `snapshot_idle`
until PLAYING because length stayed 0 for the whole RECORDING window.
[`114012`](../captures/session_20260826_114012.log) started `snapshot_idle`
at `record_length=768` while bar 1 was still being written, then rebuilt
on every later length step.

**Amendment (2026-09-12):** Record session-exit retains live-record work when
`appliedLiveRecordCompletedTicks` is valid (`> 0`, `<= sealedLength`) or when
mid-window capture progress is still in flight (`unpublishedCandidate`,
`construction`, or active `passChangeProgress.fromCaptureStore`). Candidate or
construction `loopLengthTicks` may still differ from the sealed loop until
`handoffPendingSnapshotLength` runs. Discard only when applied progress exceeds
the sealed length or handoff fails. Supersedes the discard-on-length-mismatch
intent of the 2026-08-26 first-bar amendment for successful handoff at record
stop ([`record_stop_snapshot_publication_gap_display_bugfix.md`](Plans/record_stop_snapshot_publication_gap_display_bugfix.md)
stage 4 / RC1).

**Amendment (2026-09-12):** Before the first published root exists, OVERDUBBING idle
may `startNextPendingConstruction` when session-exit publication is queued. In-flight
stepping during OVERDUBBING is unchanged once a root exists; post-root successor
construction on PLAYING/OVERDUBBING remains frozen per prior amendments
([`record_stop_snapshot_publication_gap_display_bugfix.md`](Plans/record_stop_snapshot_publication_gap_display_bugfix.md)
stage 5 / RC2; device trigger [`124756`](../captures/session_20260912_124756.log)).

**Amendment (2026-08-26):** First-record live `snapshot_idle` starts only
when `currentTick - startLoopTick >= TICKS_PER_BAR`. `loopLengthTicks` can
already be one bar at record start because stop-quantize of a small raw
length is one bar. Do not construct that in-progress bar.

**Amendment (2026-08-26):** First-take RECORDING display length stays the
continuous playhead (`currentTick - startLoopTick`). Quantized
`loopLengthTicks` remains the snapshot-pending / stop-quantize working
length and must not drive the piano-roll window.
[`120020`](../captures/session_20260826_120020.log) had one first-bar
`snapshot_idle` burst, then `DISP` `loopLen` jumped on every
`record_length` step. Overdub display keeps the sealed length.

**Amendment (2026-08-26):** Live first-record construction continues after
bar 1 for each later completed bar. The first parent is created with one
extra bar of tick room so the next completed bar can append. The
in-progress bar is not applied. [`120020`](../captures/session_20260826_120020.log)
stopped after bar 1 because unpublished blocked further live construction.

**Amendment (2026-08-26):** Live RECORDING appends completed bars only when
the unpublished parent still has tick room. It does not create a new empty
parent and re-apply the settled prefix on the record path. The first parent
is created with 16 extra bars of room so a typical first take appends.
When the parent is too short, idle does not start. Session-exit may still
discard unpublished and rebuild at the sealed length.
[`121754`](../captures/session_20260826_121754.log) rebuilt the prefix on
each length step (2611 RECORDING `snapshot_idle` steps, 17.7 s CPU, BPM
89–184, post-stop `midi_gap` 361 ms).

**Amendment (2026-08-26):** During unpublished first-root construction,
PLAYING/OVERDUBBING in-flight snapshot idle alternates eligible turns
(round-robin cadence). This keeps live updates active while preventing
consecutive snapshot turns from monopolizing timing-critical deferred
windows. [`125527`](../captures/session_20260826_125527.log) showed
repeated OVERDUBBING/PLAYING `snapshot_idle` slices (2.6–27.4 ms) with
`midi_gap` spikes while unpublished work stayed in-flight.

**Amendment (2026-08-26):** During unpublished snapshot contention in
PLAYING/OVERDUBBING, visual idle fallback stays constrained and cadence-gated
while snapshot updates continue. This prevents long compatibility visual
rebuild bursts from stacking on every non-snapshot realtime turn.
[`130244`](../captures/session_20260826_130244.log) still showed
`idle_reconstruct` 145–217 ms with unpublished in-flight work and
`midi_gap` spikes (`415272`, `242612`, `451784`).

**Amendment (2026-08-26):** Unpublished visual fallback constraints apply
whenever unpublished snapshot work is pending (not only while an in-flight
construction already exists). Live-record snapshot append starts cap admitted
new settled range to one completed bar per start so setup cost does not jump
when completed ticks race ahead. [`131338`](../captures/session_20260826_131338.log)
still showed first-PLAYING stacked `idle_maint`/snapshot/persist and later
`usbdisp`/`load_frame`/`late_on` windows.

## DEC-044 — Immutable loop snapshot model replaces maintained LCR direction

**Date:** 2026-08-25
**Status:** Accepted — design phase
**Scope:** `feature/immutable-loop-snapshot-passes`, forked from
`546169f66658b87c54e6166b79eba74628aa3d1e`
**Authority:** [`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/),
[`loop_pass_snapshot_effective_state_refinement.md`](Plans/loop_pass_snapshot_effective_state_refinement.md)

**Context:** The maintained `LoopContentResolution` lifecycle added after the fork
introduced a second effective-state representation, identity binding, deferred
history application, playback fill readiness, and first-consume refusal. The
feature objective is instead a complete immutable snapshot selected directly by
consumers. The fork point already contains the `Loop` / `LoopPasses` ownership
boundary and the existing deep-cloned `PersistedLoopSnapshot`, while avoiding the
maintained-LCR implementation sequence.

**Decision:**

1. Build the immutable complete-snapshot model from the selected fork point rather
   than reverting the maintained-LCR branch.
2. `Loop` owns active snapshot selection, candidate publication, and immutable
   object allocation; `LoopPasses` owns snapshot records and reference roots.
3. Existing capture, overlap, pairing, NOTE_EDIT geometry, playback runtime, and
   `StorageManager` owners retain their responsibilities during migration.
4. `LoopContentResolution` is not pursued as a maintained effective-state owner.
   Any compatibility query surface is stateless and snapshot-based, and is retired
   after consumer migration gates pass.
5. Undo, redo, and clear-slot restore select complete snapshot identities rather
   than replaying intervening history.
6. Snapshot construction stays at existing off-clock commit boundaries until a
   later phase defines an explicit execution bound and overrun policy.
7. This decision authorizes the OpenSpec design/specification phase only. Firmware
   implementation requires the phase gates in the change's
   `ARCHITECTURE-REVIEW.md` and a corresponding `CURRENT_WORK.md` authorization.

**Consequences:** The existing `PersistedLoopSnapshot` is only a starting point;
it deep-clones mutable `LoopPasses` and does not satisfy immutable structural
sharing. The current branch's maintained-LCR playback and load behavior is not
carried forward. Compatibility parity tests, storage design, lifetime policy,
and consumer migration remain required before removal of materialization or any
old representation.

**Amendment (2026-08-25) — Phase 0 reference-root refinement:**

8. `LoopSnapshot` is the complete immutable effective-state object. `LoopPass`
   (`RecordPass`, `OverdubPass`, `EditPass`) is the historical/compatibility
   operation record. A commit may produce both; they are not interchangeable.
9. A successor snapshot reuses unchanged event-object references and allocates
   new immutable objects only for added or modified content. The logical
   reference root is pitch → bar membership for notes and bar membership for
   events; unchanged membership remains shared. The physical directory
   implementation is selected later through measurement. Phase 2 is blocked
   until the physical directory and construction bounds are admissible.
10. Intermediate record prefixes, overdub wraps, and NOTE_EDIT batches are
    playable provisional snapshots, not global undo entries. The session-exit
    commit is the global undo transition.
11. After undo from C to B and a new commit D, C is an abandoned branch and not
    a redo target. Reclamation follows named-root reachability.
12. Persistence stores the latest saved complete snapshot only. Reboot loads
    that snapshot, starts with undo depth 0 and redo empty, and discards unsaved
    session tails. This change does not persist undo history or promise
    post-reboot undo.
13. Later verification uses existing gates plus invariant-protecting checks
    only. This change does not add a new test architecture.

**Amendment (2026-08-25) — logical pitch/bar indexes:**

14. The logical snapshot indexes are selected. Notes: pitch → canonical 768-tick
    bar → bar NoteRefs of complete notes intersecting that bar. Events: bar →
    EventRef list with a defined canonical tick/order contract. Occupy /
    open-at-S remains `ActiveNoteLedger` / `presentAt`, not a snapshot index.
    Unmatched open Ons are not stored in the span index.
15. A `NoteRef` may appear in multiple bar lists in one snapshot; every
    occurrence identifies the same immutable version and `NoteId`. Candidate
    sets deduplicate by `NoteId`. Inspecting a candidate is not replacing it.
16. Dirty pitch is the incoming note's pitch. Dirty bars are the union of bars
    intersected by the incoming span and every note whose geometry or
    membership the existing resolver changes. Snapshot construction translates
    that mutation; it is not a second overlap owner.
17. Wrap-crossing notes keep two linear segments. Each segment inserts
    membership in the bars it intersects; both sets share one `NoteId`.
18. Only dirty pitch/bar membership nodes and the minimum immutable directory
    path required to reach them may be newly allocated. Unchanged membership
    remains shared with the parent. Path-copy is rejected as the semantic
    overlap model and remains allowed as a possible directory implementation.
    The physical directory is a Phase 1 / measurement decision.
19. Affected pitch plus dirty bars is semantic locality, not an execution
    bound. Execution cost depends on intersecting memberships and the
    physical directory and requires measurement. A long note can still touch
    every intersecting bar. Phase 2 stays blocked on formal reassessment and
    an `ExecutionBounds.md` row.

**Amendment (2026-08-25) — Phase 0 design trim:**

20. A snapshot's logical content is completely determined by its own
    reference root. Resolving a reference or answering a content query never
    requires consulting a parent snapshot.
21. Bar `EventRef` lists are an indexed view of existing event-stream
    order, not a second ordering authority.
22. The architectural construction contract is identify, replace, construct
    root, freeze, and publish. Phase 1 decomposes those steps.
23. Temporary LCR machinery may construct candidates. It never owns
    effective state, publication, or snapshot lifetime.
24. Record-prefix snapshot retention is a measured lifetime/memory risk
    against the named-root policy.

**Amendment (2026-08-25) — formal architecture reassessment approval:**

25. The formal reassessment in
    [`ARCHITECTURE-REASSESSMENT.md`](../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REASSESSMENT.md)
    is approved for the design/specification phase. It approves the
    ownership evolution to `Loop` / `LoopPasses` for complete immutable
    `LoopSnapshot` state while retaining existing capture, overlap, NOTE_EDIT
    geometry, playback, undo, persistence, and derived-representation owners.
    It does not authorize firmware, runtime publication, a physical directory
    choice, or an execution-bound classification.

**References:** [`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/),
[`loop_pass_snapshot_effective_state_refinement.md`](Plans/loop_pass_snapshot_effective_state_refinement.md),
[`loop_snapshot_pitch_bar_span_index_refinement.md`](Plans/loop_snapshot_pitch_bar_span_index_refinement.md),
[`timeline-passes`](../openspec/specs/timeline-passes/),
[`loop-content-history`](../openspec/specs/loop-content-history/).

---

## DEC-043 — Execution bound class is a gated architectural property

**Date:** 2026-08-22  
**Status:** Accepted  
**Owner:** `main.cpp::loop()` (service order); domain owners stay Track / Loop / StorageManager / DisplayManager / `DeferredJobScheduler`  
**Authority:** [ARCHITECTURE_RULES.md](Authority/ARCHITECTURE_RULES.md) § Execution bound rules; [Architecture/ExecutionBounds.md](Authority/Architecture/ExecutionBounds.md)  
**Contract:** [runtime_scheduling_admission_model_architecture.md](Plans/runtime_scheduling_admission_model_architecture.md) invariants N and O  
**Does not supersede:** DEC-027 (`DeferredJobScheduler` north star); DEC-016 (four-layer data flow)

**Context:** Ownership already has rules, a transfer protocol, a checkpoint question, and a DEC requirement. Bound class lived only in a research inventory marked as authorizing no firmware. Local slice budgets (`LoadLoopBudget`, `PersistenceBudget`) exist and remain necessary. A proposed `RuntimeWorkBudget::admit(WorkClass)` was rejected because class admission cannot bound nested or indivisible work (contract B1; cause class C6).

**Decision:**

1. **Bound class is a gated architectural property** alongside ownership. Every owner on the interval path must declare structurally bounded, empirically bounded, or unbounded in [ExecutionBounds.md](Authority/Architecture/ExecutionBounds.md).
2. **Adding or extending an unbounded owner on the interval path is a design session** — architecture-checkpoint question 3, same weight as ownership and state-transition changes.
3. **`RuntimeWorkBudget::admit(WorkClass)` remains rejected.** Do not introduce a scheduling class, a new Manager, or interval reservation from this decision.
4. **Local owner budgets stay.** They do not replace a declared bound class and do not make an indivisible step structurally bounded.
5. **Inventory home:** classification is maintained in `ExecutionBounds.md`. Measurements and capture citations stay in the R1C research doc.

**Consequences:** Hygiene and bugfix work on idle maintenance, persistence, display resolve, load, and playback/clock paths must answer checkpoint question 3. Reclassify a row in the same commit as the firmware that enforces the new class. Owner-Boundary Gate (L1–L7) is unchanged and still does not authorize Stage A.

**References:** [ExecutionBounds.md](Authority/Architecture/ExecutionBounds.md), [runtime_scheduling_owner_boundary_admission_refinement.md](Plans/runtime_scheduling_owner_boundary_admission_refinement.md), DEC-027.

---

## DEC-042 — Same-pitch active-note identity is a cardinality problem, not an Off-identity problem

**Date:** 2026-08-19  
**Status:** **Implemented.** Native shipped. Nested HITL **MET** [`095902`](../captures/session_20260819_095902.log) (`n=0 a=1` = 0). Extra-open is [`overdub_occupy_catchup_open_note_stack_bugfix.md`](Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).  
**Owner:** `ActiveNoteLedger` — `LoopPlaybackRuntime::ledger`. `playCommittedLoopMidi` writes via `applyPlaybackLedgerEvent`; `Track::snapshotOverlapHoldCandidates` / `collectOverdubNoteOnParticipantIds` read.  
**Plan:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md)  
**Investigation:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](Plans/overdub_occupy_unmatched_off_ledger_investigation.md)  
**Parent:** [DEC-041](#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) point 9  
**Evidence:** [`090050`](../captures/session_20260819_090050.log) spans; [`092336`](../captures/session_20260819_092336.log) event-backed (`DIAG,lcr,mmevt`)

**Context:** Occupy reported `n=0 a=1` at holds where the source view showed a note present. The
proposed root cause was that committed NoteOffs carry no `noteId`, so an Off clears a lane it does
not own — with a fix of stamping identity onto Offs. The device diagnostic disproves that framing.
The proven geometry is **nested**: outer note `240–480` (id 4819) with `288–336` (id 4814) fully
inside it, on one `(channel, pitch)`. `eq=1` on all 105 occupies and `b == a` on every mismatch, so
the LCR prepared spans and the source view agree; only the ledger disagrees.

**Decision:**

1. **Off identity alone is rejected as the fix.** `ActiveNoteLedger::noteOn` overwrites the single
   `Entry` unconditionally, so the outer note's identity is destroyed by the **inner NoteOn**,
   before any Off. An identity-matched clear at `Off@336` compares 4814 against 4814, matches, and
   still empties the lane. Stamping Offs cannot repair this geometry.
2. **FIFO stamping stays rejected.** Untagged Off pairing in this repo is **LIFO**
   (`appendCanonicalSpansFromMidiRange`, `LoopEventValidation` Pass 2,
   `orderSamePitchNoteOffsForLifo`, `findLinearOffForNoteOnLifo`). LIFO pairs the proven nested
   geometry correctly, so the pairing rule is not the defect either.
3. **The defect is cardinality.** One `(channel, pitch)` can carry several **open NoteOns**.
   Implemented: sparse open-NoteOn table (`kMaxOpenNotes = 128`). Occupy collects every open
   identity via `forEachActive`. `noteId()` is newest-on-lane only (compatibility).
4. **Rejected alternative:** having the ledger consult the derived canonical spans the source view
   uses. It conflates "what is sounding" with "how source geometry is represented" and risks
   derived-resolution work on a hot path.
5. **Supersedes DEC-041 point 9.** The ledger is no longer one Entry per `(channel, pitch)`. It
   does not Hide/Shorten stored overlap; it keeps every playback NoteOn open until NoteOff
   **resolution**.
6. **NoteOff resolution (design session):** known identity + found → exact remove; no identity →
   LIFO on the lane; known identity + not found → orphan, **no** LIFO fallback. Do not stamp
   capture Offs. `silenceSlotMidiOutput` sends one physical Off per unique `(channel, pitch)`;
   `forEachActive` is not deduped. Overflow refuses the additional On (never evicts).

**Design session must answer:** what an active entry means when 2+ identities are active on one
lane; how committed notes get identities; how untagged committed Offs resolve to one of several
active notes; how an Off crossing the loop boundary finds its note; how overdub notes participate;
what `snapshotOverlapHoldCandidates` selects when several identities are active; whether the ledger
needs all active notes or only those occupy consumes; and RAM1 cost on Teensy (`ActiveNoteLedger`
is `16 × 128` entries today, and `longestActiveSpanBars` plus orphan-Off suppression in
`applyPlaybackEvent` both depend on the current single-entry shape).

**Does not change:** clock equal-phase Off before On (shipped); occupy staying a
reader; `playMidiEvents` never called from occupy; capture never folded into `mergedMidiEvents`.

**Validation:** observability only. `pio test -e native` 1359/1359;
`test_nested_same_pitch_note_lost_at_second_note_on_090050_pitch12` pins the proven geometry and
point 1. RAM1 unchanged (code 425852 / locals 4768).

---

## DEC-041 — Occupy present-at-S is JIT, not full-loop `lcr,mat`

**Date:** 2026-08-18  
**Status:** Accepted. Occupy-lookup firmware **shipped**. **Amended 2026-08-18:** `collectOverdubNoteOnParticipantIds` reads at most one `ActiveNoteLedger::Entry` per `(channel, pitch)` at `currentTick`. `LoopPlaybackRuntime::ledger` holds that state. `playCommittedLoopMidi` writes it via `applyPlaybackLedgerEvent` before `sendMidiEvent` may emit. Occupy **reads** it. Do not say “occupy = ledger.”  
**Owner:** `Loop` / `LoopContentResolution` = `LoopPasses`. `LoopPlaybackRuntime::ledger` = state at `currentTick`. `playCommittedLoopMidi` writes. `sendMidiEvent` and occupy = readers.  
**Plan:** [`overdub_present_at_tick_jit_architecture.md`](Plans/overdub_present_at_tick_jit_architecture.md)  
**Implementation:** [`overdub_present_at_tick_jit_enhancement.md`](Plans/overdub_present_at_tick_jit_enhancement.md)  
**Parent:** [DEC-037](#dec-037-loop-content-resolution-parallel-prototype)  
**Does not supersede:** DEC-037 6.0 (no cold LCR on the overdub button); DEC-040; Phase 3 note-on fill disable; Phase 4 note-off fill skip when the source view covers the loop.

**Context:** Stage 1 `from=span` HITL treated STOPPED full-loop `deviceGateComplete` as occupy readiness. A 64-bar gate is 31–40 s STOPPED ([`173842`](../captures/session_20260815_173842.log) / [`185931`](../captures/session_20260815_185931.log)). That is rejected option A (always-ready). [`132806`](../captures/session_20260818_132806.log) Stage 1c held; 64-bar `from=span` missed because PLAYING started during `prep`. Occupy needs notes of pitch P present at `currentTick`, not a finished source-view copy.

**Decision:**

1. **Occupy contract** is present(S, P) via `displayNotePresentAtHold` on `NoteSpan`. Prepared hit stays `tryCollectPreparedPresentNoteIdsAtTick`. USB miss stays the source-view walk until resolved state at S exists. Note-on still must not `resolveWindow` / `ensureOverdubSourceNotesForHold`.
2. **`from=span` / wait-for-`lcr,mat`** is opportunistic source-view quality, not occupy or product readiness. Stage 1 membership, length identity, and dirty/save stall stay shipped. Stage 2 consume merge stays parked.
3. **Resolved state + overlap (amended).** At every resolved tick the resolver has enough state to determine which notes are present. Occupy evaluates that state **only** when a NoteOn/NoteOff for the recorded pitch occurs. It does not maintain or reconstruct a list of all notes present at the current tick. Time advancing does not make occupy revisit notes; the resolver advances state.
4. **Not a collection primitive.** Do not add `collectPitchPresentNoteIdsAtTick` as the architectural API. `tryCollectPreparedPresentNoteIdsAtTick` is the prepared-path helper (evaluate P against prepared spans). Occupied ids are the overlap result, not a stored `vector<NoteId> presentAtTick`.
5. **USB must not reconstruct pass history** to build a present list (`resolveWindow`, cold `resolveState(passes)`, this-pitch gather-then-reconstruct on note-on). Checkpoint tail / `tryResolvePreparedState` stays **rejected as a cold USB miss** (needs `deviceGateComplete` products). It is **accepted as consume of already-resolved state** when the resolver has advanced to S. Occupy then evaluates P against that state; it does not return the full `PresentNoteVec` as the occupy set.
6. **Scheduled prepare** may extend the existing device gate as a **range** around the playhead (Stage 9 range tests) so resolved state exists at `currentTick` while PLAYING. Name: `deviceGate` range / `deviceGateBeginRange`. Not a new Session or Manager. PLAYING-admitted only for bounded range work. Full-loop LCR stays STOPPED-only and is not occupy readiness. Option B (PLAYING full-history LCR) stays rejected.
7. **Consume / Hide** stays on `overdubSourceView`. Do not fold consume into occupy.

**Does not change (pre-amendment):** DEC-037 6.0; `notePresentAt`; `overdubSourceView`; `kOverdubSourceWindowBars` production size.

**Superseded by amendment 8–15:** occupy as present(S,P) span-collection (point 1); “occupy = ledger” investigation framing. Points 2, 4, 5 (no USB reconstruct), 7 (consume on source view) still hold. Point 6 (`deviceGateBeginRange` as occupy prepare) is **not** the path; keep `ActiveNoteLedger` at `currentTick` via `playCommittedLoopMidi`.

**Validation:** none this architecture slice (docs). Implementation **paused** pending `PresentNote.noteId` vs `evt.noteId` — see amendment below.

**Superseded (same day):** “this-pitch pass events whose LinearSpan can contain S” as the **production USB miss**. That path reconstructs notes from pass history on occupy. It is not the architecture.

**Paused (same day):** `evaluateOccupyOverlap` / span-collect native tests.

**Amendment 2026-08-18 — `ActiveNoteLedger` at `currentTick` (not “occupy = ledger”):**

8. **Product contract.** `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` at `currentTick`: at most one `Entry` per `(channel, pitch)`. `Track::sendMidiEvent` / `midiHandler.sendMidiEvent` use that `Entry`; they do not create it.
9. **Runtime invariant.** `ActiveNoteLedger` has at most one active `Entry` per `(channel, pitch)`. After `Entry.noteId`, that slot holds at most one `noteId`. This is **not** a stored-content invariant. [`152940`](../captures/session_20260813_152940.log) `max_same_pitch=322` means `LoopPasses` may contain overlapping same-pitch notes. `playCommittedLoopMidi` applies `PlaybackMergedMidiEvents` to that one-slot ledger (last NoteOn overwrites). The ledger represents the resulting runtime owner; it does not resolve stored same-pitch overlap. The event stream does not guarantee one owner.
10. **Content-commit owners.** Overdub Hide/Shorten (`Loop::accumulatePendingNoteChangesFromSourceNotes`) and NOTE_EDIT overlap (`NoteGeometryResolver`) remain owners for **new** overlap. They do not rewrite stored loops from this decision, and they do not currently guarantee every path that reaches playback is already one-owner. Not `ActiveNoteLedger`. Not `sendMidiEvent`.
11. **Runtime.** `ActiveNoteLedger` holds state at `currentTick`. `playCommittedLoopMidi` writes via `applyPlaybackLedgerEvent` **before** `sendMidiEvent` may emit. Completing `Entry` with `evt.noteId` is identity on the slot, not a new occupy owner.
12. **Do not say occupy = ledger.** `Loop` / `LoopContentResolution` keep `LoopPasses`. `LoopPlaybackRuntime` keeps `ledger`. Occupy reads `Entry.noteId`.
13. **`PresentNote`.** Has `noteId`, but `PresentNoteVec` is **all** spans containing S (`upsertPresentNote` by `NoteId`) and has no `endTick`. Occupy already uses `NoteSpan`. Do not copy `PresentNoteVec` onto the ledger. Add `noteId` to `Entry`; keep `StateCheckpoints::presentAt` until a later decision. Do not add a third type.
14. **`sendMidiEvent` lag.** Goes away if `playCommittedLoopMidi` writes `ledger.noteOn` at tick 32 **before** `midiHandler.sendMidiEvent`. USB at 32 reads `Entry.noteId` even if MIDI is not yet sent.
15. **Keep ledger at `currentTick`.** Split write vs emit on `playCommittedLoopMidi` / `PlaybackMergedMidiEvents`. Not a second occupy resolver. Not full-loop `deviceGateBegin`. Not `resolveWindow` on USB.
16. **No `length` on `Entry`.** The ledger is state at `currentTick` until NoteOff. Occupy does not need end geometry. `Entry.startTick` exists for `longestActiveSpanBars` (no `.cpp` call sites). Do not add `length` or `endTick`. Do not delete `startTick` in this occupy work. After Stage 0: `noteOn(channel, pitch, noteId, tick, velocity)` / `noteOff(channel, pitch)`.

**Does not change:** DEC-037 6.0 (no cold LCR on the overdub button); `overdubSourceView` for consume; `kOverdubSourceWindowBars` production size; `notePresentAt` as checkpoint fill.

**Validation:** Stage 0 answered: `PresentNote.noteId` and playback `evt.noteId` are both `MidiEvent.noteId`. Write-before-emit: `applyPlaybackLedgerEvent` before `sendMidiEvent`. Occupy lookup: `collectOverdubNoteOnParticipantIds` reads `ledger.noteId`.

---

## DEC-040 — Skip PLAYING/STOPPED/MUTED overdub participant HITL

**Date:** 2026-08-18  
**Status:** Accepted  
**Owner:** overdub participant discovery (`snapshotOverlapHoldCandidates` / prepared present-at-S)  
**Plan:** [`overdub_participant_loop_content_architecture.md`](Plans/overdub_participant_loop_content_architecture.md)

**Context:** Phase 2 required A vs B identity under PLAYING / STOPPED / MUTED / outside-gather before Phase 3 fill disable. Overdub participant `#CAP` (`lcr,part`) only fires on USB note-on while **OVERDUBBING**. PLAYING, STOPPED, and MUTED are not overdub states, so that HITL matrix cannot be run. Prepared HITL `eq=1` already passed [`040236`](../captures/session_20260818_040236.log).

**Decision:**

1. Do **not** require PLAYING / STOPPED / MUTED participant HITL. Those states cannot run overdub.
2. Present-at-S must still not leak MIDI execution (send / `ActiveNoteLedger` / mute). That stays a definition, not a transport-state device matrix.
3. Phase 3 may start after prepared OVERDUBBING `eq=1` (met: 040236). Production occupy uses prepared present-at-S when ready (Phase 3 firmware).
4. Does **not** skip: 64-bar OVERDUBBING; a note present at `S` whose NOTE ON is outside the 2-bar playback gather (still testable while OVERDUBBING). That is Phase 3 validation, not a PLAYING-state test.

**Does not change:** DEC-037 6.0 (no cold LCR on the overdub button); RC8 occupy/consume; `overdubSourceView` remaining responsibilities; `notePresentAt`.

**Validation:** none for the skipped states. Phase 3 firmware in tree. Device gate is OVERDUBBING: no `why=hold` on note-on; 1-bar occupied `eq=1`; 64-bar / outside-gather occupy from prepared present-at-S without USB `resolveWindow`.

---

## DEC-039 — Persist NoteId reconciled at NOTE_EDIT commit boundary

**Date:** 2026-08-16  
**Status:** Accepted  
**Plan:** [`note_edit_persist_noteid_identity_bugfix.md`](Plans/note_edit_persist_noteid_identity_bugfix.md)

**Context:** [`173806`](../captures/session_20260816_173806.log) exit saved NoteRange+Pitch targeting session/display **256**; rematerialize still had M24@888. `applyNoteEditPass` is strictly `targetNoteId`-keyed. `DisplayNote.noteId` is not persist authority. B1 assign-at-open does not unify two different valid ids.

**Decision:**

1. Capture / rematerialized NoteOn is the authoritative persisted `NoteId`.
2. NOTE_EDIT focus/session identity may temporarily differ.
3. `EditManager::commitEditAction` reconciles at the commit boundary via `resolvePersistIdentityForExistingNote` / `reconcileMoverPersistIdentity` against `LoopPasses::materializeToEventVector` **before** `saveNoteEditPass`.
4. Reconciliation succeeds only with **exactly one** rematerialize NoteOn at `commitBaseline` pitch+start. Zero = unresolved; more than one = ambiguous. Neither guesses. `findNoteOnById == -1` does not by itself imply geometry identity.
5. `applyNoteEditPass` stays strictly NoteId-keyed. Added notes keep the session-allocated id until Create persists it.
6. Stage 2 select-time bind is parked until Stage 1 is device-proven and a fresh select still mismatches.

**Does not change:** DEC-029 current-state geometry ownership; Loop `saveNoteEditPass`; `applyNoteEditPass` lookup.

**Validation:** Native `test_edit_apply` unique / added / zero / ambiguous / already-present; device 173806 gesture (`replay_flat` home missing).

---

## DEC-038 — Overdub wrap commit and session undo

**Date:** 2026-08-15  
**Status:** Accepted. **038.1 HITL PASS.** **038.2 landed.** Live +1 HITL PASS ([`011413`](../captures/session_20260816_011413.log)). **Amended 2026-08-20:** `OverdubPass.overdubSessionIndex` is content metadata so reboot derivation matches 038.2 grain. Not a `session-id` noun. **Amended 2026-09-01 by [DEC-051](#dec-051--overdub-wrap-seals-at-loop-end-not-session-start):** wrap persist seals at loop end (phase 0); S remains session identity.  
**Owner:** overdub lifecycle — `Track` trigger / `Loop` pass list + `capture.store`. LCR publish stays `publishPreparedOverdubPass`. GUS kind stays `OverdubPassAdded`.  
**Plan:** [`loop_content_resolution_overdub_state_evaluation_refinement.md`](Plans/loop_content_resolution_overdub_state_evaluation_refinement.md)  
**Parent:** [DEC-037](#dec-037-loop-content-resolution-parallel-prototype) (6E native PASS); [DEC-031](#dec-031-overdub-overlap-encode-pending-buffer-to-editpass) / [DEC-032](#dec-032-overdub-editpass-unification-reassessment) companions  
**Does not supersede:** DEC-037 6.0 (no LCR construct on the button); DEC-036 3b for **display** idle visual cache; DEC-031 companion encode. Overdub **source view** does not copy visual cache (RC7).

### Problem

Overdub source view is frozen at session start. A wrap that returns to the start-overdub tick cannot overlap its own prior-wrap notes. Native 6E proved `resolveState` + publish can make wrap-1 the wrap-2 source. Production still treats one overdub session as one `OverdubPass` sealed only at stop, and undo while OVERDUBBING discards live capture without a session gate (`loopHasLiveOverdubCapture`).

### Decision

1. **Wrap persist.** **S** is the `playheadPhaseTick` already computed in `startOverdubbing` and passed to `beginCapture`. Store that first-session value on the overdub lifecycle owner (`Loop` / `Track`). Session-scoped: wrap `beginCapture` and `discardCapture` must not clear it. Clear S when the overdub session ends. When the playhead crosses **loop end (phase 0)**, seal the **current wrap** as an `OverdubPass` + DEC-031 companions, `publishPreparedOverdubPass`, push a session-stack entry, `beginCapture(Overdub, S)` again, stay OVERDUBBING. `sealedAtTick = 0`. Stop seals the partial wrap the same way. Do not add a new domain noun for S. **Amended [DEC-051](#dec-051--overdub-wrap-seals-at-loop-end-not-session-start).**
2. **Still an `OverdubPass`.** No new domain noun. No session-id on the pass in this DEC.
3. **Held note at S.** Do not call `Track::finalizePendingNotes`. That closer is STOP only (6E.5). Publish completed ON/OFF pairs only. Add stays NoteOff-owned (`accumulatePendingNoteChangesForIncomingNote`).
4. **Empty wrap.** No pass, no session-stack push, no revision bump, no LCR publish.
5. **6.0 at wrap and enter.** Bounded 6D.4 publish (wrap), then a **bounded source-view rebuild** (`Loop::rebuildOverdubSourceView`: prepared `NoteSpan`s → `DisplayNote`s when ready; else `tryResolvePreparedWindow` / windowed `resolveWindow(passes)` + `reconstructDisplayNotes` + wrap-paired overdub fill). Session-start `establishOverdubSourceView` calls the same helper (`why=open`). Wrap undo/redo of a sealed pass calls the same helper (`why=undo` / `why=redo`) when the source view is already established. Live discard does not. Note-on occupy does **not** hold-fill: `Loop::collectOverdubNoteOnParticipantIds` uses prepared present-at-S when ready, else the source-view walk — never `resolveWindow` on USB note-on (Phase 3). Note-off consume JIT-fills (`Loop::ensureOverdubSourceNotesForHold`, 16-bar `resolveWindow(passes)` for **this pitch** only) **only when the source view does not already cover the loop** (`loopLen > overdubSourceWindowLengthTicks()`, Phase 4). Empty occupy still walks source-view notes. Not visual cache. No LCR construct on the MIDI/button path, no full-loop materialize, no `resolveState` on note-off, no `VCACHE,full`, no SD. Clarified 2026-08-17 (RC6 wrap, RC7 enter, RC8 hold-window JIT). Undo/redo refresh 2026-08-18 ([`010422`](../captures/session_20260818_010422.log): after wrap undo A was `a=2` vs B `b=1`). Prepared-span fill 2026-08-18 ([`021716`](../captures/session_20260818_021716.log): pre-wrap extras live only on finished checkpoint opens). Note-on fill disable 2026-08-18 (Phase 3). Note-off fill skip when source view covers the loop 2026-08-18 (Phase 4).
6. **Undo while OVERDUBBING.** Session-gate in `MidiButtonActions::handleUndo` / `handleRedo` (same routing as NOTE_EDIT **E:** — no **U:** fallthrough). Sidebar prefix is **O:** while `Loop::hasOverdubSession()` (same role as **E:**). Stack is a Loop-owned `PassId` list of this session’s **sealed** wraps. `overdubSessionUndoDepth` increments only at `pushOverdubSessionPass` (start-tick S wrap), not when live `capture.store` gains notes and not on undo. Sidebar `O:` is `overdubSessionDisplayDepth` = sealed + 1 (the wrap in progress). Not `NoteEditSessionUndoStack`. Not `EditManager`. Undo: disable the last sealed wrap (`setCapturePassState(Disabled)` + companions), then `rebuildOverdubSourceView` if the source view is established. Live store stays. Live discard only when cursor is 0 (before the first sealed wrap). No GUS pop. Clarified 2026-08-18 ([`005000`](../captures/session_20260818_005000.log): seven live-first undos never peeled sealed wraps). Display +1 restored 2026-08-18 ([`005509`](../captures/session_20260818_005509.log)).
7. **Undo after stop.** One **U:** `OverdubPassAdded` for the whole session. `passIds` = wrap 1..N. `editPassIds` = all companions. Extend that kind; do not add a new `UndoEntryKind`. GUS wire bump (STK2 → next token) for the `passIds` list.
8. **Crash mid-session.** Out of this DEC. Reboot during OVERDUBBING is the same loss as today’s uncommitted overdub.

### Implementation stages (firmware after approval)

| Stage | Prove |
|-------|--------|
| **038.1** | Re-entry at S: completed-pair seal + publish + session-stack push + `beginCapture` + stay OVERDUBBING. Session undo/redo while OVERDUBBING. No GUS `passIds` yet. |
| **038.2** | Stop: one `OverdubPassAdded` with `passIds` + companions. GUS wire. Session stack cleared. |

### Rationale

Persist grain is one wrap so wrap-2 can overlap wrap-1 via prepared `resolveState`. Undo grain is one session so GUS depth and sidebar **U:** do not grow with wrap count. NOTE_EDIT already session-gates `handleUndo`; overdub reuses that routing, not that payload.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Path B (live capture in LCR) | LCR invents note-offs; forbidden by DEC-037 / OpenSpec 6.5 |
| N **U:** entries (one per wrap) | N gestures after stop; GUS grows with wraps |
| Reuse `NoteEditSessionUndoStack` | Wrong payload and owner |
| Commit at loop tick 0 when S ≠ 0 | Wrong wrap origin (6E.1b) |
| `finalizePendingNotes` at S | Seals an incomplete Add (6E.5) |
| SD persist on wrap | Out of this DEC |

### Affected modules

`Track` overdub lifecycle / playhead wrap detect; `Loop::commitCapturePass` / `beginCapture`; `TrackUndo` session gate + `pushOverdubPassAdded`; `MidiButtonActions::handleUndo`; `LoopContentResolution::publishPreparedOverdubPass` (already 6E.4). `EditManager` unchanged.

### Constraints created

- Wrap commit must not call `finalizePendingNotes` or `LoopStopFinalize::finalizeWrapWindowOnStore` to invent an OFF at S.
- `handleUndo` while OVERDUBBING must not fall through to GUS.
- Do not add a new undo kind. Do not put wraps on `NoteEditSessionUndoStack`.
- Keep display idle 3b (`DIAG,lcr,vch`) on `tryResolvePreparedState` miss. Overdub source-view enter/wrap uses `rebuildOverdubSourceView`, not a visual-cache copy.
- No SD on wrap. **Amended 2026-08-20:** stamp `overdubSessionIndex` on each wrap `OverdubPass` (same grouping role as `editPassIndex`). Do not name it session-id. `0` is ungrouped (legacy / missing `OSI1`).
- S is session-scoped. Do not store it only on `Capture` if `discardCapture` would drop it mid-session.

### Related OpenSpec

`openspec/changes/loop-content-resolution/`. Preflight: `openspec/changes/loop-content-resolution/PREFLIGHT-WRAP-COMMIT.md`.

### Migration notes

038.2 GUS wire: persist `passIds` on `OverdubPassAdded`. Legacy STK2 rows keep a single `passId`. **Amended 2026-08-20:** loop file adds additive `OSI1` after `GEO1` (`passId` + `overdubSessionIndex` per overdub). Cards without the tail load index `0` (one unit per pass). `deriveContentUndoUnits` groups wraps that share a non-zero index into one `OverdubPassAdded` with `passIds`.

---

## DEC-037 — LoopContentResolution parallel prototype

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `LoopContentResolution` (new) — effective state/window queries; `LoopPasses` remains content authority; `StorageManager` remains persist owner  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](Plans/loop_event_sourced_resolution_architecture.md)  
**OpenSpec:** `openspec/changes/loop-content-resolution/`  
**Parent:** [DEC-036](#dec-036-runtime-effective-event-source-for-overdub) (overdub entry without display reconstruction still holds; D1 eager flatten stays withdrawn)  
**Does not supersede:** DEC-016 four-layer model; DEC-035 content-only persist; DEC-036 overdub-entry contract; DEC-031/032 overlap semantics

### Problem

Layer D 3b made overdub **entry** cheap when `visualCache` is clean ([`045556`](../captures/session_20260814_045556.log) `begin_capture` 2214 µs). After a new overdub **commit**, `markDisplayCachesStale` still dirties every bar and idle/window gather still walks every active pass list (`CommittedEventRange::inWindow` + `collectActiveCommittedChunkLists`). Short loops still full-materialize on stop (`VCACHE,full`). D1 tried an eager full `passesMaterializedStore_` and `invalidateCaches` discarded it.

A, B, C, and D (range-dirty cache, incremental bake, tick index, checkpoint+tail) each fit **part** of that remaining cost. Shipping them as separate caches repeats D1.

### Decision

1. Introduce **`LoopContentResolution`** as the derivation owner for effective musical state. Primary APIs: `resolveState()`, `resolveWindow()`. `resolveNotes()` is a derived consumer, not the architecture center.
2. **G is the union of A+B+C+D**, not a different physics. Do not ship A then B then C as independent derived owners.
3. Vocabulary: **`RawMidiEvent`**, **`EditAction`**, **`ResolvedEvent`**. Do not collapse them into `Event`. Do not name the owner `Resolver` or `LoopContentResolver` (`NoteGeometryResolver` already owns live NOTE_EDIT overlap Resolution).
4. **Committed pass content is immutable; Active/Disabled is mutable history state.** Resolution uses the active pass set plus edit history.
5. After indexing/checkpointing, cost is proportional to **candidate events and affected state**, not historical pass count. Finding candidates must not walk every pass list.
6. **`resolveState(tick)` is a fundamental query.** Replay distance is bounded by checkpoints at `checkpointIntervalTicks`. Loop switch must not replay from tick 0.
7. For a fixed active pass set and edit history, resolution is **deterministic** and independent of cache state, chunk boundaries, or previous resolution order.
8. **Physical PSRAM chunks are not resolution boundaries.**
9. Build a **parallel native prototype**. Do not delete `materializeToEventVector`. Do not put resolution on `handleMidiInput`. Do not cascade `invalidateCaches` onto the prototype store.
10. Production consumers swap only after **three gates**: correctness vs materialize+reconstruct; complexity (`commit P(N)` does not traverse `P0…P(N-1)` except indexed affected regions); device worst-case latency on the `035414` class (no multi-second MIDI/OLED stall, no `VCACHE,full` on the normal path, no full materialization after commit).

### Rationale

DEC-016 already requires representation × interval. The missing owner is query-time resolution of active history, not another full flatten. D1 proved that an O(history) derived store plus global invalidation cannot survive. A parallel prototype with a hard scaling gate is safer than another cache on `passesMaterializedStore_`.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| A only (range-dirty `visualCache`) | Equal for display bars; idle slices still walk all pass lists |
| B only (true incremental bake) | Equal for derived MIDI current; D1 implementation was full flatten + invalidate, not a disproof of bake — but bake alone does not give loop-switch `resolveState` |
| C only (tick index on `CommittedEventRange`) | Equal for window find-cost; does not stop unrelated invalidation or boot-from-zero |
| D only (DEC-035 Layer C checkpoint) | Equal for load/switch; does not stop per-commit derived rebuild during overdub |
| F (stop at Layer D 3b) | Entry is already PASS; post-commit rebuild remains |
| E (CoW derived note versions) | Overlaps G’s range cache; content undo is already O(1) chunk-ref; do not build both |
| Keep optimizing `materializeToEventVector` | Explicit experimental boundary: prove whether resolution makes materialize unnecessary on the normal path |
| Name owner `Resolver` / `LoopContentResolver` | Collides with `NoteGeometryResolver`; NAMING prefers domain-owner nouns |

### Affected modules

Native prototype first: new `LoopContentResolution` headers/tests. Production later: `LoopMaterialization`, `LoopVisualCache`, `TrackPlaybackWindowBuild`, `establishOverdubSourceView` fallback only. Editor consume (2026-08-16): `NoteGeometryResolver` candidate source, `SelectNavigation`, `openNoteEditSession`, `ensurePlaybackMergedMidiEventsBuilt` overlay. `LoopPasses`, `StorageManager`, `NoteGeometryResolver` unchanged as owners.

### Amendment 2026-08-14 — Checkpoint is a jump point

Device per-bar `soundingAt` on the 68-bar / 1847-note loop is another O(history) derived store ([`225351`](../captures/session_20260814_225351.log) heap Critical). Short-loop sliced `lcr` remains PASS ([`225744`](../captures/session_20260814_225744.log)).

- A checkpoint MUST reduce historical replay work without becoming a proportional copy of the resolved loop.
- Checkpoint density is a performance parameter, not a semantic property of the loop (native 1 bar, device 8/16 bars, later adaptive — identical answers).
- `spans` plus a tick-ordered span-boundary index (start **and** exclusive-end) are sufficient for `resolveState` tail replay. The container need not be a PSRAM `std::multimap` (5.15).
- Split `prepareRebuildSpans` before treating RAM as the only stall. 5.7 probe is a selected loop **>63 bars** ([`115750`](../captures/session_20260815_115750.log) 139 bars accepted). Arm cap stays off. [`143009`](../captures/session_20260815_143009.log) complete `hist=2394` `walk=0`; project PASS; `startsByTick` emplace still fails 5.7. Do not change batch size to chase `emplace`. Do not start 5.1/5.2 or Stage 6 until cold-build latency passes.

### Amendment 2026-08-15 — 5.15b pick flat A

Native A (`append` + `stable_sort` by tick only) matches C `resolveState` and the materialize oracle, including equal-tick end-then-start. `walk=0`. Host: A index 5 µs vs C emplace 29 µs; A resolve not worse. 5.15c swapped the device gate to `spanBoundaries`. Device [`151450`](../captures/session_20260815_151450.log): `app=5490706` `sort=10202` `reb=13851136` `st=13045` `walk=0` (was `reb=104359946`). **5.15 complete.** Do not build B or A2. Do not reopen the resolver query model. 5.16c slices `RebuildPrepare` materialize at `kDeviceGateEventsPerSlice`. Device [`153920`](../captures/session_20260815_153920.log) `prep` PASS — no `loop_rem`; `idle_maint` 22–25 ms.

### Amendment 2026-08-15 — 5.17a–c pick flat A for TickIndex

`TickIndex::byTick` query contract is `tick ∈ [begin, end)` → Active `(passId, eventIndex)`. Native A (`TickEventEntry[]`, C-order append + `stable_sort` by tick only) matches C walk, wrap, disabled-pass skip, and `resolveWindow` vs the materialize oracle. Equal-tick insertion order preserved (NOTE_OFF then NOTE_ON at 192). `walk=0`. Host (94 entries): C emplace 23 µs, A 26 µs, A query 144 vs C 151. Pick **A**. Device [`161355`](../captures/session_20260815_161355.log): `iapp=4819607` `isort=27415` `win=13971` `st=3108` `walk=0` vs C [`155953`](../captures/session_20260815_155953.log) `idx` 50→120.6 ms. **5.17d PASS.** Do not build B or A2. Do not fold `recon` or `pair`. `TickIndex` and `StateCheckpoints` stay separate owners. 5.17e dropped `byTick`: `indexCapturePassEventRange` appends `tickEvents`; `findRawWindow` reads that list only.

### Amendment 2026-08-15 — 5.7a sliced append reserve

[`162630`](../captures/session_20260815_162630.log): `spans` notes/s 565→125 and `DFRAME` 1.003→1.591 s while paint stayed 12 ms. `appendSpanBoundaryEntries` reserved this slice only (`size+16`); `spans` already reserved `notes.size()`. Same growth on `idx` `tickEvents` (`size+8`). 5.7 leftover is that realloc, not `channelForNoteId` (constant per 8-note slice). `appendSpansFromNotes` reserves `2 * notes.size()`; `appendTickEventEntries` reserves remaining events in the pass. Device [`163942`](../captures/session_20260815_163942.log): reserve **PASS** (`app=2349` `iapp=202981`; `idx` 1310→1212 events/s). 5.7 `DFRAME` not closed: `spans` 930→169 notes/s. 5.7b: `appendSpansFromNotes` fills `channelByNoteId` from every NOTE_ON in `resolved` (first wins). Open notes whose OFF is outside the current 8 events still pair (`activeNoteStacks` / `openOnByPitch`) and still get channel. Native `test_stage57_recon_*` / `test_stage57_pair_*` / `test_stage57_span_channel_*` PASS. Do not rewrite `pair` / `recon`. Do not start 5.1.

### Amendment 2026-08-15 — 5.7b device FAIL

Device [`164922`](../captures/session_20260815_164922.log): first `spans` slice `loop_rem,idle_maint,14744010` filling `channelByNoteId` (PSRAM `unordered_map` `emplace`). `DFRAME` 15.702 s with consecutive `frameIndex`. After fill, `spans` ~1000 notes/s flat. Open-note contract held. Do not keep this map on device.

### Amendment 2026-08-15 — 5.7c flat channel lookup

Lookup contract from code: `NoteId` → first NOTE_ON channel in resolved C-order. Channel is the value, not a key or partition. First-wins unique on `NoteId` only. `channelByNoteId` is now `{noteId, channel}[]`: C-order append, `stable_sort` by `noteId`, unique keep-first, `lower_bound`. Device sequences `chan` / `csort` before `spans`. Complete line adds `capp=` / `csort=`. Device [`170024`](../captures/session_20260815_170024.log) **PASS**: `capp=12373` `csort=1779`; `dedup`→`chan` 11.55 ms; `csort`→`spans` 22.63 ms; no `idle_maint` `loop_rem`; `chan`/`spans` `DFRAME` 0.933–0.965 s. Pair `DFRAME` 1.277 s remains (different owner). `walk=0`. Do not rewrite `pair` / `recon`. Do not start 5.1.

### Amendment 2026-08-15 — Derived-index storage invariant

Not a new DEC. Device evidence from three LoopContentResolution derived indexes:

| Structure | Associative PSRAM | Flat A | Device |
|-----------|-------------------|--------|--------|
| `startsByTick` | `multimap` 224–413 ms / 8 inserts | `spanBoundaries` append+sort | 5.15 [`151450`](../captures/session_20260815_151450.log); after reserve [`170024`](../captures/session_20260815_170024.log) `app=1420` `sort=9439` |
| `TickIndex::byTick` | `multimap` 50–121 ms / small batches | `tickEvents` (5.17e **removed** `byTick`) | [`170024`](../captures/session_20260815_170024.log) `iapp=201044` is **bulk append total**, not map insert (was `iapp=4.82 s` before 5.7a reserve) |
| `channelByNoteId` | `unordered_map` 14.7 s [`164922`](../captures/session_20260815_164922.log) | `{noteId, channel}[]` append+sort+unique | [`170024`](../captures/session_20260815_170024.log) `capp=12373` `csort=1779` |
| `TickIndex::byNoteId` | `unordered_map` 5.346 s [`172927`](../captures/session_20260815_172927.log) | `{noteId, loc}[]` append+sort+unique keep-last | [`173842`](../captures/session_20260815_173842.log) `bn=225` `nsort=10003` |

**Invariant:** Derived indexes used by LoopContentResolution must use contiguous/bulk storage on the target device. Per-entry dynamic allocation into PSRAM associative containers (`std::map`, `std::multimap`, `std::unordered_map`) is prohibited on realtime-adjacent index construction paths. Where the query contract permits, indexes are flat PSRAM arrays built by append/bulk construction and ordered or uniqued in a bounded operation. The representation is selected from the query contract; flat storage is not an automatic replacement for every associative structure. Representation B (bucket/offset table) is not justified unless a measured flat query is too expensive.

This is **derived indexes + PSRAM + per-entry construction**. It is not “never use maps anywhere.”

**Still associative (not this invariant’s swap list):** pairing `openOnByPitch` (LIFO, retained after 5.18); `TickIndex::passById` (pass-count, not note-count). Do not fold `recon` into this rule. Do not flatten a LIFO stack.

5.15–5.18 establish this invariant **empirically**, not as a style preference:

| Contract | Representation |
|----------|----------------|
| ordered many-to-many range boundaries | flat + sort (`spanBoundaries`) |
| ordered event window | flat/bulk (`tickEvents`) |
| unique first/last-wins lookup | flat + sort/selection (`channelByNoteId` first-wins; `byNoteId` last-wins) |
| LIFO pairing state | associative/container semantics retained (`openOnByPitch`) |

The last row stops the rule from becoming “replace all maps with arrays.”

### Amendment 2026-08-15 — 5.7c FROZEN; pair is 5.18

Representation part of Stage 5.7 is **closed**. Do not reopen 5.7c.

| Gate | Status |
|------|--------|
| correctness (`walk=0`) | PASS [`170024`](../captures/session_20260815_170024.log) |
| complexity (no B) | PASS |
| derived-index RAM / construction (flat/bulk) | PASS — `spanBoundaries`, `tickEvents`, channel lookup, `byNoteId` |
| device latency on the whole idle gate | **5.1 PASS** [`173842`](../captures/session_20260815_173842.log) |

Successor: [`loop_content_resolution_pair_index_refinement.md`](Plans/loop_content_resolution_pair_index_refinement.md). Flatten from the **query**, not the container type. `byNoteId` is last-wins `NoteId → {passId, on, off}`. `openOnByPitch` is a per-pass LIFO stack keyed by **pitch only**. Measure which produces the stall before picking a representation. No 5.1. No Stage 6. No B. Do not rewrite `recon`.

### Amendment 2026-08-15 — Loop-internal channel is not a resolution key

Not a new DEC. Restates [DEC-033](#dec-033-overdub-overlap-ignores-per-note-channel) for LoopContentResolution.

A loop’s notes are scoped to that loop. MIDI output channel is `Track::midiChannel` (`Track::sendMidiEvent` remaps recorded 1–16). `NoteUtils::DisplayNote` has no channel field. Pairing, overlap, and sounding identity use `NoteId` / pitch + tick, not `event.channel`.

`openOnByPitch` stays pitch-only. Do not add `(pitch, channel)`. The 5.7c `channelByNoteId` index copies a stored MIDI byte onto `PresentNote.channel`; it is not a musical query. Do not reopen 5.7c to delete that copy in 5.18.

### Amendment 2026-08-15 — 5.18a pair instrument (no flatten)

`pair` reports `tot` / `bn` (`byNoteId`) / `op` (`openOnByPitch`) / `lk` (`find`) / `oth` plus entries, inserts, overwrites, pushes, pops, peak depth, openOn heap bytes. Device complete line `DIAG,lcr,pair`. 1 Hz `phase,pair` lines add `bn=` `op=` `lk=` `pk=`. Native last-wins and peak-depth tests. No header allocator probe (ITCM). Do not flatten until the 139-bar remasure names the expensive owner. No 5.1. No B. No `recon`.

### Amendment 2026-08-15 — 5.18a device: byNoteId owns pair

[`172927`](../captures/session_20260815_172927.log): `tot=5356927` `bn=5345535` `op=2433` `lk=1424` `oth=7535` `pk=1` `ins=2396` `ow=0`. Pair `DFRAME` 390→420 is **1.273 s** (consecutive `frameIndex`). `openOnByPitch` is not the stall. Next representation experiment is **last-wins flat `byNoteId` only**. Retain the LIFO stack. No 5.1. No B. No `recon`.

### Amendment 2026-08-15 — 5.18b last-wins flat byNoteId (native)

`TickIndex::byNoteId` query is `NoteId` → last `{passId, on, off}`. Storage is now `{noteId, loc}[]`: C-order NOTE_ON append, reverse-scan OFF during the unsorted walk, one `stable_sort` by `noteId`, unique **keep-last** (opposite of 5.7c channel keep-first). `appendNoteEvents` uses `lower_bound` after unique. Device sequences one `nsort` slice after all pair ranges, before `isort`. Sliced pair ranges do not unique every 8 events. Native last-wins vs keep-first tests. `openOnByPitch` stays a per-pass LIFO stack. No 5.1. No B. No `recon`. Do not flatten `openOnByPitch`.

### Amendment 2026-08-15 — 5.18b device PASS; 5.18 FROZEN

[`173842`](../captures/session_20260815_173842.log): `tot=7950` `bn=225` `nsort=10003` `op=2256` `pk=1` `ins=2396` `ow=0` `walk=0` `hist=2394`. Pair `DFRAME` consecutive `frameIndex` **0.980–1.026 s** (was 1.273 s [`172927`](../captures/session_20260815_172927.log)). `idle_maint` during pair 25.2 ms, no `loop_rem`. `nsort` 10.0 ms and `isort` 28.1 ms under 50 ms. Do not flatten `openOnByPitch`. Do not reopen 5.18.

### Amendment 2026-08-15 — 5.18 closed

The remaining Stage 5.7 device-latency violation was traced to `byNoteId` PSRAM associative construction. Replacing that construction with the minimum representation matching its last-assignment-wins query contract reduced `bn` from 5.346 s to 225 µs and total pairing from 5.357 s to 7.95 ms. `openOnByPitch` remains a LIFO stack because its mutation contract is not equivalent to a sorted/unique index. No new >50 ms construction slice was observed. Pair is frozen. Do not clean up `recon`, `byNoteId` naming, or `openOnByPitch` before the device realtime gate.

DEC-037 Stage 9 index work:

```text
correctness             PASS
complexity              PASS
derived-index RAM       PASS
flat/bulk construction  PASS
device latency
  spanBoundaries        PASS
  tickEvents            PASS
  channel lookup        PASS
  pair                  PASS
```

Next is the **whole-gate** 5.1 measurement, not another index. Production stays on materialize / 3b copy.

### Amendment 2026-08-15 — 5.1 idle-path device latency PASS

[`173842`](../captures/session_20260815_173842.log) complete LoopContentResolution idle gate on the 139-bar class (`hist=2394`, larger than `035414`). Production MIDI/display unchanged. The gate runs only from `processDeferredIdleMaintenance` when transport is not PLAYING / RECORDING / OVERDUBBING / STOPPED_RECORDING.

| Check | Worst in LCR window (20.67–52.43 s) |
|-------|--------------------------------------|
| OLED | consecutive `DFRAME` **1.034 s** (`frameIndex` +30, paint 9.9 ms) vs healthy after-complete **0.968 s** |
| MIDI | `midi_gap` **39.1 ms**; `idle_maint` **34.9 ms**; no `loop_rem`; `clockrate` 0 (transport idle) |
| `VCACHE,full` during LCR | none |

Not multi-second MIDI or OLED stall. **5.1 PASS.** 5.2 (overdub entry / `VCACHE,full` on the PLAYING path) is not this measurement. No Stage 6.

### Amendment 2026-08-15 — Stage 6 overdub must not cold-build LCR

Not a new DEC. Stage 9 device gates are complete. **5.2 PASS** [`180624`](../captures/session_20260815_180624.log) `begin_capture` **10050 µs** after restoring DEC-036 3b (copy authoritative `visualCache.notes`; no `markDisplayCachesStale` on `startOverdubbing`). Prior FAIL [`175544`](../captures/session_20260815_175544.log) **108979 µs**. Original 3b [`045556`](../captures/session_20260814_045556.log) **2214 µs**.

**Invariant:** Overdub **start and stop** MUST NOT cold-build `LoopContentResolution`. LCR construction belongs to idle/background preparation. Overdub entry consumes already-prepared derived state (prepared window when ready). Clarified 2026-08-17 (RC7): source view is `rebuildOverdubSourceView` (prepared window, else bounded `resolveWindow(passes)`). Display idle may still copy visual cache (`vch`). Do not copy visual cache into `overdubSourceViewNotes_`.

Content authority stays `LoopPasses`. Do not add a `LoopContent` type. Do not treat the 2214 µs 3b number as proof that LCR queries are faster — that number is a cache copy, not an LCR setup/query cost.

**Forbidden on overdub start/stop:**

```
start/stop overdub
  → create/rebuild LCR indexes
  → resolveState / full-loop materialize
```

That recreates [`175544`](../captures/session_20260815_175544.log) (`markDisplayCachesStale` → discard → materialize → reconstruct) under a new owner. Bounded `rebuildOverdubSourceView` is allowed (DEC-038 RC6/RC7).

**Acceptance (Stage 6 overdub path):**

| Kind | Bar |
|------|-----|
| Architecture | no synchronous full-loop work; no materialize; no reconstruct; no `VCACHE,full`; no cache invalidation on entry |
| Performance | `begin_capture` **< 3 ms** target; **< 50 ms** hard gate |

A 4–10 ms landing still proves the invariant if those architecture bars hold. Do not optimize the old 2214 µs number at the expense of the invariant.

Dirty-cache is **not** a license to cold-build LCR indexes from `startOverdubbing`. Idle slices (`6.2`) own LCR gather. Source-view miss uses bounded `LoopContentResolution::resolveWindow(passes)` (RC7), not a visual-cache copy and not `ensureLcrIndexCurrent()`.

Firmware production swap (`6.1`+) does not start until an explicit implement request. This amendment pins the invariant only.

### Amendment 2026-08-15 — Stage 6 consume-only; 6A / 6B / 6C experiment

Not a new DEC. Strengthens the previous Stage 6 amendment. 5.18 is **FROZEN**; derived-index construction on device is tractable (flat/bulk/sliced). The remaining question is **not** whether the LCR representation is viable.

**Question:** Does migrating remaining production consumers to the already-proven LCR owner recover original ~2–3 ms overdub start/stop, or is there another independent stop-path cost? Do **not** answer by wiring LCR into overdub.

**Two dimensions:**

1. **Entry is largely explained.** 3b [`045556`](../captures/session_20260814_045556.log) `begin_capture` **2214 µs** (copy prepared `visualCache.notes`). Restored production [`180624`](../captures/session_20260815_180624.log) **10050 µs** still under the 50 ms gate. The 3b architecture shows **< 3 ms is achievable when the committed visual/source representation is already prepared**. LCR does **not** yet show that it can improve on that copy. Do not claim “switching everything to LCR makes start < 3 ms.”
2. **Stop is the interesting path:** `commitCapturePass` → `notifyCommittedContentChanged` → `markDisplayCachesStale` → dirty bars → idle rebuild. LCR restores responsiveness **indirectly**: idle prepares derived state; overdub start/stop consume it and return.

**Invariant (strengthened):** Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state. It may only consume already-prepared derived state. `ensureLcrIndexCurrent()` (or any other ensure/rebuild helper) on that path is still a violation.

**Role:** `LoopContentResolution` is the **producer of prepared derived state**, not a replacement for `overdubSourceView`. `establishOverdubSourceView` stays the consumer via `rebuildOverdubSourceView`. Display idle may copy visual cache (`vch`). Source view does not.

**Preparation is established architecture** (cooperative / bulk / sliced). Device complete-path [`173842`](../captures/session_20260815_173842.log) / channel [`170024`](../captures/session_20260815_170024.log): `spanBoundaries` `app=1420` `sort=9439`; `tickEvents` `iapp=197743` `isort=28116`; channel `capp=12373` `csort=1779`; pair `tot=7950` `nsort=10003`. Do not rediscover this in the migration.

**Stage 6 experiment (no new DEC):**

| Slice | What | Oracle / fallback |
|-------|------|-------------------|
| **6A** | One dirty display range: `resolveWindow` → display projection instead of `CommittedEventRange` → `reconstructDisplayNotes`. Idle only. | Keep old path as oracle. Measure `resolveWindow`, projection, total slice, worst slice, `midi_gap`, `DFRAME`. |
| **6B** | Overdub stop: commit → mark **only affected ranges** → return. No materialize, no whole-loop reconstruct, no `VCACHE,full`. | Separate `stopOverdubbing` entry, `commitCapturePass`, bookkeeping, return-to-MIDI, first idle prep, display repaint, eventual consistency. |
| **6C** | Prepared LCR range → `overdubSourceView`. **Only after 6A/6B.** | RC7: miss uses bounded `resolveWindow(passes)`, not visual-cache copy. Score `begin_capture` against 3b **2214 µs**, not against 5.2 **10050 µs**. |

`< 3 ms` is a **regression target**, not an architectural promise. LCR’s job is to eliminate post-commit / full-rebuild machinery, not to make an already-cheap transition intrinsically faster.

Firmware 6A+ waits for an explicit implement request.

### Amendment 2026-08-15 — 6D incremental post-commit LCR maintenance (investigation)

Not a new DEC. Prepared LCR is not late at the overdub button. `Track::processDeferredIdleMaintenance` runs LCR only when STOPPED; visual-cache slices run while PLAYING; `deviceGateComplete` is one-shot; `commitPendingCapturePass` bumps `playbackRevision` so `preparedWindowReady` is false. Evidence: [`loop_content_resolution_stage9_handoff.md`](Plans/loop_content_resolution_stage9_handoff.md) § Why LCR is not ready.

**Pick:** investigate incremental maintenance of the **overdub-query index** after commit (**6D**). First experiment **6D.1**: one `OverdubPass` → `capturePasses` + `tickEvents` + stamp. Not all of LCR. Plan: [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md).

**Rejected:**

- **A** — re-arm the STOPPED cold-build on stamp mismatch. Repeats 30–60 s preparation. Does not meet PLAYING overdub-over-overdub.
- **B** — slice the existing full-history LCR build during PLAYING. That is another continuously maintained O(history) cache on the perform path.

**6C** stays consume-when-ready (`tryResolvePreparedWindow` when the stamp already matches). It does not address always-ready.

**6D is not capability letter C** and is **not** “LCR is now always live.” It is DEC-037 capability **B** for the overdub query. `openOnByPitch` stays a pairing-time LIFO stack. Checkpoints, `byNoteId`, edits, and undo are later 6D slices, not 6D.1.

**Gate before firmware:** native 6D.1 **FAIL** (one-vector sort/merge tracks H). **6D.2/6D.3 native PASS** (split history+delta; repeated overdubs track accumulated Δ, not H). **Production architecture gate posted 2026-08-15 — not approved.** Formal trigger: restamping `preparedWindowReady` after a PLAYING overdub commit. 6.0 vs commit-site sort is the approval pin. Preflight: `openspec/changes/loop-content-resolution/PREFLIGHT.md`. Production architecture stays untouched until that approval. `< 3 ms` is consume of already-prepared state ([`045556`](../captures/session_20260814_045556.log) 2214 µs copy), not a promise that switching to LCR is 2.2 ms.

**6.0 approved reading 2026-08-15:** overdub **button / source-view open** must not construct, sort, checkpoint, or resolve LCR. Bounded delta sort + restamp may run at `finalizeCommitSideEffects` after a committed `OverdubPass` when a prepared index already exists. A prepared index may become valid through that commit-site update, not exclusively through STOPPED `deviceGateComplete`. **6D.4 landed** (`publishPreparedOverdubPass`, session `delta`). Not all of LCR incrementally live.

### Amendment 2026-08-16 — Editor consumes prepared LCR around `selectedTick`

Not a new DEC. Pins the Editor consumer already drawn in DEC-037. Architecture: [`note_edit_selectedtick_lcr_resolution_architecture.md`](Plans/note_edit_selectedtick_lcr_resolution_architecture.md). **Work identity:** [`note_edit_hydrate_enhancement.md`](Plans/note_edit_hydrate_enhancement.md) — separate from remaining LCR 6.x firmware. Firmware not authorized. Wrap-move persist is **parked** (current rematerialize / session-store structure is part of the [`201446`](../captures/session_20260816_201446.log) defect); it is not a start gate.

**Overlap mapping withdrawn 2026-08-17:** pin 1 (`selectedTick` as the same-role origin as overdub `currentTick`) does **not** apply to overlap. Overlap is the selected/mover LinearSpan participant query. See amendment 2026-08-17 (NOTE_EDIT overlap). Select neighborhood around `selectedTick` stays.

**Problem:** NOTE_EDIT analyze still rematerializes the loop (`rematerializeEditView`, `visualCache.notes`, `collectEvaluationScopeNoteIds` over `liveStore`). On a 16-bar / ~1000-note loop the 16-bar piano-roll window is the whole loop, so windowed reconstruct does not shrink the working set. Overdub already consumes prepared `resolveState(tick)` / identity lookup (6E Path A; 6.0 consume-only).

**Decision:**

1. **`selectedTick` is the Editor query origin**, the same role `currentTick` / `playheadPhaseTick` has for overdub.
2. **Idle `visualCache` stays the piano-roll committed list.** LCR consume is select, overlap, and analyze only.
3. **Select encoder is a neighborhood around `selectedTick`.** Not a full-loop onset list. Not `resolveState(selectedTick)` — that is sounding-at-tick (overlap at S). Select is a bounded onset/navigation query on `tickEvents` / `spanBoundaries`, then identity geometry only if needed.
4. **Participating notes are found after the mover stops.** Last geometry input, then one completed `DisplayManager::update` as **trigger only**, then indexed overlap find vs the **current** mover span of this Move/Pitch action. Data source is prepared LCR ∪ `NoteEditCurrentState`, not the display frame’s `visualCache` rebuild and not playback refresh. Do not run that find on the fader stack. Do not reuse `kDeferredNoteEditPlaybackRefreshIdleMs`. Within that action, the overlap set **updates**: still intersecting → Active Shorten/Hide; no longer intersecting → `RestoreNote` to `committedSpan` and drop from the overlap delta (existing leave-restore). That is not DEC-030 sticky overlap end-of-participation (`Ended` after deselect). If the fader moves again before the action ends, repeat the find against the new span. Remaining Active rows after the action stay in the session delta.
5. **Selected note(s) are the incoming hold.** Same DEC-031/032 `[S, E)` Shorten/Hide as overdub. `NoteGeometryResolver` stays live overlap Resolution. Empty candidate set does not gather (overdub Gate 3). Prepared-hit lookup is indexed query → bounded identities → `appendNoteEvents`. Cost of `appendNoteEvents(NoteId)` is O(events of that id), not O(loop)×candidates. `resolveWindow` must not return a reconstructed whole-loop `DisplayNote` vector.
6. **Audition while the session is open:** `currentTick` sounds every settled this-session overlay row when the playhead crosses it — selected note, impacted overlaps, and earlier edits in this session (wrap-1-still-playing analog). Overlay those `NoteId`s onto the existing playback window. Do **not** full-replace `mergedEvents` from a whole-loop `sessionMidiEvents()`. Unrelated notes keep committed playback around `currentTick`.
7. **Session is overlay state, not a second loop.** Paint follows overdub compose: committed `visualCache` + this-session overlay. Overlay is the mover plus **remaining Active** participants of the current/last action, not every note that was ever overlapped. After settle, that overlay is the session delta for the **next** select/overlap (6D.4 analog). Do not insert it into LCR indexes (Path B). `EditSession.store` is not a display or query layer and must not be a full-loop rematerialize. `saveNoteEditPass` remains the content record at deselect/close.
8. **6.0 applies to NOTE_EDIT open and fader.** No LCR construct / sort / checkpoint / `ensure*` rebuild on those stacks. Prepared **hit** is the analysis authority. Prepared **miss** is a legacy compatibility path with today’s semantics — not a second analysis authority. Never Path B (live session geometry into LCR).
9. **Implementation stages are split.** Select neighborhood (work-shape gate) → overlap identities (`appendNoteEvents` bounded) → resolver consume + legacy miss → 4a open without full-loop analysis → 4b first select → 4c playback overlay (not `sessionMidiEvents()` full-stream replace) → integrated device gate. Do not redesign `rematerializeEditView` / `materializeToEventVector` / `EditSession.store` during this consumer migration. Device gates measure work shape, not only function-call absence.

**Rejected:**

| Alternative | Rejected because |
|-------------|------------------|
| New DEC | Editor was already a DEC-037 consumer; interval change is an amendment |
| `EditSourceView` / second note map | 6E rejected premapped maps; NAMING forbids a new domain noun |
| 16-bar `resolveWindow` then reconstruct all `DisplayNote`s | On a 16-bar loop that is the full cache cost under a new name |
| Select defined as `resolveState(selectedTick)` | Sounding-state is overlap at S; Select is onset/navigation |
| Full-loop select encoder via `tickEvents` | User pin: neighborhood only |
| Overlap find on every geometry apply | User pin: after motion stops, one display frame |
| Keep full session-store playback replace | `ensurePlaybackMergedMidiEventsBuilt` assigns all `sessionMidiEvents()`; audition only needs selected + participants |
| Miss path as a second analysis authority | Prepared hit is authoritative; miss is temporary legacy compatibility |
| Redesign `EditSession.store` / `rematerializeEditView` in this migration | Prove the consumer first; then ask which old machinery is unreachable |
| New DEC to delete session store | Apply scratch can remain; deleting the store is a later ownership question |

**Affected modules (when firmware is authorized):** `NoteGeometryResolver` candidate source; `SelectNavigation`; `openNoteEditSession`; `ensurePlaybackMergedMidiEventsBuilt` overlay. Owners unchanged.

### Amendment 2026-08-17 — 6C/6D closeout; playback gather is a separate work path

Not a new DEC. Closes the OpenSpec 6C recapture and 6D HITL rows on existing device evidence. Moves leftover 6.3 out of `loop-content-resolution` the same way 6.4 moved to hydrate.

**6C closed** as consume-when-ready. Native landed. Device: [`205928`](../captures/session_20260815_205928.log) `DIAG,lcr,6c` `begin_capture` **37747 µs**; [`210508`](../captures/session_20260815_210508.log) restamp hit **31128 µs**, third consume **312636 µs**. [`194643`](../captures/session_20260815_194643.log) RING dropped the first `6c`. 3b copy stays the fast path ([`045556`](../captures/session_20260814_045556.log) **2214 µs**). LCR window reconstruct on the button is not a 3b replacement. Do not treat 6C as “LCR beat 3b.”

**6D closed** as the overdub-query slice only. 6D.4 firmware + HITL: [`205928`](../captures/session_20260815_205928.log) PLAYING publish restamp (`6c` without a second `deviceGateComplete`); [`210508`](../captures/session_20260815_210508.log) undo miss → 3b `begin_capture` **120 µs**, then PLAYING publish `6c`. Not all of LCR live. A/B remain rejected.

**6.3 moved.** Long-loop playback gather is not remaining firmware in this OpenSpec change. Work identity: [`playback_gather_lcr_consume_enhancement.md`](Plans/playback_gather_lcr_consume_enhancement.md). Same 6.0 rule: no construct on MIDI / `handleMidiInput`. Miss keeps today’s gather. Firmware not authorized until that file is in CURRENT_WORK § Now implementing.

### Amendment 2026-08-17 — NOTE_EDIT overlap is selected-span participants

Not a new DEC. Withdraws mapping `currentTick` → `selectedTick` as the **overlap** origin. That mapping is the 16-bar analog (geometric interval around a tick, then reconstruct, then find overlaps). Sibling: [`overdub_participant_loop_content_architecture.md`](Plans/overdub_participant_loop_content_architecture.md) §5 NOTE_EDIT sibling.

**Decision:**

1. **Overlap** uses the same participant query as overdub, keyed by **selected / mover LinearSpan**. Identity is `NoteId` + LinearSpan. Phase 0b applies (`PresentNote` lacks `endTick`; wrap predicates not proven equal).
2. **Select** stays a `tickEvents` / `spanBoundaries` neighborhood around `selectedTick`. That is navigation, not participants.
3. **Open** must not rematerialize the loop to analyze.
4. Paint stays `visualCache` + `NoteEditCurrentState`. Path B stays forbidden. `NoteGeometryResolver` stays overlap Resolution.
5. Do **not** start hydrate Stages 1–5 from “prepared LCR × interval around `selectedTick`.” Firmware stays unauthorized until hydrate is in CURRENT_WORK § Now implementing.

**Rejected:** treating `selectedTick` as the overlap origin because overdub used `currentTick` for present-at-S ON discovery. Overdub hold is already `[S, E)`; NOTE_EDIT overlap is that hold analog from the first query.

### Amendment 2026-08-17 — PresentNote C++ rename

Not a new DEC. Executes the naming phase of [`overdub_participant_loop_content_architecture.md`](Plans/overdub_participant_loop_content_architecture.md): LCR present-at-S type and helpers are `PresentNote` / `PresentNoteVec` / `presentAt` / `notePresentAt` / `presentAtHoldOnly` / `displayNotePresentAtHold`. `isSoundingNoteOn` (pairing helper) and `ActiveNoteLedger` are unchanged. No ownership or transition change. Phase 1 observation firmware still not authorized.

### Constraints created

- Overdub button / source-view open must not synchronously construct, sort, checkpoint, or resolve LCR state; they may only consume already-prepared derived state (`ensure*` rebuild helpers included). Commit-site delta publish + restamp is the approved 6D.4 placement (**landed**; HITL PASS [`205928`](../captures/session_20260815_205928.log) / [`210508`](../captures/session_20260815_210508.log)).
- No second O(history) derived owner that `invalidateCaches` will discard.
- A checkpoint must not be a proportional copy of the resolved loop (per-bar full `soundingAt` fails this).
- Derived indexes must not per-entry-allocate into PSRAM associative containers on realtime-adjacent construction paths (5.15 / 5.17 / 5.7c). Representation follows the query contract; B only if a measured flat query is too expensive.
- `resolveNotes` must not become the playback primitive.
- Failure gate: if the prototype cannot show a materially better scaling model without another O(history) derived owner, stop and implement A+C on existing owners. A weak first tick index does not by itself disprove the architecture. Copying sounding state at every checkpoint does.
- Always-ready for the **next overdub query** is **6D** (6D.1 one-vector mutation FAIL; 6D.2 split PASS; 6D.3 repeated overdub PASS native; **6D.4 HITL PASS** [`205928`](../captures/session_20260815_205928.log) / [`210508`](../captures/session_20260815_210508.log)). Not all of LCR. Not a re-armed STOPPED cold-build (A) and not a sliced full-history build during PLAYING (B). 6C is consume-when-ready **closed** (same captures; 3b stays the fast path).
- NOTE_EDIT open / fader must not construct, sort, checkpoint, or resolve LCR (same 6.0 consume-only rule). Select is neighborhood `tickEvents` / `spanBoundaries` around `selectedTick`, not `resolveState`. Overlap participants are the selected/mover LinearSpan query (same class as overdub), not a window around `selectedTick`. Participating-note find runs after mover stop plus one `DisplayManager::update` as trigger; data source is LCR ∪ overlay, not `visualCache` rebuild. Intra-action leave restores (`RestoreNote`); it does not apply DEC-030 sticky overlap end-of-participation. Remaining Active overlay rows are the session delta for the next select/overlap/audition. Do not insert live geometry into LCR (Path B). Editor paint follows overdub compose: `visualCache` + neighborhood overlay. Audition overlays settled session `NoteId`s onto the playback window around `currentTick`. Do not full-replace `mergedEvents` from a whole-loop session store. Do not fill current-state from every visual-cache row. Prepared miss is legacy compatibility only. `appendNoteEvents(NoteId)` must not scan the whole loop per id.

### Related OpenSpec

`openspec/changes/loop-content-resolution/` (producer). Editor hydrate is a separate work path: [`note_edit_hydrate_enhancement.md`](Plans/note_edit_hydrate_enhancement.md). Playback gather is a separate work path: [`playback_gather_lcr_consume_enhancement.md`](Plans/playback_gather_lcr_consume_enhancement.md). DEC-036 change `loop-effective-event-source` remains for Layer D 3b closeout; D3/D4 stay out of that change.

### Migration notes

No SD format change. Materialize remains the production path until all three gates pass. Persisted checkpoints (DEC-035 Stage 6) reuse the in-RAM checkpoint **shape** after Stage 7; persist owner stays `StorageManager`.

---

## DEC-036 — Runtime effective event source for overdub

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `Loop` — incremental effective committed content; `establishOverdubSourceView` consumes range only  
**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](Plans/loop_layer_d_overdub_rebuild_architecture.md)  
**OpenSpec:** `openspec/changes/loop-effective-event-source/` (D1 + D2)  
**Evidence:** [`session_20260814_035414.log`](../captures/session_20260814_035414.log) — `begin_capture` 6.779 s; `set_state` 7 µs  
**Parent:** [DEC-035](#dec-035-loop-persists-content-only) Layer D runtime track

**Context:** Layer A closed the `UndoStacks` bundle walk. Overdub-open latency on a 68-bar / 3385-event loop is dominated by `establishOverdubSourceView` (`gatherCommittedEvents` + `reconstructDisplayNotes`). The overdub FSM transition is cheap; source-view acquisition is not. Display reconstruction must not gate overdub entry.

**Decision:**

1. Maintain an **incrementally updated** runtime effective representation of committed loop content, valid **before** `beginOverdubSession()` — updated on pass commit, undo/redo toggle, edit apply, and load complete; never built on overdub button press.
2. **Overdub entry must not require display reconstruction.** Forbidden at overdub open: full-loop `gatherCommittedEvents()`, full-loop `reconstructDisplayNotes()`, full visual-cache rebuild as prerequisite.
3. Effective store exposes **range query** (`range(window)`) for overdub source establishment (D2). Dependency: layered passes → effective store → tick range → overdub source — not effective store → entire `DisplayNote` vector → overdub.
4. Display is **eventually consistent** during overdub; MIDI capture is **immediately** active. Idle `slice_clean` may lag; blocked MIDI may not.
5. **Out of scope:** persisted checkpoint + tail (D3 / Layer C); range-first load publication (D4 / Layer D); post-stop `PlaybackFullMaterialize` cleanup (separate slice).

**Consequences:** Evolve `passesMaterializedStore_` from lazy full rematerialize to eager incremental maintenance. `establishOverdubSourceView` rewritten for windowed effective range. Device gate: `ODUB,begin_capture` < 50 ms at `035414` scale.

**Validation:** Native equivalence vs `passes.materialize`; overdub-entry counter guards; RC-K3 / overlap fixtures unchanged; device `035414` class.

---

## DEC-035 — Loop persists content only

**Date:** 2026-08-14  
**Status:** Accepted  
**Owner:** `StorageManager` persist; `Loop` content records; `TrackUndo` in-session undo until a later DEC  
**Plan:** [`loop_layer_history_persistence_architecture.md`](Plans/loop_layer_history_persistence_architecture.md)  
**OpenSpec:** `openspec/changes/loop-content-history-persistence/` (Layer A)  
**GitHub:** Task [#33](https://github.com/Lytrix/MidiLooper/issues/33); Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32)  
**Does not supersede:** [DEC-024](#dec-024-loop-owned-undo-ownership-direction) Phase 2 (move `GlobalUndoStack` Track → Loop). Stage 3b will be a new DEC that **replaces** that stack with derived Loop editing state.

**Context:** 64-bar overdub stop walks `DeferredSaveStage::UndoStacks` for ~1300 slices (`LoopUndoHistory` 4–16 s; `SlotMeta` the same walk). The loop file already stores the content those entries describe. A scoped `undo_TT_SS.bin` was withdrawn — it still serializes the duplicate payload.

**Decision:**

1. The Loop persists **content records only**. Undo/redo is runtime/editor behavior derived from ordered content plus grouping rules. Do not persist an undo stack, history cursor, history transition, or undo/redo record.
2. **Layer A (now):** prove content is sufficient (Stage 1); reconstruct load-time editing state while `GlobalUndoStack` remains in-session authority (Stage 2); then delete `UndoStacks` / `admitLoopUndoHistory` (Stage 3). Stage 3 is gated on Stage 2: reboot must reconstruct equivalent undo depth (`U:nn`) from content.
3. A persisted content prefix must be sufficient to define the effective Loop. If Stage 1 finds a gap, add immutable **content metadata** on the content record. Do not restore persisted `stateRaw` as hidden undo persist, and do not assume `active = records <= tip` until the audit survives grouping, companions, geometry, and `dropRedoBranch`.
4. Persisted content and runtime-resident content are different. Chunk-pool occupancy bounds how much history stays in RAM.
5. **Recorded, not authorized now:** publication uses `isRangeAvailable(PlaybackWindow)`, not `COMMITTED` (Layer D; conflicts with `lazy-slot-hydration` — reassessment before that firmware). Clear-as-unlink is Set last-state (`lastUnlinkedSlotLink`) (Layer B). Journal is append-structured with redo-tail reclamation (Layer B). Do not use **Source** as a domain noun.

**Consequences:** First persistence-level lever is deleting the `UndoStacks` stage. New floor is remaining `LoopPersist`, not cheap persist. In-session undo is unchanged until Stage 3b. DEC-024 Phase 1 filter stays until then.

**Validation:** Native Stage 1 audit + Stage 2 load-time editing-state fixtures. Stage 3 device: no `PERS,bundle` at UndoStacks scale; post-stop MIDI gap bounded by remaining `LoopPersist`; reboot `U:nn` matches pre-reboot tip depth.

---

## DEC-034 — Overlap shorten seals at the user-triggered commit

**Date:** 2026-08-13  
**Status:** Accepted  
**Plan:** [`note_edit_overlap_shorten_commit_seal_bugfix.md`](Plans/note_edit_overlap_shorten_commit_seal_bugfix.md)  
**Withdraws:** slice B of [`note_edit_resolver_authority_contracts_refinement.md`](Plans/note_edit_resolver_authority_contracts_refinement.md) § Stage 7.5

**Context:** `buildCommitOverlapRowsFromCurrentState` skipped an overlap participant while `participatingNoteVisibleOverlapTailInProgress` held — the mover's live span still overlapping the participant's `committedSpan`. At a deselect that is true by construction, since the mover is parked on the note it just shortened. `commitEditAction` then reloads the session store from committed passes, so a skipped row is lost, not postponed: the shorten either vanished (display reverted to the pre-shorten length) or landed on the next commit against an unrelated focus ([`204700`](../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2`, then @166.848 `Length 10 960–1247` while the mover was already note 14). Slice B was misattributed: [`225025`](../captures/session_20260807_225025.log) shows note 9 at 534 ticks, mover parked at 2832, commit `2544–2831` @26.291 and reselect `len=287` @36.927 — the correct truncation. "Stub was 47" was the mover's length.

**Decision:** An overlap shorten seals at the commit the user triggers. Active overlap closure defers **leave-restore** only, never persistence. The host note's tail after the mover stays discarded (truncate to `moverStart − 1`); no split, no restore-on-leave.

**Owner:** Unchanged — `buildCommitOverlapRowsFromCurrentState`. The predicate keeps its original home in `determineConstrainedGeometryTargetNoteIds` and `appendOverlapTargetActions`.

**Validation:** Native `test_commit_seals_overlap_length_while_mover_covers_committed_span_225025` and `test_deselect_commit_seals_overlap_shorten_under_parked_mover_204700` (canonical rows equal parity rows). `test_deselect_clears_overlap_participation_without_geometry_restore_232118` unchanged — `Ended` participation still blocks the row.

---

## DEC-033 — Overdub overlap ignores per-note channel

**Date:** 2026-08-12  
**Status:** Accepted  
**Plan:** [`realtime_incremental_work_overdub_note_change_bugfix.md`](Plans/realtime_incremental_work_overdub_note_change_bugfix.md)

**Context:** `Loop::accumulatePendingNoteChangesForIncomingNote` filtered overlap candidates with `noteIdHasChannel`, which scanned every `overdubSourceViewEvents_` row per same-pitch reconstructed note. S0e measured that scan as `notepair` 98 ms on a 4257-event / 2109-note loop ([`204221`](../captures/session_20260812_204221.log)). `NoteUtils::DisplayNote` has no channel field; the lookup existed only to recover channel from events.

**Decision:** A loop's committed notes are already scoped to that loop. Overdub G2 overlap resolution matches NOTE_EDIT: geometry uses pitch + tick window, not a per-note channel check. `noteIdHasChannel` is removed. A stored same-pitch note whose recorded channel differs from the incoming channel participates in overlap resolution.

**Previous owner:** File-local `noteIdHasChannel` in `LoopPendingNoteChange.cpp`.

**New owner:** None — filter deleted. Incoming `channel` still stamps the pending Add / Shorten / Hide row.

**Validation:** Native `test_pending_shorten_ignores_recorded_channel` — source note on channel 1, incoming on channel 2, same pitch and overlapping ticks, produces Shorten.

---

## DEC-032 — Overdub / EditPass unification reassessment

**Date:** 2026-08-12  
**Status:** Accepted — **G2**  
**OpenSpec:** [`archive/2026-08-12-overdub-pass-overlap-resolution`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/)  
**Review:** [`UNIFIED-PASS-ARCHITECTURE-REVIEW.md`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/UNIFIED-PASS-ARCHITECTURE-REVIEW.md)

### Decision

**G2:** Unify the **resolution** model now; keep **dual storage** as transitional encoding.

- MIDI overdub and NOTE_EDIT share canonical Add/Shorten/Hide geometry.
- Session accumulates one pending logical delta; commit encodes via capture chunks (Add) + edit rows (Shorten/Hide).
- Dual seal / `OverdubPassAdded`+`editPassIds` is encoding, not the semantic abstraction.
- U1/U2 persistent unification explicitly out of scope for this change.

### Consequences

- Phase 2 firmware unblocked under G2.
- DEC-031 describes transitional seal/undo encoding under this pin.
- Design §19: resolution unification ≠ storage unification.

---

## DEC-031 — Overdub overlap encode pending buffer to EditPass

**Date:** 2026-08-12  
**Status:** Accepted (encoding under DEC-032 G2)  
**OpenSpec:** [`archive/2026-08-12-overdub-pass-overlap-resolution`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/)  
**PREFLIGHT:** [`PREFLIGHT.md`](../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/PREFLIGHT.md)

### Decision

Overdub overlap encode is **C → A**:

- Session-local **pending overdub-operation buffer** on `Loop` (not a timeline pass).
- At stop: Adds → existing `OverdubPass` chunks; Shorten/Hide → existing `EditPass` rows.
- Do not extend `OverdubPass` with Shorten/Hide fields; do not write `EditPass` mid-session.
- One logical undo: extend `OverdubPassAdded` to carry companion `editPassIds` (field already on `UndoEntry`); apply/redo/reclaim/GUS honor those ids.
- Grouping identity: `OverdubPass.id` — no new overdub-operation identifier.
- Commit order: publish OverdubPass → save EditPass rows → push undo.
- When `overdubSourceView` is established, `shouldRestoreCommittedOverlapOnOverdubStop` / `removeOpenCaptureNoteOn` is **not** authoritative.

### Alternatives rejected

- **B** — ops on `OverdubPass` + materialize/schema rewrite.
- **Pure A mid-session EditPass writes** — live materialize / cancel / undo boundary races.
- **Two undo entries** — breaks one logical overdub undo.
- **New UndoEntryKind** — unnecessary when `editPassIds` exists on `OverdubPassAdded`.

### Consequences

- Phase 2 slice 1: pending buffer + native geometry bridge.
- Phase 2 slice 2: seal, undo bundling, restore gate, GUS round-trip for companion ids.
- No loop SD schema bump expected; GUS wire extends for `OverdubPassAdded` companions.

---

## DEC-030 — Sticky overlap end-of-participation on current state

**Date:** 2026-08-08  
**Status:** Accepted  
**Plan:** [`note_edit_resolver_authority_contracts_refinement`](../Plans/note_edit_resolver_authority_contracts_refinement.md) §11 step 5.3

**Context:** Sticky deselect cleared Focus `changedOverlapNoteIds` while leaving Visible shortened `currentSpan` (no flash restore). Participation membership could not be derived from geometry alone; the latch was a second authority.

**Decision:** Encode sticky end-of-participation as `NoteEditOverlapParticipationType::{Active,Ended}` on `NoteEditCurrentNoteState` (mirrored on `ParticipatingNoteState`). `clearChangedOverlapParticipationWhenInteractionCleared` sets Ended without rewriting geometry. `currentStateRowIsOverlapParticipant` returns false for Ended. Shorten/Hide/Restore re-enters Active. Focus latch remains dual-write until §11 step 5.5 deletion.

**Previous owner:** End-of-participation expressed only by forgetting `changedOverlapNoteIds`.

**New owner:** `NoteEditCurrentNoteState.overlapParticipation`.

**Validation:** Native fixtures for sticky clear → Ended, Shorten reactivates Active; display/inventory readers use current-state participation.

**Completion (2026-08-08):** §11 step 5.5 removed `changedOverlapNoteIds` and live-store membership reconcile; participation is current-state only (smoke HITL `025807`, `030432`, `032118`).

---

## DEC-029 — NoteEditCurrentState owns NOTE_EDIT editable note state

**Date:** 2026-08-07  
**Status:** Accepted  
**OpenSpec:** [`note-edit-current-state`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/) — normative [`openspec/specs/note-edit-current-state/`](../../openspec/specs/note-edit-current-state/)

**Context:** Same-pitch NOTE_EDIT overlap captures (`session_20260807_021939`, `session_20260807_021022`) showed stable `NoteId` identity was correct, but current editable geometry was reconstructed from `EditSession.store`, `baselineMap`, focus fields, overlap scratch, display order, and live-store scans. Stale committed baseline geometry affected later edits; stopgap guards (`sessionMovedNoteSpans`, overlap skip guards) prevented correct current-span overlap edits.

**Decision:** During NOTE_EDIT, `EditSession` owns editable note state through `NoteEditCurrentState`. `baselineMap` remains committed transaction baseline. `EditSession.store` becomes the canonical event projection of `NoteEditCurrentState` for playback preview, serialization, compatibility, and parity checks; it is not editable-state authority.

**Previous owner:** Current editable NOTE_EDIT geometry was effectively owned by `EditSession.store` plus scattered reconstruction helpers.

**New owner:** `NoteEditCurrentState` inside `EditSession`.

**Migration strategy:** Split read/projection access from mutation APIs, add read-only current-state build/verify, prove projection parity, migrate readers, migrate writers, migrate undo/redo and commit diffs, then remove compatibility state and direct projected-store mutation.

**Compatibility period:** `EditSession.store` remains projected MIDI event storage while readers/writers migrate. Legacy store-diff builders may remain as parity checks only.

**Removal trigger:** Delete `sessionMovedNoteSpans`, session-moved overlap skip guards, live-store geometry authority in resolver/action builder/commit, and direct NOTE_EDIT writes through `track.editAwareMidiEvents()` after native fixtures and HITL edit retest pass.

**Validation:** Native tests for 021939, 021022, repeated A/B move undo/redo commit, selection reorder, hidden/deleted/added rows, projection invariant, accessor gate, undo snapshot invariant, and commit parity; then firmware build and user-approved HITL edit retest.

**Completion (2026-08-08):** Phase 8 closeout — native 969/969; HITL PASS via capture matrix in [`PHASE8_CLOSEOUT.md`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md). **Archived** `2026-08-08-note-edit-current-state`.

---

## DEC-028 — EditSessionAction geometry pipeline (Phase 1 native)

**Date:** 2026-08-04  
**Status:** Accepted  
**OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/)

**Decision:** Implement Phase 1 as pure native modules — `EditSessionAction` types, D17 orchestrator helpers, `analyzeEditSessionInteractions`, `groupEditSessionInteractionsByTarget`, `resolveConstrainedGeometry` — without wiring `NoteMovementUtils` yet. Brownfield `overlapNotes` guards remain until Phase 4 retires scratch.

**Rationale:** Derived geometry pipeline needs testable analyze/resolve boundaries before replacing imperative restore-first paths. UIP Edit projection (`IntervalProjection`) is the D20 pre-analyze hook — no `normalizeWrapToLinear` module.

**Tests:** `test_edit_session_interaction`, `test_resolve_constrained_geometry`.

---

## DEC-027 — Deferred job scheduler north star

**Date:** 2026-07-18  
**Status:** Accepted; amended 2026-07-18 (vocabulary: `DeferredJobScheduler`, Job owns state, Workspace reserved for product)  
**Context:** Lazy restore, deferred persistence, and future display/export work share the same interruptible shape. Byte-chunk restore alone does not scale; a second ad-hoc scheduler per domain would duplicate policy (budget, focus, demote, reclaim). Early draft names (`CooperativeScheduler`, job Workspace) collided with product Workspace and did not name scheduler scope.

**Decision:**

1. **North star:** **`DeferredJobScheduler`** owns execution of all non-realtime, interruptible, resumable firmware work. Domain managers submit jobs and own job logic.  
2. **Phase A (now):** implement time-budgeted resumable **`LoadLoopJob`** under `StorageManager::runDeferredFrame()` — no new top-level scheduler type yet. See [`deferred_storage_time_budget_scheduler_enhancement.md`](Plans/deferred_storage_time_budget_scheduler_enhancement.md).  
3. **Phase B:** introduce `DeferredJobScheduler::runFrame()` and migrate execution ownership out of StorageManager (formal ownership trigger; OpenSpec before firmware).  
4. **Focus change:** demote jobs; do not cancel (preserves SD/parser progress). Cancel only for memory pressure discard, SD failure, unload, set close.  
5. **Commit:** atomic only — job execution state → Commit → published loop. No partial loop states. Job owns its unpublished fields until Commit (no separate Workspace/Context/Scratch type for job temp).  
6. **Preemption policy:** High normally preempts; finish current job when estimated remaining work &lt; scheduler slice (not %-complete).  
7. **Vocabulary:** Job; `DeferredJobScheduler`; product **Workspace** and **Slot** reserved. Avoid Workspace/Scratch/Context/Slot for scheduler resources.  
8. **Phase A pins:** load frame before display; deferred save stays separate under StorageManager; `applySnapshot` may exceed one slice in MVP.

**Consequences:** Phase A may ship without a new Manager. Phase B requires an OpenSpec change + architecture gate before `DeferredJobScheduler` lands. Extends DEC-026 Commit model; does not replace it. DEC-020 “cooperative budget-driven” persistence remains shipped history; the unified deferred owner type name is `DeferredJobScheduler`.

**References:** [`deferred_job_scheduler_architecture.md`](Plans/deferred_job_scheduler_architecture.md), DEC-026, DEC-020 (persistence budgets as prior art).

---

## DEC-026 — Commit-centered lazy slot load

**Date:** 2026-07-18  
**Status:** Accepted  
**Context:** Full-set boot drain is SD-bound (~10.5 s / 26 slots). Architecture review froze Commit (verb) over Publish; audible-first boot + on-demand deferred load.

**Decision:**

1. All runtime-visible loop changes occur through **Commit** (semantics, not a single helper).  
2. Hydration lifecycle `UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY` is architectural; storage flexible. COMMITTED is sufficient for play/edit/display; DERIVED_READY optional.  
3. Boot sync-commits audible set only; MVP may keep existing sync load. Runtime priorities: audible + explicitly requested. No speculative prefetch. Load-while-PLAYING interactive = later phase.  
4. OpenSpec change: `unified-commit-lazy-slot-load`. Architecture plans frozen under `docs/Plans/unified_publish_pipeline_*`.

**Consequences:** New branch for firmware; Phase 1 rename Publish→Commit; Phase 2 audible boot before cooperative session requirement.

**References:** [`unified-commit-lazy-slot-load`](../openspec/changes/unified-commit-lazy-slot-load/), [`unified_publish_pipeline_deferred_lazy_loading_architecture.md`](Plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md), DEC-021 (audible-ready gate amends full-set boot drain practice).

---

## DEC-024 — Loop-owned undo ownership direction

**Date:** 2026-07-08  
**Owner:** `TrackUndo` / display sidebar / multi-loop slots  
**Status:** Accepted — Phase 1 shipped; Phase 2 deferred

**Context:** Sidebar `U:nn` showed track-wide `GlobalUndoStack.cursor` and did not update when switching loops. Multi-loop specs describe per-loop undo; Track is becoming a coordinator while Loop owns Capture, Passes, Playback, and Persistence.

**Decision:**

1. **Phase 1 (now):** Keep `GlobalUndoStack` on `Track`; add `TrackUndo::*ForLoop` compatibility layer — filter depth and gate undo/redo when stack tip `slotIndex` matches selected loop. Display resolves `Loop&` once.
2. **Phase 2 (future):** Move `GlobalUndoStack` member from `Track` to `Loop`; target API `loop.undo()` / `loop.undoDepth()`. Requires design session before implementation.

**Consequences:** `U:` reflects selected loop pass-undo depth. Slot/Record gestures remain thin wrappers. Phase 2 touches persistence footer, `PassReclaim`, and redo tests.

**Reference:** [`docs/Plans/loop_undo_ownership_refinement.md`](Plans/loop_undo_ownership_refinement.md)

---

## DEC-022 — Runtime bundle save tail integrity

**Date:** 2026-07-07  
**Owner:** M5 boot load / `continuous-runtime-persistence` gate  
**Status:** Accepted; **shipped** 2026-07-07

**Context:** Fresh record after workspace wipe failed cold boot with `Current workspace load failed` despite `runtime.bundle.bin` tail SAVE token. Deferred save reopened `runtime.bundle.bin.tmp` with `seek(file.size())` after loop-slot writes; when a prior temp file was larger than the new bundle body, footer/undo stacks appended after a stale gap — bundle parse failed silently at epilogue.

**Decision:**

1. **Remove** existing `runtime.bundle.bin.tmp` at `beginDeferredSaveJob` before writing.
2. Track **`metaBundleWritePos`** on meta temp close; **reopen at that cursor**, not `File::size()`.
3. Emit **`BOOT load_stage=…`** / `#CAP,BOOT,load_stage,…` at bundle parse failure sites.
4. Native **`test_runtime_bundle_boot_load`** — snapshot skip round-trip + stale-gap footer regression.

**Consequences:** Cold boot shows `Current workspace loaded successfully` on device. Phase 2 persistence queue (DEC-020) remains paused until full 64+64 HITL passes.

---

## DEC-020 — Continuous runtime persistence architecture

**Date:** 2026-07-07  
**Owner:** OpenSpec `continuous-runtime-persistence`  
**Status:** Accepted

### Problem

64+64 HITL (`20260707_192649`) shows deferred save starved for the entire overdub window because `isCaptureActiveForPersistence()` blocks all slices during capture. Chunk pool and internal heap fill; overdub stop faults before `PERS,result`. Heap routing fixes (`runtime-derived-representation-heap`) improve seal heap but do not fix transport-gated persistence.

### Decision

Replace transport-gated save with **invariant-driven, cooperative budget-driven capture-chunk persistence**:

- Pass ownership ≠ storage ownership; pass may stay open while chunks persist.
- Separate lifecycles: runtime (`Free → Recording → Sealed`) vs persistence (`Not scheduled → Queued → Writing → Persisted`).
- **ChunkManager** (`LoopEventStore`) owns sealing, refs, allocation, reclamation.
- Persistence queue: seal-order admission, exactly-once enqueue, seal-order drain.
- Runtime recording/playback always precede persistence; persistence is cooperative and budget-driven.
- Memory reclaim independent of persistence completion.
- Scope v1: append-only capture-pass storage only; note edit / undo snapshots unchanged.
- Phased rollout: diagnostics (Phase 0) → ownership → queue → scheduler → mid-pass → recovery → HITL.
- Park persistence/stop-path patches on `runtime-derived-representation-heap`.

### Rationale

Architecture checkpoint: ownership and state transitions change — not a stop-path bugfix. SD writer is already chunk-granular; gap is scheduling. Implementation guided by invariants, not ad-hoc patches.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Remove transport gate first (scheduler before queue) | No persistable sealed-chunk work during capture until queue exists |
| More stop-path flush/defer patches | Does not drain SD during overdub; starves pool |
| Mandate CurrentSet v6 layout Option A | Invariants over implementation; format may evolve |

### Affected modules

`LoopEventStore`, `Loop.cpp`, `StorageManager`, `StorageLoopIo`, `PersistenceBudget`, `main.cpp`

### Constraints created

- Phase 0 diagnostics before scheduler behavior change; validate 64+64 HITL assumptions first.
- No note-edit continuous persistence in v1.
- Extend `StorageManager` / `LoopEventStore` — no new top-level manager classes (DEC-008).

### Related OpenSpec

[`openspec/changes/continuous-runtime-persistence/`](../../openspec/changes/continuous-runtime-persistence/)

### Migration notes

Supersedes persistence starvation workarounds on `runtime-derived-representation-heap`. Guide: [`docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md`](Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

---

## DEC-021 — Defer inactive loop slot restore at boot

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change (M5)  
**Status:** Accepted; **shipped** 2026-07-07

**Context:** Cold boot stack overflow (`DACCVIOL`) during synchronous restore of all 8×8 loop slot payloads plus full undo snapshot bodies from SD. `workspace-session-persistence` had listed eager 8×8 load as a non-goal deferral; boot stack depth now requires it.

**Decision:**

1. **Split current set restore from SD** into two steps: (a) load current set bundle (transport + slot metadata + undo metadata), (b) restore loop slot payloads from `slots/loop_TT_SS.bin`.
2. At boot, queue **every** track/slot with a verified SD loop payload (up to 64) into `processDeferredLoopSlotRestore`; **priority orders restore only** — admission does not depend on playback layer or saved index flags. *(Amended 2026-07-09 — [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) D21; was: enabled/active/selected only.)*
3. At boot, read undo stack **metadata** only (`readGlobalUndoStackMetadataFromFile`); hydrate snapshot bodies per track in idle (`processDeferredUndoSnapshots`) or before first undo.
4. Emit `#CAP,BOOT,ram1,...` after restore; extend `stabilizeBootMemoryAfterLoad` to trim undo when heap or pool free is below reserve.

**Consequences:**

- Supersedes workspace-session-persistence non-goal “all 8×8 at boot” for stack safety.
- Play entry uses `loop.midiEvents()` in `ensurePlaybackWindowBuilt`; transport start skips `updateAllTracks(0)` when no capture is pending.

**References:** DEC-019, [`m5_sd_load_extmem_routing_handoff.md`](Plans/m5_sd_load_extmem_routing_handoff.md), `loadCurrentSetBundleAndActiveLoopSlots`. **Amendment:** DEC-025, [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) D21.

---

## DEC-025 — Split focus: playing / preview / pending

**Date:** 2026-07-09  
**Owner:** [`slot-performance-interaction`](../openspec/changes/slot-performance-interaction/) (D18–D21)  
**Status:** Accepted

**Context:** While transport runs, slot UI mixed preview (piano roll, edit) with playing (audible MIDI, bar/16th LEDs). Boot admission for deferred restore still filtered non-active slots (DEC-021 pre-amendment).

**Decision:**

1. **Preview slot** = `selectedSlotIndex` (no separate `previewSlotIndex` field). Updates immediately on slot peek / launch queue.
2. **Playing slot** = `activeLoopIndex`. While transport running, changes only via **committed playback transitions** (launch commit, capture finalize, quantized record start, layer-hold commit, post-clear restore).
3. **Pending slot** = `pendingSlotIndex` until `SlotQuantization` commit. Performance short-press launch uses **`LoopEnd`** to preserve phrasing.
4. **Display:** preview playhead at destination `loopStartTick`, flashing until playing catches up; **LED phase** stays on playing slot.
5. **`slotHasLoopContent`:** true when RAM has data or SD has verified payload.

**References:** [`slot-performance-interaction/design.md`](../openspec/changes/slot-performance-interaction/design.md) D18–D21, DEC-021 amendment.

---

## DEC-019 — SD load path extmem routing (M5 spike)

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change  
**Status:** Accepted (spike documented); **Step 1b adopt-on-load shipped** 2026-07-07; hardware boot validation pending

**Context:** M2 routed runtime published flat to `SessionMidiEventVec`. After 64+64 HITL failure, device internal heap reached 0 bytes; clear and deferred save blocked. Reboot restored broken 64+64 state via recovery checkpoints. Code review: `deepCloneChunkRefs` in `Loop.cpp` flattens each pass to internal-heap `MidiEventVec` on `restorePassesSnapshot` and undo restore; load also calls `rebuildVisualCacheFromPasses()` synchronously.

**Decision:**

1. **Document** load-path gap as M5 spike in OpenSpec (`spike_sd_load_extmem_routing.md`) — not part of M1–M4 ship criteria.
2. **Target state:** undo pass clone uses extmem-first flat; **SD load adopts chunk refs** (`adoptPersistedSnapshot`) without deep clone; defer visual rebuild on load per Phase C idle policy; evaluate lazy slot load at boot.
3. **M4 archive** remains gated on 64+64 HITL; quarantine + M5 implementation may be required for reliable re-test after failed long runs.

**Consequences:**

- Spec delta ADDED scenarios under `internal-heap-external-memory-routing` (marked spike).
- Implementation tasks tracked in `tasks.md` § M5.

**References:** DEC-016, DEC-018, [`spike_sd_load_extmem_routing.md`](../../openspec/changes/runtime-derived-representation-heap/spike_sd_load_extmem_routing.md), [`64bar_regression_commit_analysis_enhancement.md`](Plans/64bar_regression_commit_analysis_enhancement.md).

---

## DEC-018 — Admission current heap; derived-rep OpenSpec consolidation

**Date:** 2026-07-07  
**Owner:** runtime-derived-representation-heap OpenSpec change  
**Status:** Accepted

**Context:** July HITL proved 64-bar record stop→PLAY works at 8 KB RAM1 but deferred save never dispatches because `processDeferredSaveState` gates on frozen `admissionHeap` from record stop. Separately, `passesMaterializedStore_` lazy flat still uses internal heap; 64+64 verifier misses `#CAP,ST,OVERDUBBING,PLAYING` when capture ring overflows under MO burst. Cursor plans `64bar_regression_commit_analysis_b1378b37` and `heap_recovery_16bar_ebfaa9cd` were never converted to OpenSpec.

**Decision:**

1. **Consolidate** root-cause analysis, architecture, and tasks into OpenSpec change `runtime-derived-representation-heap` (no heap↔PSRAM FIFO).
2. **Admission:** gate deferred-save **dispatch** on current `getInternalHeapFreeBytes()`; stop-path `admissionHeap` is telemetry only.
3. **HITL floor:** `record_stop_min_free_ram2_bytes` default **0** (telemetry); optional warn at 12 KB — long-run PASS is heartbeat + core transitions + persistence, not stop-entry floor.
4. **Derived reps:** published flat and playback window on `SessionMidiEventVec`; tier-A `#CAP` flush priority under long overdub.
5. **Bisect gates** remain parked (DEC-017); re-open selective 64-bar gates after M1–M4.

**Consequences:**

- [`docs/Plans/64bar_regression_commit_analysis_enhancement.md`](Plans/64bar_regression_commit_analysis_enhancement.md) links to the OpenSpec change.
- User-local Cursor plans marked superseded (not deleted).

**References:** DEC-016, DEC-017, commit `cdd9c2b`, [`openspec/changes/runtime-derived-representation-heap/`](../../openspec/changes/runtime-derived-representation-heap/).

---

## DEC-017 — Skip long HITL/capture gates; implement runtime redesign

**Date:** 2026-07-07  
**Owner:** runtime architecture track (`docs/Authority/Architecture/`, Phase A→C in [64bar_regression_commit_analysis_enhancement.md](Plans/64bar_regression_commit_analysis_enhancement.md))  
**Status:** Accepted

**Context:** 64+64 commit bisect and serial capture gates are slow, unreliable across SD/firmware version mismatches, and duplicate evidence already in archived `captures/` (June PASS vs July FAIL). Partial Phase A shipped in `d635296` (boot + 16-bar record). Remaining blocker is playback materialize on PLAYING entry (H6) — an architecture/scheduling fix, not another bisect loop.

**Decision:**

1. **Skip** as implementation gates: save-bypass HITL, commit bisect at anchor SHAs, `validate-64x64` HITL, UIP Phase 5.5 HITL matrix — until runtime Phase A→C is complete.
2. **Keep** archived serial evidence in `captures/` and investigation plans as **historical** reference only; do not block progress on new long captures.
3. **Implement** runtime invariants per DEC-016: Phase A → Phase B → Phase C (see [64bar_regression_commit_analysis_enhancement.md](Plans/64bar_regression_commit_analysis_enhancement.md)).
4. **Verify** with `pio test -e native` and short manual smoke (boot, 16-bar record); re-open long HITL only after Phase C if needed.

**Consequences:**

- [CURRENT_WORK.md](Runtime/CURRENT_WORK.md) priority is runtime redesign, not bisect.
- Bisect helper `scripts/run_64bar_bisect_anchor.sh` is parked, not maintained.
- UIP Phase 6 / overlap resume stays blocked until runtime phases ship.

**References:** DEC-016, [next_session_handoff_overdub_uip_architecture.md](Plans/next_session_handoff_overdub_uip_architecture.md), commit `d635296`.

---

## DEC-016 — Runtime architecture four-layer model

**Date:** 2026-07-07  
**Owner:** `docs/Authority/Architecture/` (conceptual); brownfield build owners per layer (`Loop`, `Track`, `EditManager`, `DisplayManager`, `IntervalProjection`)  
**Status:** Accepted

**Context:** The 64-bar PLAYING-window regression investigation showed display, playback, and LED paths each deciding when to rebuild timeline data. UIP (DEC-015) unified interval math but not representation ownership, revision chains, or consumer scheduling. Investigation detail must not live in permanent architecture docs.

**Decision:**

1. **Four layers** (orthogonal): **Capture Storage** → **Derived Representations** → **Interval Projection** → **Runtime Request** (representation × `TickInterval` → consumer result).
2. **Derived representations** are not consumer-owned caches. Each documents: owner, dependencies, revision, invalidation, build policy, consumers. Prefer the term *derived representation* in new docs; reserve *cache* for true memoization (e.g. COW materialized flat).
3. **Intervals are consumer-agnostic** — one `TickInterval` (e.g. bars 8–24); playback, display, edit, and LED interpret the same range. Interval projection remains in `IntervalProjection` (DEC-015); it does not own storage or representations.
4. **Revision chain:** storage mutation → event representation → downstream display/playback representations. Consumers validate staleness; they do not call peer rebuild APIs on hot paths (e.g. `ensureVisualCacheBuilt` from `MidiLedManager` during PLAYING).
5. **Scheduling:** document **responsibilities** (owner, policy, defer vs immediate). A dedicated scheduler is optional implementation — not an architectural requirement.
6. **NOTE_EDIT exception:** `NoteEditSession.store` is a live overlay on passes during edit (Tier-2 playback audition via `sessionMidiEvents()` / `sessionPreviewRevision_`) — not folded into a single loop event representation.
7. **Doc split:** permanent model in `docs/Authority/Architecture/`; regression bisect/evidence in `docs/Plans/*_bugfix.md`; concrete patches in `docs/Plans/*_refinement.md`.

**Consequences:**

- Agents load [RuntimeArchitecture.md](Authority/Architecture/RuntimeArchitecture.md) for display/playback/LED read paths before changing rebuild behavior.
- PLAYING/overdub hot-path work defers full display representation rebuild (see [overdub_start_playing_window_hot_path_refinement.md](Plans/overdub_start_playing_window_hot_path_refinement.md)).
- Future features (clip launch, multi-window, LTS) add a representation, an interval source, or a consumer — not parallel cache systems.
- Code rename (`VisualCache` → representation naming) is deferred; docs use architecture terms with brownfield mapping table in [DerivedViews.md](Authority/Architecture/DerivedViews.md).

**References:** DEC-015, [unified-interval-projection/design.md](../openspec/changes/unified-interval-projection/design.md), [overdub_start_64bar_playing_window_regression_bugfix.md](Plans/overdub_start_64bar_playing_window_regression_bugfix.md).

---

## DEC-015 — IntervalProjection Stage 1 / Stage 2 split

**Date:** 2026-07-05  
**Owner:** `IntervalProjection` (`include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`)  
**Status:** Accepted

**Context:** Display, playback, and edit each duplicated loop-wrap math. `edit-session-action-geometry` would add a third path (`normalizeWrapToLinear`).

**Decision:**

1. **Module home:** `IntervalProjection` in `Utils/` — single engine for all wrap/linearization consumers.
2. **Stage 1 (`generateEquivalentIntervals`):** Pure math — bounded ±k·`loopLength` shifts from canonical `TickInterval` spans; k bounds derived from `ProjectionContext.window` intersection; no `ProjectionType` policy.
3. **Stage 2 (`selectProjectedInterval` / `selectProjectedIntervalsForDisplay`):** Consumer selection only (Playback / Display / Edit / reserved Timeline).
4. **Phase helpers** (`tickPhaseInLoop`, `noteRelativeTick`, `noteStorageTick`, projection-cycle helpers) centralized in `IntervalProjection`; `TickPhase.h` and `SelectNavigation` thin-wrap for brownfield call sites.
5. **`window` is `TickInterval` input frame (D23)** — never `ProjectedNoteInterval`; projection preserves `noteId` across k copies (D8).

**Consequences:** Phases 2–5 migrate consumers to supply `ProjectionContext` only; overlap pipeline blocked until Phase 5 HITL. Native gate: `test_interval_projection`.

**References:** `openspec/changes/unified-interval-projection/`, DEC-013 linear storage.

---

## DEC-014 — Dual normalization boundaries (micro vs macro)

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, NOTE_EDIT commit path  
**Status:** Accepted

### Problem

Single `normalizeWindow` at every boundary blurred live-interaction consistency with persistent canonical commit.

### Decision

- **Micro:** `normalizeWindow` on **edit closure set** at `publishDependentFaderLatch` — local geometric consistency for faders/projection; not sole persistent canonical authority.
- **Macro:** `normalizeAll` at `commitAllPendingNoteEditActions` — full-store canonical invariants, undo snapshots, pass readers. MUST NOT be skipped.
- **Closure set:** seed modified `NoteId`s → paired on/off, overlap participants, wrap interactors; no UI window/selection as scope.
- **Playback during edit:** Tier 2 `sessionMidiEvents()` — verification only, no merge overlay.
- **Set window:** drives F1/F2 range; full-loop window → wrap at fader extremes; partial-window slide deferred.

### Consequences

- Phase 2 wires closure-set computation + dual hooks before HITL 152335.
- OpenSpec: `linear-loop-tick-storage`, `note-edit-modification-session`, `note-edit-fader-feedback`.

---


## DEC-013 — Linear loop tick validate vs normalize

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, `LoopEventValidation` (`include/Utils/`)  
**Status:** Accepted

### Problem

Loop MIDI storage mixed modulo ticks, wrap-pair geometry, and projection — causing edit move cutoff and loop-stretch display inflation.

### Decision

- **Invariants** — pure boolean predicates in `LoopEventValidation`; MUST NOT mutate storage or call normalize.
- **Conversion rules** — pure transforms in `LoopTickNormalize`; ONLY place legacy wrap-pair / synth-off shapes become canonical linear spans.
- **Normalize timing** — boundary-based only (not mid-pipeline, not on SD load). See DEC-014 for micro/macro split.

### Consequences

- `validateAndCleanupMidiEvents` logs canonical failures and performs orphan removal only (no synth loop-end insert on idle).
- OpenSpec: `openspec/changes/linear-loop-tick-storage/`.

---

## DEC-012 — StorageSession persistence state model

**Date:** 2026-06-29  
**Owner:** `StorageManager` (persistence)  
**Status:** Accepted

### Problem

Revision persistence and overlay work (~30 commits) left ~80 anonymous statics in `StorageManager.cpp`. Overlay coordination used imperative `PersistencePhase` assignments (8 sites) alongside bool FSM flags. Sprint naming (**pipeline**, **prompt**, **saveThenLoad**, **LoadRequestGate**) reads as prose and blurs the display/backend split.

### Decision

1. Introduce **`StorageSession`** struct — RAM aggregate of storage **jobs**, owned exclusively by **`StorageManager`** (DEC-008).
2. Jobs: `currentWorkspaceSave`, `revisionCommit`, `revisionLoad`, `setBrowserNavigation`, `bootRecovery`.
3. Revision load lifecycle: **requested → held** (`heldForWorkspaceDirty`) **→ dispatched** (`pending` / `inProgress` / `stage`). Flag **`loadAfterRevisionCommit`** for commit-then-load path. Remove **`revisionLoadPipelineActive`** when derived phase lands (Tier 2).
4. APIs use **request / confirm / cancel / dispatch**; rename away from `*DirtyPrompt*` / `*Pipeline*` / `*Staged*` on backend surfaces.
5. Drop **`LoadRequestGate`**; use `RevisionLoadPolicy::shouldHoldRevisionLoadRequest(bool workspaceDirty)`.
6. **Derived** coordination phase (Tier 2): `AwaitingRevisionCommit`, `RevisionLoadActive`, `RevisionCommitActive` — not stored on session.
7. Display overlay names stay on **`DisplayManager`**; no `StorageOverlaySession` or `*Intent*` struct names.
8. Implementation tiers: 0 snapshot/tests → 2 derived phase → 1 struct migration → 3 TU split before `transport.bin` / `global.bin`.

### Rationale

Matches **EditSession + EditManager** pattern (session struct + coordinator) without a new Manager. Reuses established **request**, **pending**, **inProgress**, **stage**, **job**, **deferred** vocabulary. Keeps front/back naming split.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| `SaveSessionManager` / `StorageHandler` | DEC-008; Handler suffix is event routing, not FSM ownership |
| `SaveSession` / `PersistenceSession` namespace as owner | Ownership ambiguity; collides with rejected Manager name or new top-level noun |
| `StorageOverlaySession` | **Overlay** implies display scope; backend is load/save jobs + navigation |
| `*Intent*` subgroup | New metaphor; not in repo suffix vocabulary |
| `LoadRequestGate` / **Gate** suffix | Only used in 3.7 sprint; queue vocabulary (**held**, **requested**) already fits deferred save |
| Keep imperative `PersistencePhase` only | Desync risk across 8 assignment sites; tested derivation is safer |
| Move drill navigation to `DisplayManager` now | Would be ownership transfer; defer — `StorageManager` already mutates `NavigationState` |

### Affected modules

`StorageManager`, `StorageSession` (new), `RevisionLoadPolicy`, `SetBrowserOverlayPolicy`, `DisplayManager` (getters), `MidiButtonActions`, native tests

### Constraints created

- No parallel persistence owner (DEC-008).
- Tier 0–2 must not reshape deferred/revision FSM **step logic** during active overlay tasks except naming/migration agreed in handoff.
- HITL/serial wire strings may keep legacy names until explicitly migrated.

### Related OpenSpec

`set-revision-persistence`, `load-save-overlay-display-regression`

### Migration notes

Handoff: [`docs/Plans/storage_session_state_refactor_handoff.md`](Plans/storage_session_state_refactor_handoff.md). Builder starts Tier 0.

---

## DEC-011 — Bias toward progress

**Date:** 2026-06-29  
**Owner:** Process (`ARCHITECTURE_REASSESSMENT.md`, agent workflow)  
**Status:** Accepted

### Problem

Governance risked becoming a bottleneck — agents stopped for full preflight/reassessment on normal extension work (new methods, display updates, bug fixes).

### Decision

**Default: continue implementation.** Decision ladder: extend owner → reuse extension point → follow DECISION_LOG → reassess only on [formal triggers](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) → else implement. Max one reassessment per session; if rejected, return to implementation. Lightweight inline preflight for non-trigger work; full PREFLIGHT only when triggered.

### Rationale

Governance exists to detect **structural** change, not to approve every edit.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full preflight always | Too heavy; slows bug fixes and display work |
| No triggers / trust agents | Loses structural guardrails |
| Stop on “feels unclear” | Indefinite planning loops |

### Affected modules

Agent workflow, PREFLIGHT modes, ARCHITECTURE_REASSESSMENT trigger list

### Constraints created

>3 files alone is not a trigger — must cross ownership. Explicit “not triggers” list documented.

### Related OpenSpec

N/A

### Migration notes

Supersedes implicit “always stop for 3+ files” interpretation.

---

## DEC-010 — Ownership evolution protocol

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/ARCHITECTURE_RULES.md`)  
**Status:** Accepted

### Problem

Strict ownership rules risked freezing architecture. Agents responded with permanent adapters, duplicated state, and shadow Managers instead of explicit transfer.

### Decision

**Ownership transfer protocol** in ARCHITECTURE_RULES: reassessment + migration plan + documented compat layer + removal schedule + approval + DECISION_LOG. Temporary delegation/adapters allowed; parallel permanent ownership forbidden.

### Rationale

Architecture must evolve without drift or workaround layers that never leave.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Never allow ownership change | Forces shadow owners and duplicated fields |
| Allow transfer without removal schedule | “Temporary” compat becomes permanent |
| Adapters without reassessment | Same as shadow ownership |

### Affected modules

All modules; template `OWNERSHIP_TRANSFER.md`; reviewer/builder/architect roles

### Constraints created

Compat layers require creation date, removal condition, maximum lifetime. Search owner + extension points before transfer.

### Related OpenSpec

N/A

### Migration notes

Example path: surface-agnostic Actions (DEC-005) should use this protocol when moving input ownership off `MidiButtonActions` only.

---

## DEC-009 — Runtime state vs roadmap split

**Date:** 2026-06-29  
**Owner:** Process (`docs/Runtime/`)  
**Status:** Accepted

### Problem

`PROJECT_STATE.md` mixed active work, future milestones, and next actions. Agents could implement roadmap items (D13, JamRecorder) thinking they were current scope.

### Decision

Split `docs/Runtime/` into **PROJECT_STATE** (execution context), **CURRENT_WORK** (now / not now / completion), **ROADMAP** (future only, never implementation authority). Agents load PROJECT_STATE + CURRENT_WORK before planning; ROADMAP optional.

### Rationale

Operational state must be separable from sequencing information to prevent speculative scaffolding.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Single PROJECT_STATE file with sections | Agents still skimmed roadmap sections as tasks |
| ROADMAP in DELIVERABLE_TRACKING only | Session start does not load that file by default |

### Affected modules

Agent workflow, session closeout, PREFLIGHT loaded docs

### Constraints created

No future milestones in PROJECT_STATE. Implementation must appear in CURRENT_WORK § Now implementing.

### Related OpenSpec

N/A

### Migration notes

Post-M7 sequence moved from PROJECT_STATE to ROADMAP.md.

---

## DEC-008 — Authority conflict resolution

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/README.md`)  
**Status:** Accepted

### Problem

Authority ordering defined precedence but not conflict handling. Example: OpenSpec proposes `SaveSessionManager` while ARCHITECTURE_RULES assigns persistence to `StorageManager` and discourages new Managers — agents had no deterministic resolution.

### Decision

Explicit conflict matrix in `Authority/README.md`. OpenSpec owns **behavior**; ARCHITECTURE_RULES owns **structure**. On conflict: **STOP**, architecture reassessment, user approval, update docs, then code. Prefer extending existing owner over new Manager.

### Rationale

Prevents architecture drift, endless replanning, and partial “stub” implementations that bypass ownership.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| OpenSpec always wins | Would redefine architecture through tasks without review |
| Architecture always wins | Would make specs unimplementable without reassessment path |
| Implement and fix later | Partial implementations become production debt |

### Affected modules

Agent workflow, OpenSpec proposals, PREFLIGHT authority conflict check

### Constraints created

No code on authority conflict until reassessment approved. No stub Managers to unblock specs.

### Related OpenSpec

N/A — applies to all changes

### Migration notes

Worked example: `SaveSessionManager` → extend `StorageManager` or approved new owner + DECISION_LOG.

---

## DEC-007 — Historical decision reuse enforcement

**Date:** 2026-06-29  
**Owner:** Process (`docs/DECISION_LOG.md`, agent workflow)  
**Status:** Accepted

### Problem

Agents recreated rejected abstractions, helpers, and ownership patterns because historical decisions were not mandatory reading before implementation.

### Decision

Mandatory **DECISION_REVIEW** + PREFLIGHT § Similar Historical Decisions before firmware edits. Structured **DECISION_LOG** at `docs/DECISION_LOG.md`. Reviewer rejects duplicate abstractions and ignored DEC-### entries.

### Rationale

Planning protected architecture but did not force explicit reuse/challenge of prior conclusions as project history grows.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on agent memory across chats | Unreliable; tabs close |
| Changelog-style decision doc | Conflates shipped code with architectural choices |
| Block all redesign | Legitimate overturn via reassessment + supersede entry required instead |

### Affected modules

Documentation and `.cursor/rules/Agent-Context-Workflow.mdc` only

### Constraints created

New architecture decisions MUST be logged. Supersede — never delete. Challenge via ARCHITECTURE_REASSESSMENT.

### Related OpenSpec

N/A

### Migration notes

`docs/Runtime/DECISION_LOG.md` redirects here. Entries DEC-001–DEC-006 migrated to structured format.

---

## DEC-001 — Load/save overlay confirm control

**Date:** 2026-06-29  
**Owner:** `SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`  
**Status:** Accepted

### Problem

Load/save overlay needs a single confirm gesture without conflicting with record, scroll, or note-edit bindings.

### Decision

Overlay confirm = **Edit short (note 38)** or **encoder short** only.

### Rationale

Scroll uses REC/PLAY and track buttons; NOTELEN is suppressed or bound to note-edit semantics elsewhere. Edit short and encoder are already the user-facing “commit row” affordance in overlay docs.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| NOTELEN short for confirm | Conflicts with note start/end and delete/create in edit; overlay suppresses NOTELEN for other roles |
| REC/PLAY short for confirm | Used for list scroll down in overlay |

### Affected modules

`SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`, README overlay table

### Constraints created

No new confirm binding without surface-agnostic Actions refactor for base module.

### Related OpenSpec

`set-revision-persistence` (set-browser-overlay)

### Migration notes

Reopen when base-module-only confirm needed without encoder — extend Actions layer first, do not add a third confirm note without reassessment.

---

## DEC-002 — Set revision vs flat SavedSet snapshot

**Date:** 2026-06-29  
**Owner:** `StorageManager`  
**Status:** Accepted

### Problem

Need durable Sets on SD without disrupting live **Current** workspace during performance.

### Decision

**Current** = mutable auto-saved workspace. **Save** = append immutable **Set revision** (REVPK) via deferred FSM. Current unchanged after Save.

### Rationale

Performers keep playing from Current; revisions are catalog snapshots for recall. Flat replace-on-save breaks flow.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Flat SavedSet as primary model | Superseded by revision catalog (`currentset-savedset-storage-layout` parked) |
| Save clears Current | Violates “music keeps running” intent |

### Affected modules

`StorageManager`, `SetRevisionCatalog`, `SavedSetCatalog`, overlay browser

### Constraints created

No silent revert to flat SavedSet-only layout. Schema changes require OpenSpec + migration notes.

### Related OpenSpec

`set-revision-persistence`, `workspace-session-persistence` (SUPERSEDED.md for old M3/M4)

### Migration notes

Parked: `currentset-savedset-storage-layout`. Reopen only via new OpenSpec change if revision model fails field validation.

---

## DEC-003 — D13 jam recording ordering

**Date:** 2026-06-29  
**Owner:** Timeline / Phase 3 (not yet implemented)  
**Status:** Accepted

### Problem

Phase 3 jam capture was scoped before timeline prerequisites existed, causing architectural drift.

### Decision

Ship order: **JamRecorder (JamAction) → M10 → D13 arrangement capture last**. Pool-budget and M8 edit precede jam capture.

### Rationale

Capture depends on stable passes[], undo, and slot infrastructure already shipped; D13 OpenSpec started too early.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Early `jam-recording` OpenSpec | Parked — prerequisites missing |
| Implement capture from `phase-3-multi-loop.md` alone | Slots yes, capture no; R1–R5 unresolved |

### Affected modules

Future: `Track`, `TrackManager`, jam state fields (read-only until JamRecorder)

### Constraints created

No D13 wiring in production paths until JamRecorder + M10 scoped. See DELIVERY_RULES hard guards.

### Related OpenSpec

Parked: `archive/20260617-parked-jam-recording-d13/`

### Migration notes

Reopen when `jam-recorder` change proposed and M10 design exists.

---

## DEC-004 — Record/overdub stop validation

**Date:** 2026-06-29  
**Owner:** `Loop`, `Track`, `LoopStopFinalize`  
**Status:** Accepted

### Problem

Long loops cannot afford full-loop validate or SD I/O on record/overdub stop.

### Decision

Stop path = `finalizeLoopAtStop` + wrap window only. Full validate via deferred idle maintenance. Persistence via `requestDeferredSaveState`.

### Rationale

Documented in LOOP_MIDI_STORAGE guide and archived pool-budget / memory-headroom specs; HITL and native tests depend on bounded stop path.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full `validateAndCleanupMidiEvents` on stop | Latency and memory on long loops |
| SD write on stop hot path | Violates deferred save architecture |

### Affected modules

`Loop.cpp`, `Track.cpp`, `StorageManager`, `main.cpp` idle drain

### Constraints created

Hot path: no full flatten/validate on stop. No new stop-side persistence without reassessment.

### Related OpenSpec

`openspec/specs/timeline-epochs/`, `long-record-memory-headroom/`, `storage-loop-io/`

### Migration notes

Normative — challenge only via architecture reassessment and spec amendment.

---

## DEC-005 — GPIO base module vs DROID-only actions

**Date:** 2026-06-29  
**Owner:** Input / `MidiButtonActions` (future: shared Actions)  
**Status:** Accepted

### Problem

DROID development left `ButtonManager` dormant while actions live only in MIDI paths.

### Decision

DROID is extension only. Encoder + 4 GPIO must remain capable in principle. Revive GPIO via **shared Actions layer**, not parallel implementations.

### Rationale

PROJECT_INTENT decisions 1–4; base module is the product hypothesis.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| DROID-only core workflow | Violates intent litmus “base module alone” |
| Wire `ButtonManager` without shared Actions | Duplicates `MidiButtonActions` |

### Affected modules

`ButtonManager` (dormant), `MidiButtonActions`, `GpioButtonManager`

### Constraints created

No new MIDI-only action paths for core record/undo/nav. New input surfaces must map to existing Actions.

### Related OpenSpec

`note-edit-session-undo-gpio` (archived GPIO geometry)

### Migration notes

Reopen when surface-agnostic Actions refactor is explicitly scheduled.

---

## DEC-006 — Agent context harness vs chat history

**Date:** 2026-06-29  
**Owner:** Process (`docs/Authority/`, `docs/Runtime/`)  
**Status:** Accepted

### Problem

Design exclusions and rejected alternatives were lost when chat tabs closed, causing repeated debates.

### Decision

Durable memory in `PROJECT_STATE.md`, this `DECISION_LOG.md`, OpenSpec, and mandatory historical review before implementation.

### Rationale

Chat threads are not searchable and agents do not load them by default.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on open chat tabs | Not portable across sessions |
| Full transcript archive in docs | Noise; extract decisions only |

### Affected modules

Documentation and agent workflow only (no firmware)

### Constraints created

New architecture decisions MUST be recorded here. Session closeout when alternatives discussed.

### Related OpenSpec

N/A

### Migration notes

Supersedes informal “keep the tab open” workflow.

---

2026-09-16 task-2.3 follow-up: keep canonical prefix and base preparation in
one shared resumable continuation used by live overdub and Stop. Reuse the
existing VisualCache owner, quantum and pass/view workers. Disabled overdub
visual reconstruction cannot block this prework merely because paint is dirty.
Stop estimates use owned prewarmed capacity, not the final tail size, and count
the incoming capture in the pass horizon. This implements the approved earlier
preparation direction; it does not close the manual reentry or deadline gates.

2026-09-16 user-approved continuous preparation: overdub punches in/out of the
existing record/play/stop process; it does not define the lifetime of derived
canonical work. Reuse completed passes after a proven append or same-revision
snapshot publication. Retain final companion normalization through successful
stop via existing scratch storage and a prewarmed descriptor array. Preserve
per-pass order, owner/generation checks and selected-source immutability. Use
existing idle owners and workers; keep timing and repeated-punch HITL gates open.


**DEC-064 implementation follow-up (2026-09-16):** the existing stop worker now
copies canonical rows at Q=32 into an explicit Capture backed by eight pre-reserved
physical chunks. Raw chunks remain unchanged until one-chunk retirement turns.
Prepared pass receipts distinguish live capture retirement from external-source
transfer. Active transfer checks its admitted directory before source mutation
and can transfer already-owned chunks at full pool. These changes establish
single-head copy ownership only; they do not authorize removal of reentry guards.
The descriptor FIFO, causal predecessor and full session/preview/undo/snapshot
lifetime migration remain required. Generic runtime reset still owns shared frozen
staging; its isolation remains part of that migration. No task/HITL gate closes.


**DEC-064 reset-lifetime follow-up (2026-09-16):** the admitted stop receipt now
pins the existing runtime scratch until cleanup. Cache reset denies destructive
reset, requests STOPPED recovery and preserves accepted source/copy/companion
state. Track cache invalidation defers its generation change until that recovery.
This closes the admitted singleton reset hole, not FIFO ownership. Phase-by-phase
native reset regressions preserve accepted data. Before additional simultaneous
interval reservations, take an intermediate on-device prewarm/memory and copy/Stop
timing checkpoint, prompted by the recorded 2026-09-14 eager-prewarm PSRAM failure.
Added Tier-A output-credit diagnostics use counters, not a PSRAM allocator walk.
Reentry requirements and all task-2.3/task-5.3 acceptance gates stay open.


**DEC-064 hardware follow-up (2026-09-16):** authorized track-5/slot-1 checks pass
normal cleanup, focused Stop silence and three restart-content cycles. Host
interruption verification now binds the pending job's grain identity/epoch and
cleanup, preserving the earlier obsolete-transition report separately. Twenty-four
host regressions pass. Dense capture rebooted/disconnected with no retained cause;
its result is INCONCLUSIVE_CAPTURE, not a firmware-fault diagnosis or PASS. No
FIFO/reentry or task-5.3 acceptance is claimed. See active FOCUSED-STOP-HITL.md.


**DEC-064 crash-evidence follow-up (2026-09-16):** user confirmed spontaneous
reboot at record-pass/overdub overlap. Native overlap regression passes but does
not explain the device failure. Installed core clears CrashReport when printed;
startup now snapshots the flag and renders once to Serial plus SD. Previous
crash=0 is not exculpatory evidence. Diagnostic build uploaded; human reproduction
pending on the already authorized scratch slot. No acceptance gate waived.

**DEC-064 diagnostic stack follow-up:**

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**DEC-064 descriptor integration / dense validation:**

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

**DEC-064 latest verification:**

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.
