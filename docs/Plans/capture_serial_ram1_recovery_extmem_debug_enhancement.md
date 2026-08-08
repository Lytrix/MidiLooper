# Capture-serial RAM1 recovery + EXTMEM deferred debug — enhancement

**Kind:** enhancement  
**Date:** 2026-07-04  
**Build env:** `teensy41-capture-serial` (`SESSION_CAPTURE=1`, `PERF_TELEMETRY=1`)

## Problem

Commit `df6c51b` (note-edit overlap/display pairing) pushed `teensy41-capture-serial` over the Teensy RAM1 link limit (~28 KB). `PERF_TELEMETRY` was disabled as a partial workaround; the build still failed to link until note-edit code was moved off ITCM.

## Fix (shipped)

### Track A — FLASHMEM note-edit cold path

- [`include/Utils/NoteEditMem.h`](../../include/Utils/NoteEditMem.h) — `NOTE_EDIT_MEM` → `FLASHMEM` on IMXRT1062
- Applied to function entry points in:
  - [`src/Utils/NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp)
  - [`src/NoteEditFocus.cpp`](../../src/NoteEditFocus.cpp)
  - [`src/Utils/NoteUtils.cpp`](../../src/Utils/NoteUtils.cpp)

**Result:** `pio run -e teensy41-capture-serial` links with ~4 KB RAM1 headroom (with `PERF_TELEMETRY=1`).

### Track B — PSRAM deferred capture ring

- [`src/Utils/DebugSessionCapture.cpp`](../../src/Utils/DebugSessionCapture.cpp) — 96 KB ring via `extmem_malloc`
- Record types: `Revt` (deferred `#CAP,...,REVT`), `Text` (`#DBG`, `PERF,...`)
- Producers: `queueStoredNoteOn`, `appendCaptureTextLine` (FLASHMEM-safe — PSRAM not DMAMEM)
- Consumer: `flushCaptureBuffer(maxRecords)` from RAM in [`src/main.cpp`](../../src/main.cpp) (`SC_CAPTURE_FLUSH(64)`)
- [`src/Utils/HotPathTelemetry.cpp`](../../src/Utils/HotPathTelemetry.cpp) — telemetry state in RAM2 (not DMAMEM); `emitSummary` queues `PERF` line when `SESSION_CAPTURE`
- [`src/Logger.cpp`](../../src/Logger.cpp) — `#DBG` `logger.info` lines deferred via `appendCaptureTextLine`

HITL-critical low-volume lines (`ST`, `PERS`, `RECA`/`RECS`, etc.) remain synchronous `Serial.printf`.

## Ring policy

| Constant | Value |
|----------|-------|
| `kCaptureRingBytes` | 96 × 1024 |
| `kMaxCaptureTextBytes` | 192 |
| Main-loop flush budget | 64 records (`SC_CAPTURE_FLUSH`) |
| Overflow | Drop oldest record; one sync `#CAP,...,RING,overflow` on next flush |

## Verification

```bash
pio run -e teensy41-capture-serial   # RAM1 free > 0
pio test -e native                   # note-edit suites
```

HITL (user device): record/overdub baseline + note-edit baseline — confirm `#CAP`, `REVT`, `ST`, `#DBG`, `PERF` in serial log.

## Constraints

- FLASHMEM code must not access DMAMEM (IMXRT1062). Ring lives in PSRAM; `HitlRevisionCommitBackup` remains DMAMEM in [`src/StorageManager.cpp`](../../src/StorageManager.cpp).
- Do not move MIDI clock / capture append hot paths to FLASHMEM.
