# OpenSpec iterative closeout plan

**Status:** guidance and handoff only. Not implementation authority, not firmware authorization, not a checkoff. OpenSpec `tasks.md` and `docs/Authority/**` own scope and precedence; if this plan and a task clause disagree, the clause wins. Readiness statements in this plan apply **only** to the one selected packet and its own G2 execution packet — never to a parent task, a stage, or the change.

**Revision disposition.** One focused assessment reviewed revision 1 and returned **REVISE** with five findings; this revision addresses all five. No second approval was requested or is claimed, and this document is not marked PASS or universally ready. The durable artifact is usable now as a documentation deliverable even while the broad firmware, hardware and timing gates remain open.

**Provenance and conversion boundary.** Source canvas: `/Users/eelkejager/.cursor/projects/Users-eelkejager-Documents-PlatformIO-Projects-250513-215524-teensy41/canvases/openspec-closeout-triage.canvas.tsx` (verified present, 17,495 bytes, 11 Oct 21:46; the earlier slash-variant path was a mistyped path, not a missing file). The canvas is a read-only pre-cleanup triage snapshot; its inventory counts are superseded by the owner-approved reconciliation closeout. Where they differ, the closeout governs. Conversion boundary: this plan carries the canvas's *dispositions* (what is a candidate, a transfer, an invariant, an archive, a parked folder) plus its batching intent — it does not copy the canvas's counts, does not restate its tables, and adds no acceptance requirement the tasks do not already carry. Every task clause below is cited by its searchable ID and quoted opening text, with a file link; no frozen line numbers and no deep section anchors are used.

## Start prompt (copy verbatim; audit-only by default)

> Repo `/Users/eelkejager/Documents/PlatformIO/Projects/250513-215524-teensy41`. Unless this prompt names a specific authorized task, permission or packet, execute **packet I1 (audit-only)** and nothing else — I1 changes no production behavior and checks no box. Confirm revision yourself (`git rev-parse --short HEAD`, `git branch --show-current`, `git status --porcelain`), then read `AGENTS.md` and follow its full before-coding sequence: `docs/Authority/README.md`, `PROJECT_INTENT.md`, `ARCHITECTURE_RULES.md`, a targeted search of `docs/DECISION_LOG.md`, `docs/Authority/IMPLEMENTATION_READINESS.md` §G2, and the active scope in `docs/Runtime/PROJECT_STATE.md` and `## Now implementing` in `docs/Runtime/CURRENT_WORK.md` (search the heading). Then take the selected packet from `docs/Plans/openspec_iterative_closeout_plan.md`, open only the task clause it names plus the one evidence packet it names, and write its G2 execution packet before changing any production behavior. Report ≤15 lines and update only the latest handoff block — never append a diff of an earlier handoff. If a permission is missing, stop and name it.

## Checkpoint already closed — never queue again

Verified in the owner-approved reconciliation closeout (`openspec/changes/runtime-working-set-ownership/evidence/2026-10-11-openspec-reconciliation/CLOSEOUT.md`): five documentary obligations closed (S4f.7 — single write authority; runtime stale-envelope wording; playback 5.4 — honest envelope; DEC-020 decision entry; the narrow lazy-slot-hydration drain delta synced to its single main requirement with all scenarios retained); S2.9 — extracted playback ownership as a transferred dependency; LCR 6.3/6.4 as successor references and 6.5 as a standing invariant; `loop-effective-event-source` archived with an owner-approved `specsUpdated=false` disposition. Bookkeeping then: 24 active changes, 719 checked / 374 unchecked / 8 partial, native 3071 total / 3064 passed / 7 skipped / 0 failed, and seven global spec-validation failures in HEAD-identical, untouched specs that stay out of scope. `successor-capture-reentry-unguarding` task 5.11 is checked yet REOPENED in prose — reconcile that inside its own change, retaining its repeat-run rate; it is recorded here as a status defect, not a blocker.

## Iteration manifest

| Packet | Task IDs + searchable clause | Evidence packet | Command(s) | Exit predicate |
|---|---|---|---|---|
| **I1** audit-only | `S2b.0 **First-slice prerequisites`; `S4f.8 Make the waits observable`; `S4f.9 Prove no new capacity is implied` (`runtime-working-set-ownership/tasks.md`) | `IMPLEMENTATION_CONTRACT.md` §1; `evidence/2026-10-10-edge-effect-boot/EXECUTION.md`; `evidence/2026-10-11-record-return-recovery/EXECUTION.md` | `pio test -e native -f test_shared_capture_finalization`; `... -f test_runtime_working_set_ownership`; `pio test -e native` | A clause→symbol→test→carrier map per task, plus a characterization run. **No checkbox.** |
| **I2** decision | `S7.5a **Pin the continuity envelope and timing oracle` | `evidence/2026-10-11-s75a-continuity-envelope/LIMITS-AND-ORACLE-PROPOSAL.md`, `DECLARED_ENVELOPE.md`, `INSTRUMENTS.md` | `.venv/bin/python -m hitl.measure.envelope_timings <capture>`; `PYTHONPATH=scripts .venv/bin/python -m hitl.measure.envelope_timings` | Owner-recorded limits in each metric's own unit, per-row measurement plan, holder/reserve/competitor budget, verdict oracle. No limit fitted after a run. |
| **I3a** boot | `**Qualify repaired edge-effect boot on hardware.**` (runtime `tasks.md`, live-observation/lifecycle section) | `evidence/2026-10-10-edge-effect-boot/EXECUTION.md` | managed run via `.venv/bin/python scripts/host_midi_hitl.py run …` with a registered stimulus | Fresh pinned-image boot: no pre-publication invalid edge-effect rows, no unsolicited drain, boot restore completion, live UI handoff, ordinary deferred effects — physical panel observed, not inferred. |
| **I3b** seeded edit | `S7.5b.8b **Bound entry, selection and focus preparation on the existing edit owner.**` | `evidence/2026-10-09-s75b8-note-edit-entry-boundary/EXECUTION.md`, `NOTE-EDIT-ENTER-24BAR-DEVICE-RUN.md`, `LIVE-ENTRY-HITL-RECIPE.md` | offline `--verify-only` replay first, then the seeded edit preset on track 8 / slot 4 | Intended seed committed, PLAYING target, move → deselect → reselect, final shutdown. Entry-only evidence stays entry-only; I3a and I3b are separate packets with separate predicates. |
| **I4** UI/storage | `save-status-display`, `slot-selection-focus`, `load-save-overlay-display-regression` task clauses | those changes' own evidence folders | one managed session, separate assertions | Spinner completes during PLAYING; focus/reboot/revision restored; dirty-load overlay without freeze. |
| **I5 series** playback | ladder below | `windowed-playback-generation/tasks.md`; `CONTINUITY-HANDOFF.md`; `evidence/2026-10-08-s315-end-state-timing/EXECUTION.md`; `evidence/2026-10-08-s315bc-short-loop/EXECUTION.md`; `evidence/2026-10-11-first-overdub-supply-stall/FIRST-OVERDUB-SUPPLY-STALL.md` | `pio test -e native -f <named suite>`; `pio run -e teensy41-capture-serial` | Per slice: exact MIDI/ledger against the independent oracle, old emitter retained until 3.7b. |
| **I6** content/service | `4.3`, `4.4`, `6.4`, `3.10`, `3.15f` clauses | `evidence/2026-10-09-s39-hardware-receipts/EXECUTION.md`; registered `STIM-WINDOW-CONTENT-01` | `.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity …` on the pinned qualification image | Accepted/committed/effective inventories compared element-wise; span, density and clock identities retained; missed deadlines named as failures. |
| **I7** finalization | `S4f.13`, `S4f.22` | their evidence packets | managed session | Natural overlap and continuous transport; repeat-rate and consecutive same-boot requirements not waived by one PASS. |
| **I8** long-loop | `S7.6 **Mandatory continuous device session`, `S7.5 **Combined continuity acceptance` | the change's evidence folder | one managed session | The required many-slot continuous sequence, not isolated passes. Last. |

**Playback ordering (authoritative).** `3.15a` one-reader production oracle → `3.15b` single-owner preparation → `3.10` bounded owner service → `3.15c`/`3.15d`/`3.15e` route reader adoption → `3.15f` all-route cutover qualification → `3.7b` legacy emitter removal. Each is a separate packet with its own G2 packet and evidence file. `3.15f` cannot be closed natively: the **I6** hardware batch supplies its device qualification, so I6 runs after the implementation slices without waiting for "all of I5"; `3.7b` runs only after `3.15f` and every supported route pass its gates. Default rolling stays OFF and compatibility emitters stay live until then.

## Verification discipline

Mandatory repository gates: **`pio test -e native` before finishing, push, merge or PR**, and **`pio run -e teensy41-capture-serial` after any firmware change** (`docs/Authority/DELIVERY_RULES.md`). Additionally build `teensy41-capture-serial-window-qualification` when lane rows or a RAM1 guard are in play, and never claim its rows from a default image. Upload only on explicit per-session permission; the dedicated HITL-preset evaluation is the documented exception. Reuse registered stimuli and the canonical managed entry point — list them first with `.venv/bin/python scripts/host_midi_hitl.py stimulus list`; never edit a canonical stimulus. Check the offline verifier on saved evidence before any device contact, pin image/stimulus/target, honour the managed run's hard time bound, and complete canonical Stop/drain before concluding; a failed or missing drain is inconclusive. No blind retries, no harness redesign, no new instrumentation unless the exact required endpoint is genuinely absent — then name it as a host prerequisite rather than inventing a PASS. Native evidence is never promoted to a timing, pixel, wire or deadline claim, and an entry-only or short-loop result never closes a parent. No code is accepted merely because a historical document contains it; no unsupported concurrency claim; repeat, consecutive and same-boot rules are never weakened. One focused independent lifecycle review where the repository requires it — after that, fix concrete findings without a repeated broad review.

## Tracking, handoff, rollback

Track at packet level only: `done` (with exact criterion met), `next` (dependency-ready), `blocked` (exact blocker). Do not mirror the full task inventory. Append the **latest** handoff block below, replacing the previous one rather than accumulating diffs:

```
Packet: <ID + task IDs/short names>
Revision: <branch> @ <short HEAD> (+ dirty-tree note)
Changed: <files + symbols>
Evidence: <paths>
Local: <command> -> <observed>
Device: <none | image/stimulus/target + result>
Disposition: done | next | blocked
Next: <ID + why unblocked>
Blocker: <none | exact>
Open criteria: <what still fails the full criterion>
Do not re-queue: <IDs consumed>
```

**Handoff — the latest block (replaces the previous one; no diff is kept).**

```
Packet: I3a — edge-effect boot qualification on hardware, plus the owner-directed host stimulus fix it needed.
Revision: feature/unified-rolling-playback-path @ e6c1118f plus the preserved dirty tree.
Changed: scripts/hitl/boot_handoff.py (new bounded handoff gate), one call site in scripts/hitl/legacy_record_baseline.py, the proof-race wait + press-length guard in scripts/hitl/edit_mode_precondition.py, and the two matching suites; evidence/docs otherwise. No firmware, marker, stimulus, threshold or SD line.
Evidence: openspec/changes/runtime-working-set-ownership/evidence/2026-10-10-edge-effect-boot/{I3A-QUALIFICATION-PACKET.md,EXECUTION.md}; …/evidence/2026-10-12-base-leg-boot-handoff-gate/EXECUTION.md; captures/i3a-edge-effect-boot-{preflight,postflash,t8s4-01,rerun-t8s4-02,rerun-t8s4-03,t8s4-04}, captures/i3a-latch-discharge-01.
Local: focused 13/13 + 13/13; neighbours 42/42, 16/16, 23/23, 3/3; pio test -e native -> 3,072 cases / 3,065 succeeded / 7 skipped / 0 failed.
Device: owner-authorized flash of e6c1118f / HEX 7678b719…; runs 01-04 on track 8 slot 4 with the registered base stimulus (owner-authorized overwrite of slots 4-5, owner watching the panel). Run 04 (--clock-source host --tempo-bpm 300) is the qualification: fresh crash=0 boot, boot_commit, post-restore census all readiness 1, 0 LoopEnd cancellations, 0 drop warnings, 69 DISP/38 DFRAME, record 1536/1536 exact, two overdubs, undo/redo, canonical complete/drains 2/ok true, no latch; owner: "Normal — no display hang after boot this time". First press 12.05 s against an 11.77 s boot_commit (was ~10 s early). An incomplete-shutdown latch left by run 03 was discharged in the documented order (drains=2, ok true, payload preserved).
Disposition: done — the clause "Qualify repaired edge-effect boot on hardware" is checked on its own criterion, on the current pinned image.
Next: I3b — seeded edit (S7.5b.8b), unchanged order. Separately owed and NOT part of I3a: the stored-span/wrap content class this run surfaced as FAIL (FIRMWARE_CONTRADICTION) — overdub_stored_span_too_short / second_overdub_stored_span_too_short (last=2241, min_span=1296, bars=3/2 in a 1536-tick loop, span_ok=False) — needs its own authorized device leg.
Blocker: none for I3a. Upload/hardware authority remains per session, and the boot-handoff gate amends PX1 under the owner's authority (recorded in response-driven-hitl-acceptance/tasks.md).
Open criteria: S7.5a/S7.5b/S7.5, S7.5b.6 pixel/deadline and display continuity, the stored-span/wrap content verdict, I3b/I4/I5/I6/I7/I8.
Do not re-queue: I1 — completed audit; I2 — numerical declaration and table correction; I3a — qualified on e6c1118f (do not re-run the base stimulus for it, and never convert its content FAIL into an I3a result).
```

Close a box only when the exact full criterion holds; transferred and standing-invariant dispositions stay separate. Update the runtime state/current-work entries, deliverable tracking, the change's `tasks.md`, and any guide describing changed behavior — a behavioral change is not closed while durable docs describe the old behavior. Never reset or roll back the shared dirty tree: rollback is scoped to the current packet's own files, recorded with a pre-slice hash/`git diff` capture, using a guarded baseline, and only after confirming the file list belongs to this slice. Preserve unrelated dirty work. Deliver on the working branch, never directly on the stable branch, and no automatic commit, push or PR. SD destructive operations always need explicit authorization; hardware upload/reset/overwrite authority is per session.

## Open questions (defaults)

S7.5a sign-off before any timing claim — default yes, it blocks timing claims only. Whether I1 may check off — default no, audit-only. Whether I2 precedes I3 — default yes. Whether the archived change needs a later spec sync — default no.
