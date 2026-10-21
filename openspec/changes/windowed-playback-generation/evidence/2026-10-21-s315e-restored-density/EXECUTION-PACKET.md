# 3.15e — execution packet (slice 1: G0 + restored-density measurement)

**Change:** `windowed-playback-generation` · **Task:** 3.15e (bounded dense/legacy and overdub content through the single
reader) · **Branch:** `feature/unified-rolling-playback-path` · **Base:** `7bf11593`.
**Slice 1 nature:** read-only discovery + **one temporary measurement fixture** (withdrawn before any commit). No
production line changes in this slice.

## Scope of this slice

3.15e's claims split into a *serving* claim and an *architecture* claim:

| # | Claim (task text) | Status before this slice |
|---|---|---|
| S1 | restored over-limit rows emit completely and in order through bounded pages, never refused for density | pages exist (3.7a I3b/I3c, 3.12/5.5); the **short** (whole-loop interval) shape is documented as `CannotStraddleTheWrap` |
| S2 | the paged storage is an **internal bounded window delivery**, never an alternative clock-edge stream | the ring still carries its own pass model and its own admission boolean, and the hot path consults it separately |
| S3 | same-tick accumulation and closure capacity keep their **named** refusals only at their owning boundary | `ClosuresExceedDeclaredCapacity` (3.12e repair) and `WindowExceedsRetainedCapacity` exist and are asserted |
| S4 | loop wraps do not duplicate or omit accepted events | I3c-b asserts two passes element-wise at the adapter; **not** at the production edge across loop wraps |
| S5 | live overdub stays an overlay with exactly-once note/ledger ownership; **no second writer to `current` or hidden whole-loop materialization** | merged fallback materializes the whole loop when no window route selects |
| S6 | exercise short and long lengths against one **independent effective-content oracle** | long only; no short shape |

Slice 1 measures S1/S4/S5/S6 before choosing any production change, because S2's correct shape depends on which of them
is false.

## Transition oracle (fixed before the measurement)

Content: a **2-bar** loop (1 536 ticks) and a **64-bar** loop (49 152 ticks) at restored density — 3 072 accepted events,
two per tick inside the first two bars, no duplicate `(channel, note, tick)`.

| Term | Oracle |
|---|---|
| completeness | over one full traversal, every accepted event is emitted exactly once: `emitted == accepted` |
| order | emitted `(tick, note, On/Off)` sequence equals the effective-content traversal order, element-wise |
| wraps | a second traversal repeats the same sequence, with no event carried over and none dropped |
| single reader | the serving route is a window/paged delivery; the **merged** store is not built for the slot |
| no hidden materialization | the runtime's merged store stays empty for the slot, and the paged ring is the bounded source |
| refusals | any refusal is **named** (`CannotStraddleTheWrap`, `ClosuresExceedDeclaredCapacity`, `WindowExceedsRetainedCapacity`) and happens at its owning boundary, not as silent fallback |
| independence | the expected stream is computed from the committed content (`copyEffectiveCommittedEventsInRange`), never from the ring's own state |

## Fixture-first rule for this slice

The measurement fixture is **temporary**: it is run once, its readings recorded here, and it is removed byte-identically
(`git diff` empty) before any commit. Slice 2's fixtures are written against the measured gap and land **GREEN with their
production change** — the suite never carries a red case.

## G0 findings (read-only, before measurement)

| Class | Finding |
|---|---|
| VERIFIED | The clock edge selects the paged ring **before** the merged fallback (`TrackPlaybackHotPath.cpp`, `pagedEmission = … admittedForEmission()`), and the ring feeds the **one** `advancePlaybackCursor` walk — there is no second edge consumer. `consumePagedPlaybackWindowForSlot` has **no production caller** (tests only) |
| VERIFIED | The ring's selection is a separate delivery decision: its own `bound`/`admissible` booleans, its own pass boundary (`servicePagedPassBoundaryForTurn` called from the hot path), its own producer (`stepPagedPlaybackForSlot`) and its own retirement (`retireThroughOrdinal`). That is the "alternative stream" the task names — not an alternative *consumer* |
| VERIFIED | A whole-loop interval is reported as `CannotStraddleTheWrap` (`pagedEmissionDeliveryStateForSlot`), documented as "the merged fallback keeps playing the slot exactly as it does today" |
| VERIFIED | `admittedForEmission()` does **not** consult the delivery state: it is `bound && admissible`, and `admissible` is set purely by the pass-straddle judgement. The reporting refusal and the selection predicate are **not the same question** |
| VERIFIED | Over-capacity content is produced by live capture append + seal (`prepareOverCapacityPagedSlot`), i.e. a committed pass — structurally the same body SD restore produces; "restored" is not a separate serving path |
| VERIFIED | Existing coverage: `test_3_7a_i3b_an_over_capacity_demand_is_prepared_as_pages`, `…_pages_walk_the_interval_exactly_once_across_pages`, `…_an_over_capacity_admission_switches_to_pages`, `test_3_7a_i3c_a_a_paged_pass_emits_through_the_walk_and_straddles_its_wrap`, `…_paged_emission_never_produces_and_names_its_states`, `test_3_7a_i7_the_paged_lane_refuses_pinned_note_edits_it_cannot_resolve` |
| ASSUMPTION | The merged fallback's whole-loop build is what serves the short dense shape in production; the measurement decides this |
| UNKNOWN | Whether a whole-loop dense interval can be served from pages at all without a re-arm budget (the ring is three pages; the content straddles the wrap by construction) |

## Named finding (hypothesis, not a defect)

> `pagedEmissionDeliveryStateForSlot()` and `admittedForEmission()` currently express different admissibility contracts;
> production consequence unmeasured pending combined dense-production-edge harness.

What is known: the reporting API can say `CannotStraddleTheWrap`; `admittedForEmission()` does not consult that report; and
the first probe could not reach a valid sealed production state. What is **not** known: whether the production edge actually
serves an invalid whole-loop dense delivery, whether another gate prevents it earlier, or whether the report is
advisory/diagnostic rather than normative. The selector is not patched on this evidence.

## Combined harness (built, measurement in progress)

RWO's dense builder (`prepareOverCapacityPagedSlot`, 2 500 sealed events) handed to the **production edge**
(`Track::playMidiEvents`), with the deferred lane serviced between edge turns. Four cases wired: sparse short-loop control,
dense 2-bar over `L`, dense 512-bar over `L`, and the wrap-sensitive report case. The harness was withdrawn byte-identically
after these readings (both suites back to green: RWO 230/230, preview 29/29):

| Drive | Control (200) | Dense 2-bar (2 500) | Dense 512-bar (2 500) | Report at start |
|---|---|---|---|---|
| edge turns only (no lane turn) | **200 = accepted** (pass) | **128 emitted** | **128 emitted** | `CannotStraddleTheWrap` (case D passed) |
| one lane turn per edge turn, deadline stamp advancing | pass | **0** | **0** | — |
| one lane turn per edge turn, deadline stamp held | pass | **0** | **0** | — |

**What the numbers say, and do not say.** 128 is exactly `kWindowPageCapacity`: the production edge *did* select the paged
delivery and emitted precisely one staged page, then had nothing further because the lane was not serviced. Once a lane turn
is interleaved, emission drops to **0** — the delivery is discarded or de-proven by that turn in this state, and the edge
then emits nothing at all rather than a named refusal. That is a *reading about the harness state*, not yet a production
verdict: `driveGatherUntilPagedSwitch` leaves a demand that the real scheduler would not leave, and the existing I3c fixtures
step the lane on its own cadence and drive the walk through a frame helper rather than `playMidiEvents`. So the harness is
close but the demand state is not yet production-faithful, which is the next increment's work.

**Not claimed:** no completeness/order/wrap reading for any dense case, no statement that the edge serves an invalid
delivery, and no defect. The four-case oracle (selected source, emitted count, element-wise order, wrap continuity,
duplication/loss, merged usage) is defined and unwritten.

## Production-faithful harness — measured RED (2026-10-21)

The faithful sequence is now reproduced: RWO's sealed dense builder → the demand exactly as the gather/switch leaves it →
**one bounded owner turn per edge turn on the demand's own stamp** (`nativePlaybackDeadlineClock`, the I3c discipline) →
`Track::playMidiEvents` consumes what the ring has staged. No `drivePagedEmissionFrame` is used for the behaviour under
test. The two earlier zeros were **my harness**, not firmware: a synthetic clock stamp (`{1,1}` against a demand registered
at `{1000,1}`) invalidated the frozen demand and discarded the ring (`pagedActive=0`, `bound=0`,
`frozenViewRequest.active=0`). Holding the demand's own stamp keeps it (`111`).

One whole traversal of each shape, walk `0…1536` (`+128` past the wrap in the order case), production edge each turn:

| Case | Report at start | Delivery state after | Emitted | Oracle | Order |
|---|---|---|---|---|---|
| dense **512-bar** (over-`L` interval in the first two bars) | `NoDelivery` until the switch | bound, admissible | **2 500** | 2 500 | complete |
| dense **2-bar** (interval *is* the loop) | **`CannotStraddleTheWrap`** (2) | `pagedActive=1`, `bound=1`, `admissible=1` | **2 437** | 2 500 | first 2 437 element-wise **exact** |

**The discrimination the harness was built for:** report = `CannotStraddleTheWrap`, production **selects** the paged delivery
anyway (`admittedForEmission()` true, `pagedActive`/`bound` true), and the stream is **truncated by exactly 63 events at the
tail** — with no reordering, no duplication and no mid-stream loss (the element-wise comparison matched for all 2 437
emitted events, and the packed mismatch reading was `631` = 63 missing, emitted exhausted before the oracle). Walking 128
ticks past the wrap does not recover them, so this is not a final-step artifact.

That is the first of the four outcomes: **the report and the selector disagree and the selector's answer loses content.**
Promoted from hypothesis to a measured RED; the fix is **not** applied in this slice.

**Also recorded (harness shape, not firmware):** a *sparse* control inside `prepareOverCapacityPagedSlot`'s rootless
demoted shape emits 0 (`test_dense_edge_01_control`), because that shape exists to force the paged lane and has no
published root to fall back to. The valid sparse control remains the preview suite's `commitEmissionLoop` +
`readyEmissionPlaying` fixture, which emits its accepted stream in full (200 = 200, measured in the first harness run). The
faithful dense harness therefore needs its control from that shape, not from the dense fixture.

## As executed (2026-10-21) — G0 closed, measurement **not** obtained

**G0 verdict: PASS for discovery, BLOCKED for slice 2's shape.** The discovery found the two facts that matter and no
authority conflict:

1. **The paged ring is a second delivery, not a second consumer.** It feeds the one `advancePlaybackCursor` walk, and
   `consumePagedPlaybackWindowForSlot` has no production caller at all (tests only). So "never an alternative clock-edge
   stream" is about the ring's **own** pass model, admission boolean and producer — which is exactly the machinery
   `PagedEmissionState` carries.
2. **The reporting refusal and the selection predicate are different questions.** `pagedEmissionDeliveryStateForSlot`
   names `CannotStraddleTheWrap` for a whole-loop interval (its doc says "the merged fallback keeps playing the slot
   exactly as it does today"), while `admittedForEmission()` is only `bound && admissible`, i.e. it never consults that
   state. A whole-loop dense delivery is therefore not refused by the selector — it is merely never proven.

**Measurement attempt (temporary probe, withdrawn byte-identically — `git diff` empty, focused suite back to 29/29):**
the probe tried to build the short-dense and long-dense shapes in `test_windowed_preview_integration` and walk the
production edge. It never produced a reading, and the reason is a harness fact worth recording rather than hiding:
`commitEmissionLoop` → `Loop::sealCapture` returned **`SealOutcome::FailedValidation`** (chunk-transfer preparation) for
every probe shape, including the sparse control, and for slot 0 and fresh slots 5/6/7, whether the probe ran first or
last in the suite. The preview suite's harness therefore cannot seal a standalone dense pass in a probe case; the suite's
own fixtures seal fine, so this is a probe-context limitation, not a content limitation.

**What that leaves:** the over-capacity path is already built and driven in
`test_runtime_working_set_ownership` (`prepareOverCapacityPagedSlot` seals 2 500 events successfully — its harness is the
one that can), but that suite drives the walk with a **test-side** `drivePagedEmissionFrame`, not the production edge.
The missing fixture is therefore: RWO's dense-content builder **plus** the preview suite's production-edge driver, in one
harness. That harness is slice 2's first deliverable, and no production line should change before it exists.

**Not claimed:** no emitted count, no completeness/order reading, no wrap reading for the short shape, and no statement
about which route serves a short dense loop in production. The G0 code readings above are the only claims.
