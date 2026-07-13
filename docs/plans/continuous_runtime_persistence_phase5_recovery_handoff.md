# Handoff — DEC-020 Phase 5 recovery (longest valid prefix load)

**Kind:** handoff  
**Date:** 2026-07-09  
**Branch:** `continuous-saving` @ `b180ef9`  
**Authority:** [DEC-020](../DECISION_LOG.md#dec-020-continuous-runtime-persistence-architecture), OpenSpec [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/)

Load first: [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md), [`ARCHITECTURE-REVIEW.md` § Phase 5](../../openspec/changes/continuous-runtime-persistence/ARCHITECTURE-REVIEW.md), [`tasks.md` § Phase 5](../../openspec/changes/continuous-runtime-persistence/tasks.md), [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

---

## Run here or in a new chat?

| | **Continue in this chat** | **New chat** |
|---|---------------------------|--------------|
| **Best when** | Phase 4 context (mid_pass, seal journal) is still loaded | Clean agent focused only on load/recovery |
| **Recommendation** | Fine for exploration / fixture design | **Prefer new chat** for firmware implementation — paste the agent prompt at the bottom |

Either way: complete the **Phase 5 architecture gate** in chat before the first firmware edit ([`OpenSpec-Phase-Gate.mdc`](../../.cursor/rules/OpenSpec-Phase-Gate.mdc)).

---

## What Phase 5 fixes (one sentence)

After a **power loss or crash mid-capture**, load SHALL reconstruct the **longest valid prefix** of sealed chunks already persisted (authoritative slot file and/or `.sealj` sidecar), quarantine the invalid tail, and boot with a playable partial loop — instead of treating the slot as empty or faulting.

---

## What Phase 5 is **not**

| Topic | Relationship |
|-------|----------------|
| **Phase 4 happy-path boot** | **Done** — full deferred save + clean boot: [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) (`BOOT,load,ok`, deferred restore `4/0`, `DISP` 41472 ticks). Phase 5 targets **incomplete** writes only. |
| **DEC-023 capture-commit owner** | Orthogonal — stop-path commit FSM; do not refactor `commitCapturePass` / `finalizeLoopAtStop` in this phase. |
| **M5 extmem adopt-on-load** | Shipped (`adoptPersistedSnapshot`) — reuse; Phase 5 changes **what bytes qualify as loadable**, not clone routing. |
| **Phase 6 archive** | Blocked on Phase 5 native gate + remaining Phase 6 checklist (`oldestDirtyChunkAge`, `/opsx:archive`). |

---

## Prerequisites (shipped)

| Phase | Evidence |
|-------|----------|
| **0–3** | Diagnostics, chunk lifecycle, persistence queue, cooperative scheduler — native + 16-bar HITL (`f0ee520`) |
| **4 mid-pass writer** | `stepMidPassChunkPersist` → `MidiLooper/current/slots/loop_TT_SS.sealj` ([`MidPassChunkPersist.cpp`](../../src/StorageManager/MidPassChunkPersist.cpp)) |
| **4 HITL 64+64** | [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) — 282× `PERS,mid_pass`, 9× `PERS,result,...,ok` |
| **4 journal format** | [`include/MidPassSealJournal.h`](../../include/MidPassSealJournal.h) — `LMSJ` v1 file header + per-chunk record header + `MidiEvent[]` |
| **4 journal lifecycle** | Appended during capture; **removed** on successful deferred save ([`finalizeDeferredLoopSlotTemp`](../../src/StorageManager/WorkspaceSave.cpp) → `removeLoopSlotSealJournal`) |

**Gap:** There is **no read path** for `.sealj` today. `readPersistedLoopSnapshot` is all-or-nothing; any truncated tail returns `false`.

---

## Current load behavior (baseline to change)

```text
loadLoopSlotFromCurrentSetSd
  → verifySaveFileTokenAtPath (missing token → empty slot)
  → readLoopFromCurrentSetFile
       → readPersistedLoopSnapshot (fail on any parse error)
  → on failure: resetLoopSlotToEmpty + WARN log
```

Relevant paths:

| Concern | File |
|---------|------|
| Slot load orchestration | [`StorageManager.cpp`](../../src/StorageManager.cpp) `loadLoopSlotFromCurrentSetSd`, `readLoopFromCurrentSetFile` |
| Wire parse | [`StorageLoopIo.cpp`](../../src/StorageLoopIo.cpp) `readPersistedLoopSnapshot`, `readCapturePassSlotFileHeader` |
| Adopt without flatten | [`Loop.cpp`](../../src/Loop.cpp) `adoptPersistedSnapshot` |
| SAVE token / quarantine | [`CurrentSetStorage.cpp`](../../src/CurrentSetStorage.cpp), `quarantineStorageFile` in [`WorkspaceSave.cpp`](../../src/StorageManager/WorkspaceSave.cpp) |
| Seal journal paths | `formatLoopSlotSealJournalPath`, `removeLoopSlotSealJournal` |

**Normative behavior** ([`persistence-lifecycle` spec](../../openspec/changes/continuous-runtime-persistence/specs/persistence-lifecycle/spec.md)):

> Recovery SHALL treat the **longest valid prefix** in **seal order**.

**Worst-case loss** ([`design.md`](../../openspec/changes/continuous-runtime-persistence/design.md)): ≤ one unsealed **recording** chunk (not yet sealed at crash).

---

## Architecture gate (mandatory before firmware)

| Question | Required answer |
|----------|-----------------|
| **Ownership change?** | **YES** — load path owns prefix reconstruction (document in gate post) |
| **State transition change?** | **YES** — partial pass visible after crash (loop may be shorter than last live session) |
| **Formal trigger?** | **YES** — full [`PREFLIGHT.md`](../templates/PREFLIGHT.md) or PR equivalent |
| **DEC-021 boot defer?** | **Amended** — queue all SD loop payloads cooperatively at boot; still one slot per `processDeferredLoopSlotRestore` idle slice |
| **DEC-022 bundle tail?** | Verify `runtime.bundle.bin` temp / append cursor with partial slot prefix |
| **DEC-023 conflict?** | **NO** — do not move capture commit ownership |

Checkpoint both architecture-checkpoint questions: **ownership YES**, **transitions YES** — expected; proceed only after user-approved Phase 5 scope.

---

## Design constraints (locked)

1. Reuse `readLoopPersisted` / `readPersistedLoopSnapshot` **where practical** — behavior normative, on-disk layout flexible.
2. Seal-order prefix: journal records carry `sealSequence`; reject out-of-order or corrupt tail records only.
3. Quarantine invalid tail via existing SAVE-token / `quarantineStorageFile` patterns — do not leave torn files on the load path.
4. **No** full `validateAndCleanupMidiEvents` on load hot path beyond existing policy ([`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)).
5. Native fixture **before** optional HITL crash smoke.

---

## Suggested execution order

### Step 0 — Native fixture design (no firmware)

Add tests under [`test/test_storage_loop_io/`](../../test/test_storage_loop_io/) (or dedicated `test_mid_pass_recovery` if cleaner):

| Fixture | Intent |
|---------|--------|
| **Truncated `.sealj`** | Valid `LMSJ` header + N complete records + partial last record → load recovers N chunks |
| **Truncated loop slot** | Complete passes prefix + torn chunk stream → longest pass prefix or fail-safe empty (specify per pass boundary) |
| **Orphan journal + incomplete slot** | Deferred save never finalized (no SAVE token on slot) but `.sealj` has sealed prefix → recover from journal |
| **Seal order violation** | Record with `sealSequence` gap → tail quarantined, prefix kept |

Use in-memory `MemoryStorageIo` patterns from existing `test_truncated_edit_tail_fails_read` — extend toward **prefix success**, not only hard fail.

Run: `pio test -e native -f test_storage_loop_io`

### Step 1 — Read path for `.sealj`

New helpers (names illustrative — match action+scope vocabulary):

- `readMidPassSealJournalPrefix(...)` — parse records until first incomplete/corrupt tail
- Map records → `ChunkIdList` / pool adoption consistent with `adoptPersistedSnapshot`

Files: new `StorageLoopIo` or `StorageManager` module; [`MidPassSealJournal.h`](../../include/MidPassSealJournal.h) unchanged unless v2 required.

Emit optional `#CAP,PERS,recover,...` diagnostics for HITL (Phase 0 style).

### Step 2 — Integrate into slot load

In `loadLoopSlotFromCurrentSetSd` / `readLoopFromCurrentSetFile`:

1. Try authoritative slot file with SAVE token (existing path).
2. On missing token or parse failure: attempt `.sealj` prefix merge.
3. If partial success: build `PersistedLoopSnapshot` with prefix only; quarantine torn slot and/or journal tail.
4. If no valid prefix: existing empty-slot behavior.

**Do not** block boot — one cooperative slice per slot restore (DEC-021).

### Step 3 — Quarantine policy

| Artifact | Action |
|----------|--------|
| Torn `loop_TT_SS.bin` | `quarantineStorageFile` or slot-specific rename (match existing `*.bad` patterns) |
| Torn `.sealj` | Truncate or rename after successful prefix read |
| Workspace meta inconsistent with recovered slots | Log + optional workspace quarantine (narrow scope — avoid whole-workspace wipe unless existing policy requires) |

Reference: [`scripts/quarantine_workspace_serial_boot.py`](../../scripts/quarantine_workspace_serial_boot.py) for manual recovery loops during HITL (not Phase 5 implementation).

### Step 4 — Guide + OpenSpec closeout

- Update [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) § Recovery with prefix algorithm and quarantine rules.
- Check off [`tasks.md` § Phase 5](../../openspec/changes/continuous-runtime-persistence/tasks.md) + ARCHITECTURE-REVIEW implementation review.
- Update [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../runtime/PROJECT_STATE.md).

### Step 5 — Verification

| Gate | Command / artifact |
|------|-------------------|
| Native | `pio test -e native` (full suite) |
| Phase 5 fixture | New tests green |
| Optional HITL smoke | User: kill power mid-64+64 overdub, reboot, capture serial — prefix playable; share log |

**Baseline captures (do not regress):**

- Full save boot: [`session_20260709_171951.log`](../../captures/session_20260709_171951.log)
- Full 64+64: [`session_20260709_171043.log`](../../captures/session_20260709_171043.log)

---

## Primary files (expected touch list)

| Area | Path |
|------|------|
| Load orchestration | `src/StorageManager.cpp` |
| Wire format / snapshot | `src/StorageLoopIo.cpp`, `include/StorageLoopIo.h` |
| Journal read | `src/StorageManager/` (new or `MidPassChunkPersist.cpp` sibling) |
| Paths / quarantine | `src/CurrentSetStorage.cpp`, `src/StorageManager/WorkspaceSave.cpp` |
| Adopt | `src/Loop.cpp` (`adoptPersistedSnapshot` — extend only if needed) |
| Tests | `test/test_storage_loop_io/` or `test/test_mid_pass_recovery/` |
| Docs | `docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md` |

**Protected:** Do not edit capture stop / commit paths (`Track::stopRecording`, `commitCapturePass`, `finalizeLoopAtStop`) unless a formal DEC-023 scope merge is approved.

---

## Phase 5 done when

- [ ] `readPersistedLoopSnapshot` or successor recovers longest sealed-chunk prefix (native fixture)
- [ ] Invalid tail quarantined; boot does not fault
- [ ] Loaded slot chunk refs match prefix only; loop plays at reduced length
- [ ] `pio test -e native` green
- [ ] `RUNTIME_STORAGE_AND_PERSISTENCE.md` § Recovery updated
- [ ] `tasks.md` Phase 5 checked; ARCHITECTURE-REVIEW § Phase 5 implementation review APPROVE

---

## Agent prompt (new session)

```text
Implement DEC-020 Phase 5 from docs/plans/continuous_runtime_persistence_phase5_recovery_handoff.md.

Branch: continuous-saving. OpenSpec: openspec/changes/continuous-runtime-persistence/

Before firmware: post Phase 5 architecture gate from ARCHITECTURE-REVIEW.md (ownership + transition = YES, expected).

Order: native fixtures first → .sealj prefix read → integrate loadLoopSlotFromCurrentSetSd → quarantine → docs → pio test -e native.

Do not touch DEC-023 capture commit / stop paths. Reuse adoptPersistedSnapshot.

Baseline boot captures must keep working: session_20260709_171951.log (full restore), session_20260709_171043.log (64+64).
```

---

## References

| Doc | Role |
|-----|------|
| [`openspec/changes/continuous-runtime-persistence/design.md`](../../openspec/changes/continuous-runtime-persistence/design.md) | Recovery invariant |
| [`openspec/changes/continuous-runtime-persistence/specs/persistence-lifecycle/spec.md`](../../openspec/changes/continuous-runtime-persistence/specs/persistence-lifecycle/spec.md) | Seal-order prefix |
| [`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md) | SD layout, SAVE tokens |
| [`docs/plans/m5_sd_load_extmem_routing_handoff.md`](m5_sd_load_extmem_routing_handoff.md) | Adopt-on-load (orthogonal) |
| [`test/test_storage_loop_io/test_storage_loop_io.cpp`](../../test/test_storage_loop_io/test_storage_loop_io.cpp) | Existing truncate-fail tests |
