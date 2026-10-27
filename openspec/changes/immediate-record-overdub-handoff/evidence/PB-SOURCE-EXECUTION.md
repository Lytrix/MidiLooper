# P-B — private-source preparation and immediate ingress

## Execution identity and preflight

- Task2.2 (C3 implementation half); [P-B packet](../packets/P-B-SOURCE.md), [contracts](../CONTRACTS.md) C3.1–C3.6, DEC-159 custody, DEC-142 presentation lifetime split.
- Branch `feature/unified-rolling-playback-path`; no commit/reset/rebase, no upload/device/SD/content action. Installed `74392e15` unchanged.
- Historical reuse: existing `Loop` source-view/span-cache owner and the bounded-cursor pattern of `RecordSourceStaging`; no new Manager, no second reader, no whole-take derived vector, no relabel of private rows as committed.

## What changed

| Owner | Change |
|---|---|
| `Loop::SourceOrigin` | new `PrivatePredecessor` member. Reads from it are visibly non-committed: published revision stays 0 and published identity stays null, and no committed reader consumes the private cache. |
| New `LoopPrivateSource.cpp` | the resumable cursor: bind by `(receipt, loopId, interval, frozenCaptureContentGeneration, playbackRevision)`, `step` at most 32 units per grain, finish/discard, retained hold intents. One unit is exactly one sealed-range row read, one pair decision, or one pending-change upsert. Pair table 16, retained holds 4, overflow denied by name (`pair_capacity`, `hold_capacity`). |
| `Loop::rebuildOverdubSourceSpanCache` | private origin branch; published stamps never advance for it and there is no committed merge; a pending scan makes the cache non-ready rather than partially usable. |
| `LoopPendingNoteChange.cpp` | `resolveOccupancyAtNoteOn` retains the hold intent without waiting for the build; `stepPrivatePredecessorHolds` materialises decisions; `sealPendingNoteChangesToEditPasses` names the C3.4 denial. |
| Supersession | `Loop::notifyCommittedContentChanged` and `openOverdubSession` discard the cursor/holds on publication, clear, slot switch, load, transport stop and record re-entry; a partly-staged private prefix is never mixed with a committed suffix. |
| `Track` | `bindPrivatePredecessorSourceForOverdub` at `startOverdubbing`, plus `stepPrivatePredecessorSourceGrain` steps; the receipt is revalidated every grain and a published/retired/moved receipt discards. |
| Resource | `sizeof(Loop)` pin re-measured **1008 → 1784** (`LoopPool` allocates externally); two `.rodata.str*` carve-outs in the linker script avoid a +4,096 DTCM align snap. RAM1 unchanged. |

## Oracle results

- **P-B-1** `test_private_record_is_the_same_pitch_overlap_source` — **GREEN**. A sealed, unpublished A appears in B's same-pitch hold source with its real accepted `NoteId1`; origin is `private_predecessor` with published revision 0 and null identity; the hold decides A's pair (12..63), matching the sequential canonical control for the same pitch set.
- **Canonical control** `test_control_canonical_predecessor_is_visible_to_same_source_query` — still **GREEN**, now with a per-target differential against the private run.
- **P-B-2** no interval leakage: C starts with 0 staged rows, re-reads 64 rows, and its decisions carry none of B's ticks.
- **P-B-3** mid-scan publication: after one grain the Snapshot owner publishes A; the cursor is superseded, staged rows drop to 0, the origin becomes `passes`, and the committed source answers.
- **P-B-4** boundedness: 64 rows need 4 grains, max 32 units and max 21 row reads per grain.
- **P-B-5** pressure: a refused staging increment returns retryable `Pressure` with 0 rows consumed and no transform; releasing pressure resumes the same cursor.

## Verification receipts

- `pio test -e native -f test_immediate_capture_handoff`: **14/14 PASS** (`pb-parent-focused.log`).
- `IMMEDIATE_HANDOFF_CASE=source`: **exit 0**, 6/6 (`pb-parent-source.log`); `=stop` and `=empty` still exit 0.
- `pio test -e native -f test_overdub_source_view`: **113/113 PASS**.
- Full native: **3,465 total / 7 skipped / 3,458 passed / 0 failed**.
- Shipping build: **SUCCESS** — RAM1 variables 96,192 / code 392,280 + padding 936 / locals 34,880, i.e. the same approved total.
- Cursor cost: +776 bytes per `Loop` shell, allocated through `LoopPool` in external memory (≈49.7 KB at eight allocated tracks × eight slots), not RAM1.

## Contract deviation requiring an owner ruling — C3.3

C3.3 named the derived-view branch of `Track::processDeferredIdleMaintenance` as the **one** wakeup. That
branch is unreachable in exactly the state C3 exists for: maintenance returns from the stop lane whenever
`hasPendingDeferredOverdubStop()` is true, which is the whole time A is private. Implementation therefore
keeps the contracted site **and** adds one bounded grain after `processDeferredOverdubStopGrain()` on the
same lane, gated to the `VisualCache` owner and `isOverdubbing()` — same single producer, same 32-unit grain,
no priority grant, no second scheduler. Without it P-B-1 cannot be reached at all.

This needs one of two rulings before the packet is closed: **accept the second site as C3.3's single
producer** (amending the clause to name the reachable branch), or require a different reachable wakeup. The
slice is otherwise green; nothing in it removes a guard or relabels private content.

## Not closed by this slice

- P-D bounded visual continuation remains open (task2.2.12); `IMMEDIATE_HANDOFF_CASE=start` is P-A's
  manager-route RED and is untouched.
- Device evidence NOT RUN: input-service gap, command→ingress latency, PSRAM head-room for the +776-byte
  Loop shell, retirement stack high-water, allocator fragmentation. Native ordering and a successful link
  are not hardware qualification.

## Focused independent review and its repairs

The first review returned **REQUEST CHANGES** with six findings; all are fixed and retested.

1. **HIGH — the seal could lose B's decision.** `privatePredecessorSourcePending()` read only the cursor
   phase, while `discardPrivateSourceScan()` resets the phase but keeps the retained intents; publication
   is exactly that edge. So after A published mid-session, the seal saw nothing pending, recorded no
   denial, and sealed B without A's Shorten/Hide — the original defect, silently. Repair: a seal-owed
   predicate that includes retained intents, supersession that keeps the intents (instead of discarding
   them), and `materializeDeferredPrivateHoldsAtDeadline()` at the last moment the Loop owns the
   interval's pending set.
2. **MED-HIGH — holds materialised before the binding check**, against last turn's view. Repair:
   qualification runs first for both phases, the session view is refreshed once per materialisation
   episode, and the interval's own accepted note is never transformed.
3. **MED — a "pair decision" unit was O(prefix) and view growth was uncharged.** Repair: the open-pair
   table stores the staged index, the completing grain walks only that fixed table, and every append is
   admitted through the charged increment so refusal is a named `Pressure` with the row unconsumed.
4. **MED — the supersession stamp covered only notifying revisions.** Repair: any identity *or*
   revision move discards the scan, and `publishPreparedCompanionHistory` discards at its own revision
   bump.
5. **MED-LOW — the hold-capacity denial was unreadable** because each grain reset it. Repair: sticky
   named denial surfaced at the seal.
6. **LOW — a Loop-identity mismatch left the cursor pending forever.** Repair: qualification discards
   the bound cursor before any early return.

Four regression cases were added (`pair decisions and view growth are bounded`, `a revision move without
a notify supersedes the scan`, `five holds are bounded and denied by name`, `a loop identity mismatch
cancels the scan`) and P-B-3 was extended to stop B after publication and assert the pass-set outcome.

## Final verification after the repairs

- Focused handoff suite: **18/18 PASS** (`pb2-focused.log`).
- `IMMEDIATE_HANDOFF_CASE=source` **exit 0**, 10/10 — `origin=private_predecessor, participants=3,
  decisions=2, staged=64, notes=32` (`pb2-source.log`); `=stop` exit 0 (4/4), `=empty` exit 0 (3/3).
- Full native: **3,469 total / 7 skipped / 3,462 passed / 0 failed**, 109 s (`pb2-native.log`).
- Shipping build: **SUCCESS**, RAM1 variables 96,192 / code 392,280 + padding 936 / locals 34,880
  (`pb2-build.log`). `sizeof(Loop)` is now **1,848** (+64 for the staged index), external through
  `LoopPool`: +6,720 B per allocated Track. `CaptureStopJobs` 29,600 and `Track` 384 unchanged.

## Clause deviations — resolved by owner ruling (2026-10-25)

Both deviations were put to the owner and **accepted as amended clauses**, not waived:

- **C3.3 — bounded progress.** Preparation receives at most one bounded grain per eligible owner turn at
  the reachable maintenance wakeup while preparation is owed; no catch-up loop, no synchronous
  completion. The contracted branch was unreachable in exactly this state, so the eligible wakeup is the
  reachable maintenance turn. The implementation's two grain sites are mutually exclusive within a turn
  (the pending-stop lane returns before the derived-view branch), so one grain per turn holds.
- **C3.4 — materialisation deadline.** Retained predecessor-dependent overlap intents must be materialised
  or denied by name before the pending-change set leaves the Loop's current ownership domain: the seal
  edge for the synchronous route and the stop-admission pending-set swap for the deferred route.
  Materialising the retained intents there is bounded by the declared hold capacity; it is not a
  synchronous completion of the scan.

Conformance check against the amended text: one grain per turn (mutual exclusivity, verified above);
`materializeDeferredPrivateHoldsAtDeadline()` is bounded by `kPrivateSourceHoldCapacity` and only runs at
the two named edges; no catch-up loop, no second scheduler, no priority grant, no guard removed.

## Honest limits

- The participant query also reports this interval's own accepted note (3 participants for one
  predecessor pair). That is the pre-existing `Add` merge, not a private-origin row; the transform filter
  excludes it and P-B-1's decision assertion is per-target.
- While B stays live, a published predecessor's presentation retirement owns every deferred turn, so
  retained intents are materialised at the deadline edges rather than in the idle grain; P-B-3 proves
  the pass-set outcome and the named denial remains the fallback.
- The prepared-companion-history discard could not be driven end-to-end (it needs real companion rows);
  the generic any-revision rule is covered by fault injection and the discard is a code-level guarantee.
- Device/HITL NOT RUN; no upload, reset, SD, content, commit or push.
- No upload, reset, content stimulus, SD operation, commit or push.
