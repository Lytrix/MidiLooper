# Preflight — runtime-code-tidyup (lightweight)

Per [`OpenSpec-Workflow.mdc`](../../../../.cursor/rules/OpenSpec-Workflow.mdc) § Preflight and
[`ARCHITECTURE_REASSESSMENT.md`](../../../../docs/ARCHITECTURE_REASSESSMENT.md): a formal reassessment
is required only when a formal trigger fires. This file records the lightweight preflight for this
change.

## Formal trigger assessment

**Verdict: no formal trigger fires. No design review required.** Evidence against each trigger, with
the owner/change/test summary the rule asks for.

| Trigger | Fires? | Evidence |
|---|---|---|
| New owner introduced | No | Every shared helper extends an owner that already exists (`include/Utils/InternalHeapFirstAllocator.h`, `include/MidiEvent.h`, `include/ActiveNoteLedger.h`, `include/PassReclaim.h`, `include/RuntimePlaybackOrderBuilder.h`) — design § D3. No `*Manager`, no new top-level noun (NAMING rule 3). |
| Ownership transfer required | No | No logic moves between modules. `src/Track/TrackUndoRetirement.cpp` and `src/StorageLoopIoBodyFormatV1.cpp` keep their responsibilities; no `pendingSnapshotPublishBox_`-class internal crosses a TU (design § D8.1). |
| Persistent state model changes | No | One vestigial writer field cluster is deleted from a **transient save state** (never persisted); no canonical model changes. |
| Storage schema changes | No | DEC-057 LSV1 is untouched byte-wise: only write-only fields are removed, so the emitted byte stream is identical and the `test_storage_loop_io` round-trip oracle is the proof. |
| >3 files changed AND ownership crossed | No | Files change in many phases, but no responsibility shifts between owners — design § D8.4 lists the four TUs explicitly left intact. |
| Undo semantics change | No | No new undo kind, no `handleUndo` reorder, no snapshot shape change. `DEC-063` trim **values** (`MIN_UNDO_DEPTH = 8`, `ABSOLUTE_MAX_UNDO_ENTRIES = 512`, `include/Globals.h:86,88`) are unchanged; only the duplicated predicate is extracted (design § D6). |
| Timing model changes | No | The one ordering question found (note-side `applySoonestBar` ignoring `playheadPhaseTick`) is deferred, not changed — `proposal.md` OD-3, `tasks.md` 5.6. |
| OpenSpec conflicts architecture | No | `.openspec.yaml` sets `skip_specs: true`: this change declares no behavior requirement, so there is nothing to conflict. |
| No reusable extension point found | No | Extension points were found for every de-duplication (design § D3); none required a new owner. |

The `ARCHITECTURE_REASSESSMENT.md` § "Explicitly not triggers" table names this class directly:
*"Refactor with no ownership movement — Rename, extract private function same module."*

## Owner / change / test summary

| Field | Value |
|---|---|
| **Change kind** | Behavior-preserving refactor (dead code, duplication, naming, constants, file seams, formatting) |
| **Owners touched** | `Loop` / `LoopSnapshot` / `LoopPasses`, `Track` playback+stop+undo commit path, `RuntimePlayback*` and `RuntimeCompanion*` resumable workers, `StorageLoopIo` LSV1 save/load state, `StorageManager` admission surface, `Utils` diagnostics |
| **Ownership movement** | None |
| **Public API changes** | Removals only, each with a zero-reference proof (tasks 2.1–2.15); no new public type except header-inline helpers on existing owners |
| **Test strategy** | `pio test -e native` per phase; differential/oracle fixtures byte-identical for P3 (`test_playback_prewarm`, `test_continuous_capture`, `test_loop_take_survival`, `test_storage_loop_io`); firmware build + RAM1 comparison per split in P5 |
| **HITL** | Not required — see below |

## Decision-log constraints (task 1.1)

Searched with the `grep` tool, never a full read. Each entry is what it constrains in this change.

| Decision | Constraint on this change | Phase |
|---|---|---|
| [`DEC-072`](../../../../docs/DECISION_LOG.md) (line 410) — *A staged capture pass is owned by its capture interval, not by the Loop producer slot* | The `staged` / `pendingPass` receipt idiom must keep its "never consult the Loop producer slot on the deferred path" meaning when collapsed (tasks 4.4, 4.13). Ownership is not moved. | P3 |
| [`DEC-068`](../../../../docs/DECISION_LOG.md) (line 853) — *An overdub session belongs to one capture interval* | `TrackPreparedStopCommit::completeDeferredCleanup` keeps its `ownsLiveSession` gate verbatim; only the inert `identityPreserved` block is removed (task 2.7). | P1 |
| [`DEC-067`](../../../../docs/DECISION_LOG.md) (line 939) — *Finalization-resource admission is stop-directed* | `advanceAtomicTransfer` keeps its exact admission predicate when the two bodies collapse; denial-reason bit values stay stable per group (tasks 2.12, 3.4, 4.4). | P1–P3 |
| [`DEC-063`](../../../../docs/DECISION_LOG.md) (line 1243) — *Protected stop undo publication and bounded reclamation* + [`ExecutionBounds.md`](../../../../docs/Authority/Architecture/ExecutionBounds.md) § DEC-063 retirement helpers | `UndoEntryReleaseCursor` / `GlobalUndoStackTrimCursor` keep their per-unit bounded behaviour and survivor moves; the `cursor == 0` branch keeps its current shape (task 4.11). Cursor `phase_` numbers become named enumerators with **unchanged values** (task 3.5). | P2–P3 |
| [`DEC-057`](../../../../docs/DECISION_LOG.md) (line 1449) — *LoopSnapshot body format version 1 (LSV1) dual-write* | The LSV1 save state may lose vestigial fields, but the emitted byte stream must not change; `test_storage_loop_io`'s round-trip is the gate (tasks 2.9, 2.10, 2.11). | P1 |
| [`DECISION_LOG`](../../../../docs/DECISION_LOG.md) line 1844 — capture-gated acceptance requires `SESSION_CAPTURE_SNAPSHOT_IDLE=1` | The instrumentation-gating observation is **recorded only** (task 3.6); no `#if` changes in this change, so no capture-gated acceptance evidence is invalidated. | P2 |

No `DEC-###` entry yet exists for `view` vs `window`, for the `snapshot_unit` gate, or for note-side
`applySoonestBar` ordering — those are the three decisions this change records rather than assumes
(`proposal.md` OD-1/OD-2/OD-3).

## Verification gates (task 1.2)

[`DELIVERY_RULES.md`](../../../../docs/Authority/DELIVERY_RULES.md) § Verification gates (lines 127–147)
makes exactly two gates unconditional, and qualifies every HITL row by **behavior coupling**:

| Matrix row | Applies to this change? |
|---|---|
| `Native unit tests — Before push, merge, or PR` → `pio test -e native` | **Yes — mandatory**, every phase. |
| `Firmware build — After firmware changes` → `pio run -e teensy41-capture-serial` | **Yes — mandatory**, P1–P5. |
| `Teensy upload — Only after user confirms` | Not performed unless the user asks. |
| `HITL baseline — Record/overdub/undo regressions` | No: no regression is claimed or expected; native byte-identity is the evidence. |
| `HITL edit — Note edit / overlap regressions` | No: no note-edit or overlap behavior is touched. |
| `Timeline / capture / undo` domain → native + HITL baseline | Native satisfied; HITL keyed to regressions, of which there are none. |
| `Persistence / SD / overlay` → *deferred save HITL **when touching save path*** | **Nominally triggered** — task 2.9 edits `src/StorageLoopIoBodyFormatV1.cpp`. See the note below. |
| `Display-only` → *HITL when behavior couples to record/edit timing* | No: no display behavior changes. |

**Save-path note (surfaced, not silently waived).** Task 2.9 removes fields from the LSV1 *save state*
struct, which is the save path. The fields removed are write-only or only read by a `SESSION_CAPTURE`-
gated signature hash, so the emitted byte stream is unchanged; the equality evidence is the
`test_storage_loop_io` round-trip oracle plus the fact that no writer expression changes. If the user
prefers a deferred-save HITL run on the device for that task despite the byte-identity argument, it can
be added as a stimulus-backed run — `design.md` § D10 currently concludes it is not needed.

**HITL determination:** no phase changes observable device behavior, so no registered stimulus is
required. `AGENTS.md` requires a stimulus for a firmware change *observable over serial*; none of these
are. If a phase cannot demonstrate equivalence natively, the phase is dropped rather than HITL-verified
(`design.md` § D10).

## Baselines (task 1.4)

Measured at change commit `7eb0d7f0` on `refactor/runtime-code-tidyup`, stacked on
`feature/resumable-midi-boundary-playback-maintenance` at `4aacd18e`. All gates compare against these.

| Gate | Baseline |
|---|---|
| `pio test -e native` | **2,262 cases, 1 skipped, 2,261 succeeded** in 00:01:12 |
| `pio run -e teensy41-capture-serial` | SUCCESS — RAM1 `variables:96128, code:392344, padding:872, free for local variables:34944`; FLASH `code:1009608, data:90492, headers:8888` |
| `openspec validate runtime-code-tidyup --strict` | valid (`skip_specs` acknowledged) |
| Linker budget | `.text.itcm` derived `_itcm_block_count`; one bank = 32 KB of local-variable headroom (`linker/imxrt1062_t41_lcr.ld:103-105,165-167`) |

Baselines are re-measured and recorded per phase in the phase commit message; the final values land in
task 8.5.
