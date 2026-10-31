# Spec delta — runtime-profiling

## Purpose

A standard, dumpable timing instrument for the firmware: declared operations are timed
automatically on every exit path, aggregate cost is kept per fixed identity in RAM2/OCRAM (`DMAMEM`), outside RAM1/DTCM, and only
calls that exceed a declared ceiling enter a bounded trace ring that a capture can read later.

## ADDED Requirements

### Requirement: A profile scope times the whole operation on every exit path

A profile scope SHALL measure one declared operation from entry to exit **without a separate start
trigger**, so that every exit path — including each early `false` return — is timed. The scope SHALL
be identified by a fixed binary identity, and the measured duration SHALL come from the platform's
cycle counter.

Inside the measured operation the scope SHALL perform no serial write, no allocation, no formatting,
no logging and no blocking wait. The scope SHALL NOT change the measured operation's control flow:
an instrumented operation SHALL return the same value and take the same branch structure as the same
operation compiled without scopes.

Scope entry is restricted to the main-loop and deferred-owner lanes. An operation reached from an
interrupt context SHALL NOT be instrumented in this capability.

A scope SHALL time the operation's **work body**, not a surrounding poll function that usually
returns without doing work.

#### Scenario: Early return is timed

- **WHEN** an instrumented operation returns `false` from one of its early exits
- **AND** the operation is entered again and reaches its final `return true`
- **THEN** both calls appear in that identity's aggregate record
- **AND** neither call falls outside the measured interval

#### Scenario: Uninstrumented build takes the same path

- **WHEN** the same operation is compiled without the profiler enabled
- **THEN** it returns the same values for the same inputs
- **AND** no serial row, no table write and no counter read is emitted for it

#### Scenario: A poll function's idle turn is not measured

- **WHEN** a poll entry point is admitted every turn but performs no work on most turns
- **THEN** the scope is placed after the cheap no-work gate, on the work body
- **AND** the identity names that work rather than the poll entry

### Requirement: A short scope requires an independent guarantee below one counter period

A short scope SHALL be permitted only where an **independent guarantee**, recorded beside the site,
establishes that the operation cannot span one cycle-counter period. The declared ceiling SHALL NOT
be used as that guarantee: a ceiling is a reporting threshold and says nothing about how long the
operation actually ran.

The independent guarantee SHALL be a structural property (a declared grain, a window-sized working
set, a fixed frame) or a recorded measurement with its source, not a restatement of the ceiling.

#### Scenario: A short scope names its guarantee

- **WHEN** a site declares a short scope
- **THEN** the site records the structural reason its operation cannot reach one counter period
- **AND** a reviewer can check that reason against the operation's own code or bound

#### Scenario: The ceiling is not accepted as validity evidence

- **WHEN** a short scope's only stated justification is "the ceiling is below the counter period"
- **THEN** the review rejects the site until an independent guarantee or the long primitive is used

### Requirement: A long scope resolves multi-second operations instead of aliasing them

A long scope SHALL bracket the operation with a coarse monotonic wall clock in addition to the cycle
counter. When the cycle delta lies inside one counter period it SHALL report the exact cycle
duration; when it does not, it SHALL report the wall-clock duration and mark the record as
coarse-sourced.

Two 32-bit wall-clock samples cannot detect a full `micros()` wrap, so a long scope SHALL be
permitted only where the site records an independent guarantee that the operation cannot span one
wall-clock period (≈ 71.6 minutes). For the synchronous main-loop operations in this capability that
guarantee is a structural property — no unbounded wait, no user input, no network, no sleep — and is
recorded beside the site. A site that cannot state it SHALL use an extended 64-bit monotonic clock
instead, which this capability does not provide.

A site whose operation performs file I/O, waits on a device, or composes unbounded work SHALL use the
long scope.

#### Scenario: A pathological multi-second call is reported, not aliased

- **WHEN** an instrumented long scope's operation spans more than one cycle-counter period
- **THEN** its reported duration is the wall-clock duration, marked as coarse-sourced
- **AND** the value is not a modulo of the cycle counter presented as a short duration

#### Scenario: Normal durations keep cycle resolution

- **WHEN** a long scope's operation completes inside one counter period
- **THEN** its reported duration is the cycle-derived duration
- **AND** the record is not marked coarse-sourced

#### Scenario: A long scope names its wall-clock guarantee

- **WHEN** a site declares a long scope
- **THEN** the site records the reason its operation cannot reach one `micros()` period
- **AND** a site without that guarantee is rejected in favour of the 64-bit clock, not shipped with a
  silent wrap exposure

### Requirement: Profile identities are fixed, append-only binary identities

Each profiled operation SHALL have one identity from a compile-time enumeration whose numeric value
never changes. An identity that has appeared in device evidence SHALL NOT be renumbered or reused for
a different operation; new operations SHALL append. The dump SHALL be able to name an identity without
reading a symbol table, and a build SHALL declare an identity at most once.

#### Scenario: Appending does not disturb earlier evidence

- **WHEN** identities are appended for a later slice
- **THEN** every previously declared identity keeps its numeric value
- **AND** older captures still decode with the current name mapping

#### Scenario: Duplicate declaration is rejected

- **WHEN** two operations are declared with the same identity
- **THEN** the build fails rather than silently reporting one operation's time under the other's name

### Requirement: Aggregate statistics are kept per identity in RAM2, outside RAM1

The profiler SHALL keep exactly one aggregate record per identity for the whole session in external
RAM, never in the DTCM-resident variables bank. Each record SHALL carry: call count, total inclusive
cycles, total self cycles, maximum duration, last duration, over-ceiling count, refused-measurement
count, and last declared result. Maximum and last duration SHALL be retained in both cycle and
wall-clock units so a coarse multi-second measurement is never truncated to a 32-bit cycle count.

The profiler owns one table **for its own identities**. It SHALL NOT replace, migrate or modify an
existing telemetry facility; that facility keeps its own records, its own build gating and its own
output format.

A result code is optional: a scope SHALL record one only when the site supplies it, and the measured
duration SHALL be recorded correctly whether or not a result was supplied.

An aggregate record SHALL be cleared only by the explicit reset command or by boot.

#### Scenario: A scope without a result still records its duration

- **WHEN** an instrumented operation completes and its site never supplied a result code
- **THEN** the call count, inclusive, self, maximum and last durations are recorded
- **AND** the result field reports that none was supplied
- **AND** no fabricated default result is stored

#### Scenario: Aggregates survive the whole run

- **WHEN** an identity is profiled repeatedly across a long capture
- **THEN** its aggregate record reports the total number of calls and the maximum single call
- **AND** neither figure is reset by a transport stop, a slot change or a save

#### Scenario: No DTCM growth from the table

- **WHEN** the profile image is linked
- **THEN** the approved RAM1 variables figure is unchanged
- **AND** the aggregate and trace storage is reported in RAM2/OCRAM (`DMAMEM`), not in RAM1/DTCM

#### Scenario: Existing telemetry is untouched

- **WHEN** the profile image and the default image are both built
- **THEN** the existing per-operation telemetry rows and their format are identical in both
- **AND** the profiler introduces no second table for that facility's identities

### Requirement: A declared ceiling decides ring entry and is report-only

Each identity SHALL declare a cost ceiling taken from a fixed class set — a clock-scale, hot-path,
frame, deferred-work and cold-admission class — or from an existing named owner budget. The ceiling
SHALL decide only whether a call enters the trace ring and whether the over-ceiling count increments.

A ceiling SHALL NOT bound, defer, refuse, re-order or re-own the measured operation, and it SHALL NOT
be treated as evidence about the measured interval. Exceeding a ceiling is a reported observation,
never an execution limit, and no owner's existing bound class or `ExecutionBounds.md` row changes
because an operation is profiled.

#### Scenario: A breach changes nothing but the report

- **WHEN** an instrumented operation exceeds its declared ceiling
- **THEN** the operation completes with its normal result
- **AND** one trace record is written
- **AND** the over-ceiling count increments
- **AND** no admission, deferral or ownership decision changes

#### Scenario: Ceiling classes are not new bounds

- **WHEN** the profiler's declared ceilings are compared with the maintained execution-bound inventory
- **THEN** the inventory records no new bound and no reclassification for a profiled owner
- **AND** the existing owner budgets remain the only enforced limits

### Requirement: Only over-ceiling calls enter a fixed-size trace ring

Normal calls SHALL update the aggregate record only. A call whose duration exceeds its identity's
declared ceiling SHALL additionally write one fixed-size record to a preallocated circular ring in RAM2/OCRAM (`DMAMEM`),
outside RAM1/DTCM. Each record SHALL carry: a monotonic sequence number, a wall-clock timestamp, the
identity, the immediate enclosing profiled identity (if any), the duration **both as cycles and as
wall-clock microseconds with exactly one of the two meaningful and marked**, the track and slot when
the site supplies them, the declared result code when the site supplies one, and a source/flag byte.

The ring SHALL carry the **immediate enclosing profiled scope only**. It SHALL NOT claim a complete
call chain, and the dump SHALL NOT present one.

The ring SHALL have a fixed capacity declared in the contract, SHALL overwrite its oldest record when
full, and SHALL count overwritten records. The ring SHALL NOT grow, allocate, or emit while recording.

#### Scenario: Normal call leaves no trace record

- **WHEN** an instrumented operation completes under its ceiling
- **THEN** no ring record is written
- **AND** the aggregate record still counts the call

#### Scenario: Ring overflow is visible, not silent

- **WHEN** more over-ceiling calls occur than the ring can hold
- **THEN** the newest records are present
- **AND** the overwritten count reported at dump time is greater than zero
- **AND** a capture that reports zero overwritten records may be read as a complete breach history

#### Scenario: Ordering survives overwrite

- **WHEN** the ring has overwritten older records and is later dumped
- **THEN** every record carries a monotonic sequence number
- **AND** the reader can detect the gap at the overwrite boundary from the sequence numbers alone

#### Scenario: A breach names its immediate enclosing scope

- **WHEN** an over-ceiling call is nested inside another instrumented operation
- **THEN** its record carries the immediate enclosing identity
- **AND** the dump does not claim grandparents or a full chain

### Requirement: Invalid measurements are refused, not fabricated

A measured duration SHALL be recorded only when the timing source is available, the scope variant's
validity rule holds, and the scope exits in nesting order. The cycle counter is the **only** timing
source: when it is unavailable, every scope — short and long — refuses its measurement. The long
scope's wall clock exists to disambiguate a cycle-counter wrap, not to serve as an alternate source.

A scope that exits while a different scope is the innermost active scope SHALL be recorded as an
ordering fault, SHALL NOT contribute a duration to either identity, and SHALL poison the nesting
state so that no further measurement is recorded until an explicit reset.

Refusal SHALL NOT abort or alter the measured operation, and a refused measurement SHALL NOT be
stored as a zero or as a modulo value presented as a duration.

#### Scenario: Unavailable timing source is reported, not assumed

- **WHEN** the cycle counter is not running at the expected rate
- **THEN** the boot row reports the timing source as unavailable
- **AND** short and long scopes alike refuse their measurements and increment the refused count
- **AND** no duration is reported as measured

#### Scenario: Out-of-order exit is refused and poisons nesting

- **WHEN** a scope exits while another scope is the innermost active scope
- **THEN** an ordering fault is reported
- **AND** neither identity receives a fabricated duration
- **AND** no further measurement is recorded until the profiler is reset
- **AND** the reset is permitted once the live scopes have unwound and the depth is zero

### Requirement: Nested scopes separate inclusive and self time

The aggregate record SHALL report both the inclusive duration of an operation and its self duration,
where self excludes the duration of measured child operations. Nested scopes SHALL contribute the
same measured interval to the enclosing scope's inclusive total and to their own record.

Nesting arithmetic SHALL use one normalized 64-bit duration domain, so a coarse long child's
multi-second duration cannot be truncated into a 32-bit cycle accumulator or silently dropped from
the parent's self time.

#### Scenario: A parent's self time excludes its timed children

- **WHEN** an operation with a declared ceiling of the cold class runs two instrumented children
- **THEN** the parent's inclusive total covers the whole operation
- **AND** the parent's self total excludes the children's measured intervals
- **AND** the parent's breach, if any, is explained by the child totals within the record's own
  arithmetic tolerance

#### Scenario: A coarse child is not truncated in the parent's accounting

- **WHEN** a measured child's duration exceeds one cycle-counter period and is reported from the wall
  clock
- **THEN** the parent's self and inclusive totals account for the child's full duration
- **AND** the parent's recorded maximum is not a 32-bit cycle truncation of it

### Requirement: Profile commands are on demand, bounded, and mutate only profiler state

The firmware SHALL answer three commands: one that dumps the aggregate table, one that drains the
trace ring, and one that resets the profiler. The commands SHALL be answered while the transport is
running and SHALL NOT touch content, storage, undo, ownership or transport state.

Both dumps SHALL be **batched**: one request SHALL emit at most a declared maximum number of rows
(separate maxima for the aggregate table and the ring), followed by a terminator row that states how
many rows were emitted and whether another batch is owed. A persisted cursor SHALL continue the dump
on the next request; the aggregate dump and the ring dump SHALL have independent cursors.

A ring dump session SHALL pin a **sequence horizon** at its first batch — the range it will drain —
and SHALL NOT include records that arrive after that horizon; those belong to the next session. If
part of the pinned range is overwritten before it is read, the sequence gap SHALL be reported
explicitly rather than skipped silently.

**Every profile command SHALL execute only when no scope is active.** A command received at a
non-zero nesting depth SHALL be refused with a short row naming the depth, and the host retries later.
A refused command changes no table, ring or cursor state.

The reset command SHALL clear the aggregate table, the ring, the overwrite count and both dump
cursors, and nothing else.

The boot calibration row is the only profile row emitted without a command.

#### Scenario: Stats are readable during a running transport

- **WHEN** the transport is running and the aggregate dump command is received
- **THEN** the firmware emits at most one batch and a terminator row without stopping the transport
- **AND** no MIDI output, content, undo or persistence state changes

#### Scenario: Both dumps are bounded per request

- **WHEN** the table or the ring holds more rows than one batch may emit
- **THEN** the request emits the declared maximum for that dump
- **AND** its terminator states how many were emitted and that another batch is owed
- **AND** a later request continues from the persisted cursor

#### Scenario: A ring dump has a stable horizon

- **WHEN** new breaches arrive between two batches of the same ring dump
- **THEN** the drain still covers only the sequence range pinned at its first batch
- **AND** the new records are not duplicated into, or interleaved with, the pinned range
- **AND** if part of the pinned range was overwritten, the gap is reported from the sequence numbers

#### Scenario: A command under a live scope is refused

- **WHEN** any profile command is received while a scope is active
- **THEN** the command is refused with a row naming the current depth
- **AND** no dump work, no table change and no ring change occurs
- **AND** the host may retry the same command later

#### Scenario: Reset clears only profiler state

- **WHEN** the reset command is received with no active scope
- **THEN** the aggregate table, ring, overwrite count and dump cursors are cleared
- **AND** no other state is affected

### Requirement: Profile rows are Tier-A for the capture ring

Every profile row SHALL use the timestamped `#CAP,<micros>,DIAG,profile_…` form and SHALL be
classified Tier-A by the predicate the capture ring asks, with a classifying prefix short enough for
the ring's head read.

The registration point SHALL be the classifier source list, not the inline header list whose literals
are instantiated in every translation unit. The build's marker pre-flight, the ring's tier suite, and
the header-list suite SHALL each pin the resulting behaviour so that the two lists cannot drift
silently.

A profile row that is absent from a capture SHALL be read as evidence only when the capture's own ring
statistics report no loss.

#### Scenario: Marker pre-flight passes before a flash

- **WHEN** the marker pre-flight is run for each profile prefix
- **THEN** it reports the prefix as emitted, Tier-A classified, and within the head-read length
- **AND** the audit mode passes for the whole registered set

#### Scenario: The header list is not grown

- **WHEN** the classifier source list is extended with the profile prefix
- **THEN** the inline header list is unchanged
- **AND** both tier suites pin the resulting classification

#### Scenario: Absence is judged against ring loss

- **WHEN** a profile row is missing from a capture
- **AND** the capture reports Tier-A eviction or a large shed count
- **THEN** the absence is not evidence that the row was never emitted

### Requirement: A host decoder reproduces the dumps without a device

A host-side reader SHALL decode the profile rows from a capture log alone — no serial port, no device
and no firmware execution — into the aggregate table, the breach timeline, and each breach's
**immediate enclosing profiled scope**. It SHALL use the sequence numbers to report ordering and
overwritten gaps.

The reader SHALL state which parts of its output are unavailable when the capture lacks the
corresponding rows or reports ring loss. It SHALL NOT present a missing identity as a measured zero.

#### Scenario: Capture-only decoding

- **WHEN** the reader is given a capture log containing profile rows and ring statistics
- **THEN** it prints the aggregate table and the breach timeline
- **AND** it prints each breach's immediate enclosing profiled scope where the record carries one

#### Scenario: Sequence gaps are reported

- **WHEN** the ring's sequence numbers are not contiguous
- **THEN** the reader reports the gap and attributes it to overwrite or loss
- **AND** it does not print the surviving records as a complete history

#### Scenario: Missing rows are reported, not guessed

- **WHEN** the capture lacks the aggregate rows or reports ring loss
- **THEN** the reader marks the affected output as unavailable or incomplete
- **AND** it does not present an absent identity as a measured zero

### Requirement: The profiler exists only in a profile build

The profiler SHALL be compiled out of the default firmware and the no-grain acceptance image. Those
images SHALL contain no profile code, no profile table, no profile ring and no profile command
answers, and SHALL remain unchanged by this capability.

The profile build SHALL be a dedicated environment that extends the default diagnostic environment
and enables the profiler explicitly.

#### Scenario: Default image is unchanged

- **WHEN** the default capture environment and the no-grain acceptance environment are built
- **THEN** their linked RAM1 variables, ITCM figures and boot behavior are the same as before this
  capability
- **AND** a profile command sent to them is not answered as a measurement

#### Scenario: Profile build is one flag away

- **WHEN** the profile environment is built
- **THEN** the profiler is present and the same source tree produces the default behavior when the
  flag is absent

### Requirement: The layer reports its own per-scope overhead

The profile build SHALL measure the cost of an empty scope over a declared sample of **128 to 256
entries** once at boot, with no serial output inside the measurement loop, and SHALL report at least
the minimum and the average cost in the boot row.

The boot row SHALL be emitted once per boot even when the timing source is unavailable (with the
source marked unavailable), so a capture always shows whether the profiler was armed.

#### Scenario: Overhead is a measured number, not a single sample

- **WHEN** the profile image boots
- **THEN** one calibration row reports the per-scope minimum and average cost in cycles and in
  microseconds at the reported CPU rate
- **AND** the sample count is stated
- **AND** the row is emitted once per boot, not per call

#### Scenario: A site above the hot-path scale is named

- **WHEN** a profiled site's own overhead is of the same order as its declared ceiling class
- **THEN** the calibration row and the aggregate table make that visible
- **AND** removing that site is a recorded decision, not a silent acceptance

### Requirement: The first profile image covers the domains that have produced stalls

The first profile image SHALL declare at least one identity in each of: the Record admission edge, its
prewarm and its views child, playback window build and rolling service, snapshot publication, the
deferred save step, the deferred idle-owner turn, display base gather and frame build, the deferred
slot-load step, and overdub start admission.

The DEC-230 vertical proof SHALL land before the remaining domains are instrumented: the Record edge,
its prewarm and its views child SHALL be proven on device to reproduce the known attribution first.

#### Scenario: Coverage is verifiable from the dump

- **WHEN** the aggregate dump is read after a run that exercised record, overdub, playback, a save
  and a slot load
- **THEN** at least one identity from each declared domain is present with a non-zero call count
- **AND** the domains with no exercised call are named as not exercised rather than as absent
  instrumentation

#### Scenario: The known answer comes before the wide spread

- **WHEN** the remaining domains are wired
- **THEN** the Record/prewarm/views attribution has already been accepted on device
- **AND** the later sites reuse the same primitives without re-opening the validity contract
