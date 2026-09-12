# Stage 6C — Live-record snapshot candidate capacity extension

**Status:** **6C-A SHIPPED** (2026-09-12)
**Priority:** P0 — first-root publication debt on loops longer than ~17 bars
**Parent plan:** [`record_stop_snapshot_publication_gap_display_bugfix.md`](record_stop_snapshot_publication_gap_display_bugfix.md)
**OpenSpec:** [`immutable-loop-snapshot-passes`](../openspec/changes/immutable-loop-snapshot-passes/)
**Authority:** DEC-044, DEC-045 (amendment required), DEC-047, [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) rows 21–22

**Supersedes as next slice:** Stage **6B** (first-root PLAYING construction-unit boost) — **DEFERRED**, not rejected permanently.

---

## Verdict (2026-09-12)

| Item | Verdict |
|---|---|
| Continuous incremental Snapshot construction | **APPROVED WITH TIGHTEN** — already implemented; truncated by capacity policy |
| Stage 6C design | **APPROVED** — implement from this plan only |
| Stage 6B | **DEFER** — tactical optimization, not architecture; wrong evidence if shipped first |
| Construction unit budget (`kSnapshotIdleConstructionUnitsPerTurn = 2`) | **UNCHANGED** |
| Firmware implementation | **6C-A shipped** — native `test_loop_take_survival` PASS; device freeze-scaling [`144434`](../../captures/session_20260912_144434.log) |

---

## Debugging boundary

```text
RC1 / RC2 (session-exit admission)     ← CLOSED — do not reopen
6A / 6D (drain + draw-source bound)  ← SHIPPED
6C (live-record candidate capacity)  ← THIS PLAN
6B (units/turn boost)                ← DEFERRED
Stage 7 (preview parity)             ← OPEN — separate owner
RC-W / RC-WQ / DOC                   ← PARKED — not in 6C scope
```

---

## Executive summary

The residual first-root publication latency is **not** primarily a construction-throughput problem. The architecture is already **continuous and incremental** during RECORDING. A **fixed tick-capacity ceiling** on the live-record candidate (~17 bars) stops incremental construction while the take continues, accumulating **session-exit publication debt** proportional to `(bars − 17)`.

**Distinction that matters:**

| Term | Meaning in this codebase |
|---|---|
| **Construction throughput** | How fast bounded `LoopSnapshotConstruction::step` work is drained (units/turn, Ring B share) |
| **Candidate capacity** | How many ticks the private `unpublishedCandidate` (and in-flight construction candidate) can represent before admission is rejected |

6B addressed throughput. **6C addresses capacity.**

---

## Root cause

### Mechanism

`liveRecordNewParentLength` provisions the first live-record parent with a fixed **16 extra bars** of tick room beyond the first completed bar:

```text
first completed bar = 768 ticks (1 bar)
kExtraBars = 16
initial candidate capacity = 768 + 16 × 768 = 13056 ticks = 17 bars
```

Three independent gates treat `unpublishedCandidate->loopLengthTicks() < completedTicks` as **reject construction** rather than **extend capacity**:

1. `prepareLiveRecordWindow` — returns false when candidate length is insufficient
2. `resolvePendingParent` — live-record branch returns empty ref
3. `Loop::liveRecordCanApplyCompletedTicks` — returns false when candidate is shorter than completed frontier

Historical intent (DEC-045 amendment 2026-08-26): avoid prefix rebuild on each length step (`session_20260826_121754`). That rationale applied to **rebuilding** the snapshot, not to **`LoopSnapshot::withLoopLengthTicks`**, which shares `spanRoot_` / `eventRoot_` and only changes `loopLengthTicks_` (O(1) header clone).

### Evidence — capacity cliff, not throughput saturation

`snapshot_alloc` fires on every `freeze()` and is **not** downsampled. Freeze count during RECORDING:

| Capture | Bars | Record duration | Freezes **during** RECORDING | Last live freeze before stop | Publish vs OD |
|---|---|---|---|---|---|
| [`135133`](../../captures/session_20260912_135133.log) | 17 | 34.56 s | **33** | 1.28 s | **−0.29 s** |
| [`135759`](../../captures/session_20260912_135759.log) | 20 | 40.27 s | **34** | 4.96 s | +0.58 s |
| [`130411`](../../captures/session_20260912_130411.log) | 21 | 41.60 s | **35** | 6.34 s | +1.41 s |
| [`140318`](../../captures/session_20260912_140318.log) | 25 | 49.38 s | **34** | **14.06 s** | +2.49 s |

Freezes during RECORDING stay **constant at 33–35** while loop length grows 17 → 25 bars. That is a **capacity cliff**, not gradual throughput exhaustion.

[`140318`](../../captures/session_20260912_140318.log) is the clearest proof: live construction runs ~2 freezes/bar at ~1.975 s/bar, then goes **silent for 14.06 s** (~7 bars) before record stop, then resumes post-stop drain at ~5.6 freezes/s at the **same** 2 units/turn. Post-stop throughput is healthy; live path was **stopped by policy**.

Stall gap vs bars past 17: `(bars − 17.5) × ~1.975 s/bar` (within one bar across all four captures).

### Superseded diagnosis (Stage 6B framing)

Prior Stage 6 residual framed the problem as:

```text
throughput ≈ Snapshot idle turns in PLAYING × construction units / turn
```

That misattributes loop-length scaling. The dominant term is **debt accumulated during RECORDING** after bar 17, not PLAYING-window drain rate. Increasing units/turn is a constant factor against linearly growing debt; it moves the cliff (~21 → ~25 bars) without removing it and can **PASS a 21-bar acceptance capture while leaving the mechanism broken**.

---

## Architectural invariants

### Central design sentence (Stage 6C)

> **Candidate length is extendable; candidate length is not a reason to reject otherwise valid completed live-record content.**

All three gates must express this rule consistently.

### Invariant S (tightened — correctness, not measured headroom)

> While a capture pass is open, live-record snapshot construction admits completed content incrementally and **candidate capacity does not prevent admission of the next completed bar**. Within the classified execution envelope, the applied frontier remains bounded behind the completed-bar frontier.

**Do not** treat “applied frontier trails by at most one bar” as a formal guarantee independent of MIDI density or scheduling. Admission is capped at **one completed bar per fragment** (`prepareLiveRecordWindow`); service rate depends on Ring B share and per-turn cost.

**The architectural guarantee 6C enforces:**

> **Candidate capacity cannot itself create publication backlog.**

### Invariant C (capacity at operation boundary — design checkpoint)

> Any candidate on which the next live-record fragment may operate has capacity ≥ the completed-tick frontier that the fragment is admitted for.

Checked at the ownership boundary (`prepareLiveRecordWindow` / extension helper), not assumed because only the parent ref was extended.

### Corollary (session-exit debt, after 6C)

Session-exit publication debt should be **at most ~two bars** of capture content (in-progress bar + final sealed bar), **not** `(loop_length − 17)` bars. A 100-bar loop still contains more snapshot data than a 10-bar loop; what must disappear is the **17-bar cliff**, not total work proportional to loop length.

**Goal (defensible):**

```text
10 bars → construction follows completed-bar frontier
25 bars → construction follows completed-bar frontier
40 bars → construction follows completed-bar frontier
```

subject to the established execution envelope (rows 21–22, 2 units/turn).

---

## Desired behavior

### Today (broken)

```text
completed bar 18
       │
       ▼
candidate only supports 17 bars
       │
       ▼
STOP (all three gates reject)
       │
       ▼
wait for record stop
       │
       ▼
session-exit debt ∝ (bars − 17)
```

### After 6C

```text
capture progresses
       │
       ▼
completed bar N
       │
       ▼
candidate capacity < completed frontier?
       │
       ├── no ──► admit one bar; continue construction
       │
       └── yes ─► O(1) extend capacity (withLoopLengthTicks / handoffLoopLengthTicks)
                         │
                         ▼
                    continue construction
```

---

## Design checkpoint — in-flight construction length

**This is the mandatory pre-implementation proof before firmware edits.**

### State A — no construction in flight

```text
unpublishedCandidate.loopLengthTicks = 13056
liveRecordCompletedTicks           = 13824  (bar 18 complete)

→ extend unpublishedCandidate to ≥ 13824 (withLoopLengthTicks)
→ prepareLiveRecordWindow / startNextPassChangeFragment
```

### State B — construction in flight

```text
unpublishedCandidate.loopLengthTicks = 13056
construction.candidate.loopLengthTicks = 13056
construction applying bar 18 content
liveRecordCompletedTicks               = 13824

→ extending only unpublishedCandidate is insufficient
→ must also call LoopSnapshotConstruction::handoffLoopLengthTicks(sealedLength)
   on the in-flight construction (already used at RC1 session-exit handoff)
```

**Owner:** extend at a single helper called from `prepareLiveRecordWindow` (or immediately before admission) that updates **both** `unpublishedCandidate` and in-flight `construction` when present.

**Semantic checks before implementation:**

| Check | Risk if ignored |
|---|---|
| `LoopSnapshotNote::isWrap` / linear record notes | wrap notes mis-classified after length extend |
| `eventAlreadyInSnapshot` tick guard | events skipped or duplicated |
| `LoopSnapshotEventBarApply::begin` rejects `tick >= loopLengthTicks` | bar 18+ events fail construction |
| `SnapshotBarPageDirectory::pages` resize at 32-bar page boundaries | rare vector growth inside one unit — confirm bounded |

Reuse existing machinery:

- `LoopSnapshot::withLoopLengthTicks` — shares roots, no prefix re-apply
- `LoopSnapshotConstruction::handoffLoopLengthTicks` — updates private `candidate_.loopLengthTicks_`
- `handoffPendingSnapshotLength` — RC1 session-exit pattern (extend candidate + construction)

---

## Three gates (single rule, three sites)

| Gate | File | Current behavior | Required behavior |
|---|---|---|---|
| Admission | `prepareLiveRecordWindow` | `return false` when `unpublishedCandidate->loopLengthTicks() < completedTicks` | Extend capacity, then admit |
| Parent resolution | `resolvePendingParent` live-record branch | Returns `{}` when candidate shorter than completed | Extend or use extended candidate |
| Eligibility | `Loop::liveRecordCanApplyCompletedTicks` | `return false` when candidate shorter than completed (non-session-exit) | Return true after extension path engaged |

**Do not** add a new Manager, scheduler, or abstraction. Extend the existing owner (`LoopSnapshotPublish.cpp`).

**Do not** fix only the admission gate while parent resolution still treats “too short” as impossible.

---

## Implementation slices

### 6C-A — Remove the capacity cliff (first commit)

**Keep `kExtraBars = 16` unchanged** — isolates the architectural change.

**Files (expected):**

- `src/Loop/LoopSnapshotPublish.cpp` — `prepareLiveRecordWindow`, `resolvePendingParent`, shared extend helper
- `include/Loop.h` / `LoopSnapshotPublish.cpp` — `liveRecordCanApplyCompletedTicks`
- `test/test_loop_event_store` or `test_record_stop_publishes_complete_snapshot` — native fixture **> 17 bars**

**Explicitly untouched:**

- RC1 / RC2 predicates
- `kSnapshotIdleConstructionUnitsPerTurn` (2)
- 6A session-exit drain
- Wrap journals (`queueOverdubWrapSnapshotJournal`, rebase behavior)
- Display / preview / Stage 7
- `MidiServiceDrain.h` unit-boost helpers (6B)

**DEC-045 amendment (6C-A):** supersede clause “when the parent is too short, idle does not start” (2026-08-26). Replace with: live-record candidate capacity is extended via `withLoopLengthTicks` / `handoffLoopLengthTicks`; stall is not permitted when completed frontier exceeds capacity.

**Architecture checkpoint:**

| Question | Answer |
|---|---|
| Ownership change? | **NO** |
| State transition change? | **YES** — live-record admission no longer stalls at 17 bars |
| Execution bound change? | **NO** — row 21 unchanged; row 22 adds O(1) header clone already classified at session-exit |
| Behavior-preserving? | **NO** — intentional fix |

### 6C-B — Reduce oversizing (second commit, after 6C-A proof)

Only after 6C-A device + native proof:

- Reduce `kExtraBars` from 16 to **1 or 2** in `liveRecordNewParentLength`
- Verify: no pathological repeated extension, no construction-unit regression, no page-directory issue, no allocation regression, no publication regression

**Do not** combine 6C-A and 6C-B in one commit.

---

## Verification

### Primary gate (6C-A) — freeze count scaling

**Not sufficient alone:** “publish happened before overdub” on a 21-bar capture (6B could fake PASS).

**Required:**

> Freeze count during RECORDING **continues scaling with completed recording bars** rather than saturating at ~33–35.

**Procedure:**

1. Device capture: record **25–40 bars** before overdub (same stimulus density as [`140318`](../../captures/session_20260912_140318.log)).
2. Parse `#CAP,…,DIAG,snapshot_alloc,nodes=…` timestamps during RECORDING only.
3. **PASS:** last live freeze ≤ ~2 s before record stop; freeze count ≥ `bars − 2` (approximate; exact threshold from bar duration).
4. **FAIL:** freeze count flat at 33–35 with `bars > 17`; gap > 5 s between last live freeze and record stop.

### Secondary gates (after 6C-A)

| Check | Criterion |
|---|---|
| RC1 | Record-stop discard remains µs-class (handoff, not discard) |
| RC2 | No permanent `exit=1,inflight=0` self-lock during overdub |
| MIDI | `DIAG,midi_gap` / `BAR` tick error within existing post-fix envelope during RECORDING |
| RAM1 | `pio run -e teensy41-capture-serial` locals ≥ 4000 |
| Native | `pio test -e native`; extend multi-bar live-record fixture |
| Publication latency | Improved; report but not sole acceptance for 6C-A |

### Service margin (empirical, not invariant)

Post-stop drain in [`140318`](../../captures/session_20260912_140318.log): ~5.6 freezes/s at 2 units/turn vs ~1.0 freezes/s demand during live RECORDING → **~5.5× headroom**. Document in closeout; do not encode as correctness invariant.

---

## What must NOT change

- Immutable published `LoopSnapshot`; atomic `storePublishedSnapshot`
- Capture storage ownership (`capture.store`); commit/seal → `queueSessionExitSnapshot`
- `ActiveNoteLedger`, pass ownership, overlap/pairing (construction consumes settled changes)
- Wrap-journal structural caps and ordered fragments (DEC-047) — **investigate separately (RC-W)**
- Visual cache authority; runtime consumer contracts; MIDI reserve/yield (`kSnapshotIdleReserveYieldEnabled` parked)
- Row 21 per-unit caps; **2 construction units per turn**
- No new Manager, scheduler, queue, or fourth execution-bound class

---

## Parked follow-ups (not 6C)

| ID | Topic | Why separate |
|---|---|---|
| **6B** | First-root PLAYING units/turn boost | Deferred optimization; wrong evidence; row 22 cardinality change |
| **RC-W** | Wrap-journal `SnapshotNoteChange.previous` geometry captured at queue time, applied after rebase onto newer candidate | Latent correctness; longer continuous window increases exposure |
| **RC-WQ** | `queuedWrapJournals` depth policy (fragments capped, count unbounded) | Separate bound decision |
| **DOC** | DEC-045 §3 successor-freeze vs DEC-047 §5 vs shipped `allowSnapshotIdleStep` | Authority reconciliation only |
| **Stage 7** | `missed_preview` / `compose_miss` | Display compose; after 6C publication anchor |

---

## Pre-implementation review

### Ready

- Root cause identified with capture proof (freeze saturation)
- Extension primitive exists (`withLoopLengthTicks`, `handoffLoopLengthTicks`)
- Three gates enumerated; RC1 handoff pattern reusable
- 6C-A / 6C-B split agreed; `kExtraBars` frozen for 6C-A
- 6B explicitly deferred

### Resolved (user / architecture review 2026-09-12)

| Topic | Decision |
|---|---|
| Next slice | 6C-A capacity extension, not 6B |
| Invariant | Capacity cannot create backlog; do not formalize 5.5× as invariant |
| `kExtraBars` in first commit | Keep 16 |
| In-flight construction | Must extend construction candidate, not only parent ref |
| Wrap journal | Park RC-W; do not bundle |
| Goal wording | Remove 17-bar cliff; not “loop length irrelevant” |

### Open before coding (implementer must pin)

1. Exact helper name and call site for dual extend (parent + in-flight construction)
2. Native fixture bar count (recommend 25 bars minimum)
3. Whether `extendLiveRecordCandidateCapacity` lives beside `handoffPendingSnapshotLength` or is folded into it for live-record-only path

### Proceed?

**YES** — design approved; firmware edits only after implementer posts architecture gate table in session and confirms State B handling in code review.

---

## Implementation order (authorized sequence)

1. Post architecture gate in implementer session (copy table from 6C-A above)
2. **6C-A firmware** — three gates + in-flight handoff; `kExtraBars` unchanged
3. Native tests + `pio test -e native`
4. `pio run -e teensy41-capture-serial` (RAM1 ≥ 4000)
5. Device capture 25–40 bars — **freeze scaling proof**
6. Stage commit (6C-A)
7. **6C-B** — reduce `kExtraBars`; re-verify
8. Update parent plan + DEC-045; optional DEC-047 DOC follow-up
9. Stage 7 parity re-check on new anchor

**Do not** implement from paraphrase of this plan. Implement from the pinned gates, State B invariant, and verification table above.

---

## References

- [`LoopSnapshotPublish.cpp`](../../src/Loop/LoopSnapshotPublish.cpp) — `liveRecordNewParentLength`, `prepareLiveRecordWindow`, `resolvePendingParent`, `handoffPendingSnapshotLength`
- [`LoopSnapshot.cpp`](../../src/Loop/LoopSnapshot.cpp) — `withLoopLengthTicks`, `handoffLoopLengthTicks`
- [`MidiServiceDrain.h`](../../include/Utils/MidiServiceDrain.h) — `kSnapshotIdleConstructionUnitsPerTurn`, `liveRecordCompletedTicks`
- [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) — rows 21–22
- Architecture review session 2026-09-12 (continuous incremental construction review)
