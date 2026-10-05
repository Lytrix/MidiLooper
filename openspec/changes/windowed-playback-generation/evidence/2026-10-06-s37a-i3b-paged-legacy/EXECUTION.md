# Task 3.7a — increment I3b: the paged legacy path reaches the production lane

Execution packet for **I3b** of slice 3.7a, written **before** the production edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), the ladder
([`../2026-10-06-s37a-production-consumer/EXECUTION.md`](../2026-10-06-s37a-production-consumer/EXECUTION.md) row **I3**,
which [`../2026-10-06-s37a-i3-preview-wiring/EXECUTION.md`](../2026-10-06-s37a-i3-preview-wiring/EXECUTION.md) split into
I3 (preview, landed) and **I3b**), `tasks.md` items **3.12** (its owed production consumer) and **3.7a**, **DEC-108
§14(c)** (the bounded segmented page path) and `WindowPagedStream.h`'s own contract.

**Scope split, stated up front.** DEC-108 §14 describes the paged path as a **storage bound, not throughput**, and
`ExecutionBounds.md` row 27 still denies active windowed playback. This increment therefore lands the **production
trigger and the consumer entry point**: an interval that cannot fit one window is now *prepared as pages* by the
deferred lane instead of ending as a refused demand, and a caller can walk those pages exactly once through the
runtime's own cursor. The **MIDI-emission adapter** (the hot path emitting from the ring) is **I3c** and is *not*
claimed here; its blockers are named below, not smoothed over.

## Identity and scope

- **Owner and chain:** `Track::stepCommittedPlaybackRetry` (the deferred qualification, I2) → the **new paged
  preparation** for an over-`L` demand → `WindowPagedStream::produce` (one page per turn) →
  `Track::consumePagedPlaybackWindowForSlot` (the consumer entry).
- **What I3b adds:**
  1. **The trigger.** The retained lane's own over-`L` detection (`kRetainedGatherMaxSlices` reached) currently ends in
     `WindowExceedsRetainedCapacity` with the demand retained. Measured fact the trigger rests on: `appendWindowBudgeted`
     is bounded by *events per call*, so a sparse interval completes quickly and cannot hit the budget — a budget hit
     means the interval genuinely cannot fit one window.
  2. **Runtime state**: one `WindowPagedStream`, its `WindowPageCursor`, whether paging is active, the revision and
     interval the pages belong to, the page count and whether the source is complete — all per slot, in the externally
     allocated runtime, released by `discardPagedWindow()` (swap, not `clear`) and by `reset()`.
  3. **One source of truth for the bodies**: the pinned-pass enumeration both readers use (`collectPinnedCaptureLists`),
     so the window gather and the pager cannot enumerate different content for the same revision.
  4. **The consumer entry**: `Track::consumePagedPlaybackWindowForSlot(slot, out, capacity, emittedOut)`, which refuses a
     cursor from another revision by name (`StaleRevision`) and never waits (the ring's own contract).
- **Preserved:** the window path (a demand that fits is unchanged), the merged fallback, the snapshot lane's own
  over-`L` refusal (`WindowExceedsRetainedCapacity` on that lane stays exactly as it is), every existing refusal name,
  and the hot path (untouched — I3c).
- **Primary files:** `include/TrackPlaybackRuntime.h`, `src/Track/TrackPlaybackRuntime.cpp`, `include/Track.h`,
  `src/Track/TrackPlaybackWindowBuild.cpp`, `test/test_runtime_working_set_ownership/…`.
- **Non-goals:** no hot-path emission adapter (I3c), no change to `WindowPagedStream`'s contract, no new lane/scheduler,
  no device run, no timing claim, no `ExecutionBounds.md` row change.
- **Commit boundary:** independently buildable; with no over-`L` demand the new state is idle and costs one boolean.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-15** an over-`L` demand is prepared, not refused | demoted slot with **more than `kWindowAdmissionLimit`** committed events in the interval's window, no published root, the current revision retained, prewarmed runtime; a CommittedSnapshot demand retained | drive the deferred turn repeatedly | the demand is **not** left as `WindowExceedsRetainedCapacity`: the lane switches to paging (`pagedActive`), each turn produces **at most one page** (`pagedPagesProduced` +1 per turn, never two), and the source eventually reports `sourceComplete` | today's behaviour (`WindowExceedsRetainedCapacity`, demand retained, no pages); or producing more than one page per turn; or truncating the content to fit a window |
| **W-37a-16** the consumer walks the interval **exactly once**, across pages | as W-37a-15, paging complete | consume into a bounded buffer repeatedly until `Exhausted` | the emitted sequence is exactly the interval's effective events in **non-decreasing tick order**, with **no duplicate and no missing** event, delivered across **more than one page**, and the second full walk of the same interval (after re-preparing) yields the same multiset | dropping or duplicating an event at a page boundary (the sequence/offset cursor abused); or emitting from an unpublished page |
| **W-37a-17** the ring's bound is respected, not allocated around | as W-37a-15 | produce without consuming until the ring is full | the produce call reports **`RingFull`** (never an allocation, never a silent drop); after the consumer drains a page, production resumes with the **next** slice | growing the ring (or overwriting an undrained page) to keep producing |
| **W-37a-18** identity is enforced at the read | as W-37a-15, pages produced for revision R, then the loop's revision advances | consume with the cursor from R while the loop is at R+1 | **`StaleRevision`**, and nothing is emitted | serving pages whose revision the loop has left |

**Invariants asserted, not assumed:** the window path is unchanged (its own fixtures stay green); the paged state's
buffers are released on discard/reset (the runtime's capacity returns to 0); the pager's identity is the retained
revision, never the playback generation; no RAM1 variable grows and no new DTCM state exists.

## Controls and coverage

- **Production paths exercised:** `requestCommittedPlaybackWindow` (the demand), `stepPlaybackViewMaintenance` /
  `stepCommittedPlaybackRetry` (the deferred turn), the pinned-body enumeration, `WindowPagedStream::produce/consume`.
- **Bounded work:** one page per turn (the same shape as I1/I2's page-per-turn gather).
- **Not covered here (I3c, named):** the hot-path emission adapter needs (a) a stream index → page base mapping on the
  ring (today a page carries only its `sequence`), (b) page retirement driven by the emission position rather than by
  `consume`, and (c) the timing envelope — `ExecutionBounds.md` row 27 denies active windowed playback, so enabling it
  before the envelope is measured would be a claim, not a delivery.

## Red-proofs (I3b)

| ID | Mutation | Must fail |
|---|---|---|
| **MR-3.7a-M** | the over-`L` switch removed (the budget branch refuses as before) | W-37a-15 (and 16/17/18, which build on it) |
| **MR-3.7a-N** | the page-per-turn bound removed (produce in a loop until the ring is full or the source completes) | W-37a-15 (the per-turn page accounting) |
| **MR-3.7a-O** | the read-side revision guard removed | W-37a-18 |

Each mutation is reverted and the files restored byte-identically (sha256 recorded); a mutation that does not fail its
fixture is recorded as a non-red-proof rather than reworded. The ordering/dedup fixture's own teeth are shown by
mutating the **cursor** semantics (re-feeding the same page's offset) if the ring's own implementation allows it; that
is attempted and reported either way.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (the new fixtures plus the lane's existing ones)
  and `test_window_paged_legacy` (3.12b's own ring fixtures, unchanged).
- Broad: `pio test -e native` — the baseline is **2,825 cases / 7 skipped / 2,818 succeeded / 0 failed**.
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` / `code 391,832 + padding 1,384` (the I2/I3 line), and `LoopPlaybackRuntime`'s two size pins are
  re-based **in this commit** if the new state moves them (compiler-measured on both ABIs).
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN**.
- Evidence path: `evidence/2026-10-06-s37a-i3b-paged-legacy/`.

## Completion record

**Filled after the run** (2026-10-06); detail and the full tables live in [`EVIDENCE.md`](EVIDENCE.md).

- **Scope as written, with two recorded deltas.** (1) The packet named one trigger (the gather's own slice ceiling);
  the implementation also switches at the **admission's own `WindowExceedsRetainedCapacity` refusal**, which is the same
  fact reached by the destination's bound — an addition inside the oracle's own wording ("more than
  `kWindowAdmissionLimit` committed events"), red-proved by the added **MR-3.7a-P**. (2) The switch is
  **`CommittedSnapshot`-only**: a launch keeps its dense-legacy merged fallback, because nothing emits from pages until
  I3c and switching a launch would remove a working delivery. Both are in the evidence; neither weakens a claim.
- **Oracle:** W-37a-15 (both triggers), W-37a-16 and W-37a-18 green; W-37a-17 was folded into W-37a-15/16 by the
  packet's own wording (the ring's bound, and the resume behind the consumer), and those halves are asserted there.
- **Red-proofs:** **MR-3.7a-M, -N, -O, -P, -Q all red** on the final revision, each restored byte-identically
  (sha256 `3384f62271ed1cd8663badbfb990823ffac9eaeb140d0a1be6ce910a03abfe30`). **One non-red-proof recorded as such:**
  MR-Q's first attempt was a no-op against the original full-page-consuming fixture (the ring drains a whole page in one
  call, so re-feeding an offset changed nothing); the fixture was given a **partial-buffer** walk, which is the shape the
  cursor's own advance is observable in, and MR-Q then failed as intended.
- **Verification:** focused RWS **154/154**, ring **3/3**, preview **9/9**, full native **2,829 / 7 skipped / 2,822
  succeeded / 0 failed**, capture build **SUCCESS** (`variables 96,192`; `code 392,216 + padding 1,000`; **160 B** above
  the floor), size pins re-based (**7,312** target / **9,448** host, compiler-measured), strict validation valid,
  `git diff --check` clean. **Device NOT RUN**, no upload.
- **Cost, named:** +384 B of ITCM against the approved I2/I3 line, leaving 160 B of headroom — the tightest figure on
  the lane, recorded as I3c's first constraint rather than smoothed over.
- **Correction to a prior record:** I3's evidence line "151/151 (150 + the admission fixture)" counted that fixture
  twice; the committed tree registers **150** and I3b's baseline is 150/150.
