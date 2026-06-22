# Parked — 2026-06-22 (superseded)

**Status:** Parked. Superseded by `openspec/changes/long-record-memory-headroom/`.

## Why superseded

This change treated the >32-bar record crash as a stop-path timing/persistence
bug. HITL evidence (`captures/hitl_record_only_48bar_20260622_203457_serial.log`)
proved the root cause is **RAM2 heap exhaustion that scales with record length**,
present at `record_stop` entry before the save runs:

- 16-bar record-only: `getFreeHeap = 77824` at seal, `PERS,result,...,ok`.
- 48-bar record-only: `getFreeHeap = 4096` at `record_stop` entry; `PERS,dispatch`
  fires, device silent ~30 ms later, no `PERS,result`.

Mechanism: `ExtMemAllocator::allocate()` is `malloc()`-first (RAM2), so
length-scaling buffers fill the 512 KB RAM2 heap before the 8 MB PSRAM. The
successor change re-targets those buffers to PSRAM and adds a RAM2 floor guard.

## What carried forward

Into `long-record-memory-headroom`:

- Open: overdub start after long stop; native + HITL gates; docs closeout
  (see successor `tasks.md` §3–§5).
- Shipped and reused (not reverted): `StorageLoopIo` chunk-stream writer,
  `RECS`/`PERS` stop-path instrumentation, 64-bar reload test.

## What was dropped from scope

- Long-loop display scaling (16-bar window + overview strip, original §6) is deferred to
  **`long-loop-piano-roll-window`** (`openspec/changes/long-loop-piano-roll-window/`).

Working state preserved in checkpoint commit `db26344`.
