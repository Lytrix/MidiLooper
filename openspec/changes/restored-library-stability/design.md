## Context

See `proposal.md` — *Why*. This design fixes **how the attribution is obtained and what may change in this
change**, not the library behavior itself.

Constraints that shape the approach:

- **No new runtime state is assumed.** The tree sits on the RAM1 FlexRAM bank edge; the post-link budget
  (`scripts/check_ram1_budget.py`, floor `padding >= 840`) is enforced, so a new per-slot observable is a
  cost to declare, not a free addition.
- **The existing observability is already the right tier.** `StorageManager::emitPostRestoreCensusIfReady`
  emits one readiness-attested, timestamped Tier-A marker per boot
  (`#CAP,<micros>,DIAG,post_restore_census,…`), parsed by `scripts/hitl_boot_census.py`. The library sweep
  (`scripts/hitl_library_growth_sweep.py`) already records `n_committed_slots`, `free_before/after`,
  `reconcile_warnings`, `empty_state_count`, `loaded_state_lines` and a `library_rows` table.
- **The recorded library observables do not include per-slot attribution.** In both `s13-slope-final/` and
  `s13-slope-02/` the `library_rows` are all-zero (16 rows, `phases: 0`), so the "library walk" captured no
  inventory; only the committed count moved.
- **Every input the attribution needs already has an owner.** SD payload presence/token:
  `probeLoopSlotPayloadOnSdFromSd` / `loopSlotPayloadOnSdInRam_`. Boot enumeration and admission:
  `LoopSlotRestoreQueue` + `needsSlotLoad`. Request outcome: `restoreRequestState`,
  `restoreRequestWaitFor`, `restoreRequestTerminalFor`. Content: `Loop::hasCommittedPasses()` and the
  committed inventory. Selection: the boot playback set / `isBootPlaybackSlot`.
- **`MidiLooper/sets/index.bin` is not a slot catalog.** `SavedSetCatalog::SetIndex` is
  `{ uint32_t nextSequence; }`; the live library is `/MidiLooper/current/loops/loop_TT_SS.bin`. This is why
  "catalog omission" must be tested against the *payload enumeration*, not assumed from a reconcile warning.

## Goals / Non-Goals

**Goals:**

- Make a missing/undiscovered committed slot **attributable from one capture**.
- Separate the two quantities that are currently conflated: the **payload set** and the **boot playback set**.
- Make the attribution **decision-testable natively** (a pure classifier over the inventory) while the raw
  inventory itself is device evidence.
- Give `runtime-working-set-ownership`'s device legs a fixture that can be **held deterministically**.

**Non-Goals:**

- Not a library-behavior change in Packet A, and not a speculative fix. Any production change is proposed only
  after the attribution is evidenced.
- Not a re-definition of the hydration lifecycle or of the restore-request contract.
- Not a drain-behavior change (S2b.7 is landed in `runtime-working-set-ownership`).
- Not an SD format or schema migration.

## Decisions

**D1 — The inventory is a derived report over existing production state, emitted at the existing census
point; it is not a new stored catalog.**
Reuse `emitPostRestoreCensusIfReady` (one bounded, session-gated line per boot) and extend the existing sweep
parser. *Alternative rejected:* a new persisted per-slot catalog — it would duplicate
`loopSlotPayloadOnSdInRam_`, add RAM1/DTCM state, and re-introduce the second-authority problem the working-set
change exists to remove.

**D2 — The four-class attribution is a pure function over the inventory, so it is node-testable.**
The device line carries raw fields; a pure classifier maps them to `catalog omission` / `restore refusal` /
`intentional eviction` / `content loss` / `committed`. *Alternative rejected:* reading the class off the device
log by eye — the previous attribution did exactly that and produced three separate misreads
(`Track 6 loaded state: EMPTY`, `DIAG,lcr,skip,restore`, and `could not reconcile` as the cause).

**D3 — Packet A is read-only, and the fixture is bounded to the already-approved scratch range.**
Read-only investigation needs no authorization. Building the fixture may require emptying a slot: that is a
per-slot, explicitly authorized action in the same session (TBD-1), never a sweep. *Alternative rejected:*
"grow the library until it fails", which is what wrote outside the approved range last time and produced the
capacity finding that is still not attributable.

**D4 — The payload set and the boot playback set are reported as two named, separately-counted quantities,
and only their difference is treated as library loss.**
The committed boot-playback set is deliberately independent of the library (the union of selected/active slots
per track), so a flat or shrinking committed count is not evidence of payload loss. *Alternative rejected:*
treating `n_committed_slots` as the library size, which is the assumption that made `s13-slope-final`'s flat
`N=29` look like a broken fixture rather than a correct reading.

**D5 — The device confirmation of `runtime-working-set-ownership` S2b.7 is carried here as a task, but the
task stays owned by that change.**
This change supplies the fixture that can hold "a save owed + a session-holding restore + another track
playing" deterministically; the verification criteria (progress, the named `waiting_commit_gate`, preserved
request and save, no unintended publication) are S2b.7's and are copied by reference, not re-specified.

**D6 — The attribution distinguishes "no valid payload" from "payload refused", and neither is reported as
the other.**
A missing/invalid payload is a payload-set fact; a `refused`/`unnecessary` terminal is a restore-outcome fact.
Conflating them is how the two classes were previously merged.

## Risks / Trade-offs

- **[The fixture needs a destructive action to build]** → Read-only Packet A runs first; any CLEAR is
  authorized per named slot, and the fixture reuses slots already inside the approved range (track 5 slot 1,
  track 6 slots 1–8) before asking for anything new.
- **[The new observable costs RAM1/ITCM]** → Emit it in the existing session-capture tier at the existing
  census point, keep the classifier pure and FLASH-resident, and re-read the post-link budget line in the
  slice that adds it.
- **[Packet A finds intentional eviction, not a defect]** → That is a valid outcome; the change then owns
  *documenting the policy* (TBD-2) rather than a fix, and the fixture is rebuilt around the accepted policy.
- **[The measurement could be confounded by the base-preset harness failures]** → Those are a separately
  tracked harness/device interaction and are not generalised to "recording is broken"; the fixture records the
  payload inventory rather than relying on a base-preset verdict.
- **[Scope creep into S1/S2b/S3]** → The handoff is explicit: those stay with `runtime-working-set-ownership`,
  and this change does not begin them even when the library is stable.

## Migration Plan

No data migration, no SD format change, no schema change. Rollback is reverting the added observable and its
parser; the firmware's library behavior is untouched until a later, separately-evidenced production slice.

## Open Questions

- **TBD-1 — fixture construction.** Which specific slots inside the approved scratch range may be cleared.
  Owner-gated; blocks only the fixture, not the read-only investigation.
- **TBD-2 — disposition of an intentional eviction policy**, if Packet A finds one: accept and document, or
  open a separate change.
