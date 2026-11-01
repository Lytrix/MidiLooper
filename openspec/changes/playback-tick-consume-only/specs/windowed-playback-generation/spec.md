# Spec delta — windowed-playback-generation

## ADDED Requirements

### Requirement: Every playback tick consumes only a published source, on either clock

For any playback tick, on the internal clock (the timer interrupt) or the external clock (the MIDI-input tick on the
main loop), the selected playback source SHALL already be published, immutable while published, order-clean, current
for the slot's generation and revision, and SHALL cover that tick. Tick processing SHALL NOT perform source construction,
ordering, allocation, publication or any prerequisite work for its own source.

If no such source exists, the tick SHALL owe the work to the off-clock owner, SHALL count a playback miss, and SHALL
contain the turn through the existing no-route path. A miss is an exceptional failure, never the normal way a window
advances.

#### Scenario: A seam is crossed with the next window already published

- **WHEN** the playhead reaches the end of the window it is consuming
- **THEN** the window covering the next tick was published before the playhead arrived
- **AND** the handoff at the seam performs no build, sort, merge, allocation, scan or publication

#### Scenario: A missing source is contained, counted and owed

- **WHEN** a tick finds no published, current, order-clean source covering it
- **THEN** no source is built or ordered on that tick
- **AND** a playback miss is counted for the clock source the tick ran on
- **AND** the off-clock owner is asked to produce the source

#### Scenario: Both clocks follow the same rule

- **WHEN** the same slot plays first on the external clock and then on the internal clock
- **THEN** both ticks read the same published source contract
- **AND** neither tick context may prepare playback

### Requirement: Tick playback misses and consumed routes are observable in the default image

The default capture image SHALL count, at negligible cost, every tick's playback miss (split by internal and external
clock) and the route each tick consumed: served window, bridge, paged delivery, merged fallback, or none/contained. The
counts SHALL be emitted in the periodic telemetry family as Tier-A rows, with state outside the RAM1 variables budget and
literals in a carved read-only unit.

#### Scenario: A long-loop chain reports its routes

- **WHEN** an owner chain on a long loop is captured
- **THEN** the capture contains, per telemetry window, the tick count per consumed route and the miss counts per clock
- **AND** a decoder prints the totals for the run without reading the device

#### Scenario: Absence of the rows is not read as zero

- **WHEN** the capture lacks the telemetry rows or reports Tier-A loss
- **THEN** the decoder reports the counts as unavailable, never as zero

### Requirement: A bypassed windowed lane names the first failed predicate

When a sounding slot is not served by the windowed lane, the route decision SHALL record the **first** predicate that
rejected the lane, as a named reason from a fixed set, emitted when the reason changes. A generic "not usable" reason
SHALL NOT be used.

#### Scenario: The long-loop bypass is named

- **WHEN** the owner chain's long loop is served by the merged fallback
- **THEN** the capture names the predicate that kept the windowed lane out, with the slot it applies to
- **AND** the reason is stable enough to drive a repair decision

### Requirement: Capability is proved separately from routing

Before any new playback buffering is introduced, a native fixture SHALL drive the existing windowed lane alone on the
long-loop shape — a 20-bar loop, a 2-bar moving coverage, publication ahead of the cursor and seam transitions with
note closure — and SHALL record whether the lane can serve it. A repair that adds a second buffer to the merged
fallback SHALL cite a recorded failure of this fixture.

#### Scenario: The lane can serve the shape

- **WHEN** the fixture passes
- **THEN** the defect is classified as admission or routing, and the repair targets selection, demand or publication

#### Scenario: The lane cannot serve the shape

- **WHEN** the fixture fails on a named capability
- **THEN** that capability gap is recorded and becomes the only justification for new buffering

### Requirement: Long-loop qualification stays on the windowed lane with zero misses

The change SHALL be accepted only on a long device run spanning many window seams, on both the external and the
internal clock, in which the playback miss count is zero, merged-fallback route use is zero or explicitly justified per
occurrence, and no playback build is entered from interrupt context. MIDI timing SHALL be reported separately for the
external-clock and internal-clock intervals.

#### Scenario: Qualification run

- **WHEN** the qualification run completes
- **THEN** `tick_playback_miss` is zero on both clocks
- **AND** the consumed-route counts show the windowed lane serving the long loop
- **AND** the runtime profiler reports zero `PlaybackWindow_build` entries from interrupt context
- **AND** external- and internal-clock MIDI timing are reported as separate intervals
