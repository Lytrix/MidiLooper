# Ordering constraint found: the worker cannot publish into the span cache yet

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Status:** finding recorded; a candidate change was **reverted** on evidence.

## What was tried

The natural next step after the worker landed was to make its work durable: on the grain that completes a
tile, merge that tile's candidate notes into `overdubSourceSpanCacheNotes_` (replacing the tile's slice,
deduped by identity) and set the prepared bit on the same grain. That would make the cached content
tile-authoritative and let the view's cut become correct for prepared tiles.

## What it broke

`test_predecessor_publication_mid_scan_discards_the_private_prefix` failed with *"the admission deadline
must decide against the committed origin"* — a fixture about the **private-predecessor scan**, not about
coverage at all.

The cause is a single-owner contract, already written down in the code and in C3.1/C3.5:

> a private-predecessor scan owns the span cache from its first grain until its deferred holds are
> materialized, and a committed origin must never be mixed into it in either direction.

The worker publishing a committed tile over the scan's staged prefix is exactly the mixing those clauses
forbid. Adding a guard (`privatePredecessorSourcePending()` → the worker yields, tile stays owed) was
necessary but **not sufficient**: the fixture still failed, because the moving-window producer writes the
same cache on the same lane, so the cache has *two* producers and the scan's ownership cannot be
established by the worker alone.

## Decision

**Reverted the publication.** The worker now advances coverage and publishes only the **prepared bit**;
it does not write the span cache. The guard that makes it yield to a private-predecessor scan is kept,
because it is correct independently.

The ordering this establishes:

1. **Retire the moving-window producer** (task 4.8) so the span cache has exactly one producer.
2. **Then** the worker becomes that producer, publishing tile content together with its prepared bit on
   the completing grain.
3. Only then can the view's reuse predicate become "every tile of the window is prepared".

Doing 2 before 1 would mean two producers of a single-owner cache, which is the defect the fixture caught.

## Verification after the revert

| check | result |
|---|---|
| `test_immediate_capture_handoff` | **21/21** (the private-prefix fixture green again) |
| full `pio test -e native` | **3693 cases / 7 skipped / 3686 succeeded / 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**, RAM1 `96,192`, ITCM `392,120 + 1,096` |

`RIAO-4.7` still proves the working chain: a real overdub session marks a tile, the production idle lane
runs, and the owed tile becomes **prepared** (`owedCount() == 0`, `preparedCount() > 0`).

## Why this is worth recording rather than working around

The tempting workaround — publish only when the scan is idle — leaves the cache dual-producer and makes
correctness depend on turn ordering between two lanes. The fixture that failed is about a *different*
subsystem, which is exactly the kind of coupling that would have surfaced later as a lost private prefix
on device rather than as a red suite. The ordering above is cheaper than debugging that.
