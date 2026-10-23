# Qualification unblocking continuation

User requested further implementation to unblock live operation. Follow existing
DEC-108 — window publication/source retention and DEC-073 — effective currency;
CURRENT_WORK task3.10 scope. Existing owners only, no scheduler, capacity reduction,
RAM1 rebase or fallback removal. Prior evidence remains valid for its own image.

## Slice A: topology-qualified retirement (contract before edits)
The current retry charges every source 13,059 retirement units /205 turns, even
empty pinned bars. A completed immutable cursor traversal already counts every
reference, every page advance and every bar exit. Therefore:
`pages = readUnits - inspectedReferences - pinnedBars`;
`releaseUnits <= pages*(membershipPageCapacity+1) + pinnedBars`.
This prices ALL fixed reference slots in every pinned page, including holes and
zero-live-count pages. It is independent of event-output count and of whether a
head is shared now. No scan, additional owner, allocation or runtime layout growth.
Incomplete/cancelled traversal retains the existing maximum bound; only complete
qualification may use the tighter value. Rewind invalidates that qualification.

Track admission uses the bound before rewind, prices retirement in the same lane
contract, and still checks residual ORIGINAL absolute lead. No allowed service-gap,
H8, one-bar margin or work quantum changes. This can unblock sparse long-lead
requests, not the two-bar sustained throughput claim.

Oracles: known nine-page source yields156 units; retire root then cursor and verify
actual release equals bound; null bars cost one unit each; rewind/incomplete walks
retain maximum; partially populated/empty membership pages price all16 slots;
real Track request that previously paid205 release grains admits on a shorter
original deadline with observed topology, while residual-lead refusal stays green.
Focused source and ownership fixtures, mutation of page cost/admission charge,
full native and capture/RAM1 checks before closeout.

## Production reachability investigation
SlotProvision is the existing cold requester/provisioning owner. Its current
`prewarmPlaybackForSlot(false)` establishes occupancy, not rolling views, while
`prewarmViews` also reserves capture/companion storage. Separate rolling-only cold
provisioning must preserve later capture upgrade and must not activate an
unqualified consumer. No production trigger is changed by slice A.


## Verified result
Slice A implemented. `EventWindowCursor::qualifiedReleaseUnitBound()` uses the
completed traversal's page count in constant work with no new state. Track
admission uses it before rewind. Incomplete/rewound cursors retain the maximum;
fixed page slots, not emitted/live events, determine the qualified charge.

- Nine-page/three-bar source: bound156 units, actual uniquely-owned retirement156
  after root retirement. Pricing only inspected references132 fails the fixture.
- Empty two-bar source: two release units /one retirement turn instead of205.
  Real Track request admits with four bars of original lead after128 elapsed ticks;
  origin and deadline remain unchanged. No larger gap or smaller margin/capacity.
- Production composition driven natively at64 ticks per lane opportunity, eight
  concurrent tracks: eight-bar initial lead completes all8 empty windows, each in
  six turns (freeze/count/admit/fill/rank+publish/retire),48 lane turns total.
  The same eight tracks with two-bar lead complete0, retain their original requests
  and deadlines, release pins, and refuse further unqualified progress.
  This proves preparation behavior under controlled clocks, NOT device scheduling
  or a continuous eight-bar-ahead consumer. Three rolling windows have not gained
  storage or a larger coverage horizon.
- Mutation restoring the blanket205-grain surcharge changes the positive eight-track
  test from8 completions to0. Both mutations restored byte-identically.
- Source47/47, ownership113/113; full native2,763 total /2,756 passed /7 skipped /
  0 failed (288.166s). Capture build SUCCESS56.92s; RAM1 variables96,192 /
  code390,728 /padding2,488 /locals34,880, floor840 unchanged. Runtime target7,072 /
  host9,096 unchanged. Strict OpenSpec and diff checks pass. No upload or commit.

## What remains blocked, and why
The minimum base contract still costs3 grains, H8/gap64 plus margin768 requires
2,304 ticks >two-bar1,536. Removing the false retirement surcharge does not alter
that fact; the real eight-track negative fixture now reaches the refusal through
request/offer/selection/worker rather than just arithmetic.

Production pending-switch preparation is not merely a missing function call:
`servicePendingSlotProvision` supplies occupancy and mutable readiness; target
projection is established by the eventual launch in `updateAllTracks`. Inventing
an absolute deadline for the inactive target before that frame exists would violate
the qualified-clock contract. A safe continuation needs explicit unpositioned
launch preparation/ready binding in the existing owners, rolling-only cold
reservation with later capture upgrade verified, and the dedicated registered
HITL route. Steady-state continuity additionally needs fewer bounded lane turns
or a proven faster service opportunity; changing constants or initial lead is not
that proof. No new scheduler, unsupported activation or fallback removal performed.
