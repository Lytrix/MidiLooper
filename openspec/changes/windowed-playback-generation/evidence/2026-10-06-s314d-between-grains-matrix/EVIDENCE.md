# Task 3.14 (increment d) — the between-grains matrix: repeated gestures, a selection change and a stale completion, each landing while real preparation grains are paused

**Slice:** `windowed-playback-generation` / task **3.14**, increment **d** · **Branch:**
`feature/windowed-playback-generation` · **Date:** 2026-10-06 · **Firmware:** *unchanged* — the increment is
**test-only** (`git diff --stat -- src include` is empty) · **Device:** **NOT RUN** (nothing device-visible is claimed).

Packet written before the fixture: [`EXECUTION.md`](EXECUTION.md). Authority: `tasks.md` item **3.14** (its verify
clause: *"pause between real preparation grains for repeated undo/redo, track/slot switching and stale completion.
Assert exact MIDI output, ledger state, retained-source lifetime and eventual release"*),
`G2-EXECUTION-PACKET.md` §"Increment 3.14", **DEC-108** §13(d)/(g), **DEC-114**, and the three landed increments
(`s314`, `s314b`, `s314c`), whose *"still owed, unchanged"* line names this matrix and records that it **needs 3.7's
serving path** — delivered by 3.7a's I1/I2/I5.

## 1. What the increment is, and what it is not

3.14's own verify clause asks for a **timing-shaped** matrix: gestures that land *while the window lane's real
preparation is mid-flight*, with the wire, the ledger, the served/rejected window and the retained source all asserted
at once. The lane's pause is real and production-only — `Track::stepPlaybackViewMaintenance` serves **one declared
grain per turn** and `Track::hasPendingPlaybackViewMaintenance` is the eligibility — so no test-only seam was added.

The increment is **verification**: 3.14a/b/c implemented the behaviour (the blanket silence is gone, the settlement is
one shared core, the committed-pass identity source closes the session case), and every fact below is measured through
the production entries. No production line changed, and a difference would have been a finding rather than a tolerance.

## 2. The mechanisms, measured rather than assumed (and one packet expectation corrected)

Two gestures that both advance the currency do **not** affect a paused build the same way, and the difference was
measured, not predicted:

| Gesture | What it moves | What happens to a paused build |
|---|---|---|
| a **committed pass** (`commitRecordPassIntoTrackSlot` → `Loop::commitPendingCapturePass`) | `playbackRevision` only | the build stays **legitimate**, completes, and **publishes at the revision it froze** — the window is stale by its own revision and readiness **names** it (`OwesCurrency`, holder `window_rebuild`) |
| a real **undo** (`TrackUndo::undoForLoop` → `refreshPlaybackAfterEditPassStateChange` → `Track::invalidatePlaybackMergedMidiEvents`) | the revision **and** `bumpPlaybackGeneration()` | the gesture **discards** the paused build: the invalidation's slot release drops the preparation (its refusal terms are the *live owners*, and a preparation in flight is not one), so `hasPendingPlaybackViewMaintenance()` is false and `buildOwed` is false immediately afterwards (measured with a temporary probe: `runtime=1 pending=0 owed=0 gen=1 rev=4 published=0`, readiness `2` = `OwesSource`, holder `published_source`) |

**This corrects the packet.** The packet's W-3.14d-2 expected a **late completion** — the build surviving the gesture
and its completion being refused by `Track::publishCompletedPlaybackWindow`'s moved-identity guard. The measurement
shows the discard comes **first**, so the publication guard is the *second* line of defence rather than the one this
case exercises. The fixture and the packet's W-2 row were corrected to the measured mechanism **with the reason
recorded here**, and the red-proof set was re-expressed accordingly (§4).

## 3. Measured

| Case | Result |
|---|---|
| **W-3.14d-1** a committed pass between real grains | the demand is five declared turns (100 events; a two-bar root gathers in one, which is why the paused cases hand the owner entry an explicit source); after **one** grain the build is still owed; the commit emits **nothing** on the wire, advances the revision, and **leaves the generation and the build untouched**; the remaining grains complete and **publish** a window at the **frozen** revision; `playbackWindowReadinessForSlot` answers **`OwesCurrency`** with holder **`window_rebuild`**; neither the grains nor the publication emit on the wire |
| **W-3.14d-2** a real undo between real grains | the gesture's output is **exactly one NoteOff** (the batch's own identity 60), the unrelated sounding 64 is untouched, and the removed identity is discharged *at the gesture*; the revision **and** the generation moved (+1); the paused build is **discarded** (`pending`/`buildOwed` false); no window is published, no turn emits, and readiness answers **`OwesSource`** with holder **`published_source`** — the restored root is deliberately not stamped current, so the lagging source is *named*, never served |
| **W-3.14d-3** repeated gestures, grains between them | gesture 1 (undo of batch B): exactly one closure (60); a real grain; gesture 2 (the re-establishing direction, the Track entry the redo route uses for identities): **0 closures**, 1 suppressed retrigger, 2 preserved, **0** emitted; a grain; gesture 3 (undo of batch A): exactly one closure (61); generation moves once per gesture (+1, +2); afterwards 60 is still sounding (suppressed, neither closed nor retriggered), 61 discharged, the control 62 alive, and **no** gesture's build published anything |
| **W-3.14d-4** a slot/track selection between grains | the slot row (`applyPlaybackViewTransition(SlotSwitch)`), the slot selector and the real `TrackManager::setSelectedTrack` emit **nothing** and silence nothing; the departing slot's sounding (70) is untouched; the generation is **unchanged** and the worker still owns its build; the build completes **into its own slot** at the frozen revision, and the newly selected track's slot has **no** window on that account |
| **W-3.14d-5** retained-source lifetime across the gestures | the source is admitted for the served revision; the readers answer **that** revision (`servingRetainedRevisionForSlot`); a committed pass moves the loop past it and the source is **not** re-aimed; 3.6a's single release predicate **refuses** while that window serves it; after a **newer** window is adopted the same predicate releases it and `hasRetainedPlaybackSourceForSlot` answers false |

**Controls that made the counts mean something:** the per-slot runtime is **reused across the fixture's cases** (only
the pool and the manager are reset per case), so each sounding case clears the ledger explicitly — found when a first
run's `preservedCount` was 3 rather than 2 (an earlier case's sounding survived); the untouched soundings (64, 62) and
the incoming track's slot are the negative controls; the fixture's own re-establishment of a sounding before the
suppress direction is the same construction 3.14a uses for that half.

## 4. Red-proofs

Six runs; every production file restored **byte-identically** from a sha256-checked copy (`shasum -a 256` compared
after each restore), never `git checkout --`.

| ID | Mutation | Result |
|---|---|---|
| **RP-D1** | `Track::playbackWindowReadinessForSlot`: the revision-lag clause inverted (`==`) | **RED** — *"the lag is named: a complete but stale window is not ready"* (W-1). 5 fixtures failed in total |
| **RP-D2c** | `Track::invalidatePlaybackMergedMidiEvents`: its slot release dropped (`(void)preserveLedger;`), so a gesture no longer discards a paused preparation | **RED** — *"the gesture discards the paused build: no turn is owed afterwards"* (W-2). 3 fixtures failed in total |
| **RP-D3** | `resolvePlaybackViewTransition`: an explicit `SlotSwitch` row that releases the candidate (the operation no longer continues) | **RED** — *"and the worker still owns its build"* (W-4). 2 fixtures failed in total |
| **RP-D4** | 3.6a's release predicate: the served-window clause inverted (`!=`), so a source is released while a window reconstructs from it | **RED** — *"a served window keeps its source retained"* (W-5). 2 fixtures failed in total |
| **RP-D5** | the edit-batch site stops settling (`refreshPlaybackAfterEditPassStateChange`'s reconcile call removed — the **class** of 3.14c's M47, re-run because this fixture is what a future reader will use) | **RED** — *"exactly one closure, for the identity the batch owned -- the gesture's output is exact"* (W-2, `Expected 1 Was 0`) |
| **RP-D2a / RP-D2b** | `Track::publishCompletedPlaybackWindow`'s moved-identity guard **inverted**, and separately **dropped** | **NON-RED-PROOF for this fixture, and that is the finding:** with the guard inverted the failure surfaces at *W-1's own publication* (the legitimate completion is refused) **and 3.11's** `…_a_late_completion_publishes_nothing` fails; with the guard dropped the fixture still passes, because the **discard** of §2 happens before any completion can reach it. The guard is the second line of defence, and only the discard (RP-D2c) discriminates this case. Named rather than reworded |

## 5. Verification

| Check | Result |
|---|---|
| Focused `pio test -e native -f test_runtime_working_set_ownership` | **PASSED — 174/174** (baseline on this tree **173/173**; the packet's 170 was I8's older figure) |
| Full native `pio test -e native` | **2,853 cases / 7 skipped / 2,846 succeeded / 0 failed** (baseline 2,852 / 7 / 2,845) |
| Capture build `pio run -e teensy41-capture-serial` (RAM1 guard) | **SUCCESS**, line **byte-identical**: `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880` (floor 840; 128 B headroom) |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat -- src include` | **empty** — no production line changed |

Fixture: `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`, function at `:6637`,
registered at `:13825` (sha256 `a127ba1c…`); the case helpers are a file-local
`requestBetweenGrainsBuild`/`kBetweenGrainsSourceEvents` pair at `:6603`.

## 6. Named, not claimed

* **No device leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b)**, and nothing is
  observable on the shipping env (the lane has no row family).
* **The re-establishing direction is driven through the Track entry the redo route uses for identities**
  (`reconcileAfterEditPassStateChange(…, false)`), not through `redoForLoop`: the production redo route's other entry
  kinds (clear-slot side effects, geometry-preview cancellation) would drag the whole edit stack into a suite that
  deliberately stubs it — the same discipline 3.14a records, not a new limit.
* **The paused cases hand the owner entry an explicit source** (`requestPlaybackViewMaintenance`) because a two-bar
  root gathers in one turn and cannot be paused at all; the *root-gathering* requester is exercised by W-5 and by the
  suite's other fixtures, not by the three paused cases.
* **The zero-MIDI assertions are the fixture's own capture** (`MidiSendCapture`), read between steps; the probe that
  measured the post-undo state was **withdrawn before this commit** (it is recorded, not left in).
* **Task 3.14 stays OPEN:** the refresh as a **request through the immutable publication owner** (3.14c measured that
  its owner is 4.1's current root, then 3.7's consumer), **displayed provenance** (the display's, per
  [`DISPLAY-CONSUMER-RECONCILIATION.md`](../../DISPLAY-CONSUMER-RECONCILIATION.md) — `runtime-working-set-ownership`
  S4e), and preview-session undo/redo (3.13/2.12). What this increment closes is the **between-grains matrix** its own
  verify clause asked for.

## 7. Deviations, recorded rather than smoothed

1. **The packet's W-3.14d-2 expectation was corrected by measurement** (§2): the gesture discards the paused build
   instead of letting it complete late. The packet's W-2 row and its red-proof table were updated in place, with the
   measurement named.
2. **The packet said the existing 3.14 fixture would be *extended*; this increment adds a new fixture in the same
   suite instead.** The existing fixture is already ~140 lines of five numbered sections, and this matrix is four new
   scenarios with their own controls; keeping them separate is what makes each case's failure line attributable.
3. **The per-slot ledger is cleared per case** (§3) — a control the first run proved necessary, since the runtime object
   outlives a case inside one fixture.
4. **A temporary probe was used and withdrawn** to establish the post-undo state (`pending`/`owed`/generation/
   readiness/holder); the assertions now state those measured values exactly.
