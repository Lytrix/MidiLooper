# Prepare-algorithm decomposition — after Slice 1A measure FAIL

**Status:** **Open.** Slice D1 native + device PASS ([`221655`](../../captures/session_20260826_221655.log)). Next is D2 design session. Do **not** start owner-boundary Slice 1B.
**Kind:** investigation
**Parent:** [`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md)
**GitHub:** [#38](https://github.com/Lytrix/MidiLooper/issues/38). Work: [Work](https://github.com/users/Lytrix/projects/1)
**Evidence:** [`213537`](../../captures/session_20260826_213537.log) (1A FAIL / F8), [`221655`](../../captures/session_20260826_221655.log) (D1 PASS)
**Bound authority:** [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) + [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)

1A asked whether existing prepare can become a cooperative job without changing its
semantics. Native resumability for F2 / F3 / F5 passed. Device measure on `213537`
**failed** the gate. This investigation owns the FAIL path: decompose the remaining
indivisible work, and stop F8 from wiping completed units during a normal STOPPED
browse.

**Do not:** start 1B (60% timer); add playback-window consume; treat
`DIAG,lcr,mat=` / `pair,tot=` as the 1A pass condition; recapture for those old
completion lines.

---

## 1A measure gate (closed — FAIL)

| Question | `213537` result |
|---|---|
| Known max unit cost? | **No.** `idx` grows with `ev` (260 µs at 8 → 2446 µs at 704). First-of-chunk 271–3720 µs. |
| Resume from last unit? | **Inside one attempt, yes.** Across `reset,dirty`, **no.** |
| F1–F7 converted or named? | **Named.** Device only ran `idx` / `chunk` / `pair`. Never left `IndexCommit`. |
| STOPPED avoids F8 wipe? | **No.** `DIAG,lcr,reset,dirty` at 25.080 s wiped pass 0 `ev=720`. |

Authorized 1A instrument: `DIAG,prepare,slice,<step>,us=,units=,resume=,indiv=`.
That is the measurement. Completion totals are not.

---

## D0 pin — why attempt 1 died

Last slices before the wipe (`24.930–24.942 s`):

```text
prepare,slice,idx,  us=1397, units=8, resume=720
prepare,slice,chunk, us=27,  units=8, resume=216
```

Still `Phase::IndexCommit` on pass 0. Next cooperative unit would have been more
`chunk` / `pair`, then the named F1 blockers `nsort` / `isort`.

Then:

| Stamp | Line |
|---|---|
| 25.000 s | `VCACHE,stale_all` `notes=0` `total=0` `dirty=1` |
| 25.003 s | `DISP,0,STOPPED,0,0,0,0,0,0` |
| 25.003 s | `SAVE,pending` |
| 25.080 s | `DIAG,lcr,reset,dirty` |
| 25.839 s | `DISP,5,STOPPED,3072,…` — other track |
| 26.026 s | prepare restarts `idx` `ev=0` |

Earlier the same session painted track 0 at `50688` ticks / 1429 notes
(`DISP,0,STOPPED,50688,…` at 20.319 s). At 25.003 s track 0’s **active loop** is
empty (`loopLengthTicks=0`).

`DeviceGateSession` is one global session. `Track::processDeferredContentResolutionDeviceGate`
runs only on the selected track. When that track’s active loop has no committed
passes or `loopLengthTicks == 0`, it calls `deviceGateReset()`. Selecting an empty
slot on track 0 therefore wipes prepare progress that belonged to a different
loop on the same track.

`deviceGateDirtyPolicy` is not the wipe here. Same-length dirty would have logged
`skip,dirty`. The log is `reset,dirty`, from the empty-loop branch in
`processDeferredContentResolutionDeviceGate`.

Resume across PLAYING **did** work in the same capture: prepare paused at
`ev=200` while tracks flickered PLAYING (`19.112–20.227 s`) and continued at
`ev=200` after STOPPED. F8 is slot/loop switch, not transport.

---

## Slices

```text
D0  pin 1A FAIL + F8 wipe owner          shipped (this document)
D1  bind session to track + loop index   shipped — device PASS 221655
        empty / other loop must not reset
D2  F1 IndexCommit nsort / isort         ← next (design session before firmware)
D3  remaining named blockers F4 / F6 / F7
```

**D1 invariant:** a DeviceGate session begun for track T loop L is reset only
when **that same** loop is cleared, length-mismatched under Reset policy, or
explicitly abandoned. Selecting another loop, or an empty slot, parks the
session.

D1 implementation: `deviceGateBegin` stores track index + loop index.
`processDeferredContentResolutionDeviceGate` returns without reset when
`deviceGateMatchesLoop` is false. Native:
`test_device_gate_other_loop_does_not_match_session` PASS. Firmware RAM1
locals **4640**.

### D1 device PASS — `221655`

Clock off. One prepare attempt on track 0 / 50688 ticks / 1429 notes.

| Stamp | Fact |
|---|---|
| 18.849 s | first `prepare,slice,idx` `resume=0` |
| 112.595 s | last slice before switch: `chan` `resume=2560` |
| 112.708 s | `DISP,1,STOPPED,50688` (194 notes). **0** slices on track 1. **0** `reset,dirty`. |
| 117.372 s | `DISP,0,STOPPED,50688` (1429 notes) |
| 117.462 s | `skip,dirty` + `incomplete,reason=dirty,step=chan` — F10 visual-cache dirty, not F8 |
| 119.077 s | `chan` `resume=2568` — continues 2560, not `idx ev=0` |
| 178.738 s | `prepare,slice,state` then `DIAG,lcr,mat=` / `pair,tot=` (`Complete`) |

`mat=` / `pair,tot=` are not the 1A pass condition. They only show this
attempt reached `Complete` after the parked switch.

Device-measured indivisible costs on this loop (not a 1A pass; 1A still has
no known max on `idx`):

| Step | us | indiv |
|---|---|---|
| `idx` | 14–**9296** | 0 |
| `nsort` | **2660** | 1 |
| `isort` | **6573** | 1 |
| `win` | **14423** | 1 |
| `dedup` | **14899** | 1 |
| `spans` first | **7205** | 0 |
| `sort` | **2404** | 1 |
| `csize` | **3334** | 1 |
| `ckpt` | 9–234; eight `0→1424` walks | 0 |

**D2** is not authorized as firmware until a design session. `sortAndUniqueByNoteId` /
`sortTickEventEntriesByTick` are whole-vector operations. Subdividing them is
algorithmic decomposition, not another yield-around-sort.

**Investigation next:** D2 design session for F1. Do not start 1B.

---

## Owners

| Behavior | Owner |
|---|---|
| Prepare session + cursors | `DeviceGateSession` in `LoopContentResolution` |
| Idle queue / interrupt | `Track::maybeQueueContentResolutionDeviceGate`, `Track::processDeferredContentResolutionDeviceGate` |
| Dirty same-length skip | `deviceGateDirtyPolicy` — unchanged by D1 |

Reuse: add track index and loop index on the existing session. No new owner.

---

## Out of scope

- Slice 1B operating-target timer
- Horizon consume / Ring A / RC4
- Cooperative `NoteUtils::reconstructDisplayNotes` (4b.1b, parked)
- Making `DIAG,lcr,mat=` / `pair,tot=` reliable
- Per-loop parallel sessions (one global session remains)
