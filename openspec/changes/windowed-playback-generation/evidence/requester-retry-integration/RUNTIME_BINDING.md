# Runtime snapshot fill/order binding and retry continuation

Task 3.10 — live requester/retry integration. Baseline 25542299 plus prior
uncommitted verified repairs and EventWindowCursor. User explicitly requests
runtime fill/order binding, live retries and timing qualification.

## Contract fixed before edits
Extend existing RuntimePlaybackViewFill, RuntimePlaybackViewBuild and
RuntimePlaybackOrderBuilder, then LoopPlaybackRuntime/Track maintenance. Follow
DEC-108 — single window publication owner/immutable candidates; DEC-073 — current
effective source. No new scheduler, event buffer, lowered capacity, RAM1 rebase,
SD format or fallback removal. Existing source cursor owns window membership pins.

Fill reads at most quantum cursor units, including filtered/page/bar work. It copies
into the unpublished prewarmed view. During this binding only, storage.phases holds
canonicalOrder and the unqualified passOrigins fields hold eventId high/low halves;
passOriginsReady remains false, so these are never presented as pass provenance.
First bounded order uses (canonicalOrder,eventId), bounded rank assignment records
that order in existing canonicalRanks, then ordinary phase/Off-before-On/raw-tick
order uses those ranks for ties. No source-order inference from page layout.
Source cancellation/rewind/reuse invalidates its traversal token, not content
currency. The source owner must drain explicit retirement before destruction.

B1: snapshot source with scrambled and tied 32-bit canonical order/event IDs,
same-tick CCs, NoteOn/Off, and rejected boundary rows. Real runtime build in small
quanta equals independent canonical gather + existing raw-source build row-for-row;
no complete flag before final order, no source/root dependency after fill.
B2: overflow, published destination, cancellation, cursor rewind/rebind between
fill grains refuse without partial publication. Empty window completes as valid
empty coverage. Existing prewarm allocations/capacities remain unchanged.
B3: source inspected work and two order passes plus rank assignment are charged;
no callback hides a scan; count qualification is separate bounded preparation.

Verification: focused playback-prewarm and ownership native suites, mutation of
canonical tie/cursor token, full native, capture build/RAM1, strict OpenSpec and
diff checks. Live retries and device qualification are not claimed by these tests;
subsequent binding must preserve original absolute deadlines, separate shared
builders, and make pending-only work eligible in the existing lane.

## Completion
Initial fill/order increment results (superseded by final retry verification below).
- New typed runtime overload uses the real shared fill/order builders and existing
  prewarmed arrays; no extra event/provenance allocation. Host and target runtime
  size pins unchanged (8,784 / 6,832).
- Focused playback-prewarm 193/193; ownership103/103; snapshot/store47/47;
  trio343/343. Full native not yet rerun for this increment.
- Three mutations (canonical ranks, event-ID high-half ordering, cursor traversal
  token) fail intended assertions; each restored byte-identically. Logs in scratch
  `binding-mutation-*.log`, `binding-trio.log`.
- Capture build SUCCESS49.156s, RAM1 variables96,192/code390,536/padding2,680/
  locals34,880; floor840 unchanged. `binding-build.log`. No upload or timing claim.
- Test setup correction: use public `storePublishedSnapshotForTest({},0)`, not the
  private clear method. Guarded edit stopped on a two-occurrence anchor; corrected
  with the raw-storage overload explicitly scoped. No unrelated changes reverted.

## Timing constraint carried into integration
Even the empty snapshot build costs one fill + one rank/order transition + one
conservative promotion grain. At H=8 and permitted gap64 ticks, with margin768,
its declared lead is 3*8*64+768=2,304 ticks, exceeding a two-bar (1,536-tick)
lookahead. Source counting/retirement can only add work. Thus the current declaration
cannot qualify continuous two-bar-ahead consumption at eight tracks, even before
content density. This is not permission to increase gap, reduce capacity, or claim
measured maxima as bounds. Retry plumbing can still be correctness-tested against
valid long-lead requests; live continuity qualification remains separate.


## Retained retry execution contract (before firmware edits)
Historical review: DEC-108 (single publication owner, bounded source, retained
promises), DEC-073 (effective source currency), existing Track lane and runtime
viewBuild extension points reused. No transfer, new scheduler, SD migration or
fallback removal. Active CURRENT_WORK explicitly covers this integration.

A committed request records an explicit replay kind; low-level borrowed/preview
requests do not become snapshot recipes. Runtime owns one window cursor and one
frozen request while a newer coalesced request may wait. Freeze, count (64 inspected
units), admission, fill (64), order/ranks (48), and retirement (64 release units)
are separate turns. Count reserves the existing builder; other owners cannot
advance or overwrite it. Admission prices observed source work, both order passes,
and conservative retirement; original absolute origin/deadline is transferred,
never recomputed from a later modular phase. Pending invalidation is sticky.
Preparation starts only with no admitted sibling work on the track; once started
its worst-case count/fill/order/retirement is charged as outstanding work, so later
admission cannot spend a protected sibling deadline. Preparation admission remains
separate from timing qualification and does not enable interference enforcement.

Cancellation detaches the builder then enters bounded source retirement; that
retirement keeps the slot owned and eligible even without a builder. A frozen
request cannot clear a newer pending request. Pending-only offers use the same
classification as the selected worker; blocked slots cannot hide runnable siblings.

Oracles: one request then dispatch-only completion, unchanged original deadline
with elapsed time, blocked+runnable siblings, borrowed/shared-owner exclusion,
count quantum, newer pending preservation, expired/reset clocks refuse without
new laps, cancellation retains pins until drained, empty negative timing envelope.
Full native and capture/RAM1 build before closeout; no unrelated baseline can
qualify this requester. Production boundary activation stays gated on geometry,
provisioning, registered stimulus and the documented negative envelope result.


## Final verification and qualification verdict
**Runtime binding and bounded retry scheduling verified; live activation/timing
NOT QUALIFIED.** `retry-verification.json` pins the source and built image.
No upload, device stimulus, commit, capacity reduction, or RAM1 rebase occurred.

Implemented within existing owners:
- `requestCommittedPlaybackWindow` records an explicit committed recipe, no
  scratch gather. The clock-derived overload uses effective transport tick and
  projection-cycle origin; non-active slots, jam and queued launches refuse this
  ordinary-projection overload. Explicit phase input remains available to owners
  with a separately qualified frame.
- Runtime-owned window cursor and frozen demand reserve `viewBuild` while counting;
  each selected turn freezes, inspects <=64 count/fill units, admits, orders/ranks
  <=48 units, or retires <=64 release units. No root/event-buffer allocation.
- Actual count qualifies admission; both sorts and rank assignment are charged.
  Retirement is conservatively charged at205 grains. Freeze/count/admit have
  already consumed time before the residual absolute lead is checked. They start
  only without sibling admitted work; outstanding preparation is conservatively
  priced for subsequent budget readers. Legacy interference enforcement remains
  gated: no whole-system promise is inferred from this accounting.
- One common offer/worker classification sees pending-only committed demand and
  retirement. Runnable siblings are not hidden behind blocked earlier deadlines.
  Generic shared builders are neither progressed nor published by the window lane.
- Admission transfers original origin/epoch/deadline; it does not compute a new
  modular lap. Newer pending demand survives older admission or cancellation.
  Source staleness is not clock invalidation (review regression). Pending clock/
  geometry invalidation and expiry are sticky and reported separately from admitted
  completion misses. Explicit boundary requalification remains owed.
- Cancel/reset detaches readers then drains source retirement. `noLiveOwnerForSlot`
  names this ownership; working-set release refuses while pins remain.

### Checks
- Ownership **110/110**, prewarm **193/193**; full native **2,760 total / 2,753
  passed / 7 skipped / 0 failed**, 169.504s. Includes earlier source47/47.
- Five retry mutations fail the intended assertion: reset original origin
  (`Expected1000 Was1321`), remove owner discrimination, let blocked offer hide
  runnable sibling, retire65 instead of64 units, and conflate source/position
  invalidity. All restored byte-identically. Earlier fill/order's three canonical
  rank/event-ID/token mutations are recorded above, not rerun as new work.
- One bounded review, one concrete defect fixed and mutation-proved. No review loop.
- Capture build **SUCCESS40.13s**. RAM1 variables96,192, code390,728,
  padding2,488, locals34,880; approved floor840 unchanged. Runtime target
  **7,072 B (+240)**, host **9,096 B (+312)**. Eight target runtimes add1,920
  external metadata bytes, no event-buffer growth. This is memory accounting,
  not a supported multi-track service-envelope claim.
- Strict OpenSpec validation and `git diff --check` pass.
- Recorded corrections: compiler size pins measured then updated; parity test
  initially compared canonical gather order directly with playback order, corrected
  to existing flat-builder oracle; first full-suite tool call timed out at240s,
  preserved as incomplete, final full run passed. No failures concealed.

### Remaining activation and timing blockers
1. New committed recipes are dispatchable through the **production lane**, proven
   by native calls to the real request/offer/worker/publication functions. No
   production gesture/provisioning trigger invokes the new requester yet. Cold
   rolling-view provisioning and boundary adoption/revalidation are not closed.
2. Registered stimulus list was read successfully; no entry exercises this
   requester. No unrelated shared-builder baseline was run or counted as proof.
3. The empty base build still needs3*8*64+768=**2,304 ticks**, greater than two-bar
   **1,536** lookahead. Conservative count and retirement maxima each add205
   grains (13,059 units at64). Existing measured gaps112/168/149 exceed64 and
   remain observations, not bounds. No new device timing evidence exists.
4. Keep mutable fallback, current capacity and disabled legacy interference gate.
   Do not check off task3.10 (live service envelope) or task3.7 (fallback removal)
   from these correctness results. No new architecture decision: this implements
   the existing DEC-108 contract and preserves its pre-existing activation gates.
