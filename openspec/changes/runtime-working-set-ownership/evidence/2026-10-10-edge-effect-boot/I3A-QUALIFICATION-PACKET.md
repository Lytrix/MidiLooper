# I3a — edge-effect boot qualification on hardware (G2 execution packet)

Packet **I3a** of [`docs/Plans/openspec_iterative_closeout_plan.md`](../../../../../docs/Plans/openspec_iterative_closeout_plan.md)
(manifest row: boot), executed against the task clause **Qualify repaired edge-effect boot on
hardware** in `openspec/changes/runtime-working-set-ownership/tasks.md` (section *Edge-effect boot
initialization repair*). Template:
[`docs/Templates/EXECUTION_PACKET.md`](../../../../../docs/Templates/EXECUTION_PACKET.md).

## Identity and scope

- **Packet/task IDs:** I3a → *Qualify repaired edge-effect boot on hardware.* Evidence packet named by
  the plan: this folder's [`EXECUTION.md`](EXECUTION.md) (local repair + the 2026-10-10 authorized
  upload/bounded boot smoke). No `DEC-###` clause applies: the repair restores the already-intended
  empty boot state of an existing owner (`TrackManager::initializeEdgeEffectsForBoot`), and
  `docs/DECISION_LOG.md` has no edge-effect/queue-initialization decision (searched: `edge effect`,
  `EdgeEffect`, `edge-effect` — no hits).
- **Status:** device qualification, pre-flight. Not verified before the run recorded below.
- **Owner and production entry:** `main.cpp::setup` → `TrackManager::setup` →
  `initializeEdgeEffectsForBoot` (`gEdgeEffectQueue.initializeForBoot()`, `TrackManagerTransportTick.cpp`)
  → later `ClockManager::setup`; produced on the tick edge by `TrackManager::recordEdgeEffect`, consumed by
  `TrackManager::drainEdgeEffects` from `loop()`. Product entry for this packet:
  `scripts/host_midi_hitl.py run --preset base …` (managed capture; registered stimulus).
- **Pinned image:** `teensy41-capture-serial`. Qualification image = **`e6c1118f`** (boot-title fix,
  committed while this packet was open), HEX SHA-256
  `7678b719f2c26c21942baff617ff7780a1f3f5d71fd75dc3b3ba5d78185e463b`, RAM1
  `variables 96,192 / ITCM 393,216 (code 392,248 + padding 968) / locals 34,880` (floor `≥ 840`).
  The first pass of this packet ran on the earlier image `75a1bbd7` / `1f9282b6…`; the owner re-pinned the
  image mid-session ("a fix has been commit, rerun the stimulus"), so the run recorded below is on
  `e6c1118f`. The device cannot report its own firmware hash: the owner-approved upload of exactly
  `7678b719…` is recorded in [`EXECUTION.md`](../2026-10-11-boot-title-first-frame/EXECUTION.md), and
  rebuilding this tree reproduces that hash byte-for-byte.
- **Preconditions already present:** local repair landed and reviewed ([`EXECUTION.md`](EXECUTION.md));
  focused `test_runtime_working_set_ownership` 190/190; full native 3037/3030 passed/7 skipped/0 failed;
  a bounded boot smoke exists on the older repair image (`29b62f4d…`, 0 cancellation rows vs 60 pre-flash).
- **Prerequisites included in this slice:** the owner-authorized upload of the pinned hex (this session),
  the registered stimulus `STIM-BASE-2-2-01@v1` through the `base` preset, and the owner's physical-panel
  observation.
- **External blockers:** none open. Owner authorizations held for this session: flash the pinned hex;
  overwrite/clear **only** track 8 slots 4 and 5 (the stimulus's own declared targets); owner watches the
  panel. No SD rename/delete/quarantine, no other slot.
- **Mutation, postconditions, non-goals:** *no production change at all* — this packet changes no firmware,
  test, host tooling, marker, stimulus, threshold or budget line. Non-goals: display frame timing and pixel
  deadlines (S7.5b.6), the S7.5a continuity envelope (`DEC-117`), the composed seed/edit leg and its
  entry-only boundary (I3b), and the preset's own pre-existing residuals (overdub unmatched-note class,
  `persistence_stage_missing:result` report-window gap).
- **Commit boundary:** n/a (no source mutation); evidence and runtime docs only.

## Oracle fixed before behavior edits

Stable case, executed once; device clock source and tempo are read from the pre-flight capture and pinned
in the run record rather than assumed.

1. **Fresh boot of the pinned image.** The run's capture carries the firmware's own fresh-boot rows:
   `#CAP,<us>,DIAG,boot_reset,crash=0` and a boot commit row (`DIAG,loop_rem,boot_commit`). The capture is
   fresh (banner at the top, no prior-session tail), and the run's `metadata.json` records this
   `git_commit` + `firmware_hex_sha256`.
2. **No pre-publication invalid edge-effect rows.** Zero occurrences of `LoopEnd playback commit cancelled`
   (either variant — `(no committed MIDI)` / `(no RAM data)`) anywhere in the capture, and in particular
   none before the first current-boot publication. The defect signature to be absent is the pre-flash
   **60** repeated cancellation rows from stale queue records.
3. **No unsolicited drain effect.** Zero `Edge effects dropped (` warnings (the counted-never-overwritten
   overflow row) and no cancellation row whose device time precedes this boot's own restore/publication.
4. **Boot restore completion.** The boot reaches its post-restore census marker
   (`DIAG,post_restore_census` with the readiness fields attested) and `boot_commit`; the read-only census
   attach before/after the flash reads `checkpoint_readiness_attested=true` with all readiness fields `1`.
5. **Live UI handoff.** After boot the device emits its display rows (`DISP`, `DFRAME`) for the restored
   state instead of a dark screen — the 2026-10-10 dark-screen observation (alive firmware, zero
   `DFRAME`/`DISP`/`#CAP` rows) must not recur.
6. **Ordinary deferred effects.** The run's own ordinary gestures drain through the queue: the pre-record
   slot selection (`EnabledSetReplacement` / `MultiSlotCommit`, LED-refresh work, no log row) and any
   loop-end commit the leg produces (`LoopEnd playback commit track=…`, the logged kind). The LED/display
   half of these effects has **no serial row** by design, so it is carried by the owner's panel observation
   and is never inferred from rows.
7. **Canonical teardown.** The managed run performs its canonical Stop/drain before the capture closes
   (`status=complete`, `drains=2`, `ok=true`) and writes **no** incomplete-shutdown latch.

**Named inconclusive conditions (not pass, not waived).** A leg that aborts at a pre-existing host blocker
still yields cases 1–5 from its own rows, but then case 6 is only partially exercised and is reported as
such. The preset's own verdict is **not** this packet's criterion: the base preset is known to end
`INCONCLUSIVE_CAPTURE` for its own residuals at this revision, and that is recorded, never converted into
an I3a pass or an I3a failure. Physical-panel observation is required for case 6; without it I3a stays open
on that half.

## Controls and coverage

- Production paths exercised: `main.cpp::setup` → `TrackManager::setup` →
  `initializeEdgeEffectsForBoot`; `recordEdgeEffect`/`drainEdgeEffects` on the ordinary gesture path; the
  boot restore path that publishes the previous session's SD content.
- Reused seams: the canonical managed entry point and its `DeviceSession` teardown, the read-only
  `scripts/hitl_boot_census.py` census/`!HEAP_STATS` reader, the registered stimulus list. No new
  instrument, no marker, no stimulus variant.
- Counterexample that must fail (= the defect): a boot whose retained queue metadata is taken as live and
  drained as cancellations. The pre-flash evidence for exactly that failure is recorded in this folder's
  [`EXECUTION.md`](EXECUTION.md) (60 rows).
- Baseline/failure reuse: the same entry point's earlier runs at this revision (`captures/host-grid-record-t8s4-05`,
  `captures/edit-entry-probe-t8s4-05`) are non-PASS or non-census runs used only as context, never as passes.
- Bounded drain limit: `EdgeEffectQueue::kCapacity` per call, unchanged; the run is bounded by
  `HITL_MANAGED_RUN_TIMEOUT_S` (360 s default).

## Verification recipe

- Pre-flight (no reset): `.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/i3a-edge-effect-boot-preflight-01 --no-reset`
  → read-only attach: latch absent, device state, and the device-reported tempo used to pin the leg.
- Upload (owner-authorized): the pinned hex via `pio run -e teensy41-capture-serial -t upload`
  (`--build-upload` path or `hitl_boot_census.py`'s own re-upload), hash re-checked before and after;
  then the post-flash census attach (`captures/i3a-edge-effect-boot-postflash-01`) for the fresh-boot
  banner + `boot_commit` + post-restore census + `!HEAP_STATS`.
- Verdict run: `.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4
  --midi-channel 8 --boot-reset --capture-boot-wait 30 --out-dir captures/i3a-edge-effect-boot-t8s4-01`
  (with the clock/tempo flags this packet's pre-flight pins, if the leg needs them), while the owner
  watches the panel.
- Row-level reading of the capture against cases 1–7 above.
- Not required and therefore not run: `pio test -e native` (no source change), the window-qualification
  build (no lane row or RAM1 guard in play).
- Evidence path: this folder (`I3A-QUALIFICATION-PACKET.md` + the completion record appended to
  [`EXECUTION.md`](EXECUTION.md)). Baseline revision `feature/unified-rolling-playback-path @ 75a1bbd7`
  with the unrelated dirty tree preserved.

**Qualified on hardware, 2026-10-12, image `e6c1118f` / `7678b719…`** — all seven oracle cases hold on
`captures/i3a-edge-effect-boot-t8s4-04` (fresh `crash=0` boot, `boot_commit`, post-restore census all
readiness `1`, **0** invalid edge-effect rows, **0** drop warnings, live `DISP`/`DFRAME` handoff, the full
ordinary leg with the owner's panel report *"Normal — no display hang after boot this time"*, canonical
`complete`/`drains 2`/`ok true` teardown and no latch). The same run's `FAIL (FIRMWARE_CONTRADICTION)` on
`overdub_stored_span_too_short` / `second_overdub_stored_span_too_short` is recorded as a separate
content finding, never as an I3a result. Full record:
[`EXECUTION.md`](EXECUTION.md#i3a-hardware-qualification-on-the-pinned-image-2026-10-12). The leg ran with
the owner-directed host fix from
[`../2026-10-12-base-leg-boot-handoff-gate/EXECUTION.md`](../2026-10-12-base-leg-boot-handoff-gate/EXECUTION.md)
and `--clock-source host`.

## Completion record

*(filled after the run; see the appended qualification record in [`EXECUTION.md`](EXECUTION.md))*
