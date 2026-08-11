## 1. Phase 0 — OpenSpec closeout

- [x] 1.1 Confirm proposal, design, specs deltas, tasks, and ARCHITECTURE-REVIEW are consistent with the two-pass model
- [x] 1.2 Point CURRENT_WORK / PROJECT_STATE at `overdub-pass-overlap-resolution` as proposed (docs; non-blocking to persistence)
- [x] 1.3 Add `docs/Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md` linking this OpenSpec and `183525` evidence
- [x] 1.4 Record Stage 5a-3 note: `183525` append WARNs are `duplicate` (reclaim hypothesis falsified for that class)
- [x] 1.5 Refine OpenSpec: `overdubSourceView` naming, complete delta, Phase 1 = view + tests only

## 2. Phase 1 — overdubSourceView + native tests

- [x] 2.1 Architecture gate Phase 1 posted in session (ARCHITECTURE-REVIEW)
- [x] 2.2 Implement establish/clear `overdubSourceView` at overdub start (`Loop` provides; `Track` lifecycle triggers)
- [x] 2.3 Ensure view is materialize-aware (includes `editPasses`; not bare CER when edits active)
- [x] 2.4 Native tests: view stable across simulated wraps; expected geometry exposed; source immutability
- [x] 2.5 Native tests: candidate lookup into the view (wrap-safe; high-then-low capture order does not break lookup)
- [x] 2.6 Do **not** wire view into `appendCaptureEventWithResult` accept/reject in this phase
- [x] 2.7 `pio test -e native`
- [x] 2.8 Implementation review Phase 1 checklist

## 3. Phase 2 — G2 (unified resolution, transitional dual storage)

- [x] 3.1 Architecture gate; PREFLIGHT encode path; DEC-031 transitional seal encoding
- [x] 3.1b Unified-pass review ([UNIFIED-PASS-ARCHITECTURE-REVIEW.md](UNIFIED-PASS-ARCHITECTURE-REVIEW.md))
- [x] 3.1c **User pin G2** (DEC-032) — design §19 updated; U1/U2 out of scope
- [x] 3.2 Slice 1: pending session delta (logical Add/Shorten/Hide) on `Loop`; clear with source view
- [x] 3.3 Bridge `resolveConstrainedGeometry` → pending delta; source view immutable; survives wraps
- [x] 3.4 Native matrix: Add, Shorten, Hide, multi-source, multi-wrap, immutability (`test_pending_note_change`)
- [x] 3.5 Slice 2: stop seal — capture chunks then edit rows; one undo grouping both (**encoding**, not semantic model)
- [x] 3.6 Gate `shouldRestoreCommittedOverlapOnOverdubStop` when `overdubSourceView` established
- [ ] 3.7 Wrap-safe candidate lookup on insert; demote reverse-tick/`isDuplicateCaptureEvent` as semantic authority
- [ ] 3.8 Shared `noteMinLengthTicks` globals
- [x] 3.9 One logical undo per stopped overdub session (`OverdubPassAdded` + companion `editPassIds`; GUS **STK2**)
- [x] 3.10 `pio test -e native` (slice 2: 1015/1015)
- [ ] 3.11 Implementation review Phase 2 checklist

## 4. Phase 3 — Retire capture dedup authority + device verify

- [ ] 4.1 Demote or remove `isDuplicateCaptureEvent` as semantic authority
- [ ] 4.2 Optional separate commit: deny WARN/CAP throttle if RING still floods (observability only)
- [ ] 4.3 Device capture past wrap + bar 41: continuous DFRAME, OLED updating
- [ ] 4.4 Update Stage 5 / wrap bugfix / CURRENT_WORK closeout
- [ ] 4.5 Implementation review Phase 3; ready for `/opsx:archive` when gates pass
