# Slice 3.4 — the window identity is carried, and a replacement cannot be straddled

**Change:** `windowed-playback-generation` · **Task:** 3.4 (F-1) · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md)
**Contract:** DEC-108 items 1, 4 and 5; RWO `S4C45-CHECKPOINT-PARITY-AND-FIXTURES.md` §1 **F-1**; spec
*A published window is immutable and no consumer read straddles a replacement* and *Currency is the playback
revision, and "current" has one definition*.

**What F-1 asks for:** the fixture asserts `(slot, playbackGeneration, playbackRevision, interval)` identity, that a
consumer read **cannot straddle a replacement**, and that a consumer **cannot mutate** the window.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `PlaybackWindowIdentity` — the tuple a published window carries: `{slot, playbackGeneration, playbackRevision, interval}`, with equality over exactly those fields. `playbackRevision` is the sole **content currency** (`Loop::committedPassRevision()` is its alias); `playbackGeneration` guards **ownership**; `interval` carries the loop geometry; the snapshot identity stays provenance *inside* `PlaybackViewSourceIdentity` and is deliberately **not** part of the comparison. |
| `include/Track.h` + the owner TU | `Track::publishedPlaybackWindowIdentityForSlot(slot, out)` — the consumer-facing identity, a **const** accessor returning a copy (see the no-mutation clause below); and `Track::adoptPreparedPlaybackWindow(slot)` — the atomic adoption of a prepared successor (the lifecycle's own `handoff`), the edge task 3.7 will call. |
| `include/RuntimePlaybackView.h` + `src/Track/RuntimePlaybackView.cpp` | `RuntimePlaybackViewLifecycle::publishCompletedBuildAsReadySuccessor` — the **successor lane**. `publishCompletedBuildAsReady` treats the prewarmed shell at `ready` as an existing successor and refuses, so a lane without a record-return shell could never publish a successor at all; the new variant accepts an *incomplete* shell while keeping the rules that matter: **one real successor at a time** and **only a complete view is ever installed**. It touches nothing else — the served `current` window is untouched. |
| the owner TU | a completed build now routes by what it is replacing: **no window yet → `current`** (3.2's primitive); **a window being served → the `ready` successor**, leaving the served window byte-for-byte as it was. The request bound changed with it: `WindowAlreadyPublished` (3.2's "no second window") became **`SuccessorAlreadyPrepared`** — a window merely *being served* is not a bound, because preparing its replacement *is* the successor lane; what is bounded is a second *prepared successor*. |

**The 3.2 finding is resolved rather than worked around.** 3.2 measured that a second `beginCoverageBuild` against
a slot whose window was already published surfaced the new request's identity in the published view *before* its
build had completed, and refused the case instead of shipping it. With the successor lane the second build targets
the free build role and publishes into `ready`, and the F-1 fixture now asserts the served window is identical
field-for-field throughout the successor's preparation. The mutation that reverts the routing to "always replace"
reproduces the old symptom, which is how the fix is held in place.

## F-1's three clauses, and where each is asserted

1. **Identity** — `test_window_owner_resolves_the_interval_through_the_published_snapshot` builds its window
   through the real resolver, so the identity is the real one: `isValid()`, `slot == 0`,
   `playbackRevision == loop.playbackRevision`, and `interval` equal to the policy's interval. It then makes a
   **second request at the same revision and the same ownership but a different interval**: after adoption the
   generation is **unchanged** (`the generation guards ownership: a new interval does not move it`) and the
   revision is **unchanged**, while the *interval* differs (`the interval is not currency`). That is DEC-108
   item 5's separation, asserted rather than asserted-about.
2. **No straddling read** — `test_window_owner_publishes_the_interval_window_atomically` now prepares a replacement
   successor and asserts, **before every single preparation turn**, that the served window's identity is identical
   to the first one (`a consumer read cannot straddle a replacement`), that a prepared successor does **not**
   become the served window until it is adopted, and that the adoption swaps it atomically: afterwards the window
   is the successor's interval and carries the successor's own generation.
3. **No consumer mutation** — by construction and by the API: the only window accessors on `Track` are `const`
   and return **copies** (`publishedPlaybackWindowForSlot`, `publishedPlaybackWindowIdentityForSlot`), and the
   runtime/view set they read is private to `Track` (there is no public accessor handing out a
   `RuntimePlaybackViewSet*` or a non-const view). The fixture also asserts the window is *unchanged* across the
   operations that surround it, so a future accessor that leaked a mutable reference would have to break those
   assertions to pass.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **50/50** |
| `pio test -e native -f test_midi_service_drain` | **38/38** |
| `pio test -e native -f test_playback_prewarm` | **190/190** (unchanged — cited, not restated) |
| `pio test -e native` (full) | **2681 cases: 7 skipped, 2674 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** (all TUs recompiled, 70.8 s — not a cache hit) — `variables 96,192` (**unchanged**, no re-base) / `code 392,200 + padding 1,016` / locals 34,880, floor 840, 176 B headroom |

**Red-proofs (two mutations, each restored byte-identically** — the restore verified with `shasum -a 256 -c`
against the pre-mutation hashes, `TrackPlaybackWindowBuild.cpp` `c96ed5ab…853c` and `RuntimePlaybackView.cpp`
`28bbbbb0…5b72`, both reporting `OK`, with a byte-level `diff` against the backups also clean):

- the completed build always replaces the served window (the successor routing reverted) fails at *"the served
  window must not change while its successor is prepared"* and *"preparing a successor must leave the served
  identity untouched"* — the 3.2 symptom, reproduced by mutation;
- the ready-role shell treated as an existing successor (the shell tolerance reverted) fails at the successor
  publication's expectations (`Expected TRUE Was FALSE`, twice).

## Not claimed

- No device evidence, no flash, no stimulus.
- **The adoption edge is not wired into production** in this slice: `adoptPreparedPlaybackWindow` exists and is
  exercised by the fixtures, but no consumer calls it, and readiness (3.5) and the consumer switch (3.7) are still
  the ones that decide *when* a boundary is eligible. Nothing a consumer reads changed in this slice.
- No capacity/density/`N`/timing claim; the declared contract's phase advance stays unmeasured (task 6.3).
