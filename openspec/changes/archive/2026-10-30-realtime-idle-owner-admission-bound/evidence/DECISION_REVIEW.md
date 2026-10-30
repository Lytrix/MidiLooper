# Decision review — realtime idle-owner admission bound

Run per [`docs/Templates/DECISION_REVIEW.md`](../../../../docs/Templates/DECISION_REVIEW.md) before firmware edits
(task 1.1). This is the review for the change's first slice; later slices re-run the relevant rows only.

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../../docs/DECISION_LOG.md)
- [x] Active OpenSpec — `openspec/changes/realtime-idle-owner-admission-bound/`, and the sibling
      `windowed-playback-generation`, `resumable-midi-boundary-playback-maintenance`
- [x] `openspec/specs/` — `windowed-playback-generation`, `overdub-pass-overlap-resolution`,
      `deferred-job-scheduler`
- [x] `docs/Plans/` — `runtime_scheduling_owner_boundary_admission_refinement.md`,
      `runtime_scheduling_admission_model_architecture.md`,
      `playback_window_build_owner_boundary_refinement.md`
- [x] `docs/Authority/Architecture/ExecutionBounds.md` (rules 1–4 + owner inventory)

**Search terms used:** `idle_maint`, `loop_rem`, `admit(WorkClass)`, `bound class`, `ExecutionBounds`,
`kDeviceGateSliceBudgetUs`, `kLoopRemainderOneShotUs`, `resolveDisplayNotes`, `overdubSourceSpanCache`,
`rebuildOverdubSourceView`, `persist_save`, `maxPersistenceMicrosActive`, `SEVT`, `stored_verify`,
`deferral`, `retained Start`, `owner turn`.

### Relevant findings

| ID | What it says | Bearing on this change |
|---|---|---|
| **DEC-043** | Bound class is a gated architectural property; point 4: *"Local owner budgets stay. They do not replace a declared bound class and do not make an indivisible step structurally bounded."* Point 3: `RuntimeWorkBudget::admit(WorkClass)` remains rejected; no scheduling class, Manager or interval reservation. | This change must **not** introduce a work-class scheduler. `kOwnerWorkBudgetUs` is a local owner budget in DEC-043's sense: it admits *dispatch*, it does not reclassify a child. Row 14 stays unbounded-in-effect until its step is split (correction 5 is DEC-043 point 4 applied literally). |
| **DEC-191** | A retained overdub Start re-drove a ~109 ms `Track::startOverdubbing` on **every** owner turn: `idle_maint` 130.8 ms repeated ~7×/s, `midi_gap` 132 ms, paint 0.7 fps. | The precedent for this exact failure class: one expensive child on the owner turn converts the instrument into a low-rate loop. It is also why a deferral must be **observable** — the retry was invisible except through its cost. |
| `resumable-midi-boundary-playback-maintenance` (2026-09-15) | "idle maintenance reached 281,346 us, a sampled display frame 538,356 us, and the window MIDI-service gap 281,883 us … The realtime dispatcher now services realtime tracks only; pending stopped-track wrap work cannot block the active display." | A prior **partial** fix in this same area: it stopped background *stopped-track* work from stacking, but left the selected realtime track's own children unbounded. This change generalises that fix instead of replacing it; the per-track `hasRealtimeIdleWork` gate stays. |
| `openspec/changes/loop-content-resolution/tasks.md` | "investigate MIDI Input Gap > 50 ms … Owner: `processDeferredSaveState` / `FinalizeWorkspace` … Persist slice budget while PLAYING is 300 µs; one finalize/SD step overruns. Fix is a persist change, not this LCR change." | The persistence breach is **already attributed in principle and deferred on purpose**. Correction 5 (do not reclassify row 14) matches that record; section 3 only names the step, section 6 does the repair. |
| `ExecutionBounds.md` rows 19 / 12 / 5 / 8 / 9 / 10 / 27 / 14 | 19 **Unbounded in aggregate**; 12 **Unbounded**; 5/8/9/10 the whole-loop resolve fallback **Unbounded**; 27 caps declared, **not admitted**; 14 **Empirically bounded** (budget between steps). | The inventory already declares the defect. This change moves 19/12/5/8/9/10 in the commits that enforce them and leaves 14 alone until section 6. |
| `windowed-playback-generation` | "Playback maintenance SHALL receive a recurring, **bounded** service opportunity on the existing deferred machinery — no second scheduling framework … an admitted turn SHALL perform one declared grain with no hidden stacking." | The sibling contract this change completes for the *aggregate* turn. The playback lane's own grains measured 0.175–0.684 ms; it was starved, not overrunning. Do not touch its internal grain. |
| `overdub-pass-overlap-resolution` C3.1/C3.2/C3.5 | A private-predecessor scan in progress is not a source; `fullLoop` is not raised until holds are materialized; a partial prefix must never mix with a committed suffix. | Preserved verbatim. The change adds a **coverage currency** rule: an established view is authoritative only while its coverage contains the required interval (see `design.md` D3). It does not weaken C3.2 — it removes the case where a stale window could be served as current. |
| DEC-181 | Windowed display-note resolve with the same per-pass local pairing/`NoteId`s as the whole-pass resolve. | The mechanism item 1 reuses. The parity gates in tasks 4.1/4.7 exist because DEC-181 proved window/whole equivalence for the *display* window shape, not for the assembled multi-window cache. |

### Existing reusable pattern

- **Owner boundary:** `MidiServiceDrain`'s existing inline policy helpers (owner rotation,
  `allowDeferredIdleOwner`, `allowVisualCacheReconstructSlice`, `serviceCanonicalPrefixTurn`), consumed
  by `runDeferredIdleMaintenance` in `src/main.cpp`. The predicate extends this header; it does not add
  a scheduler.
- **Observation:** the existing `idle_maint` span + `loop_rem` one-shot (`kLoopRemainderOneShotUs`) and
  the Tier-A classifier in `include/Utils/CaptureLineTier.h`.
- **Bounded resolve:** `LoopContentResolution::resolveDisplayNotesInWindow` (DEC-181) and
  `CommittedEventRange::inWindow`.
- **Resumable grain:** the repo's established cursor + budget shape (`LoopSnapshotConstruction::step`,
  `PersistenceBudget`, `DeferredJobScheduler::runFrame`).
- **Named deferral:** the existing `win_readiness` / `win_frozen_readiness` "name the holder that owes
  it" pattern from `windowed-playback-generation`.

### Reuse decision

**YES.** No new owner, scheduler, queue, work class or bound class is introduced. The change extends the
existing deferred-idle-owner boundary, the existing resolve primitive, and the existing observation
rows. `RuntimeWorkBudget::admit(WorkClass)` stays rejected (DEC-043 point 3).

### New constraints introduced (for future work)

1. A child on the deferred idle-owner turn **must** declare a maximum grain; admission reserves it
   before dispatch. A child without one is denied while timing-critical, not "run and hoped to stop".
2. The owner work budget is **strictly smaller** than the breach threshold, and both are recorded; a
   turn landing at "threshold minus one microsecond" is not success.
3. An established `overdubSourceView` is authoritative **only inside its coverage**. Any future change
   that defers source-view maintenance must prove coverage currency, or land atomically with the
   bounded refresh (this change's D3).
4. `fullLoop=true` is a **parity-gated** claim, not a "all windows visited" claim.
5. Naming an indivisible step does **not** bound it (DEC-043 point 4); row 14 does not move until
   section 6 splits it.

**Migration required:** **NO** — firmware-only; no SD format, payload, workspace or revision format
change. No data migration, no test-matrix change beyond the new suites, one HITL/device oracle run.

## Reviewer gate

Reject if: an equivalent abstraction already exists (no — see reuse decision); ownership is duplicated
(no — the owner and rotation are unchanged); a historical decision is ignored without supersede (no —
DEC-043, DEC-181, DEC-191 and the C3.x contract are cited as constraints, not overturned); or the
migration path is missing (not applicable).
