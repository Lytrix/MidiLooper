# Handoff — M5 SD load path extmem routing

**Kind:** handoff  
**Date:** 2026-07-07  
**Branch:** `derived-note-overlap-logic` (HEAD `cdd9c2b` + **large uncommitted diff** — M1–M4 firmware + docs)  
**Authority:** [DEC-019](../DECISION_LOG.md#dec-019-sd-load-path-extmem-routing-m5-spike), [DEC-018](../DECISION_LOG.md#dec-018-admission-current-heap-derived-rep-consolidation), [DEC-016](../DECISION_LOG.md#dec-016-runtime-architecture-four-layer-model)

Load first: [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md), [`PROJECT_STATE.md`](../runtime/PROJECT_STATE.md), OpenSpec [`spike_sd_load_extmem_routing.md`](../../openspec/changes/runtime-derived-representation-heap/spike_sd_load_extmem_routing.md), [`tasks.md` § M5](../../openspec/changes/runtime-derived-representation-heap/tasks.md).

---

## Run here or in a new chat?

| | **Continue in this chat** | **New chat** |
|---|---------------------------|--------------|
| **Best when** | You want implementation to start immediately; context from M1–M4 investigation is still loaded | Context is getting long; you want a clean agent focused only on M5 |
| **Risk** | Summarization may drop nuance (quarantine timing, reverted tier-A Serial changes) | Agent must re-read handoff + spike; may miss uncommitted-work warning |
| **Recommendation** | Fine for **Step 0 (quarantine + HITL re-test)** only | **Prefer new chat** for **Step 1–3 (firmware M5)** — paste the agent prompt below |

Either way: agent **must** read this file + spike doc before coding.

---

## What M5 fixes (one sentence)

M2 moved **runtime** published flat to extmem; **SD load / undo restore** still flattens each pass through internal-heap `MidiEventVec` in `deepCloneChunkRefs`, then eagerly rebuilds display on boot — leaving 0 bytes RAM1 after a failed 64+64 run and blocking clear / M4 re-test.

**Not M5:** missing `#CAP,ST,OVERDUBBING,PLAYING` on first 64+64 fail — that is M3 capture ring (already shipped in uncommitted tree). M5 unblocks **re-test after failure** and **boot reload** of long loops.

---

## Architecture checkpoint (pre-implementation)

Answer before landing a patch:

| Question | M5 answer |
|----------|-----------|
| **Ownership change?** | **Maybe** — if shallow chunk share replaces deep clone, undo snapshot immutability contract must be confirmed. Default safe path: extmem flatten in `deepCloneChunkRefs` only (no ownership move). |
| **State transition change?** | **Yes, minor** — `restorePassesSnapshot` stops calling sync `rebuildVisualCacheFromPasses`; visual cache becomes dirty until idle maintenance (Phase C policy). Display may be stale briefly after load — acceptable per DEC-016. |

If shallow chunk share is chosen → **design session** with user first. If extmem clone + defer visual rebuild only → **proceed** as minimal bugfix patch.

---

## Uncommitted work (important)

Working tree includes **M1–M3 shipped code** not yet on `cdd9c2b` HEAD:

- `StorageManager.cpp` — admission current-heap; boot `!QUARANTINE_WORKSPACE`
- `Loop.cpp` / `Loop.h` — M2 extmem published flat
- `Track.cpp` — M3 pre-stop flush (tier-A Serial fallback **reverted** per user)
- `DebugSessionCapture.cpp` — tier-A direct Serial **reverted**
- OpenSpec M5 spike docs, DEC-019

**Before M5 firmware:** build with `pio run -e teensy41-capture-serial`. Do **not** re-add tier-A Serial bypass or HITL firmware-text fallback (broke heap / boot).

---

## Execution order

### Step 0 — Break recovery reload loop (hardware, no firmware change required if quarantine already flashed)

Device may reload broken 64+64 state from `MidiLooper/recovery/checkpoints/_*` even when `current/` is cleared.

1. Upload capture-serial firmware (if not already):
   ```bash
   pio run -e teensy41-capture-serial -t upload
   ```
2. Immediately after reset, run quarantine (USB must be up during boot listen ~3s):
   ```bash
   .venv/bin/python scripts/quarantine_workspace_serial_boot.py --port /dev/cu.usbmodem154944801
   ```
3. Reboot; confirm serial contains either:
   - `Boot recovery chain exhausted; starting empty.` — **clean**
   - or successful load with sane heap (if you intentionally keep data)

**Alternative:** `-D BOOT_QUARANTINE_WORKSPACE=1` in `platformio.ini` for one upload (auto-quarantine every boot) — remove after clean gate.

**Host SD script** (`scripts/quarantine_dirty_current_set_sd.sh`) only works if SD mounts on Mac (`/Volumes/...`) — often fails; prefer serial boot command.

---

### Step 1 — `deepCloneChunkRefs` → extmem (do first)

**File:** `src/Loop.cpp` ~134–145

**Change:** Replace `MidiEventVec flat` with `SessionMidiEventVec flat` (or route through existing extmem-first helper used by M2 merge paths).

**Callers (verify after edit):**

- `deepCloneRecordPass` / `deepCloneOverdubPass` / `deepClonePasses`
- `Loop::restorePassesSnapshot` (SD load)
- `Loop::shareForSnapshot` (undo push)

**Open question (resolve before shallow-share shortcut):** Can snapshot restore **share** chunk IDs instead of re-flatten? If unsure → extmem flatten only.

**Guide:** [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — undo `restoreFromSnapshot` always `cloneShared()`.

---

### Step 2 — Defer visual rebuild on load

**File:** `src/Loop.cpp` `restorePassesSnapshot` ~534–553

**Change:** Remove synchronous `rebuildVisualCacheFromPasses()` at end of restore. Keep:

```cpp
discardPassesMaterializedCache();
markDisplayCachesStale();  // visualCacheDirty = true; idle slice rebuild later
```

**Also check:** `TrackUndo.cpp` paths that call `rebuildVisualCacheFromPasses()` after restore — undo restore may still need sync rebuild for edit UX; scope change to **SD load / boot only** if undo needs immediate display.

**Invariant:** Phase C idle bar-slice in `Track::processDeferredIdleMaintenance` must eventually rebuild; no PLAYING-entry full materialize.

---

### Step 3 — Evaluate lazy slot load (optional, same or follow-up session)

**File:** `src/StorageManager.cpp` boot load — `loadCurrentSetFromDirectory` / `loadLoopSlotFromCurrentSetSd`

Load active track/slot first; defer other slots until selected or idle. Reduces boot peak when 8×8 set has multiple long loops.

**Skip** if Step 1–2 alone restore enough heap for M4 gate.

---

### Step 4 — Native test

Add or extend suite: restore persisted 64-bar two-pass fixture via `restorePassesSnapshot`; assert internal heap delta **bounded** (not × pass count × full loop on internal heap).

```bash
pio test -e native
```

Target: 473+ PASS (current baseline 473/473).

---

### Step 5 — HITL M4 gate (after clean boot)

**Canonical 64+64 command** (track 2 / slot 1):

```bash
.venv/bin/python scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 2 --loop-slot 1 --midi-channel 2 \
  --record-bars 64 --overdub-bars 64 --second-overdub-bars 0 \
  --clear-before-record --start-transport \
  --stop-after-overdub --no-undo-redo-after-overdub-stop \
  --boot-settle-ms 8000 \
  --record-stop-min-free-ram2-warn-bytes 12288
```

**With serial capture** (recommended): external `capture_session.py` + `--verify-serial-log`.

**Pass criteria:** 64-bar record-only already PASS (`20260707_171500`). 64+64 needs `#CAP,ST,OVERDUBBING,PLAYING`, overdub MIDI boundaries, transitions — see [`HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc) (adapt bar counts).

**After PASS:** mark M4 + M5 tasks in OpenSpec `tasks.md`; `/opsx:archive` for `runtime-derived-representation-heap`.

---

## Acceptance checklist

- [ ] Step 0: quarantine → clean boot or confirmed empty workspace
- [ ] `deepCloneChunkRefs` uses extmem-first flat
- [ ] `restorePassesSnapshot` does not sync full visual rebuild on SD load
- [ ] `pio test -e native` PASS
- [ ] 64+64 HITL track 2/slot 1 PASS (or documented remaining M3 ring issue separate from clear/block)
- [ ] Post-failure clear succeeds (heap > floor)
- [ ] Update `CURRENT_WORK.md`, `tasks.md`, DEC-019 status if implementation ships

---

## Key files

| Concern | Path |
|---------|------|
| Pass clone (M5 primary) | `src/Loop.cpp` `deepCloneChunkRefs`, `deepClonePasses` |
| SD restore | `src/Loop.cpp` `restorePassesSnapshot` |
| SD I/O | `src/StorageLoopIo.cpp`, `src/StorageManager.cpp` boot recovery |
| Undo restore | `src/TrackUndo.cpp` |
| Visual defer | `src/Track.cpp` `processDeferredIdleMaintenance` |
| Quarantine | `scripts/quarantine_workspace_serial_boot.py`, `StorageManager.cpp` boot listen |
| OpenSpec | `openspec/changes/runtime-derived-representation-heap/` |
| Analysis | `docs/plans/64bar_regression_commit_analysis_enhancement.md` |

---

## Parked (do not start in M5 session)

- Tier-A direct `Serial.println` when ring full (reverted — heap regression)
- HITL firmware-text fallback for missing ST lines
- Commit bisect / save-bypass gates (DEC-017)
- Lazy slot load if Step 1–2 sufficient
- NOTE_EDIT / UIP overlap geometry

---

## Evidence index (2026-07-07)

| Run | Result | Notes |
|-----|--------|-------|
| `20260707_171500` | 64-bar record-only **PASS** | heap_before=81920 |
| `20260707_171942` | 64+64 **FAIL** | missing ST line; device overdub stopped |
| Later runs | Clear **FAIL** | `Clear aborted: could not complete deferred save first`, heap_floor |

Broken loop SD path: `MidiLooper/current/slots/loop_01_00.bin` (track 2 slot 1) + recovery checkpoints.

---

## Agent prompt (copy to new chat)

> Implement OpenSpec M5 from `docs/plans/m5_sd_load_extmem_routing_handoff.md`.
>
> 1. Read handoff + `openspec/changes/runtime-derived-representation-heap/spike_sd_load_extmem_routing.md`.
> 2. Architecture checkpoint: extmem `deepCloneChunkRefs` + defer visual rebuild on `restorePassesSnapshot` only — ask before shallow chunk share.
> 3. Do **not** re-add tier-A Serial bypass or HITL ST fallback (reverted).
> 4. Run `pio test -e native` after firmware edits.
> 5. If user confirms hardware: quarantine via `scripts/quarantine_workspace_serial_boot.py`, then 64+64 HITL per handoff Step 5.
>
> Branch `derived-note-overlap-logic`; large uncommitted M1–M4 diff — build `teensy41-capture-serial`.

---

## Agent prompt (continue in this chat — shorter)

> Continue M5 from `docs/plans/m5_sd_load_extmem_routing_handoff.md`: Step 0 quarantine if needed, then Step 1–2 firmware, native tests, offer 64+64 HITL.
