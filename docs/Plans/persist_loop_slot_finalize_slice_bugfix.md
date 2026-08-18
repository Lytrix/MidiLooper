# Persist LoopPersist finalize slice (MIDI after overdub stop)

**Status:** Relanded — boot **PASS** [`213246`](../../captures/session_20260816_213246.log); PLAYING `persist_save` rem **FAIL** once (206 ms)  
**Date:** 2026-08-16  
**Kind:** bugfix  
**Evidence:** [`031229`](../../captures/session_20260816_031229.log) last-slice→`done` 73–83 ms. Boot FAIL [`032137`](../../captures/session_20260816_032137.log), [`032326`](../../captures/session_20260816_032326.log) on `8abeac6` (`teensy_size` RAM1 locals **2432**, was **6528**). Reverted `baa03e1`. Reland omits stage-name strings and extra `Serial.println` in the CRC grains.

## Device FAIL (first land)

`8abeac6` hung at `BOOT,scan,start` (no `scan,t0`). Title screen. Cause: extra persist telemetry / ERROR string literals in RAM1 `.rodata`, same class as Slice 1 child-span literals. Reland keeps `STORAGE_PERSIST_MEM` grains and does **not** add `deferredLoopFinalizeStageName` or new CRC-path Serial strings. Size check vs pre-reland: RAM1 locals **4512** unchanged (variables 93792; code 424876→424940 absorbed in padding). First land was locals **2432**.

## Invariant

One `processDeferredSaveState` call during PLAYING must not CRC-walk a whole loop temp file.

## Owner

`StorageManager` — `stepFinalizeDeferredLoopSlotTemp` / `stepLoopPersistWorkItem`. No new admit/cancel. Visual-cache `idle_maint` out of scope.

## Device gate

Boot reaches `BOOT,scan,done`. `teensy_size` RAM1 locals stay near the pre-slice margin (not the 2432 FAIL). PLAYING `persist_save` rem after overdub stop stays near the 300 µs slice budget except one 256-byte SD grain. `clockrate` stays ~47.

## Device [`213246`](../../captures/session_20260816_213246.log)

**Boot PASS:** `scan,start` → `t0`…`t7` → `scan,done` → `load,ok`. No title hang.

**CRC grain holding:** six `LoopPersist,loop:0` jobs after overdub stops; median slice spacing ~0.9 ms; each job ends `done`. Not the [`031229`](../../captures/session_20260816_031229.log) last-slice 73–83 ms whole-file CRC.

**persist_save rem FAIL once:** `loop_rem,persist_save` **206560 µs** @ 25.683 s on the first LoopPersist job (between two `slice` lines; BPM 119→85 on that hitch). Later five jobs: no persist_save rem ≥ 50 ms. 5 s DIAG persist_save max after that **15–53 ms**. PLAYING `clockrate` **47–48**. PLAYING `midi_gap` **67–93 ms** tracks `idle_maint` (out of scope). `PERS,result` **73–111 ms** is FinalizeWorkspace wall-clock sum of grains.

No `VCACHE,full`.
