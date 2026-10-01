# EVIDENCE — S2b.7's drain-edge observables (2026-10-01, current image)

**Runs:** `captures/s2b7-drain-probe-01` (a track-5 slot-switch leg) and `captures/s2b7-drain-probe-02` (the
cross-track gesture leg). **Image:** `aed…`-class build from this session (`a1f4e4c0…` plus the row below).

## What was added, and why it is the honest shape

S2b.7's device items need two things no capture row carried: whether the deferred save is blocked by an active slot
load **at the drain edge**, and `restoreProgressCounter()` sampled **inside** the drain call (an urgent-save
discharge with no movement must *not* check off the advancement clause). A host-side command was tried first and
rejected: calling the restore-state probe pulls a 4 KB USB receive buffer into DTCM (`RAM1 variables 96,192 →
100,288`), for a read-back the firmware already performs. The row is therefore emitted where the drain is — from
predicates and counters `SyncDrainSaveState.cpp` already uses, at **no RAM1 cost**:

```
SC_PERSIST("drain_progress", progressBefore, progressAfter,
           (progressAfter != progressBefore ? 1) | (deferredSaveBlockedByActiveSlotLoadSd() ? 2 : 0),
           result.reason);
```

i.e. `PERS,drain_progress,<before>,<after>,<flags>,<verdict>` with `flags` bit 0 = the counter moved inside the
call, bit 1 = the save was blocked by the active slot load.

## Observed (`s2b7-drain-probe-01`)

```
#CAP,29387614,PERS,drain_progress,795,801,3,ok
```

* **`before=795 → after=801`** — the restore progressed **inside the drain call** (movement observed, so the
  "urgent-save with no movement" exclusion does not apply), and the drain's own verdict is `ok` (its `reason`), i.e.
  it completed rather than returning `TemporarilyBlocked`.
* **`flags=3`** — bit 0 set (moved) **and bit 1 set** (`deferredSaveBlockedByActiveSlotLoadSd()` true **at the
  drain edge**).
* The edge coincides with an active restore holding the SD session: the `RRQ` rows run continuously around it
  (`RRQ,2,4 terminal`, `RRQ,2,5 running → terminal` at 29.021/29.186 s, `RRQ,2,6` at 29.231/29.387 s, then
  `RRQ,3,2` from 29.459 s) — a boot/library restore sweep.
* No `waiting_commit_gate` denial and **no** committed pass inside the window (the run's own switch commit is at
  25.753 s, 3.6 s earlier), so "no unintended publication while the gate denies" is not violated there.

## What is **not** observed (stated rather than implied)

* **The third conjunct of S2b.7(a) — "another track playing" at that same edge — is not simultaneous in this run.**
  The `ST,Track` rows nearest the edge read `PLAYING → STOPPED` at 27.8-27.9 s (tracks stopping) and the next
  transition is `STOPPED → ARMED` at 31.195 s, so at 29.388 s the tracks were stopped. The blocking term and the
  live restore are observed; the momentary playback is not.
* The alternative permitted outcome — `TemporarilyBlocked` with the named **`waiting_commit_gate`** — did not occur
  in these runs. The name exists in `-01`'s capture once, at another moment, and is recorded as *not observed here*
  rather than as a failure.
* **`drain_progress` did not fire at all in `-02`** (the gesture leg, where a track plays throughout): the blocking
  drain is not the path a mid-run deferred save takes. So a run that holds all three conjuncts at one drain edge
  still has to be arranged; the row is what makes it *observable* when it happens.

## S2b.7 clause status after this slice

| clause | status |
|---|---|
| (a) drain fixture: hold the condition on device | **partial** — the blocking term and the held restore are observed at the edge (`flags=3` with continuous `RRQ` activity); "another track playing" at that instant is not |
| (b) report which permitted outcome, with progress defined inside the drain | **observed** — movement `795 → 801` inside the call, verdict `ok`, no publication inside the window; the `TemporarilyBlocked`/`waiting_commit_gate` outcome is recorded as not occurring in these runs |
| (c) independently reportable | **yes** — this evidence cites no other change's closeout |
