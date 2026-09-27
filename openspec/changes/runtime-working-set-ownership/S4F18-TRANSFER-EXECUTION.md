# S4f.18 — bounded prepared-pass chunk transfer

Status: bounded transfer native/build verified; Record worker integration remains open. Baseline b9740ddc; unrelated dirty HITL/doc files preserved.
Authority: shared-finalization contract (bounded pass preparation), DEC-093 —
shared capture finalization; DEC-064 — immutable exclusive raw capture ranges.

## G0 / G1 / decision review

VERIFIED: `Loop::stagePreparedCapturePassWithChunkStorage` hands prepared canonical
output to `LoopEventStore::detachChunksToCommittedChunkIds`. Its prewarmed path
still seals, copies directory entries and walks references over the whole take.
The legacy API is synchronous; a long Record cannot call it as one deferred grain.
Existing store/chunk ownership, prewarmed committed directories and explicit ref
release remain the owners. Canonical output is distinct from immutable raw input;
this operation must never mutate a `CaptureRange`.

Extend LoopEventStore's existing directory transition with a caller-owned cursor.
Begin is scalar preflight: empty destination with capacity for the complete chunk
directory, exclusive prepared source, stable owner-generation receipt. Each step
moves at most Q tail chunk references, seals only those chunks, or swaps at most
Q pairs to restore original directory order. No event copies, allocation, vector
front erase, or full-directory walk inside a step. Moving a reference neither
increments nor decrements the pool reference count. On partial completion the
source owns its remaining prefix and destination owns the moved suffix (initially
reversed). Neither destination nor candidate pass is publishable before Complete.
Partial cancellation uses existing bounded source/destination release APIs.

Cursor binds source address/ownership generation, pool lifetime, destination
address/data/extent and an external owner-generation reference. Shape or identity
changes deny before more work. Preserve content and owned references on denial;
caller owns both containers and the generation through completion/cancellation.
Direct mutation of borrowed containers without changing that generation is
forbidden. No new Track/job/runtime field or allocation; cursor layout is measured.
The existing fully prewarmed synchronous wrapper drains this operation for compatibility;
its callers remain synchronous until the Record worker integration is complete.
The cold allocation fallback remains unchanged. No normalized-row or publication
policy change and no new storage representation/owner; G0 PASS, extension preferred.

## G2 oracle before code

- More than 2,048 rows: one transfer/reversal operation per quantum=1; exact
  original directory/content at completion, no pool growth or refcount churn.
- Empty source and reserved empty tail: complete without fabricated content.
- No capacity/nonempty destination/zero quantum: no mutation; provision correct
  capacity and retry. Never steal another destination's existing references.
- Wrong store, changed external generation, cleared/reused source, changed
  destination extent and pool reset: fail closed before another ownership move.
- Partial cancellation: release both remaining source and moved destination refs
  exactly once; another live producer remains intact.
- Repeated completion is idempotent. Existing synchronous wrapper preserves the
  real normalization fixtures, including 2,056-row and filtered-empty cases.

G2 PASS WITH TIGHTENING: do not expose reversed partial output as a pass, do not
use a whole-take reverse in completion, do not reallocate the directory in a step,
and do not return Complete before all moved entries regain original order. Pool
reset invalidates the receipt; stale destination ids must be discarded without
releasing new pool occupants. No fault path silently discards accepted rows.
Other transport/undo/save/playback gates remain unchanged because worker dispatch
is not added here. Device deadlines and full normalization remain open.

## Verification recipe

Focused capture-range plus shared-finalization suites; mutation of reversal,
owner/pool receipt checks as applicable; full native; capture-serial build with
RAM1 padding >=840; strict OpenSpec/whitespace validation. Record layout sizes.
Registered integration stimulus unchanged; no upload or device acceptance here.

## Review tightening and measured evidence

`PersistenceQueue::admitSealedChunk` uses lazy `queueState()`, so a destination
reservation alone cannot prove allocation-free sealing. Nonempty bounded sources
must pass `reservedAppendReady()`: source directory/pool lifetime established by
`prewarmCaptureDirectory`/`reserveCaptureTail`, which also prewarms persistence.
Legacy stores lacking that admission retain their old synchronous path. A fixture
proves that cold source + reserved destination denies until source prewarming.

Every move and reversal advances the source transaction ownership generation.
A copied old cursor cannot replay a reversal and undo ordering. Source replacement
uses the same extent (512 rows/two chunks) to isolate generation from shape checks.
External owner generation, source identity, pool lifetime and destination storage
remain separate guards. Destination lifetime is caller-owned; resizing/replacing
it without updating its generation is outside the contract and observable data/
extent changes still deny before further mutation.

- Focused capture-range + shared-finalization suites: **40/40** (11 + 29).
- Five independent mutations fail intended assertions: missing reversal,
  missing source ownership check, pool lifetime check, external generation check,
  and persistence/source prewarm check. All restored before final broad checks.
- ARM compiler object-size probe: cursor **64 bytes**; host compiler assertion:
  **96 bytes**. Cursor is local to the synchronous wrapper / caller-owned for
  resumable use, with no new field in Track, Loop, job or playback runtime.
- No whole-take event buffer, chunk allocation or refcount churn: the fixture
  checks the 2,056-row transfer grain by grain, exact final IDs/order/content,
  stable destination pointer/capacity/pool occupancy and independent successor.
- Cancellation owns two disjoint directories; existing bounded release methods
  free each reference once. The cursor itself owns no chunk reference.

Capture-serial build PASS (89.377 s): FLASH code 1,043,728; RAM1 variables
96,160 / code 392,376 / padding 840 / local headroom 34,912; RAM2 variables
155,232; static EXTRAM 9,312. All existing resident layout pins pass unchanged.
No upload. Stimulus inventory was listed; STIM-BASE-2-ONE-OD-01@v1 remains the
registered integration stimulus, unmodified. This build proves placement, not
hardware timing or completion of the connected deferred Record path.

## Final verification / closeout

Full restored native regression: **2,525 passed / 7 skipped**, 2,532 total,
20m07.251s. Focused transfer/shared-finalization suites also pass in that run.
Strict OpenSpec validation and whitespace checks pass. Runtime state/current work,
execution bounds and task progress updated. No parent task checked off, no shipped
deliverable, no new architectural decision or exclusion. The bounded helper and
its synchronous reuse implement the existing store/pass ownership transition;
Record normalization, publication/undo and worker/manager integration remain open.
