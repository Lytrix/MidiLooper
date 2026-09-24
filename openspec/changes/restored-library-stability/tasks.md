# Tasks — restored-library-stability

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing. **Status 2026-09-24: proposed, nothing started.
G2 review corrections applied**: measurement definitions (D4), an on-demand diagnostic (D2), a nullable
observed-vs-attributed classifier (D3), and a manifest-based fixture gate (D6). Read-only attribution may
proceed; the corrected measurement definitions come first, because they may explain some apparent instability
without a firmware fix.

This change is **investigation-first**. Packet A is read-only and needs no authorization. Packet B may need a
per-slot destructive action (TBD-1) and stops at read-only without it. Packet C is a **device** confirmation
that belongs to `runtime-working-set-ownership` S2b.7 and is **independently reportable**; this change supplies
the fixture that can hold it.

Every task that edits firmware follows the [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice):
pin the scope and transition oracle, establish the controlled production-path fixture, and record actual
evidence before checkoff. No behavior change is proposed until Packet A evidences the observed state of each
slot.

Standing constraints for every task:

- **No wider sweep.** Recording is limited to the already-approved scratch range (track 5 slot 1, track 6
  slots 1–8). Anything outside it is not written.
- **No destructive SD action** (quarantine, delete, rename, CLEAR) without the owner's explicit authorization
  for the **named slots** in the same session — [`Destructive-SD-Operations`](../../../.cursor/rules/Destructive-SD-Operations.mdc).
- **Observe first; attribute second, and only with evidence.** `empty/not applicable`, `pending`, `ambiguous`
  and `unknown` are valid outcomes. Never force one of four causes.
- **State the window.** Every count is window-scoped; an incomplete window is reported as incomplete.
- New firmware observables are session-gated, bounded per invocation, FLASH-resident where possible, and the
  post-link RAM1 budget (`padding >= 840`) is re-read in the slice that adds them.

## 1. Packet A — read-only attribution (no behavior change)

- [ ] 1.0 **Execution-packet gate.** Before any behavior edit, write the G2 execution packet for that slice
      into the change's review artifact (`ARCHITECTURE-REVIEW.md`): fixed transition oracle, controlled
      production-path fixture, repeatable recipe, and evidence expectations. Scale to risk. **Verify:** a packet
      exists for every slice that edits firmware; the read-only investigation tasks need no packet.
- [ ] 1.1 **Correct the recorded measurement definitions and attribution (documentation only).** Record, with
      citations, in this change and in `runtime-working-set-ownership` S1.3/S2b.3 and the S2b gate:
      (a) `n_committed_slots` is the distinct `(track, slot)` of `RRQ,…,terminal,none,committed` observed
      **within the capture window**, background admission included — so it is neither the boot playback set nor
      the library size, and the earlier explanation for `N=29` is withdrawn;
      (b) `MidiLooper/sets/index.bin` is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a saved-set
      folder counter, not a slot catalog;
      (c) the boots that appeared to shrink (`captures/s13-slope-02/`) report `reconcile_warnings=0`;
      (d) `DIAG,lcr,skip,restore` is a **content-resolution** line, not a library marker;
      (e) `Track 6 loaded state: EMPTY` appears on the stable `N=29` boots too;
      (f) `free_before/after` 512 → 480 is the enabled-set walk, not the library;
      (g) both sweeps' `library_rows` are all-zero, so no per-slot inventory was captured.
      **Verify:** no durable doc still asserts the catalog reconcile, or `n_committed_slots`, as a demonstrated
      cause of loss; each fact is cited with its evidence path.
- [ ] 1.2 **Define the observed-state fields, the nullable classifier, and the four counts.** Specify the
      per-slot observed fields (payload validity, enumeration, request state/wait/terminal with the **full
      identity** `(track, slot, requestGeneration, setEpoch)`, committed content inventory, selection), the
      four quantities of design D4, the snapshot-completeness fields, and a **pure** classifier mapping
      observation to a nullable cause with stated precedence. **Verify (native, red first):** a case per
      outcome; an empty slot yields `empty/not applicable` and never a loss cause; a corrupt payload that also
      produced a `refused` terminal records both and yields `ambiguous`; a queued/running/waiting request yields
      `pending` with no terminal asserted; a historical deletion with no prior inventory yields `unknown`; two
      equally consistent causes yield `ambiguous` with both recorded; the four counts and the window fields are
      separate inputs/outputs.
- [ ] 1.3 **Add the bounded on-demand diagnostic snapshot.** Provide a snapshot that does **not** depend on
      `emitPostRestoreCensusIfReady`'s settled gates (`bootBackgroundFillAdmissionComplete`,
      `bootInteractiveReady`'s empty queue / no live session / inactive jobs, no undo hydration pending,
      transport stopped, no holdoff or in-flight persistence work, no owed playback prewarm). Specify:
      **incremental collection** — bounded slots and bounded SD reads / content traversal per invocation, with
      the per-invocation bound declared, so one line never implies a bounded traversal over the whole library;
      **snapshot consistency** — the observation turn per slot, and a cross-turn snapshot marked as such;
      **capture framing** — window start/end, whether every initial-playback slot was observed, whether
      admission completed, and the four counts. Retain the settled census unchanged for settled memory
      measurements. **Verify:** a native case drives a **permanently waiting** request and asserts the snapshot
      is still produced and names the state/wait, while the settled census does not fire; a case over a library
      larger than one invocation asserts incremental collection within the bound and that the partial result is
      marked incomplete with its turns; the snapshot is never emitted as the settled measurement; the RAM1 line
      is recorded.
- [ ] 1.4 **Re-analyze the existing captures read-only, then extend the sweep parser.** Run the classifier over
      `captures/s13-slope-02/` and `captures/s13-slope-final/` and extend
      `scripts/hitl_library_growth_sweep.py` to record the observed fields, the four quantities, the window
      completeness and the nullable cause per slot. **Verify:** each slot gets an observed state; each
      attribution carries its evidence or is `unknown` with the missing field named; the re-analysis performs
      **no** device write; the reported `N` is explicitly window-scoped.
- [ ] 1.5 **Read-only device attribution, including incomplete windows.** Capture fresh boots of the current
      device state without writing or clearing anything, classify per boot, and **explicitly test the
      incomplete-window case** — a capture closed before background admission completes or before every
      initial-playback slot is observed. **Verify:** the four quantities are reported per boot; the window
      completeness is stated; an incomplete window is reported as incomplete and its counts are **not**
      presented as the settled library state; 0 `FATAL`; no SD mutation.

## 2. Packet B — one repeatable fixture inside the approved scratch range

- [ ] 2.1 **TBD-1 authorization gate.** If the fixture needs a slot emptied, request the owner's explicit
      authorization naming the **specific slots** and the action, in the same session. **Verify:** the
      authorization is recorded in this change before the action; without it, Packet B stops at read-only and
      this task stays unchecked.
- [ ] 2.2 **Prove fixture stability by the complete manifest, not by one slot surviving.** Record the expected
      per-slot manifest — payload presence/token, committed content identity and counts, the initial
      playback-required set, and the authorized changes — and compare it in full on each of three consecutive
      boots. **Verify:** any unexplained difference between the observed and expected manifest is a
      **failure**, including a difference in a slot that is not part of the fixture; the pass requires every
      slot to match, not merely the fixture slot to persist; **no slot outside the approved range is written**
      (asserted from the offline manifest, not from intent).
- [ ] 2.3 **Prove the fixture can hold the S2b.7 condition.** Show the fixture yields a save owed + a restore
      holding the SD session + another track playing, with `deferredSaveBlockedByActiveSlotLoadSd()` true at
      the drain edge. **Verify:** the condition is observed, not assumed; the exact observables are recorded;
      no external advancement is performed inside the run.

## 3. Packet C — S2b.7 device confirmation (independently reportable; task owned by `runtime-working-set-ownership`)

- [ ] 3.1 **Run the S2b.7 device leg on the held fixture.** Use the authoritative HITL entry point with a
      registered or reused stimulus (list before adding; never edit a canonical stimulus). Report **which**
      permitted outcome occurred, with evidence, rather than asserting one:
      (i) the drain returns `TemporarilyBlocked` with the named **`waiting_commit_gate`**; **or**
      (ii) the owed save is discharged by the documented **urgent-save priority** while the restore remains
      commit-gated. Both require **no unintended publication** (no committed pass appears while the gate
      denies), the request preserved, and progress advanced. (ii) is **not** evidence that the drain advanced
      the restore, and the report says so. **Verify:** the capture and the exact observables are recorded; the
      outcome is stated as observed.
- [ ] 3.2 **Keep Packet C independently reportable.** Give this packet its own checkoff and evidence, and do
      **not** make it gate, or depend on, this change's closeout or another change's closeout — the defect the
      archived sub-change's closeout dependency caused. **Verify:** S2b.7's device half can be checked from
      Packet C's evidence alone; this change's closeout is not a precondition for it.

## 4. Handoff and closeout

- [ ] 4.1 **Hand the stable fixture and the classified library to `runtime-working-set-ownership`.** Record the
      evidence link there for S1.3's device slope, S2b.3's post-restore baseline, the S2b gate, then S3 —
      lease ownership. **Verify:** the handoff is recorded in that change; **this change does not start S1, S2b
      or S3**, and does not bypass the stage order because hardware evidence was blocked.
- [ ] 4.2 **Correct the stale drain requirement in its owning change.** The main `lazy-slot-hydration` drain
      requirement still states that the drain's advancement is "**Not delivered** … deferred to
      `runtime-working-set-ownership` S2b.7"; S2b.7 landed 2026-09-24. **Verify:**
      `runtime-working-set-ownership` carries the correction in its spec delta / closeout; this change does not
      rewrite another change's requirement.
- [ ] 4.3 **Closeout.** Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md` and
      `docs/DELIVERABLE_TRACKING.md`; append a `DEC-###` entry if `ARCHITECTURE_RULES.md` requires one; record
      the wording "no **currently eligible** native task remains" for `runtime-working-set-ownership`.
      **Verify:** docs updated; `openspec validate restored-library-stability` clean; the archived scope matches
      what was actually evidenced.
