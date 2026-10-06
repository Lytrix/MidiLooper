# Evidence — S4e.9 / DEC-115: the display-note projection family refuses instead of parking

**Date:** 2026-10-06. **Baseline:** `0459e12c` (the pool-attribution docs commit; `git status` clean before this
slice). Packet: [`EXECUTION.md`](EXECUTION.md).

## What this slice changes

1. `MemoryMonitor::admitOperationMemory` gains `OperationKind::DisplayProjectionReserve` as a **second
   point-of-use kind** (its name is emitted through a leading `if` + `PSTR`, deliberately *not* a new `switch`
   case: extending that switch would grow a jump table in `.rodata`, which is DTCM in this linker script and is
   booked by the RAM1 guard as `variables`).
2. `MemoryMonitor::admitDisplayProjectionReserve(bytes)` — the family's admission. Device: one FLASH-resident
   out-of-line definition with its counter in `DMAMEM`; native: header-inline, so a suite that compiles a
   projection translation unit needs no per-suite wiring (a **weak** stub, added to every native suite by
   `scripts/native_test_link.py`, keeps suites that do not link the gate from failing to link; the real gate
   wins wherever it is linked).
3. The gated sites, each with its **exact** byte count immediately before the reserve it is about to make:
   `dedupeProjectedDisplayNoteVec` (both copies), `CachedNoteList::getNotes` (both overloads, via the countable
   refusal), `DisplayWindowUtils::filterDisplayNotesToWindow`,
   `DisplayWindowUtils::filterDisplayNotesByWindowInclusion`, `Loop::rebuildVisualCacheIdleSlice`.
4. **The allocator is untouched.** `ExternalMemoryFirstAllocator::allocate` keeps its `abort()`; containment
   comes from the owner that asked. Verified reason: the device has no exception channel — **0 of all target
   objects carry `.gcc_except_table`/`.eh_frame`**, and the tree's only `catch (const std::bad_alloc&)`
   (`src/Loop/LoopSnapshot.cpp:272`) sits inside `#if !defined(ARDUINO)`. Returning `nullptr` instead would be
   worse: `std::vector` would hold a non-zero capacity with a null `data()` and write into it.

## Failure contract (the part this slice exists to state)

| Owner class | On refusal |
|---|---|
| Derived display/visual caches (`CachedNoteList::getNotes`, `rebuildVisualCacheIdleSlice`) | keep the **previously served** content, do **not** advance the cache key / clear `dirtyBars`, re-attempt on the next call or idle slice |
| Pure window filters (`filterDisplayNotesToWindow{,ByWindowInclusion}`) | return an **explicitly refused** empty frame result, re-attempt on the next projection |
| Every path | allocates nothing, blocks on nothing, drops no edit (the projection is derived; the pending change set is untouched) |

## Verification (this slice, run in this session)

| Check | Command | Result |
|---|---|---|
| Focused suite | `pio test -e native -f test_display_window_utils` | **126/126 PASSED** (three new fixtures; was 123) |
| Full native | `pio test -e native` | **2,839 / 7 skipped / 2,832 succeeded / 0 failed**; no suite ERRORED or FAILED (baseline was 2,836 cases) |
| Build + RAM1 guard | `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` unchanged, `locals 34,880` unchanged, `code 392,248 + padding 968`; baseline `392,360 + padding 856` ⇒ **112 B more ITCM headroom**, no re-base |
| Whitespace | `git diff --check` | clean |

### Red proofs (each restored with a sha256-verified copy; `git checkout` was **not** used — see the incident note)

| ID | Mutation | Result |
|---|---|---|
| **RP-A2** | both `dedupeProjectedDisplayNoteVec` probes removed | **RED** — *2 failed, 124 succeeded* |
| **RP-B** | the cache adoption guard removed | **RED** — *1 failed, 125 succeeded* |
| **RP-C** | the window-filter probe removed | **RED** — *1 failed, 125 succeeded* |
| **RP-A1** | only the `ranked` probe removed | **NOT RED**, recorded as a **non-red-proof by redundancy**: the sibling `finalNotes` probe still refuses, so no fixture can discriminate that call site alone |

**Incident recorded, not glossed over.** The first red-proof harness restored mutations with
`git checkout -- <file>`, which reverted the **uncommitted** slice edits in `src/Utils/NoteUtils.cpp` and
`src/Utils/DisplayWindowUtils.cpp` to `HEAD`. Both files were re-applied from the recorded edit text and the
whole slice re-verified (the numbers above are from the final tree); the harness now saves and restores a
byte-checked copy instead, and the final run confirms both files' sha256 are unchanged after the mutations.

## What is *not* claimed

- **No device leg.** The claim is native + build only: a long/dense leg that no longer parks, read from
  `#MEM_DENIED,op=display_projection,…` plus the `[Memory] ext` rows, is owed.
- **Not the whole family.** The other content-proportional reserves in the projection family
  (`LoopContentResolution`, `LoopMaterialization`, `LoopPendingNoteChange`, `EditManager/*`,
  `IntervalProjection`) are **not gated** here. The measured park's chain is covered; the remainder are
  follow-ups, and what this slice adds for them is attribution: the refusal names the operation, the exact
  size, and (via `caller=`) the requesting site.
- **No retention/leak answer.** The measured 58 % untagged residency and the leak-versus-retention question stay
  open (see the pool-attribution finding).
- **No allocator semantics change**, no `ExecutionBounds.md` row change (this gate runs on the display and
  idle-slice paths, not on the MIDI edge), no timing claim.
- **No new serial marker** (the refusal rides the existing `#MEM_DENIED` shape), so no Tier-A/marker-contract
  change is implied.
