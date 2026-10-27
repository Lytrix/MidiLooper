# Production packets — index

Four G2 packets instantiate [the contracts](../CONTRACTS.md). All four are specified with their named RED
demonstrated, and **P-A step 1 is applied and verified**; **task 2.2 — production packet generation is
complete**. Everything else is unimplemented.

| Packet | Scope | Contracts | State |
|---|---|---|---|
| **[P-A — custody, admission and the early-Stop input cut](P-A-CUSTODY.md)** | receipt/slab, interval-qualified admission, input cut at Stop, per-obligation release, C2.4 pre-flight | C1.1–C1.7, C2.1–C2.5 | **step 1 applied and verified**; step 2 = per-interval lease conversion |
| **[P-B — private-source preparation and immediate ingress](P-B-SOURCE.md)** | non-committed `PrivatePredecessor` origin, 32-unit cursor, single wakeup, semantic deadline | C3.1–C3.6 | specified; `P-B-1` RED demonstrated |
| **[P-C — successor-safe finalization, undo and empty cleanup](P-C-FINALIZATION.md)** | predecessor-local cleanup, scoped undo erasure, per-receipt publication | C4.1–C4.7 | specified; `P-C-1` RED demonstrated; authorised by **DEC-160** |
| **[P-D — bounded visual continuation](P-D-VISUAL.md)** | attempt order, production-qualified prepared-index producer, row reclassification | C5.1–C5.5 | specified; `P-D-1` RED demonstrated; **step 1 attempted and reverted — needs C5.2a** |

## P-C authorisation

P-C was withheld until the undo/lifetime semantics it implements had an authority record, because the
cost of being wrong there is a lifetime bug in the one area — accepted successor content — the whole
change exists to protect. **DEC-160** now records the owner's ratification: the predecessor-local
successor predicate, `UndoEntry::interval` (**+12** measured), the causally scoped erasure rule, and the
rule that an empty predecessor cannot reset a successor's geometry.

## Ordering and independence

```text
P-A  (custody + cap pre-flight)
 |
 +-> P-B  (private source; needs a receipt to hang the cursor on)
 |
 +-> P-D  (visual; independent of P-A/P-B, may run in parallel)
 |
P-C  (finalization/undo; needs P-A's receipt; DEC-160 ratified)
```

P-D has no dependency on P-A: the visual cache does not touch capture custody. It may be sliced first
if the shallowest risk reduction is wanted, because its producer is already implemented and only its
wakeup is missing.

## Shared rules for all four

- One commit per independently buildable step inside a packet; firmware commits stay on the working
  branch, never `dev`.
- An invariant that fails is returned as a measured conflict — never answered by narrowing the
  A/B/C/next-Stop trace, lowering `K = 5`, or deleting a guard.
- Native logical proof, shipping build/RAM1 and device timing stay three separate claims. No packet
  claims device timing from native counts.
- No packet authorizes upload, clear, or SD content operations.
