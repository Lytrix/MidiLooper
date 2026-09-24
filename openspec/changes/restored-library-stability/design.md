## Context

See `proposal.md` — *Why*. This design fixes **how the attribution is obtained and what may change in this
change**, not the library behavior itself. It incorporates the G2 review corrections: the measurement
definitions were wrong, the diagnostic was unavailable exactly where it was needed, the classifier could not
truthfully classify every slot, and the fixture gate did not establish stability.

Constraints that shape the approach:

- **No new runtime state is assumed.** The tree sits on the RAM1 FlexRAM bank edge; the post-link budget
  (`scripts/check_ram1_budget.py`, floor `padding >= 840`) is enforced, so a new observable is a cost to
  declare, not a free addition.
- **The settled census is not usable as the diagnostic.** `StorageManager::emitPostRestoreCensusIfReady`
  fires at most once per boot (`static bool emitted`) and only when `bootBackgroundFillAdmissionComplete()`,
  `bootInteractiveReady()`, no pending undo hydration, transport stopped, no capture, no holdoff/in-flight
  persistence work, and no owed playback prewarm all hold. `bootInteractiveReady()` is
  `pendingLoopSlotRestoreCount() == 0 && !SlotLoadSession::isActive() && !anyLoadLoopJobActive()`. A request
  stuck waiting therefore **suppresses** the very report meant to explain it.
- **`n_committed_slots` is not the boot playback set.** `scripts/hitl_library_growth_sweep.py` computes it as
  the distinct `(track, slot)` values of `RRQ,…,terminal,none,committed` lines observed **within the capture
  window**. Background admission visits every slot, so it is a count of **observed restore completions in a
  window**, not of playback-required slots, and not of the library. The earlier design's explanation for
  `N=29` (and the "subtract the boot playback set from the payload set" inference) is **withdrawn**.
- **The recorded library observables remain thin.** In both `s13-slope-final/` and `s13-slope-02/` the
  `library_rows` are all-zero (16 rows, `phases: 0`); `free_before/after` 512 → 480 is the enabled-set walk,
  not the library.
- **Every input the attribution needs already has an owner.** SD payload presence/token:
  `probeLoopSlotPayloadOnSdFromSd` / `loopSlotPayloadOnSdInRam_`. Boot enumeration and admission:
  `LoopSlotRestoreQueue` + `needsSlotLoad`. Request outcome: `restoreRequestState`, `restoreRequestWaitFor`,
  `restoreRequestTerminalFor`, `restoreRequestGenerationFor`, `restoreRequestEpochFor`. Content:
  `Loop::hasCommittedPasses()` and the committed inventory. Selection: the boot playback set.
- **`MidiLooper/sets/index.bin` is not a slot catalog.** `SavedSetCatalog::SetIndex` is
  `{ uint32_t nextSequence; }`; the live library is `/MidiLooper/current/loops/loop_TT_SS.bin`. "Catalog
  omission" must be tested against the payload enumeration, not assumed from a reconcile warning.

## Goals / Non-Goals

**Goals:**

- Make a missing/undiscovered committed slot **attributable from recorded evidence**, without a bisect.
- Report the four distinct quantities that are currently conflated, and make the **observation window** an
  explicit, measured dimension — so a measurement artifact is not read as library loss.
- Make the inventory **available in the stuck states it must explain**.
- Make the observation **node-testable** (a pure classifier over a snapshot) while the raw snapshot is device
  evidence.
- Give `runtime-working-set-ownership`'s device legs a fixture whose **stability is proven by a complete
  manifest**, not by one slot surviving.

**Non-Goals:**

- Not a library-behavior change in Packet A, and not a speculative fix. Any production change is proposed only
  after the attribution is evidenced.
- Not a re-definition of the hydration lifecycle or of the restore-request contract.
- Not a change to the settled census checkpoint's purpose; the on-demand snapshot does not replace it.
- Not a drain-behavior change (S2b.7 is landed in `runtime-working-set-ownership`).
- Not an SD format or schema migration.

## Decisions

**D1 — The inventory is a derived report over existing production state; it is not a new stored catalog.**
Reuse the existing owners listed in *Context*; add no persisted per-slot catalog and no second queue.
*Alternative rejected:* a persisted inventory — it would duplicate `loopSlotPayloadOnSdInRam_`, add
RAM1/DTCM state, and re-introduce the second-authority problem the working-set change exists to remove.

**D2 — The inventory is collected by a bounded, on-demand path that does not depend on the settled census.**
Add an on-demand diagnostic snapshot gated only by an explicit request, with:
- **incremental collection** — bounded slots (and bounded SD reads / content traversal) per invocation, so a
  large library never forces an unbounded traversal in one turn;
- **snapshot consistency** — each slot records the observation turn, and a snapshot assembled across turns is
  marked as such rather than presented as one instant;
- **capture framing** — window start/end, whether every initial-playback slot was observed, whether the
  background admission scan completed, and the four counts of D4.
*Alternatives rejected:* (a) relax `emitPostRestoreCensusIfReady`'s gates — that would corrupt the settled
memory measurement it exists for; (b) harvest the inventory from the existing census only — it is exactly the
state that a stuck request suppresses.

**D3 — Observation and attribution are separate, and attribution is nullable.**
The snapshot records observed fields (payload validity, enumeration, request state/wait/terminal with the full
identity `(track, slot, requestGeneration, setEpoch)`, committed content, selection). A **pure** function maps
the snapshot to a cause that may be `empty/not applicable`, `pending`, `catalog omission`, `restore refusal`,
`intentional eviction`, `content loss`, `ambiguous`, or `unknown`, always with the supporting fields and a
stated precedence:
- `empty/not applicable` — no payload and no outstanding request; never a loss cause;
- `pending` — a request that has not terminated; no loss or refusal is asserted;
- `ambiguous` — two or more causes are equally consistent (recorded together);
- `unknown` — the evidence is insufficient, including any **historical** eviction/deletion claim from a single
  snapshot with no prior inventory or explicit event.
*Alternatives rejected:* a forced one-of-four classification (cannot classify an empty slot, cannot represent a
corrupt payload that also refused, and cannot prove a past event); and reading the cause off the device log by
eye, which produced three separate misreads already.

**D4 — Four distinct quantities and an explicit window; none of them is "the library size".**

| Quantity | Meaning |
|---|---|
| **payload inventory** | slots with a payload file present and its token valid |
| **initial playback-required set** | slots the boot requires committed before interactive ready |
| **observed restore completions** | distinct slots whose `committed` terminal was observed **in this window** (background admission included) |
| **currently committed at the checkpoint** | slots holding committed content at the observation |

The report SHALL state which window produced the completion count. *Alternative rejected (and withdrawn):*
treating `n_committed_slots` as the boot playback set and subtracting it from the payload inventory — a
window-dependent count cannot carry that inference.

**D5 — Packet A stays read-only; the fixture is bounded to the already-approved scratch range.**
Read-only investigation needs no authorization. Building the fixture may require emptying a slot: a per-slot,
explicitly authorized action in the same session (TBD-1), never a sweep.

**D6 — Fixture stability is proven by a complete per-slot manifest comparison, not by one slot surviving.**
Each fixture boot compares the **whole expected manifest**: every slot's payload presence/token, committed
content identity and counts, the initial playback-required set, and the authorized changes. Any unexplained
difference — including a slot outside the fixture that loses content — is a **failure**, not a pass.

**D7 — The S2b.7 device confirmation is carried here but is independently reportable.**
This change supplies the fixture that can hold the condition; the verification criteria belong to
`runtime-working-set-ownership` S2b.7 and are cited, not re-specified. Its result is reported as **what was
observed**, which may be either:
- the drain returns `TemporarilyBlocked` with the named `waiting_commit_gate`; **or**
- the owed save is discharged by the documented urgent-save priority while the restore remains commit-gated.

Both are acceptable only with **no unintended publication**. The second is not a pass for "the drain advanced
the restore"; the report names which occurred. This task is checkable on its own and does not gate, or depend
on, this change's closeout or any other change's closeout.

## Risks / Trade-offs

- **[The apparent instability may be partly a measurement artifact]** → D4 makes the window an explicit field
  and the completion count window-scoped; Packet A reports the four quantities separately and never infers loss
  from a count difference.
- **[The on-demand snapshot costs SD reads / content traversal]** → D2 bounds it per invocation, marks partial
  snapshots, and keeps the settled census unchanged for memory figures; the post-link RAM1 line is re-read in
  the slice that adds it.
- **[A partial snapshot could be misread as a settled one]** → capture framing is mandatory and an incomplete
  window must be stated; the spec forbids presenting an incomplete observation as the settled state.
- **[The fixture needs a destructive action to build]** → D5: read-only Packet A first; per-named-slot
  authorization; reuse of the approved range before requesting anything new.
- **[Packet A finds intentional eviction, not a defect]** → A valid outcome; the change then owns documenting
  the policy (TBD-2) and rebuilds the fixture around the accepted policy.
- **[The measurement is confounded by base-preset harness failures]** → Those are a separately tracked
  harness/device interaction, not generalised to "recording is broken"; the fixture records the payload
  manifest rather than relying on a base-preset verdict.

## Migration Plan

No data migration, no SD format change, no schema change. Rollback is reverting the added diagnostic and its
parser; the firmware's library behavior is untouched until a later, separately-evidenced production slice.

## Open Questions

- **TBD-1 — fixture construction.** Which specific slots inside the approved scratch range may be cleared.
  Owner-gated; blocks only the fixture, not the read-only investigation.
- **TBD-2 — disposition of an intentional eviction policy**, if Packet A finds one: accept and document, or
  open a separate change.
