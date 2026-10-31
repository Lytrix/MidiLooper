# Independent review — slice 1 (runtime core)

**Reviewer:** fresh-context agent following [`docs/Agents/reviewer.md`](../../../../docs/Agents/reviewer.md)
(bounded lifecycle review), 2026-10-31. Static review only: it did not build or run tests because a
full native run was in progress. **Verdict:** accept with fixes. All six findings were fixed and
re-verified in the same slice (author's verification below; no second review round).

| # | Sev | Finding | Fix | Verification |
|---|---|---|---|---|
| 1 | Medium | Dense `switch` lookups (`ceilingUs`, `profileName`) are tabulated into `.rodata.CSWTCH.*`, which the generic rule sends to DTCM; the slice-3.9 trap | Linker: `.rodata.CSWTCH*` added to the `ProfileDiagRows.cpp` rule and a new `*RuntimeProfiler.cpp.o(.rodata.str* .rodata.*.str* .rodata.CSWTCH*)` rule | **Confirmed before fixing:** `RuntimeProfiler.cpp.o` `.rodata.CSWTCH.18` 56 B, `ProfileDiagRows.cpp.o` `.rodata.CSWTCH.3` 60 B. **After:** both symbols in FLASH (`0x600e1e04`, `0x600e1dc8`); RAM1 line unchanged |
| 2 | Medium | Coarse boundary test could not tell `>` from `>=` (sticky flag, overwritten `last*`) | Each delta asserted on its own boot | Mutation `>=` → `>` now fails (`Expected 6158278 Was 20`) |
| 3 | Low/Med | A scope before `initAtBoot()` on NOLOAD RAM2 could index the ring with a garbage head | Ring index masked (`static_assert` power of two); overflow-path stack index and tracked-exit depth bounded; `initMagic` rejects cold-boot garbage. The magic alone is **not** enough: a warm reset keeps the previous boot's magic, so bounding every index is the real guard | New cases: cold garbage refused; warm-reset corrupt head writes ring slot 255. Mutation (unmasked head) → `SIGBUS` |
| 4 | Low | Tracked exit while an overflowed child is live went undetected | `overflowDepth != 0` is an order fault; orphaned overflow count cleared | New case; mutation removing the check fails |
| 5 | Low | Reset also cleared `isrRefused`, beyond D8 | Kept across reset (a wiring fault, not a measurement); D8 updated | New case `test_reset_keeps_the_interrupt_refusal_count` |
| 6 | Low | Test gaps: overflowed ring record, long calibration variant observable, inner order-fault count | Added: overflowed breach names no parent and carries the flag; fixture counts `micros()` reads (2 × 256 + 1); `SelfTest.orderFaults == 1` | All in the 21-case suite |

**Reviewer-confirmed correct:** self-time arithmetic (u64 coarse child, clamp at zero), single
attribution of the outermost overflowed scope, depth restoration after a poisoned exit, ring
head/oldest indexing, seq/dropped, seq monotonic across reset, IPSR refusal scope, no calibration
residue, default image unaffected by the header stubs and empty TUs.

**Not verified by the reviewer:** device behavior of the DWT/IPSR reads and the `emitRow` sink (slice 2
device leg, task 3.7).
