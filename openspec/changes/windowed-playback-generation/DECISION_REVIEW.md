# Decision review — windowed-playback-generation

**Date:** 2026-10-02
**Change:** `windowed-playback-generation` (task 1.2 of its `tasks.md`)
**Decision under review:** [`DEC-108`](../../../docs/DECISION_LOG.md#dec-108--windowed-playback-generation-one-publication-owner-window-sized-reservation-pinned-source-retention-and-readiness-as-completed-publication)
(plus its companion [`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md) and the scope clarification
[`DEC-109`](../../../docs/DECISION_LOG.md#dec-109--continuity-acceptance-belongs-to-the-runtime-windowed-playback-owns-its-handoff-obligations))
**Method:** [`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), a historical review as
required before firmware implementation because the change's
[formal triggers](../../../docs/ARCHITECTURE_REASSESSMENT.md#formal-triggers) — new owner, ownership transfer, timing
model — have fired.

This review is **documentation and evidence only**: no firmware, no `src/`/`include/` change, no build, no device
action. It records the prior decisions this change inherits, the reuse decision, and **two authority findings** that
must be resolved before the slices they touch are implemented.

---

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) — headers scanned for the topic, then the entries named
  below read in full (no whole-file read)
- [x] Active OpenSpec — this change, `runtime-working-set-ownership` (`IMPLEMENTATION_CONTRACT.md`, `S4C0`, `S4C1`,
  `S4C45`, `S2C1`, `S2C23`, `tasks.md`), `response-driven-hitl-acceptance`, `loop-content-resolution`,
  `immutable-loop-snapshot-passes`
- [x] `openspec/specs/` — `playback-runtime-prewarm`, `pass-reclaim`, `loop-event-pool-admission`,
  `loop-commit-semantics`, `revision-load`, `note-edit-*`, `timeline-passes`, `deferred-job-scheduler`
- [x] `docs/Authority/` — `README.md` (authority order and conflict table), `NAMING.md`,
  `Architecture/ExecutionBounds.md`, `Architecture/DerivedViews.md`, `Architecture/Snapshots.md`,
  `Architecture/RuntimeArchitecture.md`
- [x] `docs/Plans/` — `playback_window_round_robin_admission_refinement.md`,
  `loop_event_sourced_resolution_architecture.md` (context only)

**Search terms:** `windowed playback`, `merged window`, `mergedMidiEvents`, `playback window`, `coverage`,
`readiness`, `one pass late`, `pinned source`, `retention`, `PassReclaim`, `CommittedEventRange`, `TickIndex`,
`LoopContentResolution`, `effective-content`, `CoverageServiceContract`, `RuntimePlaybackViewFill`,
`SlotProvision`, `silenceSlotMidiOutput`, `playbackRevision`, `snapshot identity`, `prewarm`, `page`, `admission
limit`.

### Relevant findings

- **DEC-037 — `LoopContentResolution` parallel prototype.** The entry that *created* `LoopContentResolution` states:
  item 1, it is "the derivation owner for effective musical state"; item 3, "do not name the owner `Resolver`"; items
  9–10, the work lands as a **parallel native prototype** and "production consumers swap only after **three gates**"
  (correctness vs materialize, complexity, device worst-case latency). Its `tasks.md` shows 65 checked and 3 open.
  The repo's current position (see F-1) is that the swap never happened.
- **DEC-044 — immutable loop snapshot model replaces the maintained LCR direction.** Complete immutable snapshots are
  the target; `LoopPasses` remains the historical/compatibility operation record.
- **DEC-046 — round-robin admission; representation coverage is a prerequisite.** Ring A admits one eligible
  content-sized playback-window rebuild per clock dispatch; the admitted class stays **Unbounded**; **Invariant R**
  ("a derived representation may satisfy a consumer query only when its coverage corresponds to the requested
  interval at the requested revision"); **Invariant A1** (serve the old immutable window and raise a starvation
  diagnostic — no synchronous rescue rebuild on the clock path); **Invariant A2** (no persistently eligible
  destination may be indefinitely displaced; the cursor keeps advancing and newly eligible members cannot reset the
  rotation); prefetch lead is **derived**, not chosen; window **bias** is to be measured, not guessed.
- **DEC-058 — runtime-owned derived playback views and bounded stop admission.** The view set, its roles and its
  bounded admission this change extends.
- **DEC-073 — coverage decides currency.** A published root carries the committed-pass coverage it was built from;
  `committedPassRevision()` **is** `playbackRevision` (the alias the spec's identity clause relies on);
  `publishedSnapshotIdentity()` is non-null only while current.
- **DEC-075 — publication admits memory per construction turn and parks on allocation failure.** The admission
  pattern this change's retained source and pages must respect.
- **DEC-076 — role-owned working sets; the law this change executes.** Points 1 (window-sized, `owner + generation +
  role`, released when no live owner remains), **3** (immutable readiness owed per affected tick; a missed deadline
  keeps the previous version authoritative for that pass; the publication **unit and deadline** change and the
  **existing** resumable machinery is reused rather than a second incremental-publication system), **4** (a published
  root is the checkpoint authority, **not** the playback authority), 5 (durability gates eviction), 6 (the looper never
  stops playing), 9 (every interaction is accepted; timing is the pipeline's decision). Also fixes the vocabulary
  "Playhead-scheduled MVCC", and states a **window-size tunable** with its three coupled effects.
- **DEC-100 — the declared envelope is reported, never enforced.** Accept-and-schedule, and the report names its
  holder; the shape this change's fairness requirement (spec §"Service is fair, bounded and deadline-ordered")
  extends.
- **DEC-102 / DEC-103 / DEC-104 — the switch lane.** A switch's inbound occupancy has one off-clock owner; the
  provisioning vehicle is the contract's `SlotProvision` turn (`DeferredIdleOwner::SlotProvision = 5`,
  `TrackManager::servicePendingSlotProvision`); the long-loop pending hold is **bounded** and its terminal is a
  **report** rather than a decision. DEC-108 §10 revises the report-only shape for this lane and must not be read as
  reopening DEC-102/DEC-103.
- **DEC-105 — one active-slot write authority** (`setActiveLoopIndex` over a non-re-entrant commit point, and one
  cancel for the trio). Any transition this change adds must not add a second writer.
- **DEC-106 — a playback switch's inbound role is its occupancy; the lease question is deferred, not assumed.**
  Directly relevant to task 3.3's budget: the lease may not be silently assumed to exist.
- **DEC-107 — a frozen-window build no writer can advance must not park the record job.** The precedent for bounding
  this change's own owner wait, and the source of the current `sizeof(LoopPlaybackRuntime) == 6368` pin.
- **DEC-108 / DEC-109 — this change's own contract and scope.** DEC-108 is the owner-supplied contract (owner,
  reservation, retention, identity, readiness, deadline, limits, envelope, demoted hold, plus §12–§14 resolutions);
  DEC-109 assigns end-to-end continuity to the runtime spec and this change the handoff obligation
  (task 3.14, task 6.8).
- **`openspec/specs/playback-runtime-prewarm`** — pins today's reserved-capacity formula and requires prewarm before
  the first play tick. **Cited, not restated**, and `test_playback_prewarm` must stay green unchanged (tasks 3.3).
- **`openspec/specs/pass-reclaim`** — reclaim frees disabled pass rows and chunk refs when **no `GlobalUndoStack`
  entry** references them, and `collectReferencedPasses` scans **undo stacks only**. This is the spec-level form of
  the G2 finding that a playback-owned pin list is invisible to the consumer, and it is why task 2.9 requires the pin
  to be an input the consumer actually reads.
- **`docs/Authority/Architecture/ExecutionBounds.md` rows 2, 27–29 and the `CoverageServiceContract` row** — row 2
  classes the present merged-window build **Unbounded**; rows 27–29 declare
  `Track::stepPlaybackViewMaintenance` as the intended owner and state that "until the code declares those caps and
  proves maximum phase advance/denied-turn coverage, **active windowed playback is denied**". This is the gate that
  makes task 3.1 the first slice and forbids behaviour behind it.
- **`docs/Authority/Architecture/DerivedViews.md`, `Snapshots.md`, `RuntimeArchitecture.md`** — the maintained owner
  table (representation × owner × storage × revision × build policy × consumers), including the note that
  `LoopContentResolution` "is retained only as a snapshot-based compatibility adapter until its caller gates pass".
- **`docs/Authority/NAMING.md`** — the canonical vocabulary: **Bridge**, **Emission cursor**, **Overlay**,
  **Deferred / Pending / Scheduled / Queue / Resolution / Pipeline**, **ActiveNote**, **Analyze**. Its review
  checklist item "Committed effective-state reads use the selected **LoopSnapshot**, not `NoteGeometryResolver` or a
  generic `Resolver`. **Do not introduce `LoopContentResolution` in new docs**" is the basis of finding **F-1**.
- **No prior decision found** that names an interval-publication identity `(slot, playbackGeneration,
  playbackRevision, interval)`, a per-affected-tick readiness predicate with a named holder, a paged legacy event
  path, or an admission limit accounted on the capture interval. These are genuinely new in DEC-108.

### Existing reusable pattern

The change extends existing owners and machinery; it introduces no new abstraction beyond the owner `ExecutionBounds`
**already declares**:

| Concern | Extension point to reuse |
|---|---|
| Off-clock owner turn | `Track::stepPlaybackViewMaintenance` (declared, not implemented) on the existing post-MIDI deferred machinery; `DeferredIdleOwner::SlotProvision` + `TrackManager::servicePendingSlotProvision` stay the requester/reader |
| Build contract + bounded grains | `CoverageServiceContract` (`include/RuntimePlaybackViewBuild.h`) with `RuntimePlaybackViewBuild` / `RuntimePlaybackViewFill` and `TrackPlaybackRuntime::beginCoverageBuild` |
| Window storage and roles | `RuntimePlaybackViewSet` (`LoopPlaybackRuntime`) — the bounded storage, lease and three rolling views this change sizes rather than duplicates |
| Previous-version retention | the prepared pair's existing `PreparedPairConsumeDecision::held` semantics, extended to interval windows |
| Window gather | `Loop::tryGatherPublishedSnapshotEventsInWindow[WithCapture]` (`include/Loop.h`, `src/Loop/LoopSnapshotPlayback.cpp`, `src/Loop/LoopMaterialization.cpp`) — the published-snapshot coverage read that already exists |
| Commit binding | `TrackManager::updateAllTracks` (`src/TrackManager/TrackManagerTransportTick.cpp`) — readiness → occupancy → stay pending, never build inline |
| Interval traversal primitive | `CommittedEventRange::inWindow` / `appendWindowBudgeted` (used as a body-reading primitive only after its inspected-work budgeting is extended, per DEC-108 §14(c)) |
| Retention pin plumbing | `PassReclaim::pinPassesFromLoopPasses` / `pinCapturePass` / `pinEditPass`, and the **named-term list** `Track::noLiveOwnerForSlot` (which already documents itself as "a list of named owner kinds … a new owner kind is added here by name") |
| Evidence delivery | `DebugSessionCapture` one-shot rows + the `persistenceResultImmediate` / `SC_PERSIST_RESULT` immediate-emit shape, with `Utils/TelemetryCadence.h` bounding diagnostic cadence |
| Content verification | `.agents/skills/hitl-content-verification` and `docs/Guides/HITL_CONTENT_VERIFICATION.md` — extended with a windowed profile, not replaced |
| Size pins | the existing `static_assert` pins in `src/Track/TrackPlaybackRuntime.cpp`, re-based deliberately with the reason recorded at the assert |

### Reuse decision

**YES** — with the two conditions recorded under **Authority findings** below. No equivalent owner, abstraction or
helper has to be invented: the publication owner is the one `ExecutionBounds.md` already declares, the storage is the
view set that exists, the retention semantics are the pair's, the gather and the commit binding already exist, and
the change's own `design.md` §"Open Questions" keeps the two remaining code-shape choices (one build entry point or
two; how the owner's turn is wired into the deferred enumeration) as implementation detail rather than new structure.

### If NO

**Not applicable.** There is no new owner beyond the declared one, no ownership transfer of checkpoint state (the
published root stays the undo/save authority), and no new Manager, scheduler, repository or parallel effective-state
owner. Migration is therefore the one `design.md` §"Migration Plan" already states: additive by construction at
Apply, no schema/card-format change, rollback = "the publication unit stays as it is". **Migration required: NO.**

---

## Authority findings (both must be resolved before the slices they touch)

### F-1 — RESOLVED 2026-10-02 (path A): the resolver owner is restated in NAMING vocabulary

**What the change said** (each item below is corrected as recorded at the end of this finding). It made

- `specs/windowed-playback-generation/spec.md:181` — "through the **effective-content resolver owned by
  `LoopContentResolution`**" (normative requirement *One pass late is the only stale authority*);
- `design.md:449-460` (§"Retained-source resolver and interval index") and `design.md:150-154`;
- `tasks.md` §3.2 and §3.6;
- `docs/DECISION_LOG.md` DEC-108 §14(c) (item 12(a) was already restated by the second pass and does not carry the name);
- `docs/Runtime/CURRENT_WORK.md:259` (this change's own scope note).

**What the higher authorities say.**

- [`NAMING.md`](../../../docs/Authority/NAMING.md) — "**LoopContentResolution** | Historical DEC-037 prototype name.
  **New docs do not use it.** Callers are **LoopSnapshot** consumers"; "New docs and new types do not use
  **LoopContentResolution**. Committed effective state is the selected **LoopSnapshot**. Do not name a query owner
  `Resolver`"; the review checklist item "**Do not introduce `LoopContentResolution` in new docs**".
- **DEC-037** items 9–10 — a **parallel prototype**; "production consumers swap only after three gates".
- [`DerivedViews.md`](../../../docs/Authority/Architecture/DerivedViews.md) — "`LoopContentResolution` is retained
  only as a snapshot-based compatibility adapter until its caller gates pass"; its owner table gives the committed
  content representation to `Loop` / the published `LoopSnapshot`.
- [`README.md`](../../../docs/Authority/README.md) authority order — **NAMING ranks above OpenSpec**, and
  "DECISION_LOG vs new proposal … Logged decision stands. **Supersede** via reassessment + new `DEC-###` entry — do
  not ignore."

**Why it is a conflict and not a wording nit.** DEC-108 §14(c) asserts an **ownership status** ("is the
effective-content owner") that NAMING denies and that DEC-037's three caller gates never granted. If the slice is
implemented as written, the repository would carry an OpenSpec requirement and a DEC entry that a firmware
implementation is *required* to satisfy via a name the vocabulary rules forbid and an owner the snapshot model
retired — the case the README's conflict table resolves as "**Stop** … amend OpenSpec design/tasks **or** extend
existing owner per architecture".

**Resolution taken — path A, extend the existing owner (owner's ruling, 2026-10-02; confidence high).** Keep every
DEC-108 §14(c) and restate its **owner** in NAMING vocabulary: the effective-content owner to extend is the selected
**`LoopSnapshot`** query path — the published-snapshot window gather
(`Loop::tryGatherPublishedSnapshotEventsInWindow`) plus the new interval index — indexed over **pinned bodies**, with
`CommittedEventRange` a body-reading primitive only after its inspected-work budgeting is extended and verified. This
changes no requirement: "effective-content resolver" stays, the index-over-pinned-bodies rule stays, the
`TickIndex`-semantics-without-its-event-copy rule stays, the edit-moved-destination rule stays. It costs: an
amendment note on **DEC-108 §14(c)** (the entry is uncommitted; an in-place correction with the reason recorded is
enough and needs no new DEC-### because no decision changes — only the name of the owner it names), a
`docs/Runtime/CURRENT_WORK.md:259` correction, and the spec/design/tasks wording.

**Alternative — path B, re-admit the name (confidence: low).** Supersede DEC-037's demotion, update `NAMING.md`
deliberately, and add a new `DEC-###` entry. Rejected as the default: it re-opens a retired prototype name to serve a
requirement that the snapshot owner can serve, and it inverts the direction `DerivedViews.md` records the repository
as having taken. It becomes the right path only if the architect wants the interval index to *be* the maintained LCR
lifecycle — which is a **new ownership decision**, not a naming one, and `CURRENT_WORK.md:9669-9671` currently
records the opposite ("The maintained `LoopContentResolution` lifecycle and its playback-readiness continuation are
not pursued on this branch … the proposed snapshot model must not create a replacement maintained effective-state
owner").
**Amendments applied under path A (2026-10-02).** The owner is now named as the selected **`LoopSnapshot`** and the
query path over it, with every behavioural clause untouched: the change's `spec.md` (*One pass late is the only stale
authority*), `design.md` (the G2 row 3 resolution, §"Recorded resolutions" 1, and §"Retained-source resolver and
interval index"), `proposal.md`, `tasks.md` §3.2/§3.6, **DEC-108 §14(c)** (an in-place correction with its reason
recorded — **no new `DEC-###`**, because no decision changed, only the name of the owner it names), and
`docs/Runtime/CURRENT_WORK.md:259`. **`NAMING.md` and DEC-037 are untouched** — nothing was superseded, and the
historical prototype name is still cited only where the repository's history is being described.

**Scope, now discharged.** F-1 gated the **naming and ownership** statement of tasks **3.2, 3.6 and 3.12** and the
spec/design wording they cite. It did **not** block task **3.1** — the owner `Track::stepPlaybackViewMaintenance` and
its `CoverageServiceContract` are declared by `ExecutionBounds.md` rows 27–29, independent of the resolver name, and
task 3.1 changes no behaviour behind the owner — and it no longer gates the other three either.

### F-2 — MINOR, fixed in this session: three dangling fragments in the change's normative spec

The third G2 pass "replaced superseded clauses in place" and left three mid-sentence deletions in
`specs/windowed-playback-generation/spec.md`. Each is repaired by restoring the clause the change's own `design.md`
already states; **no requirement changes**. `openspec validate --strict` does not detect prose damage, which is why
they survived three passes. Line numbers in the table are those **before** the repair.

| Line | As it stood | Restored |
|---|---|---|
| 144–145 | "Non-empty content SHALL NOT be a / completion state SHALL NOT become a second revision counter." | "…SHALL NOT be a **readiness requirement**, and the completion state SHALL NOT become a second revision counter." (design D3, DEC-108 item 6) |
| 180–181 | "…cancelled. Retention SHALL be named among the / through the **effective-content resolver owned by …**" | "…`Track::noLiveOwnerForSlot`; enumeration of the retained source SHALL go through the effective-content resolver…" (design §"Recorded resolutions" 1 — the term in `Track::noLiveOwnerForSlot`) |
| 193–194 | "…presented as window-bounded. / **second** missed traversal SHALL be reported…" | "…A **second** missed traversal SHALL be reported…" (the *A second miss latches a fault* requirement at line 573) |

**Note:** the owner name restored in the second row above is the one the text carried; **finding F-1 decides what that
name becomes**, and its resolution amends this clause's owner — not its requirement.
---

## Reviewer gate

Applying the template's rejection list:

| Gate | Result |
|---|---|
| Equivalent abstraction already exists | **No new one is proposed** — the owner is the declared one; the storage is the view set |
| Equivalent helper already exists | **Reused and named** (table above) |
| Ownership duplicated | **No** — one publication owner; `SlotProvision` requests and reads; the published root keeps checkpoint authority |
| Historical decision ignored without supersede entry | **Discharged by F-1's resolution** — the resolver owner is restated in NAMING vocabulary under path A, so no logged decision is ignored and **nothing needed superseding**; implementing a slice that named `LoopContentResolution` as an owner would still be a reviewer rejection |
| Migration path missing when `Migration required: YES` | **Not applicable** — `Migration required: NO` |

**Eligible now:** task 1.2 (this review), task 1.5 (§3.1's execution packet), task 1.7 (F-1's resolution, taken by the
owner on 2026-10-02), and **slice 3.1 itself**, whose declared contract already exists and which changes no behaviour.
**F-1 no longer blocks anything:** its naming gate is discharged for tasks 3.2, 3.6 and 3.12, whose behaviour always
stayed as DEC-108 §14(c) states it.
**Unchanged by this review:** every bound in DEC-108 §11 and `design.md` §"Still owed — unproven bounds" stays
**unproven**; this review adds no measurement and claims no capacity, density, `N`, deadline margin or residency.
