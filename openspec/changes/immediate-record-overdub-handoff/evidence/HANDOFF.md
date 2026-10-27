# P1 — production-path handoff/source/lifetime evidence

Baseline: `faa089b8` plus the existing dirty installed74392e15 implementation; no firmware behavior edits. New suite `test_immediate_capture_handoff` includes the same real shared-finalization closure as the existing rolling retirement suite, rolling enabled. Native boundaries are clock/pool/physical I/O; no success stub replaces asserted button, manager, Track, Loop, worker, undo or persistence operations.

## Commands and case presence

```sh
pio test -e native -f test_immediate_capture_handoff
# Immediately after that suite builds this native executable:
bin=/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/native/program
"$bin"
IMMEDIATE_HANDOFF_CASE=start "$bin"
IMMEDIATE_HANDOFF_CASE=stop "$bin"
IMMEDIATE_HANDOFF_CASE=source "$bin"
IMMEDIATE_HANDOFF_CASE=empty "$bin"
```

The program path is the configured build directory, not assumed `.pio/build`. Another native suite replaces that executable, so build the named suite immediately before these commands. Profiles select actual desired assertions; they remain failing acceptance cases until production implementation. Default GREEN controls are current characterization only and do not cover/suppress those desired cases. Unknown profile rejects the run. Register desired cases normally when their implementing slices pass.

Final PlatformIO default: **4/4 controls PASS**, exit0,6.041s. Every explicit profile: **1test/1failure/0ignored**, exit1, named assertion below. Compile/link failures are not RED evidence. Initial API mismatches (`forEach`, STL set iteration) were fixture compile errors and corrected using actual indexed store/membership APIs before any behavioral claim.

## Fixture geometry and oracle

Real clear/start/accepted input/button-stop produces64 sealed raw rows, no committed Record pass, one Record job and free producer directory. The input spans32 bars; this fixture's stop policy reports final length23,808ticks (31bars), so later phases are computed from actual final geometry, not predicted32bars. Direct B uses a monotonically forward clock to the next exact projection phase48; native assertion verifies it. Raw first pair is explicitly pitch60, accepted NoteId1, On12/Off96; phase48 is strictly inside it. No old take's reused numeric ID or source-cache-only row is used as oracle.

The source/Stop cases use actual `Track::startOverdubbing` as an independent production entry. This deliberately bypasses the manager's separately tested retained-Start gate, not a forced state/ready/publication result. A must remain private throughout those assertions. Actual source correctness and early sealing are independently tested, not assumed from direct live admission.

## Four intended acceptance failures

### 1.1 — immediate Start

`test_immediate_start_accepts_input_before_record_publication`

Real button request followed by next input leaves state PLAYING, capture interval1, A raw64/gen1, jobs1, producer0 and committed0. Fails **“B ingress waits for private Record publication.”** The request is retained, but input is not in a new B interval. This proves the installed publication gate, not a manual rolling-mode requirement.

### 1.2 — early Stop

`test_early_stop_ends_B_without_reusing_A_custody`

Direct B starts as interval2 and accepts2rows while A retains64 immutable rows/gen1. Stop B leaves it OVERDUBBING, jobs1, producer2; sending input after the requested endpoint increases producer **2→3** in the same interval. A's complete tuple inventory remains unchanged. Fails **“B remains live/appends after its requested Stop while A finalizes.”**

C start/Stop is present after that assertion but **not reached**. There is no claim that B/C have independently sealed custody. Current pending job/finalizer/single-range restrictions must be resolved; removing Start gating alone cannot pass the trace.

### 1.5 — private same-pitch source

`test_private_record_is_the_same_pitch_overlap_source`

A's independently accepted pitch60/NoteId1/12..96 pair exists in the sealed range. Direct B starts at asserted phase48 while A is private, but source note count and same-pitch participants are **zero**. Fails **“private predecessor A is absent from B's same-pitch hold source.”**

A separate default control drains actual A canonical publication, calls the same production source rebuild/query and sees the expected accepted NoteId at phase48. Thus the query/oracle works with the canonical predecessor; the missing private-source preparation/binding is real. This does not provide a bounded incremental private source, its service deadline, pending-transform custody or B/C overlap differential. Task1.5 remains incomplete.

### 1.3 — filtered-empty cleanup

`test_filtered_empty_record_does_not_clear_live_B`

A uses the existing known filtered-empty recipe (On780/Off800, min48, stop1536). Direct B accepts2distinct rows and starts interval2 before A worker runs. After actual empty A completion:

- B interval2 and its **2 raw rows survive**; do not claim they were erased.
- Track remains OVERDUBBING and Capture phase assertion passes.
- A is released (raw0, range generation1→2), no committed Record pass.
- Loop geometry **768→0**, and diagnostic pending-change count **1→0**.

The desired geometry assertion fails **“empty A cleanup reset geometry still needed by accepted B.”** Pending-change disappearance is an observed diagnostic, not an independently asserted second failure. Accepted B's raw survival is not enough for valid subsequent playback/overlap/Stop. The successor-safe empty-result continuation is missing.

## Default characterization controls

1. Actual A range stays sealed/generation-qualified, exact note tuple rows64 retained while direct B uses the empty live producer and accepts2rows. No extra job is created; no independent B Stop claim.
2. Normal nonempty A worker/publication preserves B's exact rows, interval and OVERDUBBING state, creates canonical Record and one undo entry in this isolated trace, and dirty-marks persistence for A's slot. FIFO jobs reach0 while A range remains owned/readable through presentation. Extra worker calls leave undo state unchanged. **Persistence admission counts/exactly-once persistence are not asserted**; a sticky dirty flag cannot prove them.
3. Sequential canonical source control sees accepted A12..96/NoteId1 at48 through the same source query after actual publication.
4. Clear supersedes A, drains its actual release, rejects its old generation receipt, and a new recording accepts pitch79 without late A republishing or clearing the new producer. This is stale-range/control proof, not full generalized custody-slab lifetime proof.

## Limits and task state

Task1.1 immediate-button counterexample and task1.2 independent early-Stop counterexample are delivered. Lifetime task1.3 is partial: normal publication, raw-reader retention, undo/dirty-mark control, clear/stale generation and empty geometry failure are covered; independent sealed B/C undo/persistence chronology and all stale continuations are not yet reachable/proved. Source task1.5 has a concrete RED and canonical positive control but lacks the required bounded recipe/deadline and full differential.

No accepted-successor global undo entry was manufactured before B's currently blocked Stop. No private canonical state was published to make a test pass. These gaps remain **BLOCKED**, not waived.

Scratch logs: `immediate-handoff-controls-final.log`, `immediate-handoff-controls-trace-final.log`, `immediate-handoff-{start,stop,source,empty}-final.log`. Full default native run after adding both suites: **3,454cases /7skipped /3,447passed /0failed**, exit0,105.775s (`immediate-p1-native.log`). This is regression/characterization success only; the five intended handoff/visual acceptance assertions remain RED separately.

One focused independent proof review found a persistence-overclaim in the execution note. It was narrowed to dirty-marking; no instrumentation, production change or assertion weakening followed. Review did not execute tests or establish hardware timing. No shipping build/upload/device/SD/Git action in this packet.
