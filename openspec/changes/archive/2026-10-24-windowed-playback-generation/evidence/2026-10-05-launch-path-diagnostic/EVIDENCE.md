# The launch path's gate row: the silent exits are named, and the switch leg's first failed transition is the **source publication**

Evidence for `EXECUTION.md` in this directory. Three device episodes on the qualification image
(`teensy41-capture-serial-window-qualification`, `variables 96,192 / code 392,232 + padding 984`), one
new qualification-only row family, and the answer to the question five earlier runs could not settle.

## 1. What the earlier trace got wrong (corrected here, and it changed the instrument)

The four corrections the slice was told to make are not stylistic; each one moved either the design of
the diagnostic or the reading of the result.

| # | The earlier trace said | What is true | Why it mattered |
|---|---|---|---|
| 1 | the six `playback_build,snapshot` rows are "the lane serving a demoted 24-bar slot" | `snapshot` is the **merged representation's** own rebuild path label (`Loop`'s `sourceOrigin`, `RuntimeTimingTelemetry`'s path name) — a gather from a published root, **not** an immutable/windowed build | the lane's work was never established by those rows; the row that names *lane* work is the lane's own (`grain_denied`, `build_complete`, …) |
| 2 | `RRQ,7,4,…,terminal,none,committed` shows "the target's boot publication completed" | it is the **restore request's** terminal — the load job finishing. It says nothing about a root being published or current | measured in this episode: the target is `root=0` at switch time, so "the root was published" was false all along |
| 3 | the exits are equally open | `requestCommittedPassesRepublish()` is called **before** `prewarmRollingViews()` (`TrackPlaybackWindowBuild.cpp:2151-2152` vs `:2167`) | a refused or skipped request explains a *later* silent exit; a row must carry that fact on the same line or the two are indistinguishable |
| 4 | the leading cause is `:2188`'s `coverageRoleAvailable()` under the active slot's edit session | `coverageRoleAvailable()` reads **only the target slot's own runtime** (`viewSetIfPrewarmed`, `viewBuild`, `completedPromisePendingJudgement`, `ownsCoverageSource`, `canonicalWindowPhase`, `lifecycle`) — another slot's `note_edit` activity cannot reach it | measured here: the target's role is **available** (`role=1`) at the decision, with the loop-edit session still open. Another slot's activity never established a target-slot conflict |

Also carried over and unchanged: `win_gap`'s `eligible`/`progress` counters are not a work indicator (they
read 0 while the lane serves), so lane work is read from the lane's own rows.

## 2. The instrument (qualification-only, on state change, target identity)

`DIAG,launch_gate,t=<track>,s=<slot>,x=<exit>,len=<loopLengthTicks>,prep=<0..6>,root=,pend=,role=,own=,dem=,bld=,cpm=,avoid=,cmt=,req=`

- assembled by `Track::reportPlaybackLaunchGateRow` from the target slot's own state, emitted by
  `Track::reportPlaybackLaunchForSlot`'s **every** exit and by the two decisions its caller
  (`TrackManager::servicePendingSlotProvision`) takes before reaching it;
- **one row per state change**, never per turn: the tuple (exit, length, flags, preparation) is compared
  against the last emitted one for that `(track, slot)`, so a switch that sits on one exit for hundreds of
  deferred turns costs one row while every move is still named;
- `req` carries the source-publication question the trace could not answer: 0 not attempted, 1 accepted
  (the source is now owed), 2 **refused** (`queueAllCommittedPassesInto`'s silent failure);
- `prep` is the `LaunchPreparation` ordinal and `x` names the exit (`launch_requested`,
  `launch_requested_match`, `launch_role_unavailable`, `launch_prewarm_refused`,
  `launch_build_owned_by_capture`, `launch_provision_refused`, `launch_not_reached_legacy`,
  `launch_not_reached_occupied`, … — the closed list is in the header);
- **qualification-only by build**: `SC_LAUNCH_GATE_ENABLED` is 1 only for a capture build that also opens
  the rolling lane's gate (and for native, where the row is checked). Every other image — the shared
  capture env, the plain firmware — compiles neither the row nor the state that bounds its cadence, so
  "preserve the fallback and the default-off enablement" is a property of the build rather than a runtime
  flag;
- **it observes only**: no guard, no order, no fallback and no admission is changed. The only production
  line that moved for it is the caller's refusal branch, which now calls a reporter before returning the
  same `false`.

**Two delivery rules, and they were earned on device.** The first two episodes produced **no** row at all
(runs 01/02 below) while the firmware demonstrably carried it: the ring is *retained by design* while a
transport runs, and the switch's preparation happens exactly then. The row is now emitted **directly when
USB has room** and enqueued on the ring otherwise — the save-completion receipt's own two rules
(`readinessReceiptImmediate`) — and its tag is registered as **Tier-A** in `isTierATextLine`, the trap
`DIAG,win_gap_overrun,` already recorded.

## 3. The episodes

| # | capture | what happened |
|---|---|---|
| 01 | `captures/s64-switch-07` | the driver was asked for `--build-upload`; the upload succeeded and the run then aborted at `mido.open_output` ("No MIDI output port matching 'Teensy'") — the post-flash re-enumeration. **No gesture was sent**, so no device state changed |
| 02 | `captures/s64-switch-08` | a complete episode (switch queued, NOTE_EDIT closed, hold reported, stop, 12 s drain) and **zero `#CAP` rows in the drain**: the ring's head had gone bad at 23.4 s (`RING,head_bad` / `head_resync`) and it delivered nothing afterwards. The launch-gate row existed in the ELF and was fetched by nobody. This is the failure the two delivery rules above answer |
| 03 | `captures/s64-switch-09` | **the authoritative episode.** Same leg, same target, plus the Tier-A registration *and* the direct-emission rule: 16,649 drained lines, and the gate rows arrive live |

Episode 03's identity and shape: image `teensy41-capture-serial-window-qualification` (RAM1 line
byte-identical to the approved one), track 8 (lane 7), the leg's own 24-bar loop on lane slot 5, the switch
target lane **slot 4** ("Loop 5"), and the driver's three presses in the order that makes the gesture safe:

```
[139.357] Loop 5: Queued playback switch          <- the selection queues the switch (transport running)
[139.443] #CAP,139443092,DIAG,launch_gate,t=7,s=4,x=launch_requested,len=18432,prep=0,root=0,pend=1,
                     role=1,own=0,dem=0,bld=0,cpm=0,avoid=1,cmt=1,req=1
[139.480] #CAP,139479670,DIAG,launch_gate,t=7,s=4,x=launch_requested_match,len=18432,prep=1,root=0,pend=1,
                     role=1,own=0,dem=1,bld=0,cpm=0,avoid=1,cmt=1,req=0
[140.207] MIDI Encoder: Long press - exited edit mode
[147.319] Slot switch hold slot_switch_hold_exceeded track=7 target=4 age=3074 ticks
[161.361] Transport stopped
```

`len=18432` — the target states its own geometry, and it is the demoted 24-bar shape. `root=0` — **no
current published root**, which is the correction of #2 above. `avoid=1`, `cmt=1` — one committed pass and
a length above the demotion bound, so the branch is the immutable launch. `role=1`, `own=0`, `bld=0`,
`cpm=0`, `dem=0` — every role/build/pending guard was **open at the decision**. `req=1` — the missing
source was **requested and the request was accepted**.

## 4. The first failed transition

**The launch prepared and asked for its source successfully; the source publication never became current.**

The second row is the state for the rest of the episode: the row is emitted only on a **change**, and no
third row appears — so `(x=launch_requested_match, prep=1, dem=1, root=0, pend=1, role=1, bld=0)` held from
139.48 s to the end of the capture (172.9 s), across the hold bound and the transport stop. A completed
publication would have moved `root` to 1 and `pend` to 0 and produced a row; a role/build/pending refusal
would have changed the exit. Neither happened.

What that excludes, by measurement rather than by argument:

- **not provisioning**: no `launch_provision_refused` row and no `#MEM_DENIED` in the run;
- **not the edit session** (the trace's leading hypothesis): `role=1` on the target at 139.443 s, and the
  switch's hold continued after `Long press - exited edit mode` at 140.207 s without the launch changing
  state — the session neither closed the role nor unblocked the publication;
- **not the prewarm**: the exit is past `:2167`;
- **not a pending/positioned demand or another prepared owner**: `dem=0`, `own=0`, `bld=0`, `cpm=0`;
- **not the queue's own rows**: the target produced **no row of any family** for the whole episode (0 rows
  naming `,7,4,` after the request).

What remains as the named mechanism, read from the code: the publication owner's slot selector,

```cpp
// src/Track/TrackSlotPool.cpp:128
Loop& Track::loopWithPendingSnapshotPublish() {
  Loop& active = getActiveLoop();
  if (active.hasPendingSnapshotPublish()) {
    return active;                       // the ACTIVE loop always wins
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) { … }
  return active;
}
```

gives the **active loop absolute priority** whenever it has any pending publish, and the `Snapshot` owner's
turn is bound to that loop (`TrackDeferredMaintenance.cpp:1022`). A demoted, **non-active** target's
on-demand request therefore waits behind the active slot's own publication work, and the launch can only
ever be prepared while the active slot is the one the operator left — i.e. exactly while the active slot is
busy. Two things keep this from being a claim here: the ring **shed the window's rows** in this episode
(`RING,tier_a_evicted,3,first=132125944` and `RING,shed,56863`), so what the publisher did between 139 s and
161 s is not observable in this capture; and the gate that refuses the idle snapshot turn has no row in
this build (`SESSION_CAPTURE_SNAPSHOT_IDLE` defaults to 0). **Naming which of the two refuses needs one
more row, not another episode** — the same class of visibility gap as F-16, at the *next* owner upstream.

## 5. Verification

- Focused: `test_runtime_working_set_ownership` **143/143** and `test_capture_ring` **159/159** (with
  `test_capture_line_tier` in the same run) — the new fixture drives the real production turn on the
  demoted, rootless shape and pins the row's name, its target identity, the deciding inputs, the
  **state-change cadence** (two identical turns emit nothing), the source-arrival transition, and the three
  direct-call exits; the ring suite pins the Tier-A boundary (`DIAG,launch_gate,` in, `DIAG,launch_gatex,`
  out).
- Full native: **2,797 cases / 7 skipped / 2,790 succeeded / 0 failed**.
- Builds: `teensy41-capture-serial` **SUCCESS** with RAM1 unchanged (`variables 96,192 / code 391,576 +
  padding 1,640 / locals 34,880`, 800 B above the floor) and
  `teensy41-capture-serial-window-qualification` **SUCCESS** with the approved line byte-identical
  (`variables 96,192 / code 392,232 + padding 984`). The added code is `TRACK_COLD_MEM`/`FLASHMEM`, so no
  ITCM and no DTCM moved; the qualification build was uploaded twice and the second flash is the episode's
  image.
- `scripts/check_marker_contract.py --audit`: 84 Tier-A prefixes, **0 over** the 31-byte limit
  (`DIAG,launch_gate,` is 17).
- `git diff --check` clean.
- **Not done, and named rather than implied:** no fix of the publication priority (out of scope: the slice
  was asked for the diagnostic and the first failed transition), no claim about 6.4/6.5, F-7/F-8/AC-8, and
  no content-parity run. The tree also carries an **unrelated** in-progress slice (the
  `DIAG,canon_rel_interval,` rename plus HITL authoring docs/scripts) that appeared during this session;
  it is untouched here and its one hunk inside `DebugSessionCapture.cpp` is not mine.
