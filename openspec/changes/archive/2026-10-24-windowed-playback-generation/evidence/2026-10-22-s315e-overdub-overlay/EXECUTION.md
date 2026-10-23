# 3.15e — overdub-overlay slice: completion record

**Change:** `windowed-playback-generation` · **Task:** 3.15e · **Branch:** `feature/unified-rolling-playback-path`
**Packet:** [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md) · **Status:** **native verified**; task **stays unchecked** (one
named architecture gap remains — see *What this does not claim*).

## What was measured

Fixture: `test_3_15e_a_live_overdub_is_served_exactly_once_through_the_wrap_and_the_pass_handoff`
(`test/test_shared_capture_finalization/test_shared_capture_finalization.cpp:7074`), on the production clock entry
(`Track::playMidiEvents` → `playCommittedLoopMidi`) and the real deferred overdub stop. Two-bar loop (1 536 ticks),
committed Record pass with a published root, live overdub note **A** (`70@1470/1500`) entirely before the wrap and note
**B** (`71@700/760` of the next cycle) entirely after it.

Every number below is the fixture's own reading (the temporary probe that produced them was replaced byte-for-byte by the
registered, assertion-only case):

| Step | Reading | What it pins |
|---|---|---|
| settled committed traversal | 4 rows, ledger 0 | the baseline the overlay is added to |
| live start | `capture.store = 2`, `merged = 4` | the overlay is not materialised into the merged store |
| live A, before the wrap | 6 rows: base 4 + `70@1470/1500`; ledger 0; `playbackOrderHotRebuildCount() == 0` | A is echoed **exactly once**, the echo writes **no ledger**, and the growing overlay forces **no content-sized build** on the MIDI turn |
| wrap turn (live) | **0 rows**, `overdubPasses 0→1`, `capture.store → 0` | the exactly-once seam: the interval this turn commits is **not** re-served by the committed walk |
| live B, after the wrap | 6 rows: `48@12/96`, `71@700/760`, `62@800/880`; `store = 2`; `merged = 6`; ledger 0 | B echoes once in the new pass; A reached the merged store **only by being committed** |
| pass handoff (still live) | 2 rows: `70@1470/1500` | A is served from the **committed reader** exactly once at its own phase; B stays the live tail and is **not** replayed |
| stop → retirement | `overdubPasses 1→2`, `playbackRevision` advances | the stop commits B's pass through the deferred owner |
| post-handoff traversal | 8 rows, tick order: `48@12/96`, `71@700/760`, `62@800/880`, `70@1470/1500`; ledger 0 | base + A (wrap pass) + B (stop pass), each **exactly once**; every articulation closed by its own off |

An independent hand-written stream is compared element-wise (tick, note, on/off); nothing is read back from the route that
produced it.

## G0 findings this slice rests on (read-only, code-cited)

- **VERIFIED — the live overlay has its own cursor walk.** `Track::playCommittedLoopMidi` ends with
  `advancePlaybackCursor(… PlaybackEmitPolicy::ActiveCaptureOverdub, makeCapturePlaybackStream(loop))` over
  `loop.capture.store`, after the committed walk
  (`src/Track/TrackPlaybackHotPath.cpp:580-590`). Every other route (view, bridge, prepared pair, paged ring, merged
  store) is committed-only.
- **VERIFIED — the merged store refuses live capture by contract** (`src/Track/TrackPlaybackWindowBuild.cpp:507-514`), and
  the prepared pair is published *from* that store; `LiveCapture` view requests are gated to first-take Record
  (`:159-163`, `:1110-1112`, `:6238-6240`), never an overdub.
- **VERIFIED — no reader is written with live rows.** `views.lifecycle.current` changes only through
  `RuntimePlaybackViewSet::handoffReadyCoverage` / `resetMetadataPreserveView` from a **complete prepared** source, and the
  pair is replaced by one atomic publication (`src/Track/TrackPlaybackWindowBuild.cpp:7499-7535`,
  `src/Track/TrackPlaybackWindowBuild.cpp:1882-1928`).
- **VERIFIED — the wrap hand-off is explicit.** `Track::commitOverdubWrap` seals the pass, `beginCapture(Overdub)` re-bases
  the live store and zeroes the echo cursor, and held NoteOns are carried into the new pass
  (`src/Track/TrackOverdubWrap.cpp:16-177`); the frame's own tail interval is drained by
  `advancePlaybackCursorAcrossOverdubWrap` (`src/Track/TrackPlaybackHotPath.cpp:148-176`).
- **VERIFIED — the ledger split is deliberate**: `playbackCursorAdvanceSendCapture` is emit-only, pinned by sessions
  `221334`/`224719` (`src/Track/TrackPlaybackHotPath.cpp:86-91`).
- **UNKNOWN (still)** — whether a wrap-held continuation NoteOn re-articulates through the live echo after a wrap; this
  fixture keeps both overlay notes clear of the boundary, so it does not claim that case.

## Named architecture gap (why the task stays open)

The task's remaining half — *"overdub MIDI and ledger events belong to the same reader exactly once through wrap, stop and
pass handoff"* and *"remove independent scheduled live-Capture replay and edge sorting"* — is **not** satisfied:

1. the live overlay is served by a **second cursor walk** (`ActiveCaptureOverdub`) on the same clock turn, not by the
   committed reader;
2. that turn also runs `loop.ensureCaptureEventsSorted()` + `reanchorCaptureIndex` **on the MIDI service turn**
   (`src/Track/TrackPlaybackHotPath.cpp:580-584`) — the "edge sorting" the tightening names;
3. the overlay's ledger events do not exist by design (emit-only echo), so "one ledger owner" is a design decision to
   revisit, not a bug to fix here.

Closing it is a **route/ownership change** (DEC-116-adjacent, and it touches the `221334` invariant), not a fixture
detail. It is recorded as the remaining 3.15e work and was deliberately not attempted in this slice.

## Mutations (each restored byte-identically)

| # | Mutation | Result | Restore |
|---|---|---|---|
| M1 | live-capture echo walk guarded off (`false &&` on the `ActiveCaptureOverdub` condition in `TrackPlaybackHotPath.cpp`) | **4 failed**: the new case at `live A before the wrap: expected 6 rows, got 4`, plus the three existing live-overdub fixtures (`test_record_bind_refusal_keeps_second_bar_audible_during_overdub`, `…_keeps_base_during_record_publication`, `test_record_ready_before_publication_survives_owner_and_adoption` — each `Expected 2 Was 0`), which independently names the echo as their route | `sha256 3d9df8b6f8db37c7…` before and after; `git diff` clean |
| M2 | wrap commit disabled (`false &&` on `loop.shouldCommitOverdubWrap(...)` in `TrackOverdubWrap.cpp`) | **1 failed — the new case only**: `Expected 1 Was 0. A's pass is committed at the wrap` | `sha256 ab4a4240da30baea…` before and after; `git diff` clean |

The two mutations are the two halves of the claim (the live walk emits A/B; the wrap turn commits and re-bases), and each is
discriminating rather than merely red.

## Gates

| Gate | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_shared_capture_finalization` | **90 / 90 succeeded** (89 + the new case) |
| Full native | `pio test -e native` | **3 384 / 7 skipped / 3 377 succeeded / 0 failed** (baseline 3 383 / 7 / 3 376) |
| Spec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Builds / RAM1 | — | **NOT RUN, by design:** the slice changes one test file and no `src/`, `include/`, linker or PlatformIO line, so both images and both pins stand unchanged (`391,688 + 1,528` default, `391,848 + 1,368` lane) |
| Device | — | **NOT RUN:** 3.15f owns device qualification; this is a native content/hand-off claim |

## What this does not claim

- It does not check off 3.15e; the single-reader/replay-removal half is open (see *Named architecture gap*).
- It does not claim device parity, the paged/rolling route for this shape, or the wrap-held continuation case.
- It does not claim that `playbackOrderHotRebuildCount() == 0` generally during an overdub — the counter moves with
  **committed revision advances** (measured: 2 rebuilds in the post-wrap and post-stop segments, 0 inside the live pass).
  The asserted scope is exactly "the growing overlay does not force a content-sized build".
- It does not assert the reader-role single-writer property in the fixture; that is code-verified (G0) and asserted only
  through its content consequence (the merged store never carries the uncommitted overlay).
