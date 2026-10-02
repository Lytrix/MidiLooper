# Slice 3.12a — the admission limit on the decided accounting unit

**Change:** `windowed-playback-generation` · **Task:** 3.12 (**first increment only — the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.12
**Contract:** DEC-108 §14 and item 12(c); the spec's *Accepted content is never truncated to fit a window*;
fixture **2.11**'s accounting half.

**What 3.12 asks for, split.** (a) the **admission limit** on the decided accounting unit — the capture interval's
**effective** events at acceptance, the note-off/generated-closure reserve **inside** `L`, aggregated across the slot's
overdub passes, accounted with **preallocated bounded** arithmetic that never invokes the resolver on the MIDI edge, with
per-bar counts only as a **conservative** bound; (b) the **bounded legacy compatibility path** — content beyond `L`
served through fixed-capacity immutable pages with a resumable cursor (fixture 5.5); (c) the **MIDI-edge wiring** of the
limit into capture acceptance. **This slice lands (a).** (b) and (c) are named as the remaining increments, and the task
is **not checked**.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` (in `RuntimePlaybackViewConfig`) | `kWindowAdmissionLimit = kEventsPerView` (**L is the window's storage capacity**, with a `static_assert` pinning it so no partition multiplier can drift out of step); `kWindowClosureReserve` (**inside** L, provisional value 128, pinned as strictly less than L); and `WindowAdmissionAccounting` — `acceptedEffectiveEvents` + `outstandingReservations` + the reserve, with `committed()`, the one predicate `fits(...)`, `admit(...)` returning a **named** `Outcome`, and the conservative per-bar/span bounds **clamped by L**. |
| the reference fixture | `test_window_admission_limit_accounts_effective_events_and_the_closure_reserve_inside_l`. |

**The properties the accounting is built to have, each asserted rather than asserted-about:**

- **L is the storage capacity**: `kWindowAdmissionLimit == kEventsPerView`, pinned by `static_assert` in the same
  namespace, so the sliding window-span invariant's first half cannot drift.
- **The reserve is inside L**, not an addition to it (pinned, and asserted in the fixture).
- **One below fits, at the limit fits exactly, one above does not** — capacity pressure is visible *before* acceptance.
- **Nothing is half-accepted**: `admit` mutates only after `fits` said yes, and an over-limit admission returns
  `OverCapacity` by name with the counters unchanged.
- **Same-tick events across passes** are just more effective events in one window (four × 256 asserted), and an
  **outstanding reservation occupies the window until it closes** — releasing room when it does.
- **The per-bar bound is conservative and clamped by L**: for the two-bar comparison span at the design's 4,096
  refs-per-bar ceiling the bound *is* L, i.e. the window sets the shape and L sets what may be accepted; and a
  wrap-crossing interval has the same width, hence the same bound.
- **Interaction acceptance is structurally outside the limit**: the counters hold content only, so a stop, switch or undo
  request has no representation here to be refused by (DEC-078's separation, upheld by construction rather than by a
  runtime check).

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **56/56** (was 55; +1 fixture) |
| `pio test -e native` (full) | **2687 cases: 7 skipped, 2680 succeeded, 0 failed** (baseline 2686 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` / locals 34,880, floor 840, **448 B headroom** |
| the S3.0 pin | **unchanged** (6,512): the accounting is a header-only type with no per-slot state, so nothing was re-based |

## Red-proofs

`shasum -a 256 -c` against the pre-mutation hash, `OK` after both restores:

- **M13 — the closure reserve moved outside L** (`committed()` drops it): *"the reserve alone is committed before
  anything is accepted"* fails (`Expected 128 Was 0`).
- **M14 — the per-bar bound not clamped by L**: the span-bound assertion fails (`Expected 2048 Was 8192`) — the
  conservative bound would overstate what the window can hold, which is precisely what the clamp prevents.

## Not claimed

- **The capture-acceptance wiring is not done** (3.12's clause (c)): the accounting exists and is exercised, but no
  production acceptance decision consults it yet. Until that lands, "capacity pressure is refused before acceptance"
  is a property of the unit, not of the running system, and the task stays open.
- **The bounded legacy paged path is not done** (clause (b), fixture 5.5): nothing serves over-`L` content through pages
  yet.
- The **reserve's size (128) is a declared provisional value**, not a measurement; task 6.3's sweeps may move it, and no
  density or capacity claim is made from it.
- No timing/deadline claim, no envelope claim, no device evidence, no flash, no stimulus.
