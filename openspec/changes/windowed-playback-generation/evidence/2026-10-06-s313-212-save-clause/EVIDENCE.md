# Task 3.13 (I10) — fixture 2.12's `then save` clause measured: a served preview is never what a save writes

**Slice:** `windowed-playback-generation` / task **3.13**, increment **I10** (fixture 2.12's remaining save clause) ·
**Branch:** `feature/windowed-playback-generation` · **Date:** 2026-10-06 · **Firmware:** *unchanged* — the increment is
test-only (`git diff --stat -- src include` empty) · **Device:** NOT RUN (nothing device-visible is claimed).

Packet: [`EXECUTION.md`](EXECUTION.md) (its "Sequencing, recorded rather than smoothed" section names the one deviation:
the harness probe and the first fixture draft preceded the packet). Authority: `tasks.md` fixture **2.12** and task
**3.13**'s owed item (*"fixture 2.12's **save** clause in the storage harness (why this task stays open)"*),
`G2-EXECUTION-PACKET.md` §"Slice 3.13", **DEC-073** (the persistence admission), **DEC-108** §13, **DEC-114**.

## 1. Why this is the item, and what the harness needed

Task 3.13 keeps two items open: the **display consumer** of a preview window, and this **save** clause. The clause's
substance is 2.12's last clause — **a preview is never committed currency** — read at the one consumer that *writes*: a
preview reaching the save would persist uncommitted content into a slot's body.

The structural rules were already shipped (a preview carries `PlaybackWindowOrigin::Preview` in both identities; the
capture admission counts committed windows only, 3.7a/I3). What had never existed is a harness holding **both halves at
once**: the window lane (so a preview is really served) and the storage entry points (so a save can really be driven).
`test_windowed_preview_integration` linked the edit stack and the lane; the storage suites linked the save. **Measured
before writing the fixture:** adding `StorageLoopIo.cpp` + `StorageLoopIoBodyFormatV1.cpp` to the preview suite needs
**no** additional stub (`off_path_deps.h` already stubs the SD path and the display routes the save touches) and all nine
of its fixtures kept passing. That linkage is the increment's only enabling change.

## 2. Measured

The fixture drives the suite's own long loop (64 bars, six committed notes), opens the session **before** any root is
published (a root-current session rematerializes with an empty store and has no preview content at all), mutates it and
serves the projection through the preview lane; then it publishes the slot's checkpoint, **settles** the loop's queued
publications the way the storage harness does before a save, and saves through the production entry
`StorageLoopIo::writeLoopPersisted` into memory:

| Step | Result |
|---|---|
| the served window | the session's own projection, element-wise (the suite's own oracle) |
| the checkpoint the save will embed | the loop's **published root at save time**; the settle replaces the synthetic root this fixture had published with the publisher's own (measured: **20** events, against the **8** the synthetic root carried and the session's projection), which is why the fixture reads the checkpoint **after** the settle instead of assuming which root survived; its content differs from the projection |
| committed currency while the preview serves | `committedPlaybackWindowServesSlot == false`, `committedPlaybackWindowEventCountForSlot == 0` |
| the save | `writeLoopPersisted` returns true; the body reads back `ok`, **not** `legacy`, and its embedded snapshot's content equals the **checkpoint's** and **differs from the projection's** |
| after the save | the preview is still the served window, at its own revision, with `origin == Preview` |

**The rule's nature, stated rather than implied:** the save's source selection has **no preview input at all** —
`writeLoopPersisted` reads `loop.publishedSnapshot()` and the pass set — so "a preview is never saved" holds **by
construction** at that layer. The two rules whose failure *would* let a preview reach a save are therefore what this
increment red-proves, not the absent input.

## 3. Red-proofs

| ID | Mutation | Result |
|---|---|---|
| **RP-I10-origin** | `committedServedPlaybackWindowView` ignores the window's origin (a preview counts as committed content) | **RED**: *"a preview window is not committed content"* (`test_the_save_serializes_the_committed_checkpoint_and_never_the_served_preview`, line 898). 1 failed / 9 |
| **RP-I10-route** | `loopUsesLoopSnapshotBodyFormatV1ForSave` returns false (the slot's checkpoint is no longer embedded) | **RED**: *"the written body reads back"* (line 915). 1 failed / 9 |

Both mutations were restored **byte-identically** (`src/Track/TrackCaptureInput.cpp`, `src/StorageLoopIo.cpp` verified by
sha256 after each run from a sha256-checked copy, never `git checkout --`).

**Not attempted, and named:** a mutation that makes the save *read the preview* is **not expressible** — the save's source
selection has no preview input — which is the by-construction statement above. A weaker alternative (making the save
write the passes-authoritative body) is exactly **RP-I10-route**.

## 4. Verification

| Check | Result |
|---|---|
| Focused `pio test -e native -f test_windowed_preview_integration` | **PASSED — 10/10** (baseline 9/9; one new case) |
| Full native `pio test -e native` | **2,852 cases / 7 skipped / 2,845 succeeded / 0 failed** (baseline 2,851/2,844), no `ERRORED`/`FAILED` line |
| Capture build `pio run -e teensy41-capture-serial` (RAM1 guard) | **SUCCESS**, line **byte-identical** (`variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`, floor 840) — test-only |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat -- src include` | **empty** — no production line changed |

## 5. Named, not claimed

* **No device leg, no timing claim, no `ExecutionBounds.md` row change, no format or admission change, no fallback
  removal (3.7b).**
* **The fixture does not release the snapshot the reader returns**: the storage suite's
  `releasePersistedLoopSnapshotChunks` is not linked into this suite. A native test leaks it deliberately; recorded
  rather than hidden.
* **The checkpoint is the publisher's, not the fixture's synthetic one** (the settle replaces it, measured above), so the
  clause is pinned as "the body carries the **checkpoint**, and the checkpoint differs from the projection" — not as "the
  body carries a root the fixture hand-made".
* **`hasSettledPublishedSnapshotForSave` is the save's own admission** and the fixture *reads* it (it settles the queued
  publications rather than bypassing the predicate) — the save's route is the production one.
* **Task 3.13's other owed item — the display consumer of a preview window — is untouched and 3.13 stays open on it.**
  This increment closes the item the task list names as *why* the task is open.
