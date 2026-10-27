# P-A execution — step 1: custody receipt, slab and the size re-base

Covers [P-A](../packets/P-A-CUSTODY.md) **step 1 only**. Step 2 (interval-qualified admission, the
Stop input cut, the row-28 grain) is **not** implemented and is now known to be a second-lease change —
see §"Where step 2 actually lands". No device or content action.

## What landed

`include/Track.h`, inside the externally allocated ring (`CaptureStopJobs`), per
[CONTRACTS.md](../CONTRACTS.md) C1.1:

- `CustodyState { Free, Sealed, Retiring }` and the five-bit `CustodyObligation` mask
  (`kCustodyFinalizer`, `kCustodyReader`, `kCustodyPersistence`, `kCustodyUndo`, `kCustodyDisplay`);
- `CustodyReceipt` — `rawRange`, `owner`, `loopId`, `interval`, `frozenCaptureContentGeneration`,
  `loopLengthTicks`, `generation`, `state`, `obligations`, `custodyIndex`, `slot`, `ownsExtent`;
- `static constexpr size_t kCustodyCapacity = 5` and `CustodyReceipt custodies[kCustodyCapacity]`.

`presentations[slot]` remains authoritative. **Nothing acquires or releases an obligation yet**, so
behaviour is unchanged by construction: no call site reads the slab.

## Measured result — the pre-flight's RAM1 half

The existing ring pin rejected the change immediately, which is the pin doing its job. The exact new size
was obtained by relaxing it to a temporary range, reading `size_CaptureStopJobs` from the preserved ARM
probe, then restoring an exact pin:

| Quantity | Before | After |
|---|---:|---:|
| `sizeof(CaptureStopJobs)` (shipping ARM) | 29,704 | **30,304** |
| `CaptureStopJobs` (host) | 42,016 | 42,816 |
| `CustodyReceipt` (shipping ARM) | — | **120** |
| Ring delta | — | **+600** = `5 × 120` exactly |

The re-base is recorded at the pin itself with its reason, per the ring's existing convention
(`Task 3.15g` did the same for the eight presentation records). **External pool only**: the ring is
EXTRAM-allocated at runtime, so `Track` DTCM stays 384 B.

Post-link RAM1 is **identical** to the pre-change baseline:

```text
variables 96,192 / ITCM 393,216 (code 392,120 + padding 1,096) / locals 34,880 | floor: padding >= 840
```

So the slab costs **zero RAM1** and does not approach the 840-byte padding floor — the C2.1 prediction
(`no .bss`/RAM1 growth`) is confirmed on the real type, not on a mirror.

## Verification

| Check | Command | Result |
|---|---|---|
| Shipping build + pins | `pio run -e teensy41-capture-serial` | **SUCCESS**, 43.9 s, RAM1 line above unchanged |
| Full native | `pio test -e native` | **3,454 / 7 skipped / 3,447 passed / 0 failed**, 292.7 s |
| ARM ABI probe | `run-custody-probes.py --out-dir "$PI_SCRATCH_DIR/custody-slab"` | exit 0; table above |
| Behaviour change | none by construction | all 3,447 cases unchanged; five desired profiles still RED |

Not run, and not claimed: any device measurement, stack high-water, or content qualification.

## Where step 2 actually lands

Step 2 as specified is not a mechanical follow-on, and the packet should say so before someone starts it.
Making B's Stop admissible while A finalizes requires the **single finalization lease** to become
per-interval:

- `runtime->captureStopOwner` is one owner, checked as `finalizer_busy`;
- `canonicalCaptureOutputAdmitted()` is one canonical output;
- `stepDeferredRecordRetirement` pops the ring head and consumes that owner.

C1.4 says "one serialized canonical writer, an admission receipt per interval" — that is a genuine change
of who owns the finalization lease, which is the case the workflow says to escalate rather than
improvise. It is still native-verifiable (`IMMEDIATE_HANDOFF_CASE=stop` is the oracle), but it is a
multi-owner redesign, not a predicate edit.

**Recommendation:** land step 2 as its own packet run with the lease conversion named as its subject,
and keep this step's verified boundary.

## HITL boundary

Nothing above needed hardware. The first thing that genuinely does is the **C2.4 stack high-water** for
the 720-byte retirement job (device measurement, task 2.2.8) and the **device timing of the Stop input
cut** once step 2 exists. Both need an uploaded image and a registered stimulus; neither is satisfied by
native counts.
