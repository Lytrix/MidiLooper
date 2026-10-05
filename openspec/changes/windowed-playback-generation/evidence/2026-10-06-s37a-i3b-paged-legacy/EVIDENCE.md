# Task 3.7a (I3b) — the paged legacy path reaches the production lane: evidence

Packet (written **before** the production edit):
[`EXECUTION.md`](EXECUTION.md). Authority: `AGENTS.md`, `docs/Authority/IMPLEMENTATION_READINESS.md` (G2 execution
packet), `tasks.md` items **3.7a** and **3.12** (its owed production consumer), **DEC-108 §14(c)** (the bounded
segmented page path), `WindowPagedStream.h`'s own contract.

**What this increment claims.** An interval that cannot fit one window is now **prepared as pages** by the deferred
lane instead of ending as a refused demand: the same retained revision, the same pinned bodies (one enumeration for both
readers), one bounded page per turn, and a consumer entry that walks the prepared interval exactly once through the
runtime's own cursor — refusing a revision the loop has left by name. The **MIDI-emission adapter** (the hot path
emitting from the ring) is **I3c** and is *not* claimed here; its blockers are named below.

---

## 1. Production changes

| File | Change |
|---|---|
| `include/TrackPlaybackRuntime.h` | The per-slot paged state: `WindowPagedStream pagedWindow`, `WindowPageCursor pagedCursor`, `pagedActive`, `pagedRevision` (an **identity**, never a currency), `pagedInterval`, `pagedPagesProduced`, the named `PagedPlaybackOutcome` (`Prepared` is a delivery, not a refusal), `pagedActiveForInterval(...)`, `discardPagedWindow()`. |
| `src/Track/TrackPlaybackRuntime.cpp` | `discardPagedWindow()` (releases the ring's pages through `release()`, clears the outcome), wired into `reset()`. The two `LoopPlaybackRuntime` size pins re-based on **compiler-measured** figures (see §4). |
| `include/WindowPagedStream.h` | **Added `release()`**: the ring owns its fixed page storage, so a lane that abandons a preparation (or walks one to its end) returns it instead of leaving `kWindowPageCount` page buffers resident per slot. Additive — the class's contract (produce/consume/cursor semantics) is unchanged. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The **switch** at the two places the lane concludes the interval is over `L` (the gather's slice ceiling, and the admission's own `WindowExceedsRetainedCapacity` refusal); `beginPagedPlaybackForSlot`; `stepPagedPlaybackForSlot` (one page per turn, `RingFull` stops the turn); `consumePagedPlaybackWindowForSlot` (the consumer entry); the pinned-body enumeration extracted into one file-static `collectPinnedCaptureLists` used by both readers; the lane's own bookkeeping (`committedPlaybackRetryCanProgress`, the dispatch, `hasPendingPlaybackViewMaintenance`, `fillPlaybackHolders`, `noLiveOwnerForSlot`, `applyPlaybackViewTransition`). |
| `include/Track.h` | `PagedWindowConsumeResult` and the three entry points, declared with their rules. |

**Semantics fixed by this increment.**

- **The trigger is the window lane's own conclusion, never a pre-classification.** `appendWindowBudgeted` is bounded by
  *events per call*, so a sparse interval completes in a handful of slices and cannot reach the ceiling; reaching
  `kRetainedGatherMaxSlices` means the content is over `L`. The second trigger is the destination's own bound: a
  window the gather *can* traverse but the admission refuses for `kWindowAdmissionLimit`.
- **The switch prepares, it does not deliver.** The frozen demand stays the promise; `pagedOutcome = Prepared` is
  written only when the source is complete, and then the promise is discharged (a window was asked for, and a window is
  what the content cannot fit). `retainedGatherRefusal = WindowExceedsCapacity` names *why* the window lane stopped.
- **One turn is one bounded page**, and the ring's three pages are a bound: `RingFull` → `ProducerWaiting`, the turn
  denied, the demand still owed — never an allocation, never an overwrite.
- **Identity at the read.** `pagedRevision` is stamped `reset`, the cursor carries it, and the entry refuses
  `StaleRevision` when the loop's revision has moved. A walk that ends (`Exhausted`) releases the ring's storage and
  keeps the name (`Drained`).
- **One walker per slot.** The entry reads the runtime's own cursor, so a caller cannot re-enter at a stale position,
  and a re-preparation resets the cursor: an interval re-prepared for the *same* revision cannot be continued with the
  previous interval's position.
- **A launch keeps its own answer for dense legacy content.** Switching a launch here would remove the merged-material
  fallback (a working delivery) and replace it with a preparation nothing emits from until I3c — so the switch is
  `CommittedSnapshot`-only, by name, and the launch's `launch_fallback_dense` path is untouched.
- **A paid debt is kept.** The admission-refusal branch no longer drops the demand: it is coalesced back into the record
  the lane reads (and an admission that already retained its own record is left untouched). Before this increment that
  branch could drop a promise in the narrow band where the gather completed *and* the window was over `L`.

**Named limitation (not smoothed over).** The paged path emits in **traversal order** (pass-set order, chunk order
within a pass). Global tick-ordering *across layers* is the emission adapter's business in I3c — the same division I1
made for windows, where the completing gather orders its own answer by hand. Fixture **W-37a-16** therefore
cross-checks against the effective-content resolver on **single-pass** content, where traversal order *is* tick order;
multi-pass ordering is not claimed.

## 2. Oracle and fixtures

New fixtures in `test/test_runtime_working_set_ownership/` (registered in `main`, four cases, all passing):

| Case | What it proves |
|---|---|
| **W-37a-15** `test_3_7a_i3b_an_over_capacity_demand_is_prepared_as_pages` | The window lane spends its whole ceiling first (18 pages), the concluding turn switches the demand to pages, produces **exactly one** page, releases the gathered window (capacity returns to 0), names `WindowExceedsCapacity`, and records the same revision+interval; the ring then fills to `kWindowPageCount` with **one page per turn** and the next turn is `RingFull` → `ProducerWaiting` with the counts unchanged. |
| **W-37a-16** `test_3_7a_i3b_pages_walk_the_interval_exactly_once_across_pages` | The consumer entry walks the interval across pages with the producer running behind it (`NeedsPage` drives the lane), and the emitted sequence **equals** `loop.copyEffectiveCommittedEventsInRange` for the same interval — same size, same ticks, same types, same notes and velocities — i.e. no duplicate and no missing event at a page boundary. The walk's end releases the ring's storage (no page left published; a further read answers `NoPages` rather than resuming into pages that no longer exist). The **second** walk, after re-preparation, repeats the same content, and the second walk uses a **partial buffer** (37 events per call, which does not divide a page). |
| **W-37a-17** (folded into W-37a-15/16, by the packet's own wording) | The ring's bound is respected rather than allocated around: at `RingFull` the counts do not move, and after the consumer drains a page production resumes with the next slice (the walk's sequence continuity proves it) until `sourceComplete`. |
| **W-37a-18** `test_3_7a_i3b_the_paged_read_refuses_a_revision_the_loop_left` | A page is emitted for the currency the pages belong to; once the loop's revision moves, the same entry answers **`StaleRevision`** and emits **nothing**, without discarding the preparation. |
| **W-37a-15, second trigger** `test_3_7a_i3b_an_over_capacity_admission_switches_to_pages` | An interval the gather *can* traverse (17 pages, inside the ceiling) whose window the admission refuses for `kWindowAdmissionLimit` switches to pages too, with the refused window released and the demand not dropped. |

Fixture shape: a rootless demoted slot whose dense bar holds 2,500 (2,100) events **inside the two-bar interval the
window path accepts** (`kMaximumWindowTicks`) — the events share ticks, the note advances per cycle so no event is a
capture-level duplicate, and the seal's stable sort makes one tick-ordered body. The oracle is the production
effective-content resolver, not a hand-written list.

## 3. Red-proofs

Each mutation was applied to the **final revision**, run against the focused suite, and the file restored
**byte-identically** (sha256 `3384f62271ed1cd8663badbfb990823ffac9eaeb140d0a1be6ce910a03abfe30` before and after every
run; the harness asserts the anchor is unique before patching).

| ID | Mutation | Result (measured) |
|---|---|---|
| **MR-3.7a-M** | The over-`L` switch removed (the budget branch refuses as before) | **RED** — W-37a-15, W-37a-16, W-37a-18 fail (`GrainServed` expected, the refusal's `GrainDenied` observed: *Expected 1 Was 3*). The admission-trigger fixture correctly still passes (its trigger is MR-P). |
| **MR-3.7a-N** | The page-per-turn bound removed (produce until the ring is full or the source completes) | **RED** — W-37a-15 (*Expected 1 Was 3. the concluding turn produces the preparation's first page, not a whole interval*) and the admission fixture. |
| **MR-3.7a-O** | The read-side revision guard removed | **RED** — W-37a-18 only (*Expected 2 Was 1*: `Emitted` where `StaleRevision` is required). |
| **MR-3.7a-P** *(added; the packet's MR set named only the budget-branch trigger)* | The admission's capacity refusal no longer switches | **RED** — the admission fixture only. |
| **MR-3.7a-Q** *(the packet's "cursor teeth" attempt)* | The entry re-feeds the page's offset after every `Emitted` outcome | **RED** — W-37a-16 (*Expected 2500 Was 18944*: the walk repeats its page instead of advancing). |
| **MR-3.7a-Q, first attempt (recorded as a non-red-proof)** | The same rewind, against the **original** fixture which always consumed with a full page | **NOT RED** — 154/154 passed: with `capacity == kWindowPageCapacity` the ring drains a whole page in one call, so the mutated line is a no-op for that shape. Recorded rather than reworded; the fixture was then given the partial-buffer walk, which is the shape that gives the cursor teeth. |

## 4. Verification

| Check | Result |
|---|---|
| Focused `-f test_runtime_working_set_ownership` | **154/154** (150 + the four new cases), 0 failed |
| Focused `-f test_window_paged_legacy` (3.12b's ring fixtures, unchanged) | **3/3** |
| Focused `-f test_windowed_preview_integration` (I3's fixtures, unchanged) | **9/9** |
| Full native `pio test -e native` | **2,829 cases / 7 skipped / 2,822 succeeded / 0 failed** (baseline 2,825 / 7 / 2,818 + the four new cases) |
| Device build `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 `variables 96,192` / `ITCM 393,216` (`code 392,216 + padding 1,000`) / `locals 34,880`; guard reports **160 B of headroom above the floor (840)** |
| `LoopPlaybackRuntime` size pins | Re-based in the same commit on **compiler-measured** figures: target **7,160 → 7,312 (+152 B)**, host **9,216 → 9,448 (+232 B)**; the difference is the two ABIs' vector headers and padding. The temporary size probe was removed (the probe's own diff against the pre-probe copy shows the two pins and nothing else). |
| `openspec validate windowed-playback-generation --strict` | **valid** |
| `git diff --check` | clean |
| Device run / upload | **NOT RUN** — no firmware was flashed and no HITL run was performed in this increment. |

**All of the above was measured on the final revision.** After the first pass, two assertions were added to W-37a-16 for
the packet's own invariant ("the paged state's buffers are released on discard/reset") — the walk's end leaves **no page
published** and a further read answers **`NoPages`** rather than resuming into released pages — and every figure in this
section was then re-measured on that revision: the focused suites, the **full native** run and all **five** red-proofs
(each restored byte-identically). The device build is unaffected in substance and stands as measured: it compiles the
**production** sources, which are unchanged by a test-only addition (sha256
`3384f62271ed1cd8663badbfb990823ffac9eaeb140d0a1be6ce910a03abfe30`).

**ITCM cost, recorded.** The approved I2/I3 line was `code 391,832 + padding 1,384` (544 B above the floor). This
increment is **+384 B of ITCM** (`code 392,216`, padding 1,000), leaving **160 B** of headroom. The floor still holds
and the guard passed, so no re-base is made (the floor is only re-based deliberately, with measurements). This is
**named as the tightest figure on the lane** and as I3c's first constraint: the emission adapter must measure its ITCM
cost against 160 B before landing, and if it does not fit, it belongs in flash with its own accounting rather than
moving the floor.

## 5. Corrections to prior records

- **I3's evidence line "151/151 (150 + the admission fixture)"** counted the admission fixture twice: the tree committed
  at `9cd96038` registers **150** `RUN_TEST` cases, and the run performed there reported 150. The I3b baseline is
  therefore **150/150** for that suite. Recorded here rather than amended silently in I3's file.

## 6. Not claimed

- **No MIDI-emission adapter (I3c).** The hot path does not emit from the ring: `ExecutionBounds.md` row 27 still
  denies active windowed playback, and I3c's blockers are (a) a stream index → page base mapping (a page carries only
  its `sequence`), (b) page retirement driven by the emission position rather than by `consume`, (c) the timing
  envelope measurement, and (d) the lane gate that would enable it.
- **No ordering claim across layers** (see §1's named limitation).
- **No timing/deadline measurement** for the paged path, no `ExecutionBounds.md` row change, no device run, no upload,
  no HITL run.
- **No fallback removal** (3.7b), no change to the snapshot lane's `WindowExceedsRetainedCapacity` refusal, no change to
  the launch lane's dense-legacy fallback, no new lane or scheduler.
- **3.12 stays open**: its production paging *consumer entry* is delivered here, but its fixture 5.5 timing envelope and
  the emission integration (I3c) remain.
