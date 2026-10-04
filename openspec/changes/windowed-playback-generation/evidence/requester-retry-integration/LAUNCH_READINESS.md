# Production launch readiness repair and continuous-refill finding

## Scope / decision review (before firmware edits)
Task 3.10 — requester integration. DEC-108 — window publication/source retention,
items 2, 6, 10 and 13; DEC-102/103/104 — off-clock provisioning and retained hold.
Existing owners: TrackManager pending switch -> SlotProvision -> Track's existing
fallback preparation -> updateAllTracks boundary. No ownership/format/buffer change.
Mutable fallback remains until task 3.7; this is NOT immutable launch activation.
Baseline: dirty tree atop 25542299, including completed retry/topology work.

Code trace: provisioning only prepares when occupancy is missing; a root arriving
later cannot release that hold through this owner. Inactive target gathering uses
the outgoing projection, whereas the commit launches at target phase zero. Legacy
readiness ignores coverage and rejects successful empty gathers. The commit calls
the builder again, and an unsynchronized cache generation can erase preparation.

Historical search: DEC-108 and the active tasks explicitly retain the mutable
fallback until verified replacement, require prepared-before-bind and distinguish
empty completion from missing source. Extend these owners; do not create a new
publisher or pretend the fallback is the bounded immutable worker.

## Fixed oracle
L1: queued long target already has occupancy but no root. Provisioning preserves
outgoing/pending; source arrives later; provisioning prepares phase-zero coverage,
without changing the outgoing projection; next eligible boundary binds target.
L2: outgoing phase is many bars from zero. Prepared target covers [0, 1536), not
outgoing phase. Generation and order stay valid through first playback; no inline
rebuild is needed for that launch.
L3: a current root with no events in the launch interval is ready; missing source,
wrong coverage, dirty order, stale revision/generation are not ready. Empty cache
is reused at launch, not rebuilt merely because it has no events.
L4: cancel/replace pending target cannot bind old target; source/currency changes
hold until prepared again. Existing pending-switch and content tests stay green.
No new absolute deadline is invented for inactive targets.

Controls: existing native TrackManager/Track/ClockManager harness and real published
snapshot fixtures; bounded tick loops, no sleeps or synthesized immutable publication.
Focused: pio test -e native -f test_runtime_working_set_ownership. Broad: pio test
-e native; pio run -e teensy41-capture-serial; strict OpenSpec; git diff --check.
Mutation: remove off-clock retry / phase-zero preparation / empty reuse and verify
named assertions fail, restore byte-identically. Device NOT RUN in this slice;
registered requester stimulus and timing qualification remain owed.

## Continuous two-bar finding (independent read-only trace)
At H=8, 64 ticks/opportunity and 1536 ticks/window the lane supplies 24 turns,
only three per holder per replacement. Margin768 leaves just one chargeable grain
for a fresh two-bar deadline. Empty lifecycle costs six selected turns. At N=2048,
event copying alone needs >=32 fill turns. This disproves sustained support under
the present declaration even if counting, sorting and retirement were free.

For nonempty requests: G=2+2*ceil(U/64)+Oc+ceil(N/48)+Op+ceil(A/64), where U is
inspection/page/bar work and A actual retirement. At N2048 each worst-case sort
is982 turns; ranks43. Packed aligned U2178 gives2079 turns before retirement,
2080 shared/2114 unique. Maximum topology gives2624 lifecycle turns. Initial
8-bar lead does not improve this producer rate or enlarge three two-bar buffers.

Preferred next work: existing-array ping-pong ordering and combined private
qualification/fill reduce real work; neither alone can meet the copy lower bound.
A supported service/work design needs measured bounded grain duration and a
justified opportunity guarantee, then explicit timing-model reassessment. No
constant/capacity rebase, new scheduler/buffer, or activation is authorized by this
finding. Continuous qualification and immutable launch binding remain open.

## Completion
Native/build verification complete below; activation and device qualification remain open.

## Verified completion / limits
- Baseline test compile first failed on a nonexistent accessor; corrected to the
  existing `getPlaybackGeneration()`. The subsequent red run failed on occupied
  retry, phase7112 vs zero, and generation-stale launch cache (three named failures).
- `pio test -e native -f test_runtime_working_set_ownership`: final116/116.
- Three final-source mutations caught: retry removed; launch uses outgoing phase
  (`Expected 0 Was 7112`); empty reuse removed (launch rebuild counter13->14).
  Every source restored byte-identically; restored focused run116/116.
- `pio test -e native`: final2,766 cases,2,759 passed,7 skipped,0 failed,177.023s.
- `pio run -e teensy41-capture-serial`: SUCCESS6.806s (incremental final build).
  RAM1 variables96,192,code390,856,padding2,360,locals34,880; floor840 unchanged.
  No runtime layout or event-buffer growth. Initial build also passed12.742s.
- Final diff inspection preserved nonempty NOTE_EDIT cache reuse (null snapshot
  provenance); full native and target build above were rerun after that correction.
- Strict OpenSpec validation and diff whitespace check pass. Initial hash command
  used the wrong default build directory; corrected to configured PlatformIO cache.
- Device/HITL: NOT RUN; no upload. No device readiness or timing qualification.
- L1/L2/L3 exercised through real queue/provision/updateAllTracks; existing cancel
  fixtures remain green. Dedicated new cancellation/replacement interleavings and
  immutable candidate survival are NOT claimed by this compatibility repair.
- No DEC supersession: existing owners/fallback gate retained. No deliverable ships;
  no task checkoff. Approved capacity, service constants and RAM1 floor unchanged.

Remaining implementation: rolling-only provisioning with later capture upgrade;
unpositioned immutable launch recipe; generation-qualified candidate binding and
real boundary adoption; repeated consume/refill through wrap with note closure
parity; registered requester HITL. No prelaunch absolute promise was fabricated.
See `launch-verification.json` for image/source hashes and mutation failures.
