# Future — root-independent (window-first) playback

**Status:** parked design note. **Not authority, not scheduled, no implementation approval.**
Moved out of [`display-freeze-under-overdub-chain`](../../openspec/changes/display-freeze-under-overdub-chain/design.md)
on 2026-10-29 (DEC-193) because the active defect is a **regression at the DEC-191 image boundary**, not a
demonstrated architectural invalidity of the existing playback path.

**Reopen only if** the narrow regression repair is proven impossible — i.e. the last-good image's playback
depended on an unsafe or accidental behavior that cannot legitimately be restored. Until then this document
is a record of a rejected-as-premature direction, not a queue item.

Active work: [`playback-regression-after-retained-start-fix`](../../openspec/changes/playback-regression-after-retained-start-fix/proposal.md).

## Why this direction was raised (historical)

After the retained-Start stall work (DEC-191) the device stopped emitting playback for an already-loaded,
already-playing passes-only slot. A first repair (DEC-192, on-demand whole-root publication from the
deferred Snapshot owner) eventually produced audio but only after a new whole-loop root had been built —
about 52.75 s on the owner's slot. That behavior was rejected as playback acceptance: the owner needs the
first scheduled event, not eventual recovery.

The conclusion drawn at the time was that a root must stop being a playback prerequisite. That conclusion
is still **unproven as a requirement** for the current defect: it explains the *rejected repair's* latency,
not why the last-good image played at once. See the active change's boundary evidence.

## Required behavior (as captured at the time)

1. **First scheduled event is audible after boot.** Starting any restored slot in the supported launchable
   set emits its first due On/Off through the normal MIDI output path without waiting for a newly
   constructed whole-loop `LoopSnapshot`, and without a second Play press, stop/restart, re-record or
   destructive slot operation. Include an onset at tick zero; delaying transport or dropping early events
   while a root builds is not immediate playback.
2. **Boot readiness includes start coverage.** Before declaring the promised slots playback-ready, their
   start coverage must be source-qualified and available to the normal reader. Active-role initial windows
   and a measured, capped standby envelope are candidate mechanisms, not a selected storage layout. Resolve
   how any supported slot can satisfy this guarantee without allocating a full working set for every
   recorded slot. Do not weaken "any slot" to only the selected or warm slot, or move the same
   minutes-long wait into boot and call the problem solved. Set and measure boot-to-playback-ready and
   Start-to-first-event budgets before implementation acceptance.
3. **A root is not a playback prerequisite.** Reuse a loaded valid root when available; otherwise build
   requested immutable coverage from a generation-qualified retained committed source. `LoopSnapshot`
   remains the complete effective-content/checkpoint authority. An unfinished candidate is never
   advertised as a root or a playable window. Piano-roll notes are not a source of playback events.
4. **Continuous coverage, one owner/reader.** `Track::stepPlaybackViewMaintenance` prepares upcoming
   intervals ahead of the playhead; the clock edge consumes only complete immutable coverage. All
   supported active, layered, launch, capture-return and edited routes converge on that logical
   request/build/adopt/emit contract, regardless of loop length. No new permanent merged/paged/bridge
   emitter, whole-loop fast path, or 16-bar correctness branch is introduced. Existing migration fallbacks
   retain explicit cutover/removal gates, not acceptance by assumption.
5. **Retain the source, not just the last window.** A served version and the source needed to build its
   later intervals survive until their reader/successor obligations end. On a late content update, retain
   the previous coherent version for the allowed traversal under existing currency rules. This does not
   mean repeating the last small window, replaying old notes, using an expired interval, or keeping a
   source whose undo/edit state no longer matches its identity.
6. **Checkpoint work is independent background work.** Root publication and persistence may catch up while
   music continues. Enabling, disabling, blocking or completing that work must not remove audible coverage
   or monopolize the preparation opportunities owed by sounding slots. No new whole-root rebuild at Start,
   synchronous full gather, or whole-history normalization on the MIDI edge is an acceptable replacement.
7. **Preserve content and closure obligations.** Record/overdub commit, undo/redo, load/clear, geometry
   changes, slot/focus changes and clock epochs revalidate the actual slot/generation and transfer source
   holds exactly once. Never truncate accepted content or release a held note/source to make a readiness
   test pass. Transport Stop remains an explicit user action, not a repair step.

"Always playing" is the supported-envelope readiness obligation, not a guarantee against power loss or
arbitrary hardware faults. Unsupported/resource-fault cases must be named and preserve accepted content and
existing soundings; they cannot be silently counted as supported immediate launches. Capacity shortfalls
remain blockers rather than permission to narrow the owner's goal.

## Existing foundation and unresolved integration work

- VERIFIED foundation: `retainPlaybackSourceForSlot`, the retained-source gather and
  `Track::stepPlaybackViewMaintenance` already provide generation-qualified committed-source and
  interval-build mechanisms. Reuse these owners rather than adding a Manager or reading `visualCache`.
- VERIFIED gap: ordinary boot prewarm does not by itself establish a retained source, qualified start
  coverage and an audible reader for every launchable rootless slot. A publisher request is not that
  handoff.
- UNKNOWN readiness cost: current retention pins chunk/pass metadata and builds an interval index;
  gathering also resolves edit rows. Merely calling these methods at Start is **not** evidence of bounded
  startup work. Trace/price their allocation, enumeration, sort, index and retirement units before
  selecting a mechanism. Window-sized output alone does not bound the source work.
- UNKNOWN lifetime closure: current retained-source readers consult live pass/edit structures. Prove which
  membership/body/state is immutable for a held generation through undo/redo, clear, load, successor
  capture and publication; ID pinning alone is not that proof. Record any needed lifetime-contract change
  explicitly under the existing readiness/reassessment rules.
- UNKNOWN admission/standby layout: identify exact producer, ready publication, borrow/adoption,
  pool/cap/eviction and release for active and inactive targets. Price actual RAM1/PSRAM/LoopPool costs and
  use the current supported track/slot/role envelope, not an unqualified product target.
- UNKNOWN service guarantee: derive window size/lookahead from declared source work, contention, supported
  tempo and available owner opportunities. No guessed window size or between-grain timer check may stand in
  for a bound on one indivisible step.

G0 (implementation discovery) is PARTIAL for this direction; G1 (source/admission architecture) and G2
(executable contract) remain blocked on the unknowns above.

## Acceptance gates (unstarted — do not treat as a task list)

- **Root-disabled startup:** restore a passes-only fixture through the production load/admission path and
  keep new whole-root publication disabled before Start and throughout the run. Do not inject a retained
  source, ready window or successful readiness stub that production never supplies. Continuously advance
  time/ticks at the declared cadence, including setup/preparation, and assert first due output at tick zero
  and later first-note positions. No unpriced warmup or frozen-clock drain. Check short/long, sparse/dense,
  record+overdub+edit content and different slot/track focus.
- **Any-slot launch:** exercise active and inactive targets across supported slots, multiple Tracks and
  enabled layered slots. A single warmed/selected slot is insufficient. Establish the standby cap and
  launch coverage before the scheduled first tick; existing music continues through switches.
- **Sustained playback without a root:** several full loop traversals, interval boundaries, odd-bar tails
  and wrap-crossing held notes must emit the independent expected inventory exactly once, with original
  NoteIds/order, channel remap and closure settlement. No Stop/restart or root-ready rescue turn is
  permitted. Do not freeze time to replenish coverage between traversals.
- **Mutation and lifecycle continuity:** interleave record/overdub stops and successor starts, undo/redo,
  pending publication/persistence, focus/slot changes, explicit transport Stop/Start, clock-source/epoch
  transitions and clear/load while work completes. Assert generation-qualified consumption, no foreign
  completion/release, no lost accepted requests and no false readiness.
- **Real dispatch and budgets:** use production request/build/adopt/emit and main's actual shared
  fairness/opportunity limits. No extra per-Track grants or success stubs on the asserted path. Measure
  first-event and sustained deadline lateness, individual work-unit cost, MIDI Input Gap, boot latency and
  memory at supported contention; an unset/skipped budget is not a pass.
- **Physical USB output:** compare actual USB-to-Mac NoteOn/NoteOff delivery with the independent expected
  stream. `MO`, `#PAINT_BEAT out` and the native pre-handler sink record attempts before the hardware call,
  not wire delivery. LED channel-15 activity and a moving cursor prove neither musical output nor source
  readiness. Hardware runs require separate permission and a registered, non-destructive stimulus.
- **Discriminating mutations:** withholding source admission/start coverage, waiting on root currency,
  releasing a serving generation early, starving a background sounding slot or bypassing the retry park
  must each fail the corresponding oracle.

Before an implementation slice, fill its execution packet with the resolved entry/worker/commit/release
paths, first-event and continuous-coverage oracle, resource price, fault/cancellation policy, exact test
commands and supported-envelope evidence. Any new source lifetime or working-set contract requires its own
explicit decision/review; nothing in this note authorizes firmware edits.

## Rejected variants (keep for the record)

- Whole-root publication triggered from the deferred Snapshot owner (DEC-192): produces audio only after
  root construction (52.75 s measured). Historical/eventual-recovery only, superseded as playback
  acceptance.
- Frozen-clock native oracle: measures eventual recovery, not startup. Rejected as an acceptance shape.
- Unconditional barrier clear and load-time rebuild: removed; neither had a discriminating fixture.
