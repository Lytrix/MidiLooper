# Retiring duplicate timing logging — proposal

**Status:** proposal. Nothing here is removed by `runtime-profiling-layer`; that change only produces
the evidence each retirement needs. Removals land in a follow-up change
(`retire-duplicate-timing-rows`), one stage per commit, each behind its own gate.

## Principle

A legacy row is retired only when **all** of these hold for the image where people read it:

1. **Coverage:** a profile identity measures the same interval (or a strict, stated subset), on the
   same image class. While the profiler is profile-env-only, no default-image row can be removed
   (RED_TEAM R2).
2. **Equivalence:** one device run carries both the legacy row and the profile identity, and the
   numbers agree within a pre-declared band. The legacy row is the reference, never the reverse.
3. **No non-timing consumer:** no HITL rig, parser, test or acceptance gate reads the row for anything
   except its duration. Signals and content fields are kept (class B).
4. **Consumers migrated:** every `scripts/`, `test/` and live `docs/` reference is moved to the profile
   dump or to the kept event; accepted `DECISION_LOG.md` entries stay as written (history), and the
   host parsers keep a legacy decoder so old captures still read.
5. **Memory re-baselined:** removing literals can move DTCM by ±4,096 B; `ram1_budget.json` is updated
   with a note in the same commit.

## Inventory and classification

Rows that time an owner the profiler also times (or would, with one added identity). Counts are
`scripts/*.py` / `test/` / live-docs references at HEAD `5ddba83e`.

| Legacy source | Image | Overlapping identity | Non-timing consumer? | Class | Retirement action |
|---|---|---|---|---|---|
| `Diagnostics::Timing` table (`DIAG_TIMING_RECORD`, 10 ids, 15 sites) → `DIAG,timing` / `DIAG,timing_max` | every `SESSION_CAPTURE` image | `PlaybackWindow_build`, `DisplayBase_gather`, `Display_frame_phase`; display sub-steps need new ids | `test_capture_line_tier` pins a `timing_max` line; docs ×7 | **A** | Map each `Timing` id to a `ProfileId` (append-only), then delete the table, the macro and both rows. u32 µs sums (R9) are a further reason. |
| `DIAG,capture_prewarm_split` (`StopDiagRows.cpp`) | `SESSION_CAPTURE` | `PlaybackPrewarm_slot`, `PlaybackPrewarm_views` | none (docs ×1, DEC-230 history) | **A** with precondition | Add child ids for every reported reserve (`PrewarmStopJobs`, `PrewarmCanonical`, `PrewarmHorizon`, `PrewarmCompanion`, `PrewarmOrder`) or keep the row (R4). `okmask`/`restamp` are content → move to a non-timing event if still read. |
| `DIAG,view_prewarm_step` `us=` field | `SESSION_CAPTURE` | `ViewPrewarm_unit` | **yes** — `ready=1` is the sync signal for `hitl_midifile_chain.py` and `hitl_view_prewarm_active.py` | **B** | Keep the row as an event (`track, slot, unit, ready`); drop only `us=` after equivalence. |
| `HotPathTelemetry` display-update and overdub-start metrics (`PERF,`) | `PERF_TELEMETRY` | `Display_frame_phase`, `OverdubStart_body` | `PERF,` summary text, accepted facility | **A, last** | Its own consolidation change (D1); not before stage 3. |
| `DIAG,idle_turn` (unit > 2 × `kOwnerWorkBudgetUs`) | `SESSION_CAPTURE` | `IdleOwner_unit` ring (ceiling = budget, so a superset of breaches) | tests ×2 (DEC-216 threshold) | **A** | Retire after the profile ring has shown every `idle_turn` breach in one run; threshold semantics move to the decoder (`dur_us ≥ 2 × budget`). |
| `DIAG,persist_save,step` | `SESSION_CAPTURE` | `SaveWorkItem_step` | yes — `budget`, `bar`, `indivisible` are content | **B** | Keep the row; consider dropping `us=` only. |
| `DIAG,snapshot_idle,us=` | investigation gate | `SnapshotPublish_step` | yes — pending/root/revision fields | **B** | Keep; investigation-only. |
| `DIAG,base_build_done` | `SESSION_CAPTURE` | none | outcome/reason fields | **C** | Keep. |
| `DIAG,prepare,slice` | `ARDUINO` | none in the 13 | units/resume/indivisible | **C** | Keep unless a scope is added later. |
| `midi_gap`, `late_on`, `win_gap*`, `loop_rem`, `usb_batch`, `track_update`, `stop_service`, the 5 s family | various | none — these measure **gaps and spans**, not a function | many | **C** | Keep. They are the outcome metrics the profiler explains, not duplicates of it. |

Classes: **A** — pure duration duplicate, retire the row; **B** — row is also a signal or carries
content, keep the row and drop only its duration field; **C** — not a duplicate, keep.

## Stages

**Stage 0 — evidence (inside `runtime-profiling-layer`).** Slice 3 done: `view_prewarm_step us=` equivalent to
`ViewPrewarm_unit` within [0, 50] µs; `capture_prewarm_split` needs per-reserve child ids first; and a direct
row inside a measured body can absorb the core's 120 ms USB transmit timeout at boot (125 ms
`PlaybackPrewarm_slot` breach) — `evidence/EXECUTION_PACKET-slice3.md`. Slice 3/4 device runs capture each class-A/B
row beside its identity; `evidence/<date>-retirement-equivalence.md` records the band per row. No
removal.

**Stage 1 — dedupe the profile image only.** In the follow-up change, compile class-A legacy timers out
when `RUNTIME_PROFILE_SCOPES=1` (`#if !RUNTIME_PROFILE_SCOPES` around `DIAG_TIMING_RECORD` sites and the
`capture_prewarm_split` timing reads). The default image is unchanged; the profile image stops paying
for two timers on the same body (R7). Reversible by flag. Gate: profile-env build + one device run.

**Stage 2 — promote the profiler into the default capture env (TBD-4).** Only if the slice-4 A/B on the
profile image showed no new `win_gap_overrun` and maxima within spread, and RAM1 holds at the approved
figure. Then `teensy41-capture-serial` defines `RUNTIME_PROFILE_SCOPES=1`; `-no-grain` follows the
owner's acceptance-image decision. Gate: both builds, full native, the overhead A/B repeated on the
promoted default image, owner sign-off (it changes the acceptance image).

**Stage 3 — remove class-A rows from all images.** Per row, in this order (cheapest consumer migration
first): `Diagnostics::Timing` + `DIAG,timing*` → `capture_prewarm_split` (after child ids) →
`idle_turn`. Each commit migrates consumers, updates `test_capture_line_tier`/`test_capture_ring` and
the Tier-A lists, re-baselines RAM1, and keeps a legacy decoder in `scripts/profile_report.py`.

**Stage 4 — trim class-B duration fields.** `view_prewarm_step us=`, optionally `persist_save,step us=`.
The rows stay; parsers accept both shapes.

**Stage 5 — `HotPathTelemetry` consolidation.** Its own change per D1, with an exact `PERF,`
equivalence or an accepted format change.

## Gates per removal (checklist)

- [ ] Same-run equivalence recorded, band stated, legacy row as reference.
- [ ] `grep` for the row name across `scripts/`, `test/`, `docs/` (excluding `DECISION_LOG.md` history
      and archives) is empty or migrated.
- [ ] HITL rigs that wait on the row are unaffected (class B keeps the event).
- [ ] `check_marker_contract.py --audit` exit 0; tier suites updated.
- [ ] `check_ram1_budget.py` figure recorded; `ram1_budget.json` note updated if it moved.
- [ ] `pio test -e native` green with recorded counts; one device run on the affected image.
- [ ] `DEC-###` appended naming the retired row, its successor identity and the equivalence evidence.

## Expected payoff

- One authoritative number per measured body instead of up to three.
- Fewer per-call `micros()` pairs on the display and prewarm paths.
- Freed literals and format strings in `DebugSessionCapture.cpp`/`StopDiagRows.cpp` (DTCM headroom
  that past slices fought for).
- A single host decoder for "who owned these milliseconds".
