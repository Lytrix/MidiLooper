# Finding — the pool reports free chunks while capture appends are denied (`pool_alloc`)

**Date:** 2026-10-01
**Image:** hex sha256 `cdab16f4e5f65988a7383b9fd4633d965c76a77c226c73ce462abfa6e7afde3c`
**Run:** `captures/s4b8-repeated-switches-01` (session `session_20261001_134533.log`)
**Came from:** the S4b gate's "across repeated switches" device run. The run never reached its own subject
(see below), but the state it exposed is worth more than the run would have been.

## What the device state was

Track 5 had three materialized takes from this session's legs and seeds (slots 1, 2, 3 — each a 3-bar take with
an overdub), i.e. the change's own working-set pressure on one track.

## What the run then showed (verbatim)

```
[493.282] [DEBUG] [TRACK] Track cleared @ tick 2332 (active_slot=0)
[493.282] [INFO]  MIDI: Clear selected slot 1              <- the scratch clear ran
[495.579] [WARN] [TRACK] LoopEventStore chunk pool exhausted (events=0 chunks=0)
[495.579] [WARN] [TRACK] Capture append failed (pool_alloc) ch=5 note=48
… 40+ further pairs, one per generated note …
#CAP,495579208,append,deny,pool_alloc,320,192,Normal,5,48,32,0
#CAP,495585426,append,deny,pool_alloc,320,192,Normal,5,74,34,0
…
```

and in the same seconds the census reports **free capacity**:

| device time | live | chunks_free | refused (cumulative) |
|---|---|---|---|
| 494.551 s | 8047 KB | **320** | 0/0B |
| 496.551 s | 8046 KB | **320** | 16/328448B |
| 503.552 s | 8047 KB | **320** | 66/328448B |

## The contradiction, stated plainly

* `LoopEventStore::freeChunkCount()` (the census's `chunks_free`) reports **~320 chunks free**;
* the capture-append path reports the pool **exhausted** (`events=0 chunks=0`) and denies every event with
  `append,deny,pool_alloc`;
* the refusals accumulate (`refused` 0 → 66, ~328 KB) while `chunks_free` does not move, and clearing the
  scratch slot did not return capacity either (`chunks_free` was 324 before the clear at 493.282 s and 324 after).

## The mechanism (confirmed in code, not a hypothesis)

`freeChunkCount()` counts **ids**, not backable capacity:

* `LoopEventStoreConfig::POOL_CHUNK_COUNT = 512`, `POOL_CHUNKS_PER_BLOCK = 64` → 8 blocks
  (`include/LoopEventStore.h`).
* `usedChunkCount()` counts `poolUsed_[i]`; `freeChunkCount() = 512 - used` — a pure bitmap over ids.
* `LoopEventStore::allocChunk()` walks ids in order and, for the first unused id, **opens that id's block** with
  `poolAlloc(POOL_CHUNKS_PER_BLOCK * sizeof(EventChunk))` (≈ 64 × 1026 B ≈ **65.7 KB**). If the block cannot be
  backed it returns `UINT16_MAX`, i.e. "a block that cannot be opened denies the chunk exactly as the whole-pool
  allocation failure used to" (RC-MEM-5, the comment on that line).
* `poolAlloc` is `extmem_malloc` — the PSRAM smalloc pool, which the `SESSION_CAPTURE` ring shares (the same
  function carries the ring-overlap guard).

The measured numbers land exactly on that mechanism: `used = 512 - 320 = 192` = **3 open blocks**, and the 320
"free" ids are the **5 blocks that were never opened**. Every append resolves to id 192 → block 3 → a 65.7 KB
`extmem_malloc` → refused → `append,deny,pool_alloc`.

So on this state `chunks_free` reads 320 while **zero further events can be admitted**. The census number means
"ids not in use", never "ids that can still be backed"; the refusal counter (`refused` 0 → 66) and the refused
byte total are the only fields that move, because the refusal happens at block-open time, not at id-in-use time.

This is the change's own subject (S1.6b's "distinguish allocation owner, retained memory and fragmentation",
S2's class budget): the pool's admission is a *PSRAM-arena* question, and one of the change's observability
surfaces answers a different question than the one an operator would read it as answering. Recorded here as a
**device observation with verbatim rows plus its code path**, which is what S1.6b needs as input.

## Consequences for this session's work

* The run could not exercise its subject: the recipe's generated notes were all denied, so no stream note-on
  reached the port and the switch intervention never armed (`intervention.jsonl`:
  `stream_seen: false, armed: false, sent: false` — the runner says so instead of reporting a phantom switch).
* Further content legs on this device state are blocked until the state is recovered. Recovery is not
  "free content" alone: the pool could not back a **new block**, so what has to be recovered is *PSRAM arena
  room*. A re-flash gives a fresh arena (the takes reload from the card and occupy their 3 blocks again, but the
  fresh session ring starts empty), which is the cheapest attempt; freeing content that sits in the already-open
  blocks is the other. Both were taken up in the same session, with the results recorded below.
* **Nothing here weakens S4b.8**, whose evidence was taken on a clean-enough state earlier in the same session
  (`captures/s4b8-leg-03`, with `chunks_free` 338 → 335 and the switch landing as recorded).

## Not claimed

* No owner is identified for the PSRAM pressure itself (which retained consumer leaves the arena unable to back
  a 65.7 KB block) and no defect is declared: the code path above is confirmed, the attribution of the pressure
  is S1.6b's work, for which this is an input.
* The clear gesture at 493.282 s reported success by its own row and still returned no capacity: under this
  mechanism that is expected whenever the freed ids sit in *already-open* blocks while the next needed id sits
  in an unopenable one, but that explanation is not measured here and is not claimed.
