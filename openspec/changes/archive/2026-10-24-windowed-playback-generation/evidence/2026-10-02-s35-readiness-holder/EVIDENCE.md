# Slice 3.5 — readiness is the completed publication, and its failure names its holder

**Change:** `windowed-playback-generation` · **Task:** 3.5 (AC-3, DEC-108 item 5) · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.5
**Inherited criteria:** RWO `S4C1-READINESS-CURRENCY-CONTRACT.md` **AC-3** (readiness is a predicate with a named
holder) and **AC-4** (currency is the revision, and "current" has one definition).

**What AC-3 asks for:** "Ready" must be a predicate a consumer can ask — *does the window for this interval exist
and is it current?* — and whose **failure names its holder**, so a consumer that finds a window missing can say
**who owes it**, exactly as `hasPendingSnapshotPublish`, the restore-request vocabulary and the S4b.7 hold terminal do.

---

## What landed

| File | Change |
|---|---|
| `include/Track.h` | `PlaybackWindowReadiness` — `Ready`, `OwesBuild`, `OwesSource`, `OwesCurrency`, `NotServable`, `NoRuntime` — with `playbackWindowReadinessForSlot` and `playbackWindowReadinessHolder`. The legacy predicate's own doc now says what it is: readiness answered from the live mutable merged buffer, which requires a non-empty window, and which survives only as the switch commit's hold term until 3.7 removes the live build. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The predicate: the **published** window (3.2/3.4) and `playbackRevision` as the only currency. No window published → the holder turns on whether a build *could* gather (a **current published root**): `OwesBuild` when one exists, `OwesSource` when it does not. A published window whose revision differs → `OwesCurrency`. Otherwise `Ready` — **with no event-count term at all**. The holder names are an if/else chain (no `switch`, no table). |
| `linker/imxrt1062_t41_lcr.ld` | This TU's string literals routed to memory-mapped FLASH — the fix for the RAM1 finding below, using the pattern the script already carries for `WorkspaceSave`, `TrackCaptureInput` and `TrackOverdubLifecycle`. |
| `test/test_runtime_working_set_ownership/…` | The predicate's own fixture (W-9…W-12) and **RWO's two demoted-target fixtures re-pointed** (W-13) so they no longer depend on `empty ⇒ not-ready`. |

## The oracle, case by case (packet § Slice 3.5)

| # | Case | Observed |
|---|---|---|
| W-9 | current published root, prewarmed runtime, **no window published** | `OwesBuild` + holder `window_build_owner` — the build owner owes it, not the source |
| W-10 | the same slot with **no published root** | `OwesSource` + holder `published_source` — a different holder, which is the point of naming it |
| W-11 | a window published for an interval that **holds no events**, at the loop's current revision | `Ready`, while the legacy merged term for the same slot is **false** |
| W-12 | a window stamped one revision behind the loop; then the current revision with a different generation | `OwesCurrency` + holder `window_rebuild`; then `Ready` — **the generation is not currency** (task 3.4's separation) |
| W-13 | the demoted switch path (RWO's fixtures, re-pointed) | the commit is still served by the **legacy** term while readiness names the holder for both shapes: `OwesBuild` with a root, `OwesSource` without one |
| — | a slot with no committed content | `NotServable` |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **51/51** (was 50; +1 fixture) |
| `pio test -e native -f test_midi_service_drain` | **38/38** |
| `pio test -e native -f test_playback_prewarm` | **190/190** (unchanged — cited, not restated) |
| `pio test -e native` (full) | **2682 cases: 7 skipped, 2675 succeeded, 0 failed** (baseline 2681 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** (every TU recompiled, 84.7 s) — `variables 96,192` (**unchanged**, no re-base) / `code 392,200 + padding 1,016` / locals 34,880, floor 840, 176 B headroom |

## The RAM1 finding: named, measured, and fixed (no re-base)

This slice's **first** build failed the guard:

```
[RAM1 budget] teensy41-capture-serial: variables 100,288 / ITCM 393,216 (code 392,200 + padding 1,016) / locals 30,784
[RAM1 budget] FAILED -- the approved RAM1 budget changed:
```

`variables` **+4,096 B** while ITCM `code` was **byte-identical** (392,200) — the same signature the RAM1 rule
already records twice.

**The cause, established by name rather than guessed:** this script places `.rodata` in **DTCM** —
`.data : { … EXCLUDE_FILE (*WorkspaceSave*) *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*))) … } > DTCM` — so the six
holder-name literals (~100 B) landed in RAM1 and the following `ALIGN` snapped the
`_VectorsRam`/periodic-table 4 KB edge. That is the rule's own "a capture format string can cost 4 KB of the local
allowance" mechanism, reproduced by a *readiness* slice: the new bytes are not large, they are merely on the wrong
side of an alignment boundary.

**A fix that compiled-but-was-wrong, recorded because it is the natural first guess:** marking the names
`TRACK_COLD_MEM` (``.flashmem``) failed with

```
error: 'kReadinessUnknownName' causes a section type conflict with
       'ProjectionContext makePlaybackContext(const Track&, const Loop&, uint32_t)'
```

— because this TU's **functions** already occupy `.flashmem` as *code* (`*(.flashmem*)` sits inside `.text.code`),
and one section cannot hold both text and rodata.

**The fix that works** is the pattern the script already carries for the two other TUs that hit this: route this
TU's string literals to memory-mapped FLASH in `.text.progmem`. Only read-only data moves (the row strings this TU
already carries travel with the new names). The clean rebuild then reports the approved line **byte for byte** —
`variables 96,192`, `code 392,200 + padding 1,016`, locals 34,880 — so **no re-base** of `scripts/ram1_budget.json`
was needed or made.

## Red-proofs

Two mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hashes, both `OK`,
with a byte-level `diff` also clean):

- **(a) readiness answered from the legacy merged buffer** (the red-first body: `isPlaybackMergedMidiEventsReadyForSlot
  ? Ready : OwesBuild`) — **5 failures**, each for its designed reason: the two S4b.6 fixtures (*Expected 2 Was 1*:
  `OwesSource` collapsed into `OwesBuild`, exactly the W-10 mutation); both S4b.7 shapes (*Expected 1 Was 0* and
  *Expected 2 Was 1*: with a root the legacy term says **Ready** although **no window was published at all**); and
  the new fixture's empty-interval case (*Expected 0 Was 1*: a legitimately empty interval reported as owing a build).
- **(b) the commit's hold term flipped to the new predicate** (`TrackManagerTransportTick`) — **1 failure**:
  *`Expected 2 Was 0. the root-backed demoted switch must commit`* — the pending switch stays in the predecessor
  slot instead of committing to the target.

## Measured, not claimed: why the consumer flip is 3.7's

Red-proof (b) is the measurement behind that sequencing, and red-proof (a) supplies the other half: the demoted
target reads **`Ready` from the legacy term while no window was ever published for it**. The legacy term does not
require a publication at all, and the new one requires exactly that — while **nothing in production requests a
window** (`requestPlaybackWindow` has no production caller). So flipping the commit's hold term in this slice would
leave a real, root-backed demoted switch pending forever; the requester and the flip are 3.7's, and this slice says
so rather than implying otherwise.

## Not claimed

- **No deadline claim (F-2 / task 2.2).** This slice makes readiness a predicate and names holders; that an updated
  window is ready *before the playhead returns to it* (AC-1) is a different, still-open requirement.
- **The one-pass-late allowance is 3.6's** (AC-5). 3.5's currency is exact: an older published revision is
  `OwesCurrency`, not silently accepted.
- No retention, no interval index, no adoption wiring; `adoptPreparedPlaybackWindow` still has no production caller.
- **`isPlaybackMergedMidiEventsReadyForSlot` remains the switch commit's term** until 3.7 removes the live build.
- No device evidence, no flash, no stimulus.
- **AC-4's "no second validity counter" review note:** none was introduced. Currency is `playbackRevision`, read from
  the published identity; the snapshot-side coverage outcomes (`wrapTargetRevision`, `coveredPassRevision`, current)
  are untouched, and the predicate compares no other counter.
