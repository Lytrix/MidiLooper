# Three reported playback issues — approved repair scope

Status: native/build/review verified for the scoped repairs; device acceptance and captured CanonicalTail wait remain open.

Owner explicitly approved expanding the current successor/display repair scope to pending-stop finalization, odd-bar rolling playback and snapshot admission, preserving crash protection and stopped-track button semantics. Baseline: feature/unified-rolling-playback-path, pre-existing dirty successor/display changes retained. No SD overwrite, reset, upload or Git delivery authorized by this repair.

## Evidence / decision review

- Capture: `captures/session_20261024_231958.log`, bounded excerpts only.
- VERIFIED: `MidiButtonActions::handleToggleRecord` itself logs "MIDI Button A"; it chooses playback for committed content on a stopped track. Repeated stop presses instead report `stop_job_pending`, including interval 3 phase 9 and interval 9 phase 5. A channel collision is not established by the supplied notes.
- VERIFIED pre-fix: `serviceRollingPlaybackWindow` rejected odd-bar lengths before seed; seed also required two-bar divisibility. Now repaired with loop-local, terminal-clamped coverage; see odd-bar packet.
- VERIFIED pre-fix: snapshot publication charged 32 KiB slab + 8 KiB margin against a 32 KiB internal floor. On EXTMEM targets `snapshotSlabMalloc` uses external memory only (fallible, no internal fallback). `LoopSnapshotChangeSet` vectors already use external-first allocation. The observed 68 KiB heap-tail reading therefore parked publication at the historical 72 KiB threshold despite no pool refusals; the gate is now tier-correct.
- Historical review: DEC-075 publication admission/park (retained; amend tier-specific margin only), DEC-116 one reader, DEC-156 paid absolute occurrence. Relevant authority: Architecture/Snapshots.md, Architecture/Playback.md, Architecture/ExecutionBounds.md; guide INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md distinguishes tiers and forbids live allocator scans. No new owner, queue, buffer, lifetime, persistence format or scheduler.

## Snapshot admission execution packet

Owner: Loop::stepPendingSnapshotPublish → admitSnapshotPublicationTurn → MemoryMonitor::admitOperationMemory; existing snapshot allocation failure/park and later root commit unchanged.

Mutation: charge the slab to internal memory only on targets whose slab allocator actually uses internal memory. EXTMEM targets retain 8 KiB internal working margin plus 32 KiB floor; external pool recency refusal and fallible slab allocation/parking remain authoritative. No probes, scans, allocation or extra work added to admission. Non-EXTMEM targets retain slab + margin.

Oracle before production edit: real publication starts with old immutable root and queued overdub change. At 68 KiB with external-only slabs, publication must complete and contain old + added note once. Below floor+working margin it must remain pending with old root/identity/journal unchanged; on recovery complete unchanged. Existing injected allocation-failure park/recovery case remains required. A pure tier-policy check pins 8 KiB external-only vs 40 KiB internal slabs. Native memory gate must exercise the same numeric predicate rather than its historical unconditional-success stub; forced denial remains supported.

Validation: focused test_loop_take_survival + test_memory_pressure_policy, full `pio test -e native`, `pio run -e teensy41-capture-serial`, strict OpenSpec and diff checks. Native logical/content verification does not establish physical OLED freshness, real pool headroom or device timing. Device gate remains open; no flash without separate approval (or dedicated HITL preset).

## Pending-stop execution packet

See [stop packet](STOP-REPAIR.md): fixed the reproduced post-completion affected-ledger publication gap; capacity/closure/source predicates unchanged. Interval9 CanonicalTail's failed prerequisite remains UNKNOWN; consecutive-interval worker retirement control passes.

## Completion record

Final integrated evidence is recorded below. Device verification: NOT RUN; no upload, reset, data overwrite or Git delivery.

### Snapshot native results

- Pre-fix numeric-denial regression: `pio test -e native -f test_loop_take_survival`, exit1; 152 cases,
  1 failed (`test_publication_turn_waits_for_admitted_headroom`: construction unexpectedly started below
  the internal floor+working margin). Remaining150 passed; harness reported ERRORED/SIGHUP after the failure.
- After correction: `pio test -e native -f test_loop_take_survival -f test_memory_pressure_policy`, exit0;
  **161/161** passed. Existing allocation-park recovery remains green. Tier predicate checks68 KiB
  admits8 KiB external-only margin but not40 KiB internal-slab margin; just below40 KiB refuses.
- Native slabs use malloc: the production root/journal/content denial→recovery fixture exercises the
  internal-slab platform. The EXTMEM tier is checked by the shared constexpr policy and target build;
  no claim of native execution of the Teensy external allocator or device freshness.
- Shared heap-tail metric is unchanged; there are no allocator probes/scans added. DEC-157 records
  the tier-specific correction to DEC-075. Target build/review passed below; HITL remains pending.

### Button mapping diagnosis corrected

`MidiButtonProcessor::update` reports `channel0 + 1` on tap expiry, then calls
`triggerButtonPress(note, channel0, ...)`, whose log prints the zero-based channel unchanged.
Thus the captured Ch16→Ch15 messages are the same channel, not a routing collision.
`handleToggleRecord` selects Play/Stop itself when the committed track is stopped. No mapping,
button log, stopped-track behavior or transport restart policy has been changed by this repair.
The actual capture contains repeated `stop_job_pending` in CanonicalTail/CompanionBridge;
that is the pending-stop repair target, not a second button handler.

### Odd-bar native results

See [odd-bar packet](ODD-BAR-REPAIR.md): pre-edit real launch and requester assertions RED,
post-edit focused runtime working-set suite246/246 PASS. The 33-bar terminal span, natural
wrap and three paid-deadline adoptions preserve identity and exact ordered MIDI (51 rows).
Fractional bars remain unsupported by this requester; existing fallback gaps are not
continuous rolling coverage. Full native/target build/review passed below; device acceptance remains pending.

### Integrated verification / closeout

- `pio test -e native`: first integrated run3440 cases,3 failures in shared persistence fixtures
  that waited at the removed audible AtomicTransfer yield;7 skipped,3429 passed. No production
  assertion was weakened. Pause now occurs at CompanionBridge before publication (stopped route
  retains AtomicTransfer); before/after exact persistence/slot/content assertions are unchanged.
- `pio test -e native -f test_shared_capture_finalization`:95/95 PASS after fixture adaptation.
- Final `pio test -e native`: **3439 cases /7 skipped /3432 passed /0 failures**, exit0,156.134s.
  Log: `$PI_SCRATCH_DIR/three-issues-native-final.log`.
- `pio run -e teensy41-capture-serial`: **SUCCESS**,74.315s. RAM1 unchanged variables96,192;
  ITCM393,216(code392,040+padding1,176), locals34,880, padding floor840. RAM2 static168,384;
  EXTRAM static11,680. Build warnings retained in `$PI_SCRATCH_DIR/three-issues-build.log`; no
  warning-clean claim or unrelated warning cleanup. No size pin rebased.
- Exact HEX SHA256: `01f70896ba09add68bc905f7b7a2501601d62a0e30c9638754813e9d9531e91c`.
  Artifact: `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`.
- One focused independent read-only review of all three source/fixture packets: no concrete defect;
  device timing, external pool/OLED behavior and CanonicalTail condition explicitly remain unverified.
- Stimulus registry listed successfully. Existing base/window-content stimuli are available;
  no canonical stimulus edited or hardware run performed. No device-content authorization consumed.
- Durable updates: runtime status/scope, active task/spec delta, snapshot/playback authority,
  execution-bound composition and ledger evaluation. DELIVERABLE_TRACKING unchanged: no firmware
  shipped/flashed. Session closeout completed; no unrelated dirty changes discarded.
- Remaining: CanonicalTail interval9 phase5 failed prerequisite, physical combined-grain timing,
  on-device33-bar continuation and snapshot/display freshness. Repair boxes and family-removal
  gates remain unchecked pending their own device acceptance, not claimed closed by native results.

## 2026-10-25 upload follow-up

Owner-approved shipping image uploaded and boot independently verified; see [upload receipt](UPLOAD.md).
The preceding no-upload statements describe the implementation session only. Exact image unchanged;
pre/post canonical drains complete. Behavioral device acceptance and CanonicalTail condition remain open.
