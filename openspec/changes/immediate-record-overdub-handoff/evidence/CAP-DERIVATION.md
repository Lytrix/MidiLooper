# C2.2 — cap derivation: structural custody capacity vs dynamic body admission

**Status: `kCaptureCustodyCapacity = 5` is derived from measured structural terms. Body admission
stays dynamic and is unchanged. Two pre-flight measurements remain and neither changes `K`.**

This annex closes the C2.2 cap *value* using nothing but already-measured numbers and an existing
owner decision. It does not admit content capacity, widen a budget or bound device timing.

## The distinction that was missing

Two different quantities were being carried under the word "cap", which is why the number never
settled:

| Quantity | What it is | Scope | How it is bounded |
|---|---|---|---|
| **Structural custody capacity `K`** | How many sealed-interval receipts one Track can hold | per Track, pure metadata | **fixed constant**, priced by measurement |
| **Body admission** | Whether the chunk pool can hold the raw bodies, spare tails, canonical output and credits | **global** — 512 chunks shared by every owner and the user's committed library | already enforced dynamically at each admission edge |

`kCaptureCustodyCapacity` is the first. It is **not** a reservation of bodies: fixing `K` does not
claim that `K` intervals fit the pool, and it must not, because the pool is shared with user content
the firmware cannot pre-reserve without capping the library. The pool is asked at each edge and
denies by name (`canSealCaptureRange`, the ranked/canonical/recycle credit computations,
`freeChunkCount`, `custody_cap_denied`).

Conversely an *unset* `K` leaves the slab size undefined, which is precisely what C1 needs to be
concrete. So the two tiers are separate on purpose.

## Measured inputs

| Input | Value | Source |
|---|---:|---|
| `CaptureCustodyReceipt` | 120 target bytes | [price annex](CONTRACT-PRICE.md) |
| Custody slab, K = 4 / K = 5 | 488 / 608 target bytes | [price annex](CONTRACT-PRICE.md) |
| `CHUNK_CAPACITY` | 256 events per chunk | `include/LoopEventStore.h:17` |
| `POOL_CHUNK_COUNT` | 512 chunks | `include/LoopEventStore.h:18` |
| `POOL_CHUNKS_PER_BLOCK` | 64 → 8 blocks | `include/LoopEventStore.h:25` |
| `EventChunk` | 5,208 target bytes | shipping ARM probe |
| Pool backing | 333,312 bytes/block, 2,666,496 total | 64 × 5,208 and 512 × 5,208 |
| `ACTIVE_OVERDUB_TRANSFER_CHUNKS` | 8 canonical credits | `include/LoopPasses.h:90` |
| Per armed-range directory | 1,024 requested bytes | [custody](CUSTODY.md) |
| Observed seal trace | A257 → 2, B1 → 1, C1 → 1, live 1 = **5 chunks** | [custody](CUSTODY.md) |

## `K = 5`

- **Trace requirement `K_trace = 4`:** A, B and C sealed plus the next Stop's destination receipt.
- **`+1` for bounded retirement overlap:** a receipt still retiring (C1.5 obligations outstanding)
  while the next interval seals. This is a receipt *slot*, not a second reader — a reader shares the
  receipt whose `kReader` bit it holds.
- **`K = 5` is `K_reader`.** At 608 target bytes per Track the slab costs +4,864 bytes across 8 tracks,
  which the price annex shows is not the binding tier.
- **Directories:** 5 armed ranges × 1,024 requested bytes = 5,120 bytes, also non-binding.

So `kCaptureCustodyCapacity = min(K_max, K_product) = 5`: the metadata and directory tiers pass at
`K = 5` by two orders of magnitude, and the chunk tier is not asked at compile time at all.

## Body arithmetic — why `K` reserves nothing

Worst-case resident chunks for one capturing Track at `K = 5`:

| Shape | Sealed bodies | Spare tail | Live producer | Canonical | Total |
|---|---:|---:|---:|---:|---:|
| trace density (257 rows/interval) | 4 × 2 = 8 | 1 | 1 | +8 | **18** |
| dense 33-bar (1,056 events/interval) | 4 × 5 = 20 | 1 | 1 | +8 | **30** |

`R_rec` is declared in **chunks**, because that is the unit the pool denies in:

```text
R_rec = K × 1 chunk        // one re-arm grain plus one retirement grain per admitted receipt
      = 5 chunks
      = 26,040 target bytes
```

`R_rec` covers **one capturing Track**. That is not a new product restriction: simultaneous-track
Record is already *deferred by owner decision (2026-10-24) as a future feature*
(`docs/Runtime/PROJECT_STATE.md`), so the supported concurrency is one capturing Track with another
enduring playback/retirement.

At the dense worst case the capturing Track holds 30 + 5 = 35 chunks, leaving **477 of 512** for
committed library content and every other owner. That headroom is why admission stays dynamic: the
remaining 477 chunks are the user's library, and no compile-time cap may consume them.

## What this closes, and what it does not

**Closes (C2.2 cap value):** `K = 5`, `R_rec = 5 chunks`, with both derived from measured terms and
the existing concurrency decision. The `K_max` computation no longer needs a policy choice, because
its two measurable tiers are satisfied by a wide margin and its chunk tier is explicitly a runtime
admission rather than a compile-time reservation.

**Does not close — pre-flight, and none of it changes `K`:**

- `RAM1` stack/call-frame high-water for the retirement lane (the 720-byte `CaptureStopJob` built on
  the stack). This needs the slab declared in an image, so it belongs to the first packet's slice.
- target allocator execution/fragmentation for the larger ring object.
- roots/undo/visual/persistence graph occupancy beyond wrapper `sizeof` (`UndoEntry` itself is now
  priced at +12 in the [price annex](CONTRACT-PRICE.md)).

**If the pre-flight finds RAM1 pressure, the response is to reduce how many Tracks prewarm custody —
never to narrow the A/B/C/next-Stop trace or cut the `K = 5` overlap.** That ordering is the
contract's, not this annex's.

## Reproducing the arithmetic

```sh
python3 - <<'PY'
cap, pool, chunk = 256, 512, 5208
b = lambda rows: -(-rows // cap)
assert (b(257), b(1), b(1056)) == (2, 1, 5)
assert 477 == pool - (4 * b(1056) + 2 + 8) - 5, "chunks left after dense worst + R_rec"
assert 4 * 2 + 2 + 8 == 18 and 4 * 5 + 2 + 8 == 30
print(b(257) + 1 + 1 + 1, 4 * 2 + 2 + 8, 4 * 5 + 2 + 8, 5 * chunk)
PY
```

Yields `5 18 30 26040` — the observed seal trace, the trace-density envelope, the dense envelope and
`R_rec` in bytes. No firmware, build, device or SD action; the constants are the headers and the
measured probe output, not new instrumentation.

## Capacity bump (owner request, 2026-10-25)

Owner raised the serialized-overdub envelope: FIFO **4 -> 8**, custody **K 5 -> 9**. Measured and
re-derived rather than asserted:

| Term | Before | After | Evidence |
|---|---:|---:|---|
| `CaptureStopJobs::kCapacity` | 4 | **8** | measured ring 29,632 -> **33,088** target (+4 x 744 job + 4 x 120 receipt) |
| `kCustodyCapacity` | 5 | **9** | same object; `K_trace = 8`, `K_reader = 9` |
| `kCaptureStagingRoles` | 6 | **10** | `RuntimePlaybackViewSet` 43,696 -> **54,144** (+4 x 2,612 metadata); 620,040 requested when fully prewarmed, released to 124,008 |
| `kSessionExitReceiptCapacity` | 4 | **8** | the actual binder at four successors; `sizeof(Loop)` unchanged at 1,848 |
| `R_rec` | 5 chunks (26,040 B) | **9 chunks (46,872 B)** | `R_rec = K x 1 chunk` |

Body condition at the dense worst case: 8 sealed x 5 + live 1 + canonical 8 + `R_rec` 9 = **58 of 512**
chunks, leaving 454 for the committed library. The FIFO/K bump alone did **not** widen the envelope: the
four-successor wall was the session-exit receipt ring, which is why it was raised in the same slice.
