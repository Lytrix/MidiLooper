# Slice 3.9 (increment 1) — the readiness receipt and its two delivery rules

**Change:** `windowed-playback-generation` · **Task:** 3.9 (**stays open**: the lane's emitting call site and its
cadence bound are the next increment, named below) · **Date:** 2026-10-02 · **Branch:**
`feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.9
**Contract:** design **D10** — "A readiness receipt is emitted where it is produced when the stream has room (the
`persistenceResultImmediate` / `SC_PERSIST_RESULT` shape), keeps the ring as its fallback so it is **delayed rather
than lost**, and diagnostic cadence is bounded through `Utils/TelemetryCadence.h`, so a diagnostic cannot evict the
evidence it explains."

---

## What landed

| File | Change |
|---|---|
| `include/Utils/DebugSessionCapture.h` | `readinessReceiptImmediate(track, slot, readiness, holder, revision, leadTicks)` — declared beside `persistenceResultImmediate` — and the `#ifndef`-guarded `SC_WINDOW_READINESS` macro in **both** regions (the capture form and the `((void)0)` form), so a native suite can substitute its own recorder exactly as the `SC_ACCEPTED_LATCH` pattern does. |
| `src/Utils/DebugSessionCapture.cpp` | The emitter itself, mirroring the precedent: `serialWriteRoom(96)` → direct `Serial.printf`; otherwise `emitCapPrintf` → the ring. Row: `#CAP,<micros>,DIAG,win_readiness,<track>,<slot>,<readiness>,<holder>,<revision>,<leadTicks>`. |
| `test/test_capture_ring/…` | `test_the_window_readiness_receipt_is_emitted_directly_and_delayed_rather_than_lost` — **both** delivery rules. |

**Why the direct path is the point (and it is the precedent's own reason):** a window is prepared *under transport*,
and the ring is **retained by design** while a transport runs, so a receipt enqueued during a leg arrives only after
the transport stops — after the readiness it explains. The precedent's doc records that measurement
(`captures/s4b9-cross-track-18/-19/-20`); this row inherits the same shape for the same reason.

**Why the row carries what it carries:** readiness and its **holder** are what D10 and task 3.5 require (`Track.h`'s
readiness doc: "the holder is rendered by `playbackWindowReadinessHolder` where a row is reported"); `track`/`slot`
locate it (readiness is slot-scoped); `revision` and `leadTicks` are the two figures the receipt *explains* — the
currency it judged and the urgency that decided whether it mattered. The field list beyond the holder is a choice this
slice states rather than a spec text it found (the design names the delivery shape only).

## The fixture

| Half | Assertion |
|---|---|
| **(A) with room** | the row is written **straight to the stream** (`DIAG,win_readiness,3,5,owes_build,window_build_owner,42,768`) and **nothing is enqueued** (`appendsOk == 0`, `used == 0`) — a reader that never flushes still sees the receipt |
| **(B) without room** | nothing is written directly, the ring **holds** it (`used > 0`), and after the room returns a drain **delivers** it (`…,owes_source,published_source,43,0`) — "delayed rather than lost" |

**Half (B) closes a gap that existed for the precedent too:** the only prior fixture for this delivery shape
(`test_the_save_completion_receipt_is_emitted_directly`) asserts the *immediate* path and "nothing was enqueued"; **no
fixture called the emitter with no room**, so the fallback was unproven for that row as well. It is now proven, and the
new fixture sits beside it.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_capture_ring` | **16/16** (was 15; +1 fixture) |
| `pio test -e native -f test_runtime_working_set_ownership` | **69/69** |
| `pio test -e native -f test_midi_service_drain` | **39/39** |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** |
| `pio test -e native` (full) | **2703 cases: 7 skipped, 2696 succeeded, 0 failed** (baseline 2702 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888`, 48 B headroom (the emitter is flash-resident, like the precedent) |

## Red-proofs

| # | Mutation | Observed failure |
|---|---|---|
| M80 | the **ring fallback** is dropped (the row is lost when there is no room) | half (B): `Expected 0 to be greater than 0` — nothing was held |
| M81 | the **direct path** is dropped (the receipt only ever rides the ring) | half (A): `Expected TRUE Was FALSE` — nothing reached the stream |

Both applied to the named file at an anchor whose occurrence count was asserted, and both restored **byte-identically**
(`shasum -a 256 -c`).

## Not claimed

- **Nothing in the lane emits it yet.** This increment lands and proves the *delivery*; the **emitting call site**
  (where readiness is produced — the owner turn's terminal outcomes and the refusal path, which is silent by row today)
  and the **cadence bound** (`TelemetryCadence::shouldLogOccurrence` with a `DMAMEM` counter — no `src/Track/*` consumer
  of that header exists today, and the lane's per-grain rows are ungated) are the **next increment**. So task 3.9's own
  wording — "verify a receipt **produced under transport** reaches the evidence stream and that diagnostic cadence
  cannot evict it" — is **half met**: the delivery rules are proved, the production emission is not yet wired.
- **The row family is a choice, not a spec text:** `DIAG,win_readiness` is new; the design names only the delivery
  shape. If the lane's existing `DIAG,playback_build,<path>` family is preferred for these rows, that is a
  documentation-level change to this slice rather than a behavioural one.
- **No device evidence:** the emitter's behaviour is proved through the capture build's own native seam (a substituted
  `Serial` plus the real ring), not on hardware.
