# Switch route / clean-bench finding (2026-10-24)

The accepted-sequence mixed switch is **setup-limited on this bench**, not lane-limited:

- The archived cold run (`2026-10-24-s315f-closeout-mixed-qual-2/accepted-sequence-mixed`, HEX
  `dab405b3…`) ends **`PREPARATION_FAILED`**: the slot-5 seed_b `finalization release unconfirmed within 10s;
  next target was not prepared`. Its own row shows the track-8 master at **18432 ticks (24 bars)**
  (`#CAP,56125906,DISP,4,STOPPED,18432,…`), while the seed_b stimulus is a 2-bar/16-note seed. That is the
  reader change's known **mixed-oracle setup limitation**.
- The current set carries the dense 24-bar on track 8; clearing slots 4/5 (authorized bench slots) does not
  reset the **master loop length**. A 2-bar seed_b in a 24-bar master cannot finalize as the mixed stimulus
  expects.
- Bench clears performed (standing slot authorization): track 8 slot 4 (0-based 3) cleared (`length=0`);
  track 8 slot 5 (0-based 4) still reports `length=19200` (the failed seed_b), and the base slot (track 5 slot 1)
  was cleared for the cold base check. The cold base on this set records **32/32, loop 1536**, but the full run is
  `INCONCLUSIVE_CAPTURE` (overdubs 0/0) — the same set/capture state limit.
- Therefore switch parity (cold + shipping) needs a **clean set with a base master** (a new workspace / new set),
  not slot clears alone. The 1.4 switch row is a **named setup-limited gap** until then; the manual mixed gesture
  was confirmed on the qualification image in the earlier campaign, but that is a gesture, not a ordered
  content/ledger parity.

**Not claimed:** no switch parity, no full cold base parity. The production-demand lane parity (dense,
qualification image) and the shipping base content parity stand.


## Set-browser attempt (owner-selected clean-bench path) — cannot provide an empty master

- `!REV_COMMIT` committed the current bench as **S3 v20** ("Revision commit complete S3 v20"), preserving it.
- The set-browser **root/save row** is a **revision commit of the current workspace**
  (`beginOverlaySaveRowCommit` → `requestCommitRevision`), not a new empty set. The root list exposes the saved
  sets/revisions; there is no "create empty set" action.
- The S3 revision history contains campaign states: **v1** is a 65-bar track-8 / 26-bar track-5 set
  (`LIBOBS,S,t=7,s=0,…l=49920` and `t=4,s=0,…l=19968`), and **v20** is the current 24-bar state. The **base
  2-bar** state is not among them. Loading v1 does not produce a 2-bar master; the accepted-sequence seed_b
  still records into a long master.
- Conclusion: the switch/display/full-rollback runs cannot be obtained through the set browser or revision load.
  They need either the **workspace quarantine** (a true empty workspace; explicitly declined for now) or an
  explicit **empty/new set** capability. The user's S3 revisions are preserved; the current bench is restored
  from the revision state (v20 load requested; the device settles the load).

**Not claimed:** switch parity and full cold-base parity. The production-demand activation, shipping base
content parity, qualification dense lane parity, and the cold rollback image stand.
