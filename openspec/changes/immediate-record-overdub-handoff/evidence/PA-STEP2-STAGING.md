# P-A step 2 — the blocking resource is frozen staging, not the lease

**Finding: an early Stop cannot be admitted while a predecessor finalizes, because a sealed interval keeps
one of the two frozen staging roles. The fix needs a third and fourth role at ~62,004 target bytes each, or a
redesign that lets the sealed range carry what the frozen role carries. C1.4 and C2 do not price this.**

Discovered while starting task 2.2.14 (per-interval finalization lease). No code changed; this file is the
decision input.

## The chain, in order

1. **Two roles per slot.** `RuntimePlaybackViewSet::captureStagings` is `std::array<CaptureAdmissionStaging, 2>`
   (`include/RuntimePlaybackView.h:783`) — one *live*, one *frozen*, swapped by
   `exchangeCaptureStagingRoles()`. The exchange comment states the intent directly: the live role takes the
   job's immutable rows, and "the previous frozen role becomes clean ingress ownership for the next
   interval".
2. **A role owns real storage.** `CaptureAdmissionStaging` holds `FixedPlaybackEventStorage storage`
   (2,048 rows), not a pointer. Measured: 2,612 B of metadata, and `(sizeof(MidiEvent) + 9) * 2048` =
   59,392 B of events — **≈62,004 B per role**, the figure already recorded in
   [custody](CUSTODY.md) as "another existing-style stage".
3. **A seal freezes one role and does not free it.** On Stop, `staging.freeze(generation)` and
   `exchangeCaptureStagingRoles()` hand the frozen role to the job. The job keeps reading it through
   canonical ordering and staging. The *live* role is clean for the successor — which is exactly why a
   successor can **record** after a Stop.
4. **The successor's Stop needs a frozen role that is not free.** With A frozen, B can record but B's Stop has
   no second frozen role to take. That is why `hasPendingDeferredOverdubStop()` refuses: it is not a missing
   predicate, it is a two-role resource ceiling.

So the per-track gate is *correct for the resources the firmware has*. C1.4's "admission receipt per
interval" cannot be satisfied by converting the lease alone — the lease is the third constraint, not the
first.

## What the trace needs

| Trace state | Frozen roles needed | Live | Total roles | Delta vs today |
|---|---:|---:|---:|---:|
| A finalizing (today) | 1 | 1 | 2 | 0 |
| A finalizing, B sealed | 2 | 1 | 3 | **+1 role ≈ +62,004 B** |
| A finalizing, B and C sealed | 3 | 1 | 4 | **+2 roles ≈ +124,008 B** |

Per *capturing* slot. Only one capturing Track is supported (simultaneous-track Record is deferred by owner
decision), so the trace costs ≈124 KB of external memory — about 3 % of the ~4.3 MB free PSRAM measured at
device peak. Affordable, but it is a new active-role allocation, not a free consequence of the receipt.

## Why the contracts missed it

[CUSTODY.md](CUSTODY.md) §"Unresolved terms" listed "independent frozen staging strategy" among the terms
that were **unknown, not zero**. [C1.4](../CONTRACTS.md) then specified per-interval admission without
re-pricing it, and [C2.1](../CONTRACTS.md)'s ledger charges "closure/staging role — the admitted finalizer —
existing `CaptureAdmissionStaging` 2,612 / event stage 59,392 per 2,048 rows — a second concurrent stage is
62,004". That line prices **one** additional stage; the trace needs **two**, and no clause says which.

## The decision this needs

**Option A — add independent frozen roles (accept ≈+124,008 B external per capturing slot).**
Price it in C2.1, extend `captureStagings` to at least 4 roles (or to `kCustodyCapacity`), keep one serialized
canonical writer, and make the frozen role an obligation bit on the receipt so release is per-interval. Then
C1.4 holds as written and the trace is admissible. Cost: ~124 KB external, plus a per-slot role index.

**Option B — make the sealed range carry what the frozen role carries.**
The job needs the frozen role for note reservations and closure credits, not for the event rows (those are in
`rawRange`). Carrying the reservations/credits on the receipt would let a sealed interval release its staging
role at seal, so two roles stay enough. Cost: a deeper change to `PendingStopAdmission` and the closure
credits, with a real risk of weakening the exactly-once closure accounting.

**Option C — serialize stops (today's behaviour).** Refused: it falsifies the product requirement and
DEC-159's trace, and narrowing the trace is explicitly not a permitted response to a measured conflict.

**Recommendation: A**, because it is additive, keeps the closure accounting untouched, and its price is small
against measured external headroom. B is the better long-term shape (it removes a copy boundary) but it
touches the invariant that guarantees exactly-once closure, so it should not be bundled into an admission
change.

## Consequence for the task list

Task 2.2.14 is **blocked on this decision**, not on further analysis. Once one option is chosen, the slice is:
add the roles (A) or move the reservations (B) → convert `captureStopOwner`/canonical output to per-interval
receipts → cut input at Stop → add the row-28 grain. Its oracle is unchanged:
`IMMEDIATE_HANDOFF_CASE=stop` GREEN and `P-A-2` proving three seals coexist.

## Reproduce

```sh
grep -n "captureStagings" include/RuntimePlaybackView.h          # array<..., 2>
grep -n "struct CaptureAdmissionStaging" -A 6 include/RuntimePlaybackView.h
grep -n "Another existing-style stage" openspec/changes/immediate-record-overdub-handoff/evidence/CUSTODY.md
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-staging"   # CaptureAdmissionStaging = 2612 target bytes
```
