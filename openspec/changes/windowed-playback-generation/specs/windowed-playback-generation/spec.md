## Purpose

Defines the active-playback window authority: playback is served from immutable, revision-identified windows covering
the interval the playhead is about to use, with readiness owed per affected tick against a deadline derived from tempo
and quantisation, a bounded one-pass-late staleness rule backed by a pinned source revision, one off-clock publication
owner, and checkpoint parity preserved for undo and save.

## ADDED Requirements

### Requirement: Active playback is served from an immutable window

When content is committed for a slot, the system SHALL serve active playback from an immutable **window** covering the
interval a consumer is about to use, identified by `(slot, playbackGeneration, playbackRevision, interval)`. The system
SHALL NOT require whole-loop publication in order to serve playback, and SHALL NOT assert whole-loop publication as the
unit of authority. Existing immutable representations MAY be retained: the prepared pair and the fixed view remain the
playback form for loops where they are admissible, immutable interval publication is how the demoted/windowed case is
served, and a whole-loop view SHALL be treated as coverage that happens to span the whole loop.

#### Scenario: Playback is interval-backed

- **WHEN** a slot with committed content sounds and the playhead is about to consume a tick range
- **THEN** the content served for that range comes from an immutable window covering that interval
- **AND** the window carries the slot, the playback generation, the revision and the interval it was built for

#### Scenario: An admissible slot keeps its existing representation

- **WHEN** a loop is admissible for the prepared pair or a fixed whole-loop view
- **THEN** that representation remains authoritative for playback
- **AND** it is treated as coverage spanning the loop, not as a second unit of authority

#### Scenario: The demoted case is served by interval publication

- **WHEN** a loop is past the demotion bound and no whole-take representation is admissible
- **THEN** playback is served by immutable interval publication
- **AND** no whole-take reservation is required or restored for that slot

### Requirement: A published window is immutable and no consumer read straddles a replacement

A published window SHALL be read-only to every consumer, and no interval's content SHALL change while it is being
consumed. A consumer SHALL NOT observe a window changing underneath a read in progress.

#### Scenario: Replacement does not tear a read

- **WHEN** a window is replaced while a consumer is reading the previous one
- **THEN** the consumer completes its read against the window it started with
- **AND** no consumer observes a mixture of the two

#### Scenario: Content does not change mid-consumption

- **WHEN** a replacement becomes available part-way through an interval's traversal
- **THEN** the traversal completes against the version it began with
- **AND** the replacement takes effect from the following eligible traversal

#### Scenario: Consumers cannot mutate a window

- **WHEN** any playback, display, persistence or edit consumer reads a window
- **THEN** it receives read-only access
- **AND** no consumer changes the window's content or identity

### Requirement: Currency is the playback revision, and "current" has one definition

A window's currency SHALL be judged against the loop's current playback revision, using one definition of current. The
system SHALL NOT introduce a second validity counter. The playback generation SHALL guard ownership changes and stale
deferred completions rather than currency, and a snapshot identity SHALL be source provenance and lifetime protection
rather than another definition of "current". A window whose revision is not the loop's current playback revision SHALL
be reported through the existing coverage vocabulary (`wrapTargetRevision`, `coveredPassRevision`, or current), and an
overlay SHALL pin its actual source dependencies rather than relying on a null identity.

#### Scenario: Current window

- **WHEN** a window's revision equals the loop's current playback revision and its coverage is sufficient
- **THEN** the window is current

#### Scenario: Coverage outcomes are the cases of currency

- **WHEN** a window's revision is behind the loop's current playback revision
- **THEN** the difference is reported as one of the existing coverage outcomes
- **AND** no additional validity counter is consulted

#### Scenario: Provenance is not currency

- **WHEN** a window was built from an overlay whose published root is stale
- **THEN** the window pins the source dependencies it actually used
- **AND** the missing snapshot identity is not treated as a validity answer

### Requirement: Preview content is a distinct source and is never committed currency

Live note-edit preview content SHALL be served from a **preview source contract of its own**, published through the same
**immutable deferred publication** discipline as committed content: versioned, retained and invalidated by the
edit-session lifecycle, with an identity distinguishable from a committed window's at the same committed revision.
Publication SHALL define **source freezing** (the preview revision a build works from),
**cancellation** (what happens when the session mutates, re-focuses another track, undoes or exits between build grains)
and **committed-content handoff** (how the preview source yields at session exit), and no mixed or stale preview content
SHALL be published. Preview content SHALL NOT masquerade as committed currency — the committed playback revision remains
the sole content-currency authority — and it SHALL NOT depend on a live mutable fallback for committed playback.

#### Scenario: Preview is identified as preview

- **WHEN** preview content is served for a slot that also has committed content at the same committed revision
- **THEN** the preview window is identified as preview rather than as a committed window
- **AND** consumers that require committed currency do not accept it as committed

#### Scenario: Preview is invalidated by its own lifecycle

- **WHEN** the edit session is exited, undone or re-focused on another track
- **THEN** the preview source is invalidated or re-versioned by that lifecycle
- **AND** stale preview content is not served as current

#### Scenario: Committed playback does not depend on the preview path

- **WHEN** the live mutable fallback is withdrawn for committed playback
- **THEN** preview content is still served through its own contract
- **AND** the withdrawal is reported as applying to committed playback only

### Requirement: Readiness is owed per affected tick, before the playhead returns to it

After content changes at a tick, an updated window covering that tick SHALL be ready before the playhead returns to it.
The deadline SHALL be derived from tempo, quantisation, phase advance and service availability — not asserted as a
constant and not expressed as a bar count. A whole-loop coverage interval SHALL remain an acceptable way to satisfy
readiness, and preparation SHALL run ahead of the playhead rather than being centred on it.

#### Scenario: An edit is ready before the playhead returns

- **WHEN** content changes at a tick the playhead will revisit
- **THEN** a window covering that tick is ready before the playhead reaches it
- **AND** readiness is demonstrated against the derived deadline, not against a number of bars

#### Scenario: The deadline follows the tempo

- **WHEN** the same content is played at different tempos within the supported range
- **THEN** the deadline for an affected tick changes with tempo and quantisation
- **AND** no single millisecond figure is treated as the universal deadline

#### Scenario: Readiness does not depend on loop length

- **WHEN** the same edit is applied to slots whose loop lengths differ by more than an order of magnitude
- **THEN** readiness is owed and met per affected tick in each case

### Requirement: Readiness is completed publication with a named holder

Readiness SHALL be a predicate any consumer can ask: has publication for this interval completed, does the published
window carry the identity the consumer requires, and is its coverage sufficient? A successfully prepared **empty**
interval SHALL count as ready, and serving it SHALL **emit no new events** — empty coverage is never a readiness failure
and SHALL NOT silence a note that is legitimately sustained across that interval. Non-empty content SHALL NOT be a
readiness requirement, and the completion state SHALL NOT become a second revision counter. When readiness fails, the
failure SHALL name the holder that owes the window; an unnamed failure SHALL NOT be reported.

#### Scenario: A prepared empty interval is ready

- **WHEN** an interval legitimately contains no events and its publication has completed
- **THEN** the interval is ready
- **AND** serving it emits no new events rather than failing readiness, and does not silence a sustained note

#### Scenario: Missing window names its owner

- **WHEN** a consumer asks for readiness on an interval whose publication has not completed
- **THEN** the answer is not-ready
- **AND** the report names the holder that owes the window

#### Scenario: No anonymous readiness failure

- **WHEN** readiness fails for any reason
- **THEN** the failure carries the holder's identity and the reason
- **AND** no acceptance surface has to infer the owner from absence

### Requirement: One pass late is the only stale authority

A missed deadline SHALL keep the **previous revision of that affected interval** authoritative for that traversal: never
torn, never held, never timing-deferred. To make that possible the system SHALL **hold** the affected revision — not
merely name it: an immutable description of that revision's pass membership and order, edit metadata and loop geometry,
with **actual body ownership** and a **generation-qualified identity** — the retained form is **pinned bodies plus
reconstruction through the effective-content resolver**, not a frozen per-interval copy, and the reconstruction cost is
charged to the deadline; protection that the reclamation consumer actually reads (a pin registered only alongside undo
references is not sufficient) **and** that is honoured on the direct
reset/replacement paths, so clearing, loading or replacing a slot cannot destroy or silently alias the retained content;
the retained sources SHALL be budgeted in explicit **serving, candidate and retiring** roles rather than counted as a
single revision, with unchanged bodies shared where possible and a still-needed source never discarded to satisfy a
count; and a source SHALL be released only by **one release predicate** — the loop's revision has advanced past it, no
window, builder, future-window obligation, closure obligation or reader references it, and any builder using it has been
cancelled. Retention SHALL be named among the **terms of `Track::noLiveOwnerForSlot`**; enumeration of the retained
source SHALL go through the **effective-content resolver over the selected `LoopSnapshot`** — raw committed chunks do
not apply note edits or establish effective ordering — invoked against a **generation-qualified immutable retained source**,
and SHALL bound
its **inspected** work per grain (chunks examined including rejected ones, plus source lookup), not only the events it
appends, with interval-directed lookup so total work does not grow with unrelated loop content; lookup SHALL include
**edit-moved events at their effective destinations**, and index updates SHALL cover **both the old and the new affected
ranges** and publish **atomically with the corresponding source revision**, never modifying an index still serving
readers. The index SHALL be specified over **pinned bodies**, with its representation change recorded explicitly and its
ownership, **resident-memory bound** and construction/update lifetime stated; `TickIndex` semantics are reused where
suitable but its whole-history event-copy storage SHALL NOT be inherited unchanged, and `CommittedEventRange` SHALL be
used as a body-reading primitive only after its inspected-work budgeting is extended and verified. Lookup,
reconstruction, ordering, index maintenance and retirement SHALL all be bounded **and accounted**, not merely the
emitted events. Where no effective interval-bound source exists, the complete scan SHALL be accounted in admission
rather than presented as window-bounded.
A **second** missed traversal SHALL be reported as a named contract breach rather than as successful one-pass-late
operation. A consumer SHALL NOT be blocked by a missed deadline, and normal continuity SHALL remain subject to the
approved last resort defined by *A second miss latches a fault, and continuity is preserved*.

A **pass** is one traversal of the affected interval in the playback projection — not a revision increment and not elapsed
time. An ordinary wrap advances traversal accounting; a transport stop **pauses** traversal and does not forgive an
existing miss; MIDI Start, seek and geometry changes invalidate old positional deadlines and require revalidation without
erasing pending content obligations or fault history. Superseded **preparation requests** SHALL be coalesced — never
accepted musical content — and useful preparation SHALL NOT be cancelled because newer edits arrived, so progress toward
the oldest outstanding deadline is preserved. The retained source SHALL be budgeted in explicit **serving, candidate and
retiring** roles rather than by a single revision count, sharing unchanged bodies where possible; these are source
lifetimes, not additional whole-loop copies, and a still-needed source SHALL NOT be discarded merely to enforce a count.
Publication SHALL be revalidated against the clock assumptions it was admitted under (see *Clock revalidation*), and a
failed revalidation SHALL retain the request and continue the valid audible source rather than rebuilding inline.

#### Scenario: A transport stop does not forgive a miss

- **WHEN** an interval's deadline is missed and the transport stops before its next traversal
- **THEN** the miss is not forgiven and no lag allowance is restored
- **AND** the obligation and its fault history survive the stop

#### Scenario: Later edits do not discard useful preparation

- **WHEN** a further edit arrives while preparation for an earlier deadline is in progress
- **THEN** the in-progress preparation is not thrown away merely because a newer revision exists
- **AND** superseded *requests* are coalesced while accepted musical content is never rewritten

#### Scenario: A clock change revalidates the promise, not the content

- **WHEN** tempo, clock source, projection or observed advancement invalidates the assumptions a preparation was admitted under
- **THEN** admission is revalidated before publication or handoff
- **AND** the accepted request is retained, the valid audible source continues, and the missed deadline is reported rather than rebuilt inline

#### Scenario: A missed deadline serves that interval's previous revision

- **WHEN** an updated window is not ready when the playhead reaches the affected tick
- **THEN** the previous revision of that interval remains authoritative for that traversal
- **AND** the content served is never a mixture of the two versions

#### Scenario: Later intervals stay constructible from the retained source

- **WHEN** a later traversal of the same loop needs an interval after the miss
- **THEN** it is constructed from the pinned source revision
- **AND** a resident whole-loop playback root is not required to produce it

#### Scenario: The retained source is protected and eventually released

- **WHEN** a window is still owed for the pinned revision while edit and clear operations disable or reclaim passes
- **THEN** the retained revision's pass identity stays protected from reclamation
- **AND** the source is released only by the single release predicate (revision advanced, no window, builder, obligation or reader references it)

#### Scenario: Production does not scan length the window does not need

- **WHEN** the requested interval stays the same while the loop grows and events are added outside it
- **THEN** building the window performs interval-bounded, resumable enumeration
- **AND** the work does not grow with the source's total length

#### Scenario: A second miss is a named breach

- **WHEN** the deadline is missed on two consecutive traversals of the affected interval
- **THEN** the condition is reported as a named contract breach
- **AND** reporting it is not treated as the requirement having passed

#### Scenario: Further edits do not extend the allowance

- **WHEN** another edit arrives while a window is one pass late
- **THEN** the existing lag allowance is not reset or extended
- **AND** the stale condition continues to be named

#### Scenario: A missed deadline does not block a consumer

- **WHEN** the deadline is missed
- **THEN** the consumer continues to sound
- **AND** nothing on the timing-critical path waits for the window, unless the approved last resort applies

### Requirement: One off-clock owner acquires, replaces and publishes windows

Window acquisition, replacement and publication SHALL happen on a **single named off-clock owner**, which SHALL declare
and honour a bounded service contract: a fixed work quantum, a persistent build cursor, a bounded number of consecutive
denied turns, and a demonstrated handoff margin. The MIDI input boundary, the transport tick and the clock path SHALL
only read readiness, and SHALL NOT allocate, build or block for a window. The owner that requests a preparation SHALL
not also publish in the steady state.

#### Scenario: The timing-critical edge allocates nothing

- **WHEN** a window is needed at a MIDI edge, a transport tick or a clock-path commit
- **THEN** that edge takes no capacity and performs no build
- **AND** a fixture fails if the edge allocates

#### Scenario: An unprepared window keeps the request pending rather than taking it inline

- **WHEN** the edge finds no prepared window for the target it is about to bind
- **THEN** the request stays pending
- **AND** the owner prepares it later with no further user gesture

#### Scenario: The service contract is honoured

- **WHEN** the owner performs a turn
- **THEN** it advances by at most its declared quantum and persists its cursor
- **AND** its consecutive denied turns stay within the declared bound

#### Scenario: Requesting is not publishing

- **WHEN** a switch or a slot preparation requests a window
- **THEN** the request is made through the requesting owner
- **AND** steady-state publication happens only on the publishing owner

### Requirement: The window mechanism reuses the existing resumable machinery

Window construction, replacement and any persistence interaction SHALL use the existing resumable machinery — the
declared coverage-service build contract, the bounded body-format write/parse, the resume-arming pattern and the single
persistence drain budget. A second publication scheduler SHALL NOT be introduced.

#### Scenario: No second scheduler

- **WHEN** window publication work is scheduled
- **THEN** it uses the existing resumable entry points and the declared build contract
- **AND** no new scheduling owner is introduced

#### Scenario: Work that cannot finish in one slice is armed and resumed

- **WHEN** window work exceeds its slice budget
- **THEN** it is armed and continued on a later eligible turn
- **AND** its progress stage is named

### Requirement: Readiness evidence is delivered where it is produced

A readiness receipt that an acceptance surface relies on SHALL NOT depend on bulk-retention policy for its delivery,
and a diagnostic SHALL NOT evict the evidence it explains.

#### Scenario: A readiness receipt is delivered under transport

- **WHEN** a window becomes ready while the transport is running
- **THEN** its receipt is delivered to the evidence stream
- **AND** the receipt is not displaced by higher-volume diagnostics

#### Scenario: Diagnostics do not cost evidence

- **WHEN** a device run emits high-volume diagnostic rows
- **THEN** the rows an acceptance surface fences on are still present
- **AND** the run's evidence is not invalidated by diagnostic volume alone

### Requirement: Length independence is proved by sweep, not assumed

Readiness SHALL NOT require whole-root residency, and the window span SHALL NOT be hard-coded to a bar count as its
acceptance. Length independence SHALL be swept across loop lengths and densities, and the span SHALL be a recorded
tunable with a declared initial comparison configuration rather than a fixed contract.

#### Scenario: The sweep is the evidence

- **WHEN** length independence is accepted
- **THEN** it is demonstrated by a sweep over loop lengths and content densities
- **AND** a single bar count is never the oracle

#### Scenario: The window span is a tunable

- **WHEN** the window span is chosen
- **THEN** it is recorded as a tunable with its coupled effects and an initial comparison configuration
- **AND** changing it does not invalidate the swept acceptance shape

### Requirement: Accepted content is never truncated to fit a window

A window SHALL always be able to hold the content of the intervals it serves, because the number of events an interval
may accept SHALL be bounded **at admission** by a **sliding window-span invariant**: for the configured playback span
`W`, **every** `W`-wide interval of the loop — including intervals that cross the loop wrap — SHALL fit within the limit
`L`; the window's storage capacity SHALL be **exactly `L`**, with no partition or partition-count multiplier. The
accounting unit is the **capture interval at acceptance**, counted in **effective** events, with the **reserved capacity
for note-off and generated closures counted inside `L`** rather than additively, aggregated across the slot's overdub
passes. Admission SHALL use **preallocated, bounded accounting** that includes outstanding reservations and SHALL NOT
invoke the effective-content resolver on the MIDI edge; per-bar counts MAY serve only as a **conservative bound covering
every intersected bar**, never as an exact sliding-window count, and an accepted note's closure SHALL remain guaranteed
even when further input is refused. Capacity pressure SHALL be refused **before** content is accepted, as a named
condition, and SHALL NOT be absorbed afterwards by dropping, truncating or reordering accepted events, nor by exceeding
the window. Content that was accepted SHALL remain recoverable through the existing committed representation, and the
per-interval acceptance limit SHALL be a recorded, provisional limit rather than a proven supported-density claim. A
change to the window span, to edit geometry or to content SHALL preserve the invariant before it becomes audible, and
content restored from SD or undo that was accepted before the limit existed SHALL be served through the **bounded paged
compatibility path** and SHALL NOT be retroactively refused. This limit governs **MIDI content** only: it SHALL NOT refuse
stop, switch or undo requests, whose acceptance stays unconditional (DEC-078).

#### Scenario: Capacity pressure is refused at admission

- **WHEN** an interval's content would exceed the per-interval acceptance limit
- **THEN** the condition is refused by name at admission
- **AND** no accepted event is silently dropped, truncated or reordered to make the window fit

#### Scenario: Every accepted event is served

- **WHEN** a dense interval at, and one event below, the acceptance limit is played through the windowed path
- **THEN** every accepted event is served
- **AND** no event is lost at a window boundary

#### Scenario: The limit is provisional

- **WHEN** the per-interval acceptance limit is stated
- **THEN** it is recorded as a provisional limit
- **AND** no supported-density claim is made from it before measurement

### Requirement: Legacy dense content is served through bounded pages

Content restored from SD or undo that was accepted before the limit existed SHALL be served — never retroactively
refused — and SHALL NOT be routed into the deadline-fault silence policy merely for exceeding the window's capacity. An
interval whose content exceeds one page SHALL be served as **multiple fixed-capacity immutable event pages** through a
**resumable cursor** that preserves the source revision, event ordering and **exactly-once** emission across page
boundaries; pages SHALL be prepared **off-clock** from bounded, preallocated storage. Pagination SHALL NOT delay events to
later musical ticks, rebuild content inline, restore whole-take reservations or truncate content. Capacity exceeding one
page is a **supported compatibility case and is not itself a deadline fault**; genuine preparation or playback deadline
failures SHALL still follow the approved fault-containment policy, because a storage bound is **not** a throughput proof.

#### Scenario: A long restored loop plays completely after the fallback is withdrawn

- **WHEN** a restored loop's requested interval exceeds the window's capacity and the live mutable fallback is withdrawn
- **THEN** the interval is served through bounded pages with complete, ordered, exactly-once output
- **AND** no event is delayed to a later tick, truncated or rebuilt inline

#### Scenario: Over-capacity content is not a deadline fault by itself

- **WHEN** supported legacy content exceeds one page
- **THEN** exceeding the page capacity is not by itself reported as a deadline fault
- **AND** a genuine preparation or emission deadline failure is still reported under fault containment

#### Scenario: Cross-page closures and loop wrap

- **WHEN** a note's closure falls on the next page or crosses the loop wrap
- **THEN** the closure is emitted exactly once at its correct tick
- **AND** over-capacity same-tick events are each served without reordering

### Requirement: Checkpoint parity for undo and save

The published root SHALL remain the immutable checkpoint authority for undo and save. A checkpoint's parity SHALL be
judged against the loop's **current** revision, and a checkpoint that lags the committed passes SHALL be named in the
readiness/lag vocabulary and SHALL NOT be presented as current. Undo parity and save parity are one requirement on two
consumers: an undo SHALL restore the geometry and content its entry named, a persisted body SHALL be equivalent to the
root it was serialized from, and neither SHALL consult the window's lag. Nothing in this capability removes a root
dependency.

#### Scenario: Undo restores what its entry named

- **WHEN** an undo is applied under the windowed authority
- **THEN** the restored geometry and content are identical to what the entry named
- **AND** the undo does not consult the window's lag

#### Scenario: A lagging checkpoint is named, not presented as current

- **WHEN** a checkpoint (or a restored body) is behind the committed passes
- **THEN** the outcome is reported as the named lag/staleness case
- **AND** it is never reported as current without reconciliation

#### Scenario: Save parity is judged on the serialized root

- **WHEN** a body is persisted and later restored
- **THEN** the body is equivalent to the root it was serialized from
- **AND** the restore's coverage outcome is either current or named as stale

### Requirement: Content parity across a window boundary

The content served by successive windows SHALL concatenate to the same events as the whole-loop path. No event SHALL
be lost, duplicated or reordered at a window boundary.

#### Scenario: No tear at a boundary

- **WHEN** playback crosses from one window to the next
- **THEN** the events served across the boundary equal those the whole-loop path serves
- **AND** no note-on is left unclosed by the transition

#### Scenario: Repeated windows agree with the whole-loop path

- **WHEN** the same loop is played through successive windows and through the whole-loop path
- **THEN** the resulting event streams are equal

### Requirement: A window is served without whole-root residency

A window SHALL be servable while the loop's whole root is not resident, and content parity SHALL hold for that demoted
state. The retained source revision that makes this possible SHALL be a charged, measured term of the memory envelope,
and no whole-take reservation SHALL be restored for demoted slots to achieve it.

#### Scenario: Windows resident, root demoted

- **WHEN** a slot's windows are resident and its whole root is not
- **THEN** playback is served from the windows
- **AND** the root is not required to be resident for playback to be correct

#### Scenario: Parity in the demoted state

- **WHEN** the demoted state's served content is compared with the whole-root state's
- **THEN** the content is equivalent
- **AND** the comparison uses the project's existing content-verification pipeline

#### Scenario: Retention is charged

- **WHEN** a source revision is pinned so later intervals stay constructible
- **THEN** its cost is accounted to the memory envelope
- **AND** no whole-take reservation is restored for the demoted slot

### Requirement: Envelope support is proven, not promised

The declared envelope SHALL distinguish supported behaviour from targets. Today's eight sounding roles are the supported
envelope; a larger target SHALL count as supported only once **both** a memory test and a deadline test pass, and a
target SHALL NOT be claimed from the per-role window term alone. A supported-density claim SHALL be defended by
measurement rather than asserted.

#### Scenario: A target is not a support claim

- **WHEN** an envelope target larger than the supported set is recorded
- **THEN** it is recorded as a target
- **AND** no capacity, density or concurrency claim is made from it before measurement

#### Scenario: Both budgets must pass

- **WHEN** a target is proposed as supported
- **THEN** both the memory envelope and the service deadline are shown to hold
- **AND** a passing memory figure alone is not sufficient

### Requirement: Deferred playback service is fair, bounded and deadline-ordered

Playback maintenance SHALL receive a recurring, **bounded** service opportunity on the existing deferred machinery — no
second scheduling framework. Service SHALL rotate round-robin across eligible holders, and within a holder SHALL serve
the **earliest outstanding playback deadline** first. The selection cursor SHALL advance even when a holder cannot make
progress, so a blocked holder cannot monopolise service; capture finalization and persistence SHALL keep their own
explicit opportunities and playback urgency SHALL NOT starve them; and an admitted turn SHALL perform one declared grain
with no hidden stacking. The proof of sufficiency SHALL cover **all admitted holders**, including inbound switch
preparation and overlapping finalization, and the supported envelope SHALL be **derived** from grain costs and the worst
permitted gap between service opportunities rather than asserted.

#### Scenario: A blocked holder does not monopolise service

- **WHEN** one holder cannot progress while other holders have outstanding playback deadlines
- **THEN** the selection cursor still advances and the other holders receive service
- **AND** the blocked holder does not consume every opportunity

#### Scenario: Earlier deadlines win within a holder

- **WHEN** a holder has more than one outstanding playback deadline
- **THEN** its earliest outstanding deadline is served first

#### Scenario: Playback urgency does not starve other owners

- **WHEN** playback preparation and capture finalization or persistence all have outstanding work
- **THEN** each of them retains its own service opportunity
- **AND** no owner is starved indefinitely by another

### Requirement: Published views are read-only and ownership survives transitions

A published or current view SHALL be read-only to every consumer, and only the builder that owns a candidate SHALL write
it. A ready view SHALL be immutable until it is adopted or explicitly retired, and its storage SHALL become reusable only
after every relevant owner has released it. Ownership SHALL survive selection and lifecycle changes: a **new edit** retains
the frozen builder source and coalesces the successor; a **slot switch** prepares inbound coverage before binding and
retains outbound resources until their readers and closures finish; a **selected-track change** does not retarget an
existing worker; a **record/overdub stop with a successor start** preserves the predecessor's frozen ownership and the
successor SHALL NOT borrow its buffers; **transport stop** cancels obsolete lookahead while preserving accepted content
and finalization; **clear/load** invalidates the old operation identity and retains its source until cancellation and
readers finish; and a **late completion** SHALL be rejected for publication and SHALL release only the resources it owns.
Identity comparison SHALL be **generation-qualified** — returning to a slot SHALL NOT revive an earlier operation on it,
and neither the slot number nor a pointer SHALL stand in for identity.

#### Scenario: A builder writes only its own candidate

- **WHEN** a consumer reads a published or current view while a builder is working
- **THEN** the consumer's view is read-only and the builder writes only its own candidate
- **AND** a published destination is never written by a builder

#### Scenario: A late completion publishes nothing

- **WHEN** a builder completes after its operation was invalidated or its resources were retired
- **THEN** its result is rejected for publication
- **AND** it releases only the resources it still owns

#### Scenario: Returning to a slot does not revive an old operation

- **WHEN** playback returns to a slot that had an earlier preparation in flight
- **THEN** identity is compared generation-qualified
- **AND** the earlier operation is neither revived nor adopted by the new one

#### Scenario: Storage is reusable only after every owner releases it

- **WHEN** a view is retired while a reader, a closure obligation or a builder still references its storage
- **THEN** the storage is not reused
- **AND** it becomes reusable once the last relevant owner releases it

### Requirement: A second miss latches a fault, and continuity is preserved

The first missed traversal is the one-traversal allowance. A **second** missed traversal SHALL latch a **named
deadline-contract fault**, distinct from meeting the one-pass-late guarantee. Playback SHALL continue the last coherent
revision wherever valid coverage can still be supplied through an admitted bounded path, preparation SHALL keep
progressing, and recovery SHALL occur at an eligible boundary. The system SHALL NOT silently reset the lag, publish
partial content or block the clock. **Approved last resort:** when no valid coverage remains at all, continuity cannot be
promised, and recovery SHALL be **targeted note closure plus affected-slot silence** — never a global transport stop and
never an unbounded rebuild.

#### Scenario: Fault containment is not a successful fallback

- **WHEN** the deadline is missed on two consecutive traversals
- **THEN** a named deadline-contract fault is latched and reported
- **AND** the condition is not recorded as successful one-pass-late operation

#### Scenario: Continuity continues while coverage remains valid

- **WHEN** a second miss occurs but valid coverage is still available through an admitted bounded path
- **THEN** playback continues from the last coherent revision
- **AND** preparation keeps progressing toward recovery at an eligible boundary

#### Scenario: The approved last resort is targeted, not global

- **WHEN** no valid coverage remains for the affected interval
- **THEN** the affected slot's notes are closed and that slot falls silent
- **AND** the global transport is not stopped and no unbounded rebuild is performed

### Requirement: Cross-generation note closure is reconciled off-clock

Version handoff SHALL preserve sounding notes that remain unchanged, **without retriggering** them; notes removed or
invalidated by the replacement SHALL be closed at handoff; closures that became obsolete SHALL be suppressed afterwards;
required note-offs SHALL be emitted **before** the replacement's note-ons; and closure obligations SHALL stay alive
independently of view-buffer retirement. Reconciliation SHALL be prepared off-clock. An **event-empty window means "no
new events"** — it SHALL NOT silence notes that are legitimately sustained across that interval.

#### Scenario: Sustained notes are not retriggered or silenced

- **WHEN** a note is sounding across a version handoff and its content is unchanged
- **THEN** it is neither retriggered nor closed by the handoff
- **AND** an event-empty window for that interval does not silence it

#### Scenario: Removed notes are closed before replacements sound

- **WHEN** a replacement version removes or invalidates a note that was sounding
- **THEN** its note-off is emitted at handoff, before any replacement note-on
- **AND** obsolete closures are suppressed afterwards

#### Scenario: Closure obligations outlive the view

- **WHEN** the view buffer that carried a note's origin is retired while the note is still sounding
- **THEN** the obligation to close it remains
- **AND** it is discharged at the correct boundary

### Requirement: Windowed publication contributes to the runtime continuity contract

This subsystem SHALL satisfy its part of
[Continuous runtime operation has one end-to-end acceptance contract](../../../runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md#requirement-continuous-runtime-operation-has-one-end-to-end-acceptance-contract).
That runtime requirement owns the workload envelope, MIDI timing metrics and action-to-audible limits;
this change SHALL reference them rather than define a competing end-to-end guarantee.
Window preparation, index maintenance, source retention/retirement, publication, page consumption and
closure reconciliation SHALL fit the allocated service and memory bounds without violating MIDI input or
output deadlines or starving capture/finalization. In-envelope operation SHALL NOT need content refusal,
coverage-starvation silence or involuntary transport stop. Existing refusal and last-resort recovery
policies remain required safety behavior, but exercising them SHALL fail normal continuity acceptance.

#### Scenario: Window work fits the shared runtime budget
- **WHEN** windowed or legacy-paged playback operates under the runtime's declared simultaneous workload
- **THEN** preparation and consumption meet their handoff and MIDI deadlines while capture/finalization progresses
- **AND** no accepted content is lost, no unchanged note is interrupted, and no coverage fault or capacity refusal occurs

#### Scenario: Local completion does not certify the whole runtime
- **WHEN** this change's publication, parity and ownership tests pass
- **THEN** its evidence is supplied to the runtime's combined acceptance gate
- **AND** end-to-end continuity remains unproven until capture/finalization, undo/redo and physical MIDI-output
  timing are verified together under the same declared envelope

### Requirement: Undo and redo use the prepared audible handoff

Capture-pass and overdub undo/redo playback refresh SHALL use the same immutable prepared handoff and
off-clock selective closure reconciliation as other version changes. The `TrackUndo` semantic owner and
checkpoint authority SHALL remain unchanged. Accepted undo/redo SHALL NOT blanket-silence the slot merely
to invalidate playback caches. Valid old coverage SHALL continue until the eligible prepared handoff;
unchanged sounding notes SHALL survive without retriggering, removed/invalidated notes SHALL close at
handoff, and obsolete closures SHALL be suppressed. Generation-qualified cancellation and retention SHALL
also hold when another undo/redo or a slot/track switch occurs between preparation grains. This integration
is required before normal windowed undo/redo is enabled or the mutable fallback is removed; checkpoint
content parity alone SHALL NOT discharge it. Preview-session undo/redo retains its separate source lifecycle
and SHALL obey the same audible continuity rule through the existing preview-publication requirement.

#### Scenario: Undoing one pass preserves another pass's sustained note
- **WHEN** a capture/overdub pass is undone while an unchanged note from another pass sustains across the handoff
- **THEN** the unchanged note receives neither an early NoteOff nor a replacement NoteOn
- **AND** only invalidated sounding notes close, with the remaining obligations discharged exactly once

#### Scenario: Redo and repeated undo remain continuous between grains
- **WHEN** undo/redo is repeated or the selected track/slot changes between real preparation grains
- **THEN** only the correct generation-qualified result can become audible at its valid boundary
- **AND** the current source and unchanged sounding notes survive preparation, with no stale publication,
  blanket silence, duplicate closure or reset of the publication-lag allowance
- **AND** action-to-audible latency is checked separately from timely emission of the previous revision
