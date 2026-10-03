# Slice 3.9 (increment 3) — the receipt's row family is Tier-A, because the transport regime *sheds* what is not

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`; task 3.9 stays open on the items named at the
end. **This increment corrects a claim made by increments 1 and 2 of the same task** — see "The correction" below.

## What was wrong, in one line each

- Increments 1 and 2 recorded the receipt's delivery as **"delayed rather than lost"**, reasoning that "the ring is
  retained by design while a transport runs".
- **Measured:** at a flush budget of **8** — which is exactly the transport regime — the ring **sheds non-Tier-A head
  records silently, by design**. So for a non-Tier-A family the ring fallback is not a delay, it is a **loss**, and a
  loss the run does not report.
- `DIAG,playback_build,` (the window lane's own service rows) **was already Tier-A**; `DIAG,win_readiness` — the new
  receipt family — **was not in `CaptureLineTier`'s list at all**. Nothing compared them, so the shape looked
  inherited while its protection was not.

## The mechanism, measured from the code

| Where | What it does |
|---|---|
| `src/main.cpp` | `SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64)` — the transport flush budget is 8, the idle budget 64 |
| `src/Utils/DebugSessionCapture.cpp` (`flushCaptureBuffer`) | at `maxRecords <= 8` it walks the head and `discardOldestRecord(TimingShed)` while the head is **not** Tier-A, breaking out when the head **is** Tier-A ("leave Tier-A for idle drain") |
| `include/Utils/CaptureLineTier.h` | the class list; `DIAG,playback_build,` present, `DIAG,win_readiness,` absent |
| `src/Utils/DebugSessionCapture.cpp` (`headRecordIsTierAText`) | the shed path's own question: reads at most the **first 47 bytes** of the head record's text and classifies that |

## The fix (two lines of consequence, one of documentation)

- `include/Utils/CaptureLineTier.h`: `DIAG,win_readiness,` is now Tier-A, bounded by `strlen` of its literal like every
  sibling entry, with the reasoning recorded beside it (the same defect class that made `reentry`,
  `canonical_release`, `post_restore_census` and `LIBOBS` Tier-A).
- `include/Utils/DebugSessionCapture.h`: the receipt's own doc comment now states why the classification is
  load-bearing and records the **47-byte prefix limit** of the shed path's classification.
- No behaviour was added to the emitter: the delivery rules stay exactly as increments 1–2 proved them.

## The fixture

| Prior fixture | What it covers now |
|---|---|
| `test_capture_line_tier::test_window_lane_readiness_receipt_is_tier_a_and_its_boundary_is_exact` (new) | the classification for a real row shape; **the exact boundary** (`win_readiness_partial` and `win_readinessx` must NOT classify); the lane's sibling family still classifies |
| `test_capture_ring::test_the_window_readiness_receipt_is_emitted_directly_and_delayed_rather_than_lost` (extended) | half **(C)**, the transport regime: with room 0 the receipt goes on the ring; the **stored record** is classified by the shed path's own question (`headRecordTextPrefix` + `isTierALine`); a **budget-8** flush **retains** it; the idle drain then delivers the row, and the **drained row text** is classified Tier-A by the production predicate — the end-to-end link |

**Measured while doing this:** the stored prefix the shed path sees is
`#CAP,1000000,DIAG,win_readiness,3,5,ready,ready` — i.e. truncated at the 47-byte buffer limit, with the tag ending at
byte 37. So the tag is comfortably inside the limit, and the limit itself is now recorded rather than assumed.

## Red-proofs

| # | Mutation | Observed failure (measured) |
|---|---|---|
| M86 | the Tier-A entry **removed** from `CaptureLineTier.h` | `test_capture_line_tier`: `Expected TRUE Was FALSE` (the positive case); `test_capture_ring`: the stored-prefix classification fails, and the failure message prints the actual prefix — so the fixture says *which* record it classified, not just that something failed |
| M87 | the entry changed to an over-narrow `DIAG,win,` | both suites fail: the comma makes `DIAG,win,` a *different* tag from `DIAG,win_readiness,`, so the near-miss is not swept in |
| M88 | `flushCaptureBuffer`'s `if (headRecordIsTierAText()) break;` → `if (false) break;` (the Tier-A retention removed) | `test_capture_ring`: `Expected 0 to be greater than 0. the timing-critical flush leaves a Tier-A receipt for the idle drain instead of shedding it` — **the retention assertion speaks**, and `test_real_emitters_are_accounted_and_drained` breaks with it |
| M89 | the trailing comma dropped (`DIAG,win_readiness`) | `test_capture_line_tier`: `Expected FALSE Was TRUE` at the **boundary** assertion, while the ring suite stays green — the boundary's own discriminating power, isolated |

Each mutation was applied to the named file at an anchor whose **occurrence count was asserted to be 1**, and restored
**byte-identically**: tier sha256 `432c0b93…e1dbe8`, capture sha256 `ffa08550…93cd16b9` before and after every case.

## A self-inflicted bug, recorded

The first run of half (C) failed with `Expected 0 to be greater than 59`, which reads like "the receipt was shed".
It was **my assertion**: `TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(sCaptureRing.used, 0u, …)` — Unity's order is
`(threshold, actual, message)`, so the check asked "is 0 greater than 59" and never touched the mechanism. The
receipt had been retained all along. The assertion was corrected to `(0u, sCaptureRing.used, …)`, and the
misreading is recorded here because it is exactly the class of error that produces confident false evidence.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_capture_line_tier` | **19/19** (was 18; +1) |
| `pio test -e native -f test_capture_ring` | **16/16** (extended fixture, count unchanged) |
| `pio test -e native` (full) | **2705 cases: 7 skipped, 2698 succeeded, 0 failed** (baseline 2704 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — a **true rebuild** (74.5 s, after clearing `~/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial`); RAM1 line **byte-identical**: `variables 96,192 / ITCM 393,216 (code 392,328 + padding 888) / locals 34,880`, **48 B headroom**, **floor not re-based** |

## The correction (what increments 1–2 now say)

- `evidence/2026-10-02-s39-readiness-receipt-delivery/EVIDENCE.md` and
  `evidence/2026-10-02-s39b-receipt-emission-site/EVIDENCE.md` carry a correction note: "delayed rather than lost" was
  true of the **delivery rules in isolation** (with room → direct; without room → the ring holds it), and **false as a
  claim about the device under transport** until this increment made the family Tier-A. The distinction matters because
  the *reason* for the direct path is stronger than the increment recorded: the fallback was not merely late, it was
  shed — silently.

## Not claimed

- **No device observation.** Every measurement here is native (the ring fixture's substituted `Serial`, the tier
  suite's predicate). The transport regime is reproduced by its declared budget and the ring's own shed path, not by a
  hardware run.
- **The fallback is still not timely.** Tier-A means *retained until the idle drain*, so a receipt that cannot go
  direct still arrives after the transport stops; only its **loss** is removed.
- **Everything else that is non-Tier-A is still shed silently** by the same path. This increment protects one family;
  it does not change the shed policy, and it does not make the shed reportable.
- **The 47-byte prefix limit is unchanged.** A future Tier-A tag whose distinguishing text starts beyond byte 47 would
  never be recognised — recorded in the code comment, not fixed here.
