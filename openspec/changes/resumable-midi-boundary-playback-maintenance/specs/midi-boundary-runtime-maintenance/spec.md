## Purpose

Keeps post-MIDI-boundary playback and deferred-frame maintenance small,
resumable, and observable while active transport requires timely MIDI service.

## ADDED Requirements

### Requirement: Active-transport maintenance is bounded and resumable

The system SHALL execute playback-order maintenance and deferred
load/display-frame maintenance during RECORDING, PLAYING, OVERDUBBING, or
`OVERDUB_STOPPING` only as individually bounded grains. A grain SHALL have a
declared input quantum and a measured or structurally enforced cost bound
proportional to that quantum. If a grain is not eligible at a MIDI service
boundary, or cannot finish within its permitted grain, the remaining work SHALL
stay pending and resume from owner-local continuation state in a later eligible
turn. `OVERDUB_STOPPING` SHALL retain playback continuity and all active
transport persistence/load-frame exclusions.

#### Scenario: MIDI service defers an ineligible grain

- **WHEN** active transport reaches a MIDI service boundary and a pending
  maintenance grain is not eligible
- **THEN** MIDI input is serviced without executing that grain
- **AND** the maintenance continuation remains pending without loss of work

#### Scenario: A grain yields before its semantic operation is complete

- **WHEN** a playback-order or deferred-frame operation exceeds one permitted
  grain during active transport
- **THEN** the system records its continuation before returning to MIDI service
- **AND** a later eligible turn resumes the same operation without restarting
  previously completed work

#### Scenario: Capture-source preparation precedes active seal

- **WHEN** an active overdub event is accepted into `capture.store`
- **THEN** fixed `CaptureAdmissionStaging` capacity has been reserved for it
- **AND** its capture-admission contract proves the bounded pre-seal candidate
  can cover its earliest audible instance before the applicable wrap or stop
  boundary
- **AND** an event that cannot satisfy that contract is rejected before it
  enters `capture.store`

#### Scenario: Accepted NoteOn reserves its terminal closure

- **WHEN** an active-overdub NoteOn is accepted into `capture.store`
- **THEN** its `CaptureNoteReservation` reserves fixed source, bridge, and
  stop-finalization capacity for that NoteOn, matching NoteOff, and any required
  wrap or synthetic terminal closure
- **AND** a matching later NoteOff consumes that reservation without ordinary
  capacity or readiness rejection
- **AND** if the complete note lifetime cannot be reserved, the NoteOn is
  rejected before it mutates capture or pending-note state

#### Scenario: NoteOff follows a rejected NoteOn

- **WHEN** a NoteOn was rejected by capture admission and its later NoteOff
  arrives
- **THEN** the NoteOff is forwarded through normal MIDI handling
- **AND** it does not create, close, or otherwise mutate captured-note state

#### Scenario: Load Commit is not structurally bounded

- **WHEN** a load job's Commit publication, bookkeeping, or required cleanup
  cannot demonstrate a complete content-independent active-transport bound
- **THEN** `DeferredJobScheduler::runFrame` does not admit that job during
  RECORDING, PLAYING, OVERDUBBING, or `OVERDUB_STOPPING`
- **AND** the job remains pending until STOPPED or a separately bounded
  preparation, Commit, or cleanup continuation is available

### Requirement: Pending load work does not starve admitted display frames

During active transport, load-child admission and retention SHALL distinguish
permission to restore slots from actual unfinished restoration work. Permission
alone SHALL NOT retain a load child. On an eligible `LoadAndDisplay` owner turn,
an admitted due or pending display frame SHALL take priority over a retained
load child. Each turn SHALL still execute at most one expensive child. Display
resolve/draw/transfer progress and load read/parse progress SHALL remain owned
by their existing domain state machines across yields. Existing focused-load
display admission gates and unbounded Commit/cleanup exclusions remain in force.

#### Scenario: Playback has permission to restore but no load work

- **WHEN** PLAYING permits deferred slot restoration but no restoration work remains
- **THEN** the load child is not retained on permission alone
- **AND** admitted due display frames continue through resolve, draw, and transfer

#### Scenario: A display frame becomes due while a load remains pending

- **WHEN** a load child remains pending and a display frame is admitted as due
  on the next eligible active-transport owner turn
- **THEN** that turn runs one display phase without also running a load slice
- **AND** pending display phases continue on subsequent eligible owner turns
- **AND** eligible load work resumes from saved progress when no display phase is due

#### Scenario: Background loads cannot progress during playback

- **WHEN** queued background restoration remains deferred throughout PLAYING
- **THEN** the retained backlog does not prevent admitted display frames from refreshing
- **AND** the system does not need to enter RECORDING, OVERDUBBING, or STOPPED to release the display

#### Scenario: STOPPED has no load work

- **WHEN** STOPPED permits a deferred load-frame turn with an empty load queue
- **THEN** the existing idle housekeeping path remains eligible
- **AND** active-transport load-work admission does not suppress that housekeeping

### Requirement: Deferred maintenance preserves playback and publication semantics

Deferring maintenance SHALL NOT drop, duplicate, or reorder audible MIDI
events. An active playback cursor SHALL read an event stream, playback order,
source identity, cycle-qualified emission cursor, ledger-boundary summary, and
explicit phase coverage from one immutable playback view. A successor build
SHALL NOT mutate that active view. The system SHALL retain the last validly
published view until a successor reaches its atomic promotion boundary; it
SHALL NOT expose partially built playback ordering, loop content, slot-load
state, or display-frame data. A long-loop cursor SHALL not run beyond published
coverage unless a complete contiguous successor coverage interval is selected
at the handoff.

#### Scenario: Playback order is pending at an audible event boundary

- **WHEN** a new playback order is incomplete when an audible event boundary
  is reached
- **THEN** the system uses one valid immutable playback view or an immutable
  source-change bridge paired with that view
- **AND** it does not emit a duplicate, missing, or out-of-order MIDI event

#### Scenario: Successor promotion crosses an event boundary

- **WHEN** a complete successor playback view becomes eligible between two MIDI
  service boundaries
- **THEN** its promotion preserves every event already completed by the active
  view and every event newly due after promotion
- **AND** the playback cursor cannot index an order from a different event
  stream or source identity

#### Scenario: Successor promotion occurs before loop wrap

- **WHEN** a successor promotes after an event instance has emitted in the
  current projection cycle
- **THEN** its cycle-qualified emission cursor suppresses only that already
  emitted instance after promotion
- **AND** the same source event is eligible again in the next projection cycle
- **AND** the candidate ledger-boundary summary matches the live
  `ActiveNoteLedger` without a clock-path state scan

#### Scenario: A pending source changes an already-open note

- **WHEN** a pending source would suppress or replace an event paired with a
  NoteOn already emitted into the live ledger
- **THEN** the bridge retains the base NoteOff needed to close that ledger entry
- **AND** the changed note pair becomes effective only after that safe boundary
  or in the next projection cycle

#### Scenario: A source change supersedes a pending successor

- **WHEN** a newer source identity arrives before a pending successor completes
- **THEN** the system discards or coalesces only the unpublished successor work
- **AND** it leaves the active playback view valid until the newest successor
  can be promoted

#### Scenario: A newly due source-change event precedes full successor completion

- **WHEN** a source change affects an event that becomes due before its full
  successor view completes during active transport
- **THEN** a fully prepared immutable delta bridge, with explicit insert,
  suppress, or replace records, is selected before that event is due
- **AND** the active clock path consumes the published view and bridge by a
  bounded ordered merge with the cycle-qualified exactly-once cursor
- **AND** if that bridge cannot be ready in time, its event is denied at capture
  ingestion and never reaches canonical commit, rather than delaying, omitting,
  or exposing incomplete audible content

#### Scenario: A second source change arrives while a bridge is selected

- **WHEN** a second source identity arrives before the selected bridge retires
- **THEN** the selected bridge remains immutable and continues to serve due
  events
- **AND** the system coalesces unpublished bridge and successor work to the
  newest identity
- **AND** it atomically replaces the selected bridge only after the replacement
  covers its earliest affected un-emitted event

#### Scenario: Active canonical commit requires audible readiness

- **WHEN** a capture pass would commit during active transport and its first
  affected event instance can become due in the current projection cycle
- **THEN** `PlaybackSourceAdmission` has already prepared the complete bridge
  or successor coverage for that instance before canonical commit
- **AND** `Loop` commits canonical content and its audible admission atomically
- **AND** if preparation is incomplete, the canonical service remains pending
  and resumable; work length alone does not reject the accepted capture

#### Scenario: Pre-seal candidate is incomplete at an overdub wrap

- **WHEN** `maybeCommitOverdubWrap` reaches a wrap crossing and the candidate
  generation does not match the accepted capture generation or is incomplete
- **THEN** it does not call a mutating seal/detach or restart the capture session
- **AND** it records a coverage-contract fault and takes the deterministic
  all-notes-off/stop path before an accepted event can be silently omitted
- **AND** it preserves accepted capture state for recovery after STOPPED

#### Scenario: A long-loop window approaches its handoff phase

- **WHEN** an unchanged long-loop source approaches the end of its published
  playback-view coverage interval
- **THEN** its next contiguous coverage interval has been built by resumable
  grains and is selected at the handoff phase, including across phase wrap
- **AND** a verified per-owner coverage-service contract proves enough phase
  lead and admitted grains to prepare the interval after that successor
- **AND** if that contract cannot be established, windowed maintenance is
  excluded during active transport in favor of a prewarmed full-loop view or
  STOPPED-only operation
- **AND** any runtime contract violation takes the deterministic fail-safe
  all-notes-off/stop path rather than silently omitting a due event

#### Scenario: Deferred frame is incomplete

- **WHEN** a load/display-frame operation yields before it can complete
- **THEN** the existing valid loop and display state remain visible
- **AND** the incomplete frame is not published as a completed result

#### Scenario: Stop follows an append without an intervening staging grain

- **WHEN** an active-overdub stop command arrives immediately after an accepted
  capture append
- **THEN** the system latches `stopTick`, enters `OVERDUB_STOPPING`, and freezes
  new capture input without faulting the transport
- **AND** it applies one reserved stop-finalization grain after each existing
  MIDI service boundary
- **AND** it commits only after the frozen candidate and audible admission are
  ready and transitions to the command-epoch-validated destination, PLAYING by
  default or STOPPED after transport Stop
- **AND** the stop request is always admitted when fixed staging and generation
  ownership are valid, regardless of candidate length or note count

#### Scenario: Stop finalization requires additional service turns

- **WHEN** a frozen stop generation needs more turns than the provisional
  preparation allowance
- **THEN** the system keeps `OVERDUB_STOPPING` active and resumes the same
  generation on later MIDI-separated turns
- **AND** it does not reject the stop or take the fault path solely because of
  candidate length or note count
- **AND** it preserves the capture generation until PLAYING or STOPPED recovery

#### Scenario: Transport Stop arrives during deferred stop admission

- **WHEN** a transport Stop command arrives between `OVERDUB_STOPPING`
  finalization grains
- **THEN** transport Stop increments the pending-stop command epoch, selects
  STOPPED, and invalidates the pending PLAYING transition before another grain
  or atomic transfer is selected
- **AND** normal transport Stop emits all-notes-off and prevents playback from
  resuming without a later explicit transport Start
- **AND** the frozen candidate may complete canonical recovery only under
  STOPPED policy

### Requirement: Canonical commit is distinct from audible-source admission

`Loop` SHALL retain ownership of canonical capture-pass commit, canonical
revision, persistence, and undo/redo. `LoopPlaybackRuntime` SHALL own
generation-matched `CaptureAdmissionStaging`, the pre-commit
`PlaybackSourceAdmission`, effective audible source revision, and one pending
later-effective-cycle admission that references immutable canonical content.
Each accepted NoteOn SHALL own a `CaptureNoteReservation` for its matching
closure and stop/wrap finalization work. Before active canonical commit,
staging SHALL have completed the semantic equivalents of seal sorting, identity
assignment, wrap finalization, and filtering without mutating `capture.store`.
An active canonical commit SHALL transfer that frozen candidate and ready
admission atomically; it SHALL NOT run those semantic operations, detach mutable
capture chunks, or build an audible bridge. A pending admission is allowed only
when all affected current-cycle instances are already covered; it SHALL NOT
delay, omit, or roll back an accepted canonical commit. A stop request SHALL
always enter `OVERDUB_STOPPING` when fixed staging and generation ownership are
valid, and may remain there for any number of resumable turns until the frozen
candidate is ready. Candidate length and note count SHALL NOT by themselves
trigger a rejection or fault stop.

Pass persistence SHALL remain metadata publication attached to the same
record/play/overdub/stop service. Persisting a pass, resolving companions, and
preparing an audible successor may yield independently, but they SHALL resume
from retained generation-matched cursors without introducing a separate finite
admission path.

#### Scenario: Later-effective pending admission is superseded

- **WHEN** a newer canonical revision arrives before a pending later-effective
  audible-source admission promotes
- **THEN** the runtime coalesces only the pending admission to the newest
  immutable canonical source
- **AND** persistence and undo/redo retain both their existing canonical
  semantics and ownership
- **AND** the effective audible source remains immutable until a valid promotion

#### Scenario: Source change preempts rolling long-loop build work

- **WHEN** a source change is admitted while long-loop playback has current,
  ready, and successor-after-next build coverage
- **THEN** the selected bridge covers all affected instances through the end of
  the ready coverage before the build buffer is reused
- **AND** the reused build buffer constructs the new-source interval after the
  ready coverage
- **AND** at the current-to-ready handoff the new-source interval MAY remain
  pending while the retired current buffer remains idle
- **AND** once the new-source interval is complete, it becomes ready and the
  retired current buffer begins the interval after it
- **AND** the source-change coverage contract proves the new-source interval is
  ready before ready coverage reaches its handoff, and proves the retired-current
  interval is ready before the new-source interval reaches its handoff from the
  latest permitted new-source completion point
- **AND** if that retired-current interval remains pending at the ready-to-
  new-source handoff, the retired ready buffer remains idle until it completes
  and starts only the interval after it
- **AND** current and ready coverage remain immutable until their handoffs

#### Scenario: Source-change preparation completes midway through two successive coverage windows

- **WHEN** the new-source interval completes after the current-to-ready handoff
  but before the ready interval reaches its handoff, and its following interval
  completes midway through the new-source interval
- **THEN** the pending interval becomes the ready successor without replacing
  the current ready-plus-bridge playback
- **AND** the retired current buffer starts the interval after that successor
- **AND** at the ready-to-new-source handoff that following interval remains
  pending and the retired ready buffer remains idle
- **AND** when it completes, it becomes ready and the retired ready buffer
  begins the next interval
- **AND** both successive handoffs occur with complete contiguous coverage and
  without overwriting a published or pending view

#### Scenario: A second source change preempts the source-change build

- **WHEN** a second source change arrives while the selected bridge spans the
  current/ready horizon and the build buffer is constructing a new-source view
- **THEN** the selected bridge and current/ready coverage remain immutable
- **AND** the candidate bridge covers the remaining horizon before acceptance
- **AND** the build buffer restarts only for the newest source identity

### Requirement: Active-transport maintenance is attributable by owner and grain

The system SHALL report capture telemetry for each active-transport maintenance
owner that distinguishes completed grain duration, yielded continuation, and
denied eligibility. Timing evidence SHALL distinguish active transport from
STOPPED work and SHALL permit attribution of `OVERDUB_STOPPING` and post-overdub
PLAYING MIDI gaps to the responsible owner and grain. Scored active transport
SHALL include RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING`. A timing
window containing capture-ring overflow while in any scored active-transport
interval SHALL be rejected as evidence for the active-transport bound.

#### Scenario: Stopping or post-overdub capture contains a deferred maintenance yield

- **WHEN** an `OVERDUB_STOPPING` or subsequent PLAYING turn denies or yields a
  pending maintenance grain
- **THEN** capture output identifies the owner, grain outcome, and duration
- **AND** the output can be correlated with MIDI-gap and deadline-lateness
  telemetry in the same active-transport interval

#### Scenario: Capture overflow occurs in a scored window

- **WHEN** capture-ring overflow occurs between an active state transition and
  the end of its scored timing window
- **THEN** the timing report marks that window invalid for bound verification
- **AND** it does not use the window to establish a post-overdub MIDI bound

### Requirement: Deterministic companion-pairing reference

Companion note-pair lookup SHALL filter the input sequence to the target
channel/pitch lane before identifying contiguous equal-tick runs. Equal ticks
separated by a different tick in that lane SHALL remain separate runs. Each
run SHALL process Offs before Ons and preserve input order within each kind.
Existing LIFO pairing and wrap fallback SHALL remain unchanged. Parity for
bounded companion resolution SHALL use this revised reference; this is an
explicit semantic correction to the former non-transitive comparator.

#### Scenario: Counterexample retains its lane-run closing partner

- **WHEN** the filtered lane is [On@96, On@0, Off@96, Off@96, Off@0]
- **AND** the target On is at index 0
- **THEN** companion pairing selects Off index 3 at tick 96

#### Scenario: Other lanes do not split an equal-tick run

- **WHEN** events from another channel or pitch occur between equal-tick events
  in the target lane
- **THEN** those intervening events do not split that lane's equal-tick run
- **AND** Offs precede Ons within the run with stable order within each kind

#### Scenario: A different tick separates runs

- **WHEN** two equal ticks in a filtered lane are separated by a different tick
- **THEN** the earlier and later equal ticks belong to different runs
- **AND** the resolver does not globally regroup or sort them together

### Requirement: Canonical per-pass companion preparation

Combined capture preparation SHALL apply the frozen companion sequence
independently within each participating canonical pass, including frozen capture,
and THEN merge the resulting pass streams in canonical pass order using the
existing stable raw-tick merge and within-pass result order. It SHALL retain
immutable pass membership and original event provenance through bounded
preparation. Flattened append-then-edit admission SHALL NOT be the reference.
DEC-059's lane-run pairing rule remains the reference within each pass.

Historical Create rows SHALL append their payload in stored order and stably
sort the complete individual pass by ascending raw tick, including for an empty
payload. Equal ticks SHALL preserve existing events before appended events and
order within each group. This is the approved DEC-061 reference correction;
parity SHALL use this revised reference. Create SHALL preserve existing
present-identity bookkeeping, reactivating added nonzero NoteOn identities after
earlier Delete rows. Stored Create ordering SHALL NOT add an Off-before-On
tie-break; DEC-059 pairing and canonical per-pass merging remain unchanged.

#### Scenario: Create orders equal ticks deterministically

- **WHEN** a Create row appends events at ticks already present in its pass
- **THEN** stable raw-tick ordering preserves existing events before appended
  events at those ticks and preserves order within each group
- **AND** an empty Create payload still stably orders the complete pass

#### Scenario: Create reactivates a deleted identity

- **WHEN** a Delete row removes an identity from the history sequence's present-ID
  set and a later Create payload contains a nonzero NoteOn with that identity
- **THEN** the identity re-enters that set and subsequent updates/deletes use
  the canonical reference's lookup behavior within that pass

#### Scenario: Repeated identity in separate passes
- **GIVEN** a record pass and a captured pass each contain a NoteOn with NoteId 1
- **WHEN** a frozen Delete companion targets NoteId 1
- **THEN** the companion resolves independently in both passes before merging
- **AND** deleting only the first instance in a flattened stream fails parity

#### Scenario: Length overlap is confined to its pass
- **GIVEN** record NoteId 1 spans ticks 0 to 10 and captured NoteId 2 spans 150 to 160 on the same channel/pitch
- **WHEN** a Length row extends NoteId 1 to 250
- **THEN** the captured NoteId 2 survives because overlap resolution is pass-local
- **AND** the merged result matches canonical pass materialization

### Requirement: Protected stop undo publication preserves memory feedback

Before accepting capture that requires deferred stop publication, the system
SHALL reserve private undo-entry, stack-bookkeeping, attachment and retirement
continuation capacity and explicit work credits separate from ranked preparation.
The affected Track SHALL protect the transaction against competing mutations and
SHALL keep intermediate stack state invisible to normal readers. Canonical,
audible and undo state SHALL share one final publication boundary. Retirement
SHALL NOT destroy state needed for recovery before the transaction's no-fail point.
Disabled canonical-pass reclamation SHALL be deferred until prepared proofs no
longer depend on the reclaimed storage and SHALL have a bounded continuation.

Removed undo entries SHALL release their final snapshot graph and persisted
snapshot/chunk references in bounded units. Oldest-entry trimming SHALL recheck
actual memory pressure only after the current entry's scheduled release completes.
It SHALL preserve existing feedback-based survivor semantics, including redo
removal and depth rails; retained roots SHALL NOT substitute for physical release
when deciding whether to trim another entry.

#### Scenario: Releasing one entry relieves pressure

- **WHEN** the oldest entry's final release clears memory pressure
- **THEN** the next trim decision observes that relief
- **AND** no additional entry is removed unless the existing depth rails require it

#### Scenario: Undo and Redo arrive during protected finalization

- **WHEN** Undo or Redo is requested for the protected Track
- **THEN** one reserved command slot retains the most recent request and reports deferral
- **AND** repeated or conflicting requests replace the pending request
- **AND** after finalization that request executes exactly once under the destination's normal command policy
- **AND** other Tracks retain independent pending commands

#### Scenario: Transport Stop arrives with a deferred undo command

- **WHEN** Transport Stop arrives during the protected transaction
- **THEN** its immediate stop and command-epoch precedence remain effective
- **AND** accepted capture is preserved through finalization to STOPPED
- **AND** the deferred command cannot cause a pending return to PLAYING

#### Scenario: Snapshot attachment arrives during protected finalization

- **WHEN** a snapshot-after attachment targets the protected transaction
- **THEN** reserved storage retains the attachment without exposing intermediate undo state
- **AND** publication validates the attachment's entry and loop identity


### Requirement: First-record playback windows are prepared during recording

First-record playback SHALL remain synchronized with the running transport and SHALL begin at the existing final-bar return boundary. Runtime preparation SHALL NOT postpone that launch to a later boundary. Preparation SHALL begin when recording starts and remain current with accepted recording input.

#### Scenario: Recording starts with bar-1 playback coverage

- **WHEN** first recording begins
- **THEN** its runtime establishes prewarmed bar-1 coverage, initially empty, with the recording origin
- **AND** accepted input incrementally prepares that playback window before record stop
- **AND** later-window preparation does not overwrite the bar-1 return window
- **AND** provisional one-bar coverage does not incorrectly fix the final loop length

#### Scenario: Final recording bar returns to bar 1

- **WHEN** the existing final-bar boundary returns the recorded loop to bar 1
- **THEN** bar-1 source, order, cycle cursor, and ledger boundary are ready for atomic selection
- **AND** playback emits the canonical events at their original due boundaries on the running clock
- **AND** it neither waits for a later launch nor rewinds the global clock
- **AND** a later snapshot publication neither replays nor omits the already serviced interval

#### Scenario: One-bar recording closes with no preparation turn remaining

- **WHEN** an accepted event or held-note closure affects the end of a one-bar recording immediately before its return boundary
- **AND** no maintenance grain can run between that input and the boundary
- **THEN** the previously established admission/closure path makes the affected audible events ready at their due boundaries
- **AND** finalization preserves canonical ordering, minimum-length rules, and held-note closure
- **AND** it performs no full gather, full sort, active allocation, or unbounded reanchor at launch

#### Scenario: Playback continues through recorded windows

- **WHEN** playback proceeds from bar 1 through a multi-bar first take
- **THEN** each next one-bar window is prepared before its handoff under a verified coverage-service contract
- **AND** the final-bar-to-bar-1 handoff remains ready without overwriting the return window
- **AND** no accepted audible event is missing, duplicated, reordered, or delayed


### Requirement: Recording preparation reuses playback window machinery

First-record preparation SHALL reuse the existing playback view storage, bounded
fill/order workers, coverage lifecycle, bridge mechanisms and MIDI emission
path. Necessary extensions SHALL be shared with ordinary playback; recording
SHALL NOT introduce a separate window store, ordering engine or emission stream.
Recording source ownership and preparation scheduling SHALL preserve the
original-bar return, raw-tick, late-input and held-note requirements above.

#### Scenario: Coverage preparation from a larger immutable source

- **WHEN** the shared fill worker prepares one window from a larger source
- **THEN** each examined source event consumes its declared entry quantum
- **AND** only events within the requested playback coverage enter that candidate
- **AND** raw event ticks and stable order are preserved for the shared order worker
- **AND** candidate overflow cannot publish a partial window or authorize recording input loss
