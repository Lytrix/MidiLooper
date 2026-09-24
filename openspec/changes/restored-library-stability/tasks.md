# Tasks — restored-library-stability

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing. **Status 2026-09-24: proposed, nothing started.**

This change is **investigation-first**. Packet A is read-only and needs no authorization. Packet B may need a
per-slot destructive action (TBD-1) and stops at read-only without it. Packet C is a **device** confirmation
that belongs to `runtime-working-set-ownership` S2b.7; this change only supplies the fixture that can hold it.

Every task that edits firmware follows the [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice):
pin the scope and transition oracle, establish the controlled production-path fixture, and record actual
evidence before checkoff. No behavior change is proposed in this change until Packet A evidences the class of
each non-committed slot.

Standing constraints for every task:

- **No wider sweep.** Recording is limited to the already-approved scratch range (track 5 slot 1, track 6
  slots 1–8). Anything outside it is not written.
- **No destructive SD action** (quarantine, delete, rename, CLEAR) without the owner's explicit authorization
  for the **named slots** in the same session — [`Destructive-SD-Operations`](../../../.cursor/rules/Destructive-SD-Operations.mdc).
- **Distinguish the classes; do not assume.** `catalog omission`, `restore refusal`, `intentional eviction` and
  `content loss` are separate claims with separate evidence.
- **Separate the payload set from the boot playback set** in every report, and never treat
  `n_committed_slots` as the library size.
- New firmware observables are session-gated at the existing census point, FLASH-resident where possible, and
  the post-link RAM1 budget (`padding >= 840`) is re-read in the slice that adds them.

## 1. Packet A — read-only attribution (no behavior change)

- [ ] 1.0 **Execution-packet gate.** Before any behavior edit, write the G2 execution packet for that slice
      into the change's review artifact (`ARCHITECTURE-REVIEW.md`): fixed transition oracle, controlled
      production-path fixture, repeatable recipe, and evidence expectations. Scale to risk. **Verify:** a packet
      exists for every slice that edits firmware; Packet A's investigation tasks need no packet.
- [ ] 1.1 **Correct the recorded attribution (documentation only).** `runtime-working-set-ownership`
      S1.3/S2b.3 and the S2b gate name the SD catalog reconcile as the blocker. Record the four measured
      corrections in this change and in that change's text, with citations:
      (a) `MidiLooper/sets/index.bin` is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a saved-set
      folder counter, not a slot catalog;
      (b) the boots that **shrank** the committed set (`captures/s13-slope-02/`) report
      `reconcile_warnings=0`;
      (c) `DIAG,lcr,skip,restore` is a **content-resolution** device-gate line
      (`TrackDeferredMaintenance` / `LoopContentResolution`), not a library marker;
      (d) `Track 6 loaded state: EMPTY` appears on the **stable** `N=29` boots too, so it does not track the
      shrink. **Verify:** no durable doc still asserts the catalog reconcile as the *demonstrated* cause, and
      each of the four facts is cited with its evidence path.
- [ ] 1.2 **Define the inventory field set and the pure classifier.** Specify, per `(track, slot)`: SD payload
      presence and token validity; boot enumeration/admission; the restore request state/wait/terminal; the
      committed-content inventory; and selection (boot playback set). Specify a **pure** function mapping that
      inventory to one of `committed` / `catalog omission` / `restore refusal` / `intentional eviction` /
      `content loss` / `unknown`. **Verify (native, red first):** a case per class is reachable; one inventory
      never yields two classes; "no valid payload" is separated from a `refused`/`unnecessary` terminal; the
      payload set and boot playback set are separate inputs/outputs.
- [ ] 1.3 **Emit the inventory at the existing census point.** Extend `emitPostRestoreCensusIfReady` and its
      reader with the per-slot inventory and the two named counts (payload set, boot playback set), in the
      session-capture Tier-A tier only. **Verify:** the worst-case line fits the bounded Tier-A payload and is
      session-gated (no per-turn cost); a native case pins the line's shape and tier; the post-link RAM1 line is
      recorded.
- [ ] 1.4 **Re-analyze the existing captures read-only, then extend the sweep parser.** Run the classifier over
      `captures/s13-slope-02/` and `captures/s13-slope-final/` and extend
      `scripts/hitl_library_growth_sweep.py` to record the inventory and the class per non-committed slot.
      **Verify:** each non-committed slot with a valid payload gets a class or an explicit `unknown` with the
      missing field named; the analysis performs **no** device write.
- [ ] 1.5 **Read-only device attribution.** Capture at least three fresh boots of the **current** device state
      without writing or clearing anything, and classify the committed-set difference per boot. **Verify:** for
      every non-committed slot whose payload is present and verifies, the class is one of the four or `unknown`
      with the missing observable named; engine-visible separation of the payload set and the boot playback set;
      0 `FATAL`; no SD mutation. **Blocked by:** TBD-1 only if a boot cannot be taken without a write.

## 2. Packet B — one repeatable fixture inside the approved scratch range

- [ ] 2.1 **TBD-1 authorization gate.** If the fixture needs a slot emptied, request the owner's explicit
      authorization naming the **specific slots** and the action, in the same session. **Verify:** the
      authorization is recorded in this change before the action; without it, Packet B stops at read-only and
      this task stays unchecked.
- [ ] 2.2 **Construct the fixture.** One `(track, slot)` inside the approved range with a known committed
      state, such that a reboot reproduces it. **Verify:** three consecutive boots reproduce the same committed
      state and the same inventory class (`committed`), with both counts recorded; **no slot outside the
      approved range is written** (asserted from the offline inventory, not from intent).
- [ ] 2.3 **Prove the fixture can hold the S2b.7 condition.** Show the fixture yields a save owed + a restore
      holding the SD session + another track playing, with `deferredSaveBlockedByActiveSlotLoadSd()` true at
      the drain edge. **Verify:** the condition is observed, not assumed; the exact observables are recorded;
      no external advancement is performed inside the run.

## 3. Packet C — S2b.7 device confirmation (task stays owned by `runtime-working-set-ownership`)

- [ ] 3.1 **Run the S2b.7 device leg on the held fixture.** Use the authoritative HITL entry point with a
      registered or reused stimulus (list before adding; never edit a canonical stimulus). **Verify:** progress
      **advances**; the drain reports `TemporarilyBlocked` with the named **`waiting_commit_gate`**; the request
      is preserved (no terminal); the owed save is preserved or discharged by the documented urgent-save
      priority, stated as observed; and **no unintended publication** occurs (no committed pass appears while
      the gate denies).
- [ ] 3.2 **Record the result honestly in S2b.7.** Write the capture path and the exact observables into
      `runtime-working-set-ownership` S2b.7 (device half) and its `ARCHITECTURE-REVIEW.md`. **Verify:** a device
      class is claimed only for what the capture shows; anything unobserved is recorded NOT RUN rather than
      inferred.

## 4. Handoff and closeout

- [ ] 4.1 **Hand the stable fixture and the classified library to `runtime-working-set-ownership`.** Record the
      evidence link there for S1.3's device slope, S2b.3's post-restore baseline, the S2b gate, and then S3 —
      lease ownership. **Verify:** the handoff is recorded in that change; **this change does not start S1, S2b
      or S3**, and does not bypass the stage order because hardware evidence was blocked.
- [ ] 4.2 **Correct the stale drain requirement in its owning change.** The main `lazy-slot-hydration` drain
      requirement still states that the drain's advancement is "**Not delivered** … deferred to
      `runtime-working-set-ownership` S2b.7"; S2b.7 landed 2026-09-24. **Verify:** `runtime-working-set-ownership`
      carries the correction in its spec delta / closeout; this change does not rewrite another change's
      requirement.
- [ ] 4.3 **Closeout.** Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md` and
      `docs/DELIVERABLE_TRACKING.md`; append a `DEC-###` entry if `ARCHITECTURE_RULES.md` requires one; record
      the wording "no **currently eligible** native task remains" for `runtime-working-set-ownership`. **Verify:**
      docs updated; `openspec validate restored-library-stability` clean; the archived scope matches what was
      actually evidenced.
