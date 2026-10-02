## Why

Active playback's authority is still the loop's whole published root. The merged playback window is gathered from
`Loop::publishedSnapshot()` in `src/Track/TrackPlaybackWindowBuild.cpp`: whole-loop for a short loop, and a two-bar
window centred on the playhead for a loop past the demotion bound (`Loop::shouldAvoidFullVisualRebuild` — longer than
`DisplayWindowUtils::kMaxDetailedWindowBars` (16) bars) — but the root itself stays resident, because the gather *and*
the undo/save checkpoint authority both read it. The memory law's per-slot playback term therefore prices the whole
root, and `runtime-working-set-ownership` (RWO) has measured what that costs: the conservative class sum does not fit
at any N (**12,626 KiB** against the **8,192 KiB** pool), N is stuck at today's **8**, and the owner's
64-slot / 64-bar / 3-notes-per-16th requirement fails by **6–7×** with *windowed publication* named as one of four
levers that must all land (`runtime-working-set-ownership/evidence/2026-10-02-64x64x48-capacity/ANALYSIS.md`). At the *derived* window size the same
targets cost 447/894/1,340/1,787 KiB, so the playback term stops being binding.

RWO extracted this work at Revision 18 §3 (`design.md:812`) as **phase F**, kept its own lease derivation (S2.1) and
`N` (S2.3) **provisional, bounded by phase F**, and closed its S4c gate as **contract only**. So the acceptance
criteria it wrote (`S4C1` AC-1…AC-9) and the fixture list it wrote (`S4C45` F-1…F-8) currently belong to a sibling
change that does not exist, and RWO's own close-out (S7/archive) and `N` cannot move without it. This proposal creates
that sibling.

**Why now:** the contract is written and the machinery is inventoried (`S4C0`), so nothing waits on discovery. RWO is
explicit that the sibling carries **its own DEC entry and architecture reassessment before Apply**
(`IMPLEMENTATION_CONTRACT.md` §4; `design.md:812`) — neither exists yet, and this change is **planning only**.

## What Changes

- **Active playback is served from an immutable window**, identified `(slot, revision, interval)`, instead of from
  whole-loop publication. A whole-loop publication stays *sufficient* but is never asserted as the unit.
- **Readiness is owed per affected tick, against a deadline** — the measured ~16–31 ms jammed-switch figure at
  1/16–1/32-beat quantisation (`INTERNAL_PPQN` = 192) — and is a predicate whose failure **names the holder** that
  owes it.
- **Currency has one definition**: the window's revision against `playbackRevision`, using the existing
  `Loop::publishedSnapshotIsCurrent()` / `builtFromRevision` terms, with **no second validity counter** and the
  existing coverage outcomes (`wrapTargetRevision`, `coveredPassRevision`, current) as its cases.
- **One pass late is the only stale authority, backed by a pinned source revision** — the previous revision of *that
  interval* serves that traversal, never torn and never replaced mid-consumption; later intervals stay constructible
  without a resident whole-loop root; the allowance is **not reset by a further edit**; and a **second** miss is a named
  contract breach rather than a pass.
- **Nothing allocates or blocks on a timing-critical edge**: the MIDI boundary, the transport tick and the clock path
  only *read* readiness; acquisition, replacement and publication happen on **one named off-clock owner**
  (`Track::stepPlaybackViewMaintenance`, under its declared `CoverageServiceContract`) honouring its bounded
  service contract (fixed work quantum, persistent build cursor, bounded denied turns, demonstrated handoff margin). The
  owner that *requests* a preparation does not publish in the steady state.
- **The mechanism is the existing resumable machinery** (bounded body-format write/parse, resume arming, one
  persistence drain budget). A second publication scheduler is a recorded failure mode.
- **Readiness receipts are delivered where they are produced**, and a diagnostic does not evict the evidence it
  explains.
- **Readiness is completed publication, not non-empty content**: a prepared **empty** interval is ready and sounds as
  silence, and the completion state is not a second revision counter.
- **The window identity separates content currency from lifetime safety** — `(slot, playbackGeneration,
  playbackRevision, interval)`: the revision is the sole currency authority, the generation guards ownership changes and
  stale deferred completions, the interval carries the loop geometry, and the snapshot identity is source provenance
  rather than another definition of "current".
- **Length independence is proved by sweep, not by a bar count**, and a window must be servable **without whole-root
  residency** — with the retained source revision that makes that possible **charged to the envelope**.
- **Checkpoint parity is preserved**: the published root stays the immutable checkpoint authority for undo and save;
  undo parity and save parity are judged against the *current* revision, and a lagging checkpoint is **named**, never
  silently presented as current. **No root dependency is removed by this change.**
- **Content parity holds across a window boundary**: successive windows concatenate to the same events as the
  whole-loop path, and **no accepted event is ever truncated or dropped to fit a window**.

## Capabilities

### New Capabilities

- `windowed-playback-generation`: the active-playback window authority — window identity and immutability, per-window
  currency, per-tick readiness against a deadline, the one-pass-late bound, the off-clock acquisition owner, the
  admission-capacity accounting, legacy dense-content compatibility through bounded pages, and the
  checkpoint-parity, boundary-content-parity and length-independent-residency requirements.

### Modified Capabilities

- None. This change states requirements on a **new** authority; no existing capability's requirements change. It
  deliberately does not modify `playback-runtime-prewarm` (whose fixture pins today's reserved-capacity formula —
  cited, not restated) and does not amend the RWO change's own contracts.

## Inherited contract (cited, not duplicated)

| source | what it hands this change |
|---|---|
| `runtime-working-set-ownership/S4C1-READINESS-CURRENCY-CONTRACT.md` (AC-1…AC-9) | the readiness/currency/off-clock acceptance criteria, and the four interfaces: window identity, `P`-class accounting, readiness predicate + lag bound, off-clock owner |
| `runtime-working-set-ownership/S4C45-CHECKPOINT-PARITY-AND-FIXTURES.md` (§1, F-1…F-8) | checkpoint parity for undo and save, and the fixture list this change must own; F-7/F-8 extend the existing content-verification pipeline rather than building a second one |
| `runtime-working-set-ownership/S4C0-PUBLICATION-INVENTORY.md` | the publication unit's owners/consumers, and the resumable machinery to reuse |
| RWO `design.md` Revision 11 + rows 11/12/16/28 | the decided rules: readiness at re-entry, one pass late, no allocation on the edge, reuse before invention |
| RWO `S2C1-PLAYBACK-WINDOW-LEASE.md`, `S2C23-CLASS-BUDGET.md`, `evidence/2026-10-02-64x64x48-capacity/ANALYSIS.md` | the `P`-class derivation on the target ABI (331 KiB per holder today), the class budget, and the capacity blockers and levers |

## Known behavior, evidence-backed assumptions, UNKNOWNs

**Known (read from code at HEAD).** Past the demotion bound the playback window is **two bars centred on the
playhead** (`kMergedMidiEventsGatherBars = 2`), gathered from the published root; readiness is
`builtFromRevision == playbackRevision` **and** a non-empty window (`Track::isPlaybackMergedMidiEventsReadyForSlot`). A
slot with no published root gathers **empty** (`GatherMissHoldEmpty`) — the measured cause of RWO's S4b.7
demoted-target hold.

**Evidence-backed assumptions.** The `P` derivation is S2C1's, measured on the target ABI (`sizeof(MidiEvent)` = 20 B
on cortex-m7): **331 KiB per holder** today, **28.6 KiB** at a two-bar window at typical density. The gather fanout is
**nine call sites of two entry points across six files** (`S4C45` §3's reconciliation of the older "five" wording).

**Decided since (DEC-108).** The window span, the per-window capacity, the envelope targets and the demoted-target hold
are answered above; readiness becomes completed publication (an empty interval is valid silence); the identity gains the
playback generation; retention becomes a pinned source revision. The third G2 pass's three implementation choices are
decided too (DEC-108 §14): capacity accounting is a **sliding window-span invariant** — the window's storage capacity is
**exactly the provisional 2,048-event limit**, with the closure reserve inside it and **no resolver on the MIDI edge**;
legacy dense content is served through a **bounded segmented page path** instead of the removed mutable fallback; and the
effective-content owner to extend is the query path over the selected **`LoopSnapshot`**, indexed over pinned bodies
(*corrected 2026-10-02 — **F-1** of [`DECISION_REVIEW.md`](DECISION_REVIEW.md): this line named
`LoopContentResolution`, which `NAMING.md` forbids in new docs; the owner is the selected snapshot, and no requirement
changed*). What stays genuinely
unproven is listed in the change's `design.md` §"Still owed — unproven bounds".

## Recorded decisions (owner-supplied, 2026-10-02 → DEC-108)

The four items this proposal previously left open are **answered** (owner-supplied, recorded 2026-10-02):

1. **The product limit / window capacity** — the existing **2,048-event** ceiling is retained as an explicitly
   *provisional* per-window capacity, with a **two-bar span** as the initial comparison configuration and smaller spans
   (quarter-bar, one-bar) to be evaluated. Neither is a proven supported-density claim.
2. **The window size and its tunable range** — a recorded tunable with a declared initial configuration; preparation runs
   **ahead of** the playhead, so "centred on the playhead" is an implementation detail rather than the scheduling
   contract.
3. **The envelope targets** — today's **eight** sounding roles are the supported envelope; **16 is the next target**, and
   32/48/64 are measured **without promise**. A target counts as supported only when both the memory and the deadline
   tests pass.
4. **The demoted-target hold** — replaced: the hold is satisfied by "**required window and source lifetime ready**",
   never by whole-root residency and never by admitting an unprepared switch.

All four, together with the owner, reservation, retention, identity and readiness answers, are recorded as **DEC-108** —
a proposed contract, **not** proven behaviour.

## Contribution to continuous runtime operation

The end-to-end goal and its workload/timing acceptance are defined once in the runtime spec's
[Continuous runtime operation has one end-to-end acceptance contract](../runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md#requirement-continuous-runtime-operation-has-one-end-to-end-acceptance-contract).
This change supplies bounded window publication, note-preserving handoffs and their evidence, including explicit
capture/overdub undo/redo playback-refresh integration (task 3.14 — undo/redo audible handoff). Its local completion
does not prove continuous capture/finalization or physical MIDI timing for the whole runtime; task 6.8 — continuity
evidence handoff feeds the runtime's combined acceptance. Capacity refusal and fault-induced slot silence remain
safety policies, never passing outcomes inside the supported envelope. No numerical timing bound is claimed here.

## Non-goals

- Implementing the unit, and any `src/`/`include/` change, in this step. RWO's implementation status is unchanged;
  its final acceptance dependencies now include the continuity contract above.
- **Narrowing `publishedSnapshot` to the checkpoint authority, and moving undo-root-pinning ownership.** RWO's S5
  root demotion stays gated on this change's parity proof; this change states the parity requirements and removes no
  root dependency.
- RWO S2.11's other capacity levers: snapshot de-duplication of committed events, dropping `MidiEvent`'s SysEx
  pointer, a compact committed format with derived note-offs, and re-opening `POOL_CHUNK_COUNT`.
- The visual tier's envelope / the display detailed window (RWO S4e — a different owner with its own caps).
- 16-channel bank switching and multi-slot-per-track playback (RWO S0.5: capabilities the law must not preclude, not
  implemented by it).
- Restoring whole-take reservations for demoted slots in order to serve a window (the reservation is extended
  window-sized, never reverted to the representation RC-MEM-2 removed), and truncating accepted content to fit a window.

## Impact

- **Firmware (at Apply, not now):** `src/Track/TrackPlaybackWindowBuild.cpp` (window build and readiness),
  `src/TrackManager/TrackManagerTransportTick.cpp` (commit-time readiness), `src/TrackManager/TrackManagerSlotMatrix.cpp`
  with `src/main.cpp` and `src/MidiButtonActions.cpp` (the off-clock owner's lane), the two gather entry points
  (`src/Loop/LoopSnapshotPlayback.cpp`, `src/Loop/LoopMaterialization.cpp`), and their consumers
  (`src/Loop/LoopVisualCache.cpp`, `src/DisplayManager/*`). **Undo playback refresh is explicitly in scope:**
  `src/TrackUndo.cpp`'s capture/overdub undo/redo routes must request prepared handoff rather than blanket-silence
  the slot. This does not move undo/checkpoint ownership or change checkpoint semantics. Other checkpoint consumers
  remain read-only in rationale: `src/StorageLoopIo.cpp`, `src/StorageManager/WorkspaceSave.cpp`,
  `src/StorageManager/LibraryObservationSession.cpp`.
- **New machinery and state (at Apply):** `include/RuntimePlaybackView*.h` with `src/Track/RuntimePlaybackViewBuild.cpp`
  (the declared coverage-service build contract), `src/Track/TrackPlaybackHotPath.cpp` (the consumption choice among
  pair / view / window), and `src/Track/TrackPlaybackRuntime.cpp` (the size pin, re-based deliberately).
- **Constraints:** the RAM1 budget (`scripts/check_ram1_budget.py`, floor **840 B**; invariant 36's **872 B** usable
  padding) — new per-slot state is external (`LoopPlaybackRuntime`) or `FLASHMEM`/`TRACK_COLD_MEM` constructed.
- **Process:** the change's own **DEC-108** entry and `ARCHITECTURE-REASSESSMENT.md` **landed 2026-10-02** — what RWO's
  `IMPLEMENTATION_CONTRACT.md` §4 required of this sibling before Apply. Still required before a firmware edit: the
  `DECISION_REVIEW.md` pass, one G2 execution packet per slice, and the window owner landing before any behaviour
  change. RWO's `S7 gates` and S2.3's `N` stay provisional until this change's gates pass. `openspec/specs/` gains the
  capability only at archive.
- **Docs:** RWO `tasks.md` (S2.9/F), `design.md:812`, `docs/Runtime/PROJECT_STATE.md`,
  `docs/Runtime/CURRENT_WORK.md`, and `docs/DELIVERABLE_TRACKING.md` on ship.
