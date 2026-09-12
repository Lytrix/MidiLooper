# Deferred persist — LSV1 EditTail monolithic slice reboot

**Status:** **APPROVED — PROCEED A1** — RC-P1 + Stage A architecture approved; clean close requires native byte-identity + A4 device PASS  
**Priority:** P0 — Teensy hang/reboot blocks third overdub after multi-pass save  
**Evidence:** [`session_20260912_144434.log`](../../captures/session_20260912_144434.log) (`#CAPTURE_RECONNECT` line 205739)  
**Related (closed):** [6C-A capacity](record_stop_snapshot_live_record_candidate_capacity_design.md) — publication healthy on `144434` before persist failure  
**Authority:** [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write), [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md) row **14** (`lsv1_loop_snapshot_tail`)

---

## Verdict (2026-09-12)

| Item | Verdict |
|---|---|
| **RC-P1 RCA** | **APPROVED** |
| **Stage A architecture** | **APPROVED** |
| **Grain model** | **APPROVED** (§ Grain contract) |
| **Outer scheduler** | **UNCHANGED** (300 µs / 3000 µs) |
| **`maxGrains = 1`** | **APPROVED** (deferred persist + native stepped path) |
| **`deadlineUs`** | **Safety stop only** |
| **Scratch model** | **APPROVED** — quantify actual bytes in A1 gate |
| **Serialization** | **Byte-identical required** (I-P3) |
| **Telemetry** | **APPROVED** (§ Telemetry contract) |
| **Stage B / RC-P2** | **DEFER** |
| **6C / 6C-B** | **CLOSED / untouched** |
| **Proceed A1** | **YES** |
| **Clean Stage A close** | **NO** until native byte-identity + A4 device PASS |

### Second-order failure chain (do not reopen 6C)

```text
6C-A live snapshot construction
        ↓
published root healthy (root_rev=6, ~5987 nodes on 144434)
        ↓
LoopPersist admitted (post–OD2 stop)
        ↓
runtime bundle SlotMeta ≈ 532 ms   ← RC-P2 stacking; not reboot root
        ↓
EditTail stage transition logged
        ↓
LSV1 tail = effectively unbounded (next slice)
        ↓
reboot (no payload_done, crash=0)
```

**Separation:** 6C solved snapshot **publication**. RC-P1 solves persistence **execution granularity**. RC-P2 is a separate **latency** problem.

---

## Debugging boundary

```text
6C live-record candidate capacity     ← CLOSED on 144434
record-stop publication gap (RC1–RC2) ← CLOSED on 144434
deferred persist LSV1 tail (RC-P1)    ← THIS PLAN
runtime bundle SlotMeta monolith      ← RC-P2 — DEFERRED (Stage B)
kExtraBars reduction (6C-B)           ← OUT OF SCOPE
```

---

## Root cause — RC-P1 (approved, reboot layer)

### Mechanism

**Owner:** `stepDeferredLoopPersist` → `DeferredLoopWriteStage::EditTail`  
**Writer:** `writeLoopSnapshotBodyFormatV1SaveTail` → `gatherLoopSnapshotPayload` + `writeSectionToBuffer` + SD write  
**Files:** [`WorkspaceSave.cpp`](../../src/StorageManager/WorkspaceSave.cpp), [`StorageLoopIoBodyFormatV1.cpp`](../../src/StorageLoopIoBodyFormatV1.cpp)

1. `CapturePassHeader` sets `loopWriteStage = EditTail` and returns. Telemetry `payload_slice,…,write=EditTail` (~40 µs) is **stage entry only**.
2. Next `processDeferredSaveState` turn runs `EditTail` body: `writePersistedEditsTail` + monolithic LSV1 tail when `loopUsesLoopSnapshotBodyFormatV1ForSave`.
3. No `PERS,work` until the slice returns. Hang/watchdog → silence → `#CAPTURE_RECONNECT`.

### Evidence (`144434`)

| Pattern | Saves 1–2 (OD0/OD1) | Save 3 (post–OD2) |
|---|---|---|
| `write=EditTail` | logged | logged (195757707) |
| `payload_done` | ~1.2–1.6 ms later | **never** |
| finalize / `done` | yes | **never** |
| reboot | no | **yes** (`crash=0`) |

**Telemetry rule (new):** `write=<stage>` on a `payload_slice` line means **entered stage this turn**, not **completed stage work**. Stage **completion** requires an explicit completion marker (see § Telemetry contract).

No third `ST,Track,PLAYING,OVERDUBBING` on `144434`; reboot is on the persist path regardless of overdub press timing.

**Status:** Stage A1 removed the monolithic-slice reboot on the cited captures ([`155724`](../../captures/session_20260912_155724.log), [`162239`](../../captures/session_20260912_162239.log)). **A4 device completion proven** on [`164947`](../../captures/session_20260912_164947.log) — see § A4 validation.

---

## Root cause — RC-P1.2 (completion layer) — **CLOSED**

**Trigger:** [`162239`](../../captures/session_20260912_162239.log) — 26-bar record + 3 overdubs; LSV1 gate active on OD3 save; stepped tail runs to `WritePayload` then fails before `loop_snapshot_tail,done` / `payload_done`.

### A4.1 answer — cursor monotonicity: PASS

| Phase | Steps (approx) | Duration | Verdict |
|---|---|---|---|
| `Gather` (p=0) | ~3328 | — | Advancing (`n` climbs) |
| `GatherEvents` (p=1) | ~78 | — | Advancing |
| `SortNotes` (p=2) | ~5000 | ~10 s | Advancing (bubble grains; `,stall` is false alarm — sig omits swap progress) |
| `SortEvents` (p=3) | ~7700 | ~45 s | Advancing |
| `WriteSectionHeader` (p=4) | 1 | <1 ms | Completes |
| `WritePayload` (p=5) | 2 | <1 ms | **Fails on second grain** |

**Ruled out:** gather livelock, infinite cursor loop, watchdog reboot on tail path.

**Not the completion blocker:** sort grain count (slow but finite); sync-drain stuck detector on Clear ([`160551`](../../captures/session_20260912_160551.log)) — secondary progress-accounting gap only.

### Mechanism — `writeLoopSnapshotPayloadByteSlice` quota treated as I/O failure

**Owner:** `stepLoopSnapshotBodyFormatV1SaveTail` → `WritePayload` case → `writeLoopSnapshotPayloadByteSlice`  
**File:** [`StorageLoopIoBodyFormatV1.cpp`](../../src/StorageLoopIoBodyFormatV1.cpp)

`WritePayload` writes at most `kLsv1SaveIoChunkBytes` (512) bytes per deferred slice by re-serializing the gathered payload and skipping to `writePayloadOffset`. The slice callback stops when `written >= maxBytes`:

```cpp
if (written >= maxBytes) {
  return false;  // intended: stop serialization for this grain
}
```

That `false` propagates through `writeLoopSnapshotPayloadToIo` → `writeLoopSnapshotPayloadByteSlice` returns **`false`** at the outer check — **quota-filled slice is classified as I/O failure**, not success-with-partial-write.

**Why native byte-identity passed:** `test_lsv1_stepped_save_tail_matches_monolithic` uses `buildLsv1RoundTripSnapshotFixture()` (~few notes); total payload **< 512 B**, so a single `WritePayload` grain never hits the quota branch. Device payload on `162239`: **48 317 B** (~95 grains required).

### Evidence (`162239`)

| Marker | Value |
|---|---|
| `tail_gather_begin` | 2 (one reset after first failure) |
| `tail_step` logged | 13 474 |
| `write=LoopSnapshotTail` slices | 13 608 |
| `loop_snapshot_tail,done` | **0** |
| `payload_done` (OD3 save) | **0** |

**Failure sequence (~233.594 s, step 16 222):**

```text
tail_step p=5  pb=48317 off=0
payload_slice ok   write=LoopSnapshotTail  (723 µs)    ← first WritePayload grain
payload_slice failed write=LoopSnapshotTail  (181 µs)  ← second grain: quota-as-failure
slot:4:0,start write=LoopSnapshotTail                   ← job recovery
→ replay CapturePass* → EditTail → tail_gather_begin (full scratch reset)
```

Log ends ~243 s mid-second gather (p=0, `n` climbing) — persist never completes.

### Secondary — failure recovery amplifies cost → **follow-up bundle § Post-RC-P1**

On `payload_slice failed`, persist job recovery restarts from `CapturePass*` replay + `beginLoopSnapshotBodyFormatV1SaveGather` instead of resuming `WritePayload` at `writePayloadOffset`. Explains multi-`enter` / multi-`tail_gather_begin` on [`155724`](../../captures/session_20260912_155724.log). Tracked as **RC-P1.3** in the post-closeout bundle below.

### RC-P2 — deferred

`PERS,bundle,SlotMeta,531827` (~532 ms) stacks MIDI/display debt before `EditTail`. **Not the last line before reboot.** Stage B only after A4 passes.

---

## Invariants (target)

| ID | Invariant |
|---|---|
| **I-P1** | **No LSV1 save turn may perform work proportional to the complete `LoopSnapshot` without yielding through the persisted save cursor.** Resumability is the requirement — not merely avoiding the function name `gatherLoopSnapshotPayload`. Replacing one giant gather with one giant alloc + sort + serialize in a single turn is **forbidden**. |
| **I-P2** | Progress is resumable across `processDeferredSaveState` turns via persistent cursor state on the save job. Partial slot body is not valid until `payload_done` + finalize completes. |
| **I-P3** | **On-disk LSV1 bytes unchanged:** stepped writer preserves existing **serialization field order**, **section boundaries**, and **ordering rules** (notes by `noteId`, events by `canonicalOrder` then `eventId`). Stepping changes execution granularity only — not representation. Native tests compare **byte-identical** payload to monolithic `writeLoopSnapshotBodyFormatV1SaveTail`, not semantic round-trip alone. |
| **I-P4** | Published snapshot remains authoritative during save (5.5/5.6 unchanged). |
| **I-P5** | **Telemetry distinguishes stage entry vs completion** (§ Telemetry contract). HITL/host gates use completion chains, not transition lines alone. |
| **I-P6** | Each `LoopSnapshotTail` grain is **structurally bounded** by classified work units (§ Grain contract). `deadlineUs` is a **safety stop**, not the primary grain definition. 100 ms is a **verification ceiling** (5.6 class), not the architectural target. |

---

## Architecture gate (tightened)

| Question | Verdict |
|---|---|
| Root cause | **CONFIRMED** (RC-P1) |
| Owner | **Existing** `StorageManager` deferred persist path |
| New owner | **NO** |
| Capture/transport state machine | **NO change** (stop still admits only) |
| Persist write state machine | **YES** — explicit resumable `LoopSnapshotTail` stage |
| On-disk representation | **NO CHANGE** (I-P3) |
| Serialization order | **NO CHANGE** (I-P3) |
| Execution bound | **YES** — row 14 `lsv1_loop_snapshot_tail` becomes **enforceable** |
| Primary grain | **MUST PIN** before firmware (§ Grain contract) |
| Scratch lifetime | **MUST QUANTIFY** before firmware (§ Scratch contract) |
| 100 ms `max_pers` | **Verification ceiling only** |
| Bundle 532 ms (RC-P2) | **DEFER** (Stage B) |
| 6C / 6C-B | **CLOSED / untouched** |
| New Manager / scheduler | **NO** |

### Architecture checkpoint

1. **Ownership?** NO  
2. **Capture/transport transitions?** NO  
3. **Unbounded interval owner today?** YES on LSV1 tail — fix makes grains structurally bounded (design session satisfied by this plan)

---

## Grain contract — HARD GATE (pin before A1)

### Design principle

```text
one processDeferredSaveState outer slice
    ↓
bounded number of LSV1 tail grains (per inner step call)
    ↓
each grain = one existing classified work unit
    ↓
return MoreWork; cursor persists on save job
    ↓
next deferred turn resumes
```

**Not acceptable:** `deadlineUs = 100000` as the primary grain; “do as much as possible until deadline.” That is resumable but not architecturally deterministic.

### Outer scheduler (already exists — reuse)

`StorageManager::processDeferredSaveState` ([`DeferredSaveScheduler.cpp`](../../src/StorageManager/DeferredSaveScheduler.cpp)) runs substeps until `PersistenceBudget::persistenceSliceBudgetExhausted`:

| Mode | Budget (`Config` in [`Globals.h`](../../include/Globals.h)) |
|---|---|
| Capture / transport active | `maxPersistenceMicrosActive` = **300 µs** per outer slice |
| Idle | `maxPersistenceMicrosPerLoop` = **3000 µs** per outer slice |

**RC-P1 fix does not add a parallel 100 ms budget.** The stepped tail writer is invoked **from within** this existing outer slice loop (same as capture-pass chunks today).

### Inner grain (PIN — implementer confirms in A1 gate comment)

**One call** to `stepLoopSnapshotBodyFormatV1SaveTail` from `stepDeferredLoopPersist` performs **at most one grain**, then returns `MoreWork` unless complete.

Mirror load-side discipline: `stepLoopSnapshotBodyFormatV1LoadParse` calls `stepPersistedLoopSnapshotParse` with **`maxGrains = 1`** for oracle substeps ([`StorageLoopIoBodyFormatV1.cpp`](../../src/StorageLoopIoBodyFormatV1.cpp) ~627–629).

| Grain type | Classified unit (one grain) | Bound source |
|---|---|---|
| **Membership scan** | Process **one** `(pitch, barIndex)` cell: walk at most **one** membership page (`kSnapshotMembershipPageCapacity` = 16 refs); append note/event refs to incremental accumulators | Row 21 page cap — see **A1 membership-page gate** below |
| **Note / event record** | Emit **one** `Lsv1GatheredNote` or **one** `Lsv1GatheredEvent` record to the SD stream (or to a fixed-size staging buffer flushed when full) | Fixed record size |
| **Ordering flush** | When a sort boundary is reached (end of gather phase), sort **at most N** records (N pinned in A1 — default **16**, not full snapshot) | Structural cap N |
| **SD payload write** | Write **at most** `kLsv1SaveIoChunkBytes` (pin in A1; propose **512** — same class as `kLoopPersistHeaderBytes`) | Structural byte cap |

**Traversal cursors** (persistent on save job, not grain definitions):

```text
barIndex, pitch, membershipPageIndex, noteEmitIndex, eventEmitIndex, sdWriteOffset
```

**`deadlineUs`:** pass `sliceStartUs + sliceBudgetUs` from the active outer `processDeferredSaveState` iteration as a **safety stop** inside the inner step (same pattern as `parseBudgetExhausted` on load). If exhausted mid-grain, return `MoreWork` at last safe cursor — do not partial-commit the slot file.

**`maxGrains` per inner call:** **1** always for the stepped writer (deferred persist and native tests). No `maxGrains = 0` / unlimited mode on the stepped API.

### A1 membership-page gate (mandatory)

> **A membership-page grain must bound the complete work performed by that grain; the 16-reference page capacity is not merely an iteration-count cap if reference resolution, dedupe, or temporary allocation can expand beyond it.**

Implementation must verify that one membership-page grain corresponds to one existing classified ExecutionBounds unit (row 21: ≤ 16 refs per page) and does **not** trigger hidden unbounded work — e.g. resolving an unbounded number of `NoteRef`s, constructing a container that grows with note count, deduplicating into an unbounded set, or allocating a vector sized to all notes in the bar. All work in that grain must be structurally capped by the page capacity plus fixed-size incremental accumulators (§ Scratch contract).

### Verification ceilings (not architectural targets)

| Ceiling | Role |
|---|---|
| 100 ms per `PERS,work` line | 5.6 regression guard — fail if any tail slice exceeds |
| 3000 µs outer idle slice | Existing scheduler — tail grains must fit **within** typical outer turns, not expand it |

### Open pin (A1 implementation gate — must appear in first firmware PR description)

1. Confirm `kLsv1SaveIoChunkBytes` (proposed 512).  
2. Confirm ordering-flush batch N (proposed 16).  
3. Confirm gather uses **incremental dedupe** (e.g. bitset or flat `NoteId`/`EventId` seen table with fixed capacity + spill policy) — **not** per-turn `std::map`/`std::set` over full snapshot.

---

## Scratch contract — HARD GATE (pin before A1)

### Requirement

Do not trade a monolithic CPU hang for a **multi-megabyte persistent scratch** on `CurrentWorkspaceSaveJob`.

### What may be retained between turns

| Retain | Rationale |
|---|---|
| **Cursor state** (`LoopSnapshotBodyFormatV1SaveWriteState`) | Minimum to resume |
| **Incremental gather accumulators** with **fixed upper bound** | Note/event records not yet flushed to SD stream; size bounded by grain flush policy |
| **Small SD staging buffer** ≤ `kLsv1SaveIoChunkBytes` | Optional; prefer **direct stream to `StorageIo`** when section framing allows |

### What must NOT be retained whole across turns

| Forbidden | Why |
|---|---|
| Complete `Lsv1GatheredLoopSnapshotPayload` rebuilt each turn | O(snapshot) RAM |
| Full `std::vector<uint8_t>` section buffer before any SD write | O(section bytes) — quantify if unavoidable |

### Footprint budget (PIN before A1)

Implementer must document in A1 gate:

```text
max_persistent_scratch_bytes = cursor_state
                             + incremental_accumulator_cap
                             + optional_staging_buffer
```

**Target for 25-bar / ~6k-node class (`144434`):** persistent scratch **≪** published snapshot bytes (~824 KB nodes on device is **slab** memory, not save-job scratch). Save-job scratch should stay **internal-heap sized** (order **tens of KB**, not hundreds). If streaming cannot avoid a full section buffer, state exact bytes from `measureLoopSnapshotBodyFormatV1Bytes` for the fixture and get explicit approval in A1 gate.

**Preference:** stream `LoopSnapshot` section payload field-by-field in `writeLoopSnapshotPayload` field order (same order as monolithic writer) so SD write grains do not require full section materialization.

---

## Proposed design — Stage A

### State machine

```text
CapturePassHeader / CapturePassChunk
        ↓
EditTail              → writePersistedEditsTail only
        ↓
LoopSnapshotTail      → stepLoopSnapshotBodyFormatV1SaveTail (resumable)
        ↓
payload_done → finalize → done
```

Add `DeferredLoopWriteStage::LoopSnapshotTail` to [`StorageSession.h`](../../include/StorageSession.h).

`EditTail` without LSV1: complete in one slice and advance to finalize (unchanged).  
`EditTail` with LSV1: complete edits tail, set `LoopSnapshotTail`, return `MoreWork`.

### API (mirror load naming)

- `LoopSnapshotBodyFormatV1SaveWriteState` — cursors + phase enum (Gather / OrderFlush / SdWrite)
- `LoopSnapshotBodyFormatV1SaveStepResult` — `MoreWork | Completed | Failed`
- `stepLoopSnapshotBodyFormatV1SaveTail(io, loopSnapshot, state, deadlineUs, maxGrains)`

Monolithic `writeLoopSnapshotBodyFormatV1SaveTail` remains the **byte oracle** for native tests and measure paths only; deferred persist calls the stepped API exclusively.

**Before / after boundary (one root cause):**

```text
before:  EditTail → monolithic LSV1 writer
after:   EditTail → bounded LoopSnapshotTail grains → finalize
```

### Telemetry contract (I-P5)

Explicit stage lifecycle in `SC_PERSIST` detail (SESSION_CAPTURE):

```text
EditTail,enter
EditTail,done

LoopSnapshotTail,enter
LoopSnapshotTail,step,<phase>    # one line per grain (optional: aggregate every K grains)
LoopSnapshotTail,done

payload_done
finalize_slice…
done
```

**Rule:** `write=<StageName>` on `payload_slice` without `,done` suffix means **transition / in-progress**, never completion. Host verifier must key off `EditTail,done` → `LoopSnapshotTail,done` → `payload_done`.

### Files

| File | Change |
|---|---|
| [`include/StorageSession.h`](../../include/StorageSession.h) | `LoopSnapshotTail` stage; save write state scratch fields |
| [`include/StorageLoopIo.h`](../../include/StorageLoopIo.h) | Save step types + stepped API |
| [`src/StorageLoopIoBodyFormatV1.cpp`](../../src/StorageLoopIoBodyFormatV1.cpp) | Stepped gather/write; monolithic path for tests |
| [`src/StorageManager/WorkspaceSave.cpp`](../../src/StorageManager/WorkspaceSave.cpp) | Stage machine |
| [`src/StorageManager/PersistenceWorkItemPersist.cpp`](../../src/StorageManager/PersistenceWorkItemPersist.cpp) | Telemetry stage names |
| [`src/PersistenceSyncDrainBudget.cpp`](../../src/PersistenceSyncDrainBudget.cpp) | Slice step estimates for tail grains |
| [`docs/Authority/Architecture/ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) | Row 14 — structural grain caps |

---

## Stages and commits

| Checkpoint | Scope | Commit policy |
|---|---|---|
| **A1** | Grain + scratch pins recorded; state machine + writer skeleton | **No standalone firmware commit** unless pins-only doc — pins go in plan + gate comment |
| **A2** | Resumable `LoopSnapshotTail` writer complete | Part of **one root-cause commit** |
| **A3** | Native byte-identity + ExecutionBounds update | Same commit or immediate follow-up doc/test commit per user workflow |
| **A4** | Device repro (25-bar + 2 OD, third OD during save) | Verify; stage commit if already shipped |
| **A4.1** | Tail cursor monotonicity telemetry (`tail_step` / `tail_gather_begin`) | Investigation only — prove cursor advances or stalls |

**One root-cause firmware commit** (standing instruction):

```text
Storage: make LSV1 snapshot save tail resumable.
```

A1 without A2 is not a functioning fix (would only add a stage that still calls the monolith).

**Stage B — DEFERRED.** After A4 PASS: reproduce `144434` scenario; if reboot gone, evaluate RC-P2 bundle cap independently.

---

## Verification — Stage C

### Native — primary oracle (byte identity)

```text
snapshot
   │
   ├── monolithic writeLoopSnapshotBodyFormatV1SaveTail()  →  bytes A
   │
   └── stepLoopSnapshotBodyFormatV1SaveTail()
          maxGrains = 1
          repeat until Completed                      →  bytes B

assert A == B
```

This is the **primary native gate** for Stage A: an execution-granularity refactor, not an LSV1 format change. No `maxGrains = 0` on the stepped path; the monolithic writer is the reference — the stepped writer is what ships.

| Test | Assert |
|---|---|
| Byte identity | `A == B` for fixtures including multi-bar / multi-pass |
| Per-grain bound | Each step with `maxGrains = 1` advances exactly one classified grain |
| Scratch cap | Persistent scratch ≤ documented `max_persistent_scratch_bytes` |

### Host

| Assert | Not sufficient alone |
|---|---|
| `EditTail,enter` → `EditTail,done` → `LoopSnapshotTail,enter` → `LoopSnapshotTail,done` → `payload_done` | `payload_done` within K slices after first `write=EditTail` |
| `max LoopSnapshotTail step duration` ≤ classified bound | K-slice heuristic |
| Optional: total tail turns scales with snapshot size (distributed, not lucky single slice) | |

### Device — A4 (primary proof on `144434` workload)

On **25-bar record + 2 overdubs** (same class as `144434`), all three must pass on the **same capture**:

| # | Proof | Required evidence |
|---|---|---|
| **1. Progress** | Full telemetry chain: `EditTail,enter` → `EditTail,done` → `LoopSnapshotTail,enter` → `LoopSnapshotTail,step,…` → `LoopSnapshotTail,done` → `payload_done` → finalize → `done` |
| **2. Bound** | No individual `LoopSnapshotTail` step exceeds the classified execution bound |
| **3. Correctness** | On-device save produces LSV1 bytes identical to monolithic oracle (host verify from captured slot file or native-equivalent fixture) |

Also required: no `#CAPTURE_RECONNECT`; RAM1 ≥ 4000 after build.

HITL `base` (2-bar) = smoke only. 25+2 OD repro = manual or dedicated preset (no canonical stimulus change required for A4).

If all three pass, **RC-P1 is closed.**

### A4.1 — tail cursor monotonicity (2026-09-12) — **CLOSED**

**Trigger:** [`155724`](../../captures/session_20260912_155724.log) + [`160551`](../../captures/session_20260912_160551.log) — reboot fixed; ~61k `LoopSnapshotTail` steps / ~235 s with zero `tail,done`.

**Question:** Does every `stepLoopSnapshotBodyFormatV1SaveTail` grain advance the persistent cursor, or does the writer return `MoreWork` without consuming work / get reset?

**Answer:** Cursor advances monotonically through gather and sort. Completion fails at `WritePayload` — see **RC-P1.2** above.

**Anchor capture:** [`162239`](../../captures/session_20260912_162239.log) (LSV1 gate on; valid tail path). [`161617`](../../captures/session_20260912_161617.log) inconclusive — LSV1 gate off (`loopUsesLoopSnapshotBodyFormatV1ForSave` false); legacy `EditTail` only.

**Telemetry** (SESSION_CAPTURE, sparse):

```text
LoopPersist,tail_gather_begin,bc=<barCount>     # beginLoopSnapshotBodyFormatV1SaveGather (reset)
LoopPersist,tail_step,p=,b=,pi=,pg=,ei=,ni=,nr=,off=,pb=,n=,e=,st=   # every 64 steps, phase change, sig change, stall (64 identical sigs), or done
```

**Do not:** bypass `handleClearTrack` save gate; treat sync-drain stuck as proof the tail is stuck.

### A4.2 — `WritePayload` quota fix — **APPROVED** (2026-09-12)

**Invariant:** A grain that writes exactly `kLsv1SaveIoChunkBytes` payload bytes and stops serialization must return **success** (`MoreWork` or `Completed`), not `Failed`.

**Gate 1 — quota vs I/O failure**

- `sliceQuotaFilled` set **only** when the callback stops because `written >= maxBytes`.
- Real `lsv1IoWrite` failure → `Failed` even if bytes were written earlier in that grain; `byteOffset` must not advance.

**Gate 2 — offset progression and completion**

- After each quota-filled grain, `byteOffset` advances by the actual bytes written.
- Next grain resumes at that offset (re-serialize + skip), not byte zero.
- Final slice may be `< 512 B`; completion exactly once when `byteOffset == total`.

**Implementation** — `writeLoopSnapshotPayloadByteSlice` in [`StorageLoopIoBodyFormatV1.cpp`](../../src/StorageLoopIoBodyFormatV1.cpp): `sliceQuotaFilled` + `ioWriteFailed` flags; quota stop is success when `written > 0`.

**Native gates** — `test_lsv1_payload_byte_slice_quota_and_io_boundaries` + `test_lsv1_stepped_save_tail_large_snapshot_matches_monolithic`:

| Case | Assert |
|---|---|
| Payload `< 512 B` | Single slice, byte identity |
| Payload `== 512 B` | One full slice, offset `0 → 512` |
| Payload `> 512 B` | Monotonic per-slice offsets, each delta `≤ 512`, final remainder `< 512` |
| Injected I/O failure after partial write | `false`, offset unchanged |
| `maxBytes == 0` before complete | `false`, offset unchanged |

Keep `test_lsv1_stepped_save_tail_matches_monolithic` (small fixture).

**Status:** **SHIPPED** — native boundary tests PASS; device PASS on [`164947`](../../captures/session_20260912_164947.log).

**Out of scope:** sort grain efficiency; persist job resume from `writePayloadOffset` on recovery (RC-P1.3); Clear save gate; RC-P2; 6C.

### A4 validation — **PASS** (2026-09-12)

**Anchor:** [`164947`](../../captures/session_20260912_164947.log) — 25-bar record + multi-OD; LSV1 gate on OD3 save (`bc=25`, `n=492`, `e=1067`, `pb=47709`).

| Proof | Evidence |
|---|---|
| Progress | `edit_tail,done` → `tail_gather_begin` (×1) → `loop_snapshot_tail,enter` → `tail_step` p=0…5 → `loop_snapshot_tail,done` → `payload_done` → `slot:4:0,done` → `FinalizeWorkspace,done` → `SAVE,completed` |
| WritePayload offset | p=5: `off` 0 → 512 → … → 47616; 94 grains; 0 `payload_slice failed` |
| Bound | `LoopSnapshotTail` slices ~0.9–1.2 ms; no `#CAPTURE_RECONNECT` |
| Timing | ~80 s `edit_tail,done` → `loop_snapshot_tail,done` (sort-dominated); ~4 ms tail done → `payload_done` |

**RC-P1 closed** (reboot layer A1 + completion layer A4.2). Deferred polish: **§ Post-RC-P1 follow-up bundle**.

---

## Post-RC-P1 follow-up bundle (deferred)

Single backlog queue — **not** in scope for RC-P1 closeout. OpenSpec tracking:
[`immutable-loop-snapshot-passes` §6.6](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md#66-post-closeout-polish-linked-plans--non-blocking)
(**non-blocking** for §6.5 archive). Implement as separate slices after OpenSpec **6.4** closeout unless reprioritized.

| ID | Track | Owner / hook | Problem | Target invariant | Anchor |
|---|---|---|---|---|---|
| **RC-P1.3** | Persist recovery resume | `PersistenceWorkItemPersist` / `WorkspaceSave` job recovery | **SHIPPED** — `loopPersistTailWriteResumeActive` preserves scratch on `LoopPersist` retry | Failed grain retries `WritePayload` from persisted scratch cursor; no full gather replay unless scratch invalid | [`155724`](../../captures/session_20260912_155724.log) anchor |
| **RC-P1-SORT** | LSV1 tail sort grain efficiency | `sortNotesOneGrain` / `sortEventsOneGrain` in `stepLoopSnapshotBodyFormatV1SaveTail` | **SHIPPED** — monolithic `std::sort` one grain per axis | Sort phase wall-clock and step count bounded for 500-note class without changing byte identity | [`164947`](../../captures/session_20260912_164947.log) |
| **GAP-8** | Record-stop bars 2–5 until publish | `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` in `DisplayNoteWindowGather` | **SHIPPED** — display bridge from `unpublishedSnapshot()` during `isPlaybackSessionExitPublicationGap`; no DEC-045 sync publish | Uncovered paint-window bars filled during gap handoff; publish latency independent of LSV1 tail | Native `test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot`; device [`191442`](../../captures/session_20260912_191442.log) stop→publish 0.94 s |

**Dependencies / ordering**

- **RC-P1.3** and **RC-P1-SORT** are independent persist-path slices; either can land first.
- **GAP-8** is display/publication — do **not** fold into RC-P1.3 or sort work; cross-plan with [record_stop_snapshot_publication_gap_display_bugfix.md](record_stop_snapshot_publication_gap_display_bugfix.md) (stage 7 preview parity may share fixtures).
- **Do not:** Clear save-gate bypass; RC-P2 slot-meta debt; reopen RC-P1.2 quota fix.

**Verification (when scheduled)**

| Track | Native | Device |
|---|---|---|
| RC-P1.3 | Inject failed `WritePayload` grain; assert resume from `writePayloadOffset`, single `tail_gather_begin` | Forced-failure HITL or manual repro; no multi-gather replay |
| RC-P1-SORT | Byte identity `A == B` preserved; step-count ceiling fixture for 500-note class | Same 25+OD workload; `loop_snapshot_tail,done` with fewer sort-phase steps / lower wall-clock |
| GAP-8 | Fixture for gap window + post-publish bars 2–5 coverage | Record stop → bars 2–5 visible within agreed bound **or** documented gap + `snap=1` anchor; not correlated with `SAVE,completed` tail |

---

## Explicit non-goals

- **6C / 6C-B / 6B**
- **Stage B / RC-P2** until A4 answers reboot question
- Boot sync load LSV1
- Dropping dual-write oracle
- Blocking overdub during save (follow-up only if slices insufficient)
- New Manager or parallel scheduler

---

## Pre-implementation review

### Ready

- RC-P1 approved with log sequence proof
- Owner identified; load-side `maxGrains = 1` precedent cited
- Stage B explicitly deferred
- 6C boundary preserved

### Resolved

| Topic | Decision |
|---|---|
| I-P1 | Resumability through cursor — not anti-`gather` wording only |
| Grain primary | Classified units; `deadlineUs` = safety stop |
| 100 ms | Verification ceiling only |
| Serialization | Byte-identical to monolithic writer (I-P3) |
| `LoopSnapshotTail` stage | Yes — separate telemetry boundary |
| Commits | One root-cause firmware commit for A2 behavior |
| Stage B | **DEFER** |

### Proceed?

**YES — PROCEED A1.** Implementer posts architecture gate (§ Architecture gate) in session transcript, then records A1 pins in gate comment: `kLsv1SaveIoChunkBytes`, ordering batch N, `max_persistent_scratch_bytes`, and membership-page gate confirmation.

**NO clean Stage A PASS** until native `A == B` byte identity + A4 device repro (progress + bound + correctness).

---

## Session closeout (when shipped)

- [x] [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md) row 14 grain table
- [x] [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) — RC-P1 closed (A4 PASS [`164947`](../../captures/session_20260912_164947.log))
- [ ] [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) § LSV1 save tail stepping + telemetry rule (follow-up hygiene)
- [ ] `DEC-###` if row 14 reclassification requires [ARCHITECTURE_RULES.md](../Authority/ARCHITECTURE_RULES.md) entry
