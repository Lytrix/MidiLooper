## Why

`runtime-working-set-ownership` has **no currently eligible native task** left (S2b.8 and S2b.7 landed
2026-09-24), and every remaining gate is a **device** leg. Those legs need a fixture that holds the same
library state across boots, and today none can be shown to. The observation is real; its **cause is
unattributed**, and the measurements used so far cannot attribute it.

The G2 review found the recorded explanation was itself a measurement error. Two corrections drive this
proposal:

- **`n_committed_slots` is not the boot playback set.** `scripts/hitl_library_growth_sweep.py` counts the
  distinct `(track, slot)` of `RRQ,…,terminal,none,committed` lines observed **within the capture window**.
  Background admission visits every slot, so this is a count of **observed restore completions in a window** —
  it is neither the library nor the playback-required set, and the earlier "N is deliberately independent of
  the library, so a flat N is correct" explanation is **withdrawn**. The capture may also close before
  admission finishes, which changes the count on its own.
- **The diagnostic is unavailable exactly where it is needed.** `emitPostRestoreCensusIfReady` fires at most
  once per boot and only when the system is settled, including `bootInteractiveReady()`
  (`pendingLoopSlotRestoreCount() == 0`, no live `SlotLoadSession`, both restore jobs inactive). A request
  stuck waiting therefore suppresses the report meant to explain it.

The instability also had no owner: `deferred-slot-restore-liveness` was archived with a reduced scope, and
`runtime-working-set-ownership` treats the library as an external blocker. This change owns it — and owns the
possibility that a corrected measurement explains part of the apparent instability **without a firmware fix**.

## What Changes

- **Own the observation and its attribution**, in one narrowly scoped change whose first packet is
  **observation-only firmware plus read-only analysis** — instrumentation and analysis only, with no library
  behavior change and no destructive SD action.
- **Define the measurement before drawing conclusions.** Report four distinct quantities and make the
  observation window explicit:
  1. the **payload inventory** — slots with a payload present and its token valid;
  2. the **initial playback-required set** — slots boot must commit before interactive ready;
  3. the **observed restore completions** — distinct slots whose `committed` terminal was observed in **this
     window** (background admission included);
  4. the **currently committed set** at the observation.
  An incomplete window is reported as incomplete, and a completion count is never presented as the library
  size.
- **Separate observation from attribution, on two axes.** Per slot, record observed fields (payload validity,
  enumeration, request state/wait/terminal with the **full identity**
  `(track, slot, requestGeneration, setEpoch)`, committed content, selection), then report an observed
  **current state** (`empty` / `populated` / `pending`, a set) **separately** from a **nullable historical
  cause** (`catalog omission`, `restore refusal`, `intentional eviction`, `content loss`, `ambiguous`,
  `unknown`) — always with supporting evidence and stated precedence. `empty` is a state and is **never** a
  cause; a slot that is `empty` with no history records cause `unknown`; a prior inventory establishes only the
  disappearance, so its cause stays `unknown` without an explicit event; a
  corrupt payload that also refused is not forced into one cause; a single snapshot cannot prove a historical
  eviction.
- **Make the inventory available in the stuck states it must explain** — a bounded, on-demand observation with a
  declared owner and a service point that runs **independently of normal load admission** (so a blocked load
  gate cannot suppress the diagnostic), SD-read admission, incremental collection with declared bounds,
  explicit observation consistency, and capture framing. Invalidation covers a request-generation or set-epoch
  change **and a content revision identity**, because a restore publication installs content under the *same*
  request identity (so it changes neither generation nor epoch) and ordinary commits change content too. The
  settled census checkpoint stays unchanged for settled memory figures.
- **Define one repeatable fixture inside the already-approved scratch range** (track 5 slot 1, track 6 slots
  1–8), whose stability is proven by a **complete per-slot manifest comparison** across boots — not by one
  scratch slot surviving, which is compatible with other payload-bearing slots disappearing.
- **Carry the S2b.7 hardware confirmation** (`runtime-working-set-ownership` S2b.7, drain-driven restore
  advancement) as an **independently reportable** packet that reports which permitted outcome occurred.
- **Hand off, not absorb:** when the library is stable, `runtime-working-set-ownership` finishes S1 and the S2b
  device gates, then starts S3.

## Capabilities

### New Capabilities

_None._ The behavior belongs to the existing hydration capability.

### Modified Capabilities

- `lazy-slot-hydration`: add two requirements.
  1. **Observed current state is reported separately from a nullable historical cause** — observed fields per
     slot; a current state that is a set over `empty` / `populated` / `pending`; a nullable cause
     (`catalog omission` / `restore refusal` / `intentional eviction` / `content loss` / `ambiguous` /
     `unknown`) with precedence and evidence; `empty` never a cause; no single-snapshot historical claims; and
     mandatory report completeness.
  2. **The on-demand library observation has a bound owner, admission and invalidation contract** — a declared
     existing owner and a service point independent of normal load admission, SD-read admission, incremental
     and bounded collection, invalidation or restart on a request-generation change, a set-epoch change, **or a
     content revision change** (publication under an unchanged request identity is named explicitly), permitted
     unknown degradation, and consistency marking; the settled census checkpoint is retained unchanged.

## Known behavior, evidence-backed assumptions, and UNKNOWNs

**Known (measured, cited):**

- `captures/s13-slope-02/` (the run that appeared to shrink): `reconcile_warnings=0` on every boot.
- `captures/s13-slope-final/`: `post_restore_free_chunks=452` on three fresh boots; `free_before/after`
  512 → 480 is the **enabled-set** walk, not the library.
- Both sweeps: `library_rows` are **all-zero** (16 rows, `phases: 0`) — no per-slot inventory was captured.
- `Track 6 loaded state: EMPTY` / `Track 7 loaded state: EMPTY` appear on **every** boot in both sweeps,
  including the stable ones.
- `DIAG,lcr,skip,restore` is emitted by `TrackDeferredMaintenance`/`LoopContentResolution` as a
  **content-resolution** device-gate line.
- `SavedSetCatalog::SetIndex` is `{ uint32_t nextSequence; }`; the live library is
  `/MidiLooper/current/loops/loop_TT_SS.bin` plus the `loopSlotPayloadOnSdInRam_` mirror.

**Assumptions (evidence-backed, to be tested by Packet A):**

- The live library is the per-slot payload set under `/MidiLooper/current/loops/`, mirrored in RAM and
  rebuilt by a boot scan.
- The observed completion count varies with the observation window and background admission, so **some** of
  the apparent instability may be a measurement artifact.

**UNKNOWN (must not be assumed):**

- Whether any slot lost its payload, its enumeration, its restore, its selection — or none of these.
- Whether the shrink is an intentional bounded policy (slot reuse / one sounding slot per track) or a defect.
- Whether the `sets/index.bin` reconcile failure and the library observation are related; they have never been
  observed in the same boot.

**Open decisions (TBD, owner-gated):**

- **TBD-1 — fixture construction.** Which slots inside the approved scratch range may be **cleared** (not just
  read). Read-only investigation needs no authorization; any CLEAR, rename or delete needs the owner's explicit
  authorization for **named slots** in the same session (`Destructive-SD-Operations`). No wider sweep.
- **TBD-2 — scope growth.** If Packet A finds intentional eviction, whether the bounded policy is accepted and
  documented or becomes its own change.

## Non-goals

- **Not a claim that the library is broken.** The first deliverable is a measurement that can tell loss from a
  measurement artifact; a firmware fix is proposed only after attribution.
- **No wider sweep and no uncontrolled recording.** The previous attempt wrote into track 3 slots 2–4, outside
  the approved range; that is not repeated.
- **No destructive SD action** without per-slot authorization.
- **Not the lease pool, and not S1/S2b/S3.** Those stay with `runtime-working-set-ownership`; its stage order is
  not bypassed because hardware evidence is blocked.
- **Not a drain-behavior change.** S2b.7's implementation is landed in `runtime-working-set-ownership`; this
  change only carries its device confirmation.
- **Not a replacement for the settled census checkpoint.** The on-demand snapshot is a separate diagnostic and
  must not be reported as the settled memory measurement.
- **Not a claim that recording is broken.** The recorded base-preset failures are a separate harness/device
  interaction.

## Impact

- **Firmware:** `StorageManager` restore/hydration ownership — `CurrentSetStorage`/`SavedSetIo` (payload paths,
  set index), `LoopSlotRestoreQueue` (payload mirror, boot background fill, request admission), and the
  census/observability path. The on-demand snapshot is a new bounded diagnostic alongside — not a relaxation
  of — `emitPostRestoreCensusIfReady`. Packet A is **observation-only firmware plus read-only analysis**; any
  library **behavior** change is specified only after the attribution is evidenced.
- **Harness/tooling:** the boot census and the library sweep gain the observed fields, the four quantities, the
  window completeness and the nullable cause; `scripts/hitl_boot_census.py` and
  `scripts/hitl_library_growth_sweep.py` are extended rather than replaced.
- **Device:** the approved scratch range is reused; any clearing is authorized per slot.
- **Docs:** `runtime-working-set-ownership` S1.3/S2b.3 and the S2b gate lose an unnamed and now-corrected
  blocker and gain a named owner; `CURRENT_WORK` / `PROJECT_STATE` / `DELIVERABLE_TRACKING` record "no
  **currently eligible** native task remains".
- **RAM1:** the new diagnostic is bounded and session-gated; the post-link budget (`padding >= 840`) applies to
  the slice that adds it.
