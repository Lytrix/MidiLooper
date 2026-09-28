# S4f.18 — private canonical filter compaction

Status: native and firmware-build verified, 2026-09-28. Baseline `75f24e27` on the existing feature branch.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization and DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED: MinimumLengthCursor returns rejected On/Off source ordinals and retains
them until acknowledged. It does not mutate storage. LoopEventStore owns dense
256-row chunks; its existing private ordering cursor already denies shared or
persistence-admitted output. The ordered-source rewrite needs an external whole
source/index array and is unsuitable for Record. `at` scans the directory.
Append rejects ticks beyond MAX_INDEXED_TICKS. Frozen CaptureRange hides its store.
Current production canonical copy is still overdub-only; Record finalizes inline.

G0 PASS: extend LoopEventStore with bounded private filtering, retaining policy in
CaptureIncrementalSanity. No owner transfer, manager, format change or new capacity.
UNKNOWN/open: full Record order/policy composition, role admission and publication.
This slice does not choose a new equal-tick minimum-length tie policy or change
legacy no-removal ordering. Fixtures feed the existing decision cursor valid order.

## G1 contract / transition oracle fixed before firmware edits

Bind an exclusive, dense, unpublished output store. Caller holds it and its cursor
through completion/discard, prohibits other writers, readers and persistence
admission, and never uses live/frozen Capture or committed storage as the target.
Bounded preflight checks all chunks and rows before marking is enabled. Use
UINT32_MAX in the private row tick as a temporary rejection marker; all original
rows must have ticks <= MAX_INDEXED_TICKS, so the marker cannot collide. Invalid
input denies before any mutation. Mark both ordinals only after validating both
accesses; replay is idempotent. An invalid second ordinal must not mark the first.
Reading is by O(1) dense ordinal; a marked row is not readable through the receipt.

After all decisions, finish marking and compact left to right. One work unit reads
one source row and optionally copies it to an earlier destination. Retained row
order and all event fields are exact. After scanning, retire one tail ref per unit,
then refresh one retained chunk's used/first/last metadata per unit. Count and final
last tick become authoritative only at Complete; partial output remains private.
No directory shifts, allocations, per-event bitmap, new store field or whole-take
copy. Cursor is borrowed role control, not resident per slot. Denial/discard does
not restore mutated output; discard the private output, preserving frozen source.

Receipt pins store generation, pool lifetime, expected count and directory size;
its own tail retirement updates expected size. Every accessed chunk rechecks live
ID, unique reference, NotScheduled and expected used shape. Chunk used counts change
only during metadata refresh. Q=0 denies without progress. Each step reports <=Q
constant-work units. Mark/read/finish each perform fixed work independently of N.

## G2 red-team review

PASS WITH TIGHTENING: exclusive preparation is a caller obligation, as for private
ordering; this is not a lock and cannot make published chunks safe to mutate.
- COVERED: stale clear/replacement, pool reuse, append and foreign cursor fail the
  identity/shape receipt; no replacement mutation. Complete also checks identity.
- COVERED: shared and Queued/Writing/Persisted output deny before first mutation;
  late admission denies the next touched chunk. Both marker targets checked first.
- COVERED: retries mark exactly the same rows; no duplicate remove count is used.
- COVERED: long take and every pair removed use constant scratch; one tail pop
  preserves retained directory prefix and reference accounting.
- COVERED: slot/track/transport/clock/undo/display remain outside private storage;
  no production route switches, persistence calls or public pass handoff here.
- TIGHTENING resolved: reject all out-of-domain ticks during preflight, not just
  the marker value. Never publish/serialize intermediate markers or partial counts.
- TIGHTENING resolved: minimum-policy input order remains explicit. Do not claim
  stable tick sorting supplies Off/On/other order or legacy equal-key identity ties.

## Fixtures and verification recipe

Production store and real MinimumLengthCursor, no policy or storage stubs. Compare
exact retained sequence against independent host selection and real legacy helper
for unique-tick inputs. Cases: 0/1/255/256/257/>2048 rows; none/some/all removed;
across-chunk pair; duplicate mark; bad second ordinal; Q=1/7; metadata, pool refs,
directory prefix/capacity; stale owner/pool/append; persistence/shared refusals;
late refusal; frozen source unchanged beside private output. Watchdog O(N+chunks).
Mutations must catch retained-row loss and omitted stale/persistence checks.

Required: focused native fixtures; full `pio test -e native`; capture-serial build
with RAM1 padding >=840; ARM cursor size; strict OpenSpec/diff checks. No device
claim or upload for unconnected owner API. Later integration retains stimulus
STIM-BASE-2-ONE-OD-01@v1 and the always-playing acceptance. Parent remains unchecked.

## Completion record

Ten new owner fixtures pass; final focused filter/minimum/order suites **24/24**.
32 differential cases compare 2,056-row exact inventories against the real legacy
helper using unique ticks (no invented unstable-sort tie oracle). Empty reserved
tails, late target borrowing and late tail persistence admission are also covered.
Frozen 4,096-row source remains unchanged when all private output is removed.
Preflight and compaction work counts equal N+K+2 and N+K+3 respectively, at Q=1/7.
Logs: `/tmp/filter-focused.log`, `/tmp/filter-final-focused-2.log`.

Five mutations rejected by assertions, not compile failures: inverted retention,
removed owner generation, removed pool lifetime, removed persistence guard and
marking the first pair row before validating the second. Source restored before
final checks. `/tmp/filter-mutation-{keep,generation,pool,persistence,partial_pair}.log`.
ARM layout measured from the real firmware compile flags: cursor **56 bytes**,
store **56 bytes** unchanged (`/tmp/filter-layout.o`). No resident field added.
Final review added a check before reading last-chunk metadata at completion. A
sixth counterexample admits that chunk between its refresh and finalization: the
old code incorrectly returned Complete; the added guard denies before changing
the authoritative event count. `/tmp/filter-completion-counterexample.log` records
the failing assertion. Final focused **24/24** passes with the guard restored.
The superseded broad run `/tmp/filter-final-native.log` was stopped; it is not a
full-suite pass. Final full-native/build runs use the `-2.log` evidence paths.
Final capture-serial build PASS (`/tmp/filter-final-build-2.log`): FLASH code
**1,049,000**; RAM1 variables **96,160** / code **392,376** / padding **840** /
locals **34,912**; RAM2 **155,232**, EXTRAM **9,312**. All filter methods are
FLASH-resident in the ARM object; no upload. Strict OpenSpec and diff checks pass.
Final full native PASS: **2,559 passed / 7 skipped**, 2,566 total,
`pio test -e native`, `/tmp/filter-final-native-2.log`. Final focused **24/24**.
Existing unrelated dirty files preserved.

Closeout: runtime state/current work, execution bounds, storage guide and task
evidence updated. Existing-owner implementation under DEC-093/DEC-043, no new
architecture decision, exclusion or shipped deliverable; no decision-log or
deliverable-tracking entry required. No device acceptance or parent checkoff.
Overall OpenSpec remains **50/176** complete. Next is composing Record preparation
(ordering/tail/minimum policy), resource admission and the shared worker/publication
handoff; this slice alone does not enable deferred Record stop.
