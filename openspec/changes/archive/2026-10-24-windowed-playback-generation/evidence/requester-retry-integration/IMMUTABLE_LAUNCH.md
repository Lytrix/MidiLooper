# Immutable initial launch — execution packet

Task 3.10 requester integration; DEC-108 items1/3/5/6/10/13. Existing owners:
TrackManager pending-switch intent -> SlotProvision cold reservation/request ->
Track maintenance bounded cursor/count/fill/order/publication -> actual switch
boundary -> fixed-window MIDI consumer. No new owner/scheduler/event buffer.
Decision review: existing ready/current/build roles, explicit cursor retirement,
source revision and operation generation are reused. An inactive target has no
absolute deadline: launch recipes are explicitly distinct from positioned demand.
Do not grant a fictional outgoing-phase deadline. Do not weaken existing promises:
launch qualification waits for sibling work; positioned admissions price it.

Rolling-only prewarm reserves three existing views, no capture staging/companions
or whole-take representation. Normal capture prewarm must upgrade in place without
resetting ready data. Layout/byte accounting and target RAM1 must be verified.

Oracle: queue long target -> provision records recipe only -> maintenance freezes,
counts, admits unpositioned build, fills/orders and publishes ready -> no audible
change until current source/revision/generation/coverage at valid boundary -> bind
initial window and consume. Empty completion is valid no-new-events coverage.
Root absent waits, source change re-prepares; cancellation/replacement clears only
launch-owned metadata and retires pins in bounded turns. Returning to target must
not revive the earlier operation. Bound window only covers the first traversal,
never silently reuses a prelaunch candidate on later laps. Existing mutable fallback
remains after initial coverage; this is not continuous immutable activation.

Native real TrackManager queue/provision/maintenance/tick fixtures, empty/nonempty,
late source, stale/cancel, no launch rebuild, rolling-to-capture upgrade. Existing
retry/deadline/source lifetime tests must stay green. Full native, capture/RAM1,
strict OpenSpec, diff check; registered device qualification NOT RUN unless genuine
stimulus exists. Baseline dirty tree atop25542299; preserve previous work.
Status: specified, verification pending. No task3.7 removal or timing qualification.

## Verified result / execution record
Production SlotProvision now requests immutable preparation, and the clock switch
binds it before moving the active slot/projection. It does not build on that edge.
Native queue/provision/maintenance/updateAllTracks fixtures verify nonempty/empty,
late source, no mutable rebuild at first target tick, no artificial deadline,
identity/coverage/completion readiness and no capture/whole-take cold reservation.
Six new lifetime/liveness fixtures verify cancellation at seven progress points,
generation/revision requalification, geometry rejection, first-traversal/version
fixity and sticky expiry, capture-upgrade preservation/shared-worker exclusion,
legacy dense fallback, blocked-track scanning and temporary sibling interference.

The explicit dense fallback is required to preserve existing accepted material:
counting beyond2,048 refuses immutable publication, drains pins, then names
`launch_legacy_dense` and uses the existing untruncated mutable preparation. This
is NOT the future bounded paged dense path, nor a new supported-envelope claim.

One read-only review found two concrete liveness defects. Before fixes, dedicated
fixtures failed on (1) a source-blocked lower track hiding later provisioning and
(2) AwaitAdmission expected2/actualRetiring4 when a sibling appeared. Both fixed:
continue scanning after no work; launch interference waits without assigning a
terminal deadline refusal. No second review round. Initial write delegation was
blocked by that subagent's workspace restriction; parent implemented directly.

Checks:
- ownership122/122 and prewarm196/196 (combined318/318).
- final `pio test -e native`:2,775 total,2,768 passed,7 skipped,0 failed,177.685s.
- final `pio run -e teensy41-capture-serial`:SUCCESS41.068s; RAM1 variables96,192,
  code391,512,padding1,704,locals34,880; approved padding floor840 unchanged.
  Runtime target7,072/host9,096 size pins still pass; new metadata uses padding.
- Final-source mutations: readiness-generation check, immutable hot-path selection,
  cancellation hook; all fail named fixtures and restore byte-identically. Ordering
  direction/final-normalization mutations also caught (five total).
- strict OpenSpec and `git diff --check`:PASS.
- stimulus registry listed (11 entries); no dedicated immutable launch/refill
  qualification profile. Device/HITL NOT RUN, image NOT uploaded, no commit.

Failures retained in scratch logs: first focused run exposed old synchronous
fallback fixtures and obsolete sort-cost assertions; these were updated to the
new production path and independently measured work. One diagnostic call had a
const mismatch. New fixture setup initially left its target active; fixed the
outgoing slot precondition. None is reported as a passing run.

Remaining: continuous producer/consumer refill with eligible-boundary adoption,
all note-closure/clock/competing-owner continuity obligations, and registered
hardware proof. Launch wait-for-ready is not sustained timing qualification.
See `immutable-launch-verification.json` for final source/image hashes.
