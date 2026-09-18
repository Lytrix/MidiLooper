# Session closeout — `runtime-code-tidyup`

Run per [`docs/Templates/SESSION_CLOSEOUT.md`](../../../docs/Templates/SESSION_CLOSEOUT.md), because this
change made design decisions (OD-1/OD-2), recorded exclusions (OD-3/OD-4/OD-5) and declined several
candidate refactors. Date: 2026-09-18. Branch: `refactor/runtime-code-tidyup`, stacked on
`feature/resumable-midi-boundary-playback-maintenance` at `4aacd18e`.

## Template checklist

| Template item | Status | Evidence |
|---|---|---|
| Update `PROJECT_STATE.md` and `CURRENT_WORK.md` | **Done** (task 8.1) | `PROJECT_STATE.md` gained a lead entry plus the `runtime-code-tidyup` row in § Active OpenSpec; `CURRENT_WORK.md` § Now implementing records P1–P6 complete and that no other scope is active. Both name the final gates and the relocated journal. |
| Append to `DECISION_LOG.md` per new/changed decision | **Done** | [`DEC-076`](../../../docs/DECISION_LOG.md) (OD-2, snapshot idle/unit probes stay on the broad `SESSION_CAPTURE` gate) and [`DEC-077`](../../../docs/DECISION_LOG.md) (OD-1, `window` is canonical; `RuntimePlaybackView*` grandfathered). The log's stale `Next ID: DEC-076` marker was corrected to `DEC-078`, and DEC-076's owner line was re-pointed at `LoopSnapshotIdleTelemetry.cpp` after P5 moved the code it names. |
| OpenSpec `tasks.md` / `design.md` if the change has a folder | **Done** | Phase outcomes, per-phase gates, the measurement-corrected premises (4.2/4.3) and the P5 rejected splits (design § D8.4) are recorded. |
| `DELIVERABLE_TRACKING.md` if something shipped | **Done** (task 8.2) | A row in § Deliverable Overview and a § Complete-on-branch bullet, both naming the change and its three gates. |

## Decisions this session recorded

| ID | Decision | Where |
|---|---|---|
| **OD-1 → DEC-077** | `window` is canonical for the playback derived representation; the `RuntimePlaybackView*` family is grandfathered and converges on touch. **No rename.** | decision log + NAMING.md clause |
| **OD-2 → DEC-076** | The snapshot idle/unit probe scopes stay on the broad `SESSION_CAPTURE` gate; narrowing the producer to `SESSION_CAPTURE_SNAPSHOT_IDLE` is a follow-up with its own capture evidence. | decision log + in-place comment |
| **OD-3** | Note-side `applySoonestBar` ascending source order is intentional **for now**; the ordering question is a deferred timing-model change, documented in place and not wired. | `src/Loop/LoopSnapshot.cpp` comment |
| **OD-4** | `readLoopSnapshotBodyFormatV1` is gated to `PIO_UNIT_TEST_NATIVE`; the guide now calls it the native-only oracle, not a production load owner. | `StorageLoopIoBodyFormatV1.cpp`, `LOOP_MIDI_STORAGE_AND_VALIDATION.md` |
| **OD-5** | Merge order unresolved by design: task 8.6 asks before opening the PR (default target is the feature branch). | `tasks.md` 8.6 |

## Exclusions and rejected alternatives (actionable reasons)

| Rejected | Reason a future agent can act on |
|---|---|
| One shared `RuntimeWorkerStepResult` for all five step-result enums (task 4.2) | A ~930-site **type-aware** rename: `::Pending`/`::Complete`/`::Denied` are shared with many other enums, so no text sweep is possible. Kept the small win only (`RuntimePlaybackViewBuildGrain` folded into `RuntimePlaybackView::Grain`). |
| Converging `available()`/`idle()`/`active()` onto one admission name (task 4.3) | They are **three different predicates**, not synonyms (`available` = may `begin`, `idle` = the Idle stage only, `active` = a grain in flight). Merging would change behaviour at call sites. Only the genuine defect was fixed: `RuntimeCaptureCompanions::idle()` → `hasCaptureOwner()`. |
| Renaming the `RuntimePlaybackView*` family to `*Window*` | DEC-067's capacity table already names `RuntimePlaybackView.cpp`; a ~25-identifier rename-only sweep is forbidden by NAMING § Migration policy. Recorded as DEC-077 instead. |
| Sweeping the ~16 **pre-existing** `*Kind` enums to `*Type` | Repo-wide rename churn; only the four enums this change added were renamed. |
| Splitting `RuntimeCompanionApply::step()` (259 of its TU's 466 lines) | The batch selectors `step()` drives share its `phase_`/`candidate_`/`cursor_` state; the method is the state machine. See design § D8.4. |
| Splitting `StorageLoopIoBodyFormatV1.cpp` further (1,658 lines) | One wire format over one file-local layout table; splitting must publish or duplicate the table, and duplication is the drift DEC-057 prevents. |
| Splitting `RuntimePlaybackBridgeBuild::step()` (287 of 639 lines) | It advances the same `phase_`/`cursor_`/`order_` state its `begin*`/`rebindBoundary` helpers set up. |
| Further splitting the four `Track*` stop/undo TUs | Already one declaration owner apiece; a new/renamed TU costs a fresh ITCM-bank measurement for no ownership gain. |
| Shipping a derived `.clang-format` | The best of three candidates derived from the incumbent style rewrote **2,274 lines across 45 files**; D9's acceptance bar is "no drift beyond the header/whitespace fixes". `.editorconfig` ships instead; dry-run recorded in [`CLANG-FORMAT-DRYRUN.md`](CLANG-FORMAT-DRYRUN.md). |
| Normalizing trailing whitespace in the branch's Markdown | The 120 Markdown files that carry it use two trailing spaces as **intentional hard line breaks**; `.editorconfig` does not trim `*.md`. The 61 code files carry zero trailing whitespace (the proposal's "24 files" was a false positive from `[ \t]$` matching a literal `t`). |
| Re-pointing all 131 measured duplicated sentences at one owner | Measured duplication was **localised, not spread**: one 262-line section of `CONTINUOUS-CAPTURE-OWNERSHIP.md` held all 197 duplicated long lines and every other section had zero. That block became a citation; the remaining 8 `design.md` ↔ HITL overlaps are one-line design statements load-bearing in both docs. |

## Consequences / follow-ups created

- The base change's `tasks.md` is now a 65-line checklist; its journal lives in `CURRENT_WORK.md` and is
  cited per task. Anyone editing that change must update the relocated entry, not the checklist.
- `DEC-076`'s narrowing (producer onto the `SESSION_CAPTURE_SNAPSHOT_IDLE` gate) remains an open
  follow-up change with its own capture evidence.
- `OD-5` (PR base) is the only unresolved item in this change.
