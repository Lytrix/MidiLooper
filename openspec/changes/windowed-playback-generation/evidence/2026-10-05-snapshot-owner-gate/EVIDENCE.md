# The `Snapshot` owner's gate row: the publication side is named, and the episode that would have reproduced it lost its target shape

Follow-on to [`../2026-10-05-launch-path-diagnostic/EVIDENCE.md`](../2026-10-05-launch-path-diagnostic/EVIDENCE.md),
whose closing question this slice answers one owner upstream: *the launch asked for its missing source successfully and
the publication never became current — which loop was the owner working on, and which gate refused?*

## 1. The row

`DIAG,snap_gate,t=<track>,s=<selected slot>,a=<active slot>,u=<units>,p=,sep=,fc=,wj=,hp=,lr=,vc=,rec=,play=,od=,step=,cl=,idle=,elig=`

Emitted by `Track::reportSnapshotOwnerGateRow` from the `Snapshot` branch of
`Track::processDeferredIdleMaintenance`, **once per state change** per track, **qualification-only**
(`SC_QUALIFICATION_ROWS_ENABLED`), Tier-A, and delivered directly when USB has room (the two rules the launch row
earned). `s` is the loop `Track::loopWithPendingSnapshotPublish` **selected**, printed beside the active slot, so
starvation is visible as `s == a` with `p=1` while another slot owes work; `step`/`cl`/`idle`/`elig` are the turn's own
verdicts, so the *refusing* gate is read off the row rather than inferred. The reserve/yield gate is not a candidate in
this build (`kSnapshotIdleReserveYieldEnabled` is a `false` constexpr).

**Reading rule the episode produced:** `s == a` with `p=0` is *not* starvation — nothing was owed anywhere, and the
selector's fallback returns the active loop. Only `p=1` on a selected loop says work existed; only then does the pair
`(s, a)` say who got it.

## 2. The mechanism, pinned natively **with a red proof**

`test_shared_capture_finalization::test_snapshot_owner_selects_the_active_loop_and_starves_a_pending_slot` drives the
**production turn** (not the publication primitive the earlier regression stepped by hand — that seam is exactly what
hid the loop choice): a demoted non-active target at slot 2 with an accepted republish request and no root, and the
active loop at slot **3** with its own 24-bar publication owed. Eight Snapshot turns later the target has never been
selected, its request is still pending and its root still absent; the contrast track (active loop owing nothing)
selects the pending slot instead.

* **M1 (red proof):** with the active-loop preference removed (`if (false && active.hasPendingSnapshotPublish())`), the
  selector returns the pending slot 2 and the fixture fails at the first row — `Expected 3 Was 2`. The mutation was
  restored and the file is byte-identical (`TrackSlotPool.cpp` sha256 `1bc5db42d3f2d9f5…` before and after).
* The first draft of this fixture did **not** discriminate (`active` was slot 0, so the walk's own order also returned
  it) and M1 survived it — recorded rather than glossed: a fixture whose two mechanisms agree cannot prove either.

## 3. The episode (`captures/s64-switch-11`, exit 0, leg `serial verification ok=True`)

The row works on device and its fields discriminate the states — 170 rows in the run, one per state change, including
the whole leg. Two readings it produced:

```
#CAP,122997721,DIAG,snap_gate,t=7,s=5,a=5,u=2,p=1,sep=1,fc=1,wj=0,hp=0,lr=1,vc=1,rec=0,play=1,od=0,step=1,cl=1,idle=1,elig=1
#CAP,167771576,DIAG,snap_gate,t=7,s=5,a=5,u=2,p=0,sep=0,fc=0,wj=0,hp=1,lr=1,vc=1,rec=0,play=1,od=0,step=0,cl=1,idle=0,elig=0
```

* owed **and** playing → the owner **was admitted** (`step=1, idle=1, elig=1`), so the owner is not categorically
  blocked under transport — one loop at a time is served;
* nothing owed and playing → the owner refused itself (`step=0`) because the selected loop has a root and nothing
  pending: harmless, and now visible rather than mysterious.

**And the reason this episode could not reproduce the starvation** — its own first row says it:

```
#CAP,167768815,DIAG,launch_gate,t=7,s=4,x=launch_not_reached_occupied,len=1536,prep=0,root=1,pend=0,
                     role=0,own=0,dem=0,bld=0,cpm=0,avoid=0,cmt=1,req=0
```

The switch target is a **2-bar, root-backed** slot: `avoid=0` (below the demotion bound) and `root=1` (nothing to
request), so the immutable-launch branch was never taken and no release request existed to starve. That is the *bench*,
not the code: a read-only library observation (`captures/s64-census-02`, `--library-observe --no-reset`) puts track 7
slot 4 at `loop_length_ticks 1536 / 2 passes / root_is_current true`, while the only rootless long body in the library —
`18432` ticks, 5 passes — is on **track 4 slot 0**, outside the previously authorized scratch range. Two flashes
(fresh boot → runtime rebuilt from the card) are what reset lane slot 4 from the 24-bar body run 09 switched to.

## 4. Verification

* `test_shared_capture_finalization` **61/61** with the new fixture; `test_capture_ring` pins the Tier-A boundary
  (`DIAG,snap_gate,` in, `DIAG,snap_gatex,` out); `test_runtime_working_set_ownership` **143/143**.
* Full native: **2,798 cases / 7 skipped / 2,791 succeeded / 0 failed**.
* Both capture builds **SUCCESS** with their RAM1 lines byte-identical (`variables 96,192`; qualification
  `code 392,232 + padding 984`); `scripts/check_marker_contract.py --audit` reports 0 prefixes over the 31-byte limit;
  `git diff --check` clean.
* Device: the qualification image uploaded; **run 10 aborted at the leg's own `clear failed` precondition** (a recorded
  setup flake, no capture written, no gesture beyond the clear attempt) and **run 11 is the episode above**.
* **Honest limits:** the starvation is proven **natively** (with its red proof) and the device row is proven to *name*
  the selection and the verdicts; the device has **not** yet been observed starving a pending non-active slot. That
  needs a demoted rootless target on the driven track, which the current bench does not have — and rebuilding it means
  a bench write (a >16-bar take into a track-7 slot plus a reboot, hoping the restore comes back pass-backed) outside
  the previously authorized slot range. **Not claimed:** no fix of the selector, no 6.4/6.5, F-7/F-8 or parity claim.
