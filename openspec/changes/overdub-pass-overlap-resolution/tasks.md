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

## 3. Phase 2 — Canonical overlap → overdubPass delta

- [x] 3.1 Architecture gate Phase 2; Open Q4 = C→A; PREFLIGHT + DEC-031 (undo/`editPassIds`, restore gate, commit order)
- [ ] 3.2 Slice 1: session pending-op buffer + bridge from `resolveConstrainedGeometry` → pending Add/Shorten/Hide (no mid-session EditPass writes)
- [ ] 3.3 Keep `overdubSourceView` immutable; pending buffer survives wraps; clear on discard/commit
- [ ] 3.4 Native matrix (buffer level): Add, Shorten, Hide, multi-source, multi-wrap, source immutability
- [ ] 3.5 Slice 2: stop seal — OverdubPass then EditPass rows then one `OverdubPassAdded` (`passId` + `editPassIds`); GUS + PassReclaim
- [ ] 3.6 Gate off `shouldRestoreCommittedOverlapOnOverdubStop` remove path when `overdubSourceView` established
- [ ] 3.7 Replace invalid reverse-tick early-out; wire wrap-safe lookup into `overdubSourceView` on insert path
- [ ] 3.8 Reconcile Q16 capture min-length with shared `noteMinLengthTicks` globals (document + tests)
- [ ] 3.9 Native/integration: one overdubPass + one undo reverses Adds and Shorten/Hide; edit-path parity
- [ ] 3.10 `pio test -e native`
- [ ] 3.11 Implementation review Phase 2 checklist

## 4. Phase 3 — Retire capture dedup authority + device verify

- [ ] 4.1 Demote or remove `isDuplicateCaptureEvent` as semantic authority
- [ ] 4.2 Optional separate commit: deny WARN/CAP throttle if RING still floods (observability only)
- [ ] 4.3 Device capture past wrap + bar 41: continuous DFRAME, OLED updating
- [ ] 4.4 Update Stage 5 / wrap bugfix / CURRENT_WORK closeout
- [ ] 4.5 Implementation review Phase 3; ready for `/opsx:archive` when gates pass
