# No-hardware OpenSpec reconciliation — 2026-10-11

User approved execution of the bounded triage batch on `feature/unified-rolling-playback-path`, HEAD `37a63011` plus pre-existing dirty work. No firmware/host behavior edits, device contact, build/upload/reset, SD operation or Git delivery.

## Completed documentary obligations

1. S4f.7 — single write authority: checked against production source and S4b.5 — active-slot authority evidence. Track setter's sole caller is private `TrackManager::applyActiveLoopIndex`; public callers request through the manager. Recorded actual decision/commit split, not a new behavior.
2. Runtime stale-envelope wording: checked absence of fixed32-lease wording in current scope and current N=8/derived-capability requirement title/body. No new capacity qualification.
3. Playback5.4 — honest envelope: checked proposal/design's eight-today/16-next/32–64-without-promise language and memory+deadline conditions. Other capacity gates open.
4. Continuous persistence DEC-020 — decision entry: checked existing decision; no duplicate append.
5. Stale drain specification: new narrow runtime delta for `lazy-slot-hydration` synced to its single main requirement, all8 scenarios retained and exact match verified. Native advancement delivered; task3.3 — bounded commit/cleanup and device confirmation remain separately owned. No full runtime spec sync or acceptance claim.

## Checklist disposition, not implementation checkoff

- Runtime S2.9 — extracted playback ownership becomes a non-checkbox transferred dependency to the playback sibling.
- LCR6.3 — playback consumer and6.4 — editor hydrate consumer become non-checkbox successor references, preserving their existing work identities.
- LCR6.5 — forbidden consumer paths becomes a standing invariant, retaining all three prohibitions. LCR's executable checklist now reads complete; no additional archive is performed or automatically inferred safe.

## Historical archive

`loop-effective-event-source` archived to `openspec/changes/archive/2026-10-11-loop-effective-event-source/` with explicit owner selection **without spec sync**. Initial strict validation found its legacy plain Requirements header. Normalized the header for validation, retained historical bodies and recorded withdrawal/replacement disposition; never promoted the obsolete eager-store/window-only delta to main specs. All26 checkboxes were already checked, including failed-eager-flatten withdrawal and replacement visual-cache acceptance. Archive CLI reports `specsUpdated=false`; `.openspec.yaml` retained. Outgoing evidence/reference links rebased for the archive depth; incoming plan/runtime links updated. Historical device evidence retained, not rerun or requalified.

## Checks and boundaries

- `pio test -e native`:3071cases/3064succeeded/7skipped/0failed,96.982s, exit0; one broad run for archive gate on preserved dirty tree.
- Strict validation passed: runtime-working-set-ownership, windowed-playback-generation, lazy-slot-hydration main spec, normalized archive candidate before move.
- `openspec validate --specs`:24passed/7failed. Failures: hitl-automation, internal-heap-external-memory-routing, long-record-memory-headroom, loop-content-history, revision-load, storage-session-jobs, storage-session-layout. These files are unchanged from HEAD; left outside this batch.
- Final exact narrow-sync parity, counts, changed-link resolution and diff hygiene checked at closeout. Code/tool/test diffs compared against initial dirty snapshot; no authored behavior edits.
- Runtime state/current scope, deliverable tracking and affected tasks updated. No new architectural decision; this implements existing transfers/invariants and owner-approved historical archival disposition.
- Do not close full composed NOTE_EDIT, timing-envelope, capacity, reader migration, long-loop/pixel or continuous-session gates from this batch.

## Final measured bookkeeping result

Active changes25→24. Unchecked383→374; partial8 unchanged (391→382 unfinished checklist entries).
Five evidence-backed obligations checked, three transferred references and one standing invariant
removed from executable accounting; archived change had no open tasks. Current counts719checked/
374unchecked/8partial. Exact parity/all8 drain scenarios and every archived relative link verified.
All7 globally failing specs are byte-identical to HEAD; all pre-existing tracked source/include/scripts/test
diffs are byte-identical to the initial dirty snapshot. Touched strict validations passed again.
Archive intentionally has `specsUpdated=false`; no new main loop-effective-event-source spec exists.
