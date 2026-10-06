# Execution packet — S4e.9 / DEC-115: the display-note projection family refuses instead of parking

Status: **native verified; device leg owed.** One independently reviewable behaviour change on the
display/note-edit projection path, behind the existing point-of-use admission gate.

## Identity and scope

- **Task / decision:** `runtime-working-set-ownership` **S4e.9** (allocation-failure containment for the
  display/note projection family), under **DEC-115**. Follows **DEC-065** + its 2026-09-17 amendment
  (point-of-use probe is the authority; a gate that denies a succeeding reserve violates clause 2) and
  **DEC-075**'s scope boundary for the shared allocator.
- **Owner:** `MemoryMonitor::admitDisplayProjectionReserve(bytes)` over
  `MemoryMonitor::admitOperationMemory(OperationKind::DisplayProjectionReserve, bytes)`, plus the countable
  refusal (`displayProjectionRefusalCount()`). Production entry points: `NoteUtils::reconstructDisplayNotes`
  (through `dedupeProjectedDisplayNoteVec`), `NoteUtils::CachedNoteList::getNotes`,
  `DisplayWindowUtils::filterDisplayNotesToWindow{,ByWindowInclusion}`,
  `Loop::rebuildVisualCacheIdleSlice`. **The allocator is not touched**: `ExternalMemoryFirstAllocator::allocate`
  keeps its `abort()`; containment comes from the caller, because the device has no exception channel (no
  target object carries `.gcc_except_table`, and the tree's only `catch (std::bad_alloc)` is
  `#if !defined(ARDUINO)`, `src/Loop/LoopSnapshot.cpp:272`).
- **Primary files:** `include/Utils/MemoryMonitor.h`, `src/Utils/MemoryMonitor.cpp`,
  `src/Utils/NoteUtils.cpp`, `src/Utils/DisplayWindowUtils.cpp`, `src/Loop/LoopVisualCache.cpp`,
  `test/test_display_window_utils/test_display_window_utils.cpp`,
  `test/test_support/ProjectionGateWeakStub.cpp`, `scripts/native_test_link.py`.
- **Prerequisites already present:** `MemoryMonitor::admitOperationMemory` with its point-of-use kind
  (`CompanionHistoryReserve`) and `probeBlockAvailable` (takes the exact block with the raw entry points and
  gives it back, so a failed probe is a `nullptr`, never process death); the native forced-denial seam
  (`setNativeForcedReserveDenied`).
- **Explicit non-goals:** the shared allocator's failure semantics; the retention question (the measured
  **58 % untagged residency** and leak-versus-retention stay open); the other content-proportional reserves in
  the projection family (`LoopContentResolution`, `LoopMaterialization`, `LoopPendingNoteChange`,
  `EditManager/*`, `IntervalProjection`) — named as follow-ups, see Completion; the copy-elimination half
  (**S4e.8**); any `ExecutionBounds.md` row (this gate runs on display/idle-slice paths, not the MIDI edge).
- **Commit boundary:** firmware + fixtures + the native weak stub + this packet; independently buildable, no
  temporary compatibility shim.

## Oracle fixed before behaviour edits

| Case | Initial state | Action | Expected observation |
|---|---|---|---|
| **O1 admit** | healthy pool, forced denial off | `reconstructDisplayNotes(events, 1536, false)` | non-empty; `displayProjectionRefusalCount()` unchanged |
| **O2 refuse** | forced denial on | same call | **empty** result; refusal count **+1**; nothing allocated |
| **O3 retry** | denial cleared | same call | non-empty, same size as O1 (the refusal was not sticky) |
| **O4 cache keeps previous** | cache holds projection A; denial on | `cache.getNotes(eventsB, len)` | returns **A's content**; refusal count **+1**; cache key not advanced |
| **O5 cache retries** | denial cleared | `cache.getNotes(eventsB, len)` | returns B's projection (different size from A) |
| **O6 filter refuses** | hostile denial on | `filterDisplayNotesToWindow(notes, 0, 1536, 1536)` | **empty** result; refusal count **+1** |

Counterexample that must fail: with the probe removed, O2/O4/O6 observe a *successful* projection under
forced denial (the reserve proceeds) — pinned by the red proofs below.

## Controls and coverage

- **Seams reused:** the existing `setNativeForcedReserveDenied` (host) and the production
  `admitOperationMemory` policy; no new test-only seam beyond a **weak** native default for the gate symbol
  (`test/test_support/ProjectionGateWeakStub.cpp`, added to every native suite by `scripts/native_test_link.py`)
  so that a suite compiling a projection TU without `MemoryMonitorNativeDeps.cpp` still links — the real gate
  wins wherever it is linked, and no suite needs per-file wiring.
- **Production path exercised:** the real `reconstructDisplayNotes` chain (including the two `reserve` calls
  and the dedupe), the real `CachedNoteList` adoption logic and the real window filter. No copied state machine.
- **Failure/recovery case:** refusal → previous content preserved → retry succeeds (O4/O5).
- **Numeric limits:** none introduced; the gate's exactness is the contract (DEC-065's amendment), and the
  point-of-use byte count comes from `count * sizeof(element)` at the call site (24,576 × 16 B in the measured
  park).

## Verification recipe

```
pio test -e native -f test_display_window_utils     # 126/126 (3 new)
pio test -e native                                  # 2,839 / 7 skipped / 2,832 succeeded / 0 failed
pio run -e teensy41-capture-serial                  # RAM1: variables 96,192 / code 392,248 + padding 968
python3 scripts/check_marker_contract.py 'DIAG,my_event,'   # not applicable: no new serial marker
git diff --check
```
Red proofs: remove the probe / the adoption guard, re-run the focused suite, expect the named fixture to fail,
restore byte-identically (sha256). Device claim owed: a long/dense leg that no longer parks (a stimulus whose
content exceeds the pool on the projection path), read from `#MEM_DENIED,op=display_projection,…` plus
`[Memory] ext` rows; **not run in this slice**.

## Completion record

| Check | Command / context | Result |
|---|---|---|
| Fixture implemented, discriminating | `pio test -e native -f test_display_window_utils` | **126/126 PASSED** (was 123) |
| Red proof **RP-A2** (both dedupe probes removed) | focused suite | **RED** — *2 failed, 124 succeeded* |
| Red proof **RP-B** (cache adoption guard removed) | focused suite | **RED** — *1 failed, 125 succeeded* |
| Red proof **RP-C** (window-filter probe removed) | focused suite | **RED** — *1 failed, 125 succeeded* |
| **RP-A1** (only the `ranked` probe removed) | focused suite | **NOT RED** — recorded as a **non-red-proof by redundancy**: the sibling `finalNotes` probe still refuses, so the fixture cannot discriminate that one call site alone |
| Restores | sha256 compare after each mutation | byte-identical (both files) |
| Native verification | `pio test -e native` | **2,839 / 7 skipped / 2,832 succeeded / 0 failed**; no suite ERRORED/FAILED |
| Build + RAM1 | `pio run -e teensy41-capture-serial` | **SUCCESS**; `variables 96,192` unchanged, `locals 34,880` unchanged, `code 392,248 + padding 968` (baseline `392,360 + 856` — **112 B more headroom**, no re-base) |
| Device verification | — | **NOT RUN** (owed) |
| Durable docs | DEC-115; S4e.9; `CURRENT_WORK`/`PROJECT_STATE` | written with this slice |
