# Overdub paint — STOPPED frame uses the already-resolved closed Off

**Status:** Host in tree. Occupy-paint verifier **PASS** [`172213`](../../captures/session_20260905_172213.log) / [`172322`](../../captures/host_midi_hitl_occupy_paint_20260905_172322.json): `rc1b.mismatches=[]`. Process exit 2 is baseline `persistence_stage_missing:dispatch` only.  
**Date:** 2026-09-05  
**Kind:** bugfix  
**Parent:** [`overdub_paint_effective_frame_parity_bugfix.md`](overdub_paint_effective_frame_parity_bugfix.md) RC1d-d  
**Evidence:** [`165050`](../../captures/session_20260905_165050.log) `52@2179` F=13 / E=R=14; `48@2564` F=43 / E=R=44  
**Does not authorize:** `endTick+1` rewrite; changing RC1b skip; events; reconstruct pairing; occupy resolve

---

## Debugging boundary

```text
occupy / capture Off                 ← trust (SEVT N→F already closed)
sealed / preview inclusive end = Off ← product display end
playhead tail end = currentTick      ← paint copy only (Stage 1j)
STOPPED RC5a handoff                 ← this slice: copy sealed Off for this-pass
RC1b prefix remnant                  ← keep (`154310` 143 vs sealed 576)
```

## Problem

Verifier on [`165050`](../../captures/session_20260905_165050.log):

| Key | reconstruct | effective | frame | SEVT |
|-----|-------------|-----------|-------|------|
| `52@2179` | 14 | 14 | **13** | `N,2179` → `F,2193` |
| `48@2564` | 44 | 44 | **43** | `N,2564` → `F,2608` |

Occupy already resolved the source notes. The leftover keys are incoming Adds. Events and effective already have the Off. Frame is one tick short of that Off.

RC1d-c only rewrote tails at `stopPhase` / `stopPhase-1`. Mid-pass closes never match that guard.

`endTick+1` is not the product end. The product end is already Off (`applyCaptureEventToPreview`, reconstruct `exclusiveEndForLoopOff(Off)+displayInclusiveEndTick`).

## Invariant

```text
live OVERDUBBING paint may grow open notes to currentTick
liveDisplayNotes stays at capture-preview / sealed closed Off
this-pass STOPPED rows copy that Off; RC1b remnants stay
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — stop-path walk of live notes × sealed preview |
| Reuse | **YES** — Stage 1j compose-base restore + `realizeClosedCaptureOffOnStopPlayheadTails` |

## Implementation

- Draw-source compose-base count is `liveDisplayNotes.size()`, not the paint-copy size.
- Live-capture restores all capture-preview rows onto compose base (open and closed).
- `refreshViewportAfterOverdubStop` clears `liveDisplayPendingPaintNotes_` and copies sealed closed Off for this-pass suffix rows (`thisPassBegin` from `liveDisplayCommittedPrefix_`). Same nonzero noteId also matches wrap-promoted this-pass rows.
- `stopPhase` / `end+1` discriminant removed.

## Host pins

[`test/test_display_window_utils/test_display_window_utils.cpp`](../../test/test_display_window_utils/test_display_window_utils.cpp):

- `165050` `52@2179` live tail 2192 / sealed Off 2193 → frame end **2193**
- `165050` `48@2564` live tail 2607 / sealed Off 2608 → frame end **2608**
- `153848` still 2231
- `154310` still 143

## Pre-implementation review

### Ready

- `DisplayWindowUtils::realizeClosedCaptureOffOnStopPlayheadTails`
- `DisplayManager::refreshViewportAfterOverdubStop`
- `resolveDisplayNotesByDrawSource` compose-base count
- Stage 1j restore in `resolveDisplayNotesLiveCapture`

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Product end | sealed / preview Off — not `end+1` |
| Discriminant | this-pass suffix after committed prefix; same nonzero noteId |
| `154310` | prefix remnant stays |
| Leak | paint size as compose base; open-only preview restore; uncleared pending paint |

### Open before coding

None.

### Proceed?

YES

## Verification

- `pio run -e teensy41-capture-serial`; RAM1 locals >= 4000
- `pio test -e native` (pre-existing playhead-tails fail stays red)
- HITL `overdub_occupy_paint`: no Off-1 `occupy_rc1b_frame_effective_mismatch` on these keys
