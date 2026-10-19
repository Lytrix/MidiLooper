# I4 — boot loop diagnosis: uninitialized capture critical journal

## Outcome

**Verified defect:** `DebugSessionCapture::initCaptureBuffer()` does not initialize the critical journal, which is placed in Teensy RAM2 `.bss.dma` (NOLOAD). Startup clears ordinary `.bss` only. The dump routine trusts journal count/index without validating them against the 32-row capacity.

**High-confidence explanation of this boot loop:** retained/undefined journal metadata is used as an index, producing an invalid string address during the first stopped capture flush. The device faults inside `strnlen` and automatically reboots. Exact hardware caller/LR and journal values were not captured, so this is not a complete register-level causal proof.

No evidence establishes corrupt Set payloads. Do not delete, quarantine, re-save or replace the workspace to work around this defect.

## Hardware evidence

Single passive serial owner, no command/MIDI/reset request, 75 seconds: `captures/i4-boot-loop-passive-01/serial.log` and observation.json. Opening serial can affect USB/device state; no deliberate reset was requested. Five complete CrashReports and five `DIAG,boot_reset,crash=1` markers:

- PC `0x595F2` in every report.
- CFSR `0x82`: DACCVIOL plus MMARVALID.
- Accessed address `0x64D8722D` in every report.
- Automatic reboot after fault/bad interrupt explicitly reported.
- About 13.8–13.9 seconds between recurring USB disconnections; temperatures about37°C.
- Boot reaches Current manifest scan, catalog ensure/reconcile, `BOOT,load,ok`, setup and first DMA census; no `DisplayManager: Boot setup complete` handoff.

This proves a fault loop without host mutation commands. Firmware's existing CrashReport path itself may append crashlog.txt automatically; no host SD mutation was performed.

Cached image SHA256 `ecd4f6713c8977aa0523b1fed10bf858439a16438c8f3112dfe9c20b394e8ec2`, branch feature/unified-rolling-playback-path, HEAD26125ceb plus preserved dirty work. Pinned ELF and HEX both dated2026-10-12 22:56:22; `arm-none-eabi-objcopy -O ihex -R .eeprom` regenerates the cached HEX byte-for-byte (`cmp`). This establishes ELF/artifact consistency, not binary readback from the device.

## Fault mapping / owner trace

`arm-none-eabi-addr2line` and objdump map PC0x595F2 to `strnlen`, specifically `ldrb r1,[r2]`. Disassembly of the entire pinned ELF finds exactly three direct call sites through `__strnlen_veneer`:

1. `journalCriticalLine` at60089ea8: caller-provided line.
2. `emitCriticalJournal` at60089f36: line derived from `sCriticalJournal.lines[dumpIndex]`.
3. `queueCaptureTextLine` at6008ae3c: caller-provided line.

All are DebugSessionCapture paths, not direct SD/Set parsing. Stored journal is at202080a0, size0x1810; first text starts at202080ad. Target arithmetic uses192-byte rows. An unchecked dumpIndex28374338 (0x1b0f542) computes `(202080ad +192*index) mod2^32 ==64d8722d` exactly. This is a feasible counterexample, NOT an observation of the actual hardware index.

`emitCriticalJournal` normally runs from `flushCaptureBuffer` when maxRecords>8. Main uses64 while stopped, including boot. Journal count/dumpIndex/dumpStarted can be stale because:

- `SC_JOURNAL_MEM_ATTR` is DMAMEM on target.
- Linker `.bss.dma` is NOLOAD and is outside `_sbss`–`_ebss`.
- Pinned Teensy startup performs `memory_clear(&_sbss,&_ebss)` and no `.bss.dma` clear.
- Linked ELF has no dynamic constructor for the critical journal.
- `initCaptureBuffer` only ensures the ring and marks boot time; `sessionHeader` calls it at setup end.
- `emitCriticalJournal` loops while dumpIndex<count with no capacity validation. Stale dumpStarted can skip the initial evidence-begin row.

Current native fixture `setUp` explicitly resets `sCriticalJournal = CriticalJournal{}`, masking the missing production boot initialization.

## Reproduced production-init failure

Scratch-only `boot_journal_init_probe.cpp` includes the existing real capture fixture/production TU, substitutes only the native USB boundary and avoids invoking the unsafe dump. Journal initialized to count28374339/index28374338/dropped123/startedtrue, then actual `initCaptureBuffer()` called:

```
after production init: count=28374339 dumpIndex=28374338 dropped=123 started=1
boot initialization oracle: FAIL
probe_exit=1
```

Host compile succeeded; result is behavioral failure, not missing symbol. No repository firmware/test edits. Compiler output, result and disassembly saved in session scratch: boot-journal-init-compile.txt, boot-journal-init-result.txt, boot-diagnosis-disassembly.txt, boot-diagnosis-elf.hex.

## Classification / G0 — evidence verdict

- VERIFIED: repeated data-access fault, exact PC/invalid address, ELF-to-HEX consistency, three capture-only call sites, NOLOAD startup gap, unbounded trusted journal index, poisoned-state production initialization failure.
- ASSUMPTION (high confidence): the hardware caller is emitCriticalJournal using retained RAM2 index rather than one of the other two strnlen callers.
- UNKNOWN: exact retained RAM2 metadata/LR, why earlier boots happened to have valid journal state, any concurrent corruption beyond the initialization defect.
- CONFLICT: no authority conflict for diagnosis; no proof supports blaming/deleting the loaded Set.
- G0 verdict: PASS for establishing the boot-init defect and designing a narrow correction; not implementation/upload approval.

Owner/lifetime: DebugSessionCapture owns the boot-local journal/ring. No persistence/undo/content/slot lease transfer. Boot transition currently fails before interactive handoff. RAM2 journal store already exists; diagnosis adds no target RAM or instrumentation. Existing tests exercise emission/retention but pre-clear journal outside production. Firmware content, SD formats and playback policies are untouched.

## Preferred next action (not implemented)

Explicit boot-local once-only journal initialization before use; repeated init/sessionHeader must preserve current-boot journal evidence. Validate count/index against capacity before deriving a row address, report/refuse corrupt metadata through existing evidence mechanisms rather than dereference it. Prove poisoned startup, repeated-init retention, invalid-index refusal and ordinary journal replay on the real production path; native/defaultcapture build/RAM1 and focused review. Upload and boot qualification need separate approval. No SD cleanup required.

## Corrections / closeout

Earlier preflight note suggested bootheap differed from the qualified image. Direct comparison to run03 disproves that: both report BOOTram1/heap184320 and displayManager20200568/object9736/neighbour20202b70. Those are not evidence of image drift or heap exhaustion. Lack of CrashReport in the earlier short preflights was missing capture, not crash=0.

No host reset/upload/Save/Load/SD-destructive action or Git delivery. Passive reader released; board remains in the fault loop, not claimed stopped/stable. Full playing-load qualification stays blocked. Runtime/task evidence updated.

## Owner-approved repair follow-up

The narrowcapturejournalinit/boundsfix isimplemented/nativeverified/defaultbuilt, candidate8050aabc NOTuploaded.57focused/3098fullnativepass7skip/78host/onefocusedreviewAPPROVE; first-use/reinit/partialreplay/corruptindices/lossreporting oracles verified. No additionaldeviceaction; diagnosis hardwarecallerconfidence/qualificationlimits unchanged. [Repair execution/results](BOOT-JOURNAL-FIX.md).

**Recovery follow-up:** separatelyapproved8050aabc uploadandsettled90smanagedwindowpass with16validquerypairs/no newreset-reconnect-loss; ownernormalstableOLED/no rebootloop. Supportsinitializationdefectcausality, without claimingcapturedhardwareLR/index. NoSet/cardcleanup/Load. [Recovery outcome](BOOT-JOURNAL-RECOVERY.md).
