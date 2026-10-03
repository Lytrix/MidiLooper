# Slice 3.10 (increment 18) — review step 1: the work of each request is bounded and retained

**Status:** 2026-10-03. The first half of the reviewer's ordered plan (*"bound the work of each request"*), landed with
native fixtures. Two defects found by the fixtures themselves are recorded below, one of them in the cost model.

## What was missing

`applyWindowCostToContract` had existed since increment 8 as the single source of truth for a window's cost, but it had
**no caller**: the admission still judged the lead with the *declared* counts (3 fill + 3 order + 1 promotion grains),
and nothing bounded the *retained* window at all. Nor was the cost of an admitted request **kept anywhere**, so no
later reader could ask what work a slot had already been promised.

## What landed

1. **The cost helper is now the contract's own method** — `CoverageServiceContract::deriveGrainsFrom(windowEventCount)`,
   declared in `RuntimePlaybackViewBuild.h` and defined where the quanta live (`TrackPlaybackWindowBuild.cpp`). One
   definition, three readers: the admission, the retained record and the fixtures. It **sets only the three grain
   fields** and leaves the advance, the denial allowance and the lead alone, so *deriving cost is not the same act as
   changing what may be admitted*.
2. **The retained-event bound is enforced**: a gather larger than `RuntimePlaybackViewConfig::kWindowAdmissionLimit`
   (which *is* `kEventsPerView`, `static_assert`ed as the admission limit, DEC-108 §14) is refused by a **new named
   outcome**, `PlaybackViewRequestResult::WindowExceedsRetainedCapacity` — **refused, never truncated**, because
   accepted content is never silently dropped.
3. **The bounded work is retained on the runtime**: `PlaybackViewAdmittedWork { sourceEntries, retainedEventBound,
   fillGrains, orderGrains, promotionGrains }`, written where the build begins and read through
   `activeViewBuildWork()`, which **returns null unless the build it describes is active** — a finished build owes
   nothing and its record must not be read as outstanding work. This is the record step 2's per-track budget will need.
4. **No capacity change.** The lead judgement still uses the declared counts, so no shape admitted before this
   increment is refused by it. Enabling the *lead-side* derivation is the reviewer's step 4, and it needs a measured
   `Delta-max` and the `H × K` interference bound first.

## Two defects the fixtures found (both mine, both recorded rather than smoothed)

- **The cost model under-cut an empty window by one fill grain.** `ceil(0 / kPlaybackViewFillQuantum)` is 0, but a
  window is filled by that grain *even when it turns out to hold nothing*: measured, an empty window spends **one fill
  grain** and then its promotion. `deriveGrainsFrom` now floors the fill count at one, so the bound no longer
  under-cuts the very work it exists to bound. The doc comment that had claimed "an empty source derives no fill work
  at all" is corrected at the source.
- **An empty gather is *admitted*, not refused.** My first version of the fixture asserted a refusal; the run showed
  the empty interval is a legitimate (Ready) window that still owes its promotion, so the fixture now asserts admission
  and the emptiness's own cost (1 fill + 0 order + 1 promotion).
- **A fixture-coupling defect of my own making:** the first draft drove the **lane turn**
  (`stepPlaybackViewMaintenance`), which samples the **process-wide** service-gap series at its top, so it perturbed the
  gap fixture later in the same binary. The fixture now drives the runtime's own grain primitives with the turn's own
  quanta — the same work, no lane-turn side effect. (The gap fixture went from failing to passing on that change alone.)

## Fixtures (native, test-only)

- `test_window_request_work_is_bounded_and_retained_across_source_shapes` — **empty, sparse, dense and filtered**
  sources. For each: the record names the inspected entries and the retained bound; the grain counts equal the
  contract's own derivation (not the declaration); the build finishes; and **actual fill/order/promotion grains never
  exceed the calculated cost**; and the record is unreadable once the build is finished.
- `test_window_request_beyond_the_retained_capacity_is_refused_by_name_and_truncates_nothing` — a gather beyond the
  capacity is refused **by name**, owes nothing, and retains nothing.
- **Bound of these fixtures, stated:** they prove the record bounds the *observed* work for four shapes on one loop
  length; the systematic shape sweep (and a real `Delta-max`) is still owed.

## Red-proofs

Each mutation applied by a guarded replace (unique anchor asserted, abort otherwise), restored from a saved copy and
verified **byte-identical**: `TrackPlaybackWindowBuild.cpp` `4a6a48df…`, `TrackPlaybackRuntime.h` `ed39edf7…`.

| mutation | change | the assertion that spoke |
|---|---|---|
| **M108** | capacity bound dropped | *"Expected 10 Was 0. a gather beyond the retained capacity is refused by name, not truncated"* |
| **M109** | record filled from the **declared** counts | *"Expected 0 Was 3. an empty window costs no order grain"* |
| **M110** | record not gated on the build | *"a finished build retains no readable work record"* **and** *"nothing is retained for a refused request"* |
| **M111** | empty-window fill floor removed | *"Expected 1 Was 0. an empty window still costs its one fill grain"* — the defect above, now pinned |

## Verification

- Focused `test_runtime_working_set_ownership` **79/79** (+2 fixtures).
- Full native **2718 cases / 2711 succeeded / 7 skipped / 0 failed**.
- `pio run -e teensy41-capture-serial` **SUCCESS**; RAM1 line **byte-identical** (`variables 96,192 / code 390,088 +
  padding 3,128`, 2 288 B headroom, floor 840) ⇒ **no RAM1 re-base**.
- **One recorded size re-base**, measured not assumed: `sizeof(LoopPlaybackRuntime)` **6 624 → 6 640** (+16 B) on the
  target, where the new record is one `extern`-storage per-slot field (no RAM1 `variables`, no DTCM). The **host** pin is
  **unchanged at 6 544** — the host layout absorbed the same field, and its unmodified `static_assert` is the evidence.
  The figure was measured with the temporary compile-time probe that the file's own comment describes.

## What this does *not* do

Step 2 (the per-track outstanding-work budget, and admission that keeps **every** already-admitted window's deadline
feasible) is **not** implemented here. Step 3 (the lane dispatch boundary, and bounding or repairing what prevents
dispatch) is a separate investigation, and its result is what `Delta-max` must come from — the largest observed gap is
evidence, not a guaranteed maximum. The derived admission stays **disabled**.
