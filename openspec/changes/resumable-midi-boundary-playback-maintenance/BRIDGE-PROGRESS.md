# Bridge implementation progress

2026-09-13. Tasks 2.3–2.5 remain open; task 3.5 is complete.

## Accepted undo lifetime implementation (DEC-063)

2026-09-14: User accepted the recommended protected transaction and latest pending
Undo/Redo coalescing rule. The reassessment, decision log, design and normative
MIDI-boundary delta now record that approval before implementation.

`LoopSnapshot::ReleaseCursor` owns a retired immutable root and visits child
ownership slots incrementally. Shared nodes are detached without mutation;
exclusive nodes drain child references before destruction. Directory vectors pop
one emptied page at a time. Membership tails move sideways after their 16 slots
are empty, avoiding recursive chain destruction. The fixed walk depth is five;
node destruction visits at most 128 already-empty fixed-array slots. Mutable
ownership fields exist solely for exclusive retirement, including objects
originally constructed const. No semantic reader or weak resurrection may acquire
the retired root between grains; the enclosing transaction must enforce this.
No weak-pointer acquisition exists in the production snapshot owners audited here.

`PersistedLoopSnapshotReleaseCursor` drains one owned chunk, payload event, empty
pass row or geometry row per unit. Empty backing arrays are then freed before the
root. Shared DTOs remain untouched. `LoopEventStore::releaseLastCommittedChunkRef`
returns one owned reference through the existing pool and persistence lifecycle.
Use owned/deep-cloned chunk lists: `tryCopyCommittedChunkIds` alone is not a retain
operation. A focused fixture was corrected to use independent deep-cloned storage.

`UndoEntryReleaseCursor` drains both persisted and both published snapshots and
ID vectors. `GlobalUndoStackTrimCursor` applies the existing depth/pressure rule
to an exclusively owned, unpublished stack, finishes each entry's release, shifts
one surviving entry per unit, then rechecks actual pressure. It must start beyond
the enclosing transaction's no-fail point; it has no rollback or cancellation API.
It does not reclaim canonical passes or publish stack state.

Focused native: snapshot/event-store **37/37**
(`/tmp/resumable-snapshot-release-focused.log`), storage/retirement **43/43**
(`/tmp/resumable-undo-retirement-focused.log`). Coverage includes Q=1/7/32, shared
roots and descendants remaining readable after every grain, final leaf release,
owned chunk return, busy/zero-quantum denial, shared before/after roots relieving
pressure, minimum/absolute rails and undo/redo cursor survivor parity. Firmware
build passes (`/tmp/resumable-undo-retirement-build.log`): RAM1 code **425736**,
variables **91808**, locals **6496**; FLASH code **938832**. Full native regression
passes **2069 with 1 skipped** (`/tmp/resumable-undo-retirement-native.log`, 2:45).
Strict OpenSpec validation and `git diff --check` pass.

These helpers do not yet enable the protected production transaction. Still
required: complete retirement admission/credits, redo removal and canonical reclaim
sequencing, reader/mutator exclusion and no-fail proof, reserved snapshot-after
attachments, latest-command dispatch, joint stop publication and snapshot-box
refill. Public stack mutation remains on the compatibility path. Tasks remain
**11/19**; HITL is not the next gate yet.

## Global undo publication: reclamation review

2026-09-14: Native investigation confirms that retaining removed undo roots can
change pressure-feedback trimming. With 11 entries and pressure relieved by the
oldest root's destruction, immediate release retains 10 entries; retaining that
root retains 8. The actual trim routine passes the controlled counterexample
in `test_pool_budget` (**18/18**, `/tmp/resumable-undo-trim-reference-focused.log`).
No firmware behavior changed in this review.

[Undo publication reassessment](UNDO-PUBLICATION-REASSESSMENT.md) recommends
preserving feedback-based trimming through bounded reclamation and a protected
stop transaction. The user accepted this recommendation (DEC-063), including latest-request
Undo/Redo coalescing per affected Track. Implementation is authorized. No latched trim
reference, restricted capture subset or unbounded atomic release was adopted.
This decision precedes runnable HITL; tasks remain **11/19**.

## Prepare a private UndoEntry with a constant-size handoff

2026-09-14: TrackUndo::prepareOverdubSessionEntry delegates its implementation
to the Arduino-independent TrackPreparedUndo seam used by native tests. It
requires current completed collection, a valid slot, nonempty pass IDs, and an
unoccupied private UndoEntry with empty ID vectors and snapshot references.
Every denial precedes mutation, including occupied scalar single-pass entries.

Preparation sets the legacy OverdubPassAdded metadata and companion edit index,
retains the supplied before-published snapshot, and swaps prepared companion IDs
into the entry. Multiple pass IDs are swapped too; a single pass uses only the
legacy scalar passId field. Destination vector allocations remain in the consumed
collection outputs instead of being freed. Snapshot assignment retains into an
empty destination, so it releases no old root. The collection is explicitly
consumed even for a single pass with no companion IDs, preventing a second entry
from being prepared from the same completed continuation. No global entry ID,
stack cursor, Track session-before ownership or canonical content changes.

The body is content-independent after bounded ID collection: scalar metadata,
one snapshot retain, at most two vector swaps and logical collection retirement.
The caller still must select the existing session-before snapshot or published
fallback and hold the prepared entry privately until atomic publication. No
production stop dispatcher or global-stack push has been enabled. The native
combined-receipt fixture now transfers its complete lists into a private entry
before recycling the Loop receipt, retaining all copied IDs.

Focused **116/116** passes (`/tmp/resumable-undo-entry-focused.log`) before the
final combined-handoff fixture addition: empty/single/multiple pass behavior,
vector-buffer retention, duplicate companions, snapshot retain counts, no-mutation
denials and prevention of repeated preparation. Final full native, including the
combined handoff: **2061 passed, 1 skipped** (`/tmp/resumable-undo-entry-native.log`).
Capture-serial build passes (`/tmp/resumable-undo-entry-build.log`): RAM1 code
**425736**, variables **91808**, locals **6496**; FLASH code **936656**. Strict
OpenSpec and diff whitespace checks pass. Tasks remain **11/19**; no upload or HITL.

Global redo-branch retirement, memory trimming, entry-ID/cursor publication,
session-before root retirement and snapshot-after attachment still need their
bounded publication/cleanup path. TrackUndo remains the undo owner; no persistence
format or DEC-038 session semantics change.

## Combine session undo IDs with the final capture receipt

2026-09-14: beginCollectOverdubStopUndoPasses extends the existing bounded
SessionUndoCollection with FinalPass and FinalCompanions stages. It binds the
Loop-owned Ready/Committed companion receipt by preparation generation and
size, alongside the existing session-generation proof. Receipt canonical storage,
revision and next-ID identity must remain current. Each unit appends at most one
ID or advances one stage boundary, always within caller-reserved private output.

The final capture ID is appended only when includeFinalPass is true, the ID is
valid, and it differs from the last collected ID. An earlier duplicate is not
deduplicated. Final companion IDs are always appended in order, including when
includeFinalPass is false or the final capture ID is invalid/already last. This
matches TrackUndo::pushOverdubSessionOnStop. Collection does not consume the
receipt. Ready-to-Committed publication preserves it; recycling/discard/prewarm
invalidate paused and completed collection before any later receipt read.

The combined bound is ceil((2P+C+A+3)/Q), where P is session rows, C session
companions and A final receipt IDs, P <= 2048 and C+A <= 2048. Output pass count
also cannot exceed 2048, including an appended final pass. Capacity denial keeps
partial output private and never grows vectors. These collection grains remain
separate from ranked preparation and recycling credits; production capture-time
reservation and admission are still required.

Focused **227/227** passes across playback-prewarm and overdub-source-view suites
(`/tmp/resumable-final-undo-receipt-focused.log`) before the final canonical-identity
guard. Coverage compares the complete legacy append rule for invalid, same-last,
earlier-duplicate and new final IDs with inclusion enabled/disabled, verifies
Q=1 completion bounds and stable output allocations, and rejects missing,
recycled, replaced or undersized receipt/output state. Final full native, including
the canonical-identity guard: **2059 passed, 1 skipped**
(`/tmp/resumable-final-undo-receipt-native.log`). Final capture-serial build passes
(`/tmp/resumable-final-undo-receipt-build.log`): RAM1 code **425736**, variables
**91808**, locals **6496**; FLASH code **936280**. Strict OpenSpec and diff
whitespace checks pass. Tasks remain **11/19**; no firmware upload or HITL.

The resulting ID lists still need bounded UndoEntry preparation, global redo
retirement/memory trimming and atomic attachment in TrackUndo, followed by
production stage selection and mailbox refill. Existing ownership, persistence
format and one-global-entry-per-session semantics are unchanged.

## Collect session undo IDs in bounded private output

2026-09-14: Loop now extends its existing collectOverdubSessionUndoPasses with
begin/step/current-query entries and a caller-held SessionUndoCollection cursor.
The caller supplies empty, pre-reserved private ID lists and retains exclusive
access. Begin performs constant metadata checks; each step visits at most Q
pass IDs, companion IDs or row/completion boundaries. No active clearing,
allocation, global undo mutation or canonical pass mutation occurs. Capacity
exhaustion invalidates the partial private result rather than growing output.

The result preserves the legacy session cursor prefix, duplicate pass and
companion IDs, companion order, and invalid-pass skipping. Undone redo rows are
excluded. Session open/close, accepted pass append, redo-tail removal and cursor
undo/redo invalidate paused or completed collection through a generation marker.
The continuation checks the exact Loop and output sizes before source access;
wrong-owner and zero-Q calls do not cancel otherwise valid work. A consumer may
use results only after collectedOverdubSessionUndoPassesCurrent succeeds.

For P visited rows and C copied companions, the conservative collection bound is
ceil((2P+C+1)/Q), with P,C <= 2048. At the maximum horizon and Q=32 this is
**193 grains**. Each valid pass needs a companion-row boundary; invalid pass rows
need only the pass visit. This collection work is separate from the DEC-062 6240
ranked allowance and from receipt recycling. Companion capacity is checked as
IDs are copied; complete capture-time resource reservation still must precede
production use. No active collector caller has been enabled.

Focused **113/113** passes (`/tmp/resumable-session-undo-collect-focused.log`):
legacy differential parity at Q=1/7/32, unchanged output allocations, capacity
and alias denial, generation invalidation including close/reopen with identical
IDs, empty and maximum horizons, and stale completed-result denial. Full native
**2057 passed, 1 skipped** (`/tmp/resumable-session-undo-collect-native.log`). Final
capture-serial build passes (`/tmp/resumable-session-undo-collect-build.log`): RAM1
code **425736**, variables **91808**, locals **6496**; FLASH code **935664**.
Strict OpenSpec and diff whitespace checks pass. Tasks remain **11/19**; no upload
or HITL.

Next integration still needs the final capture/companion receipt combined with
these IDs, bounded UndoEntry preparation, global redo-branch retirement and
memory trimming, atomic attachment, scheduler credits and mailbox refill. The
existing Loop/TrackUndo owners and DEC-038 one-global-entry-per-session semantics
remain unchanged; this is a bounded replacement for the collection part only.

## Execute admitted history recycling on the committed owner

2026-09-14: TrackPreparedStopCommit now exposes separate beginHistoryRecycle
and stepHistoryRecycle entries. The successful joint commit binds the exact
Loop and its preparation generation. Prewarm, new preparation and discard advance
that generation, so an old stop cannot act on replacement preparation even when
canonical counters are unchanged. Reset clears the borrowed binding.

Begin explicitly acknowledges consumed undo/snapshot receipt IDs, charges one
setup turn, and ends receipt access. Execution requires that begin succeeded,
DeferredCleanup with finished preparation, the exact admitted Q, and matching
owner/generation. Each attempted Loop quantum consumes one recycling credit;
wrong owner/generation/Q/phase and exhausted reservations cannot advance work.
A Loop source change discovered during the attempted grain is charged and denied.
Completed queries are free but still require current canonical/mailbox identity.
Transport Stop retains cleanup capability and cannot cause another PLAYING commit.

The joint fixture uses these entries, verifies receipt lifetime, wrong owner/Q/
phase/generation and exhausted-credit denial, free completion, retained bridge
and canonical revision, and rejection after same-source STOPPED prewarm. Q=1
source-change fixtures exercise Pending and charged denial without canonical
mutation. Focused **112/112** passes (`/tmp/resumable-recycle-dispatch-focused.log`).
Capture-serial build passes (`/tmp/resumable-recycle-dispatch-build.log`): RAM1
code **425736**, variables **91808**, locals **6496**; FLASH code **934832**.
Full native **2054 passed, 1 skipped** (`/tmp/resumable-recycle-dispatch-native.log`).
Strict OpenSpec and diff whitespace checks pass.

The existing undo chain still gathers session IDs into growing vectors
(Loop::collectOverdubSessionUndoPasses), allocates and filters a
second pass-ID list (TrackUndo::pushOverdubPassAdded), then drops the redo branch,
pushes the global entry and trims for memory (pushUndoEntry). Its full traversal,
allocation and retirement costs must be separated; prebuilding only the companion
receipt does not bound this chain. It cannot run inside the bounded joint commit. Bounded
undo preparation/attachment, production stage selection, complete capture-time
reservations, surplus-credit retirement and active mailbox refill remain open.
No undo semantics or ownership change. Tasks remain **11/19**; no upload or HITL.

## Reserve post-commit history recycling separately

2026-09-14: PendingStopAdmission now declares history-recycling credits and Q
separately from closure, order, ranked and canonical preparation. The wide input
and the combined total are checked before narrowing or mutation; UINT16_MAX is
the counter limit, not a realtime duration policy. Legacy callers default to zero.
The prepared joint handoff requires a nonzero recycling reservation, retains it
across publication, and enters DeferredCleanup. Recycling credit cannot be consumed
until preparation has finished and that phase is selected. Commit readiness
deliberately excludes these post-commit credits, avoiding a circular dependency.
Transport Stop preserves them; reset clears them.

Native coverage checks overflow, phase/preparation gates, exhaustion, reset and
legacy opt-out. The joint fixture reserves setup plus the existing recycle bound,
consumes the ID receipt, then charges each recycle grain while verifying canonical
revision and selected bridge survive, including a transport Stop after commit.
This extends the existing stop admission owner without changing undo semantics.
Production receipt/undo sequencing, grain dispatch (including enforcing the
admitted Q), full horizon reservation at capture acceptance, surplus-credit
retirement and mailbox refill remain open. This reservation covers history
recycling only; other deferred cleanup needs its own complete bound. DEC-062's
6240 ranked allowance is unchanged. Task progress remains **11/19**.

Final focused **112/112** passes, including missing-reservation handoff denials
(`/tmp/resumable-recycle-credits-focused.log`).
Final capture-serial build passes (`/tmp/resumable-recycle-credits-build.log`):
RAM1 code **425672**, variables **91680**, locals **6624**; FLASH code **934264**.
Full native **2054 passed, 1 skipped** (`/tmp/resumable-recycle-credits-native.log`).
Strict OpenSpec validation and diff whitespace checks pass. No firmware upload
or HITL run.

## Recycle prepared history within its STOPPED-reserved horizon

2026-09-14: STOPPED prewarm now reserves both sides of the history and snapshot
ID swaps. Canonical history relocation advances storage generation before moving
rows. The original row/snapshot limits remain fixed across commits; successful
companion appends consume that horizon instead of requesting active growth.

After undo/snapshot consumers finish with the ID receipt, the caller explicitly
begins recycling. That ends receipt access. Each bounded unit pops one retired
ID or Create payload event, visits one empty historical row, or constructs one
empty row within proven vector capacity. No vector buffer or payload allocation
is released. Canonical rows, payloads, IDs and revision remain unchanged. On
completion the retained side returns to Prewarmed with the remaining row and
snapshot capacity. A request exceeding the remaining horizon denies before
preparation; a zero-companion preparation may still fit.
The restored row count is H plus usable companion slack, not the full reserved
row limit: a tighter snapshot horizon must not introduce unreserved Trim work.
The prewarm query also checks retained canonical mailbox capacity.

The recycle bound is ceil((L+E+A+S+4)/Q): reserved row limit L, retired payload
count E, consumed receipt size A and retired snapshot IDs S. A row visit replaces
one less row-growth unit, so the retired row count cancels. L/A/S/E retain their
existing 2048 horizons. Revision/storage/next-ID/history and snapshot mailbox
guards must stay current through recycling. Intervening canonical or mailbox
replacement denies reuse and requires STOPPED prewarm; active snapshot-box
replacement/refill remains an integration constraint, not a completed capability.
This extends Loop's existing preparation owner and changes no canonical or undo
semantics. Production receipt consumption and cleanup scheduling remain open.

Native coverage verifies two successive swaps using the same row/payload/ID
buffers, Q=1 bounded recycling, horizon exhaustion, source-change denial and
unchanged canonical storage, including recycling 2048 retired payload events.
Full native **2053 passed, 1 skipped**
(`/tmp/resumable-history-recycle-native.log`). Final focused **111/111** passes
after correcting unequal row/snapshot slack
(`/tmp/resumable-history-recycle-focused.log`). Final capture-serial build passes
(`/tmp/resumable-history-recycle-build.log`): RAM1 code **425608**, variables
**91648**, locals **6656**; FLASH code **934136**. Strict OpenSpec validation and
diff whitespace checks pass. No firmware upload or HITL run. Tasks remain **11/19**.

## Admit and charge canonical preparation separately

2026-09-14: PendingStopAdmission now accepts separately declared canonical
preparation grains and stores their remaining count and quantum. The DEC-062
ranked allowance stays 6240. Both wide inputs and the combined legacy/ranked/
canonical total are checked before narrowing or mutating admission; a total
above UINT16_MAX denies. Existing callers default to zero canonical work.
Finalization requires all four counters exhausted. Transport Stop preserves
canonical credits while changing command epoch/destination; reset clears them.

`TrackPreparedStopCommit::stepPreparation` verifies the reservation quantum,
closed reservations, frozen generation/epoch and exact history/input owners
before charging one canonical grain and running one bounded history/proof step.
Wrong Q, stale owners and exhausted credits deny without advancing preparation.
A completed proof query charges nothing. Setup and unused conservative credits
remain separately scheduled turns. The joint commit now also requires a
canonical reservation; a prepared result alone is not a stop permit.

This extends the existing per-track stop counter and preparation owners, with
no new policy ceiling, owner or schema. The canonical bound already includes
history/snapshot staging and N+K capture verification; setup must be added by
the caller. It does not consume the 6240 ranked allowance or establish measured
latency. Production stage dispatch and reusable active prewarm/retirement, plus
undo cleanup and HITL, remain open. Task 2.3 is not complete.

Native coverage verifies exact UINT16_MAX combined admission, overflow rejection
without state changes, closure-first charging, independent ranked credits,
transport Stop/reset semantics, pending-credit commit denial and completed-query
idempotence. The joint handoff fixture now uses the admitted preparation entry.
Focused **109/109** passes (`/tmp/resumable-canonical-credits-focused.log`).
Full native **2051 passed, 1 skipped** (`/tmp/resumable-canonical-credits-native.log`).
Capture-serial passes (`/tmp/resumable-canonical-credits-build.log`): FLASH code
**933112**, RAM1 code **425608**, padding **376**, variables **91648**, locals
**6656**. Strict OpenSpec validation and `git diff --check` pass. Tasks remain
**11/19**; no upload or HITL.

## Verify staged capture against the frozen canonical input

2026-09-14: optional typed capture verification is now part of Loop's existing
prepared-history continuation, and Track's joint handoff requires it. After
history staging, each quantum unit compares one sealed event against the raw
frozen capture's private canonical order, or advances one sealed chunk boundary.
A constant-work pool accessor rejects invalid/unsealed chunks. Field-aware MIDI
equality detects payload/type/tick/NoteId differences without padding comparisons.
Missing, extra and differently ordered events deny the entire candidate; matching
counts alone no longer authorize publication. No extra event buffer is allocated.

The proof binds the pending pass ID, chunk-vector address/count, prepared input
owner/generation and frozen capture owner, retaining their current-source guards
through commit. Preparation/discard and commit release only borrowed pointers.
Its bound extends canonical staging to ceil((H+E+R+S+N+K+2)/Q), with N staged
capture events and K sealed chunk refs (within the existing active-transfer chunk
horizon). These are separate canonical verification grains, not part of 6240
ranked preparation. The production scheduler must admit and run them explicitly.

While reviewing the producer, its rewrite cursor lacked destination ownership:
a paused rewrite could resume against another store. It now binds the first
store and rejects another destination until explicit discard. Native coverage
checks that the foreign store remains unchanged and the original rewrite resumes.

The existing joint-handoff fixture now runs Q=1 verification with correct input,
same-count payload/order mismatches, missing/extra events and an unverified legacy
candidate. Only verified candidates can publish. This extends existing Loop,
normalizer and pool access; it introduces no owner, schema or semantic change.
Focused **108/108** passes, including the rewrite-owner regression and Q=1
verification of 2046 captured events across eight sealed chunks within its
formula bound (`/tmp/resumable-capture-proof-focused.log`). Full native
**2050 passed, 1 skipped** (`/tmp/resumable-capture-proof-native.log`).
Capture-serial passes (`/tmp/resumable-capture-proof-build.log`): FLASH code
**932776**, RAM1 code **425544**, padding **440**, variables **91616**, locals
**6688**. Strict OpenSpec validation and `git diff --check` pass. No upload or
HITL; tasks remain **11/19**. Production
stage dispatch/history-grain admission, undo cleanup and HITL remain open.

## Track joint canonical and audible handoff

2026-09-14: Track now exposes a prepared-stop handoff backed by a stateless,
Arduino-independent implementation (`TrackPreparedStopCommit::commit`) so native
coverage executes the production body. It checks the exact LoopPasses and live
counter owners, the same prepared companion/capture owners, canonical capacity,
stop phase/credits/epoch/destination, source identities, cursor and ledger before
any mutation. Equal counter values from another owner do not authorize commit.
The target revision is the existing capture-plus-companion increment and its
snapshot identity is null because the old published snapshot becomes stale.
Revision overflow denies before mutation.

Bridge selection now separates its validated entry from no-fail publication.
Track commits canonical capture/history, then publishes the already-preflighted
bridge and advances to DeferredCleanup in the same non-interleaved turn. No
post-canonical typed-source recheck can reject the transaction after its own
revision changes. The selected bridge retains independent data after preparation
is discarded. There is no new owner, schema, undo policy or staging semantics;
the small test seam avoids pulling Arduino/Track construction into native tests.

The caller must still stage canonical capture from the matching frozen source
before entry. Production stop stage dispatch, that staging proof, admission of
canonical-history work, undo attachment and cleanup are not wired to this entry
point yet. Existing production routing is unchanged; task 2.3 remains open.
Native end-to-end coverage builds a real Loop-owned prefix and frozen tail,
stages capture/history, builds the bridge and commits zero/nonzero companion
sets. It verifies no mutation on foreign-counter-owner, cursor, ledger, transport
Stop or target-revision denial; matching canonical/audible identities on success;
repeat-commit rejection; and selected bridge lifetime after preparation discard.
Focused **108/108** passes (`/tmp/resumable-track-handoff-focused.log`).
Full native **2050 passed, 1 skipped** (`/tmp/resumable-track-handoff-native.log`).
Capture-serial passes (`/tmp/resumable-track-handoff-build.log`): FLASH code
**931992**, RAM1 code **425544**, padding **440**, variables **91616**, locals
**6688**. New handoff/publication helpers are verified in flash in the ELF;
RAM1 remains below the **425984** gate. Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL; tasks remain **11/19**.

## Joint canonical capture and companion handoff

2026-09-14: history preparation now remembers the already-reserved pending
capture pass ID. `canCommitPreparedCaptureAndCompanions` jointly checks history
readiness, pending pass identity, Overdub phase, nonempty sealed chunk ownership,
an empty mutable capture store and prewarmed canonical transfer capacity.
`commitPreparedCaptureAndCompanionsAtomic` transfers capture first, then uses
an internal no-fail history publication step. Rechecking history after capture
would incorrectly reject its own revision advance and leave a partial commit.
All fallible checks now precede mutation. Capture ownership retires through
scalar writes; there is no capture clear, visual coverage scan or buffer release.

The established revision convention remains: capture adds one revision and a
nonempty companion set adds one more. No-companion capture still commits once.
IDs stay ordered after the reserved capture ID; snapshot IDs and the undo receipt
publish with history. This composes existing Loop helpers without an ownership,
schema or semantic change. The caller must still establish that staged capture
matches the frozen runtime source and jointly preflight source/epoch/ledger and
audible selection. Production Track routing and session-exit undo attachment
remain open; this helper is not yet selected by the production stop path.

Native coverage verifies zero/nonzero companion commits, retained chunk-vector
ownership, exact ID/revision changes, repeat-commit rejection and unchanged
canonical state/receipt on pending-ID, row-owner, mutable-store, phase, capacity
or revision denial. Focused **107/107** passes
(`/tmp/resumable-joint-canonical-focused.log`). Full native **2049 passed,
1 skipped** (`/tmp/resumable-joint-canonical-native.log`). Capture-serial passes
(`/tmp/resumable-joint-canonical-build.log`): FLASH code **928304**, RAM1 code
**424712**, variables **91616**, locals **6688**. Strict OpenSpec validation and
`git diff --check` pass. Tasks remain **11/19**; no upload or HITL.

## Publish prepared companion history and retain its ID receipt

2026-09-14: Loop preparation now copies the existing snapshot companion-ID set
in Q-bounded visits and builds both its successor and the companion-only undo
receipt while appending prospective rows. STOPPED prewarm reserves both vectors
and the existing snapshot box. Snapshot ID appends invalidate by size; clear/
replacement and our swap invalidate through a monotonic generation, preventing
same-size/address reuse from accepting an obsolete candidate. Prospective IDs
must be newer than queued IDs, avoiding a duplicate scan at commit.

`commitPreparedCompanionHistoryAtomic` preflights readiness, swaps history and
snapshot ID vectors, advances nextPassId and canonical revision/storage identity,
and makes the receipt independent of the frozen runtime owner. Repeated commit
is rejected. Empty companion sets preserve canonical identity. Retired history
and its payload storage stay owned by Loop until STOPPED prewarm; no destructor
traversal runs at handoff. Cache invalidation is scalar: the regular notification
would scan visual-cache coverage in capture builds and is intentionally omitted.
The preparation bound is now ceil((H+E+R+S+2)/Q), where S is the existing queued
snapshot-ID count and S+R <= 2048. It remains separate from the 6240 ranked grains.

This extends the existing Loop and DEC-056 companion mailbox; it changes no
schema, ownership or undo policy. The receipt still needs routing through the
existing session-exit undo/snapshot bookkeeping. Capture transfer and audible
selection must be jointly preflighted before production use; the helper is not
connected to the production stop state machine. Task 2.3 remains open.

Validation: focused **106/106** (`/tmp/resumable-history-swap-focused.log`);
full native **2048 passed, 1 skipped** (`/tmp/resumable-history-swap-native.log`).
The full-suite playback object was rebuilt after the scalar-invalidation change.
Capture-serial passes (`/tmp/resumable-history-swap-build.log`): FLASH code
**928304**, RAM1 code **424712**, padding **1272**, variables **91616**, locals
**6688**. Strict OpenSpec validation and `git diff --check` pass. No upload or HITL.

## Stage canonical companion history on Loop without publishing

2026-09-14: the existing `sealPendingNoteChangesToEditPasses` appends visible
rows and may allocate, so it cannot run inside the future constant-work atomic
handoff. Extend Loop's existing preparation ownership with a separately
prewarmed edit-history vector. Copy metadata and Create payloads in bounded
units, append prepared Hide/Shorten rows with prospective IDs, and trim unused
empty companion slots one per unit. The canonical vector, nextPassId, revision
and storage generation stay unchanged throughout preparation.

Prewarm is STOPPED-only and limits total history/companion rows and copied
payload events to their 2048 horizons. The bound is ceil((H+E+R+2)/Q), separate
from ranked preparation. Steps/readiness reject source revision/storage, row
owner or next-ID changes. Output stays hidden until complete. Logical discard
retains storage and never clears pending capture or changes. Atomic vector swap,
ID publication and deferred old-history cleanup remain next.

Historical review: reused Loop's canonical ownership/prepared-capture pattern,
RuntimeCaptureCompanions payloads and DEC-060 scope. `saveNoteEditPass` and
`sealPendingNoteChangesToEditPasses` cannot be used during invisible preparation
because they append active rows immediately. This adds state to the existing
owner; no ownership transfer, schema or undo semantics change is introduced.
Native coverage verifies Q=1 invisibility, history payload independence, disabled
row preservation, prospective IDs, stale guards, capacity and ID overflow.
Focused prewarm **103/103** before the maximum-payload fixture
(`/tmp/resumable-companion-history-focused.log`). Capture-serial build passes:
FLASH code **928720**, RAM1 code **424712**, padding **1272**, variables **91616**,
locals **6688** (`/tmp/resumable-companion-history-build.log`). Full native
**2046 passed, 1 skipped**, including maximum-payload staging
(`/tmp/resumable-companion-history-native.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL. Task 2.3 remains open.

## Joint stop-reservation and audible preflight

2026-09-14: `canSelectReservedFrozenCaptureBridge` checks AtomicTransfer phase,
a ranked reservation, exhausted closure/order/ranked counters, matching command
epoch and PLAYING destination, explicit base/target identities, and existing
typed bridge/ledger/cursor readiness. This is read-only: canonical capacity and
companion-row publication still need their own proof before combined commit.

The end-to-end fixture checks outstanding order/ranked credits, wrong phase,
wrong source identities, ledger changes, transport Stop and no selection/revision
mutation during preflight. Its new out-of-range cursor assertion initially
failed: modulo coverage accepted phase == loop length. Shared bridge selection
preflight now explicitly rejects zero length and phase >= loop length.
Focused prewarm **101/101** (`/tmp/resumable-stop-preflight-focused.log`).
Capture-serial build passes: FLASH code **928416**, RAM1 code **424712**, padding
**1272**, variables **91616**, locals **6688**
(`/tmp/resumable-stop-preflight-build.log`). Full native **2043 passed,
1 skipped** (`/tmp/resumable-stop-preflight-native.log`). Strict OpenSpec
validation and `git diff --check` pass. No upload or HITL.
Production ranked stop dispatch and canonical companion-row staging remain
open; task 2.3 is not complete.

## Route Track STOPPED preparation through canonical baseline construction

2026-09-14: `Track::ensurePlaybackMergedEventsForSlot` now attempts typed
canonical baseline construction inside its existing all-tracks-stopped guard.
The runtime uses idle prewarmed workers to select passes, apply history and
merge the effective baseline, compares every event against the existing oracle,
and publishes origins only on exact sequence/payload/identity agreement.
The comparison reuses the mapper's field-aware MIDI equality (no struct-padding
memcmp). A mismatch, busy worker, unsupported window or invalid source retains
the flat publication path. Owned prefixes are never cancelled. Temporary workers
are discarded after success/denial so no stack source references survive.

This is synchronous STOPPED throughput with no new allocation or active-path
work. Native tests cover historical Length, payload mismatch without published
view mutation, retry/worker cleanup, stale source, window rejection, occupied
prefix preservation and empty canonical content. Focused prewarm **101/101**
(`/tmp/resumable-track-baseline-focused.log`). Capture-serial build passes:
FLASH code **928256**, RAM1 code **424712**, padding **1272**, variables **91616**,
locals **6688** (`/tmp/resumable-track-baseline-build.log`). Full native:
**2043 passed, 1 skipped** (`/tmp/resumable-track-baseline-native.log`). Strict
OpenSpec validation and `git diff --check` pass. No upload or HITL.
Production ranked stop dispatch
and atomic canonical/audible handoff remain next; task 2.3 remains open.

## Publish typed STOPPED baselines with owned provenance

2026-09-14: tracing production dispatch found that the existing Track STOPPED
publisher supplies flat events without pass origins, so it cannot serve as the
base of a ranked companion bridge. A typed runtime publisher now accepts a
completed unedited effective-pass baseline and copies events/origins together,
orders the current view and publishes only after completion. A flat cached view
with the same identity must be upgraded; invalid source/coverage preflight
preserves the existing published view. No allocation or extra array is added.
This synchronous path is STOPPED-only and does not count as an active grain.

The end-to-end reserved bridge fixture now uses this publisher, upgrades a flat
view, checks repeated publication and invalid preflight, and discards the source
baseline before mapping. Additional fixtures cover empty and 2048-event baseline
publication and storage ownership. Focused prewarm **98/98** before the added
capacity/empty fixture (`/tmp/resumable-typed-oracle-focused.log`). Track-level
canonical baseline construction remains to be wired before production stop
stage dispatch and atomic source handoff. Capture-serial build passes:
FLASH code **927816**, RAM1 code **424632**, padding **1352**, variables **91616**,
locals **6688** (`/tmp/resumable-typed-oracle-build.log`). Full native **2041
passed, 1 skipped** (`/tmp/resumable-typed-oracle-native.log`); final focused
prewarm **99/99** includes additional maximum-capacity origin/event assertions
(`/tmp/resumable-typed-oracle-final-focused.log`). Strict OpenSpec validation
and `git diff --check` pass. No upload or HITL. Task 2.3 remains open.

## Charge reserved tail and bridge steps

2026-09-14: added reserved entry points for canonical-tail and ranked-bridge
steps. They require a pending stop with closed reservations, matching frozen
generation/epoch and the admitted quantum. One credit is charged before each
child step; no credit is spent for a completed readiness query. Wrong quantum,
stale reservation and exhausted credits deny without progressing the child.
The normalizer exposes a current frozen-source check during preparation, so
reservation validation does not require an already-complete input owner.

The end-to-end fixture now uses reserved steps for ordinary and prepared-order
tails, charges the two setup turns, verifies exact credit consumption through
mapping/compilation, and rejects a later transport Stop at completed bridge
readiness. Separate coverage checks closure gating, invalid quantum, generation,
epoch, exhaustion and frozen-capture preservation. Focused prewarm **98/98**
(`/tmp/resumable-reserved-steps-focused.log`). Capture-serial build passes:
FLASH code **927320**, RAM1 code **424632**, padding **1352**, variables **91616**,
locals **6688** (`/tmp/resumable-reserved-steps-build.log`). Full native:
**2040 passed, 1 skipped** (`/tmp/resumable-reserved-steps-native.log`). Strict
OpenSpec validation and `git diff --check` pass. No upload or HITL.
Production stage dispatch and
atomic source publication remain next; task 2.3 stays open.

## Separate pending-stop ranked credits

2026-09-14: `PendingStopAdmission::begin` now accepts an optional uint64 ranked
preparation bound, rejects values above the DEC-062 allowance and combined
counter overflow before state mutation, and stores admitted work in a separate
counter. Ranked consumption requires closed reservations and cannot consume
legacy order credits. Commit accounting remains incomplete until all counters
are empty. Reset clears the new counter; transport Stop preserves outstanding
work while changing epoch and destination. Existing production callers default
to zero ranked work until typed tail/bridge routing is connected. Budget
exhaustion alone never establishes prepared-source readiness.

Native regressions cover the full 6240 reservation, independent counters,
closure gating, early-commit denial, transport Stop during preparation,
underflow, reset, invalid uint64 bounds, combined-total overflow and exact
UINT16_MAX total admission. Focused prewarm **97/97**
(`/tmp/resumable-ranked-credits-focused.log`). Capture-serial build passes:
FLASH code **926848**, RAM1 code **424568**, padding **1416**, variables **91616**,
locals **6688** (`/tmp/resumable-ranked-credits-build.log`). The counter adds
32 bytes to static RAM1 across track instances; code remains below 425984.
Full native **2039 passed, 1 skipped** (`/tmp/resumable-ranked-credits-native.log`).
Strict OpenSpec validation and `git diff --check` pass. No upload or HITL.
Task 2.3 remains open;
producer horizon admission and typed atomic source handoff are still required.

## Provisional allowance approved (DEC-062)

2026-09-14: user approves **6240 ranked-preparation grains** for now and defers
realtime reservation sizing to HITL when the path can run. The named constant
`StopAdmissionContract::kProvisionalRankedPreparationGrains` records this policy.
The allowance is separate from legacy closure/order/rewrite/transfer work.
Earlier comparisons to 3072 below remain historical calculations, not blockers
to provisional integration. Larger horizons still need explicit admission
checks; source/epoch validation and preservation of accepted events remain.
Task 2.3 and final validation/HITL gates stay open. No upload performed.

## Compose the prepared-order ranked-tail bound

2026-09-14: `requiredPreparedRankedTailGrainsForPasses` replaces only capture
normalization in the existing ranked-tail aggregate with the prepared-order
import bound. Companion rows, per-pass application/merge, provenance mapping,
bridge compilation and eight setup/transition turns retain their conservative
allowances. Invalid horizons return UINT64_MAX before subtraction/addition.
The caller must establish a completed/current canonical order; its first sort
remains upstream work. This helper grants no production admission.

At C=2048,P=2,B=2048,H=R=0,Q=32 the additional bound is **6240 grains**, compared
with **7648** without prepared order. Both still exceed the existing **3072**
credits, and exclude closures, canonical rewrite/transfer, cleanup and denied
service turns. Production stop routing remains gated.

The coordinator-through-ranked-bridge fixture now runs with both ordinary and
prepared canonical order. Both finish within their aggregate, preserve the
same shortened-note result and reject final preflight after epoch change.
Bounds also cover C=1/16/2048, Q=1/32, empty/single/maximal history and companion
horizons, plus invalid arguments. Focused prewarm **95/95**
(`/tmp/resumable-prepared-tail-bound-focused.log`). Capture-serial build passes:
FLASH code **926760**, RAM1 code **424504**, padding **1480**, variables **91584**,
locals **6720** (`/tmp/resumable-prepared-tail-bound-build.log`). Full native:
**2037 passed, 1 skipped** (`/tmp/resumable-prepared-tail-bound-native.log`).
Strict OpenSpec validation and `git diff --check` pass. No upload or HITL.
Task completion stays **11/19**; task 2.3 and production stop/atomic integration,
admitted-tail/service proof, telemetry and HITL remain open.

## Bind completed orders to shared index storage

2026-09-14: review found that canonical builder completion outlived shared-index
reuse: canonical order → audible order → audible discard left a bound, complete
canonical builder pointing at audible indexes. Tail import would attempt that
stale order, and canonical rewrite could accept it after the audible binding
was cleared. Capture generation alone cannot distinguish these representations.

Both `FrozenCaptureOrderBinding` instances now record the shared storage
generation at successful begin. Resume, step, canonical rewrite, audible
candidate readiness and prepared-tail reuse all require the current generation.
A stale canonical binding causes tail admission to use its private sorter.
Explicit discard/rebuild restores a current binding; no implicit restart occurs.
The check adds constant work and fixed metadata, with no allocation or new
content scan. Semantic references and declared grain bounds remain unchanged.

Regressions cover audible reuse/discard followed by canonical resume/rewrite,
private-tail fallback preserving On-before-Off raw ties, explicit rebuild, and
audible invalidation while pending and complete. Focused prewarm **94/94**
(`/tmp/resumable-order-receipt-focused.log`). Capture-serial build passes:
FLASH code **926672**, RAM1 code **424504**, padding **1480**, variables **91584**,
locals **6720** (`/tmp/resumable-order-receipt-build.log`). Full native validation:
**2036 passed, 1 skipped** (`/tmp/resumable-order-receipt-native.log`). Strict
OpenSpec validation and `git diff --check` pass. No upload or HITL.
Task completion stays **11/19**; task 2.3 and production stop/atomic integration,
admitted-tail/service proof, telemetry and HITL remain open.

## Reuse completed canonical capture order for the frozen tail

2026-09-14: the frozen-tail coordinator now supplies completed canonical capture
indexes when their binding matches the frozen source and audible ordering has
not reused the shared array. `RuntimeCanonicalPassInputs` imports at most Q
indexes per grain into its existing private array, validating index range,
retention and stable raw-tick/source-index order. The shared-order generation
advances before runtime order begin/discard; reuse during import denies even
when capture generation is unchanged. After import, the receipt is released and
audible ordering can reuse the shared array independently. If canonical order
is unavailable or shared order is audible, the private sorter remains the path.
No allocation, extra array or semantic correction is introduced.

The conditional prepared-tail normalization bound is
`3*ceil(C/Q)+(H ? historyApplyBound : 0)+4`: **196 grains** at C=2048,H=0,Q=32,
versus **1604** without prepared order. The first canonical sort is still charged
upstream. The general **7648-grain** ranked-tail estimate, production credits,
and stop-routing gates are unchanged.

Native coverage verifies Q=1 import, canonical On/Off tie preservation after
audible reuse, same-capture-generation rebuild denial, audible-order fallback,
maximum staging capacity, and all-filtered capture without a new pass. Focused
edit/prewarm tests passed **215/215** before the additional capacity fixture;
the final full native run includes it: **2034 passed, 1 skipped**
(`/tmp/resumable-canonical-order-reuse-native.log`). Capture-serial build passes:
FLASH code **926456**, RAM1 code **424504**, padding **1480**, variables **91584**,
locals **6720** (`/tmp/resumable-canonical-order-reuse-build.log`). Strict OpenSpec
validation and `git diff --check` pass. No upload or HITL.
Task completion stays **11/19**; production frozen-tail/atomic routing,
admitted-tail/service proof, telemetry and HITL remain open.

## Skip merges for inputs verified ordered during fill

2026-09-14: `RuntimePlaybackOrderBuilder` now compares each retained input with
the preceding retained input during the existing fill grain. The check calls
the exact merge comparator, including phase-zero Off-before-On, raw-tick ties,
canonical ranks/provenance and stable index ties. Canonical-capture mode retains
its separate stable raw-tick rule. Any inversion keeps the existing merge path;
otherwise fill completes without merge/copy passes or writes to merge scratch.
Discard/reuse resets the check. Filtered rows still consume input quantum.

For already ordered 2048-row inputs, Q=32 completes in **64 grains**, compared
with the **1472-grain conservative sort allowance**. This applies to owned view
orders, raw capture order and borrowed ranked indexes. It adds at most one
comparison per retained fill entry, with no extra scan or allocation. General
bounds, reserved production stop credits and the 7648-grain worst-case ranked
tail estimate are unchanged. This does not establish on-device grain cost or
reduce the guaranteed stop allowance.

New native fixtures exercise all three modes at maximum capacity with Q=1/32,
verify unchanged merge scratch, retained adjacency across a filtered inversion,
restart after ordered/unordered runs, and same-tick rank inversion fallback.
Existing wrap, equal-phase, differential edit and ranked-bridge fixtures pass.
Focused edit/prewarm **212/212** (`/tmp/resumable-ordered-fill-focused.log`).
Full native **2030 passed, 1 skipped** (`/tmp/resumable-ordered-fill-native.log`).
Capture-serial build passes: FLASH code **925880**, RAM1 code **424504**, padding
**1480**, variables **91584**, locals **6720**
(`/tmp/resumable-ordered-fill-build.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL.
Task completion stays **11/19**; production
frozen-tail/atomic routing, service/latency proof, telemetry and HITL remain open.

## Stop-work horizons and invalid-contract rejection

2026-09-14: `requiredRankedTailGrainsForPasses` accepts an explicit maximum
effective-pass count P and published-base event count B. Candidate horizon C
must cover every intermediate state, including transient Create growth. The
bound sums normalization, companion rows, P-pass application, B/C mapping and
B/C compilation with eight setup/transition turns. Full-horizon helpers retain
P=B=C, using the same composition. Invalid horizons and zero quantum return
UINT64_MAX; results are never narrowed into existing stop credits.

For C=2048,P=2,B=2048,H=R=0,Q=32 the conservative additional ranked-tail bound is
**7648 grains**, versus **146776** with P=C. This excludes closure/canonical
rewrite/transfer, cleanup and service denials and still exceeds the production
3072-order-grain allowance. It is not a production admission or latency proof.

The budget audit also found that saturated order counts could look admissible
with a UINT16_MAX allowance, and direct zero-quantum queries could divide by zero.
Legacy contracts now reject overflowing total credits, event counts above fixed
staging capacity, and closure counts above physical reservations. Invalid order
queries return the UINT16_MAX sentinel; that sentinel never authorizes a stop.
Existing production limits and valid admissions are unchanged.

Native tests cover invalid sizes through SIZE_MAX, zero quantum, exact maximum
valid total credits, overflow, no stop-state mutation after denial, refined-bound
monotonicity and invalid horizons. The coordinator-to-bridge fixture now counts
its actual preparation/mapping/compiler turns against the explicit bound.
Focused edit/prewarm tests pass **209/209**
(`/tmp/resumable-stop-horizons-focused.log`). Full native validation:
**2027 passed, 1 skipped** (`/tmp/resumable-stop-horizons-native.log`).
Capture-serial build passes: FLASH code **925584**, RAM1 code **424440**, padding
**1544**, variables **91584**, locals **6720**
(`/tmp/resumable-stop-horizons-build.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL.
Production frozen-tail/atomic routing, admitted service/latency proof, telemetry
and HITL remain open. Task completion stays **11/19**.

## Runtime-owned frozen-tail coordination

2026-09-14: `beginFrozenCanonicalTail` admits only this runtime's completed,
current prefix and frozen capture, with idle companion row/pass owners and an
available apply worker. It starts row ownership and appends the frozen tail
without replaying/copying the prefix. Failed admission leaves the prefix usable.
The caller must keep pending changes immutable and live counters/owners alive;
capture IDs, retained rows and reserved closures must already be final.

`stepFrozenCanonicalTail(Q)` runs exactly one normalization, row, companion-setup
or per-pass companion child per turn. Stage transitions do not step the next
child. The coordinator holds independent input/row/pass generations, checks row
source validity during normalization, and checks normalized inputs through the
remaining stages. Typed readiness retains live revision/storage/epoch guards.
Cancellation discards only still-owned generations, preserves frozen capture,
and returns an owned prefix to idle for rebuild. An unrelated reused owner is
not cancelled. No new external arrays, stop credits or audible selection paths.

Native tests verify prefix-buffer reuse, separate child turns, Shorten applied
per pass alongside a capture CC, completion within the composed child bound,
source/change-vector cancellation, same-capture-generation row reuse, normalizer
reuse, epoch invalidation after completion, and restart. The complete result
feeds the existing ranked bridge mapper/compiler and admission preflight in the
same fixture; changing the epoch then denies that preflight. Initial focused
edit/prewarm **207/207** (`/tmp/resumable-tail-owner-focused.log`). Final full
native run including bridge preflight: **2025 passed, 1 skipped**
(`/tmp/resumable-tail-owner-native.log`). Capture-serial build passes: FLASH code
**925512**, RAM1 code **424440**, padding **1544**, variables **91584**, locals
**6720** (`/tmp/resumable-tail-owner-build.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL.

The production stop path still reserves 3072 order/rewrite grains and has no
credits/service guarantee for this extra work, mapping or compilation. It is
therefore not routed through the new coordinator. Production frozen-tail/atomic
handoff, capture-to-base provenance integration, telemetry/device cost, aggregate
stop/service proof and HITL remain open. Task completion stays **11/19**.

## One prefix child per VisualCache turn

2026-09-14: production `main.cpp` now selects at most one canonical prefix child
across tracks during a VisualCache owner turn. A fixed track-count scan first
preserves STOPPING/recording and active visual-cache/source/wrap work priority.
Stopped inactive empty slots do not block this scan. A rotating cursor then
selects one eligible overdub track; success returns from the global idle owner
before any other track maintenance or cleanup can run. The existing multi-track
compatibility path remains when no prefix child runs; this change does not
claim to bound that path.

`Track::processDeferredCanonicalPrefix` uses `slotIfAllocated`, checks existing
prewarm/readiness, and binds actual Loop revision/storage generation/length and
Track playback generation. Each consumed turn does setup, stale cleanup, or one
Q=32 normalization child. Missing runtimes are not allocated. Ready/denied
source keys do not consume turns; eligible owners resume existing cursors.
Priority denial leaves both prefix state and selection cursor unchanged.
Published MIDI, capture admission, stop credits and atomic transfer are unchanged.

Native policy tests cover all five owners, priority work on every track,
zero-track/no-work turns, exactly one consumed child and rotation. A two-runtime
fixture checks pause without restart, alternate startup, one grain per turn,
and both prefixes completing without another child stacking. Earlier focused
policy/prewarm validation passed **113/113**
(`/tmp/resumable-prefix-scheduler-focused.log`). Final full native validation:
**2023 passed, 1 skipped** (`/tmp/resumable-prefix-scheduler-native.log`).
Capture-serial build passes (`/tmp/resumable-prefix-scheduler-build.log`): FLASH
code **924488**, RAM1 code **424376**, padding **1608**, variables **91584**,
locals **6720**. Strict OpenSpec validation and `git diff --check` pass.
Row 30 in ExecutionBounds records the structural quantum. Device grain timing,
telemetry detail, admitted-tail service/latency proof and frozen/atomic handoff
remain open. Task completion stays **11/19**. No upload or HITL.

## Storage invalidation independent of audible revision

2026-09-14: the lifetime audit found three cases that an audible revision alone
cannot cover: timeline reset releases content and restarts revision at zero;
disabled-edit reclamation can relocate retained rows without changing audible
content or revision; edit append can defer the audible revision update while
still reallocating history. `Loop::canonicalStorageGeneration` now advances before
reset releases chunks/history, and before each removal predicate lets
`remove_if` relocate history, and before `saveNoteEditPass` appends a row.
A no-op reclamation does not invalidate work.
The marker is runtime-only and is not restored from persisted snapshots.

`beginCurrent` requires a live storage-generation reference. Each step and
completed-output access compares it before dereferencing borrowed canonical
inputs. Prefix service keys include the storage owner/value, so unchanged
audible identity can still request stale cleanup and a separate restart turn.
The guard remains attached when the completed prefix accepts frozen capture.
Raw/frozen-only entry points retain their explicit caller-frozen lifetime
contract. No pairing, edit application or persistence semantics changed.

Native tests use actual Loop reset/reclaim operations: reset both a pending and
completed prefix at the same playback identity; relocate an active Length row
by reclaiming a preceding disabled row; rebuild and preserve the effective Off
tick; keep a completed prefix for no-op reclamation; append a deferred Length
row without changing audible revision and rebuild its new Off tick; reject a
reset after tail attachment. Initial focused validation: **204/204**
(`/tmp/resumable-storage-generation-focused.log`). Final full native run, including
the required reference and deferred append guard: **2020 passed, 1 skipped**
(`/tmp/resumable-storage-generation-native.log`).
Capture-serial build passes (`/tmp/resumable-storage-generation-build.log`):
FLASH code **924120**, RAM1 code **424376**, padding **1608**, variables **91584**,
locals **6720**. Strict OpenSpec validation and `git diff --check` pass. No upload
or HITL. No scheduler hook or audible admission route is enabled yet.
Remaining mutation/lifetime integration, scheduler eligibility, aggregate stop
proof, atomic transfer and HITL remain open; task completion stays **11/19**.

## Live-source prefix continuation

2026-09-14: `beginCurrent` binds live revision, source generation and loop length
before capture freezes. Every normalization step and completed-output access
checks those fields. `LoopPlaybackRuntime` owns a stable source token plus a
prefix continuation that admits one setup, one Q-bounded normalization child,
or stale cleanup per call; restarting is a separate turn. Ready and denied
source keys do not request more service. Denials retry only after a key change.
A busy apply worker postpones initial admission. Normalizer generation protects
unrelated reuse; capture attachment yields ownership immediately to the frozen
tail, even while that tail is incomplete. Reset clears the prefix continuation.
No external arrays, pairing rules, or canonical content mutations are added.

Four native tests exercise Q=1 setup/completion, all three live invalidators,
cleanup/restart separation, zero-quantum refusal, oversized-history denial
latching/recovery, reused-owner protection, frozen-tail handoff retaining prefix
storage, and reset. Focused edit/prewarm tests pass **202/202**
(`/tmp/resumable-live-prefix-focused.log`). Initial fixture failures were a
missing reset argument and an invalid default record-pass ID; both corrected.

Capture-serial build passes (`/tmp/resumable-live-prefix-build.log`): FLASH code
**923920**, RAM1 code **424312**, padding **1672**, variables **91584**, locals
**6720**. Full native regression: **2018 passed, 1 skipped**
(`/tmp/resumable-live-prefix-native.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload/HITL.

Production scheduling is still open. The existing VisualCache owner has a
possible slot after source-span/dirty-cache work, but `Loop::resetPassTimeline`
releases chunks/history and resets the revision. Generation/discard ordering
must cover that path and other replacements before the borrowed prefix is
scheduled across turns. Live counter checks do not retain canonical owners or
prevent revision reuse. No new maintenance hook or stop-admission budget is
enabled; published-base provenance, atomic transfer, service proof and HITL
remain open. Task 2.3 and all other open task checkboxes remain unchanged.

## Indexed per-pass provenance mapping

2026-09-14: the per-pass mapper replaces repeated duplicate/lookup scans with
an ordered private index of `(pass,event)` origins. Clear fills original base
indexes; validation still checks each published event against its input origin.
A Q-bounded bottom-up merge sorts only indexes, adjacent comparisons reject
duplicate origins, and Q-bounded lower-bound searches resolve candidates.
Published events/origins/order and final canonical ranks remain unchanged.
Missing origins still insert capture events; removed base rows still suppress;
replacements still use original base indexes. No event/NoteId matching shortcut.

One extra C-entry uint16 array adds **4096 prewarmed bytes at C=2048**. Existing
unpublished record-rank storage serves as merge scratch before candidate rows
are produced. No active allocation or source mutation is introduced. Every
merge/copy entry, duplicate check, binary-search comparison and final lookup
consumes quantum. Source/generation guards still hide incomplete/stale results.

The executable bound is now
`ceil((4B+2B*ceil(log2(B))+A*(floor(log2(B))+3))/Q)+4` for B>0; for B=0 use
zero sort/search levels, giving `ceil(2A/Q)+4`. At B=A=2048,Q=32 this is
**2564 grains**, replacing the earlier quadratic bound of **196836**.
This supersedes the mapping term in earlier aggregate calculations; compiler,
companion and production service/latency gates remain unchanged.

Generated per-pass/capture traces, duplicate/invalid-provenance denial,
source/owner invalidation and runtime admission are rerun. A new maximum case
has 2048 base/candidate events with four interleaved pass origins at Q=1,
verifying completion within the indexed bound and identical emitted ordering.

Validation: focused edit/prewarm **198/198**
(`/tmp/resumable-origin-index-focused.log`). Capture-serial build passes:
FLASH code **923144**, RAM1 code **424248**, padding **1736**, variables **91584**,
locals **6720** (`/tmp/resumable-origin-index-build.log`). Strict OpenSpec
validation and `git diff --check` pass. No upload or HITL performed.

## Empty-row preparation bypasses

2026-09-14: normalization with no selected history rows copies raw scratch
straight into effective-pass storage, bypassing the apply worker and its extra
event copy. Per-pass preparation with no companions merges directly from its
immutable input descriptors, retaining original pass/event origins. It does not
borrow/copy through the apply worker. Nonempty history/companions retain their
existing paths and semantics. All copies/merges remain Q-bounded and unpublished
until completion; source, capacity and owner-generation checks remain in force.

`RuntimeCompanionPasses::requiredGrainsUpperBound` now exposes the per-pass bound
and omits the apply term when R=0. Normalization/tail bounds similarly omit the
history term when H=0. Standalone empty apply batches, which still perform their
own copy, use `ceil(C/Q)` instead of the general edit bound. Aggregate ranked
preparation/tail calculations use the new per-pass API. Nonempty full-horizon
bounds and the open production stop/service proof are unchanged.

The new test starts an unrelated apply job after no-edit admission, runs both
normalization and merge, and verifies that worker generation/state are untouched,
output bytes/origins match, and cleanup does not cancel it. Existing borrow-guard
tests now use actual edit rows so they still exercise worker ownership. The first
run's three failures were those obsolete empty-batch borrowing assumptions;
updated tests preserve the original ownership assertions. Final focused
edit/prewarm validation: **197/197** (`/tmp/resumable-empty-row-focused.log`).

Capture-serial build passes: FLASH code **922576**, RAM1 code **424184**, padding
**1800**, variables **91584**, locals **6720** (`/tmp/resumable-empty-row-build.log`).
Strict OpenSpec validation and `git diff --check` pass. No upload or HITL.

## Reuse of completed sealed prefixes

2026-09-14: `RuntimeCanonicalPassInputs::appendFrozenOverdub` extends an already
complete, still-current sealed-pass prefix with frozen capture. It performs no
pass selection, chunk read, or historical replay for the prefix. It retains the
existing effective-pass storage and selected history and starts private capture
ordering directly. Capture normalization still applies history independently,
then the existing typed companion/bridge pipeline consumes all passes.

The transition requires current source identity, matching live revision,
generation/length, fully closed frozen capture, and an available apply worker.
It increments the normalizer generation before any tail write, invalidating old
consumers. Failed admission preserves the prefix and its generation. Repeated
append, incomplete/stale prefix, invalid frozen state, or a borrowed worker is
refused. Original source/history/content owners remain retained and immutable.
No allocation, source mutation, or prefix event copy occurs in the transition.

The current canonical source used to validate the prefix remains distinct from
the target identity on the prepared bridge. Twenty end-to-end fixture cases now
reuse the baseline prefix through ranked runtime selection, including the
four-cycle open-note/capture trace. Additional cases cover old-consumer
invalidation, failed admission preserving readiness, duplicate append refusal,
borrowed-worker pressure, stale identity and tail grain-bound arguments.

`requiredFrozenTailGrainsUpperBound(C,H,Q)` charges only capture ordering,
raw/effective copies and capture history application:
`orderGrains(C,Q)+2*ceil(C/Q)+Happly(C,H,Q)+4`.
`StopAdmissionContract::requiredRankedTailGrains` substitutes this for full input
preparation in the earlier aggregate formula. It still charges companion work
on all passes, mapping and compilation. The full-horizon tail bound remains
larger than uint32; prefix reuse alone does not establish stop latency.
Production must schedule and retain the prefix before stop, reserve a proved
admitted tail, and combine canonical transfer with selection atomically. Those
steps, coalescing and HITL remain open. No task checkbox changes.

Validation: focused edit/prewarm **196/196**
(`/tmp/resumable-prefix-reuse-focused.log`); final edit rerun **123/123** adds
aggregate prefix+tail overflow rejection (`/tmp/resumable-prefix-reuse-overflow.log`).
Capture-serial build passes: FLASH code **922296**, RAM1 code **424184**, padding
**1800**, variables **91584**, locals **6720** (`/tmp/resumable-prefix-reuse-build.log`).
Strict OpenSpec validation and `git diff --check` pass. No firmware upload or HITL.

## Executable ranked-preparation grain bounds

2026-09-14: mapping and ranked compilation now expose uint64 grain-bound APIs.
For B base events, A candidate events, K records and quantum Q:

- Mapping: `ceil((3B+A+B*(B-1)/2+A*B)/Q)+4`.
- Ranked compilation: `ceil((3B+3K)/Q)+orderGrains(B,Q)+orderGrains(K,Q)+6`.

Compilation charges at most K insertions, both ordering children and linear
clear/record/closure/output walks. Fixed-size ledger scans remain part of each
entry's timing cost, not additional scheduling grains. Existing generated and
maximum per-pass bridge fixtures now assert completion within these APIs.

`StopAdmissionContract::requiredRankedPreparationGrains(C,H,R,Q)` composes frozen
canonical input normalization I, prepared companion rows, per-pass continuation
application A, mapping M and ranked compilation B:
`I+ceil(R/Q)+1+C*(A+ceil(C/Q)+4)+M(C,C,Q)+B(C,C,Q)+4`.
Every pending change is conservatively treated as a companion and pass count is
bounded by C. Invalid horizons or zero quantum return UINT64_MAX. Metadata caps
keep valid results within uint64; nothing is narrowed into the legacy stop
counters, and no production admission behavior changes.

For C=H=R=2048 the conservative result is **36,909,022,413,696 grains at Q=32**
(**1,164,015,715,564,640 at Q=1**). These are conservative structural bounds,
not predicted execution times. Even the no-history/no-companion full horizon
bound exceeds uint16. Therefore these bounds cannot justify routing the pipeline
through the existing 3072-order-grain contract. Merely widening counters or using
observed average timings would not establish acceptable stop latency.

The sum explicitly excludes accepted-note closures, canonical rewrite/transfer,
cleanup and denied turns. A complete production stop proof still needs prepared
prefix reuse/incremental work, sharper admitted-tail bounds, canonical atomic
transfer cost and a declared service/latency contract. Task 2.3 stays open.
Focused edit/prewarm validation: **195/195**
(`/tmp/resumable-ranked-bounds-focused.log`).

Capture-serial build passes: FLASH code **921888**, RAM1 code **424184**,
padding **1800**, variables **91584**, locals **6720**
(`/tmp/resumable-ranked-bounds-build.log`). Strict OpenSpec validation and
`git diff --check` pass. No upload or HITL performed.

## Ranked bridge runtime admission

2026-09-14: `LoopPlaybackRuntime::beginFrozenCompanionBridge` now connects
complete typed canonical-overdub/companion preparation to runtime-owned ranked
mapping and compilation. It requires the runtime's own frozen capture, normalized
inputs (including capture), companion row owner, command epoch and target source.
It refuses a second selected bridge pending the coalescing implementation.

`stepFrozenCaptureBridge` admits one Map or Compile child per turn; transition
setup does not process compiler entries in a mapping turn. Existing legacy Fill
behavior remains available. The admission receipt retains preparation and mapper
generations. Every step and final `canSelectFrozenCaptureBridge` preflight checks
live preparation readiness, including after compilation. Final selection retains
existing source/coverage/cursor/ledger checks. A command change, discarded rows,
or mapper reuse hides the stale admission even without another maintenance turn.

Discard resets only the unpublished candidate and the mapper generation actually
borrowed. A reused mapper and a selected bridge survive cleanup. Complete
selection remains independent of preparation storage after that boundary.
This adapter does not perform atomic canonical transfer, reserve aggregate stop
work, or enable production deferred-stop routing; those gates remain open.

The end-to-end fixture now routes 23 of its cases through runtime admission,
including the four-cycle open-note/capture trace. It verifies preflight rejection
for wrong epoch/generation, post-compile epoch/row/map invalidation, successful
rebuild, preservation of a reused mapper, and selected-bridge cleanup protection.
Focused edit/prewarm **194/194**; final extended prewarm rerun **72/72**. Logs:
`/tmp/resumable-ranked-admission-focused.log` and
`/tmp/resumable-ranked-admission-final-focused.log`. Capture-serial build passes:
FLASH code **921432**, RAM1 code **424184**, padding **1800**, variables **91584**,
locals **6720** (`/tmp/resumable-ranked-admission-build.log`). No upload or HITL.

## End-to-end canonical capture bridge validation

2026-09-14: a new native fixture starts with sealed record chunks and historical
rows, normalizes the baseline, fills/publishes its provenance-owning view, then
normalizes frozen overdub capture and applies prepared companions through the
typed owner handoffs. It maps and compiles a ranked bridge and compares emitted
MIDI with actual `LoopPasses::materializeToEventVector(SessionMidiEventVec&)`
for an equivalent committed capture plus companion history. Preparation owners
are discarded before bridge consumption, proving compiled storage independence.

Twenty-four variants cover Delete/Create history, surviving duplicate NoteIds,
per-pass Hide/Shorten, raw wrapped ticks, equal-key event order, retention, and
capture insertion. A twenty-fifth case starts with a sounding wrapped base note:
over four cycles the trace retains its original Off at phase 100, emits capture
CC/On/Off events, and uses the revised phase-150 Off for later note instances.
Comparison includes cycle-qualified timing and MIDI type/channel/data bytes.

Injected command-epoch changes during mapping and compilation deny unpublished
candidates; complete rebuilding under the new epoch then passes the same oracle.
The capture and published base remain available throughout. This is native
integration evidence for tasks 2.3/2.5, not production atomic-selection or
latency proof; production admission, coalescing, remaining differential scenarios
and HITL remain open. No task checkbox changes or firmware edits in this slice.

Playback-prewarm native suite: **72/72**, including the new 25-case pipeline
fixture (`/tmp/resumable-canonical-bridge-focused.log`). No firmware upload or
HITL performed.

## History membership survives companion preparation

2026-09-14: typed normalized-input consumers now start a continuation batch
with the immutable active history prefix. Previously the companion batch reset
sequence membership: a historical Delete could leave a duplicate On, and a
later companion would wrongly act on that surviving identity. Canonical sequence
membership removes it until a later Create adds a nonzero NoteOn with that ID.
The new bounded prefix scan preserves that rule across the preparation boundary.
History event mutations are not applied twice; only the new companion rows
modify the already normalized events/provenance. The source/input owner guards
continue to bind the borrowed history for pending and completed consumers.

Both flat and selected history prefixes are supported without copying rows or
Create vectors. Every prefix row and each Create payload event consumes quantum;
subsequent companion Deletes participate in the same scan. An empty prefix
retains ordinary companion behavior. Invalid pointer-list entries or metadata
horizons deny the result. This is a parity fix against the approved sequence,
not a new semantic correction.

For C events, H historical rows and R companions, a conservative uint64 apply
bound is `(R+1)*(G(C,Q)+ceil((H*(C+1)+R)/Q)+3)`. For typed per-pass admission, use this
bound in place of the standalone companion term; aggregate production stop
credit/latency proof remains open. No extra buffer allocation is introduced.

Sixteen differential cases compare the entire canonical history+companion
sequence against split preparation for duplicate IDs, historical Delete,
Create reactivation, zero-velocity/nonmatching Create, repeated Delete, Hide
and Shorten. The maximum fixture visits 2048 history rows and a 2048-event
Create payload at Q=1; malformed prefixes and bound arguments are refused.
General combined trace/ledger parity and production atomic admission remain
open under task 2.3.

Full native validation: **2009 passed, 1 skipped**
(`/tmp/resumable-history-membership-native.log`). Focused edit/prewarm: **193/193**. Capture-serial build succeeds
with FLASH code **920632**, RAM1 code **424184**, padding **1800**, variables
**91584**, and **6720** bytes remaining for locals. Strict OpenSpec validation
and `git diff --check` pass. Logs: `/tmp/resumable-history-membership-focused.log`
and `/tmp/resumable-history-membership-build.log`. No upload or HITL performed.

## Frozen capture joins canonical inputs

2026-09-14: `beginFrozenOverdub` includes retained frozen capture as the final,
newest overdub pass after selected sealed passes. It uses two private C-entry
index arrays and `RuntimePlaybackOrderBuilder::beginCanonicalIndexes`, then
copies at most Q events into the existing raw scratch before applying active
history and copying the effective pass. Stable raw-tick ties and retention
filtering match the canonical capture order. No shared staging event, order,
scratch, phase or retention array is changed. Audible array reuse cannot change
the candidate. Empty/fully filtered capture contributes no pass (historical
Create rows therefore cannot resurrect an absent capture pass).

The producer must finish NoteId assignment, wrap closure and retention decisions
before admission, retain frozen content and live owners until discard, and use
this entry only for a capture destined to be the newest overdub. Record commit,
canonical transfer and active deferred-stop routing are not added here.

Extra prewarm is `2*C*sizeof(uint16_t)` (8192 bytes at C=2048), included in
`fixedExternalBytes()`. Capture ordering/copy/history use separate owner turns.
The conservative uint64 grain bound adds
`orderGrains(C,Q)+2*ceil(C/Q)+H(C,R,Q)+4` to the selected-pass bound; final total
pass/event horizons still remain C. Source invalidation and capacity denial
expose no prefix. Aggregate stop credit/latency proof remains open.

Native reference cases stage an equivalent sealed capture into LoopPasses and
compare Session materialization with the typed companion pipeline across 24
combinations of wrapped ticks, stable equal ticks, filtered/empty capture,
Velocity/Create history and Hide companions targeting repeated identities.
Shared audible arrays are overwritten between every normalization grain and
remain untouched by it. Additional fixtures cover 2048 captured events, stale
ordering, input/output capacity denial and invalid grain-bound arguments.
These fixtures do not close general combined-history/companion parity or the
open-ledger bridge gate. Production lifetime enforcement, coalescing, atomic
admission and HITL remain open; task 2.3 is not complete.

Full native validation: **2007 passed, 1 skipped**, logged in
`/tmp/resumable-capture-input-native.log`. Strict OpenSpec validation and
`git diff --check` pass. Focused edit/prewarm **191/191**; capture-serial build succeeds
with FLASH code **920208**, RAM1 code **424184**, padding **1800**, variables
**91584**, and **6720** bytes remaining for locals. Logs:
`/tmp/resumable-capture-input-focused.log`, `/tmp/resumable-capture-input-build.log`.
No upload or HITL performed.

## Prepared companion-row ownership

2026-09-14: the typed `RuntimeCompanionPasses::begin` overload now accepts
`RuntimeCaptureCompanions` alongside normalized inputs. Both must be ready and
bound to the same frozen capture object, not merely equal generation numbers.
It borrows the row owner's preparation generation; discard and successful reuse
advance that generation even within the same capture generation. Every consumer
step, ready/output query and origin lookup validates both owners. Live epoch
invalidation propagates through normalized inputs without another producer turn.

`RuntimeCaptureCompanions::data(generation)` hides incomplete/stale rows. Existing
row/count access remains available for preparation diagnostics; the new typed
handoff obtains data only after readiness. No row copies or event-buffer
allocations are added, and no preparation grain bound changes. Both owners and
the frozen change vector must remain alive and immutable until consumer discard;
same-size in-place changes require producer invalidation before mutation.

The reference fixture applies Hide/Shorten to repeated identities independently
in two sealed passes and compares with canonical per-pass application/merge.
Cancellation fixtures cover before first consumer grain, borrowed apply and
completed output, row discard/reuse with unchanged capture generation, change
vector growth, live epoch change, another capture with equal counters, unbound
inputs and empty inputs/rows.

This connects prepared companion ownership, not capture events themselves.
Canonical capture input ordering, producer lifetime enforcement, aggregate stop
credits, atomic publication, coalescing and HITL remain open under task 2.3.

Validation: focused edit/prewarm **189/189**; capture-serial build succeeds
with FLASH code **919296**, RAM1 code **424184**, padding **1800**, variables
**91584**, and **6720** bytes remaining for locals. Strict OpenSpec validation
and `git diff --check` pass. Logs: `/tmp/resumable-row-owner-focused.log` and
`/tmp/resumable-row-owner-build.log`. No upload or HITL performed.

## Live frozen-source invalidation

2026-09-14: `RuntimeCanonicalPassInputs::beginFrozen` binds references to live
playback revision, playback generation, transport command epoch and loop length,
plus frozen capture staging. It rejects an unfrozen/mismatched capture or one
with outstanding accepted-note closures. Every step and readiness/output query
checks those fields and the captured event count directly. Typed per-pass
consumers inherit the check through the normalizer, including after completion;
no producer maintenance turn is needed to hide a stale result.

The receipt stores references and scalar snapshots only. It does not acquire
chunk ownership, freeze canonical vectors, append capture as a pass, or activate
the pipeline in deferred stop. The owner must retain all borrowed objects and
advance revisions/epochs before mutation or release, without reusing an identity
while preparation is outstanding. Production lifetime enforcement, canonical
capture ordering, aggregate stop credits, atomic admission and HITL remain open.

Begin adds one fixed reservation-array scan; subsequent checks have a fixed
number of scalar comparisons. No additional event visits, grain count or
external allocation is introduced. Native cases interrupt each early grain,
completed normalization, and pending/completed typed consumers for eight live
field changes while leaving the copied source identity unchanged.

Validation: focused edit/prewarm **187/187**; capture-serial build succeeds
with FLASH code **918960**, RAM1 code **424184**, padding **1800**, variables
**91584**, and **6720** bytes remaining for locals. Strict OpenSpec validation
and `git diff --check` pass. Logs: `/tmp/resumable-live-source-focused.log` and
`/tmp/resumable-live-source-build.log`. No upload or HITL performed.

## Direct canonical pass selection and history filtering

2026-09-14: `RuntimeCanonicalPassInputs::begin(..., const LoopPasses&, ...)`
now selects the active nonempty record pass (requiring a valid record id), then
active nonempty overdubs in ascending mergeSequence. Repeated minimum scans
visit at most Q source pass rows per turn. Disabled/empty passes do not enter
the candidate. Active Note history is selected in source order into a fixed
pointer array, avoiding deep copies of Create payload vectors.

`RuntimeCompanionApply::beginSelectedHistoryBatch` borrows that list. Filtering
occurs before contiguous Pitch/Velocity collapse, matching the canonical Session
materializer: skipped rows do not break a run, while retained active no-op Note
updates do. Create and prior-Delete identity scans also use the selected rows.
Default/no-op historical Update properties preserve the reference's no-op
behavior without removing that planning barrier. The caller must retain the
immutable LoopPasses/chunks/history and invalidate source identity before any
mutation/release; production lifetime binding is still required.

Extra metadata prewarm is `C*sizeof(SealedPass)+2048*sizeof(const EditPass*)`,
included in the owner's `fixedExternalBytes()`. Raw overdubs are capped at C,
raw history at 2048. Selection adds
`ceil((C*(C+1)+R)/Q)+C+4` to the earlier preparation bound with P=C; the result
uses uint64. No active sort, vector growth, payload copy or chunk retain/release
is introduced.

Duplicate active mergeSequence values are refused without exposing a prefix or
modifying canonical content: their ordering under the legacy unstable pass sort
is not reinterpreted. Parity for that case remains an explicit open gate. This
does not authorize omission of accepted content or close task 2.3. Capture
generation/lifetime binding, atomic admission, coalescing, aggregate reservation/
timing proof and HITL remain open.

Focused edit/prewarm validation passes **185/185**. Cases compare directly
against `LoopPasses::materializeToEventVector(SessionMidiEventVec&)` across
record eligibility, disabled/empty overdubs, unsorted sequence metadata including
0/UINT32_MAX, filtering before collapse, active no-op barriers, and Create
payloads. Maximum tests cover 2,048 selected passes and 2,048 history rows;
additional cases cover stale selection, duplicate-order denial, null selected
rows and metadata horizon refusal. The shared-chunk maximum fixture retains one
external reference throughout; copying chunk IDs alone does not retain another
reference. Capture-serial build passes: FLASH code **918952**; RAM1 variables
**91584**, code **424184**, padding **1800**, locals **6720**. No upload/HITL.
Final parity adjustment preserves ignored payload fields on non-Create rows;
they are neither copied nor applied. Final FLASH code is **918928**. Full native
validation passes **2001 tests, 1 skipped**, with final focused suites **185/185**.
Strict OpenSpec validation and `git diff --check` pass. Logs:
`/tmp/resumable-pass-selection-native.log`,
`/tmp/resumable-pass-selection-focused.log`,
`/tmp/resumable-pass-selection-build.log`.

## Sealed-pass history input preparation

2026-09-14: `LoopPlaybackRuntime::canonicalPassInputs` prewarms and resets a
`RuntimeCanonicalPassInputs` continuation. Given frozen sealed-pass descriptors
in canonical order and filtered active Note history, it reads each pass in
bounded chunks, borrows the existing history worker, and copies the effective
result into a separate contiguous segment. All pass descriptors stay hidden
until the complete aggregate is ready; no merge happens during normalization.
Create results obtain their effective-pass event positions here before new
companions are applied. Aggregate overflow denies the whole candidate.

The owner checks source identity throughout read/apply/copy and completed-result
access. Worker generation mismatch cancels only work actually borrowed by this
owner, never a newer worker job. The typed `RuntimeCompanionPasses::begin`
overload retains the normalizer generation/readiness through companion
preparation and origin lookup. Restarting/discarding normalization invalidates
pending and completed companion results even if scalar source fields match.

The runtime adds `C*(2*sizeof(MidiEvent)+sizeof(Pass))` external-first prewarm
bytes for raw scratch, effective events and descriptors; no active allocation
or chunk refcount changes occur. C/P/aggregate are capped at 2048, each raw
pass at C, and each chunk-ref list at 512. For positive Q the uint64 bound is
`1+P*(H(C,R,Q)+ceil((C+512)/Q)+ceil(C/Q)+3)`, using the historical apply bound H.

Focused edit/prewarm validation passes **181/181** before the final maximum-size
fixture. Eighty generated two-pass cases compare separate historical results
and subsequent per-pass companion merges against the canonical sequence.
Additional coverage checks aggregate capacity denial without exposing the first
pass, stale reads, active restart refusal, borrowed-worker reuse without
cancelling the newer job, empty input, a 2,048-event/two-pass result preserving
nonmonotonic stored order, loop-length mismatch, and pending/completed consumer
invalidation after normalizer discard. Capture-serial build passes: FLASH code
**917816**; RAM1 variables **91584**, code **424184**, padding **1800**,
locals **6720**. No upload was performed.
Full native validation, including the maximum-size fixture, passes **1998 tests,
1 skipped**. Strict OpenSpec validation and `git diff --check` pass. Logs:
`/tmp/resumable-pass-inputs-focused.log`, `/tmp/resumable-pass-inputs-native.log`,
and `/tmp/resumable-pass-inputs-build.log`.

This connects supplied sealed passes to effective companion inputs. Canonical
pass selection/order, active-history filtering, capture freezing, lifetime
retention/source invalidation in the production producer, atomic transfer,
coalescing, aggregate admission/timing, and HITL remain open. No task checkbox
or device admission is completed by this component.

## Approved Create reference and bounded processing

2026-09-14: native reproduction confirms that Create's tick-only `std::sort`
and the bounded stable sorter differ on a 25-event equal-tick example. The
[Create reassessment](CREATE-HISTORY-REASSESSMENT.md) records the user's explicit
approval of stable raw-tick ordering after append, including empty Create rows,
and the impact on existing histories. DEC-061 is accepted and the reference patch
is applied; DEC-059/060 remain unchanged. Task 2.3 and validation/HITL gates remain
open.

History-mode Create now copies at most Q payload events per append grain, then
uses the existing bounded stable raw-tick order and permutation continuations.
Existing events win equal-tick ties over appended events; no stored Off-before-On
rule is added. Empty Create still sorts. Capacity overflow or source changes
hide the whole result. Created events carry UINT32_MAX provenance; the
companion-only mapper continues refusing history results, which must become
effective baseline input before companion application.

The identity guard scans prior rows and Create payloads in charged Q-bounded
visits. Delete removes an identity even if a duplicate On survives; a later
Create payload's nonzero NoteOn restores it. Zero-velocity Ons/Offs do not
reactivate it. Subsequent lookup can select an older surviving duplicate,
matching the canonical reference rather than forcibly selecting the new event.
No new arrays or active allocations are introduced.

`requiredHistoryGrainsUpperBound(C,R,Q)` uses uint64 and bounds the full batch
by `(R+1)*(G(C,Q)+ceil(R*(C+1)/Q)+ceil(R/Q)+8)`. Every successful prior Create
payload is at most C; its payload visits and row transition are charged in the
history scan. C/R remain capped at 2048. This component bound does not extend
production stop credits or establish aggregate latency.

Focused native edit validation passes **108/108**, including the approved
25-event reference regression (all-created/mixed/empty payload), 300 generated
mixed histories, duplicate-ID Delete/Create/reactivation cycles, zero-velocity
non-reactivation, Q=1 maximum 2,048-event Create, stale partial append, overflow,
empty input, and a bound exceeding uint32 at maximum C/R. Capture-serial build
passes: FLASH code **915320**; RAM1 variables **91584**, code **424104**,
padding **1880**, locals **6720**. The new reference initially exceeded the ITCM
bank boundary by 40 bytes. `applyAddNote` is cold, and a narrowly scoped linker
rule places its generated sort helpers in flash (they do not inherit the
caller annotation), recovering the bank without changing semantics.
Full native validation passes **1995 tests, 1 skipped**; strict OpenSpec
validation and `git diff --check` pass. Logs:
`/tmp/resumable-history-create-focused.log`,
`/tmp/resumable-history-create-native.log`,
`/tmp/resumable-history-create-build.log`. Symbol inspection confirms the
Create sort helpers reside in flash. Canonical pass gathering/history integration,
production capture/atomic admission, aggregate reservation/timing proof, and
HITL remain open. No upload or task-checkbox completion is claimed.

## Bounded historical updates

Prior checkpoint; Create support and its revised bound are superseded by the
approved Create section above. The evidence below describes the earlier update-only run.

2026-09-14: the opt-in `RuntimeCompanionApply::beginHistoryBatch` now accepts
active Note Delete/Length/Pitch/Velocity/NoteRange rows. The ordinary companion
entry point retains its Delete/Length-only contract. History preparation follows
the canonical sequence's contiguous same-target Pitch/Velocity collapse and
prior-Delete identity guard. Collapse planning, lookup, and compaction resume
in bounded quanta; Pitch updates both the LIFO and wrap Off where present.
NoteRange prefers the wrap Off, removes a distinct obsolete LIFO Off, and
synthesizes a missing closure only when prewarmed capacity allows. It preserves
stored order rather than adding a sort after a move. No partial candidate is
exposed on stale source, unsupported row, or capacity denial.

No new event arrays are required. The per-row component bound is G(C,Q) plus
`2*(ceil(R/Q)+2)` for planning/identity scans, with R+1 as the conservative batch
multiplier. This does not reserve aggregate production stop credits. The
companion mapper explicitly refuses history-mode output because surviving
payload changes must become effective baseline content before companion mapping.

Validation: 300 generated mixed-history cases match `applyNoteEditPassSequence`
with repeated identities, zero-velocity updates, inferred/explicit loop length,
and contiguous collapsed rows. Targeted fixtures cover wrap-head plus distinct
LIFO-Off mutation/removal, missing-Off synthesis, a 2,048-row collapse at Q=1,
capacity denial, stale sources, immutable inputs, provenance, Create refusal,
and history-to-companion mapper refusal. Full native **1992 passed, 1 skipped**
(`/tmp/resumable-history-updates-native.log`). Final capture-serial build passes
(`/tmp/resumable-history-updates-build.log`): FLASH code **913968**; RAM1
variables **91584**, code **425192**, padding **792**, locals **6720**. Strict
OpenSpec validation and `git diff --check` pass. No upload/HITL performed.

Create rows remain unsupported, including empty Create payloads. The reference
Create helper uses `std::sort` by tick, so equal-tick behavior must be addressed
before claiming a bounded equivalent. Full history normalization, canonical
pass selection, capture producer binding, atomic admission, aggregate timing,
and HITL remain open; task 2.3 is unchanged. A device run at this stage would
exercise the existing production path rather than these new preparation helpers.

## Bounded raw sealed-pass gathering

2026-09-14: `LoopEventStore::beginReadSealedChunks` and
`stepReadSealedChunks` now copy one pass's sealed chunks into caller-prewarmed
unpublished storage. Reads preserve chunk and event order, including nonmonotonic
ticks; they do not merge passes or apply historical edits. Each Q-bounded turn
charges both event copies and chunk transitions. N events across K references
need `max(1, ceil((N+K)/Q))` calls for positive Q. Begin and discard are constant
time and do not allocate or change chunk ownership.

The caller must retain immutable references, destination storage, and the source
generation token until discard, and invalidate that token before releasing or
replacing references. This includes chunk-pool reuse. The cursor checks the
token before work and completed-result access. Recording/free/invalid chunks,
capacity exhaustion, and generation changes deny the result; no partial count
or data is exposed through the cursor. An active read cannot be restarted by
another begin. The destination must be unpublished and disjoint from sources.

The focused event-store suite passes **34/34**. Added cases cover a 2,048-event,
eight-chunk source at Q=1/7/32/4096, exact completion bounds, nonmonotonic order,
zero-quantum deferral, unchanged reference counts, stale partial/completed reads,
capacity failure, invalid and released refs, live-tail rejection, empty input,
and active-restart refusal. Capture-serial build passes: FLASH code **913128**;
RAM1 variables **91584**, code **425000**, padding **984**, locals **6720**.
Full native validation passes **1989 tests, 1 skipped**
(`/tmp/resumable-sealed-read-native.log`); focused and firmware logs are
`/tmp/resumable-sealed-read-focused.log` and
`/tmp/resumable-sealed-read-build.log`. Strict OpenSpec validation and
`git diff --check` pass.

This is a raw input primitive, not effective canonical gathering or production
admission. Canonical pass selection/order, historical edit normalization,
generation binding in the capture producer, aggregate reservation/timing proof,
atomic transfer/coalescing, combined open-ledger traces, and HITL remain open.
Task 2.3 is unchanged. No firmware upload was performed.

Next input integration must cover the canonical history sequence, including
Pitch, Velocity, NoteRange, and Create rows, as well as Delete/Length, and its
present-identity guard and contiguous-row planning. Feeding raw chunks into the
current Delete/Length-only companion owner would omit that normalization. The
new reader is deliberately below that semantic layer.

## View-owned pass provenance construction

`RuntimePlaybackViewFill::begin(view, baseline, ...)` now constructs the view's
events and pass/event origin table together from a completed baseline pass merge.
It accepts full-loop coverage and a source-matching `RuntimeCompanionPasses`
result with no new edit rows. Inputs must already represent the effective
canonical passes before the new companions; raw chunks with unapplied historical
edits are not interchangeable with those inputs. The fill copies each event and
its origin in the same Q-bounded visit, preserving the baseline's canonical
merge positions without event matching or NoteId inference.

Each prewarmed view owns a C-entry `PlaybackPassEventOrigin` array (two uint16
fields), plus a source-bound readiness receipt. The table becomes ready only
after the whole fill completes. Baseline generation/source changes or unexpected
destination writes deny an incomplete fill. Published views cannot be restarted
as fill destinations. The baseline's buffers can be reused after copying; view
ordering does not move event storage indexes or their origins. Ordinary raw
fills/reset clear the provenance receipt and cannot masquerade as provenance-
backed views.

The preferred per-pass mapper overload now accepts the published view itself.
At begin it requires complete full-loop coverage, a completed event fill, and
an origin receipt matching the view's source identity, before validating origins
against the new per-pass preparation. The old explicit-table overload remains
for component validation; production should use the view-owned table.

The new payload is `C*sizeof(PlaybackPassEventOrigin)` per view: **24,576 bytes**
across three 2,048-event views, included in fixed external-memory accounting.
Provenance copying adds no extra content-sized pass: fill remains at most Q
input events per call, `max(1, ceil(N/Q))` calls including an empty source receipt.
Preparation/fill code is cold; no allocation occurs during copying or ordering.
This does not alter production stop credits or prove aggregate latency.

Focused edit/prewarm suites pass **173/173**. The 180 generated per-pass bridge
fixtures and maximum 2,048-event case now construct baseline provenance through
this fill path rather than a test-generated table. Fixtures cover empty sources,
working-buffer reuse after fill, stale baseline denial, edited-baseline refusal,
partial/windowed refusal, published-view write refusal, and source-receipt
mismatch. Table contents and canonical playback still agree end to end.

Full native validation passes **1986 tests, 1 skipped**
(`/tmp/resumable-origin-fill-native.log`). Capture-serial build passes
(`/tmp/resumable-origin-fill-build.log`): FLASH code **912792 bytes**, RAM1 code
**425000 bytes**, padding **984 bytes**, and **6720 bytes** for locals. Strict
OpenSpec validation and `git diff --check` pass. No live HITL was performed.

This completes table construction from already-prepared effective pass inputs.
Gathering those inputs from canonical chunks and historical edit state, retaining
the corresponding immutable source ownership, binding frozen capture, production
producer/transfer, coalescing, recovery, aggregate admission, combined ledger
trace validation, and HITL remain open under task 2.3. No upload occurred.

## Per-pass provenance mapping into the bridge

`RuntimeCompanionBridgeMap` now accepts a completed per-pass preparation plus
an immutable published-base origin table. Every base table entry names the
original pass ordinal/event index in the frozen input descriptors. Preparation
and base have independently guarded source identities. A per-pass owner
generation counter invalidates mappings after reuse, even when the same source
identity or output buffer address is reused.

The mapper checks every base origin's range and event metadata/payload against
its input event and rejects duplicate base origins. Comparison is field-wise,
not over object padding; SysEx requires identical pointer/length without scanning
payload bytes. This validates the supplied table, not its historical ownership:
the production view/source owner must construct genuine provenance during
preparation, never infer it by matching identical events or NoteIds.

Survivors are matched by pass/event coordinates using bounded scans of that
table. Changed ticks produce Replace, missing base rows produce Suppress, and
surviving input rows outside the published base produce Insert (including
capture notes and non-note events). Synthesized Offs also produce Insert. Final
canonical ranks remain separate from base provenance. The existing ranked
compiler consumes this map, and the cursor matches canonical per-pass playback
in end-to-end native fixtures after mapping/preparation storage is discarded.

Every clear, validation, duplicate-origin comparison, candidate visit, origin
lookup comparison, and suppression visit consumes Q input credits. With B base
rows and A result rows, the conservative visit bound is
`3B + A + B(B-1)/2 + AB`; completion needs at most `ceil(visits/Q)+4` calls.
The per-pass overload adds fixed metadata only and reuses prewarmed map buffers;
no active allocation or canonical mutation occurs. Overflow, invalid provenance,
base/preparation source changes, or preparation reuse hide the entire mapping.
The frozen descriptors and origin table remain valid through compilation;
completed bridges own their records/ranks independently.

Focused edit/prewarm suites pass **171/171**, including 180 generated combined
capture cases through map, compiler, and cursor against canonical per-pass
reference playback. Scope fixtures preserve capture under pass-local Length,
remove repeated identities independently per pass, and synthesize an Off for a
capture-only source. Invalid/duplicate provenance, mismatched event payload,
and stale/reused owners are rejected. Full native validation passes **1985 tests,
1 skipped** (`/tmp/resumable-pass-map-native.log`), including a 2,048-event
combined base/capture result. Strict OpenSpec validation and `git diff --check`
pass.

Capture-serial build passes (`/tmp/resumable-pass-map-build.log`): FLASH code
**912000 bytes**, RAM1 code **425192 bytes**, padding **792 bytes**, and **6720
bytes** for locals. This does not establish production stop latency/admission.
View-owned provenance construction is implemented above. Gathering effective
immutable pass descriptors from the canonical source, binding the frozen capture generation, production
producer/atomic transfer, coalescing, recovery, combined open-ledger MIDI-trace
validation, and HITL remain open under task 2.3. No firmware upload occurred.

## Approved per-pass companion preparation

The user endorsed canonical per-pass application (DEC-060). The delta spec and
design now require each canonical pass, including frozen capture, to resolve
companions independently before the canonical merge. DEC-059 still governs
pairing inside each pass. The earlier scope counterexamples are retained in
[COMPANION-SCOPE-REASSESSMENT.md](COMPANION-SCOPE-REASSESSMENT.md).

`LoopPlaybackRuntime::frozenCompanionPasses` now owns a bounded unpublished
aggregate. Its caller supplies immutable contiguous pass inputs in canonical
record/overdub merge order, the frozen active Note Delete/Length rows, and one
source identity covering that input set. It borrows the existing apply/helpers
for one pass at a time and performs the reference's pairwise stable raw-tick
merge into alternating prewarmed buffers. It does not re-sort a pass after the
reference's shortening branch. Each output event retains its pass ordinal and
original event index; UINT16_MAX marks a synthesized Off's event index.

Per-pass mode preserves the existing `applyNoteEditPassSequence` identity guard:
a Delete prevents later rows for that identity from running in that same pass.
A bounded scan of preceding Delete rows implements this without a dynamic set;
the guard starts afresh for each pass. Existing direct batch mode retains its
sequential individual-resolver behavior. Generated comparisons use the canonical
session sequence independently per pass followed by the actual stable merge.

Each owner call starts a pass, steps one apply child, or merges at most Q events.
No apply and merge work stacks in the same call. Source changes, helper reuse,
unsupported rows, or aggregate capacity refusal hide the entire result. Denial
and cancellation release only the borrowed apply generation; they never cancel
a newer owner's work. Completion releases helpers and leaves source-bound output
readable until the next begin/discard. There is no selected-source publication.

Two C-entry event arrays and two C-entry origin arrays are prewarmed external-
first: `2C * (sizeof(MidiEvent) + sizeof(Origin))` payload bytes, with Origin two
uint16 fields, C <= 2,048. No allocation occurs during stepping. At most 2,048
pass descriptors are admitted, including empty passes. Each pass input and the
aggregate result must fit C; overflow is denial, not truncation.

Let G(C,Q) be the existing single-row apply bound and R the frozen row count.
A conservative P-pass bound is
`1 + P * ((R+1)*(G(C,Q)+ceil(R/Q)+2) + ceil(C/Q) + 4)` calls. The added R term
covers sequence-guard scans; evaluate the aggregate bound in a uint64 counter.
This is not an increase in production stop credits
or proof of acceptable aggregate latency; reservation/latency gates stay open.

Focused edit/prewarm validation passes **167/167**, including 220 generated
per-pass comparisons, scope/identity regressions, overflow, stale-source and
helper-reuse denial. Maximum aggregate/pass-count checks are included in the
full regression run. Full native validation passes **1981 tests, 1 skipped**
(`/tmp/resumable-per-pass-native.log`), including the 2,048-event aggregate and
2,048 empty-pass cases. Capture-serial build passes
(`/tmp/resumable-per-pass-build.log`): FLASH code **910896 bytes**, RAM1 code
**425192 bytes**, padding **792 bytes**, and **6720 bytes** for locals. Strict
OpenSpec validation and `git diff --check` pass. No upload or live HITL occurred.

Pass/event origin mapping now has the component implementation above; constructing
the genuine published-base table, immutable source gathering, producer wiring, atomic
transfer, coalescing, recovery, aggregate admission, and HITL remain task 2.3 work.

## Ranked companion bridge compilation and cursor merge

`RuntimePlaybackBridgeBuild::begin(base, bridge, &mapping)` now consumes the
completed companion map directly. It requires an empty unpublished bridge and
a full-loop immutable base matching the mapper's exact input identity. Clear
and record grains copy the base ranks, records, and insertion ranks into the
bridge's own prewarmed storage. Mapping generation/source changes or unexpected
candidate record writes deny preparation. The mapper and apply owner remain
immutable until completion; the compiled result no longer depends on either.

Source provenance remains the identity used for replacement matching and the
emission cursor. Final canonical ranks independently break equal phase/kind/tick
ties. Both insertion ordering and the two-way merge use those ranks. Unchanged
base events can also change relative order after sequential edits, so a bounded
borrowed-index order pass sorts a bridge-owned base index without writing base
events, order, or phases. Each cycle's filtered base stream derives from that
index. An original Off retained solely for the live ledger keeps its original
index as its tie rank when absent from the final candidate; provenance breaks
rank collisions. Existing cross-wrap closure and safe changed-pair rules remain.

`RuntimePlaybackOrderBuilder::beginIndexes` borrows immutable events/phases and
two caller-owned index arrays; it reuses the existing bounded merge phases.
Its caller supplies disjoint arrays and already prepared matching loop phases.
Mapped compilation adds at most `orderWork(B)` charged operations for B base
entries, plus a constant transition turn. Base-rank copying is included in the
B clear visits and record/rank copying in the R record visits. No second child
is stepped after an ordering child completes in the same call. Prewarmed bridge
storage adds `4C + 8B` bytes (insertion/base uint32 ranks and two base uint16
index arrays), or **49,152 bytes across both bridges** at C=B=2,048. The fixed
external-memory accounting includes this addition; active compilation allocates
nothing. This does not extend or prove production stop credits/latency.

Focused edit/prewarm suites pass **163/163**, including 250 generated six-row
batches compared with actual sequential reference playback, unchanged-base tie
reordering, stale-map denial, source order/phase immutability, post-completion
map/apply disposal, and an original cross-wrap Off retained exactly once.

Full native validation passes **1976 tests, 1 skipped**
(`/tmp/resumable-ranked-native.log`). The capture-serial build passes
(`/tmp/resumable-ranked-build.log`): FLASH code **908584 bytes**, RAM1 code
**425048 bytes**, padding **936 bytes**, and **6720 bytes** for locals. Strict
OpenSpec validation and `git diff --check` pass. No upload or live HITL occurred.

The ranked compiler is available to the preparation owner. Combining this map
with frozen capture insertions and routing the complete producer through the
production stop/atomic transfer path remain open. Task 2.3 also retains aggregate
admission, coalescing, recovery, and HITL gates; no selected-source or stop-path
publication is added by this component.

## Bounded companion bridge mapping

`LoopPlaybackRuntime::frozenCompanionBridgeMap` now prepares insert/suppress/
replace records from a completed `RuntimeCompanionApply` candidate. It requires
that exact immutable base pointer/count/source identity; provenance is never
rediscovered from NoteIds. Surviving rows whose ticks changed become Replace,
missing base rows become Suppress, and synthesized Offs become Insert. Unchanged
rows produce no record. The typed Delete/Length apply owner guarantees unchanged
payloads on surviving rows, so mapping compares only ticks.

Original source provenance and final canonical position are stored separately.
The mapper exposes a rank for each surviving base row and each Insert/Replace
record; removed base rows and Suppress records have rank UINT16_MAX. This is
necessary even for unchanged rows: a Length batch can first sort Off@200 ahead
of Off@300, then shorten the latter to 200, preserving that new tie order. A
regression fixture proves base index 3 must precede index 1 in the final result.
This is preservation of sequential reference behavior, not another semantic
correction to DEC-059.

The mapping's records and rank arrays stay hidden until completion. Source
invalidation, apply cancellation/restart, or capacity denial hides the whole
result. An apply generation counter detects reuse even when source identity,
output pointer, and output count match the old completed candidate. Callers
must retain the immutable base and completed apply owner throughout mapping
and consumption. No canonical or selected playback storage is modified.

Three passes clear B base ranks, visit A candidate rows, and scan B base rows
for suppression. Each `step(Q)` charges every visited row, including unchanged
rows, against Q; completion needs at most `ceil((2B+A)/Q)+2` calls. Storage is
prewarmed external-first: C records and two C-entry uint16 rank arrays,
`C * (sizeof(PlaybackBridgeRecord) + 4)` payload bytes, C <= 2,048. No active
allocation occurs. Capacity denial is not permission to omit accepted content.

Full native validation passes **1972 tests, 1 skipped**
(`/tmp/resumable-map-native.log`); focused edit/prewarm passed **159/159**.
Mapping reconstruction now
runs against the 900 single-application and 400 batch differential fixtures,
plus the maximum 2,048-event candidate and explicit ordering/lifetime guards.
The capture-serial build passes with RAM1 code **424984 bytes**, padding
**1000 bytes**, and **6720 bytes** for locals; FLASH code is **907432 bytes**.
Build log: `/tmp/resumable-map-build.log`. Strict OpenSpec validation and
`git diff --check` pass. No upload or live HITL was performed.

This mapping is consumed with its rank arrays by the ranked compiler above.
Feeding only its record pointer to the legacy capture fill is insufficient.
Combined capture ordering, aggregate credits, production transfer, coalescing,
recovery, and HITL remain open under task 2.3.

## Bounded companion application and batches

`LoopPlaybackRuntime::frozenCompanionApply` now owns a fixed unpublished event
candidate and original-source provenance. `RuntimeCompanionApply` copies the
immutable source before mutation and applies either one Delete/Length row or a
frozen batch of canonical Note Delete/Update-Length rows. Batch rows execute
sequentially on that candidate; intermediate row results remain private.
Unsupported rows, capacity refusal, or stale source identity deny the entire
candidate without exposing a prefix or modifying the source.

Length preserves first-NoteId targeting, loop-length inference, missing-Off /
equal-end no-ops, and the shortening branch's unchanged vector order. Lengthen
runs complete pitch reconstruction and the held overlap actions, performing all
shortenings before deletions. Each action uses the bounded pairing/Delete
lookup against the candidate's current ticks and retention mask. Finalization
compacts retained rows, refreshes the target, updates or synthesizes the Off,
and applies the reference's stable raw-tick order. The existing order builder
prepares indexes; bounded permutation cycles move events/provenance in place.
The legacy `orderSamePitchNoteOffsForLifo` helper copies rows back to their own
indexes and has no net event mutation; this implementation preserves that
actual result, with an explicit regression fixture, rather than introducing
another semantic correction.

Every owner call runs exactly one bounded phase/child. Copy, find, compaction,
retention writes, and permutation scans consume at most Q input slots; nested
reconstruction, overlap, pair/Delete, and ordering steps retain their declared
Q bounds. Child completion only sets up the next phase. Existing helper state
must be available before borrowing it. Denial/cancellation releases only that
borrowed work and retains prewarmed storage. Output and provenance pointers
are exposed only after the entire operation/batch completes with matching
revision/generation/snapshot identity. Input aliasing this instance's candidate
is refused before mutation. Completed output is valid until the next successful
begin/discard and is not an audible published view.

The candidate uses another fixed event/order storage of capacity 2,048 plus
2,048 uint32 provenance entries: `C * (sizeof(MidiEvent) + 13)` prewarmed payload
bytes in total, allocated external-first. No growth or allocation occurs during
apply. A synthesized Off has provenance UINT32_MAX; every other surviving row
retains its original base index across all batch edits, compaction, and sorting.
This makes final insert/suppress/replace mapping possible without rediscovering
row identity from NoteIds.

`requiredGrainsUpperBound(C,Q)` is a conservative per-row component bound:
`ceil((32C² + 64C + orderWork(C))/Q) + 32C + 64`, for C <= 2,048 and Q > 0.
It includes child-rounding and transition/action turns. A batch of R rows can
use `(R+1)` times that bound, evaluated in a sufficiently wide counter. These
are preparation bounds, not an increase to the production stop reservation or
proof of acceptable aggregate stop latency. That admission work remains open.

Full native regression passes **1970 tests, 1 skipped**, including the added
maximum-capacity checks. Differential fixtures compare 900 single applications and 400 eight-row
batches with actual sequential reference application, including repeated IDs,
non-note rows, inferred lengths, wrap cases, synthesis, stable order, source
preservation, and provenance. Fixtures also cover late batch denial, source
invalidation, output alias refusal, helper exclusion, and a 2,048-event candidate.

Validation logs: `/tmp/resumable-companion-apply-native.log` and
`/tmp/resumable-companion-apply-build.log`. The capture-serial firmware build
passes with RAM1 code **424856 bytes**, padding **1128 bytes**, and **6720 bytes**
remaining for locals; FLASH code is **905784 bytes**. Host serial timing fixtures
pass **22/22**; strict OpenSpec validation and `git diff --check` pass. No firmware
upload or live HITL was performed.

Task 2.3 remains open for mapping the completed companion candidate into the
immutable bridge, reserving the full batch before accepted input, production
stop-owner integration, atomic canonical/audible transfer, coalescing, recovery,
and live HITL. This component changes no canonical passes or selected playback
source and is not yet invoked by the stop state machine.

## Complete bounded pitch reconstruction

`LoopPlaybackRuntime` now owns `frozenCompanionSpans` and
`frozenCompanionReconstruction` alongside the existing wrap query and overlap
walk. `RuntimeCompanionSpanWalk` reproduces ordinary pitch-local canonical span
reconstruction with prewarmed per-event active/consumed/wrapped flags. It retains
the reference's pitch stack across channels, equal-tick On consumption, wrapped
tail suppression, deferred loop-end Off behavior, leftover Off@0 processing
(including uint8 counter behavior), and final open-note order. A produced span
is held until consumed. Every source/flag inspection and nested wrap-query
visit is charged to the same caller quantum.

`RuntimeCompanionReconstruction` consumes those spans, projects into the full
loop, and deduplicates by pitch/start/end while preserving the first projected
row's identity, velocity, and original order. Span starts are in-loop, so valid
positive spans use the reference's storage-aligned projection directly; no
shifted-interval vector is required. The existing wrap-head visibility helper
remains the head-split reference. The supported loop length is nonzero and
representable by the canonical signed interval type.

Output is prewarmed external-first storage for 4,096 DisplayNotes, with no
active-path allocation. Span flags add 2,048 bytes per runtime; output adds
`4,096 * sizeof(DisplayNote)` (65,536 bytes for the current layout). This is
separate from view-set and canonical-companion storage. Reset retains backing
storage. Complete output remains immutable until the owner begins another
reconstruction. Source identity is bound by revision, generation, and snapshot
pointer, checked each step and on readiness. Stale identity and capacity denial
latch failure and expose no prefix; `data()` is null and `size()` is zero until
the full result is ready. Source events and retention remain caller-immutable.

For N input rows (N <= 2,048), the span walk has a conservative bound of
`6N² + 4N` input/flag visits, with at most N spans. Projection and dedupe bring
complete reconstruction to at most `10N² + 7N` charged operations, and at most
`ceil((10N² + 7N)/Q) + 1` calls for nonzero Q. Each call still enforces Q across
child work. These are component operation bounds, **not** proof that the
existing stop reservation or latency limit can admit a maximum-size Length
edit. Aggregate stop credits/latency and source-admission proof remain open;
this continuation is not yet selected by the production stop owner.

Focused edit/prewarm suites pass **150/150**. Seven hundred generated fixtures
compare raw canonical spans and another 700 compare complete pitch DisplayNote
output with the actual reference, across retained rows, multiple channels,
repeated IDs, zero-velocity Ons, equal ticks, wrap and beyond-loop Offs, and
open notes. Maximum 2,048-event unique/duplicate fixtures exercise bounded
projection/dedupe; stale identity, capacity denial, restart refusal, held spans,
cancellation, and retained backing storage are covered.

Final validation: full native **1,963 passed, 1 skipped**; host timing/call-site
fixtures **22/22**; strict OpenSpec validation and diff whitespace checks pass.
Capture-serial build passes with RAM1 code **424,840**, padding **1,144**,
variables **91,584**, locals **6,720**; RAM2 variables **50,080**. No upload/HITL.

Task 2.3 remains open for bounded execution of selected Length actions and final
event ordering, companion bridge mapping, aggregate stop admission, combined
canonical/audible transfer, coalescing, recovery, and live HITL. The complete
pitch reconstruction component does not publish canonical or audible state.

## Bounded reconstruction wrap queries

`RuntimeNotePairLookup::beginReconstructionWrap` now supplies the distinct
reconstruction wrap query without reusing Delete's less restrictive wrap rule.
It supports automatic tail selection (`tryPairWrappedTailOn`, ordinary
reconstruction mode) and an explicit On check
(`isPreferredWrapTailForHeadOffInSpanBuild`). On success it also returns the
occupied head end (`occupiedWrapHeadOffTick`). The owner retains immutable
source events and any retention mask throughout the lookup.

The On pass finds the latest in-loop On in the Off's channel/pitch lane,
preserving the first On for equal ticks, detects the automatic-mode mid-loop
blocker, and finds possible head occupancy. A preferred tail cannot have a
later in-loop On in that lane. Once that condition is proved, the nested linear
closer test reduces to one Off pass: a normalized Off after the tail, except
loopLength-1, defeats reconstruction wrapping. That pass also counts equal-head
Off phases for the occupied-head adjustment. Explicit On checks retain the
reference's pitch-local/cross-channel stack behavior; only the automatic mode
uses the mid-loop blocker.

No source mutation, publication, sort, or additional backing allocation occurs.
The existing prewarmed pair lookup holds continuation. At most `2N` input visits
and `ceil(2N/Q)` non-denied calls complete a query (N <= 2,048, Q > 0).
`lastStepInputCount()` now records visits for all pair-lookup modes. Zero quantum
preserves pending state, and an active lookup refuses restart.

Focused edit/prewarm suites pass **144/144**. Generated differential fixtures
exercise 300 retained event streams, every retained Off, automatic selection,
and each explicit same-pitch On against the actual reference helpers. Dedicated
fixtures cover real versus synthetic linear closers, mid-loop blockers,
equal-phase head occupancy across cycles, equal-tail tie order, and maximum
capacity at Q=1,3,32 with exact visit counts.

Final validation: full native **1,957 passed, 1 skipped**; host timing/call-site
fixtures **22/22**; strict OpenSpec validation and diff whitespace checks pass.
Capture-serial build passes with RAM1 code **423,960**, padding **2,024**,
variables **91,584**, locals **6,720**; RAM2 variables **50,080**. No upload/HITL.

This bounds reconstruction's nested wrap queries only. The complete pitch
reconstruction continuation still needs active-stack walking, equal-tick
consumption, deferred loop-end handling, span emission/projection and dedupe,
and immutable output storage. The overlap walk and eventual bridge transfer
must consume that completed snapshot. Full stop-grain reservation and task 2.3
remain open; this is not a complete reconstructed candidate or live HITL gate.

## Bounded Length overlap action walk

`LoopPlaybackRuntime::frozenCompanionOverlaps` now retains a
`RuntimeCompanionOverlapWalk` over an immutable reconstructed-note snapshot.
It walks all shortenings before all deletions, preserving snapshot order within
each kind, as the reference Length resolver does. Target NoteId, pitch,
containment, shared-end exception, overlap, wrap normalization, and shortened
end tick follow the existing reference. It uses the existing constant-work
`notesOverlap` predicate without changing that predicate's semantics.

Each turn inspects no more than the input quantum; `lastStepInputCount()`
reports the actual visits and fixtures enforce that bound. Filtered and ignored notes
consume quantum too. A selected action remains pending until the owner consumes
it; repeated steps cannot overwrite it. Zero quantum denies without consuming
an action. The two scans inspect at most 2N rows (N <= 4,096); excluding owner
turns spent applying or holding an action, completion takes at most
`ceil(2N/Q) + A + 1` calls for nonzero Q and A emitted actions. The continuation
allocates no backing storage, mutates no notes, and publishes nothing. The
caller owns the immutable reconstructed snapshot and applies actions to a
separate unpublished candidate.

Focused edit/prewarm suites pass **141/141**. Five hundred generated fixtures
execute the bounded action sequence through the existing edit primitives and
compare the resulting event stream with actual `applyChangeLengthById`,
including wrapped and beyond-loop ends and multiple channels/pitches. Fixtures
also verify action order, pending-action preservation, zero quantum, maximum
4,096-row input at Q=1,3,32, and capacity refusal.

The full-suite run also exposed an existing out-of-bounds test fixture: two
wrap-grid helper copies contained nine Ons and eight Offs. Added a terminal Off@2112
for the ninth On@1920, preserving all eight existing pairs, and compile-time
array-length checks.
This fixes undefined fixture input, with no capture firmware change. All 18
fixture tests pass, including 25 consecutive direct runs after the correction.

Final validation: full native **1,954 passed, 1 skipped**; host timing/call-site
fixtures **22/22**; strict OpenSpec validation and diff whitespace checks pass.
Capture-serial build passes with RAM1 code **423,960**, padding **2,024**,
variables **91,584**, locals **6,720**; RAM2 variables **50,080**. No firmware
upload or live HITL was performed.

This proves the overlap-action traversal only. The tests deliberately use the
existing reconstruction and mutation/order primitives. Bounded reconstructed
snapshot production, execution of selected actions, final Length event update
and order, companion-to-bridge mapping, and production stop transfer remain
required. Task 2.3 and live HITL gates remain open.

## Bounded Delete companion selection

`RuntimeNotePairLookup::beginWrapHead` now matches the separate legacy
`findWrapHeadOffForOnIndex` rule. A first pass finds the latest retained On in
the channel/pitch lane below loop length. If that On is later than the target,
it defeats every otherwise eligible target head; otherwise a second pass
selects the first matching tagged head or the unique preferred head. Heads
normalize modulo loop length. Equal-tail Ons do not defeat each other. The
shared `isHeadTailWrappedPair` predicate retains the wrap-window and half-loop
distance requirements. Every inspected source row consumes quantum, including
filtered rows; at most `2 * sourceCount` visits finish the lookup.

`LoopPlaybackRuntime::frozenCompanionDeleteLookup` uses the existing prewarmed
pair lookup exclusively to prepare original-source On, LIFO-Off, and wrap-Off
indexes. It matches first-NoteId targeting and loop-length inference, supports
retention masks for previous copy-on-write deletions, and exposes readiness only
after all phases finish. It never erases or publishes source rows. One owner
turn performs only one phase: target/inferred-length scan, LIFO lookup, or
wrap-head lookup. The upper bound for N rows and nonzero quantum Q is
`ceil(N/Q) + ceil(6*N/Q) + ceil(2*N/Q)` turns. Zero-quantum denial preserves
progress; discard releases the borrowed lookup and leaves source data intact.
No additional backing allocation is introduced.

Differential fixtures compare 500 generated wrap lookups and 500 retained-row
Delete selections with the actual reference resolver. Maximum 2,048-row tests
cover Q=1,3,32, pending restart refusal, cancellation, and capacity refusal.
Full native passes **1,951 tests, 1 skipped**. Host timing/call-site fixtures
pass **22/22**; strict OpenSpec validation and diff whitespace checks pass.
Final capture-serial firmware build passes: RAM1 code **423,944**, padding
**2,040**, variables **91,584**, locals **6,720**; RAM2 variables **50,080**.
No firmware upload or live HITL was performed.
This completes the Delete *selection component*, not companion bridge record
publication: Length overlap/reconstruction, sequential companion application,
source identity binding, combined canonical/audible transfer, coalescing, and
stop-budget integration remain open. Task 2.3 remains unchecked.

## Accepted pairing reference and bounded lookup

The user explicitly approved the deterministic lane-run semantic correction
(DEC-059). The reference now groups contiguous equal ticks within the filtered
channel/pitch lane, keeping equal ticks separated by a different lane tick in
separate runs. Each run visits Offs before Ons, stable within each kind. LIFO
pairing and wrap fallback are unchanged. Target 0 in the counterexample pairs
with Off index 3 at tick 96. All parity claims use this revised reference.
See [the reassessment](COMPANION-PAIRING-REASSESSMENT.md).

`LoopPlaybackRuntime::frozenCompanionPairLookup` owns a resumable
`findNoteOffForOnIndex` equivalent. Prewarm reserves 10,240 external-first bytes
for 2,048 lane indexes, stack indexes, and paired flags per runtime, separately
from view-set storage. Allocation failure returns false. Every source scan,
run-discovery comparison, Off/On visit, and wrap-fallback visit consumes input
quantum; completion requires at most `ceil(6 * sourceCount / quantum)` turns.
The owner must retain immutable events and retention flags throughout lookup.
Reset retains allocation. No lookup step allocates, sorts, or publishes.

Full native passes **1,947 tests, 1 skipped**; host timing/call-site fixtures
pass **22/22**; strict OpenSpec validation and `git diff --check` pass.
Focused edit/pending-note/prewarm suites pass **266/266**, including 500
generated differential fixtures against the revised reference and maximum-size
quantum bounds. Capture-serial firmware builds with RAM1 code **424,600**,
padding **1,384**, variables **91,584**, locals **6,720**; RAM2 variables
**50,080**. No upload or live HITL. The subsequent continuation above adds the separate Delete wrap-head lookup.
Length overlap rules remain open, and the production stop owner
has not yet wired the complete companion resolution/bridge transfer. Task 2.3
and the listed validation/HITL gates remain open.

## Canonical companion preparation continuation

`LoopPlaybackRuntime::frozenCaptureCompanions` now owns fixed external-first
storage for up to 2,048 canonical companion payloads. The existing runtime
prewarm path allocates it before active transport; allocation failure returns
false. Reset discards preparation metadata while retaining the backing storage.
This adds `2,048 * sizeof(EditPass)` prewarmed bytes per runtime, separate from
`RuntimePlaybackViewSet::fixedExternalBytes()` and its bridge/view arrays.

`RuntimeCaptureCompanions` prepares at most the declared input quantum of frozen
pending changes per turn. Add rows consume quantum but produce no companion;
Hide and Shorten produce Delete and Length edit payloads. Legacy sealing now
uses the same payload mapping. Preparation assigns no canonical pass IDs,
does not alter canonical revisions or passes, and does not clear pending
changes. Readiness validates capture generation, capture row count, and the
frozen change-list identity/count. The caller must keep that list immutable
through preparation; this helper does not itself freeze Loop's pending list.

Native fixtures compare prepared payloads with actual legacy sealing and
verify source preservation, stale generations, capacity refusal, and fixed
backing storage. These prove payload parity, not full audible companion parity:
Length/Delete resolution still needs bounded equivalents of the legacy pair,
wrap, and overlap rules to produce matching bridge records. The combined
canonical/audible transfer and production stop-owner integration remain open.

## Runtime source-admission continuation

`LoopPlaybackRuntime` now owns persistent frozen bridge fill/compile state and
an explicit `PlaybackSourceAdmission`. The admission binds the capture
generation, command epoch, immutable base identity, target identity, and
candidate buffer. Filling and compilation resume in separate owner turns;
zero-quantum denial leaves preparation pending. Source invalidation denies the
candidate while retaining capture and the published base.

`canSelectPreparedFullLoopBridge` and `canSelectFrozenCaptureBridge` provide
read-only publication preflight. `selectFrozenCaptureBridge` rechecks frozen
identity, epoch, projection, coverage, and live ledger before selection.
Discarding preparation preserves a bridge that has already been selected.
A second source requiring coalescing is refused by this preparation API before
it can overwrite the selected bridge; this is not an accepted-input policy.

Native fixtures exercise the runtime pipeline from frozen ordered capture to
prepared bridge, incomplete/stale admission refusals, epoch changes, ledger
changes, and selected-source preservation. The production stop state machine
still needs prepared companion semantics, reserved bridge work, coalescing,
and a cursor-preserving cleanup/transition before it can use this admission
for the combined canonical/audible transfer. No new task is marked complete.

## Frozen-capture preparation continuation

The production stop owner calls each order builder's admission check on every
turn. Those checks previously rejected an active sorter, leaving any multi-grain
capture stuck after its first turn. They now resume only the same frozen
generation, row count, and playback projection. Direct steps and canonical
rewrites also validate the binding. A completed order cannot be reused for a new
generation, canonical/audible writers cannot overlap, and overdub start clears
both bindings.

The shared sorter now charges filtered input rows against its entry quantum.
Its final metadata transitions finish in the same turn as the last entry, so
`requiredGrains()` includes the completion turn. Native coverage executes the
production begin/step sequence through canonical order, store rewrite, and
audible order, with transport Stop injected in each phase; separate fixtures
check the bound at zero through 2,048 rows with multiple quanta and filtered rows.

`RuntimeCaptureBridgeFill` adds bounded copying of frozen capture and already
prepared companion overlay records into an unpublished bridge. It proves whole
output capacity before writing, requires all reserved closures to be present,
checks generation/count/target on each turn, and never clears accepted capture.
The fill is tested through bridge compilation and oracle comparison, including
append-and-stop with no earlier maintenance turn. It is a preparation primitive;
the production stop owner still needs companion semantic preparation, source
coalescing, and the atomic canonical/audible handoff before using it.

## Production consumer continuation

`playCommittedLoopMidi` now recognizes an identity-matched selected full-loop
bridge and reads its immutable base plus overlay through the existing MIDI
send/dedup/ledger policy. It retains both physical cursors between ticks, drains
the old cycle's tail before wrap, and resets the bridge cursor at explicit
playback reanchors. A bridge source revision does not itself force legacy
gather/order work. The stop owner does not yet construct and atomically select
this admission at canonical commit; this is consumer integration, not
end-to-end task 2.3 completion.

`selectPreparedFullLoopBridge` requires complete prepared storage, exact base
and target identity, full coverage, and a matching live ledger summary. Its
refusals leave the selected source unchanged. The compiler now prepares indexes
for admission, the next two cycles, and steady playback. It retains the nearest
original Off exactly once, including phase zero after wrap. Each changed On
starts at its first occurrence at or after that closure; its partner Off follows
that On even if the pair crosses another wrap. Repeated NoteIds are paired by
ordered canonical On/Off traversal, and replacement provenance preserves the
original closing path when an edit changes NoteId or pitch.

The indexes are built in entry quanta and allocated only during prewarm. MIDI
consumption still selects two indexes and does a two-way merge with constant
work per event. Phase checks reject an admission that invalidates the prepared
closure classification; a matching live ledger remains required. Missing
original closures cannot bind in any cycle. This replaces the previous blanket
cross-wrap refusal, but does not by itself connect the capture producer.

Native coverage includes 144 admission/closure/replacement phase combinations,
equal-phase Off-before-On, changed identities, repeated identities, and closure
retirement through steady playback. Expected traces are generated from safe
note instances independently of the prepared indexes and applied to the ledger.

## Implemented and verified

`FixedPlaybackBridgeStorage` now prewarms a filtered base-order index, a
base-retention scratch array, fixed insertion event/order storage, and stable
insertion provenance. `RuntimePlaybackBridgeBuild` compiles Insert, Suppress,
and Replace records in caller-declared entry quanta. The current view remains
immutable throughout preparation. The base source identity qualifies a
Suppress/Replace storage index; replacements retain that index as their
equal-key tie-break, independent of overlay record order.

`RuntimePlaybackBridgeCursor` consumes the prepared indexes with a two-way
merge. Each peek/advance is constant work, including a long run of suppressed
base events. Selection after a completed phase uses two logarithmic searches;
it is not a mid-phase handoff API. A new cycle can replay the same source
events. `RuntimePlaybackBridgeStream` connects that cursor to the existing
`advancePlaybackCursor` send, deduplication, and ledger-only callbacks. The
optional stream-consume callback also runs for interval and jam skips.

Selected or completed bridge records reject append. A paused candidate build
cannot modify the selected stream. Invalid targets, stale identities, and
malformed operations deny preparation. Open-note handling is described above.
Denial is not permission to discard accepted capture.

Task 3.5 extends the existing before/after idle and load-frame MIDI polling
predicates to `OVERDUB_STOPPING`; the input scan checks every track so an
earlier recording track cannot hide a later stopping track. Pending saves
remain excluded even when transport Stop has changed the requested destination
to STOPPED. No MIDI service call site was added.

## Remaining integration

- The production capture-stop owner does not yet create/select these compiled
  bridges. Track's selected-bridge consumer is implemented; capture preparation
  and the canonical/audible atomic handoff remain task 2.3/2.4 work.
- The compiler references one immutable base view. Current/ready long-loop
  bridge coverage, successor promotion, coalesced second-source construction,
  and cycle-qualified promotion proofs remain integration gates.
- Stop finalization still needs its complete semantic, ownership, cleanup,
  recovery, undo/persistence, and latency gates. Do not infer task 2.3 completion
  from the existence of a stream compiler or from the earlier checked build
  and native tasks.

## Validation

- Full `pio test -e native`: **1,943 passed, 1 skipped** after companion payload
  preparation and shared legacy encoding; final prewarm changes are also covered
  by the focused run below.
- Final playback prewarm/bridge, pending-note companion, and playback cursor
  suites: **198/198 passed**.
- `scripts/test_hitl_serial_timing.py`: **22/22 passed**, including the MIDI
  service call-site allowlist.
- Final capture-serial build: RAM1 code **425,864**, variables **91,584**,
  padding **120**, local-variable headroom **6,720** bytes. RAM2 variables
  **50,080** bytes. Flash code **895,488**, data **83,124**, headers **8,520**.

The initial build crossed an ITCM bank boundary. Bounded order and bridge
preparation, selection/reanchor, and ledger-summary methods now use flash,
while per-event MIDI cursor comparison/advance remains hot code. Companion
prewarm initially exceeded the ITCM bank boundary by eight bytes; placing the
deferred frozen-candidate readiness check in flash restored the RAM1 gate. The existing
complete order builder's template sort helpers now share its flash placement.
`fixedExternalBytes()` includes all retained masks and new bridge arrays
(32,768 additional external bytes per prewarmed runtime for the four extra
indexes in each of its two bridge buffers);
previous reporting omitted the existing per-event retained masks. Size results
are not grain-duration or device MIDI-timing evidence. No upload or HITL run
was performed.

## 2026-09-14 protected publication and bounded cleanup continuation

DEC-063 now has a private, prewarmed `TrackUndoPublication` transaction. Redo
entries move to private storage one entry per unit before publication and remain
recoverable. Joint commit publishes canonical capture, its prepared companion
history, the audible bridge and the reserved UndoEntry in one turn. Irreversible
redo release occurs only after that successful boundary; memory-feedback trim
follows dropped-redo pass reclamation. Restore preserves the prepared entry's
reserved buffers. The latest deferred Undo/Redo request is retained and taken
once after finish; production routing/exclusion is still unwired.

`commitStoppedWithUndo` supports the latched STOPPED destination without selecting
a PLAYING bridge. Command-epoch retargeting requires the exact owner and unchanged
frozen/canonical inputs. The production playback entry points now suppress MIDI
emission while STOPPED is latched, while retaining administrative stopping state.

`Loop::DisabledPassReclaim` builds referenced-ID membership and removes disabled
unreferenced passes in bounded units, including chunk and Create-payload release.
It retains stable pass order and invalidates canonical storage once. It preserves
audible revision because only already-disabled content is removed; persistence
and cache integration remain part of the production audit. Run it after prepared
history recycling, which still requires its original storage identity.

`Loop::SessionClose` drains nested companion IDs and event/source-view buffers
before constant final session closure. It requires exclusive protected ownership
and an already-retained session-exit snapshot (or no pending snapshot box). It
never discards a snapshot graph. Tests cover quantum bounds, stale sessions and
retention of pending wrap journals. These helpers are not yet active stop stages.

A focused device regression for the production Stop emission guard is prepared
in [FOCUSED-STOP-HITL.md](FOCUSED-STOP-HITL.md). This is distinct from full feature
HITL: the joint production path, finite bridge completion under ledger movement,
queue/attachment admission, empty-final-capture and remaining task gates stay open.

Validation for this continuation: `pio test -e native` **2,075 passed, 1 skipped**
(2:57); capture-serial build passed with RAM1 code **425,864**, variables
**91,808**, padding **120**, local-variable headroom **6,496** bytes. Flash code
**945,392**, data **83,124**, headers **8,792** bytes. No task checkbox was changed.

## 2026-09-15 prepared stop owner integration

The production `OVERDUB_STOPPING` owner now routes accepted frozen capture
through the existing canonical-prefix/tail workers, prepared companion-history
verification, and the fixed companion bridge compiler before the atomic transfer.
Admission derives bounded ranked, canonical, and recycle credits from the frozen
candidate and rejects a horizon that cannot fit the declared contract. Both
PLAYING and transport-STOP destinations use the joint capture/companion transfer;
STOPPED skips bridge selection but retains the same canonical publication and
deferred recycle ownership. Companion row storage is prewarmed with the shared
playback runtime, so the owner path performs no vector growth.

Validation: focused native playback-prewarm/edit suites **256/256 passed**;
capture-serial build passed with RAM1 local-variable headroom **6,496 bytes**;
firmware upload succeeded. Two transport-Stop HITL attempts were
`INCONCLUSIVE_CAPTURE` because the host runner could not acquire MIDI clock after
the upload reset (zero serial lines and no intervention), so no device acceptance
gate or task checkbox is closed. Commit `6ad9641c` records this implementation
checkpoint; task 2.3 still needs the remaining wrap/coalescing/parity coverage and
an end-to-end device run once the MIDI endpoint is available.

Boot correction (2026-09-15): a full companion-row reservation in the boot-only
runtime prewarm caused the display to remain blank before the first boot line.
The reservation is now limited to `prepareCapture=true` (explicit recording
preparation); boot/load staging remains lightweight. After upload, the Teensy
again emitted OLED initialization and `BOOT,load,start` normally. Commit
`4a5e3064` records the fix.

Admission-bound follow-up (2026-09-15): the first device run after the boot fix
captured a complete serial trace but fell back before `OVERDUB_STOPPING`; the
host fixture issued multiple record transitions and never established its
overdub phase. The ranked preparation estimate now uses the actual admitted
pass horizon instead of the 2,048-pass maximum, keeping normal candidates under
the provisional 6,240-grain ceiling. Focused native coverage remains **256/256**;
the clean transport-stop checkpoint must be rerun from an idle, synchronized
fixture before any 2.3 or 5.3 acceptance decision.
