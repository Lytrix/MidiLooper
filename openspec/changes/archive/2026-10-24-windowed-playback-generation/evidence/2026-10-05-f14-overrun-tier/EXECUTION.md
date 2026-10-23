# F-13 corrected → F-14: the lane's **named overrun row** is not Tier-A

Execution packet, written **before** the edits. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), DEC-108 §13b (the declared
permitted advance and its "operationally reported" exceedance) and task 3.10/6.5's hardware envelope.

## Why this packet exists, and what it corrects

The previous slice's **F-13 was wrong**. It claimed that both `DIAG,win_gap,` and `DIAG,win_gap_overrun,` were missing
from the Tier-A classification, and it proposed adding both to `CaptureLineTier.h`. Reading the code before editing
refuted it:

* `src/Utils/DebugSessionCapture.cpp::isTierATextLine` **already** classifies `DIAG,win_gap,` as Tier-A, and says why it
  lives there rather than in the header: *"that list is an inline header function, so every literal in it is instantiated
  in every including TU, and one more literal of that size was measured to tip this image's DTCM across an ALIGN boundary
  (RAM1 guard: `variables` +4,096 B)"*.
* The classification is already pinned by a production-predicate fixture
  (`test/test_capture_ring/test_capture_ring.cpp:421`).

So the previous message's **F-13 is withdrawn**, and the real gap is narrower: the **named overrun row**
`DIAG,win_gap_overrun,` — the row that reports an *actual* exceedance of the permitted advance — does not start with
`DIAG,win_gap,` (its 13th byte is `_`, not `,`) and is therefore **not** Tier-A. Under transport, at the
timing-critical budget of 8, a non-Tier-A head record is shed **silently by design**: the row that reports the
exceedance is the one dropped in the regime where an exceedance can happen.

## Identity and scope

- **Slice:** record **F-14** and fix it — one Tier-A entry for the overrun family, in the same object file and for the
  same RAM1 reason as its sibling, plus its boundary fixtures and red-proofs. Firmware, one line of classification.
- **Primary files:** `src/Utils/DebugSessionCapture.cpp` (the entry) and
  `test/test_capture_ring/test_capture_ring.cpp` (the fixture), plus the change's evidence.
- **Where the fixture can live (a layering fact found while writing it, recorded rather than worked around):** the tier
  suite asserts through `CaptureLineTier::isTierALine`, the **header** predicate — and the lane's gap families are
  deliberately *not* in that header (the recorded RAM1 reason), so they are invisible to that suite.
  `scripts/native_test_link.py` does not link the production TU into `test_capture_line_tier`, while it does for
  `test_capture_ring`. The pin therefore belongs in the ring suite, which asks the **production** predicate
  (`isTierATextLine`) that the shed path itself consults. A first attempt asserted it in the tier suite; it failed
  exactly as this explains, and was moved rather than forced.
- **Non-goals:** no change to `CaptureLineTier.h` (the RAM1 reason for that is recorded), no change to the flush budget,
  the shed rule, or any lane behaviour; no re-statement of F-12/F-13 beyond the correction; no claim about the device
  beyond what the next run measures.
- **Also in this slice, because the previous slice's device run proved it is needed:** the reused leg's **driver** must
  end with a transport **stop** and an idle-drain window, otherwise the retained Tier-A rows are never delivered to the
  host at all (the ring is retained under transport and the harness stopped reading while the transport was still
  running). That is a host-tooling change to the wrapper.

## Oracle fixed before behaviour edits

| Case | Observation | Expected |
|---|---|---|
| the named overrun row | `isTierATextLine("#CAP,<us>,DIAG,win_gap_overrun,64,3,9,1,2")` | **true** (today: false — the defect) |
| its sibling | `…DIAG,win_gap,…` | true (already, and it must stay true) |
| a longer subtag | `…DIAG,win_gap_overrun_partial,…` / `…win_gap_overrunx,…` / `…win_gapx,…` | false — the boundary is exact |
| the shed path's own question | the **stored record** of an overrun row, via `headRecordIsTierAText()` | true, so the budget-8 flush retains it and the idle drain delivers it |

## Controls and coverage

- Reused seams: `CaptureLineTier::isTierALine`, `isTierATextLine`, `headRecordIsTierAText`, `flushCaptureBuffer(8)/(64)`,
  the ring suite's `captureSerial` recorder, and the lane's own `windowGapOverrunImmediate` emitter.
- **Red-proofs planned:** M1 the entry removed → the tier fixture and the ring fixture fail; M2 the entry widened
  (prefix without the trailing comma) → the "longer subtag" boundary assertions fail.
- **Byte-identical restores** for every mutation, with the hashes recorded.

## Verification recipe

- `pio test -e native` (full) plus the focused `test_capture_line_tier` / `test_capture_ring` suites.
- The capture env and the **qualification env** must both build through the RAM1 guard; the qualification env's
  `variables` must not move (the recorded reason for putting the literal in the `.cpp`).
- `openspec validate … --strict`, `git diff --check`.
- Device: rebuild + flash the qualification image, re-drive the leg **with the post-stop drain**, and read the retained
  lane rows; that run's rows are the evidence, whatever they say.

## Completion record

- [x] Entry added in `DebugSessionCapture.cpp` beside its sibling; the pin lives in `test_capture_ring` (the
  production-predicate suite — the misplacement in `test_capture_line_tier` is recorded, not forced).
- [x] **M1** (entry removed) and **M2** (entry widened) each fail exactly their own assertion, the other 15 green; both
  restored byte-identically (`sha256 6fbe30f665ee3cad…`). M2's first attempt was a syntax error and is reported as such.
- [x] Native **2796 / 2789 succeeded / 7 skipped / 0 failed** (both capture suites PASSED); windowed verifier suite 17/17;
  both capture envs build through the RAM1 guard with byte-identical lines.
- [x] Driver drains after the stop; the retained rows are read — and the **verifier's "under transport" rule was corrected
  to device time** because a retained row is delivered after the stop.
- [x] Corrections recorded (F-13 **withdrawn**, **F-14** recorded and fixed), evidence written, docs updated, committed.
