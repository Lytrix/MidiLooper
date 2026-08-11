## 1. Phase 0 — OpenSpec closeout

- [x] 1.1 Confirm proposal, design, specs deltas, tasks, and ARCHITECTURE-REVIEW are consistent with the two-pass model
- [x] 1.2 Point CURRENT_WORK / PROJECT_STATE at `overdub-pass-overlap-resolution` as proposed (docs; non-blocking to persistence)
- [x] 1.3 Add `docs/Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md` linking this OpenSpec and `183525` evidence
- [x] 1.4 Record Stage 5a-3 note: `183525` append WARNs are `duplicate` (reclaim hypothesis falsified for that class)

## 2. Phase 1 — Source-pass lookup + deny throttle

- [ ] 2.1 Architecture gate Phase 1 posted in session (ARCHITECTURE-REVIEW)
- [ ] 2.2 Pin API that freezes/exposes pre-session canonical source view at overdub start (audit `beginOverdubSession`)
- [ ] 2.3 Implement wrap-safe source-pass candidate lookup via `CommittedEventRange` / materialize (no append-order reverse-tick authority)
- [ ] 2.4 Throttle duplicate/deny WARN and `append,deny` CAP during storms
- [ ] 2.5 Native tests: high-then-low store order still finds source candidates; wrap phase re-lookup
- [ ] 2.6 `pio test -e native`
- [ ] 2.7 Implementation review Phase 1 checklist

## 3. Phase 2 — Canonical overlap → overdubPass ops

- [ ] 3.1 Architecture gate Phase 2 posted; PREFLIGHT if encode ownership unclear
- [ ] 3.2 On insert, obtain shorten/hide/add decisions from `NoteGeometryResolver` / `resolveConstrainedGeometry` semantics
- [ ] 3.3 Accumulate ops on pending overdub pass; keep source pass immutable
- [ ] 3.4 Reconcile Q16 capture min-length with shared `noteMinLengthTicks` globals (document + tests)
- [ ] 3.5 Native matrix: duplicate, overlap shorten/cover, min-length boundary, multi-wrap same-phase re-eval, source immutability, edit-path parity
- [ ] 3.6 Ensure session still commits one `overdubPass` / one undo at stop
- [ ] 3.7 `pio test -e native`
- [ ] 3.8 Implementation review Phase 2 checklist

## 4. Phase 3 — Retire capture dedup authority + device verify

- [ ] 4.1 Demote or remove `isDuplicateCaptureEvent` as semantic authority (no contradictory policy left)
- [ ] 4.2 Device capture past wrap + bar 41: continuous DFRAME, OLED updating
- [ ] 4.3 Update Stage 5 / wrap bugfix / CURRENT_WORK closeout
- [ ] 4.4 Implementation review Phase 3; ready for `/opsx:archive` when gates pass
