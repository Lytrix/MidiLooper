# Overdub first STOPPED frame — last occupy, not next On

**Status:** implemented; STOPPED preserved-handoff bypass added after recapture [`224901`]  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_220319.log`](../../captures/session_20260907_220319.log), recapture [`session_20260907_222432.log`](../../captures/session_20260907_222432.log)  
**Parent:** [`overdub_paint_effective_frame_parity_bugfix.md`](overdub_paint_effective_frame_parity_bugfix.md) (`E != F`)  
**Does not reopen:** [`overdub_stop_dsharp_noteoff_pairing_bugfix.md`](overdub_stop_dsharp_noteoff_pairing_bugfix.md) (deferred pairing)

## Scope and boundary

```text
LoopContentResolution::resolveDisplayNotes   ← trust (last occupy)
pending paint while OVERDUBBING              ← trust (last occupy)
    → first STOPPED frame / visualCache assign   ← this slice
    → deferred reconstruct dump                  ← diagnostic only
```

Do not change pairing, occupy resolve, playback merge, or `lastOverdubSealCompanionIds_` admission (last wrap only).

## Evidence

In [`220319`](../../captures/session_20260907_220319.log), last live occupy already has the short ends. First STOPPED `dnte,src,frame` reverts those first-pass D#s to **next D# On − 1** (first occupy). Effective dump already has last occupy.

| First-pass D# | Last live / effective | First STOPPED frame | Next D# On |
|---|---|---|---|
| `768` | `863` | `1151` | `1152` |
| `1536` | `1631` | `1823` | `1824` |
| `2304` | `2495` | `2543` | `2544` |
| `3072` | `3263` | `3311` | `3312` |
| `3840` | `4175` | `4223` | `4224` |
| `0` | `143` | `143` | — |

`PLAYING → STOPPED` at `340081340` queues the 121-note frame during the persist-after-stop SAVE. `dnte,src,reconstruct` is original Offs (`768→1392`) and is not the OLED flash.

Recapture [`222432`](../../captures/session_20260907_222432.log) after the overlay-ID leftover walk: earlier first-pass D#s match effective. Last two bars still flash to first occupy:

| First-pass D# | Last live / effective | First STOPPED frame | First occupy |
|---|---|---|---|
| `3072` | `3215` | `3359` | next On `3360` |
| `3840` | `3983` | `4127` | next On `4128` |

Recapture [`224901`](../../captures/session_20260907_224901.log) showed the
direct resolver branch was still reached after the preserved overdub-stop
handoff. The frame therefore remained first-occupy geometry (`768→1151`,
`1536→1919`) while effective was (`768→911`, `1536→1679`). STOPPED
finalization now runs before that preserved-handoff return.

## Root cause

First STOPPED committed display assigned `visualCache.notes` when the cache
was clean. That cache could still contain the first occupy. Replaying
companion rows onto that cache was not equivalent to effective resolution:
cached rows and effective rows can have different identities, and applying
all Active companion rows could reapply older geometry after later geometry.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `Loop::applyPendingNoteChangesToDisplayNotes` still owns display companion apply; `lastOverdubSealCompanionIds_` stays last-wrap admission |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO — STOPPED assign and post-seal overlay apply are not on the MIDI interval path. Live OVERDUBBING with pending still uses the existing overlay-ID list |

## Invariant

First STOPPED frame ends for a repeatedly occupied `NoteId` match `resolveDisplayNotes` last Length, not the first occupy / next On.

## Implementation

The `f53cc59a` cache-overlay workaround was reverted. When the track is
STOPPED and committed passes exist, `resolveDisplayNotesCommitted` now calls
`LoopContentResolution::resolveDisplayNotes` directly. The STOPPED frame thus
reuses the same final-state resolver as `src,effective`; it does not finalize
from stale visual-cache content or the preserved overdub-stop handoff.

## Native acceptance

- STOPPED display geometry equals `LoopContentResolution::resolveDisplayNotes`.
- Existing `002200` pending-over-sealed and prior-wrap Hide overlay tests stay unchanged.

## Verification gates

1. Focused `test_pending_note_change`.
2. `pio run -e teensy41-capture-serial`; RAM1 locals >= 4000.
3. `pio test -e native`.
4. Flash only after user confirmation. HITL: same repeated D# overdub; first STOPPED `src,frame` matches `src,effective` last occupy; no next-On flash during SAVE.

Host verification: focused display/companion tests **232/232**; full native
**1724/1725** (1 skipped); capture-serial RAM1 locals **6848**. HITL
recapture after this implementation remains open.

## Out of scope

- Reconstruct dump pairing
- `Track::finalizePendingNotes` synthetic close
- RC-B unmatched NoteOff admission
- Occupancy resolve / ledger
- `keepShortest` on gather

## Pre-implementation review

### Ready
- Owners are `appendSealedCompanionOverlay` and STOPPED visual-cache assign in `resolveDisplayNotesCommitted`.
- Pairing / occupy / last-wrap admission stay frozen.

### Resolved
| Topic | Decision |
|---|---|
| Product truth | `src,effective` last occupy (`768→863`) |
| OLED flash | First STOPPED `src,frame` first occupy (`768→1151`) |
| Overlay IDs | Stay last-wrap union / admission; display apply after seal walks Active companion Lengths |
| Live overdub | Pending non-empty keeps overlay-ID apply so this-lap pending still wins (`002200`) |

### Open before coding
None.

### Proceed?
YES
