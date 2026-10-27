# Device leg — display continuity PASSES; a separate overdub-admission defect surfaces

**Date:** 2026-10-27 · **Captures:** `captures/session_20261027_104326.log` (display run), `captures/session_20261027_105518.log` (overdub attempts)
**Image:** `firmware.hex` sha256 `387725ea1ed3be2823668d7396ad6d26d79c88df665d16d7f27a18dc3ed8ffe2` (built from this tree)
**Contract:** [handoff](../../../../docs/Plans/display_source_retention_handoff.md) · implementation: [RETAINED-DISPLAY-SOURCE](2026-10-27-RETAINED-DISPLAY-SOURCE.md)

## 1. Display continuity — confirmed fixed on hardware

`DIAG,disp_turn` `st`/`why` distribution, same build shape, before vs after:

| Image | `st=fresh,why=-` | `st=ret,why=hold_gap` (the defect's signature) |
|---|---|---|
| `7c04edd8…` (owner-rejected) — `session_20261027_084714.log` | 807 | **266** |
| this image — `session_20261027_105518.log` | 521 | **1** |

The hold-with-stale-content storm is gone (266 → 1, and that one is a single legitimate hold instant), the frames keep
`n≈315–319` rows while the window moves (`req=8,12288 → 184,12288`, `key=req`), and the owner reports the display
regressions fixed. **The acceptance target of task 4.19 is met on the device**; 4.4's own checkoff stays open because the
run below was blocked by an unrelated admission failure.

## 2. The reported "overdub cannot be stopped" is an **admission denial**, not a stop path

`session_20261027_105518.log`, around the presses the owner describes (device time 676.8 s–686.2 s):

```
#MEM_DENIED,676825695,op=overdub_start,reason=heap_low,need=16384B,heap_free=28KB,pool_refused=0/0B,since_refusal=4294967295ms
[676.825] [INFO] MIDI Button A: Live Overdub
[678.812] [INFO] MIDI Button A: Live Overdub        (… repeated at 681.8, 683.1, 684.4, 686.2)
```

- Whole capture: **0** `Overdubbing started` rows, **0** `Overdubbing stopped` rows, **6** `Live Overdub` actions.
  So no overdub ever began; the button had nothing to stop and re-attempted the denied *start* each press.
- The denial is the documented policy: `kOperationHeapFloorBytes = 32 KB` plus `OverdubStart`'s `16 KB` margin
  requires `heap_free ≥ 48 KB`; the device sat at **28 KB free** for every sampled row (`min_ever=28 KB`).

## 3. Why the heap is at 28 KB — a reclamation storm, named but not diagnosed here

```
#CAP,618551307,DIAG,reclaim,386,386,0,0,922,Critical,0,67,778,23
#CAP,639678127,DIAG,reclaim,381,381,0,0,916,Critical,1,69,778,23
```

Field order (`DebugSessionCapture::passReclaim`): `chunksFreeBefore, chunksFreeAfter, passesReclaimed, chunksReleased,
durationUs, pressure, transport, capturePins, editPins, slotsWithPins`.

- **~13,900 rows in ~5 s** of device time (≈2,800 rows/s), each `passesReclaimed=0`, `chunksReleased=0`, duration
  **≈900 µs**, `pressure=Critical`, `editPins=778`, `slotsWithPins=23`. The lane paid ~900 µs per turn to reclaim
  nothing, and the pressure never cleared.
- The internal heap was already at its floor when the capture began (it starts at device time 618 s, uptime ≠ session
  start), so this capture cannot say *what* filled it — only that it was full, and that the reclaimer spun.

## 4. Attribution — this is not the retention path

- Retention storage is PSRAM-first by type: `NoteUtils::DisplayNoteVec` is `std::vector<DisplayNote,
  ExternalMemoryFirstAllocator<DisplayNote>>`, and the envelope's pass/chunk/edit vectors use the same allocator. The
  envelope holds one generation (≈5 KB for ~320 notes) and is replaced per promotion.
- The only internal-heap touch the retention code adds is the promotion's `admitDisplayProjectionReserve(16·capacity)`
  **probe** (`malloc`+`free`, nothing retained) — and on a tight heap that probe *fails into the refusal path*
  (`retained_admission`), it does not consume.
- Nothing in the change touches reclamation, undo pins or `MemoryMonitor` policy. `editPins=778` is an undo/publication
  root count, not a display term.
- Provenance note: the image on the board was written by `pio run -t upload` from the census's own reset path (the
  repo's established "reset by re-uploading" helper, used by both `hitl_boot_census.py` and
  `hitl_library_growth_sweep.py`) at ~10:45 — the same `firmware.hex`, same sha256. A CLI-2.3 re-flash for a clean
  programming receipt was attempted twice and aborted before programming; the board has been running and healthy since.

## 5. Also recorded: my tooling interrupted one run

`captures/session_20261027_104326.log` contains `#CAPTURE_RECONNECT,2026-10-27T10:45:03` followed by
`DIAG,boot_reset,crash=0` — a **reset, not a crash**: no `CrashReport`, no fault marker, no `MEM_DENIED`/`FATAL`, and no
`!REBOOT` was sent. It coincides exactly with the census run I started at 10:44, whose reset path re-uploads. Recorded
so the run is not misread as a firmware fault; the rule going forward is that no reset-implying tool runs while a HITL
session is (or may be) driving the device.

## 6. Next steps this record sets up (needs the owner's device time)

1. **Capture from boot** through several short overdubs, so the heap's decline is in the log (this capture begins at
   618 s uptime and the floor is already reached).
2. Name the holder of `editPins=778` (undo/publication roots) — the tree's S1.4 pin census vocabulary applies.
3. Only then decide the reclamation-storm fix; it must not be a policy re-base of the 32 KB floor or the 16 KB margin.

## 7. `editPins` ownership trace (code, 2026-10-27)

`editPins` is not a leak counter. `TrackManager::reclaimUnreferencedDisabledPasses` builds
`PassReferenceSet refs` from **two** sources per track and sums `slots[slot].editPassIds.size()` over all slots:

1. **The global undo stack** — `collectReferencedPasses(stack, refs)` (`src/PassReclaim.cpp`): *every* entry pins
   its `editPassIds`, and an `OverdubPassAdded` entry also pins its `passIds`, for as long as the entry lives.
2. **The windowed lane's retained playback sources** — `track.mergeRetainedPlaybackPinsInto(slot, …)`
   (`TrackManagerMemoryPressure.cpp`), merged deliberately because "a pin registered only alongside undo
   references is not sufficient protection".

With `slotsWithPins=23` of 64 slots and 778 edit ids (~34/slot) the *dominant* class is **not established** by the
count alone — and it need not be garbage: the reclaimer only removes edit/capture passes that are **Disabled *and*
unreferenced** (`Loop::reclaimUnreferencedDisabledEditPasses`), so pins on **Active** rows are legitimately
harmless to reclamation. My earlier reading "778 pins ⇒ leak" was too strong; corrected here.

Bounds that do exist: `ABSOLUTE_MAX_UNDO_ENTRIES = 512`, `PREFERRED_UNDO_DEPTH = 99`, `MIN_UNDO_DEPTH = 8`, trimming
at push (`pushUndoEntry → trimUndoStackForMemory → trimGlobalUndoStackForMemory`) plus a deferred
`GlobalUndoStackTrimCursor` stepped inside `TrackUndoPublication`.

Heap-relevant types found while tracing:

- `UndoEntryVec` is **`ExternalMemoryFirstAllocator`** — the entries themselves are PSRAM-first (good).
- `UndoEntry::editPassIds` / `passIds` are `EditPassIdList` / `PassIdList` = **`InternalHeapFirstAllocator`**, so up
  to 512 entries' payload vectors live on the internal heap.
- **`Loop::overdubSessionPassIds_` (`std::vector<PassId>`) and `Loop::overdubSessionCompanionIds_`
  (`std::vector<EditPassIdList>` — a vector of internal-heap vectors)** are appended once per wrap/overdub commit in
  `pushOverdubSessionPass` and cleared only on the undo-path resets. Nothing bounds them. **This is the concrete
  staircase candidate for "internal heap falls as small overdubs accumulate"** and the first thing the fixture below
  should measure.

## 8. The plateau fixture — designed, blocked at one admission, not landed

Written against `test_runtime_working_set_ownership` (that suite already links `PassReclaim.h`, owns
`admitStopForSlot`/`step` and `prewarmPlaybackForSlot`). The cycle is one **settled** overdub: `forceSetState(PLAYING)`
→ `prewarmPlaybackForSlot` → `startOverdubbing` → two events → `admitStopForSlot(…, interval++)` → step until the job
retires; after each cycle it samples `entries`, pinned capture/edit ids (through the same collector the reclaimer
consumes), `usedChunkCount()`, and the loop's pass/edit counts; it asserts a plateau (`max−min` over a warm window),
not a staircase.

**Blocker (measured, not guessed):** `Track::startOverdubbing` returns without entering the interval —
`admitOverdubStaging()` refuses in that suite's bare setup. `prewarmPlaybackForSlot(slot, true)` succeeds, but the
capture-staging role is not admitted there, so the fixture fails at `isOverdubbing()`. The fixture was **removed again
rather than left red** (suite green at 246/246).

Next, in order: (1) move the fixture into a capture suite that already admits staging (e.g.
`test_shared_capture_finalization`) or copy that suite's record-arm prelude; (2) measure the samples, then pin the
measured plateau as the assertion; (3) fix the first owner that does not retire — `overdubSessionCompanionIds_` is the
first suspect to check against the samples; (4) do **not** re-base the 32 KB floor, the 16 KB `OverdubStart` margin,
or `ABSOLUTE_MAX_UNDO_ENTRIES`.

## 9. Instrument landed; fixture round 2 (shared-capture suite) and its blocker

**Landed and measured:** `Loop::overdubSessionRetention()` (`include/Loop.h`, `src/Loop/LoopOverdubSession.cpp`) —
`passCount`/`passCapacity`, `companionCount`/`companionCapacity`, `companionNestedCapacity` (the nested
`EditPassIdList` capacities a `clear()` keeps) and the cursor. It is the 4.20 instrument, cold-path, and the capture
build stays at the approved RAM1 line (`variables 96,192` / `padding 3,224` / locals `34,880`, clean rebuild).

**Fixture round 2, as directed, in `test/test_shared_capture_finalization`:** reuses that suite's own
`prepareSlot(t, 0, /*overdub=*/true)` / `start` / `stop` / `driveStopToRetirement` cycle, 64 cycles, sampling the
accessor after each settled stop and asserting no per-cycle growth in size **or** capacity (including nested).

**Blocker, measured:** with `t.forceSetState(TRACK_PLAYING)` added (as the suite's `rotation()` helper does) and the
cycle ticks kept inside the prepared 768-tick loop, the cycle still stops at `start()`'s
`TEST_ASSERT_TRUE(overdub ? t.isOverdubbing() : t.isRecording())` (`test_shared_capture_finalization.cpp:629`). That
helper's assert carries no cycle context, so **which** cycle refuses is not known from this run. Both fixtures were
withdrawn again rather than left red: RWO 246/246, shared-capture 104/104, full native 3509/7 skip/3502 pass/0 fail.

**Next step (small, then decisive):** give the cycle its own pre-conditions and messages — `cycle=`, `stopPending=`,
`stopInFlight=`, `jobs=`, `noLiveOwnerForSlot`, `loop.capture.phase` — immediately before `start`, so the refusing
cycle and the blocking owner are named; then sample `overdubSessionRetention()` per cycle and pin the measured
plateau. Only after that is the session-retirement fix (or the undo-payload measurement) in scope.

## 10. Cycle-qualified probe: the session-vector hypothesis is refuted, and the first blocker is named

Fixture (landed, green): `test_4_20_overdub_session_retention_plateaus_until_the_transfer_pool_refuses` in
`test/test_shared_capture_finalization` — the suite's own `prepareSlot`/`stop`/`driveStopToRetirement` cycle, one
cycle-qualified precondition assert per cycle, sampling `Loop::overdubSessionRetention()` after every settled stop.
Receipt (visible under `pio test -v`):

```
T4.20 retention receipt: settled=16 blockedAt=16 | warm(cycle8): pass=0/0 comp=0/0 nested=0 cur=0
  || last(cycle16): pass=0/0 comp=0/0 nested=0 cur=0 (size/capacity)
  | cycle=16 tick=480 state=5 od=0 rec=0 pendStop=0 inflight=0 slotInflight=0 jobs=0 liveFree=1 phase=2
    passes=17 admission=0 views=1 stagings=1 pendPass=0 poolRows=0
```

**Result 1 — the session ID lists are not the staircase.** After every settled overdub they are `pass=0/0`,
`comp=0/0`, `nested=0`: size *and* capacity empty, so no nested internal-heap allocation is retained either. The
`overdubSessionPassIds_`/`overdubSessionCompanionIds_` hypothesis is **refuted** for the settled shape; the plateau
assertions pass with zero tolerance needed.

**Result 2 — the first blocker, named by the probe.** From cycle 16 the start is refused with a completely clean slot
(`pendStop=0 inflight=0 jobs=0 liveFree=1 pendPass=0`), views and stagings still admitted, and exactly one failing
gate: `hasActiveOverdubTransferAdmission() = 0` because **`prewarmedCommittedChunkIdPool_` is empty (`poolRows=0`)**.
`Track::startOverdubbing` re-admits that pool through `Loop::prewarmActiveOverdubTransferHorizon()`, whose first
statement is `if (horizon == 0 || !prewarmSessionExitSnapshotHandoff()) return false;` — so the refill never runs and
the refusal surfaces as "cannot start an overdub", which is the same user-visible shape as the device
(`op=overdub_start` denied). At that cycle the loop holds **17 committed passes** (one per settled overdub plus the
base record pass).

**Next candidate (named, not yet diagnosed):** why `prewarmSessionExitSnapshotHandoff()` stops succeeding at 17
committed passes, and whether a settled overdub should be able to accumulate passes without bound at all. Both are
measured-worthy before any fix: the first is a prewarm capacity/bound question, the second is the pass-count product
budget that the device's 25-bar take also multiplies.

## 11. The 16→17 boundary characterized: a fixed receipt ring, **not** pass-proportional

**Answer to the decisive question — no, start admission does not scale with historical pass cardinality.** The
requirement `prewarmSessionExitSnapshotHandoff()` computes is a **fixed ring of
`kSessionExitReceiptCapacity = 8`** `SessionExitSnapshotHandoff` receipts (`LoopSnapshotPublish.cpp`): it allocates
the ring once (external-first) and returns `canHandoffSessionExitSnapshot()` =
`sessionExitReceipts_ != nullptr && sessionExitReceiptCount_ < kSessionExitReceiptCapacity`. Nothing in it is
`O(committed passes)`.

**How the ring is drained:** only by `stepPendingSnapshotPublish(…, allowStartConstruction=true, …)` — one receipt per
publisher turn, FIFO, skipping not-ready receipts in place. So in a settled-overdub shape the receipts accumulate
until the ring is full.

**Why the refusal needs two conditions, measured:** a full ring **alone does not block a start** —
`test_4_20_a_full_receipt_ring_alone_does_not_block_an_admitted_overdub_start` holds the ring at `8/8` with 9
committed passes and the start still succeeds (`admission=1`), because
`admitOverdubStartWorkingSet()` only re-prewarms the transfer horizon when
`hasActiveOverdubTransferAdmission()` has **lapsed**. The refusal therefore needs *lapse + full ring*: at cycle 16 the
probe reads `admission=0`, `poolRows=0`, `receipts` at capacity, and the re-prewarm is gated on a free receipt slot,
so the start can never re-admit. That is a **coupling of overdub-start admission to snapshot-publisher cadence**, not
a pass-count scaling defect.

**Fixture state (green, 106/106 in the suite):**

| case | what it pins |
|---|---|
| `test_4_20_overdub_session_retention_plateaus_until_the_transfer_pool_refuses` | session ID lists are empty (size *and* capacity) after every settled cycle — the session-vector line stays **disproven**; reports the pool bound at cycle 16 instead of asserting it away |
| `test_4_20_a_full_receipt_ring_alone_does_not_block_an_admitted_overdub_start` | a full 8/8 ring with an already-admitted horizon still starts |

**Deliberately not left red:** the discriminating RED belongs on the *lapse* state (`admission=0` + full ring ⇒ start
must still be admissible, since the loop's storage limits are nowhere near exhausted). It is a three-line change in
the measurement fixture (`assert blockedAt < 0`), and it is withheld only until its repair slice starts — the owner's
sequence is characterize → RED → repair, and the repair is not in this slice.

**Repair direction (not started, no pool enlargement):** decouple start admission from the publisher's receipt ring —
the transfer horizon needs its own bounded reserve, while the receipt ring stays a publisher-side buffer whose
overflow is reported where it happens (the publisher), not converted into "cannot start an overdub". This follows the
owner's rule that start admission should depend on the bounded work of the **new interaction**.

## 12. The lapse-boundary RED is landed (fails today, as ordered) + the repair plan read from the code

**Landed RED:** `test_4_20_lapse_boundary_red_full_ring_does_not_deny_start_and_obligation_survives`
(`test/test_shared_capture_finalization`). It asserts the whole chain the owner required, not just a successful
start: no cycle may be refused by publisher receipt occupancy → the stopped interval owns exactly one publication
obligation → when capacity returns it publishes and **no** receipt is left pending. Today it stops at the first
assert with the measured state:

```
REFUSED at cycle=16: receipts=8/8 admission=0 passes=17 phase=2 pendPass=0 jobs=0 liveFree=1
```

So the coupling is pinned: `TransferAdmissionLapsed + ReceiptRingFull ⇒ OverdubStart denied`.

**Repair plan, read from the code (not started):**

1. **Split the prewarm** — `prewarmSessionExitSnapshotHandoff()` currently does two things: allocate the receipt ring
   and require a *free slot* (`canHandoffSessionExitSnapshot()`). Start admission only needs the first.
   `prewarmActiveOverdubTransferHorizon()` should call an `ensureSessionExitReceiptRing()`-shaped step, leaving the
   free-slot test where a receipt is actually **written** (`captureSessionExitTail` / `handoffSessionExitSnapshot`).
2. **Never drop the obligation when the ring is full.** `captureSessionExitTail` returns
   `kInvalidSessionExitReceipt` *before* its swap, so today the session identity lists stay in
   `overdubSessionPassIds_`/`overdubSessionCompanionIds_` and the next session would clobber them. The owner's
   "one retained/coalesced obligation outside the ring" already exists: `pendingSnapshotPublishBox_`
   (`queueSessionExitSnapshot()` sets `box.sessionExitQueued` and owns `queuedPassIds`/`companionIds`). At a full
   ring the ids must be **moved into that box** rather than left behind, so authorship — not ring occupancy — owns
   the obligation.
3. **Publishing stays the ring's job** (one receipt per `stepPendingSnapshotPublish` turn, FIFO, skipping
   not-ready receipts in place). Ring overflow is reported diagnostically; it never converts into a lost obligation
   and never into "cannot start an overdub".

**Two facts the repair must keep visible:** the 8/8 control still passes (a full ring with an established admission
is legal), and the session-vector line stays disproven (`pass=0/0 comp=0/0 nested=0` after every settled cycle) —
neither the ring nor those vectors is a pass-proportional requirement.

**After the repair (owner's steps 5–6):** free one receipt and prove single publication, then restore the 64/128-cycle
fixture — which must then genuinely run all cycles with **no** "stop at cycle 16 and report the bound" escape — and
only then resume the internal-heap measurement that started this thread.

## 13. Ownership rule adopted; why the admission split alone is unsafe; the RED now specifies the GREEN

**Rule adopted (owner, 2026-10-27):** *overdub start reserves capacity for its future session-exit
**obligation**, never a slot in the publisher receipt ring.* Ring capacity must not gate the start;
pending-obligation capacity may — and an occupied pending box is then a **named** bounded refusal, not a silent queue.

**Why steps 1–3 were not shipped as a partial edit.** `captureSessionExitTail()` returns
`kInvalidSessionExitReceipt` **before** its `swap`, so at a full ring the session ids stay in
`overdubSessionPassIds_`/`overdubSessionCompanionIds_`. If admission were decoupled first (start allowed at a full
ring) and the transfer were left as is, a stopped session would leave the obligation in
`pendingSnapshotPublishBox_` **while the session vectors still held the ids** — "both owners" — and the next session's
`pushOverdubSessionPass` overwrites them, which collapses to "neither owner" for the older obligation. That is exactly
the content-loss shape the owner flagged, so the atomic transfer and the admission split must land **together**, and
the transfer needs a mutation proof. Not shippable in this slice's remaining budget; the RED stands instead.

**The RED now specifies the GREEN** (`test_4_20_lapse_boundary_red_full_ring_does_not_deny_start_and_obligation_survives`).
Beyond "the start succeeds" it asserts, in order:

| assertion | encodes |
|---|---|
| no cycle refused by receipt occupancy (`blockedAt == -1`) | ring capacity does not gate the start |
| `lastReceiptsAtStart > 0` and `admissionRestoredAtStart` | the start happened **while the ring held receipts** and the transfer admission was restored anyway |
| session `passCount == 0` and `companionCount == 0` after the stop | authorship transferred: the live session no longer owns the ids |
| `pendingSnapshotSessionExitQueued() || hasPendingSessionExitReceipt()` | exactly one durable obligation exists (never neither) |
| stepping the publisher until no pending receipt, then `publishedSnapshotIsCurrent()` | it publishes exactly once when capacity returns, and ownership clears |

Current state: **fails at the first assertion** with the measured
`REFUSED at cycle=16: receipts=8/8 admission=0 passes=17 phase=2 pendPass=0 jobs=0 liveFree=1`.
Suite `test_shared_capture_finalization`: 107/108 (the single failure is this RED); full native carries the same one
failure (3513 cases / 1 failed / 7 skipped / 3504 succeeded as last measured).

**Repair sequence for the next slice (unchanged ordering, now with the ownership rule):**

1. `ensureSessionExitReceiptRing()` split out of `prewarmSessionExitSnapshotHandoff()`; the horizon prewarm requires
   *ring exists* + *pending-obligation slot available*, and never `canHandoffSessionExitSnapshot()`.
2. At a full ring, `captureSessionExitTail()` moves the ids into the reserved `pendingSnapshotPublishBox_`
   (`queuedPassIds`/`companionIds`, `sessionExitQueued`) **atomically** — one owner before, one owner after, and the
   swap leaves the session vectors empty. Mutation-prove it by swapping-first (must fail the "never both" assertion)
   and by moving-after-freeing (must fail "never neither").
3. Prove single publication, then restore the 64/128-cycle fixture with the cycle-16 escape removed, and only then
   return to the internal-heap question.

## 14. Reservation (not availability), exclusive ownership, exactly-once — the RED now says all three

**Adopted from the owner (2026-10-27):** "pending slot available at start" is not strong enough; the slot must be
**reserved by that session at admission**, or the sequence `check-free → overdub runs → another path occupies the box
→ stop` has no durable destination. The obligation therefore has an explicit ownership state machine, separate from
the receipt ring:

```
Empty
  → Reserved(session identity)        // at OverdubStart admission (atomic claim)
  → Populated(session identity, ids)  // atomic transfer at stop, regardless of ring fullness
  → Publishing
  → Empty
```

**One owner API, no visible intermediate state:**
`transferSessionExitToReservedPublishOwner(sessionIdentity)` — preconditions: destination already reserved *for this
session*. Internally it may `std::move` because capacity/ownership was guaranteed at admission. Observable contract:
*before* the live session owns the ids and the reserved box owns no payload; *after* the live session owns zero ids and
the box owns the complete payload. Occupied/reserved-by-another-session at admission is a **named bounded refusal**,
never a silent second slot.

**RED strengthened accordingly** (`test_4_20_lapse_boundary_red_full_ring_does_not_deny_start_and_obligation_survives`):

| assertion | replaces |
|---|---|
| `boxOwns != receiptOwns` (XOR, with both flags in the message) | the old `||`, which permitted **both** |
| session `passCount == 0` and `companionCount == 0` after the stop | "the obligation exists" only |
| `publishedSnapshotRevision()` pinned before the release, unchanged after 8 further publisher steps | `publishedSnapshotIsCurrent()`, which does not prove exactly-once |
| `!pendingSnapshotSessionExitQueued()` **and** `!hasPendingSessionExitReceipt()` afterwards | ownership-not-cleared |

Current state (unchanged, honest): fails at the first assertion with
`REFUSED at cycle=16: receipts=8/8 admission=0 passes=17 phase=2 pendPass=0 jobs=0 liveFree=1`;
suite 107/108; the reservation assertions cannot be written yet because the reservation API does not exist — they are
the first thing the slice below adds.

**Approved slice, to land as ONE unit (not partially):**

1. Split **ring existence** (`ensureSessionExitReceiptRing()`) from **receipt availability**
   (`canHandoffSessionExitSnapshot()`), so the horizon prewarm never depends on a free receipt.
2. At overdub admission, **atomically claim** the single pending-publication slot for that session (state
   `Empty → Reserved(session)`); occupied/reserved by another session ⇒ named bounded refusal.
3. Stop transfers the ids into that already-reserved owner via
   `transferSessionExitToReservedPublishOwner(sessionIdentity)` — independent of ring fullness, source cleared in the
   same operation.
4. The publisher later moves that obligation through the receipt path **exactly once**, then `Publishing → Empty`.
5. Mutation proofs, separately: (a) remove the reservation claim → admission must still refuse *by name* and the
   transfer must refuse rather than orphan; (b) remove the source clearing → the "never both" assertion must fail.
6. Then the unrestricted 64/128-cycle fixture — cycle-16 escape removed — and only then the internal-heap question.

**Why this was not started in this session:** steps 1–4 are one atomic change in the live stop/publication path, and
the owner's own rule forbids landing them partially. Starting it without room to run the two mutation proofs and the
cycle test would risk exactly the "started but cannot safely finish" outcome the whole thread exists to prevent.

## 15. Implementation packet for the one-unit slice (anchors verified; **not applied**)

Stated plainly: this session did not apply the slice. It is one atomic change across `Loop`, `Track` and the
box/publisher internals, and the owner's rule forbids landing it partially — the remaining session budget could not run
the two mutation proofs plus the unrestricted cycle test afterwards. Everything below is read from the code in this
session, so the next pass executes top to bottom without re-deriving.

**Anchors (all verified now):**

| what | where |
|---|---|
| receipt ring capacity (8) / fields / count accessor | `include/Loop.h:1168`, `:1810-1812`, `:1169` |
| `prewarmSessionExitSnapshotHandoff()` (allocate **and** require a free slot) | `src/Loop/LoopSnapshotPublish.cpp:1972` |
| `canHandoffSessionExitSnapshot()` | `…:1987` |
| **the coupling**: `prewarmActiveOverdubTransferHorizon()` first statement | `src/Loop/LoopCapture.cpp:2476-2479` |
| `captureSessionExitTail()` — returns invalid **before** its `swap` | `src/Loop/LoopSnapshotPublish.cpp:2012-2035` |
| `handoffSessionExitSnapshot()` (needs `canHandoff…`) | `…:2043` |
| receipt consumer, one per publisher turn, FIFO | `…:2178-2200` (`stepPendingSnapshotPublish`) |
| `PendingSnapshotPublishBox`, `sessionExitQueued`, `queuedPassIds`/`companionIds` | `src/Loop/LoopSnapshotBoxes.h:90`, `:119` |
| `Track::admitOverdubStartWorkingSet()` | `src/Track/TrackOverdubLifecycle.cpp:441` |
| `Track::startOverdubbing()` + the three post-admission failure exits | `…:3355`; admission at `:3417`; exits at `:3419` (viewSet/captureStopJobs), `:3437` (`MemoryLimit` admission), `:3461` (`beginCaptureInterval`) |

**Edit order:**

1. `Loop`: `ensureSessionExitReceiptRing()` extracted from `prewarmSessionExitSnapshotHandoff()` (allocation only);
   the existing function keeps its free-slot semantics for callers that genuinely write a receipt.
2. `Loop::prewarmActiveOverdubTransferHorizon()` → `if (horizon == 0 || !ensureSessionExitReceiptRing()) return false;`
   — no `canHandoffSessionExitSnapshot()` dependency. **This alone is not shippable** (see §13): it must land with 3–6.
3. `Loop`: reservation state `Empty|Reserved|Populated|Publishing` + `uint64_t sessionExitPublishClaim_` and
   `claimSessionExitPublishOwner()` (refuses by name when claimed), `releaseSessionExitPublishOwner(id)` (identity-
   qualified), plus a const accessor for the fixture.
4. `Track::startOverdubbing()`: claim after every admission guard and **before** `beginCaptureInterval`
   (single claim point, smallest possible window); `beginCaptureInterval` failure releases it — that is the one
   rollback anchor inside the function. The two earlier exits (`:3419`, `:3437`) precede the claim by construction.
5. `Loop::transferSessionExitToReservedPublishOwner(id)`: precondition `state == Reserved && id == claim`; `std::move`
   `overdubSessionPassIds_`/`OverdubSessionCompanionIds_` into `pendingSnapshotPublishBox_`
   (`queuedPassIds`/`companionIds`), set `sessionExitQueued`, leave both session vectors empty, `Reserved → Populated`.
   Called from `captureSessionExitTail()` **only** when the ring has no free slot (the free-slot path keeps today's
   swap-into-receipt behaviour and then releases the claim).
6. Publisher: when `stepPendingSnapshotPublish()` consumes the box's session-exit obligation, `Populated → Empty`
   (identity-qualified; a second turn must not advance it again).
7. Mutation proofs, separately: (a) remove the claim in step 4 → the RED must still fail, now at the **transfer**
   ("no durable destination"), not silently succeed; (b) remove the source clearing in step 5 → the "never both"
   assertion must fail.
8. RED assertions to enable once the API exists: `sessionExitPublishClaimId()` at admission, and `Empty` after an
   aborted start.
9. Then the unrestricted 64/128-cycle fixture (cycle-16 escape deleted) and finally the internal-heap measurement —
   keeping 4.20's two findings distinct, since this repair may remove an admission-scalability defect without
   explaining the device's heap decline.

**Acceptance bar (owner's list, unmodified):** RED green with the ring still full at start; reservation owned before
the interval is live; stop succeeds while the ring stays full; session vectors empty; exactly one of box/receipt owns
the obligation at every stable point; freeing capacity publishes exactly once; repeated publisher steps do not
republish; claim/box/receipt all clear afterwards; aborted start releases its reservation; both mutation proofs fail
as specified; unrestricted 64/128-cycle run passes.

## 16. §15 applied — the reserved obligation, its two mutation proofs, and the fixtures that had to change (2026-10-27)

**Outcome: applied as one unit; the lapse-boundary RED is GREEN and the ring is irrelevant to admission.** Every
step 1–9 of §15 landed in one pass, with three deliberate departures from the packet's wording, each forced by
something the code or the fixture measured. Nothing was uploaded, flashed, or committed.

| §15 step | Landed as | Where |
|---|---|---|
| 1 | `ensureSessionExitReceiptRing()` — allocation only; `prewarmSessionExitSnapshotHandoff()` keeps ring *and* free-slot semantics for callers that write a receipt | `src/Loop/LoopSnapshotPublish.cpp` |
| 2 | horizon prewarm = `horizon != 0 && ensureSessionExitReceiptRing()`; no `canHandoffSessionExitSnapshot()` dependency | `src/Loop/LoopCapture.cpp` |
| 3 | `SessionExitPublishClaim{Empty,Reserved,Populated,Publishing}` + one state byte in shell padding; the payload is a **record beside the ring** (`kReservedSessionExitReceipt = capacity`, allocation `kSessionExitReceiptSlots = capacity + 1`), not the box | `include/Loop.h`, `src/Loop/LoopSnapshotBoxes.h` |
| 4 | `Track::claimOverdubSessionExitObligation()` after every admission guard, before `beginCaptureInterval`; the one rollback anchor releases it on failure; `openOverdubSession` stamps the claim identity | `src/Track/TrackOverdubLifecycle.cpp`, `src/Loop/LoopOverdubSession.cpp` |
| 5 | `captureSessionExitTail` → free slot: receipt as before (and the reservation is released); full ring: `transferSessionExitToReservedPublishOwner` swaps the ids into the reserved record | `src/Loop/LoopSnapshotPublish.cpp` |
| 6 | publisher: `Publishing` moves the record into the receipt FIFO exactly once, then the existing one-receipt-per-turn consumer publishes it | `src/Loop/LoopSnapshotPublish.cpp` |
| 7 | both mutation proofs red, see below | — |
| 8 | RED assertions enabled: claim identity at admission, `Empty` after an aborted start, ownership at the full-ring stop, claim/box/receipt all clear afterwards | `test/test_shared_capture_finalization/` |
| 9 | the cycle-16 escape is deleted and the 64-cycle plateau run passes unrestricted | same |

**Departure 1 — the payload lives beside the ring, not in the pending box.** §15 step 5 said "`std::move` into
`pendingSnapshotPublishBox_`". The box cannot hold it: the session's companion identities are nested
(`std::vector<EditPassIdList>` + `companionRowCount`) while the box's `companionIds` are flattened, so the move
would be a copy — and the stop boundary must not allocate. Worse, creating the box at admission would make
`beginCloseOverdubSession`'s guard (`pendingSnapshotPublishBox_ && !pendingSnapshotSessionExitQueued()`) deny a
*predecessor's* deferred cleanup for the whole life of a successor's interval. The reserved record keeps the
receipt's own shape, so the transfer stays a swap, and it is allocated with the ring at the cold admission —
never at the stop.

**Departure 2 — `Publishing` is a real state, not a transient.** Step 3 named the state; step 6 collapsed it into
the consumption turn. It is observable here: `Populated` is "transferred, final pass not yet committed" and
`Publishing` is "ready, waiting for a free receipt slot". That distinction is what lets a full ring fall through
to the receipt consumer instead of wedging both queues (the first implementation returned early on a full ring
and deadlocked the drain — measured, then fixed).

**Departure 3 — the fixtures drive the publication lane.** §15 step 9 asked for an unrestricted 64/128-cycle run
with the cycle-16 escape removed. A single bounded obligation cannot survive 64 stops with a lane that is never
given a turn: the ring is a dead end in this shape (`hasSnapshotPublicationBacklogOnAnySlot()` is false once the
root is current, so `driveStopToRetirement` never steps the publisher), so *any* bounded destination saturates.
The run therefore frees capacity explicitly — which is exactly the acceptance bar's "freeing capacity publishes
exactly once" clause — and it is driven **only after** a start has proved it is admitted at a full ring, so the
discriminating observation cannot be drained away. The measured refusal on the old tree was cycle 16
(`receipts=8/8 admission=0`); with the prewarm decoupled but no reservation, the refusal merely moves to cycle 9
(`admission=1`), which is the packet's own warning made concrete.

**Mutation proofs (each restored byte-identically, `sha256sum -c` OK):**

| mutation | result |
|---|---|
| (a) the admission claim removed (`claimOverdubSessionExitObligation` returns true without claiming) | RED fails: `receipts=1/8 claim=0 queued=1 pendingReceipt=1` — the obligation is not owned by the record, and the legacy one-shot handoff silently picks the ids up later instead of the transfer having a durable destination |
| (b) the transfer's source clearing removed (copy instead of swap) | the new direct-ownership fixture fails at `the live session must not still own the ids (never both)` (expected 0, was 2) |

**The in-suite "authorship transferred" assertion was not sufficient for mutation (b), and that is recorded as a
finding:** by the time the lapse-boundary fixture inspects the session vectors, `closeOverdubSession` has already
cleared them, so a copy-instead-of-swap passed. The dedicated fixture
`test_4_20_reserved_transfer_keeps_exactly_one_owner_of_the_session_ids` drives the ownership edge directly
(`claim → open → push → transfer → ready`) and is the one that red-proves it.

**Verification:** `test_shared_capture_finalization` **108/108**; full native **3,513 cases / 7 skipped / 3,506
succeeded / 0 failed** (the pre-change tree carried the single RED failure); `teensy41-capture-serial` **SUCCESS**
at RAM1 `variables 96,192` / `ITCM 393,216 (code 390,008 + padding 3,208)` / locals 34,880 — 16 B of ITCM spent on
the new admission call, 2,368 B of headroom above the 840 floor, **no re-base**, and `sizeof(Loop)` still 1,848
(the claim byte fits existing padding; no new shell field). Ten compiler warnings on that build are all
pre-existing; none is in the new code.

**Open, and explicitly not claimed:** the device's 4.20 heap question is untouched — this repair removes an
admission-scalability defect (`OverdubStart` inheriting publisher cadence) and says nothing about
`#MEM_DENIED,op=overdub_start,reason=heap_low` or the 778 `editPins`; no upload, flash, serial capture, SD action,
or commit. `handoffSessionExitSnapshot` remains reachable as the legacy one-shot fallback when a tail is denied by
both destinations, and that path is what mutation (a) exercised.

## 17. The 128-cycle measurement the owner asked for: **the undo stack is a plateau, and the device floor is not reproducible on the host** (2026-10-27)

Owner's milestone: *commit §15 if separable → run the unrestricted 64/128-cycle heap plateau measurement → identify which
internal-heap owner actually staircases → only then change storage/allocation policy.* The 32 KB floor and the 16 KB
start reserve were left untouched throughout. This section is the middle step.

`test_4_20_settled_overdub_heap_plateau_measurement_128_cycles` runs 128 start → note → stop → retire → publish-drain
cycles on a pre-admitted slot and samples, after **every** settled cycle, the owners the owner named: undo entries
(count and capacity), the nested `UndoEntry::editPassIds` / `passIds` size *and* capacity, committed capture/edit
passes, the chunk pool, disabled/reclaimed passes, the session-exit claim (control), and the host process footprint.
It asserts only the control (claim `Empty` at each settled point) and that all 128 cycles settle — everything else is
reported, because capacity is derived from measurement, not asserted.

**Stable, allocator-independent result (identical in every run):**

| owner | cycle 1 → 128 | reading |
|---|---|---|
| undo entries | 1 → **8**, capacity 32 → **32** | **plateau**: reaches 8 and stops; the stack is bounded, not per-overdub |
| `UndoEntry::editPassIds` | size 0 → **0**, capacity 64 → **512** (+448 once) | pre-reserved once; **zero** live ids |
| `UndoEntry::passIds` | 0 → **0** size, 0 → **0** capacity | never used in this shape |
| capture passes | 2 → 129 (+1/cycle) | retained **content**, by design |
| `overdubPasses` capacity | 16 → 128 | the transfer horizon reserves against the pass count and never shrinks (external-first on the device) |
| edit passes | 0 → 104 (+1/cycle) | retained content |
| chunk pool free | 492 → 365 (−1/cycle) | one chunk per retained pass |
| disabled / reclaimed passes | 0 → **0** | nothing is ever disabled in this shape, so nothing is reclaimable |
| session-exit claim | `Empty` at every settled point | the 4.20 control holds |

**Whole-heap footprint is arena-inclusive and therefore not a per-owner oracle.** Across three runs of the same
binary the total ranged **+213 KB to +1.49 MB** over the same 127 added passes, and the occasional large step is
`+540,672 B … +606,272 B` in a single cycle with **`dEdits=+1, dCaptures=+1, dChunksFree=-1` and no undo movement**
(`dUndo=0, dUndoEditCap=0, dUndoPassCap=0`). Its cycle placement varies run to run (46, or 110, or absent), which is
allocator-arena behaviour, not a firmware owner: no enumerated internal-heap owner moves with it, and one edit pass
cannot cost half a megabyte. Per added pass the marginal cost is **not** growing — first half `+2,080` and second half
`+1,280 B/pass` in the clean run; the "GROWING" flag the fixture prints in two runs is a single arena step landing in
the second half, and the raw halves (`3,640` → `10,240`) show it.

**Conclusion, in the owner's own fork:** the answer is the *second* branch. Native heap does **not** climb with the
undo-entry count, and the per-entry internal-heap id vectors do not staircase — they are reserved once and stay empty.
So the device's `heap_low` at 28 KB is **not** explained by undo storage, and undo storage/allocation policy must
**not** be redesigned on this evidence. What the host cannot reproduce is the regime that matters: a Teensy internal
heap of tens of KB under fragmentation, where a 540 KB arena step cannot happen but a few hundred small internal-heap
blocks certainly can. The next measurement must therefore be device-side attribution of the 778 `editPins` and the
`DIAG,reclaim` storm (which passes get pinned, whether `passesReclaimed=0` is a pin problem or a candidacy problem),
not another storage change. The 32 KB floor and 16 KB margin stay as they are — this thread has already shown once
that an apparent memory failure was really an admission-coupling defect.

**Verification:** `test_shared_capture_finalization` **109/109**; full native **3,514 cases / 7 skipped / 3,507
succeeded / 0 failed**. Test-only change: no firmware source touched by this measurement, no upload, no device leg,
no commit.

## 18. The device-side allocation census, built to the owner's directive (2026-10-27)

Owner's directive: *leave undo alone; treat the 128-cycle run as proof that persistent pass cardinality is linear; add a
tiny device-side allocation census by owner class and correlate internal-heap loss with capture/edit pass growth; only
if those classes do not explain the decline return to retained pins/fragmentation; if they do, decide separately whether
old committed generations can be compacted after their undo/reference lifetime expires.* This section is the instrument;
the correlation is the device run, which needs the owner's flash gate.

**Scope discipline.** No undo change, no storage/allocation change, no floor or reserve change. The 128-cycle run stands
as the host baseline (§17): persistent pass cardinality is linear, so the device series only has to answer *which*
class the decline belongs to.

**What the census is.** One Tier-A row, rate-limited to **one per 2 s** (`kAllocationCensusIntervalMs` on the existing
`main.cpp` diagnostics turn, right after `MemoryMonitor::updateAdvisoryPressureLevel`) — never per turn, never per event,
so it cannot become the flood the reclamation storm already is:

```
#CAP,<us>,DIAG,alloc_census,heap=<free>,hmin=<min_ever>,chunks=<free>,cap=<capture passes>,
                            edit=<edit passes>,undo=<undo entries>,ib=<internal bytes>,
                            eb=<edit share>,ub=<undo share>,xb=<external bytes>
```

**The classes, and why the split is the whole point.** `Track::allocationCensus()` walks the active slot (the loop
being recorded) at **resident capacity**, because capacity is what the allocator took:

| class | owner | allocator on the device |
|---|---|---|
| `cap` | `LoopPasses::capturePassCount()` (record + overdub passes) | — (the vectors are external-first) |
| `edit` | `LoopPasses::editPasses` | external-first vector, **internal-first payloads** |
| `eb` | each `EditPass::addedEvents` capacity | **internal-first** (`MidiEventVec`) |
| `undo` / `ub` | `GlobalUndoStack::entries` and their nested `passIds`/`editPassIds` | entry vector external-first, **nested ids internal-first** |
| `xb` | pass vectors + every pass's `committedChunkIds` | **external-first** (`CommittedChunkIdAllocator` tries `extmem_malloc` first) |
| session share of `ib` | `Loop::overdubSessionRetention()` capacities | **internal-first**, and empty after a settled close |

Without that split the census would be actively misleading: the pass *content* that grows with every overdub is
deliberately in PSRAM, so a large census with a quiet internal heap is the *expected* healthy shape, not a
contradiction. `ib` is the number that can explain internal-heap loss; `xb` is the control that shows where the content
actually went.

**Delivery and tier.** Two rules from `readinessReceiptImmediate` — write directly when USB has room, fall back to the
ring — because the census is worth most while a transport runs, which is exactly the regime that sheds non-Tier-A head
records. `DIAG,alloc_census,` is classified in `isTierATextLine` (`DebugSessionCapture.cpp`, not the header literal
list) and pinned by both boundaries in `test_capture_ring`. Pre-flight:
`OK DIAG,alloc_census,: emitted=src/Utils/DebugSessionCapture.cpp:1408 tier-a-prefix='DIAG,alloc_census,' (18/31 bytes)`.

**Cost, measured.** `variables 96,192` **unchanged** — no DTCM snap, because the row's literals are `SC_FMT`/`PSTR`
(flash) and the walk is `TRACK_COLD_MEM`; ITCM `code 390,008 → 390,216` (**+208 B**), `padding 3,208 → 3,000`, floor 840,
headroom 2,160 B. No re-base. The 2 s rate limit is what keeps a per-sample walk (≈130 passes, 8 undo entries) off the
interval path.

**Correctness pinned natively, before any device time.**
`test_4_20_allocation_census_matches_the_owners_it_reports` asserts the three properties the interpretation depends on:
cardinality follows the owners (`cap` == `capturePassCount()`, `edit` == `editPasses.size()`, `undo` ==
`entries.size()`), each byte figure is **exactly** its owner's capacity × element size, and `ib` is exactly the sum of
its three parts — so a class can never be counted twice, and a wrong instrument cannot masquerade as a finding.

**Verification:** `test_capture_ring` **38/38**; `test_shared_capture_finalization` **110/110**; full native **3,515
cases / 7 skipped / 3,508 succeeded / 0 failed**; `teensy41-capture-serial` **SUCCESS** at the RAM1 line above.

**Device gate (not taken).** [Recipe](2026-10-27-ALLOC-CENSUS-HITL-RECIPE.md) is filled and stopped at the flash gate:
no new stimulus (the census self-reports every 2 s, so an ordinary long session reproduces the reported shape), the
managed part runs through the authoritative wrapper, and the wrapper's destructive slot clear needs an authorized
track/slot in the same session. Nothing was uploaded, flashed, or captured. **Verdict rule for the run:** normalise to
bytes per retained pass; `ib` rising while `heap`/`hmin` fall (attributable to `eb` or `ub` in the same series) means the
classes explain the decline and the next decision is *whether old committed generations may be compacted after their
undo/reference lifetime expires*; a flat `ib` with a falling `heap` means they do not, and only then is the
retained-pin/`editPins` and fragmentation line next.

## 19. Upload receipt — the census image is on the board (2026-10-27, owner-authorized)

Owner said "flash". Path per [`TEENSY_FIRMWARE_UPLOAD.md`](../../../../docs/Guides/TEENSY_FIRMWARE_UPLOAD.md): Homebrew
`teensy_loader_cli` **2.3** (`brew list --versions teensy_loader_cli` → `teensy_loader_cli 2.3`), explicitly by path,
one uploader, no `pio -t upload`, no GUI.

| step | evidence |
|---|---|
| Image | `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`, `sha256 f1603da2dc0fdd7e97131b6dc8ac7ae5c0d4c85fa0756035c62f3877e216d7e5` (rebuilt first; RAM1 `variables 96,192` / `code 390,216 + padding 3,000`, floor 840) |
| Loader | `Teensy Loader, Command Line, Version 2.3` — `Read … 1333248 bytes, 16.4% usage`; `Found HalfKay Bootloader`; `Programming` (complete, no parse error, no `error writing`); `Booting`; **exit 0** |
| USB, after | `Teensy MIDI`, `idVendor 0x16c0`, `idProduct **0x0489**`, `Serial Number 15494480`, `Version 2.80`, `Location ID 0x02100000` — the expected MIDI+Serial identity |
| Boot, separately | `#CAP,3005004,DIAG,boot_reset,crash=0`; `#CAP,BOOT,load,start`; `#CAP,BOOT,ram1,196608`; `#CAP,BOOT,load,ok`; **0** `CrashReport` rows |
| New image, proved by its own row | **30** `DIAG,alloc_census` rows inside the first 63 s of uptime — the row did not exist before this image |

**The device leg is the owner's own session, not mine:** `captures/session_20261027_134320.log`
(`scripts/capture_session.py --port /dev/cu.usbmodem154944801`, started 13:43:20 from the owner's shell). I did not
touch that process or the port after identifying it. *Disclosure:* before identifying it I opened the same port for a
25 s boot read and took ~32 lines that would otherwise have reached that log; two readers split one CDC stream, so that
log is short those lines at its head. Everything below is read from the owner's log, after the fact, tail only.

**First series (verbatim, abridged to the transitions; the full series is in the log):**

```
#CAP,9590527,DIAG,alloc_census,heap=192512,hmin=192512,chunks=512,cap=0,edit=0,undo=0,ib=0,eb=0,ub=0,xb=0
#CAP,11586374,DIAG,alloc_census,heap=159744,hmin=159744,chunks=492,cap=1,edit=0,undo=2,ib=0,eb=0,ub=0,xb=0
#CAP,15586308,DIAG,alloc_census,heap=118784,hmin=118784,chunks=456,cap=1,edit=0,undo=2,ib=0,eb=0,ub=0,xb=0
#CAP,25596086,DIAG,alloc_census,heap=118784,hmin=118784,chunks=417,cap=0,edit=0,undo=3,ib=0,eb=0,ub=0,xb=0
#CAP,27596846,DIAG,alloc_census,heap=118784,hmin=118784,chunks=399,cap=0,edit=0,undo=3,ib=0,eb=0,ub=0,xb=5632
```

**What this already says, and the three caveats that must temper it.**

1. **Fresh-boot headroom is 188 KB, not 512 KB.** `heap=192512` with `chunks=512`, `hmin` equal from the first sample.
   The reported failing session sat at `min_ever=28KB`, so the decline to explain is ≈160 KB.
2. **The first ≈72 KB of loss carries no attributed class**: boot 192,512 → 118,784 (−73,728 B) while `ib=0`,
   `eb=0`, `ub=0`, `xb=0`. On this shape (one Record pass, no overdubs) the internal-first classes the census names
   are *not* what moved. That is the first real pointer away from "edit/undo payloads" — but it is **not** yet the
   long-session shape, so it is not the verdict.
3. **`heap`/`hmin` are high-water, not live.** `getInternalHeapFreeBytes()` is `&_heap_end - __brkval`; `__brkval` only
   ever rises, so the number cannot recover when memory is freed. A step therefore proves *"the heap grew"*, never
   *"these bytes are live"* — the correlation is one-directional, and `hmin == heap` throughout this series is that
   property, not stability.
4. **The census is scoped to the active slot.** `cap=1 → cap=0` at 25.6 s is a slot/focus change, not a free — the
   class columns reset per slot. A single verdict must be read over a **contiguous** slot segment, and cross-slot
   totals are a deliberate follow-up, not something this row supports yet.

**Gap the data itself exposed (not yet fixed, image untouched):** with `cap=1` the row still shows `xb=0`, because
`Track::allocationCensus()` sums the *overdub* pass vector and each overdub pass's `committedChunkIds`, but not the
Record pass's own `committedChunkIds` (`LoopPasses::recordPass`). One line to fix; it affects only the external control
`xb`, never `ib` — so it does not distort the decision the row exists to inform. Deliberately **not** changed while
this image is the identified subject of a live session: a rebuild would put the tree ahead of the flashed hash
(`f1603da2…`) mid-measurement.

**Not claimed:** no content stimulus, no SD mutation, no verdict on the classes yet — the owner's session is still
running, and the verdict rule from §18 applies to its settled series.

## 20. The verdict on the pre-registered fork: the classes do **not** explain the decline, and retained pins are zero (2026-10-27)

Read from the owner's live session `captures/session_20261027_134320.log` on image `f1603da2…` (tail/extract only; the
session was still running at 170 s of uptime when these numbers were taken). §18 fixed the verdict rule **before** the
run, so this is the rule applied, not a story fitted afterwards.

**The series (census transitions only, 64 rows in the window):**

| t | `heap` / `hmin` | `chunks` | `cap` | `edit` | `undo` | `ib` | `eb` | `ub` | `xb` |
|---|---|---|---|---|---|---|---|---|---|
| 9.6 s | 192,512 | 512 | 0 | 0 | 0 | **0** | 0 | 0 | 0 |
| 11.6 s | 159,744 | 492 | 1 | 0 | 2 | **0** | 0 | 0 | 0 |
| 15.6 s | 118,784 | 456 | 1 | 0 | 2 | **0** | 0 | 0 | 0 |
| 27.6 s | 118,784 | 399 | 0 | 0 | 3 | **0** | 0 | 0 | 5,632 |
| 73.6 s | 81,920 | 389 | 1 | 0 | 2 | **0** | 0 | 0 | 5,632 |
| 105.6 s | 45,056 | 388 | 2 | 0 | 3 | **0** | 0 | 0 | 5,648 |
| 107.6 → 119.7 s | **45,056 flat** | 387 → 388 | 3 → 10 | 0 | 4 → 10 | **0** | 0 | 0 | 5,664 → 5,776 |
| 169.7 s | **45,056 flat** | 390 | 10 | 0 | 10 | **0** | 0 | 0 | 5,776 |

**Three findings, in the rule's own terms.**

1. **The pass-proportional internal-first classes are identically zero, and growing them is free.** Across the whole
   session `ib = eb = ub = 0` — no edit payloads (there were no note edits: `edit=0` throughout), no undo nested-id
   capacity, no live session lists. Then, after the last heap step, **eight further overdubs** (`cap` 2 → 10, `undo`
   3 → 10) cost **0 bytes of internal heap** and ~144 B of external (`xb` 5,648 → 5,776). That is the direct device
   confirmation of the host finding in §17: persistent pass cardinality is linear and lives in PSRAM, not here.
2. **Retained pins do not explain it either — they are zero.** All **221** `[Memory] ext` rows in this session report
   `pin1=0,pin24=0`. The `editPins=778` in `DIAG,reclaim` is the reclaimer's *candidate reference set* (as traced
   earlier), not retained memory, and `passesReclaimed=0` in every one of the 25 reclaim rows shows reclamation has
   nothing to reclaim — because nothing ever becomes disabled in this shape.
3. **What actually moved is 144 KB in four discrete steps, none of them pass-driven.** 192,512 → 159,744 → 118,784 →
   81,920 → 45,056 (−32,768, −40,960, −36,864, −36,864). The last two coincide with **stop/publication turns**
   (`stop_grain`/`stop_phase` sequences, `ODUB,stop,atomic_transfer`, `gus,publish_begin`, and the single
   `DIAG,pressure,Normal->Low,45056,388,0` at 105.08 s), and the third with a record-return/frozen-readiness
   completion. Nothing steps afterwards.

**Therefore the pre-registered fork resolves to its fallback branch:** the classes do **not** explain the decline, so the
decision "may old committed generations be compacted after their undo/reference lifetime expires" is **not** the next
lever, and undo storage stays untouched — as instructed. Within the fallback pair, the data already excludes *retained
pins* (zero in 221 samples), which leaves **fragmentation / high-water growth from internal reserves the census does not
enumerate** — the stop/publication roles, view sets, capture stagings, stop descriptors, transfer horizon.

**One thing this evidence cannot separate, stated so the next step is honest.** `heap` is `&_heap_end − __brkval`, i.e.
brk's high-water mark, so a −36,864 B step proves *brk was raised by that much to satisfy a request*, not that one
36,864 B allocation exists. The two remaining mechanisms — a bounded per-role reserve admitted at stop, versus many
small internal allocations raising brk through reuse failure — produce the same trace. Discriminating them needs
allocation **events and sizes**, not resident capacity: the next increment of this instrument is a counted internal
allocation census (per-class `count`/`bytes`/`largest`, `malloc`-level), and only then a policy change.

**Practical corollary, not a diagnosis:** at `heap=45,056` the start policy's 32 KB floor + 16 KB margin (48 KB) is at
its edge, so the next overdub start is the one to watch; no `#MEM_DENIED` had appeared in this session at the time of
reading. The reported 28 KB floor is 44 KB below where this session already sits — most of the reported headroom is
consumed by those four steps, before any long-run growth.

**Not claimed:** no leak, no fragmentation verdict, no storage/allocation change, no second image. The session is live and
the series is scoped to the active slot (`cap=1 → 0` at 25.6 s is a focus change, not a free).

## 21. The owner's counter-hypothesis, checked in source, and the probe it asked for (2026-10-27)

Owner: *the `heap` metric may be the allocator's high-water mark, not reusable heap — first determine whether the
admission metric represents real allocatable capacity.* Checked against the actual sources rather than accepted or
dismissed:

| claim | verdict | evidence |
|---|---|---|
| `heap` is `&_heap_end - __brkval` | **correct** | `MemoryMonitor::getInternalHeapFreeBytes()`; the function's own comment already said "conservative unused arena tail, **not** malloc free-list capacity" |
| freeing does not move it back | **correct** | Teensy core `startup.c` `_sbrk` is a pure bump: `__brkval = prev + incr`, never decreases |
| freed blocks stay reusable | **correct** | `free()` returns blocks to **newlib's own** free list below `__brkval`; later `malloc`s reuse them without calling `_sbrk` |
| therefore a low tail can falsely deny | **correct, and structural** | admission reads `heapFree < (kOperationHeapFloorBytes + requiredBytes)` with that tail; the tail is a **lower bound** on the largest allocatable block, so it can deny a servable request — it can never false-*admit* |
| `mallinfo`/`mallinfo2` available | **no** | absent from this toolchain's `arm-none-eabi/include/malloc.h`, so "largest allocatable block" cannot be read — only tried |
| the malloc heap lives in RAM2 | **wrong, and it does not matter** | `linker/imxrt1062_t41_lcr.ld`: `_heap_start` after `.bss.dma`, `_heap_end = ORIGIN(RAM) + LENGTH(RAM)` → the arena is **RAM1** (the `BOOT,ram1,196608` = 192 KB matches); `_estack` is in DTCM, so there is no stack-collision reading either way |

**What landed: the discriminator, not a policy change.** `MemoryMonitor::probeInternalHeapAllocatability(bytes)` takes
the exact block with raw `malloc` and gives it straight back, reporting the tail, the request, and **whether internal
heap served it** and (only if it could not) whether PSRAM could — deliberately separate from `probeBlockAvailable()`,
which answers "can anyone serve this" and is an admission *authority* for the point-of-use reserves. It is **never
consulted by `admitOperationMemory`**.

On demand from the host, never per turn: `!HEAP_PROBE` (in the existing `!` command surface, **stopped-only** so it
cannot run while a deferred stop owns the turn) asks for the exact 16 KB that `OverdubStart` requires — the same
margin `OperationKind::BootProbe` was deliberately written to mirror — and emits a plain `HEAP_PROBE,tail=…,want=…,
malloc=…,pool=…` line **plus** the Tier-A row `#CAP,<us>,DIAG,heap_probe,tail=,want=,malloc=,pool=` (classified in
`DebugSessionCapture.cpp`, both boundaries pinned in `test_capture_ring`, pre-flight `OK DIAG,heap_probe, (16/31 bytes)`).

**Verification.** `test_memory_pressure_policy::test_heap_allocatability_probe_separates_tail_from_a_servable_request`
pins the asymmetry on the host: with the tail injected at 45 KB — below the 32 KB floor + 16 KB margin — the 16 KB
request is still servable, the probe reports both facts separately, the tail is unchanged by asking, and a zero request
is not misread as a refusal. `test_memory_pressure_policy` 10/10, `test_capture_ring` 38/38, full native **3,516 / 7
skipped / 3,509 succeeded / 0 failed**, `teensy41-capture-serial` **SUCCESS** at `variables 96,192` /
`code 390,280 + padding 2,936` (floor 840, 2,096 B headroom, **no re-base**).

**Decision rule for the one device reading this needs** (fixed in advance, so the answer cannot be reinterpreted):
- `malloc=1` with `tail` below the 48 KB admission sum → the admission test is **RED**: it is refusing on arena
  high-water while the required block is available. Then the policy question is how to express "the floor has been
  reached" in allocatability terms — and *not* whether to keep the 32 KB floor, which the owner has already ruled
  sensible and which the earlier threads never implicated.
- `malloc=0` → the tail was telling the truth at that moment, the false-denial hypothesis is dead, and the
  allocation-event census (counts, sizes, largest) becomes the next step — still ahead of any storage redesign.

**What this does *not* decide.** The 16 KB probe answers "can a 16 KB block be taken", not "is there enough margin for
the operation as a whole": the floor is headroom, not one allocation. So even a `malloc=1` reading REDs the *metric*,
and any change to the policy (probe-authoritative, or a free-list-aware measure) is a separate decision with its own
contract — not a bug fix slipped in on the probe's result.

**Not done:** the probe is **not on the board**. The flash carrying it (`code 390,280`) is a new image, and the board
runs `f1603da2…`; the owner's capture session is the subject of §19–§20 and must end (or be explicitly ended) before a
reflash, because `!HEAP_PROBE` does not exist in the running image. No policy change, no class instrumentation, no SD
action.

## 22. The probe answered: **24 KB tail, 16 KB internal malloc succeeds, admission says no** (2026-10-27, device)

Second upload on owner authorization — `sha256 7b4ebc25be9a3382d5be453155dfb1bc611f463b0c57d25b6e39505a53c1958c`, loader
2.3, `Found HalfKay Bootloader`, `Programming` complete, `Booting`, **exit 0**; RAM1 `variables 96,192` /
`code 390,280 + padding 2,936`. Boot proved separately: `boot_reset,crash=0`, `BOOT,load,start`, `BOOT,ram1,196608`,
`BOOT,load,ok`, `16C0:0489` `Teensy MIDI`, and `DIAG,alloc_census` rows from the new image.

The board came up with the previous session's content restored and reached the failing state on its own. Both halves of
the question, from the same device image, seconds apart:

```
#MEM_DENIED,68222522,op=overdub_start,reason=heap_low,need=16384B,heap_free=24KB,pool_refused=0/0B,since_refusal=4294967295ms
#MEM_DENIED,70333663,op=overdub_start,reason=heap_low,need=16384B,heap_free=24KB,pool_refused=0/0B,since_refusal=4294967295ms
#CAP,93604090,DIAG,alloc_census,heap=24576,hmin=24576,chunks=392,cap=0,edit=0,undo=4,ib=36,eb=0,ub=36,xb=5632
#CAP,94513134,DIAG,heap_probe,tail=24576,want=16384,malloc=1,pool=0
HEAP_PROBE,tail=24576B,want=16384B,malloc=1,pool=0
```

**`malloc=1` at a 24,576-byte tail.** The 16,384-byte block that `OverdubStart` requires was served by the **internal
heap** (`pool=0`: PSRAM was not needed) at the exact value at which the same image refused the start. Per the rule fixed
in §21 before the run, that makes the **admission metric RED**: it is deciding on the arena's untouched tail
(`&_heap_end - __brkval`) while the block it is refusing for is available inside the arena. `pool_refused=0/0B` rules
out the external-pool side of the same decision.

**What this does and does not settle.**

* Settled: the metric is a lower bound being used as a verdict. `tail >= need ⇒ allocatable` holds; the converse does
  not, and §21 pre-registered `malloc=1` under 48 KB as red — the reading is that, on the first attempt, in the reported
  failure state.
* Not settled: **which** free region served the block (a newlib free-list hole below `__brkval`, or part of the tail) —
  it does not change the RED, but it does matter for the replacement metric, and it is why "total free bytes" alone was
  never going to be the answer either.
* Not settled: whether the *operation* has enough margin. The 32 KB floor is headroom for the whole start, not one
  16 KB block, so this REDs the **measurement**, not the floor. The owner's own framing — "the existing 32 KB safety
  floor may still be perfectly sensible; what is now suspect is how you calculate whether that floor has been
  reached" — is exactly the boundary this evidence supports.

**Also confirmed on this boot, independently of §20:** the same four-step shape recurs with restored content, and
`ib=36` (`ub=36`, `eb=0`) is the whole accounted internal share while the tail fell 192,512 → 24,576. The census classes
remain non-contributors; the probe, not the census, is what moved the question.

**Gate note (honest):** `!HEAP_PROBE` is stopped-only, and the device was playing throughout, so the reading took a
poll that waited for a stopped window — `HEAP_PROBE,denied,transport_running` appears immediately before the successful
row. The failing press itself happens *while the transport runs*, so a diagnostic that is only available stopped cannot
be read at the moment it is most needed; widening that gate (or making the probe's answer available in the denial row
itself) is a decision for the next slice, not a silent change.

**RED consequence, stated as the next step, not taken here:** the admission policy for `OverdubStart` needs a
contract-level replacement of `heapFree < floor + margin` with an allocatability-based measure (probe-authoritative or
free-list-aware), landed as its own slice with its own fixture — a native RED first, since the host can express the
asymmetry exactly (`test_heap_allocatability_probe_separates_tail_from_a_servable_request` already pins it). No policy
change was made in this session; the 32 KB floor is untouched, and the image on the board is `7b4ebc25…`.

## 23. Third instance, and it names the mechanism candidate: the steps land on **snapshot publication completions** (2026-10-27)

From `captures/session_20261027_140638.log` (the owner's next session, image `7b4ebc25…`; it opens with the tail of the
previous epoch and then a **fresh boot** at 6.6 s of the new one, so the two are kept apart below).

**Fresh-boot trajectory:** 192,512 → 159,744 (8.6 s) → 151,552 (16.6 s) → 126,976 (26.6 s) → 118,784 (70.6 s) →
81,920 (72.6 s) → **36,864** (132.7 s, a −45,056 B step) — i.e. 36 KB of tail left on a board that has been up for two
minutes. `DIAG,pressure,Normal->Low,36864,388,0` fires on that step, exactly as at 45,056 in §20.

**The step's own neighbourhood is the new information.** At 132.14 s, immediately before it:

```
DIAG,win_frozen_readiness,7,3,build_drop,published_as_current,13,99328,1,1883990820,2697524,0,16128,16,6,0
DIAG,pressure,Normal->Low,36864,388,0
```

a **playback-window readiness/adoption edge** (`published_as_current` names the frozen window being adopted, not a
snapshot allocation), with `cap=13` and `undo=10` unchanged across the step.

**Retraction (same day, from the counters that already exist).** The paragraph that stood here read the neighbour as a
snapshot publication and concluded the step matched `kSnapshotPublicationReserveBytes`. That is **wrong on both counts**
and is withdrawn: the row is a *readiness* edge, and the `DIAG,snapshot_alloc` family (64 rows in this session, carrying
cumulative `nodes`/`bytes`/`slabs` plus the live-slab census) shows publication did **not** move at the big steps —
across the −45,056 B step at 132.7 s there are **no** `snapshot_alloc` rows within ±4 s at all. Correlating every step
against the bracketing counter rows: −24,576 B at 26.6 s moves nodes 2,134 → 2,241 (slabs 6 → 6); −8,192 B at 70.6 s and
−36,864 B at 72.6 s share one slab event (13 → 14); −8,192 B at 16.6 s and −45,056 B at 132.7 s show **no** counter
movement. So snapshot allocation is neither the size nor the timing of these steps. What survives is the weaker,
safer statement: the steps are **transient peak-setting**, they are not pass-proportional, and no enumerated owner
explains them.

**What survives as the best reading, and the one instrument that would settle it.** `__brkval` only ever rises, so every
step is *the moment a request had to be served from the tail instead of a free-list hole* — i.e. a new peak in some
transient, then reused forever after. Which transient, this data cannot say: no enumerated owner moves with the steps
(classes ~0–2 KB, pins zero, snapshot counters flat at the big ones). Naming it needs the one measurement nobody has
taken — **per-`_sbrk` accounting**: count calls, bytes, and the largest request, tagged by the call site that is
mid-turn. `_sbrk` is a single weak symbol in the core (`startup.c`), so this is a small, bounded wrapper rather than a
class-by-class census, and it answers "who raised the arena, when, and by how much" directly.

**The census also passed its own test here.** In this shape the undo entries *do* allocate: `ib`/`ub` grew **+256 B per
overdub** (0 → 2,048 over 8 entries), `eb=0`, `xb` +16 B per pass. So the instrument reports growth when growth exists —
and the growth is still **2 KB against 110 KB of steps**, three orders of magnitude apart. Classes remain
non-contributors; they are no longer being reported as an artefact of a shape where they happened to be zero.

**Standing state:** `[Memory]` shows `live 4,475 → 4,497 KB` external, `slab_live 7 → 17`, `snap 171 → 435 KB`,
`pin1=0, pin24=0` — content went where it was designed to go, nothing is pinned. At 36,864 B the admission sum (48 KB)
is missed, so the **next overdub press is a guaranteed `heap_low` denial** — and §22 already proved a 16 KB internal
malloc succeeds at 24,576, so it would be a false denial again, at a still higher tail.

**Two next actions, both awaiting the owner, neither taken here:**
1. Widen `!HEAP_PROBE`'s gate (it is stopped-only while the failure occurs while playing), or fold the probe's answer into
   the `#MEM_DENIED` row itself — the denial row is where the question is actually asked.
2. The admission-metric RED and its replacement (probe-authoritative or free-list-aware). **Independent of the arena
   question:** even if a transient peak is what lowers the tail, the tail is the wrong input for a start decision — §22
   is the proof. The peak's owner matters for *reclaiming* headroom, not for deciding this denial.
3. **Per-`_sbrk` accounting** (calls, bytes, largest request, call-site tag) to name the peak's owner — the only
   measurement that turns "transient peak-setting" into a named owner, and much cheaper than a class-by-class census.

## 24. Cross-check against the session where the symptom was reported: the whole session ran at a 28 KB tail (2026-10-27)

`captures/session_20261027_105518.log` is the session behind the original report, and it settles the causal chain for
"I cannot start any new overdubs after a few":

| observation | value |
|---|---|
| `[Memory] ext` rows | 54, **every one** `heap_free=28KB` (397 occurrences of that exact token, one unique value in the whole log) |
| `#MEM_DENIED` rows | **308**, of which `reason=heap_low` 312 occurrences (some rows carry it once) |
| split by operation | **177** `op=snapshot_reserve`, `need=8192B`; **134** `op=overdub_start`, `need=16384B` |
| `Overdubbing started` rows | **0** |
| `DIAG,reclaim` rows | **14,169** (the reclamation storm from the original report) |

Two facts make this decisive:

1. **The tail never moved for the entire capture** — 28 KB at the first row and 28 KB at the last. Once `__brkval` has
   risen, nothing lowers the number again, so *every* later admission is judged against the same low value for the rest
   of the session. That is the "after a few, never again" shape, and it is a property of the metric, not of the workload.
2. **An 8 KB request was denied at 28 KB of tail** (`op=snapshot_reserve,need=8192B`) — while §22 measured a **16 KB**
   internal malloc succeeding at a **24,576 B** tail on this same board. So in the reported session both families of
   denial were false, and the reclamation storm (14,169 attempts, `passesReclaimed=0`) was the system repeatedly trying
   to buy back a number that was never capacity in the first place.

**The chain, as evidence now supports it** — and each link is measured, not inferred:
1. Early in a session, unrelated transients raise `__brkval` from 192,512 B to 24–45 KB of tail (§23; and in
   `session_20261027_140638.log` the first 65 KB of that happened while `cap=0`, i.e. **before any overdub**).
2. `_sbrk` is a pure bump, so the tail never recovers (§21, core `startup.c`).
3. The admission predicate compares that tail against floor + margin (48 KB for `OverdubStart`, 40 KB for
   `snapshot_reserve`), so once the peak is set, every start is refused (§20/§22/this section).
4. The refusal is not a resource shortage: the required block is allocatable, proven on hardware (§22).

**What is still unknown and does not block the fix:** which transient sets the peak. That matters for *reclaiming*
headroom (and for whether 188 KB of fresh headroom is simply too small for the role set), not for the correctness of the
start decision.

**The one number still missing, and it is cheap:** the *largest allocatable block* at the low tail. §22 answered "is
16 KB available" (yes); a ladder on the same probe (16/24/32/48/64 KB) answers "how much headroom do I really have",
which is the quantity a replacement metric would have to compare against the floor. Two numbers per ladder step, no new
stimulus, and the probe already exists.

## 25. The allocator-authoritative admission was built, flashed, and **withdrawn the same day** — with the mechanism (2026-10-27)

Owner's directive was a four-step slice: one-shot denial-edge ladder → pin the metric RED → replace the `__brkval` tail
predicate with an allocator-authoritative capacity test (floor and margin preserved) → confirm on device that an overdub
starts at the reported 36/28 KB state. Steps 1–3 were built and pass natively; **step 4 was reached and failed, and the
failure indicted step 3.**

**What was built.** `MemoryMonitor::allocatorCanServeInternalBlock(bytes)` (one raw `malloc`/`free` of
`floor + margin`), `probeHeapCapacityLadder(need)` (rungs 16/24/32/48/64 KB, descending, abandoned at first success so at
most one rung is ever resident), the Tier-A ladder row, and — after a first attempt cost exactly +32 B of DTCM and was
refused by the RAM1 budget gate, which was **not** re-based — a version whose ladder rides the existing `heap_probe`
prefix and whose emission is gated by the existing denial-report window instead of its own timestamp. Native RED proven
discriminating: with the policy mutated back to tail-only it fails at *"a start must be admitted when the allocator can
serve floor + margin"*, and passes with the change. Build `SUCCESS` at `variables 96,192` unchanged, padding 2,744 ≥ 840.

**What the device said.**
1. Image `2776cae3…` (policy applied to **every** non-point-of-use operation): a long press then a **hang/reset**. The
   probe sat on `SnapshotReserve`, which gates *every* publication turn on the idle lane — a malloc/free storm by
   construction, on the lane whose admission is the most repeatedly evaluated.
2. Scoped to gesture admissions only (`OverdubStart`/`RecordStart`/`SlotSwitch`), image `e65d9d0e…`: no hang in the
   captured window, but the owner's retest was **"still refused to stop"** — a symptom that had not existed before this
   slice. `#MEM_DENIED` count in that window: **0**, so the stop was not a memory denial; it was the deferred-stop
   machinery failing to finalize.

**The mechanism, and why it is not a tuning problem.** `__brkval` is a bump high-water. A probe that asks for
`floor + margin` **takes that memory from the arena it is judging**, and when it is served from the tail it raises
`__brkval` *permanently* — so every successful probe lowers the reported tail for every gate that still measures by it.
The gates that still do are exactly the ones the deferred stop depends on (`SnapshotReserve`'s tail test, the transfer
horizon, prewarms). So the "fix" for a false denial at the start **manufactured real starvation downstream**: the start
was admitted, then the stop could not finalize. That is the opposite of a safe trade, and it is why the honest reading
of the retest is that the change caused it.

**Withdrawn in code, not just in prose.** `admitOperationMemory` is back to the tail predicate
(`heapLow = tailLooksLow`), with the reason recorded at the site; the native mirror matches; the automatic
denial-edge ladder emission is **removed** (its rungs allocates at the worst possible moment and perturbed the failure it
measured); the ladder and the probe remain **on demand only**, behind `!HEAP_PROBE` (stopped-only, host-requested), which
now emits both rows. The native fixture was rewritten from "must admit" to
`test_tail_predicate_falsely_denies_a_start_the_allocator_could_serve`, which pins **both** facts: the false denial is
real (`allocatorCanServeInternalBlock(floor+margin)` is true at the 24 KB reported tail while the predicate refuses),
and `SnapshotReserve` keeps its tail test with its own margin — the gate a probe was measured to starve.

**Constraint for any future attempt (this is the reusable finding):** an admission test may not *allocate* from the
arena it protects, because the measurement changes the measured state and competes with live owners at exactly the
moment capacity is scarce. The remaining honest options are (a) allocator statistics that do not allocate (`mallinfo` is
absent from this toolchain, so this needs a tracked-allocation counter, i.e. plumbing), or (b) an admission that does
not pre-allocate at all: let the operation attempt its own reserve and *retry* on failure, turning a false denial into a
bounded retry instead of a probe. Either is a new slice; the probe-based one is closed.

**State left behind:** board runs `e65d9d0e…` (the scoped-policy image that still shows the stop refusal); the reverted,
built image `97895a6e…` was prepared but the owner chose not to flash it yet (`Not now`), and the waiting loader was
killed so nothing can program by accident. Native: `test_memory_pressure_policy` 12/12 after the rewrite; full-suite
state on the reverted tree is recorded in the closeout entries.

## 26. The stop wedge, found in code: one dead job, one unreported phase, one terminal `return false` (2026-10-27)

Owner directive: *"can't you find it in the code?"* — so this section is a read, not a measurement, and no
instrument was added until the machine had been traced end to end.

### 26.1 What the device row actually means (verified)

`Overdub stop refused: stop_job_pending …` is **stale text**. `Track::stopOverdubbing`
(`src/Track/TrackOverdubLifecycle.cpp`) has not refused since DEC-078: on a capacity answer it calls
`latchAcceptedStop(...)` and *then* logs a row whose wording says "refused". The press is accepted and retained.

### 26.2 Why it is permanent (verified)

The accepted obligation is re-driven from `Track::serviceAcceptedInteractionTurn()`, whose first line refuses to
drive anything while a job is pending:

```cpp
if (!hasAcceptedUnexecutedInteraction() || hasPendingDeferredOverdubStop()) return false;
```

⇒ **one wedged job refuses every later stop, for the life of the session.** This matches the failing capture
exactly (`captures/session_20261027_151337.log`, 4,967 lines, 570 s): three rows at 83.948 / 85.402 / 93.574 s
naming the *same* `slot=3 interval=17 phase=2 dest=0 closure=0 recycle_owner=0 recycle_started=0`, while
`Overdubbing started` appears at 82.929 s — a successor interval was admitted on top of the pending job and the
job never moved for the following ten seconds.

That session also settles two earlier hypotheses as artefacts: `stop_grain`/`stop_phase`/`ODUB` are **not**
Tier-A (`isTierATextLine`, `src/Utils/DebugSessionCapture.cpp`), so the budget-8 timing-critical flush shed the
whole stop family for 570 s — "0 `stop_grain` rows" therefore never proved "the grain never ran" (recorded here
because §24/§25 leaned on it), and the absence of a `Deferred stop stalled: …` row proves nothing either, since
`deferredStopStallNoted_` latches once per admission.

### 26.3 The gate that could not answer (verified)

`PendingStopPhase::CanonicalOrder` (phase 2) is gated by two calls only:

```cpp
if (!runtime->beginFrozenCaptureCanonicalOrder(stop.frozenGeneration)) return false;   // five conditions, no report
if (runtime->stepFrozenCaptureCanonicalOrder(...) == Denied) return false;             // no report
```

The stall reporter above that branch checks `frozenGeneration` alone, so every other denial passes it silently.
Worse, `beginFrozenCaptureCanonicalOrder` is the phase's **only** re-bind site and returned
`matches(...) && (active()||complete())` once a binding existed: a binding that names another interval can never
grow a matching one, so that `return false` is terminal by construction.

### 26.4 What was landed (native + build; no flash)

1. `FrozenCaptureCanonicalOrderBegin` names all nine outcomes; `frozenCaptureCanonicalOrderBeginName` is
   flash-resident and feeds `noteStallOnce`, so the next device run prints *which* predicate refused.
2. Self-heal **only** where refusal is unwinnable: a binding that names another interval, with no build and no
   rewrite cursor in flight, is retired and re-bound. A binding that names *this* interval (rows, epoch or
   projection moved) still returns `StagingContentMoved`, and anything in flight still returns `BindingBusy` —
   the pre-existing byte-exact guards in `test_frozen_order_binding_rejects_changed_generation_count_and_projection`
   and `test_frozen_canonical_order_receipt_expires_after_audible_discard` keep their exact semantics.
3. The human row is now `Overdub stop retained (capacity): <reason> slot=… interval=…`. Every host matcher
   addresses the row by family, not by verb (`retention_probe`'s L2 read the literal verb in two places —
   `HUMAN_DENIAL_ID_RE` *and* the line locator — which is the same defect one layer up; both are fixed and
   `first_human_denial` makes the family explicit).

### 26.5 Verification

- `test_playback_prewarm` **251/251**, three new cases. The RED
  `test_a_stale_canonical_binding_for_another_interval_rebinds_instead_of_wedging` is red-proved by mutation
  (`if (true)`, i.e. the pre-fix behaviour: `Expected 0 Was 6`); under the same mutation the in-flight control
  fails with `Expected 5 Was 6`, which is what makes it a control of the *named* reason rather than of refusal.
  Post-fix: the rebound projection contains the current rows only (`orderedEventCount == 2`) and
  `frozenCaptureCandidateReady(81)` is false — the retired interval is not resurrected.
- Host: `test_retention_probe` 44/44 (new case pins that both verbs answer identically),
  `test_capture_transitions` 63/63 (new case pins the `retained (capacity)` row as the denial),
  `test_accepted_sequence` 194/194, `test_retention_handoff_plan` 33/33, `test_hitl_boot_handoff` 33/33,
  `test_check_marker_contract` 14/14. No new `#CAP` tag was added, so no Tier-A boundary moved.
- Full native **3,521 cases / 7 skipped / 3,514 succeeded / 0 failed**.
- `teensy41-capture-serial` **SUCCESS**: RAM1 `variables 96,192` / `code 390,472 + padding 2,744` (floor 840),
  no re-base. The cost is a return type, not a field; the reason literals sit with the flash-resident function.
- Unrelated and untouched: `scripts/test_accepted_sequence_scenario.py` fails on `PRESET_SCENARIOS` (five presets
  added by the concurrent display/playback slice; that test file is unmodified here).

### 26.6 What this does not claim

It does **not** name the device's predicate. It removes the possibility that the phase refuses silently, and it
removes the one refusal that can never recover. The next ~8-cycle gesture answers the open question with one row:
if it prints `Deferred stop stalled: staging generation moved` (or `staging content moved under the binding`),
the wedge was a real invalidation and this change is a reporter only; if it prints nothing and the stop completes,
the stale binding was the wedge; if it prints `order scratch owned by an in-flight build`, a second lane is
mutating the shared scratch under the stop.

## 27. The stop wedge named on device: `CanonicalPrefix` never advances while bound for PLAYING, and the ring is what makes it fatal (2026-10-27, two captures)

Owner ran the same ~8-press gesture twice on the instrumented build (`captures/session_20261027_160050.log`,
22,036 lines; the older `captures/session_20261027_134320.log` carries the identical signature).

### 27.1 The stuck phase is 1, not 2

`DIAG,stop_phase` aggregates carry `track, slot, phase, generation, epoch, destination, calls, crossed,
unknown`. Decoded for the first overdub stop:

| time | phase | dest | grains | |
|---|---|---|---|---|
| 81.31 s | 0 `ReservedClosure` | Playing | 3 | admitted |
| 83.72 s | **1 `CanonicalPrefix`** | Playing | **768** | aggregate at the 48-row budget edge |
| 108.72 s | **1** | Playing | **9,370** | still phase 1, 25 s later |
| 108.72 s | 1 | **Stopped** | 1 | transport stop → retarget → advances on the first grain |
| 108.73-108.75 s | 2, 3, 4, 6 | Stopped | 1 each | the rest of the machine runs |

`134320.log` shows the same job at **9,607** grains. The `Stopped` retarget advances immediately because that
branch bypasses `stepCanonicalBase` entirely, so the blocked call is `stepCanonicalBase` at destination Playing --
i.e. the canonical **base** build, not the ordering/copy/transfer that the §26 work instrumented. `Deferred stop
stalled:` therefore printed nothing, correctly: it covers phase 2 only.

Healthy stops build the same base in **68, 208, 369, 438, 471** grains (`140638.log`, five stops of the same
24-bar take), and healthy tail phases (`CanonicalTail`) in 56..233. So the stall is ~20x the healthy cost, not
"slow".

### 27.2 Why it becomes "cannot stop after around 8 times"

Each later press admits one more job **behind** the stuck head -- nine generations appear in order
(29, 19, 22, 10, 7, 18, 9, 14, 89), each logging `Reentry window open slot=3`. Then:

```
101.592 ODUB,stop,admission,...,8,0,successor_stop_denied_capacity,track=255
        [TRACK] Overdub stop retained (capacity): stop_job_pending slot=3 interval=19 phase=1 ...
104.278 same   106.373 same   107.754 same
```

`8` is `captureStopQueueDepth()`, each denial preceded by `custody_cap_denied`. **The ring holds eight**: with a
head that never retires, the ninth press is retained and the track stays overdubbing. Everything drains after the
transport stop (one job per ~1.9 s, 110.6 → 124.0 s), which is why the session ends with content rather than a hang.

Two further defects are visible and are **not** this wedge: every drained stop logs
`ODUB,stop,atomic_transfer,...,causal_base_superseded` plus `Overdub stop retarget: companion path stalled; raw
pass only` (the DEC-070 fallback firing for all eight, so their note-edit representation is lost), and from
124.1 s thirteen `#MEM_DENIED,op=snapshot_reserve,need=8192B,heap_free=28KB` -- the §24 metric defect, starting
after the drain.

The renamed row earned its keep on this run: `phase=1` is what pointed at the base instead of the phase 2 the
previous slice instrumented.

### 27.3 What was changed (DEC-165)

A preparation phase that cannot advance is now **bounded**, with an escape that cannot cost rows:

* `CanonicalPrefix`, 2,048 grains → abandon the base and continue at `CanonicalOrder`. The base is a
  **companion-only input**: ordering, the canonical copy and the transfer staging all read the frozen staging
  rows, so the captured content is untouched.
* The companion family keeps DEC-070's raw-only escape. Its budget stays **512** for a retargeted job (so the
  measured drain timing above is unchanged) and becomes **1,024** for a job still bound for PLAYING, which
  previously had *no* bound at all. `PrepareTransfer` and `UndoCollection` are excluded for a PLAYING job --
  staging the pass and undo identity are not ours to drop -- and the ordering/rewrite phases are excluded
  everywhere.

The bounds are no-progress budgets: the counter resets whenever the phase changes, so a healthy build never
approaches one. Nothing new is stored (`retargetStallPhase_`/`retargetStallGrains_` carry both families).

### 27.4 Verification

`test_stop_preparation_bounds_fire_only_where_an_escape_keeps_the_content` pins the whole table -- every phase,
both destinations -- including the two healthy maxima as *non*-firing points (471 base, 233 tail). Red-proved in
both directions: forcing `stopPreparationIsBounded` to `true` fails the content-bearing expectations, and setting
the base budget to 400 (below the measured healthy 471) fails the headroom assertion. `test_playback_prewarm`
252/252; `teensy41-capture-serial` SUCCESS at RAM1 `variables 96,192` / `code 390,472 + padding 2,744` (ITCM
unchanged, as these are flash-resident helpers).

### 27.5 Not claimed

**Why** the base never completes is still not named: the candidates are `stepCanonicalPrefix` denying every grain
(`captureBridgePhase != Idle`, or `canonicalPassInputs` attached/`Denied` -- the latter is *sticky*, nothing clears
`CanonicalPrefixPhase::Denied`), or the prefix/window pair pending forever. What this change guarantees is that
**the stop completes regardless**, within seconds, with a named row saying which escape fired. The next device run
should show `Deferred stop stalled: canonical base stalled; stop continues without it` followed by the existing
`companion path stalled; raw pass only` -- and the remaining question becomes whether the companion representation
can be restored for these stops, which is a separate repair.

## 28. The next run wedged in a *different* lane: the head job was a Record stop at `CanonicalOrder`, and a count-only bound had to be withdrawn (2026-10-27)

Owner built and flashed §27's change and re-ran the gesture: `captures/session_20261027_161759.log` (3,721
lines, 360 s). **It still could not stop** — and the log says why, in one row that is *not* about the overdub lane.

### 28.1 What the run shows

* `Record stop retarget: phase=2 dest=1` at 101.171 s, with **zero** `Overdub stop retarget` rows. That message
  is emitted only for a `job.mode == CapturePhase::Record` job (`TrackOverdubLifecycle.cpp:1899`), so the FIFO
  head that held the ring was the **Record** stop admitted at 68.746 s (`Recording stopped @ tick 18192`,
  `Playback started`, `Reentry window open`), parked at phase 2 -- `CanonicalOrder`, which for a Record job is
  `stepDeferredRecordNormalization`.
* All four retained presses report `phase=2`, `interval=17`, `closure=0` -- the *same* values the transport-stop
  retarget prints, i.e. the head never moved in 32 s.
* Eight overdub stops were admitted behind it (reentry windows 68.7 → 90.5 s), then four presses were retained
  (92.2, 94.2, 97.2, 100.2 s). After the retarget the log goes quiet for 260 s.
* `stop_grain` / `stop_phase` / `ODUB` are **absent**. That is *not* evidence about the lane: those families are
  Tier-B and this session kept only Tier-A rows (the §24/§26 trap, which is why §26 stopped leaning on them).

So §27's escape never applied: it lives in the **overdub** branch, and the Record branch returns before it
(`if (job.mode == CapturePhase::Record) { ... return false; }`). Two different lanes, one shared ring, one
identical user symptom. Lane-by-lane budgets are therefore the wrong shape, and this is the third flash that a
single-lane fix did not buy.

### 28.2 The count-only bound was wrong, and is withdrawn

§27 shipped the base bound as an unconditional 2,048-grain budget. Full native caught it immediately:
`test_shared_capture_finalization` **3 failures**, all `deferred Record stop did not retire: turns=30000
declared=524739670 ... phase=9 capture_store=empty`. Those fixtures legitimately *declare* hundred-million-grain
preparation budgets, so a fixed count retires healthy jobs prematurely -- exactly the failure mode the 128-cycle
plateau fixture exists to catch.

Shipped instead (re-verified): the base's bound is **gated on its cause** -- it applies only while a *successor
interval is capturing on the same slot* (`isOverdubbing() || isRecording()`), which is the state both device
captures were in and not the state any fixture is in. With no successor capturing, the base keeps its old
unbounded treatment. The abandonment is remembered on the job through a sentinel in the existing per-job stall
phase (`kCanonicalBaseAbandonedPhase`), so a base that a successor builds later cannot tempt the job back into a
tail that its own interval never earned; the tail then takes the raw-only route deterministically
(`stopCompanionPathIsUnreachable`), by cause and not by a budget. The companion family's DEC-070 bound (512,
retargeted only) is untouched, because extending it to PLAYING jobs also failed those fixtures (phase 9 park
after the forced retire).

`test_playback_prewarm` 252/252 with the table rewritten to the cause-gated contract; full native **3,522 / 7
skipped / 3,515 succeeded / 0 failed**; `teensy41-capture-serial` SUCCESS at RAM1 `variables 96,192` /
`code 390,472 + padding 2,744`.

### 28.3 What is actually needed next

The ring's head can park in **any** preparation lane, and there are three with no bound: the overdub base
(addressed, cause-gated), the overdub companion (DEC-070 covers the retargeted case only), and the **Record
normalization** lane that this run actually hit. A per-lane budget is a losing game -- and for the Record lane the
escape is not free: the only existing no-content route is `RecordEmpty`, which **drops the take**, so bounding it
by count would trade "cannot stop" for "silently lost recording". The two candidate shapes for the Record lane,
in the order they should be evaluated:

1. **Name it first, without a policy change.** The Record branch has no stall reporter at all; a per-job report on
   `stepDeferredRecordNormalization` (its result plus `remainingCanonicalPreparationGrains`, which is *consumed*
   per grain on this lane and is therefore already an exact declared budget) turns the next run into a verdict.
2. **Then either** publish a Record take whose normalization cannot complete through the raw/unsorted publication
   route (`RecordPassReady` → `stepDeferredRecordPublication`), **or** stop admitting successor intervals behind a
   head that is not progressing. The first keeps the recording and reorders display only; the second preserves the
   current publication semantics and costs only the automatic reentry.

Both are decisions about what a not-normalized take may publish, so they are recorded here as options, not
implemented.

## 29. The Record lane now names its refusal — report only, no gating, no bypass (DEC-166)

Owner's instruction: *implement the per-job Record-normalization reporter only, plus its native diagnostic
fixture; do not gate successors and do not bypass normalization; reproduce once and let the named phase-2 refusal
choose the repair.* Implemented exactly that.

### 29.1 Why this lane needed a reporter

`stepDeferredRecordNormalization` has six `Denied` returns and reported none of them. A Record job parked at
`CanonicalOrder` therefore produced *no evidence at all* — no phase change, no grain telemetry that survives the
Tier-B shed, and no stall row — while it held the FIFO head and (capacity 8) bricked the stop ring for 32 s
(`session_20261027_161759.log`, §28). The one term that matters most is `PreparationDenied`:
`RecordPreparation::Step::Denied` deliberately keeps its binding, so that job can never advance on this interval.

### 29.2 What was added

* `RecordNormalizationDenial` (`None`, `JobIdentity`, `OwnerLost`, `OutputNotReady`, `ChunkStarvation`,
  `RoleBindRefused`, `PreparationDenied`) and `recordNormalizationDenialName` — flash-resident literals in the
  lane's own cold unit, so no RAM1 cost.
* `stepDeferredRecordNormalization(job, denial*)` writes the refusing predicate to the job
  (`CaptureStopJob::lastNormalizationDenial`) and to the optional out-parameter. **Every branch is unchanged** —
  the same predicates, the same order, the same return value.
* The grain counts **consecutive `Denied` grains at the same phase** in the existing per-job stall fields
  (`retargetStallPhase_`/`retargetStallGrains_`, no new state) and, at
  `kRecordNormalizationDenialReportLimit = 64`, raises the standard family row
  `Deferred stop stalled: <term>` (once per job, via `noteStallOnce`) plus one detail line:
  `Deferred Record stop stalled: normalization <term> phase=<n> closure=… order=… canonical=… ranked=… notes=…`.
  The lane is resumable (`Pressure`/`Pending` consume a preparation grain and count as progress), so a healthy
  job never accumulates denials; a parked one accumulates thousands.
* Nothing is bypassed: a denial still consumes no credit and does not move the phase, so a stalled job's
  declared budget is untouched by the row.

### 29.3 Cost

`CaptureStopJob::lastNormalizationDenial` sits in the 1-byte slot the struct already had as padding before
`phaseTick`, so `sizeof(CaptureStopJobs)` stays **33,088 B** (the deliberate pin passes unmodified) and
`sizeof(Track)` stays 384. `teensy41-capture-serial` SUCCESS at RAM1 `variables 96,192` / `code 390,472 +
padding 2,744`.

### 29.4 Verification

`test_record_normalization_names_its_refusal_and_changes_nothing_else` (`test_shared_capture_finalization`): a
production Record stop is admitted, driven up the ladder to `CanonicalOrder` with **no** denial named on the way,
then a manufactured identity fault is injected; the fixture asserts the lane names it (`JobIdentity`), that the
counter saturates at exactly the report limit (one row per job), that the job is **still pending in the same
phase holding the ring** (report-only proven, not asserted by convention), and that all seven names are exact.
Red-proved by mutation: making the lane store `None` instead of the reason fails it at
`Expected 1 Was 0`. `test_shared_capture_finalization` **111/111**, `test_playback_prewarm` 252/252, full native
**3,523 / 7 skipped / 3,516 succeeded / 0 failed**.

### 29.5 Next

One reproduction of the same gesture. The expected evidence is `Deferred stop stalled: <term>` on a Record job —
`preparation_denied` would say the take's preparation is terminally refused (repair: the raw-publication route),
`owner_lost` would say the slot's finalization owner was reassigned (repair: ownership), `output_not_ready` /
`chunk_starvation` would say the private output could not be provisioned (repair: provisioning), and
`job_identity` would say the job and its interval have diverged (repair: supersession). No policy change is
pending on this evidence.
