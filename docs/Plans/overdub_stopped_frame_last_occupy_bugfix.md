# Overdub first STOPPED frame — last occupy, not next On

**Status:** host in tree; RAM1 PASS (6848 locals); native **1726/1727** (1 skipped); HITL recapture open  
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

## Root cause

First STOPPED committed display assigns `visualCache.notes` when the cache is clean. That cache still has the **first** occupy. Later Length companions exist in `passes.editPasses` (effective last-wins).

Walking overlay IDs first, then leftover Active companions, reapplies an older first occupy *after* a later Length when that first Length is still Active but no longer in the overlay list (`222432` `3072–3359` over `3072–3215`). Post-seal apply must walk Active `kOverdubCompanionEditPassIndex` rows in `editPasses` order so last Length wins, matching `resolveDisplayNotes`.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `Loop::applyPendingNoteChangesToDisplayNotes` still owns display companion apply; `lastOverdubSealCompanionIds_` stays last-wrap admission |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO — STOPPED assign and post-seal overlay apply are not on the MIDI interval path. Live OVERDUBBING with pending still uses the existing overlay-ID list |

## Invariant

First STOPPED frame ends for a repeatedly occupied `NoteId` match `resolveDisplayNotes` last Length, not the first occupy / next On.

## Implementation

1. Native pin from `220319`: first occupy Length sealed into overlay; later occupy Length sealed into `editPasses` only; cache still at first occupy; apply must yield last occupy.
2. When pending is empty, `appendSealedCompanionOverlay` walks Active `kOverdubCompanionEditPassIndex` Length/Hide rows in `editPasses` order only. Overlay IDs and last-wrap admission stay unchanged.
3. `resolveDisplayNotesCommitted` applies companions after a STOPPED visual-cache assign so a first-occupy cache cannot ship as the first STOPPED frame.

## Native acceptance

- `768–1384` + first Length `1151` + later Length `863` → applied display end `863`.
- Overlay IDs may still list only the first Length; effective / applied frame must not.
- Overlay IDs list only later `3072–3215` while an older Active `3072–3359` remains in `editPasses` → applied end `3215`.
- Existing `002200` pending-over-sealed and prior-wrap Hide overlay tests stay unchanged.

## Verification gates

1. Focused `test_pending_note_change`.
2. `pio run -e teensy41-capture-serial`; RAM1 locals >= 4000.
3. `pio test -e native`.
4. Flash only after user confirmation. HITL: same repeated D# overdub; first STOPPED `src,frame` matches `src,effective` last occupy; no next-On flash during SAVE.

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
