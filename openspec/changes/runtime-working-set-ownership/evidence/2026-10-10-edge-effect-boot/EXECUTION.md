# Edge-effect boot initialization — execution packet

## Identity and scope

- Status: native/build verified; hardware qualification NOT RUN.
- User authorized OpenSpec capture and implementation after the bounded read-only investigation, on `feature/unified-rolling-playback-path` with the existing dirty tree preserved.
- Owner/path: `main.cpp::setup` → existing `TrackManager::setup` → boot-only edge-effect initialization → later `ClockManager::setup`/clock publications → `loop`/`drainEdgeEffects`.
- Files: `Utils/EdgeEffects.h`, `TrackManager.h`, `TrackManagerTransportTick.cpp`, `TrackManager.cpp`; existing `test_runtime_working_set_ownership` directly includes the real owner/queue TU.
- Preconditions: boot setup only, no queue producer or consumer active. No runtime reset/recovery; no new owner, scheduler, flags, memory, capacity, allocation, markers, SD format or content changes.
- Mutation: reset queue producer/consumer positions and dropped count, then reported-drop baseline. Payload untouched; postcondition empty queue/no unpublished payload visible. Commit boundary: independently buildable local repair; device qualification stays separate.
- Resource/timing: same 264-byte RAM2 queue plus existing 4-byte report baseline. Cold initializer belongs in FLASHMEM; no new clock-edge work or RAM1 variables. Existing fixed 16-record drain remains unchanged.

## Historical decision review / brief preflight

Searches: `docs/DECISION_LOG.md` for EdgeEffect/edge effect/DMAMEM/initialization; active change and `openspec/specs` for edge-effect ownership/boot behavior. No conflicting initialization decision or archived queue requirement found. Reuse existing TrackManager boot entry and queue owner, not a new abstraction. Ownership/execution-bound authority: `ARCHITECTURE_RULES.md` and `Architecture/ExecutionBounds.md` deferred-effect owner row. Restoring the already-intended empty boot state does not transfer ownership or introduce a new runtime transition; no architecture reassessment or new DEC entry required. No persisted-state migration. The earlier parked DMA-fault repair is not this queue-initialization defect and remains parked.

## Oracle frozen before firmware edits

1. Poison the real global queue byte representation (all record bytes stale; positions `write=0/read=1` and `write=0/read=65535`, plus equal-position case); poison drop count and reported baseline. Call real `TrackManager::setup`. Observe depth=0, drops=0, reported=0, no record available, no drain log. Payload bytes remain unchanged. Clean fixture state before assertions so baseline failure does not contaminate other tests.
2. After boot initialization, publish a real owner cancellation for track7/slot3. No log until drain; one exact legal cancellation row after drain, no duplicate on a second drain.
3. Existing FIFO/full rejection test remains. Additional fixed 4097 batches of 16 fill/reject/drain cycles cross uint16 rollover, pinning track/slot/geometry payload, depth and exact drop count. No sleeps or scheduling assumptions.
4. Counterexample: removing the boot initialization must fail the poisoned-storage assertion. Source/linked caller inspection pins setup before clock and drain; native suite does not emulate Teensy startup or peripheral DMA.

## Verification recipe

- Baseline then fixed: `pio test -e native -f test_runtime_working_set_ownership`; exact result excerpts below; raw logs retained in session scratch as `edge-baseline.log`/`edge-focused.log`.
- Broad: `pio test -e native`.
- Build: `pio run -e teensy41-capture-serial`, automatic RAM1-budget hook must pass without rebasing. Inspect ELF for RAM2 queue placement, cold initializer and real setup call chain; record new HEX hash (not uploaded).
- Strict OpenSpec validation and whitespace check; one bounded independent review of this slice, not accumulated dirty work.
- Device: NOT RUN; upload needs authorization. Catalog listed before implementation (`host_midi_hitl.py stimulus list`). Reuse `STIM-BASE-2-2-01@v1` through the authoritative `base` preset only after explicit target overwrite permission, with boot/drain evidence. No canonical stimulus edits/new serial markers. Fresh boot and ordinary deferred effects must be observed; display timing recovery is not inferred from absent shed capture rows.

## Completion record

- Fixture baseline (before firmware changes): poison regression `Expected 0 Was 65535`; new deferred/rollover controls passed. PlatformIO exit1, 189 succeeded/1 failed plus SIGHUP/ERRORED: not a clean suite completion. Cleanup prevented contamination.
- Fixed focused: `pio test -e native -f test_runtime_working_set_ownership`, exit0, 190/190, all three new cases pass.
- Full native: `pio test -e native`, exit0, 3037 total/3030 succeeded/7 skipped/0 failed; 112 PASSED suites. Raw log `edge-native.log` in session scratch.
- Capture build: `pio run -e teensy41-capture-serial`, SUCCESS/exit0. RAM1 variables96,192, code392,360, padding856, locals34,880; approved floor840, margin16. RAM2 variables163,360; EXTRAM9,312. All unchanged; no budget rebase. Raw log `edge-build.log` in session scratch.
- Linked verification: queue remains RAM2 at `0x20207f8c`, size`0x108`; reported baseline at `0x20208094`. `TrackManager::initializeEdgeEffectsForBoot` is flash at `0x600838c0`, 28 bytes, stores zero at offsets256/258/260/264 only. `TrackManager::setup` calls its flash veneer first; `main.cpp.o` relocations call TrackManager setup at offset`0x11e8`, before ClockManager setup at`0x12ba`. No payload stores or clock-edge change.
- HEX SHA-256: `29b62f4d829f28937b558422ececfccd2ddf261960754eecd4bf8950c38df50d`. Initially built without upload; subsequently uploaded unchanged with owner authorization (see upload record below), replacing `9e8270b41551ae4609b44362ad7fb696a2a6e6f0a4ad7e39184f757ce90b3dec`.
- One independent focused source review: APPROVE, no actionable findings; boot quiescence, production order, metadata-only reset and test independence checked. Reviewer did not independently run tests; full native/build were performed by the implementing agent.
- `openspec validate runtime-working-set-ownership --strict` and `git diff --check` pass. Requirement/oracle unchanged. Local-repair task eligible for checkoff; hardware/display gate remains unchecked.
- At local implementation closeout, device verification was NOT RUN. Subsequent authorized upload/smoke evidence is below; registered ordinary-effect qualification and physical panel recovery remain unproven. No broad DMA repair or behavioral gates for other tasks closed.
- Session closeout: runtime state/current work/deliverable tracking/hardware journal updated; OpenSpec requirement/design/tasks captured. No new architectural decision or exclusions beyond the authorized initialization repair, hence no DEC append; no overlap/ledger/playback-order logic changed. No commit/push/PR.

## Authorized upload and bounded boot smoke (2026-10-10)

Owner approved upload only. Existing managed capture (`boot_reset=never`) and `DeviceSession` performed two idempotent MIDI Stop → fenced STOPPED → verified-persistence-idle cycles before flash, and again after flash; both `status=complete, drains=2, ok=true`. No incomplete-shutdown latch. Stop sender reused from `hitl.managed_capture`; queries only `!CAP_IDENTITY_STATS`/`!STOP_STATUS`. No recording, selection, slot clear, SD rename/delete/quarantine or stimulus mutation. No native rerun: firmware unchanged from the verified build.

- Preflight: `captures/edge-effect-boot-upload-pre/shutdown-result.json`, capture `session_20261010_164000.log`: 60 repeated cancellation rows, 1863 LED rows, ring shed65324/Tier-A eviction226, overflow0/critical rejection0.
- `pio run -e teensy41-capture-serial -t upload`: SUCCESS/exit0, 13.804s. HEX hash before/after exactly `29b62f4d…`; RAM1 variables96192/code392360/padding856, floor840 unchanged. `/dev/cu.usbmodem154944801` reconnected.
- Postflight: `captures/edge-effect-boot-upload-post/shutdown-result.json`, capture `session_20261010_164046.log`: fresh `DIAG,boot_reset,crash=0` at3005005us; `DIAG,loop_rem,boot_commit` at14168747us; 0 cancellation/drop-warning rows, 151 LED rows, 1 DISP and8 DFRAME rows. Fenced stats at9541450us: attempts113/ok113, shed0/Tier-A eviction0/overflow0/critical rejection0. This stats sample precedes boot_commit, not a terminal full-window loss guarantee.
- Outcome: upload/reconnect/responsive stopped firmware and bounded fresh-boot smoke verified. The previous repeated invalid-record drain did not recur in this capture. Physical display illumination, all-restored-content integrity and ordinary current-boot queued effects under a registered stimulus were NOT tested; hardware task remains unchecked. No commit/push/PR.

## I3a hardware qualification on the pinned image (2026-10-12)

**Packet I3a of the iterative closeout plan, executed on hardware; the clause is qualified on the pinned
image `e6c1118f` / HEX `7678b719…`.** G2 packet and frozen oracle:
[`I3A-QUALIFICATION-PACKET.md`](I3A-QUALIFICATION-PACKET.md). Owner authorizations for this session:
flash the pinned hex; overwrite/clear only track 8 slots 4–5; owner watches the panel.

**Runs and what each contributed.**

| Run | Image | Outcome |
|---|---|---|
| `captures/i3a-edge-effect-boot-preflight-01` (`hitl_boot_census --no-reset`) | `1f9282b6…` | read-only attach: device alive, display frames advancing, DMA clean, no crash rows, no latch present |
| `captures/i3a-edge-effect-boot-postflash-01` (census re-upload, authorized) | `1f9282b6…` | fresh `DIAG,boot_reset,crash=0`, `boot_commit` 10.39 s, `post_restore_census` all readiness fields `1` (`free_chunks=403`), **0** invalid rows; the tool itself then died at its later `!HEAP_STATS` write (port vanished) and wrote no `census.json` — named, the rows stand |
| `captures/i3a-edge-effect-boot-t8s4-01` (registered `STIM-BASE-2-2-01@v1` via the `base` preset) | `1f9282b6…` | **PASS**, full leg (record + 2 overdubs + undo/redo), 0 invalid rows — the boot/edge-effect half, on the image the owner then replaced |
| owner re-pinned the image mid-session (`e6c1118f`, boot-title fix) and asked for a rerun | — | — |
| `…-rerun-t8s4-02` | `7678b719…` | abort `recording not confirmed before note stream`: the leg pressed during an unfinished boot and its `LOOP_EDIT` proof raced the boot's own ack → the device read the press as 1 ms and cycled `LOOP_EDIT → NOTE_EDIT` |
| `…-rerun-t8s4-03` | `7678b719…` | abort `second overdub start refused: no stop-completion receipt` (the pre-existing S7.5b.5 class); its baseline-report cleanup left an **incomplete-shutdown latch** |
| `captures/i3a-latch-discharge-01` | `7678b719…` | latch discharged in the documented order: read-only attach, `drains=2`, `complete`/`ok true`, payload preserved, latch cleared |
| **`captures/i3a-edge-effect-boot-t8s4-04`** (same registered stimulus, `--clock-source host --tempo-bpm 300` after the owner-directed stimulus fix) | `7678b719…` | **the qualification run**: handoff gate honoured, no edit-mode press, every phase completed, all seven cases below hold |

**Case-by-case, on `…-t8s4-04` (metadata: `git_commit e6c1118f…`, `firmware_hex_sha256 7678b719…`,
preset `base`, track 8 / slot 4):**

1. **Fresh pinned-image boot** — `#CAP,854542…`-style banner `DIAG,boot_reset,crash=0` twice (direct +
   ring), `crash=1` never, capture fresh.
2. **No pre-publication invalid edge-effect rows** — `LoopEnd playback commit cancelled` **0** occurrences
   (the pre-flash defect produced 60: `track=33 target=3` ×30 and `track=12 target=194` ×30 in
   `captures/edge-effect-boot-upload-pre`).
3. **No unsolicited drain effect** — `Edge effects dropped` **0**; ring `shed`/`overflow`/`tierA_evict`
   show no content loss.
4. **Boot restore completion** — `DIAG,loop_rem,boot_commit` present; `DIAG,post_restore_census` with
   `admission_complete=1 interactive_ready=1 undo_hydrated=1 transport_stopped=1 deferred_prewarm_idle=1`.
5. **Live UI handoff** — 69 `,DISP,` and 38 `DFRAME` rows after boot, display states advancing through
   `STOPPED → ARMED → RECORDING → PLAYING`; owner, watching the panel: *"Normal — no display hang after
   boot this time"*.
6. **Ordinary deferred effects** — the leg drove the whole ordinary path (record, two overdubs, undo/redo,
   transport stops) with zero invalid or unsolicited queue rows, and the LED/display half of those effects
   is the owner's panel observation above — observed, not inferred.
7. **Canonical teardown** — managed `shutdown-result.json` `complete` / `drains 2` / `ok true`; no
   incomplete-shutdown latch written.

**Named, not smoothed.** (a) The run's own verdict is **`FAIL (FIRMWARE_CONTRADICTION)`** on content
scoring — `overdub_stored_span_too_short`, `second_overdub_stored_span_too_short` (stored spans
`last=2241`, `min_span=1296`, `bars=3/2` in a 1536-tick loop) — a stored-span/wrap **content** finding
outside I3a's five items; it is handed over, never used to fail or to pass I3a. (b) The two aborted reruns
are recorded above with their causes rather than dropped. (c) The qualification rides on the owner-directed
host fix in
[`evidence/2026-10-12-base-leg-boot-handoff-gate/EXECUTION.md`](../2026-10-12-base-leg-boot-handoff-gate/EXECUTION.md),
which deliberately amends the recorded PX1 boot-wait policy under the owner's authority. (d) Physical-panel
observation is a single owner report on this boot; it is not a pixel-timing or deadline claim (S7.5b.6
stays open).

**Checkoff:** `tasks.md` — *Qualify repaired edge-effect boot on hardware* is checked on this evidence
(its own criterion, on the current pinned image). S7.5a/S7.5b/S7.5, the display-continuity and content
gates are untouched by it.
