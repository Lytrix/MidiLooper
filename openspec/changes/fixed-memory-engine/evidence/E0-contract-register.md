# E0 contract register (in progress)

This is the authority for the engine's expected musical behavior. Each clause cites its source. Fixtures cite clauses.
Legacy output is never an expectation; it is a cross-check whose disagreements are classified (§ Cross-check).

Status values: `draft` (written, owner review pending) · `reviewed` (owner accepted) · `question` (owner decision
needed).

## Overlap and occupy (source: `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`, DEC-033, DEC-034)

| Id | Clause | Source | Fixture | Status |
|---|---|---|---|---|
| OVL-0 | Storage geometry: NoteOn at `on`, NoteOff at `off`. A note is present at hold `h` iff `on ≤ h < off`. A wrap-held note (`off < on`) is present iff `h ≥ on` or `h < off`. | Guide § Exclusive end vs storage Off tick; § Lifecycle | all | draft |
| OVL-1 | Later B starts at `h` while earlier A is present and `A.on < h` → A's NoteOff becomes `h − 1` (`loopLength − 1` if `h = 0`). For a wrap-held A, a hold in its head (`0 < h < A.off`, `h ≠ A.on`) also shortens. | Guide lifecycle "A.startTick < holdStart → Shorten A to holdStart − 1"; `computeShortenedEndTick` | `test_ovl1_…`, `test_ovl_wrap_crossing` | draft |
| OVL-2 | Present at hold with `A.on = h` → A hidden. | Guide lifecycle "otherwise → Hide A" | `test_ovl2_…` | draft |
| OVL-3 | A starting strictly inside B's closed span `(B.on, B.off)` → hidden; a wrap-crossing B is two linear segments. | Guide § Lifecycle B Off; classifier `CompleteCover` / `OverlapNoteOn` → Hide | `test_ovl3_…`, `test_ovl_wrap_crossing` | draft |
| OVL-4 | Abutting (`A.off = B.on`) → unchanged. | Guide § Inclusive classifier vs half-open covering (consume gate rejects abut) | `test_ovl4_…` | draft |
| OVL-5 | Channel ignored; pitch alone decides. | DEC-033 | `test_ovl5_…` | draft |
| OVL-6 | A shortened remnant shorter than `noteMinLengthTicks` (8) is hidden when min-length remove is enabled. **Owner decision 2026-11-04:** evaluated on the earlier note's entire surviving duration in unwrapped time, including its pre-wrap portion (Q-OVL-3 still hides at tick 0). | Guide classifier table, min-length remove; owner ruling | `test_ovl6_…`, `test_fixture_min_length_boundaries_7_8_9`, `test_fixture_min_length_across_loop_end` | reviewed (cross-wrap reading) |
| OVL-7 | Outcomes compose monotonically: Hide absorbs; Shorten takes the minimum end and never lengthens. | Guide § Pending overwrite (`upsertSourceTransform`) | `test_ovl7_…` | draft |
| OVL-8 | A later note's effect on earlier passes persists even if that later note is itself hidden by a still later pass (companion rows are sealed per pass). | Guide § Seal: Shorten/Hide → companion EditPass | `test_ovl8_…` | draft |

### Owner semantic defaults (2026-11-04), checked against the sources

| Id | Default | Check against ratified clauses and code | Fixture | Status |
|---|---|---|---|---|
| Q-OVL-1 | **Confirmed by owner.** Within one pass, notes keep their NoteId and NoteOn/NoteOff pairing; occlusion is later vs earlier pass only. | Consistent: Record runs no resolver; overdub candidates come from the ledger and source view of earlier passes. | `test_qovl1_…` | draft |
| Q-OVL-2 | **Confirmed by owner.** Matching ignores channel (DEC-033); emission and NoteOff attribution keep original channels. | **Contradiction with legacy code:** `collectOverdubNoteOnParticipantIds` filters ledger candidates by channel, and `resolveOccupancyAtNoteOn` skips the channel-free geometric lookup when the ledger set is complete. A same-pitch note on another channel therefore escapes shortening whenever a same-channel note is sounding. DEC-033 and the default both say it is shortened → **HIB-9** (code reading; cross-check run pending). | `test_qovl2_…` | draft |
| Q-OVL-3 | **Owner ruling: the ratified rule stands.** A hold at tick 0 in a wrap-held note's head hides it (`holdPhaseTick > 0` required for shorten). | Guide lifecycle and `resolveOccupancyAtNoteOn` agree with the ruling. Keeping the pre-wrap portion is recorded as optional amendment **AMD-1** (needs owner approval), not as a defect. | `test_qovl3_hold_at_zero_hides_wrap_held_note` | draft |

## Note length

| Id | Clause | Source | Fixture | Status |
|---|---|---|---|---|
| LEN-1 | A note held for at least one loop length L replays with effective end min(recorded end, S + L − 1). Its NoteOn repeats every loop, each iteration ends one tick before its successor, the original late NoteOff is never sent against a newer iteration, and there is no clipping at tick 0. Live holds sound until the real NoteOff. | owner decision 2026-11-04 (Q-LEN-1) | `test_qlen1_*` | reviewed |
| OVL-9 (was Q-OVL-4) | **Ratified by owner 2026-11-04.** Overlap applies to effective note instances across cycles: an earlier-pass note whose NoteOn lies inside a later-pass note's effective span is hidden, also when a previous repetition of it was sounding when the later note began. Overlap at B's NoteOn is no permission to skip A's later NoteOns. An A that starts outside B is shortened by B's next start. | owner decision 2026-11-04 | `test_qovl4_case1_*`, `test_qovl4_case2_*` (incl. adoption), random comparison | reviewed |

## Edits

| Id | Clause | Source | Fixture | Status |
|---|---|---|---|---|
| EDIT-0 | An edit run's HideNote hides every row of that noteId in earlier runs; a replacement keeps the noteId (lineage). | design D4, DEC-245 | `test_edit_*` | draft |
| EDIT-1 | **Ratified by owner 2026-11-04.** An edit run is not a derived occluder (NOTE_EDIT writes its overlap results explicitly); later overdubs occlude edited notes; an edit-hidden later note still occludes earlier passes (OVL-8); a later edit of a NoteId replaces the earlier replacement. | DEC-029/030/034 reading; owner decision 2026-11-04 | `test_edit_run_is_not_a_derived_occluder`, `test_edit_hidden_later_note_still_occludes_earlier_pass`, `test_edit1_two_successive_edits_and_unrelated_same_pitch_note` | reviewed |

## Optional amendments (not adopted; owner approval needed)

| Id | Proposal | Status |
|---|---|---|
| AMD-1 | Hold at tick 0 inside a wrap-held note's head shortens it to `loopLength − 1` (keeps the pre-wrap portion) instead of hiding it. `computeShortenedEndTick` already maps 0 that way. | proposed only |

## Pairing

| Id | Clause | Source | Fixture | Status |
|---|---|---|---|---|
| PAIR-1 | A NoteOff pairs with the most recent open NoteOn on the same (channel, pitch) lane in the same pass (LIFO); at an equal tick NoteOff before NoteOn. | production `findNoteOffForOnIndex` as documented in `RevisionImport.h` (`LanePairer`) | `test_content_engine_writer` | draft |

## Order (source: DEC-244)

| Id | Clause | Fixture | Status |
|---|---|---|---|
| ORD-1 | At one tick: NoteOff (incl. NoteOn velocity 0) < NoteOn < other; then pass commit order; then append order. | `test_dec244_…` | draft |

## Still to be registered (E0 backlog)

- Record / overdub Stop closure and held-leg terminals: DEC-136, 137, 138, 144.
- Ledger closure: DEC-148.
- Wrap: DEC-051, DEC-069.
- Interval ownership: DEC-068, DEC-160.
- Interactions: DEC-078, DEC-096, DEC-233.
- Undo: DEC-038, DEC-088, DEC-095.
- Handoff: DEC-159.
- C1 owed pitches: DEC-250 (CE-E4).
- Controller closures at Stop (plan § 12.3 item 9).
- Slot-switch timing: DEC-102, DEC-106, musical part only.
- Classification of all 253 DECs as musical or mechanism.

## Historical incorrect behavior (legacy is wrong here; fixtures assert the clause, not legacy)

| Id | Behavior | Evidence |
|---|---|---|
| HIB-1 | Committed revision 7 served by a 0-event merged window while passes held 98–101 rows | revision-manifest-prototype proposal (2026-11-01 overdub-audibility) |
| HIB-2 | Forced C3.4 deadline answers that missed the interval's own pending rows | DEC-250 evidence |
| HIB-3 | Companion dropped under heap-tail pressure | `MEMORY_PRESSURE_RED` suite in `test_pending_note_change` |
| HIB-4 | "companion path skipped (canonical derivation unavailable); raw pass only": the stop's companions are lost | `captures/session_20261104_190805.log` at 208.7 s |
| HIB-5 | Stop held in `AtomicTransfer` for 176 s under exhaustion | same log |
| HIB-6 | Piano roll `win=0` for 55 beats; 186 `display_projection` refusals | same log |
| HIB-7 | Within-class equal-tick order unspecified where `std::sort` is used | revision-manifest B.2a |
| HIB-9 | Same-pitch note on another channel not shortened when a same-channel note is also sounding at the hold | `collectOverdubNoteOnParticipantIds` channel filter + skipped geometric fallback; Q-OVL-2 |

## Cross-check

Not run yet. The legacy native model will be run on the same scenarios once the harness for overdub overlap
(`test_pending_note_change`, `test_continuous_capture`) can be fed fixture timelines. Each disagreement is classified as
HIB, a fixture error, or an owner question.

## Review

Every row is `draft` until the owner reviews it. The fixtures are executable in
`test/test_engine_semantic_oracle/` (15 cases, all passing against the reference model).
