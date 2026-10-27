# Runtime architecture — playback consumer

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Playback vs Playing, interval projection vocabulary.

---

## Overdub intent during private first-Record publication

`MidiButtonActions` classifies an identity-qualified private Record predecessor through Track,
so Record/Overdub does not become an empty-slot mute/record operation. TrackManager immediately
attempts ordinary overdub admission against the existing sealed custody and private-source owners,
without waiting for canonical publication. All staging/memory/interval checks remain in force.
On resource denial only, the existing32-byte interaction latch retains the qualified Start; recovery
uses ordinary admission and supersedes on clear, slot exit, transport reset, or replaced identity.
Track focus alone does not retarget it. A later filtered-empty predecessor cannot erase an already
accepted live successor (DEC-159/DEC-160); a denied Start with no remaining base is cancelled.

The private-base query applies to every newer successor interval while the same identity-qualified
Record job remains private, not only to `A+1`. Stop advances the interval again, so restricting to that
first value misroutes the second overdub Start into empty-slot handling. This changes classification,
not ordinary admission or capacity. Stopped capture-build `!STOP_STATUS` can additionally report
`#STOP_RECORD` for the head Record job's own preparation role/output (pure request-only scalar read).
It does not progress/rebind/recover a stalled cursor or establish playback correctness.
[Later-start correction and unresolved stall](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-162915-REENTRY.md).


An accepted Start is a live resource owner, not unsettled Record geometry: the final geometry query
waits on actual stop jobs/accepted Stop only. Master-length adoption need not wait for Start admission.
[Contract and evidence](../../../openspec/changes/shipping-rolling-activation/evidence/2026-10-25-post-record-overdub/EXECUTION.md).

The clock-side frozen Record reader and main rolling-maintenance owner preserve the same qualified
Serving frame during PLAYING and OVERDUBBING. Epoch, generation, receipt, storage and coverage checks
still control reuse; stale identity, Stop and clock-epoch changes cancel normally. Exact native
main/clock interleaving is tested across four subsequent bars with an active empty overdub; this
does not qualify nonempty overlay parity, full-loop wrap or physical output timing.
[Immediate-handoff regression evidence](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-REGRESSION-EXECUTION.md).

Record pass publication changes canonical revision, not the qualified sealed Capture interval. Frozen
ready/in-flight playback work retains its own source stamp while token, playback generation and loop
geometry still match. Canonical bridge-base setup detaches a completed shared-builder witness with
the existing protected-destination discard; it must not reset that exact complete current/ready role.
Default native integration also covers nonempty overdub Stop → late Record publication → subsequent
PLAYING or overdub, checking recorded events after every tick across five bars through current-root
return. This filtered recorded-stream oracle is not full record+overdub overlay/hardware parity.
[Publication/stop handoff regression evidence](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-152128-PLAYBACK.md).

The ordinary active-slot committed requester does not admit demand while a qualified frozen
Record reader owns the interval and no current root exists. Pass publication alone is not root
readiness: admitting committed debt at that point would take holder priority over frozen preparation
and strand the audible reader. Frozen coverage continues through the existing owner; current-root
publication re-enables the ordinary request/handoff. The registered native oracle drives main's
requester before rolling maintenance, gradual finalization, Stop and successor reentry, and checks
every recorded edge over16bars, including NoteId and original tick/channel/velocity. This is not
device timing or full overlay parity.
[Requester starvation repair](../../../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-26-233615-PLAYBACK.md).


## Runtime request

```
Derived Event Representation  +  Playback interval  →  MIDI out
```

| Input | Today |
|-------|-------|
| Representation | `LoopPlaybackRuntime` (`mergedEvents`, `playbackOrder`); current-root NOTE_EDIT: committed playback interval plus identity-qualified `NoteEditCurrentState` / session-store overlay; stale-root NOTE_EDIT: the session's materialized projection |
| Interval | Rolling cycle via `projectionCycleStartTick` + `playbackEventPhase` per event |

`currentTick` is the clock. `Track::playMidiEvents` does **not** read `visualCache`. It phases `currentTick` and sends from `mergedEvents` (raw MIDI). Long loops gather **2 bars** around the playhead; short loops gather the loop. Live overdub adds `loop.capture.store`. That is the play consumer in [DerivedViews.md](DerivedViews.md) § Consumers.

---

## Owner

**`Track`** owns playback runtime per slot. **`TrackManager`** orchestrates transport-wide `updateAllTracks`.

Playback **does not** own storage or display representations.

---

## Build policy

- **No** `LoopPlaybackRuntime` allocation on first tick after transport start (prewarm when heap allows).
- Rebuild playback order off hot path when `invalidateCaches()` or pass revision changes.
- NOTE_EDIT: `sessionPreviewRevision_` gates preview refresh. For a current published root, the playback interval remains the committed base and only changed/hidden/deleted session identities overlay it; hidden/deleted active soundings close by identity. A stale-root session keeps its materialized session projection. Neither path reads display notes as playback authority or rematerializes another whole-loop copy on the current-root path.

---

## Demoted-target launch during migration

`TrackManager::servicePendingSlotProvision` reserves cold rolling-only storage and
records an unpositioned immutable launch request for an inactive demoted target.
`Track::stepPlaybackViewMaintenance` owns bounded source count/fill/order/retirement
and ready publication. Qualification fills the unpublished build role in one
bounded source pass, then pauses before ordering/admission; no ready/current view
is modified. Admitted work excludes that completed fill and preserves its original
positional promise. The actual switch boundary revalidates current revision,
operation generation and phase-zero coverage, then binds the complete ready view.
A successful empty interval is valid no-new-events coverage. A pass-backed target
without a current root requests the existing on-demand committed-pass republish,
once while no source job is pending; the Snapshot owner constructs it off-clock.
This does not enable bulk loaded-slot hydration. The launch waits for that source.
The initial window's revision stays fixed during its traversal; expiry is sticky,
so later wraps do not revive it. Capture admission upgrades the same reservation
without resetting prepared content. Blocked targets do not hide later provisioning.

The mutable fallback remains beyond initial coverage and on the named legacy dense
overflow route (`launch_legacy_dense`); accepted events are not truncated to fit.
Task 3.7 — fallback removal and task 3.10 — continuous service qualification remain
gated. Native launch correctness is verified, but device launch/timing and sustained
refill are not qualified. Off-clock legacy gathering is not a timing guarantee.

## Gated current-root rolling handoff

The off-clock `serviceRollingPlaybackWindow` prologue selects later two-bar
coverage through the existing request/maintenance owner. At the exact absolute
boundary, `resolveRollingPlaybackWindow` validates the frame and complete private
candidate before adoption and existing MIDI emission. Cursor transfer and physical
storage generation are part of the handoff; an empty interval means no new events,
not silence. Stale/late/unsupported transitions retain mutable fallback.
`PLAYBACK_WINDOW_ROLLING_ENABLED` defaults0; explicit native/capture qualification
is required to enable it. Later lookahead plus fallback gaps is not continuous
service. Capacity, service constants and fallback-removal prerequisites are unchanged.
See the window change's `evidence/requester-retry-integration/ROLLING_HANDOFF.md`.

Committed successor positioning reuses the bounded absolute future-occurrence search when the derived work cannot
fit the first handoff. The chosen absolute deadline transfers unchanged through pending/frozen/admitted/ready;
an earlier same-phase occurrence cannot adopt it. Main's ordinary committed requester preserves a matching rolling
obligation while the rolling owner refreshes a superseded source revision. Service quanta, denial allowance,
currency/miss guards and fallback-gap policy do not change. DEC-156 supersedes only DEC-155's one-traversal cap;
[native proof and pending device adoption gate](../../../openspec/changes/shipping-rolling-activation/evidence/2026-10-24-2.2-fixed-bridge/SUCCESSOR-DISPLAY.md).

Whole-bar loops need not contain an even number of bars. Rolling starts use the loop-local two-bar
grid; the final window is clamped at the loop end (one bar for odd-bar loops), and the next legal
boundary is phase zero. Work pricing includes that terminal interval; ready adoption and serving
expiry validate its exact span. Fractional-bar geometry remains excluded from this committed
requester. Source/role/storage generations, paid deadlines and fallback policy are unchanged.
[33-bar native repair and device limit](../../../openspec/changes/shipping-rolling-activation/evidence/2026-10-24-three-playback-issues/ODD-BAR-REPAIR.md).

## Native cold live-Capture qualification

The existing Track window owner may publish a complete silent first-Record
Capture current at native clock epoch 0, replacing only the unpublished empty
raw shell through the existing three-view lifecycle. The clock reader uses its
own provisional coverage period without redefining final Loop geometry. Served
content/token stay immutable across accepted input; a mismatching real stop
invalidates its positional frame while qualified frozen coverage takes over.
Source hold and release follow the Track finalizer before publication and the slot-owned
presentation after publication; see the contract below.
This is a bounded native qualification increment, not running-epoch preparation,
late-input service or default/device one-reader enablement. Default rolling
remains OFF and compatibility emitters remain; existing bound classes and
capacity/service/retirement gates are unchanged. Evidence:
[I5 — cold publication and stop handoff](../../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-capture-publication/EXECUTION.md).

## Record finalization and presentation lifetimes

The stable slot-owned `CaptureStopPresentation` holds the sealed extent from admission.
Before publication, the matching live Record finalizer authorizes frozen-source preparation
and the existing playback reader adopts only completed windows. Non-empty `RecordPublished`
activates the runtime presentation owner, releases the finalizer, and pops the interaction FIFO
in that same transition. Reader/root-return duties do not keep an immutable slice in the FIFO.

`serviceCaptureStopPresentations` retires through ordinary deferred turns independently of
window-holder availability. While audible it yields until the matching current-root reader
has actually taken over; this yield cannot prevent an accepted successor interaction from
progressing. Stopped successors retain the existing bounded expiry. Partial one-chunk extent
release remains active/owned until all chunks are returned, then drops the Loop borrow and
re-arms the descriptor. Dormant boot/load descriptors reserve no pool tails; cold capture
admission reserves the requested slot's tail. Default rolling remains OFF.

Decision: **DEC-142 — Record presentation lifetime split**. Native/review/resource and device
evidence: [presentation execution](../../../openspec/changes/windowed-playback-generation/evidence/2026-10-18-rolling-base-2bar-rerun/PRESENTATION-OWNER-NATIVE.md).


## Native input-continuation qualification

DEC-122 — native-gated MIDI input continuation keeps one cold-provisioned
MidiHandler-owned retained batch and routes at most one message per native
qualified poll, preserving USB/DIN transport grouping and host FIFO. Pending
input survives transport/epoch changes; this is not a playback publication owner
or a physical clock-delay budget. `MIDI_INPUT_CONTINUATION_QUALIFICATION` defaults0
and target activation is compile-time rejected. Default device polling/clock,
rolling defaultOFF, compatibility readers and their bound classes remain unchanged.
[I5 — native continuation mechanics](../../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/CONTINUATION-EXECUTION.md)
records the actual state price, real-input/lifecycle verification and remaining
main/checkpoint/source/physical timing gates; nativeGREEN is not device readiness.

## Invariants

1. Sort keys use precomputed `playbackEventPhase` — not allocating projection in comparator.
2. `Loop.nextEventIndex` remains event scan index; projection cycle is separate rolling origin.
3. Queued start (`queuedStartTick`) commits at grid — updates `projectionCycleStartTick` at commit (UIP Phase 4).
4. **Playback is read-only** — emit MIDI and advance indices only; never append to capture, synthesize note-offs into storage, or modify `pendingNotes`. See [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) capture ownership invariants.

## Active note ledger

`ActiveNoteLedger` holds one entry per `(channel, pitch)` sounding at `currentTick` until its NoteOff.
An **ActiveNote** carries no `length` / `endTick`; it is not note geometry (`NoteSpan`) and not the LCR
`PresentNote` query.

| Concern | Site |
|---------|------|
| Written from **committed** playback | `playbackCursorAdvanceSend` → `applyPlaybackLedgerEvent` |
| Not written by capture overdub emit | `playbackCursorAdvanceSendCapture` |
| Read by | `sendMidiEvent`, `collectOverdubNoteOnParticipantIds` |

Mute still writes the ledger.

---

## Related

- [Storage.md](Storage.md) — authoritative passes
- [Snapshots.md](Snapshots.md) — `LoopSnapshot` source, construction, and publication
- [DerivedViews.md](DerivedViews.md) — event representation
- [IntervalProjection.md](IntervalProjection.md) — phase / wrap
- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — merge / materialize
- [playback_gather_lcr_consume_enhancement.md](../../Plans/playback_gather_lcr_consume_enhancement.md) — Stage 1 lateness hooks; LCR consume still queued; same `lateness_us` is the scheduling musical gate (not remaining OpenSpec 6.3)
