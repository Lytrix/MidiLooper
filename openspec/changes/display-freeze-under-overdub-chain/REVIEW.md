# Focused contract red-team review

Planning-only review, 2026-10-28. No firmware/native/device verification claimed.

## TIGHTENING — production dispatch must be exercised

Evidence: the display harness calls `DisplayManager::update` directly through `serviceDisplay`, and the throughput harness calls Track maintenance directly. Either can pass while main-loop admission starves the other. Required: reuse production admission/owner selection in the combined fixture; if a minimal seam is needed, expose that boundary, not a copied scheduler. Design packet A and task 1.2 require this. A passing direct-call control is not a regression reproduction.

## TIGHTENING — phase progress is not frame completion

Evidence: `submitFrame` uses three admitted updates; DMA completion is separately requested. Required: distinguish all three phases and physical completion; with a healthy transport, three admitted updates must submit, then controlled completion must retire the exact submitted generation. Busy transfer controls retain latest pending work. This is logical ordering, not a hardware millisecond promise.

## TIGHTENING — source/image provenance is not established by comments

Evidence: current source contains a later reduced-burst variant; the capture predates this variant. Required: same snapshot and exact A/B mutation, no wholesale source revert. Both design and investigation now record this limitation.

## COVERED — identity and asynchronous lifetime

Alternate load/display child paths, asynchronous DMA completion and pending stop source continuation are named. Focus/slot replacement, stale root, Record→Stop→Record, overdub→Stop→overdub, transport Stop, undo/redo and clear/geometry changes remain existing qualification controls. No source reference survives a new lifetime or ownership transfer in this plan. A failure requiring such a change returns to design rather than becoming an opportunistic repair task.

## COVERED — storage and resources

Load/save and persistence-in-flight exclusion remains StorageManager-owned. No device SD operation or content run is authorized by planning. Long loops and background-track contention are fixture cases; maximum active working-set acceptance is not claimed. No new content-sized production state, second activation authority, full stop validation or unbounded synchronous gather is authorized. RAM1/PSRAM observation costs must be priced if target instrumentation is needed.

## COVERED — timing and compatibility limits

No burst on the Record lane, no clock-source behavior change and no new MIDI guarantee. Internal/external clock transitions and physical jitter remain hardware controls if a scheduling mutation is proposed. Fallback/source qualification remains unchanged. Stop and display oracles are independent, so restoring one by breaking the other cannot pass.

## G2 REVIEW: PASS WITH TIGHTENING for packet A — regression fixture

Tightenings are incorporated in the design/tasks. Packet B — production repair remains BLOCKED until a named failing predicate and minimal mutation are recorded. This is a self-review; the independent lifecycle review required for a subsequent non-trivial firmware repair is still owed.
