## Context

See `proposal.md` — Why. The design-relevant state:

**Tree and base.** The tidied code exists **only** on `feature/resumable-midi-boundary-playback-maintenance`
(636 commits ahead of `dev`; `include/LoopSnapshot.h`, `src/Track/RuntimePlaybackView.cpp` and
`include/RuntimeCompanionApply.h` are all absent from `dev`). This change therefore stacks on that
branch at `4aacd18e` on `refactor/runtime-code-tidyup`; a `dev`-based branch would not contain the
files. Merge order is OD-5 in `proposal.md`.

**Measured baselines at `4aacd18e`** (re-measure before P1; all gates compare against these):

| Gate | Value |
|------|-------|
| `pio test -e native` | 2,262 cases, 1 skipped, 2,261 passed (72 s) |
| `pio run -e teensy41-capture-serial` | SUCCESS — RAM1 `variables 96128 / code 392344 / padding 872 / free for local variables 34944` |
| `openspec validate runtime-code-tidyup --strict` | valid |

**Placement constraint (drives P5).** `linker/imxrt1062_t41_lcr.ld` puts *all* `*(.text*)` into
`.text.itcm` **except** an explicit per-object allow-list in `.text.code` (`*(.flashmem*)`,
`*LoopContentResolution*.o`, `*LoopOverdubSession*.o`, `*LoopSnapshot*.o`, `*TrackOverdubWrap*.o`,
`*TrackUndo*`, and the deferred-maintenance diagnostics). Consequence: a **new** translation unit whose
name matches none of those patterns lands its code in ITCM/RAM1 unless every function carries a
`FLASHMEM`-expanding marker. `_itcm_block_count` is derived from `.text.itcm` size in 32 KB banks, so
crossing a bank boundary costs 32 KB of local-variable headroom. This is why P5 is per-split gated and
why the naming of any new TU is a design decision, not a cosmetic one.

**Authority constraints that bound the approach.** `docs/Authority/NAMING.md`: rule 3 (no new
top-level domain nouns without a `DEC`), rule 5 (`view` is on the avoid-list unless already
established), rule 6 (`Type` preferred over `Kind`), and the ban on repository-wide rename-only
commits — so naming convergence is **touch-and-strengthen**, never a sweep. `AGENTS.md`: a
behavioral change is not closed while durable docs describe the old behavior; docs are
"cite, do not duplicate". Architecture: `ARCHITECTURE_REASSESSMENT.md` lists "Refactor with no
ownership movement" as explicitly **not** a formal trigger, so this change carries lightweight
preflight only — and the plan deliberately extends existing owners instead of adding one.

**Scope precondition.** `docs/Runtime/CURRENT_WORK.md` `## Now implementing` is currently **empty**.
`AGENTS.md` forbids implementing outside that section, so P0 updates it before any code edit.

## Goals / Non-Goals

**Goals**

- Every phase is provably behavior-preserving: the native suite is the gate, and any phase that
  cannot show equivalence is dropped rather than handed to HITL.
- Net-negative code: P1 removes more than P2–P4 add. Dead weight is measured in removed symbols and
  removed RAM1/resident bytes, not in line count.
- One concept, one name: one step-result vocabulary, one admission-state accessor name, one spelling
  of each shared idiom, each living on the owner that already exists for it.
- Structure changes are justified by ownership seams and paid for with a measured RAM1/flash check.

**Non-Goals**

- No new owner, no new top-level domain noun, no ownership transfer — this change must not trip a
  formal reassessment trigger.
- No change to any persisted/wire format, capacity, bound value, denial-reason value, gate, or
  ordering outcome.
- No repository-wide rename; no reformatting of files this change does not otherwise touch.
- No redesign of the resumable stage machines' *stage sets* — thirteen workers legitimately have
  thirteen different stage sequences. Only their **result vocabulary**, **accessor names** and shared
  **idioms** converge.
- No touching of the documented-open synchronous playback gather fallback (OpenSpec task 2.4 / HITL 5.3).

## Decisions

### D1 — Phase the work by risk, gate each phase independently

P1 dead code → P2 constants/enums → P3 de-duplication → P4 naming → P5 structural splits → P6
formatting/docs. P1 and P2 are pure subtraction and name-introduction; P3 changes shared call paths
and is verified by the existing differential/oracle fixtures; P5 is the only phase that can move code
between sections and is gated on a firmware size measurement per split; P6 is text-only.

*Alternative considered:* one "big tidy" commit. Rejected: a 39k-line branch needs bisectable,
individually revertable steps, and P1 must be mergeable even if P5 is abandoned.

### D2 — Delete by default; gate only the diagnostics that tests consume

Default action for an unreferenced symbol is deletion, not a comment or a `#if`. Five branch-added
diagnostics are referenced **only by native tests** (`RuntimeCanonicalPassInputs::borrowsCaptureOrder`,
`hasFrozenBinding`, `RuntimeCompanionBridgeMap::lastStepInputCount`, `RuntimeCompanionOverlapWalk::lastStepInputCount`,
`RuntimeCaptureCompanions::capacity`/`row`). Rule: if a test asserts a *contract* through the accessor,
keep it and mark it as test surface; if it asserts an implementation detail, re-point the test at
`data()`/`size()` and delete the accessor. Per item, in P1, with the test edit in the same commit.

*Alternative considered:* delete all five and rewrite their five test files. Rejected as scope creep:
some of those assertions are the only evidence for grain-bound contracts.

### D3 — Shared helpers extend an existing owner; nothing new is invented

| Duplicated idiom | Existing owner it extends |
|---|---|
| External-first `alloc`/`free` (16 sites) | `include/Utils/InternalHeapFirstAllocator.h` (has `isInExternalMemoryPool`, no raw-bytes pair) |
| `eventKind` ordering key (8 sites) | `include/MidiEvent.h` (`isNoteOn`/`isNoteOff` already live here) |
| Merge order key (compile + consume) | new **header-inline** `include/Utils/PlaybackEventOrder.h` — one concept with no current owner |
| Ledger `Entry` equality (4 sites) | `include/ActiveNoteLedger.h` (add `activeNoteEntriesMatch`) |
| Undo-trim predicate (2 owners) | `include/PassReclaim.h` (already declares `trimGlobalUndoStackForMemory`, `overUndoMemoryPressure`) |
| Ceiling-grain idiom (9 sites + 2 local lambdas) | `include/RuntimePlaybackOrderBuilder.h` (already owns the grain-bound vocabulary) |
| Unique-id append/contains, box factory, bar-count ceil-div | existing Loop-internal headers |

Every helper is `inline`/`constexpr` or already-`FLASHMEM`, so no new out-of-line symbol and no
placement change. No `*Helper`/`*Util` type, no `Service`/`Facade` noun (NAMING module-suffix table).

### D4 — One result vocabulary; stage enums stay per-worker

Five public result enums name the same triad with five names
(`RuntimeCompanionOverlapResult`, `RuntimeCompanionSpanResult`, `RuntimePlaybackOrderBuildResult`,
`RuntimePlaybackViewFillResult`, `RuntimePlaybackViewBuildGrain`) while
`include/StorageLoopIo.h` already establishes `…StepResult { MoreWork, Completed, Failed }`.
Decision: introduce one shared `RuntimeWorkerStepResult` for the pure triads, and rename the two that
genuinely carry an extra state to `…StepResult { MoreWork, Ready, Completed, Failed }` — preserving
the extra state rather than flattening it. `RuntimePlaybackViewBuildGrain` is folded away entirely:
its `{Fill, Order, …}` is the same information as the class's own `Phase` and the `nextGrain()`
accessor, so the two enums collapse to one. `Phase` enums remain per-worker with a common shape
(`Idle` first, terminal state last) because their stage sets are genuinely different.

*Alternatives considered:* one union enum with all stages (loses type safety and makes illegal states
representable); rename only, keeping five names (does not fix the drift).

### D5 — One accessor name per lifecycle concept

Admission state: `available()` (currently `available()` ×4, `idle()` ×3, `active()` ×3). Grain
progress: `lastStepInputCount()` (currently that plus bespoke `visited_`/`lastStepInputs_` accessors).
Source identity: `matchesSource(...)`/`ready(...)` reduced to one shape. `active()` as "is a grain in
flight" and `available()` as "may `begin()` be called" are different questions: keep at most one of
each, and document which.

### D6 — Undo-trim: extract the predicate, do **not** consolidate the cursor branch

`TrackUndoRetirement.cpp:85-86` and `PassReclaim.cpp:84-95` express the same
`absolute-rail || (depth > min && pressure)` rule. The review checked the feared `cursor == 0`
divergence and found the two agree on every reachable case today, so P3 extracts one inline predicate
and adds a comment recording the equivalence and the `cursor == 0` reasoning. Consolidating the
branches themselves would be a policy change to `undo-memory-trim` / DEC-063 — out of scope.

### D7 — Naming convergence is recorded first, applied on touch

P4 lands the `DEC` entries and the NAMING clause **before** any rename, then renames only inside files
P1–P3 already modify (`normalized*` → `merged*`, `*Kind` → `*Type`, abbreviations, the "lane" comment).
`view`/`window` is decided (OD-1) but **not** renamed: the `RuntimePlaybackView*` family is already
referenced by a DEC capacity table, so a rename would be exactly the repo-wide rename-only churn the
authority forbids.

### D8 — P5 split rules

Three seams are in scope, chosen because each separates a genuine ownership boundary:
`CaptureAdmissionStaging` out of `RuntimePlaybackView.{h,cpp}`; the read side
(`RuntimePlaybackBridgeCursor`/`Stream`) out of `RuntimePlaybackBridge.cpp`; the `SESSION_CAPTURE`
idle-telemetry block out of `LoopSnapshotPublish.cpp`. Rules for every split:

1. No data member moves between owners and no `pendingSnapshotPublishBox_`-style internals cross a TU.
2. Every moved function keeps its existing `*_MEM` marker. New TUs must either adopt a name matching
   the linker allow-list (`*LoopSnapshot*`, `*TrackUndo*`, …) or carry an explicit marker on **every**
   function; P5 states which, per split, before editing.
3. Accept only if the firmware link succeeds and the RAM1 `.text.itcm`/`_itcm_block_count` result is
   unchanged or better. If a split crosses a bank, revert that split and record it as rejected.
4. Splits with no clean seam stay intact: `RuntimeCompanionApply::step()` (one state machine),
   `StorageLoopIoBodyFormatV1.cpp` (one wire format + file-local layout table),
   `RuntimePlaybackBridgeBuild::step()`, and the four `Track*` stop/undo TUs (declaration/owner
   boundaries). D8.4 records the measurement behind each refusal.

#### D8.4 — Splits considered and rejected (task 6.4)

Every item below was declined on its seam, never on its size, and none is a deferred "later" item: each
names the state or format owner that already spans the proposed cut. No split in this change was done for
line count either — 6.1–6.3 each separate a responsibility, and the line reductions they happen to produce
are a consequence.

| Candidate | Measurement | Why the seam is not real |
|---|---|---|
| `RuntimeCompanionApply::step()` out of `RuntimeCompanionApply.cpp` | `step()` is lines 208–466 of a 466-line TU — 259 of its 466 lines | The other 206 lines are the batch selectors `step()` drives (`selectBatchRow`, `startBatchRow`, `beginHistoryBatch`, `resumeOverlaps`, the grain estimates) and they all read and write the same `phase_`/`candidate_`/`cursor_` state. A split would put one state machine's per-turn transition in a different TU from its transition targets. |
| `StorageLoopIoBodyFormatV1.cpp` split further | 1,658 lines: five file-local `Lsv1*` gather structs, `LoopSnapshotBodyFormatV1SaveWriteState`/`…SaveScratch`, the `kLsv1*` layout constants (including the `kLsv1MaxMembershipRefsPerBar == kSnapshotMembershipMaxRefsPerBar` `static_assert`), and the `BufferReader` | One wire format, written and read through the same file-local layout table; the gather structs are the shared vocabulary of both directions. A split must publish that table or duplicate it, and duplicating a wire layout is the drift DEC-057 exists to prevent. |
| `RuntimePlaybackBridgeBuild::step()` out of `RuntimePlaybackBridge.cpp` | `step()` is lines 352–638 of the 639-line compile TU — 287 lines | `step()` advances the same `phase_`/`cursor_`/`order_`/`bridge_` state that `begin`, `beginSuccessor`, `beginInitialWindow`, `rebindBoundary`, `touchesOpenNote`, `insertionMatches` and `effectiveMask` set up and inspect. The method *is* the state machine rather than a helper beside one. |
| The four `Track*` stop/undo TUs — `TrackPreparedStopCommit.cpp` 395, `TrackCaptureStopCommit.cpp` 604, `TrackUndoPublication.cpp` 159, `TrackUndoRetirement.cpp` 123 | One declaration owner each; they exist because of an earlier split | Their boundary already is the declaration/owner boundary. Splitting further separates the one TU that implements a declared owner from the header that declares it, which is the opposite of NAMING's file-per-owner rule, and (for a new or renamed TU) costs a fresh ITCM-bank measurement for no ownership gain. `RuntimeNoteDeleteLookup`/`RuntimeNotePairLookup` in P4 is the same refusal for the same reason. |

The three in-scope splits passed the D8.3 bank criterion: `_itcm_block_count` stayed **12** and `free for
local variables` stayed **34,976** after each of 6.1, 6.2 and 6.3 (tasks.md § P5 progress).

### D9 — Formatting becomes enforceable without a mass reformat

P6 adds `.editorconfig` (indent/EOF/whitespace) plus a `.clang-format` **derived from the existing
dominant style**, not from an LLVM default. Acceptance gate: `clang-format --dry-run` over the added
files shows only the drifts this change intends (copyright header, trailing whitespace). If the config
cannot reproduce the incumbent style, ship `.editorconfig` only and record why — a reformatting diff
across 56 files would defeat the review.

### D10 — Verification strategy per phase

| Phase | Gate |
|---|---|
| P1 | `grep` proof of zero references per deleted symbol; native suite; firmware build (removed `RAM1 code` is a bonus, not a requirement) |
| P2 | native suite; `static_assert` compiles; diff shows no changed literal value |
| P3 | native suite **plus** the existing differential/oracle fixtures (`test_playback_prewarm`, `test_continuous_capture`, `test_loop_take_survival`, `test_storage_loop_io`) — byte-identical traces/inventories |
| P4 | native suite; `grep` shows the old name gone from the touched files only |
| P5 | native suite + firmware build with the RAM1/`_itcm_block_count` comparison per split |
| P6 | text-only; `openspec validate --strict`; no firmware or test run needed |

No HITL run is required by any phase, because no phase changes observable device behavior. If a phase
turns out to need one to prove equivalence, that is evidence the phase is not behavior-preserving and
it is dropped.

## Risks / Trade-offs

- **[A symbol deleted that only a test needs]** → D2's per-item rule; the test edit lands in the same
  commit; a link error is treated as a finding, not a nuisance.
- **[P5 crosses an ITCM bank and silently costs 32 KB of stack headroom]** → per-split firmware size
  comparison with an explicit revert criterion (D8.3).
- **[A "de-duplication" silently changes an upper bound or a denial reason]** → grain/slack constants
  are **named only, never re-valued**; atomic-preflight bit values stay stable per group; the merge
  comparator and ledger equality are extracted verbatim.
- **[`.clang-format` triggers a 56-file reformat]** → D9's dry-run acceptance gate, `.editorconfig`
  fallback.
- **[Removing the inert `identityPreserved` computation is on a timing path]** → it is provably inert
  (computed from just-assigned values, then `(void)`-cast); removal can only reduce work; covered by
  the take-survival and continuous-capture suites.
- **[The stacked branch diverges when the feature branch moves]** → rebase `refactor/runtime-code-tidyup`
  onto the feature branch tip at the start of each phase; P0 records the base commit in `tasks.md`.
- **[Scope creep into correctness work]** → the four deferred items (OD-2/OD-3, the hot-path gather,
  the `StorageManager` uncalled statics) are Non-goals in `proposal.md` and carry their own follow-up.

## Migration Plan

No data, format or protocol migration: nothing persisted, transmitted or parsed changes. Rollout is
one commit per phase on `refactor/runtime-code-tidyup`, in P0→P6 order, each with its gate from D10.
Rollback is `git revert` of the phase commit; phases are ordered so that reverting P5 or P6 leaves
P1–P4 coherent. No compatibility-removal trigger is required (`ARCHITECTURE_RULES` compatibility
removal applies to persisted/wire surfaces, which are untouched). Closeout updates
`docs/Runtime/CURRENT_WORK.md`, `docs/Runtime/PROJECT_STATE.md` and
`docs/DELIVERABLE_TRACKING.md`, per `DOCUMENTATION_CLOSEOUT`.

## Open Questions

Deferrable without changing the task breakdown; each is OD-* in `proposal.md` with a stated default:

- **OD-1** `view` vs `window` as the canonical term — recorded in P4; no rename either way in this change.
- **OD-2** whether `snapshot_unit`/snapshot-idle instrumentation belongs on the broad `SESSION_CAPTURE`
  gate — recorded in P2; any narrowing is a follow-up change.
- **OD-3** whether note-side `applySoonestBar` should order by playhead — recorded in P4 as a comment;
  a behavior change would need its own change and HITL evidence.
- **OD-4** delete the test-only `readLoopSnapshotBodyFormatV1` or gate it to `PIO_UNIT_TEST_NATIVE` —
  P1 takes the gating default unless told otherwise.
- **OD-5** whether the tidy-up PR targets the feature branch or waits for `dev` — a process choice that
  does not affect the tasks.
