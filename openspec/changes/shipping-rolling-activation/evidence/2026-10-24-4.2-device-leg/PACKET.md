# 4.2 device leg — diverged checkpoint on a scratch slot (packet for approval)

**Goal.** Prove on the qualification image that a persisted LSV1 body whose staged root disagrees with its staged
passes is **not** installed as current, that the served slot content is the passes-derived content or no current
(never the diverged root), and measure the parity walk's load-turn cost.

**Image / build**
- Build the current source's `teensy41-capture-serial-window-qualification` (parity guard + streaming walk).
- Record the exact HEX SHA-256, RAM1 line (`392,184 + 1,032` expected) and the loader version; flash with
  `teensy_loader_cli 2.3` after the owner's PROGRAM press; keep the two-drain shutdown.
- The board currently runs `cbd4703d…` (pre-guard); the leg requires the new image.

**Scratch target (requires explicit authorization)**
- Use **track 8, slot 5** as the scratch slot (an existing bench scratch target; the mixed seed there is test content).
- The leg writes a **raw LSV1 body** to that slot's own file. That is a destructive SD write: it needs the owner's
  explicit permission for this session, a backup/quarantine copy of the slot's body file, and the slow-clear/restore
  path named in the evidence. No other slot is touched.

**Body creation and injection**
- The coherent body and the diverged body are both produced by the **production writer** in a small native helper
  (`writeLoopPersisted`), then the staged root only is mutated (a note pair's pitch or a NoteOn velocity) through
  `LoopSnapshotConstruction`; the passes/metadata/IDs are untouched. The helper writes both `.lsv1` files and a
  positive-bite record (root projection vs passes projection disagree).
- **Injection option A (recommended): host-side file patch.** The helper writes the diverged body file; the owner
  removes/reinserts the SD (or the host mounts it); only the scratch slot's body file is replaced with the diverged
  file. No firmware change; the format is the production writer's. The host keeps the original file for rollback.
- **Injection option B (fallback): test-only chunked body receive.** A `SESSION_CAPTURE` serial command receives the
  body bytes in bounded chunks and writes only the scratch slot's file. This is a firmware/host-tooling slice;
  choose it only if the SD card is not accessible to the host.
- The packet's positive bite is mandatory: read back both files with the production parser and assert the root/passes
  projections disagree, so a serialization/parse normalization cannot erase the mutation before the device sees it.

**Observation (small, temporary diagnostic; removed at closeout)**
Add a `SESSION_CAPTURE` row in `applyLoadedLoopSlotBodyCommit` on both outcomes:
`#CAP,<us>,LOAD,parity,s=<slot>,result=<ok|divergent>,events=<n>,walk_us=<us>`.
- `walk_us` is measured around the streaming compare; `events` is the compared event count (the passes stream).
- The row is flash-resident (storage TU), gated; the ITCM pins must stay at the approved line; remove it at closeout.
- The outcome is otherwise silent: with no row, "refused" and "not loaded" are the same absence (the same principle as
  the lane's `win_gate` row).

**Load and oracle**
- Load the workspace/boot path (or a slot load) with the diverged scratch body installed.
- Assert:
  1. `result=divergent` for the scratch slot, and the slot's **current checkpoint is absent or the previous
     generation** (never the diverged root);
  2. the served/effective content for that slot is the passes-derived content or empty — compare with
     `!CAPTURE_CONTENT_SLOT`/the library observation and the playback witness; the diverged pitch/velocity never
     appears;
  3. the coherent body (positive control) loads with `result=ok` and the slot current;
  4. the dense coherent body (the streaming scale fixture) loads `result=ok` and the event count/walk time are recorded.
- Name missed deadlines/refusals as failures; a fallback/empty slot is only accepted if the **named** divergent
  outcome is observed.

**Per-turn bound**
- The acceptance is the **load job's existing bound**, not a new one: no individual load turn may exceed it. `walk_us`
  plus the load job's own step time must stay inside the existing bound; if the walk exceeds it, that is a named
  failure and the packet's next step is a bounded compare split, not a bound change.
- Record the dense-body event count and `walk_us`; the same-tick group's worst case is included.

**Rollback / cleanup**
- Restore the scratch slot's original body file (or clear the scratch slot with the slow-clear path); two-Stop
  persistence drains; verify the library/effective content returns; record the exact HEX and the board state.
- The diagnostic row is removed and the final HEX re-pinned before closeout.

**Acceptance checklist**
- [ ] new qualification image built/flashed/booted; HEX + RAM1 pin recorded; two drains.
- [ ] coherent body: `result=ok`, slot current, content matches.
- [ ] diverged body: `result=divergent`, no current/old generation, passes-derived or empty served content; the
      diverged root never served.
- [ ] positive bite (root vs passes projections disagree) recorded before the device run.
- [ ] dense body: `result=ok`, event count and `walk_us` recorded; load-turn bound respected.
- [ ] rollback/cleanup complete; diagnostic removed; final HEX.
