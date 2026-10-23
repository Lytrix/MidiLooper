# 3.15f — the short-loop window-route mechanism (design)

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Status: design only, no code.** Owner approved continuing the short-loop mechanism on 2026-10-23.

## The question

3.15f must choose how a **short loop (≤ 2 bars, ≤ 1 535 ticks)** gets the unified window route for NOTE_EDIT without
assuming whole-loop coverage. Three options were named: (1) sub-interval coverage with wrap/closure and handoff
cadence, (2) whole-loop coverage under a measured span/cost policy, (3) position-free preparation in the DEC-136 shape.

## Why options 1 and 2 cannot work under the declared envelope

The ordinary admission's lead test is `CoverageServiceContract::canCompleteWithinLead`:

```
required == leadTicks + grains × (maximumConsecutiveDeniedTurns + 1) × maximumPhaseAdvancePerTurn
```

Against a 1 536-tick loop, DEC-136 already measured that **even the smallest possible coverage demand needs 1 792
ticks** (`768 + 2 × 8 × 64` for one fill/order/promotion grain set), and a five-grain demand needs 3 328. The lead
available to a window is at most the loop length, so **no interval length fits**:

- whole loop (1 536): lead 768 → refused with any grains;
- one bar (768): entry lead 1 152 → refused with the minimum 2–3 grains;
- smaller aligned intervals: the minimum demand cost stays 1 792 > 1 535.

Options 1 and 2 therefore require **changing the constants or the policy** to make one geometry fit. DEC-136 rejected
exactly that ("loosening them to make one reproducer fit would erase the information the scheduler currently gives for
every geometry, and it is a bound-class change needing its own measurement"). The task's own wording also says no
choice may weaken the DEC-136 envelope.

## Chosen mechanism: option 3, position-free preparation in the DEC-136 shape

The route is the one DEC-136 already established for the demand the ordinary envelope can never admit: **impossibility
is the waiver**. This extends it to the NOTE_EDIT preview demand, with the same narrow, derived rule and no constant
change.

### Contract

1. **Structural classification.** A preview demand qualifies only when all hold: the slot is the selected track's
   active slot; NOTE_EDIT is the active session and its playback-preview revision is nonzero; the loop has committed
   passes and a nonzero length; the ordinary admission **cannot** serve the demand
   (`Track::ordinaryAdmissionCannotServeDemand` evaluated against the demand's **own derived** cost, i.e.
   `!canCompleteWithinLead(loopLengthTicks)`); no other owner's successor holds the role; and the served source is the
   existing NOTE_EDIT fallback (the merged session stream) that already serves the same content.
2. **Position-free while owed.** The demand is marked with a **named** position-free reason (mirroring
   `PrivateRootReturnPreparation`; a distinct `PositionFreePreviewPreparation` keeps the two classifications
   observable), carries **no deadline** (none is fabricated), and the pre-admission position gates tolerate it *only*
   while (1) holds. The served fallback keeps serving throughout, so no promise is broken by the wait.
3. **Impossibility is the waiver.** The only term waived is the lead test, and only where the envelope itself proves no
   legal lead exists for this demand's own cost. If any legal lead exists, the mark is cleared and the ordinary path
   runs unchanged, including its `CannotCompleteBeforeHandoff` refusal and retention (still observable).
4. **Everything else is preserved.** The same candidate validation (source identity, preview revision, currency, row
   count, read units), the same grain accounting, the same single successor role, the same atomic adoption at a real
   boundary where the completed window's coverage contains the playhead, and the same session-exit handoff to the
   committed successor. No new owner, queue, view, emitter, resident field or scheduler; no change to
   `canCompleteWithinLead`, its constants, or another slot's promise.
5. **Containment.** The fallback is never torn; an edit mutation cancels the preparation by name (the existing
   `SourceMutated` revision edge) and the next revision re-requests; a refusal for any other reason (role occupied,
   sibling promise, identity) **defers** rather than destroys the qualified candidate.
6. **Route identity after adoption.** Once the prepared preview window is complete and adopted, the clock edge serves
   it (not the merged fallback), and the window's whole-loop coverage contains the playhead for the rest of the session
   (the preview lane sees `currentServesPlayhead` and owes nothing until the session changes). That is the cutover
   claim; whole-loop **interval** identity is not required by 3.15f, and is not asserted as such.

### Where it attaches (implementation sketch, not this step)

- `Track::servicePreviewWindowForNoteEditSession` (preview lane request) → `requestPreviewWindow` admission: consult
  the classification before the position/lead refusal, mark the demand position-free, and still run the same
  build/role/currency path.
- `Track::ordinaryAdmissionCannotServeDemand` (DEC-136's derived impossibility) is the single waiver predicate; the
  preview demand supplies its own derived cost.
- `PlaybackViewPendingReason`: add the named position-free preview reason beside `PrivateRootReturnPreparation`.
- `CoverageServiceContract`: **unchanged**.
- The served-source selection and clock-edge adoption are unchanged: the fallback serves until the window is adopted.

## Acceptance (fixtures first)

| # | Case | Expected |
|---|---|---|
| **S1** | two-bar NOTE_EDIT (the existing `test_3_15d_slice3_short_and_long_note_edit_emit_the_same_stream` case) | after the position-free build, the short route **selects the preview window** (window identity/selection witness), not the merged fallback; the emitted stream still equals the long route (the note at 300–500 is inside the whole-loop coverage, so no boundary split is introduced) |
| **S2** | waiver derivation | a 1 536-tick loop qualifies (impossible by arithmetic); a loop with a legal lead keeps the ordinary path and its refusal when positioned ahead; no draft when a legal lead exists |
| **S3** | mutation | disabling the waiver restores the whole-loop request + `CannotCompleteBeforeHandoff`; the short route serves the fallback and S1's route-identity assertion fails (RED) |
| **S4** | edit during the position-free build | the preparation is cancelled by name (`SourceMutated`), the fallback keeps serving, the next revision re-requests; no stale completion publishes |
| **S5** | session exit during the build | the existing handoff to the committed successor is unchanged; the position-free demand does not delay the exit handoff |
| **S6** | role occupied / sibling promise | the qualified candidate is retained and defers; nothing is destroyed |
| **S7** | ledger/duplicates at adoption | the adopted window emits each row once and the ledger matches the fallback's; switching from fallback to window at the boundary does not duplicate or drop a row |

## Risks

1. **Fallback-to-window handoff at adoption.** The fallback and the window must not both emit a row at the switch
   boundary. The existing preview adoption boundary plus the S7 ledger/duplicate witness bound it.
2. **Wait length.** A position-free build has no deadline; its grains are the same as any window. A very large edit
   session interval could make the build long, but the fallback serves throughout and an edit cancels/rebuilds.
3. **Classification over-inclusion.** The waiver must be derived from the demand's own cost, not from "loop length ≤ N".
   S2's 1 536/legal-lead split is the discriminator.
4. **Role capacity.** The preview lane already competes for the single successor role; the position-free demand is the
   same role and the same deferral rules, so no new capacity is introduced.
5. **The parity test's premise.** The existing test's content fits the whole-loop coverage; a future note that straddles
   the loop end is the normal wrap/closure rule, not a new split. If a sub-interval were ever chosen later, that test
   would need its premise restated; this design does not choose one.

## Rejected alternatives

- **Sub-interval coverage (option 1).** Cannot admit any interval on a ≤ 1 535-tick loop under the declared lead
  (minimum demand 1 792); would require a policy/constant change.
- **Whole-loop span/cost policy (option 2).** Same arithmetic; a policy exemption is exactly the "make one reproducer
  fit" change DEC-136 rejected.
- **Changing the constants (advance/denial/lead).** A bound-class change needing its own measurement; erases the
  scheduler's information for every geometry.

## Not claimed

No implementation, no checkbox, no device parity yet. The device leg (3.15f) consumes this design after the native
fixtures land; the short-loop route remains on the merged fallback until then.
