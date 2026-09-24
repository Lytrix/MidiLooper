## Why

`runtime-working-set-ownership` has **no currently eligible native task** left (S2b.8 and S2b.7 landed
2026-09-24), and every remaining gate is a **device** leg. All of them are blocked by one thing: the set of
slots that come back committed after a reboot is **not reproducible**, so no deterministic fixture can be
held long enough to measure anything. `runtime-working-set-ownership` recorded the blocker as the SD catalog
reconcile; the evidence does not support that attribution. Two measured facts:

- The boots that **shrank** the committed boot-playback set (`captures/s13-slope-02/`, N 27 → 27 → 26) report
  **`reconcile_warnings=0`** — reconcile did not fail there. The `could not reconcile
  MidiLooper/sets/index.bin` observation comes from a *different* session and is not demonstrated in the
  shrinking runs.
- `MidiLooper/sets/index.bin` is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a **saved-set folder
  counter**, not a catalog of slot payloads. The live library is `/MidiLooper/current/loops/loop_TT_SS.bin`
  plus the RAM mirror `loopSlotPayloadOnSdInRam_`. So an `index.bin` reconcile failure cannot by itself
  explain a slot missing from the committed set.

The instability currently has **no owner**: `deferred-slot-restore-liveness` was archived with a reduced
scope, and `runtime-working-set-ownership` treats it as an external blocker. Until it is owned and
characterised, S1 — residency measurements, the S2b — boot/load admission device gate, and S3 — lease
ownership cannot start.

## What Changes

- **Own the instability** in one narrowly scoped change whose first packet is **read-only**.
- **Pre-register the attribution taxonomy** and require the evidence to separate, per lost slot, four
  distinct classes instead of assuming one cause:
  1. **Catalog omission** — the SD payload exists and verifies, but the boot/reconcile path never enumerates it.
  2. **Restore refusal** — the payload exists and is enumerated, but the request never reaches `committed`
     (a terminal `unnecessary`/`refused`/`superseded`, a gate, or no request at all).
  3. **Intentional eviction** — a deliberate, bounded policy dropped or reused the slot (a *decision*, which
     must be named and justified if found, not inferred from the symptom).
  4. **Content loss** — the payload is present but carries no committed content (empty/truncated body, or a
     body that fails validation).
- **Require a per-slot inventory observable** — slot identity + SD payload presence/token validity + boot
  enumeration + the restore request state/terminal + committed content inventory — recorded alongside the
  existing post-restore census, so a "missing slot" is attributable from one capture instead of a bisect.
- **Define exactly one repeatable fixture inside the already-approved scratch range** (track 5 slot 1 and
  track 6 slots 1–8). Read-only investigation starts immediately.
- **Carry the S2b.7 hardware confirmation here** (`runtime-working-set-ownership` S2b.7, drain-driven restore
  advancement): once the fixture can reliably hold a session-holding restore, run the device leg and verify
  progress, the named commit-gate wait, the preserved request/save, and no unintended publication.
- **Hand off, not absorb:** when the library is stable, `runtime-working-set-ownership` finishes S1 — residency
  measurements and S2b — boot/load admission device gates, then starts S3 — lease ownership.

## Capabilities

### New Capabilities

_None._ The behavior belongs to the existing hydration capability.

### Modified Capabilities

- `lazy-slot-hydration`: add a requirement that the restored library is **inventory-attributed** — a slot that
  is not `COMMITTED` after boot SHALL be attributable to one of the four named classes from recorded
  evidence, and the system SHALL expose the per-slot inventory that makes that attribution possible. This
  extends the existing observability requirement (which covers restore request progress and the boot catalog
  reconcile outcome) without changing the hydration lifecycle.

## Known behavior, evidence-backed assumptions, and UNKNOWNs

**Known (measured, cited):**

- `captures/s13-slope-final/`: three fresh boots, `N=29` committed, `post_restore_free_chunks=452` every boot,
  `reconcile_warnings=0`, enabled-set cost 512 → 480, library restore walk adds **0** reservation.
- `captures/s13-slope-02/`: two failing steps into empty slots outside the scratch range report
  `reconcile_warnings=0` while N shrank 27 → 27 → 26.
- Both sweeps: `library_rows` are **all-zero** (16 rows, `phases: 0`); the sweep captured **no** per-slot
  inventory. The N figure, `free_before/after`, `reconcile_warnings`, `empty_state_count` and the
  `Track N loaded state:` lines are the only library observables recorded.
- `Track 6 loaded state: EMPTY` / `Track 7 loaded state: EMPTY` appear on **every** boot in both sweeps,
  including the boots where N is stable at 29 — so that marker does not track the shrink.
- `DIAG,lcr,skip,restore` is emitted by `TrackDeferredMaintenance`/`LoopContentResolution` as a
  **content-resolution** device-gate line (`kind=skip`, `reason=restore`), not by the library path.

**Assumptions (evidence-backed, to be confirmed or falsified by Packet A):**

- The live library is the per-slot `loop_TT_SS.bin` payload set under `/MidiLooper/current/loops/`, mirrored
  in RAM by `loopSlotPayloadOnSdInRam_` and rebuilt by a boot scan.
- The observed shrink is a property of the **boot playback set** (`RRQ … committed`, the union of
  selected/active slots per track), which is deliberately independent of the library — so a library that
  gained payloads can still show a smaller committed set.

**UNKNOWN (must not be assumed):**

- Whether any lost slot lost its **payload**, its **enumeration**, its **restore**, or only its **selection**.
- Whether the shrink is an intentional bounded policy (slot reuse / one sounding slot per track) or a defect.
- Whether the `sets/index.bin` reconcile failure and the library shrink are related at all; they have not been
  observed in the same boot.

**Open decisions (TBD, owner-gated):**

- **TBD-1 — fixture construction.** Which slots inside the approved scratch range may be **cleared** (not just
  read) to build the fixture. Read-only investigation needs no authorization; any CLEAR, rename or delete on
  the card needs the owner's explicit authorization for **named slots** in the same session
  (`Destructive-SD-Operations`). No wider sweep.
- **TBD-2 — scope growth.** If Packet A finds intentional eviction, whether the bounded policy is accepted and
  documented or becomes its own change.

## Non-goals

- **No wider sweep and no uncontrolled recording.** The previous attempt wrote into track 3 slots 2–4, outside
  the approved range; that is not repeated.
- **No destructive SD action** without per-slot authorization (quarantine, delete or rename).
- **Not the lease pool, and not S1/S2b/S3.** Those stay with `runtime-working-set-ownership`, whose execution
  order this change does not bypass just because hardware evidence is blocked.
- **Not a drain-behavior change.** S2b.7's implementation is landed in `runtime-working-set-ownership`; this
  change only carries its **device confirmation**.
- **Not a claim that recording is broken.** The recorded base-preset failures are a separate harness/device
  interaction.

## Impact

- **Firmware:** `StorageManager` restore/hydration ownership — `CurrentSetStorage`/`SavedSetIo` (payload paths
  and set index), `LoopSlotRestoreQueue` (payload mirror, boot background fill, request admission), and the
  census/observability path. Packet A is **read-only**; any production change is specified only after the
  attribution is evidenced.
- **Harness/tooling:** the boot census and the library sweep gain a per-slot inventory record; the existing
  `scripts/hitl_boot_census.py` marker and `scripts/hitl_library_growth_sweep.py` parser are extended rather
  than replaced.
- **Device:** the approved scratch range is reused; any clearing is authorized per slot.
- **Docs:** `runtime-working-set-ownership` S1.3/S2b.3 and the S2b gate lose their unnamed external blocker and
  gain a named owner; `CURRENT_WORK` / `PROJECT_STATE` / `DELIVERABLE_TRACKING` record "no **currently
  eligible** native task remains".
- **RAM1:** no new runtime state is assumed; any added observable is bounded and session-gated, and the
  post-link budget (`padding >= 840`) applies as usual.
