# 4.5 — the lane's adoption boundary is the production caller that recovers a latched fault (firmware + tests)

Slice of task **4.5** (its **recovery half**) on task **3.7a**'s serving path. Packet:
[`EXECUTION.md`](EXECUTION.md) (§8 is the revision record: this EVIDENCE reports the **landed** slice, and §3 below records
what an independent review withdrew from its first form). Commit message: *4.5/I10 (fix) — only the adoption boundary
recovers; the last-resort wiring is withdrawn on review*.

## 1. What was wrong, measured before the edits

`Track::advancePlaybackLagTraversal` had **no production caller at all**: `src/` contained only its definition (plus the
definition of the policy it drives), and the only callers anywhere were tests. The consequence this slice repairs:

* the latched **deadline-contract fault never cleared in production** — its only clear site is
  `advancePlaybackLagTraversal`'s `Current` branch (`lag.missedTraversals = 0; lag.breachReported = false;
  runtime->deadlineContractFaultLatched = false;`), so a fault latched through the one route that *was* wired (an admitted
  promise whose deadline passes, `serviceAdmittedDeadlineMisses` → `noteMissedTraversalForSlot`) was **permanent**.

`Track::containmentForSlot` / `Track::applyLastResortForSlot` had no production caller either — and **still do not** (§3):
the approved last resort remains unreachable, and that is now named as owed rather than implied repaired.

**Measured first failure of the landed fixture (pre-slice):** *"the eligible boundary is the production caller that clears
the latched fault: recovery through the real consumer, not a silent reset"* — the fault stayed latched across a real
adoption.

## 2. What landed

**One call** in `Track::resolveRollingPlaybackWindow` (FLASH-placed boundary work at the lane's own per-traversal frame,
reached from the clock path by the call that already exists — so no new ITCM call site in the default image), at the
frame's **adoption boundary** (`clock.tick == frame.deadlineTick && phase == frame.startPhase`, after the adopted view has
been validated and `adoptPreparedPlaybackWindow` succeeded):

```cpp
(void)advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision,
                                  /*replacementReady=*/true);
```

`resolvePlaybackLag` returns `Current` on its `replacementReady` branch, and `Current` ends the episode: `missedTraversals`
→ 0, `breachReported` → false, `deadlineContractFaultLatched` → false. So the episode and any latched fault end **where the
serving path really recovered** — at the boundary that may serve the current revision — and this is the **only** production
clear site.

No new state, no new row family, no scheduler, no second authority; `PlaybackLagState` and the containment policy are
unchanged, and `serviceAdmittedDeadlineMisses`' own wiring is untouched.

**Named honestly:** with `replacementReady=true` the call does not read `servedIsCurrent`, so at this site it is a
**statement** ("this boundary may serve the current revision") rather than a discriminator; `desiredRevision` is passed for
the record. That is the eligible boundary's intended semantics, and the red-proof (RP-A) is exactly what makes it
load-bearing: remove the call and the fixture fails.

## 3. What an independent review withdrew (and why it stays on the record)

The first form of this slice was committed as **`eefd90a5`** and wired two more sites: the **expiry** judgement
(`advancePlaybackLagTraversal(…, /*replacementReady=*/false)` when a `Serving` frame's traversal passed) and the **last
resort** (`applyLastResortForSlot`) at the resolver's three `Serving`-phase rejections. One independent review falsified
that form; the working tree was then **reduced** to §2. The findings:

| id | finding | status |
|---|---|---|
| **F1** | **Wrong silence.** The resolver's three rejections are exactly the turns the **retained merged fallback** takes the slot over, so `applyLastResortForSlot` there closes a sounding that another serving route is about to play. | verified |
| **F4** | **Wrong interval.** At the expiry the phase has advanced a whole span past `frame.startPhase`, so the call named the interval the frame had just *served* (at worst one pass late) rather than the traversal that went unserved. `PlaybackLagState` resets the episode when the interval changes, so the call minted the allowance for the wrong interval; the promise route already counts that miss with the demand's own identity. | verified |
| **F3** | The expiry call can also resolve `Current` (when the published revision is already the desired one) and therefore *clear*: "the adoption boundary is the only clear site" was **false while it existed**, and became true again only by the withdrawal. | accepted |
| **F2 / F5** | The containment sites' reachability is not established: `containmentForSlot`'s "no valid coverage remains" is defined on the published **window**, which is not this resolver's rejection condition, and only one of the three sites is reachable at all. | accepted |
| **F6** | The fixture manufactured a state the publication path cannot reach (it advanced `loop.playbackRevision` and then used `+1` again as the desired revision). | accepted |
| — | Over-claim: the first form's text said `serviceAdmittedDeadlineMisses` "keeps its own count". Both routes share `lagState.missedTraversals`, `breachReported` and the latch through `noteMissedTraversalForSlot`; only the promise route's per-promise dedup counter is its own, and `Current` resets the shared episode state. | corrected in the docs of this commit |

**Consequence, stated plainly:** the **last resort / containment application is not delivered by this slice.** It needs an
owner-level predicate for "no serving route at all" (or 3.7b's fallback removal) before it can be wired;
`containmentForSlot` / `applyLastResortForSlot` therefore still have **no production caller**.

## 4. Verification (the landed slice, re-run on the frozen tree)

* **Focused** `test_runtime_working_set_ownership` **176/176** (baseline 175/175 at I9 `2f0feced`; re-run after the fixture
  doc-comment cleanup, still 176/176), and the pre-existing rolling fixtures are unchanged.
* **Full native** `pio test -e native` **2,855 cases / 7 skipped / 2,848 succeeded / 0 failed** (baseline 2,854 / 7 /
  2,847; +1 = the new fixture). No suite ERRORED or FAILED.
* **Default capture build** `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical**
  (`variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`, approved floor `padding >= 840`):
  the call lives behind `PLAYBACK_WINDOW_ROLLING_ENABLED`, which that image keeps at **0**, so the shipping image is
  unchanged by construction and by measurement.
* **Lane-enabled build** `pio run -e teensy41-capture-serial-window-qualification` **FAILED its own guard** — and that is
  the honest number: `variables 96,192 / ITCM 425,984 (code 393,240 + padding 32,744) / locals 2,112`, i.e. **exactly the
  pre-slice baseline** (`capq_base.log` measured the same line before this slice): this slice adds **0 B** to that image,
  and the image is over its own approved budget **independently of it** (12 banks = 393,216 B; the 13th costs 32,768 B of
  local allowance and is the documented silent boot hang, I5's record). For contrast, the **withdrawn** calls had cost
  **+144 B** there (`code 393,384 + padding 32,600`), and withdrawing them restored the baseline byte-for-byte.
  **Named limit:** the lane-enabled behaviour is therefore **not device-observable**, and nothing about it is claimed from
  hardware.
* `git diff --check` clean; strict `openspec validate windowed-playback-generation --strict` valid.
* **Order of operations, stated so the numbers are not over-read:** the full native run, the capture and qualification builds
  and the red-proof were taken on the slice **before** the fixture doc-comment cleanup recorded in §6. That cleanup changed
  **comments and a duplicated declaration only** (the fixture's compiler-visible token stream is unchanged), the capture and
  qualification builds compile `src/` and are untouched by it, and the **focused suite was re-run after it: 176/176**.
  `src/Track/TrackPlaybackWindowBuild.cpp` is byte-identical to the pre-red-proof copy (`d875a6c7…`, `diff`-verified), so the
  red-proof's restore claim holds for the tree as committed.
* **Red-proof RP-A** (the adoption judgement removed): **RED** at the fixture's own assertion — reported at
  `test_runtime_working_set_ownership.cpp:6753` in the red-proof run (6754 after the comment-only cleanup), message *"the
  eligible boundary is the production caller that clears the latched fault…"* — and the suite then ends `ERRORED` (SIGHUP) —
  the same shape every aborted Unity run in this suite takes; the
  failure is the assertion, not a harness or setup error. **Restore byte-identical**, verified twice: `diff` against the
  pre-mutation copy (`rp/i10b_winbuild.cpp`) and sha256
  `d875a6c70b9f197cd9c64a292773bd5e57b01f89281017f75163ff87f54bd8c3` for `src/Track/TrackPlaybackWindowBuild.cpp`, which is
  the file's sha256 **before and after** the mutation. (Unity's summary line for the aborted run reads *177 cases: 1 failed,
  175 succeeded* against the green run's *176: 176*; recorded as observed rather than explained away.)
* **Void red-proofs:** RP-B (the expiry judgement removed) and RP-C (the containment application removed) measured the
  **withdrawn** sites and are not evidence for this slice.

## 5. The fixture, and exactly what it measures

`test_the_lane_boundary_is_the_production_caller_that_recovers_a_latched_fault` is **self-contained** (it neither borrows
nor perturbs another fixture's preconditions): it builds a 24-bar demoted loop in the selected track's slot 0, publishes its
root, prewarms the slot, forces `TRACK_PLAYING`, establishes a **served window** through the window lane's own
`requestPlaybackWindow` → `stepPlaybackViewMaintenance` → `playbackWindowServesSlot` path, latches the fault through the
policy's own entry (`advancePlaybackLagTraversal(…, /*replacementReady=*/false)`: the first miss is the allowance, the
second consecutive one of the same interval is the breach), opens the lane's demand with
`Track::serviceRollingPlaybackWindow`, and drives with the suite's `rollingTestTick` (selector + one worker turn) so
`resolveRollingPlaybackWindow` runs from the clock path:

1. **the latch's own precondition:** the fault is latched **while no lane promise is outstanding** — the state the boundary
   below recovers from (asserted, so the fixture does not merely assert the latch);
2. **the recovery:** the lane opens its demand, prepares, builds and **adopts**; at the adoption the fault is **cleared**,
   `lagState.missedTraversals` is back to **0** (the episode ended, so the next miss starts a fresh allowance), and the
   served window is untouched;
3. **the wire and the ledger are untouched by the recovery:** a following serving turn emits **zero** closures for the slot's
   marked sounding (pitch 60), the entry is still active, **no** `last_resort_slot_silence` row, and **no** `lag_breach`
   row is attributed to the recovery.

**Not covered by this fixture** (recorded in its own doc comment): the **expiry** judgement and the **last-resort
application** — both withdrawn (§3). The fixture's rows are cleared immediately before the drive, so the row family
describes the recovery only.

## 6. Recorded rather than smoothed over

* **The S4b.7a harness is not reusable from a new fixture.** The first draft reused
  `assert_s4b_7_a_demoted_target_through_the_turn`; it passed *inside* the fixture but the run reported a **trailing**
  failure at the harness's own switch commit and the suite ended `ERRORED` in every arrangement tried — including with the
  slice stashed, and at two different positions in the run order. The landed fixture is self-contained and the error is gone;
  the incident is recorded because it is a real trap.
* **Two pre-existing `RUN_TEST` entries were lost and restored** (an over-wide cut intended for my own duplicate line also
  removed two `test_window_contention_sweep_*` entries). Caught by diffing the `RUN_TEST(...)` set against `HEAD`. The
  invariant now holds and is checked: against **`2f0feced`** (I9) the entry set differs by **exactly one line** (this
  fixture), and against **`eefd90a5`** by **exactly one renamed entry** (176 entries both sides).
* **The fixture's own doc comment and a duplicated declaration were repaired in this commit.** The wide slice had left the
  comment describing the withdrawn behaviour (the "containment application lives there" clause) and a second copy of the
  `rollingTestTick` declaration plus a broken sentence across the helper insertion. Both are fixed here, because a comment
  that describes withdrawn behaviour is the same defect as a doc that does.
* **No `ExecutionBounds.md` row, no timing claim, no device row.**

## 7. Named, not claimed

* **No device leg.** The lane-enabled image cannot be flashed (it fails its own RAM1 guard; §4) and the default image does
  not contain this code, so nothing is claimed from hardware. No timing claim, no `ExecutionBounds.md` row change, no
  fallback removal (3.7b).
* **Containment / the approved last resort is NOT delivered** by this slice: `containmentForSlot` /
  `applyLastResortForSlot` have no production caller (§3), and a last resort owned by this resolver would silence a slot the
  retained merged fallback is about to serve. This is the item the withdrawn form attempted and the review rejected.
* **`serviceAdmittedDeadlineMisses` is unchanged**, and the shared episode counter means an adoption resets state the
  promise route fed (the earlier draft's "keeps its own count" was wrong).
* **4.5 stays open** on **production containment / the approved last resort** and on **displayed provenance** (the
  display's, per [`DISPLAY-CONSUMER-RECONCILIATION.md`](../../DISPLAY-CONSUMER-RECONCILIATION.md) → RWO **S4e**). The
  **recovery gate** itself is delivered here. **3.7a stays open** (3.7b's conditions are unchanged), and 3.14 is untouched.
