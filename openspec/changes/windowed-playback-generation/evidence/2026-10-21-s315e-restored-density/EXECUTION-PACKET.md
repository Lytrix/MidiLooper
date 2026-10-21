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
