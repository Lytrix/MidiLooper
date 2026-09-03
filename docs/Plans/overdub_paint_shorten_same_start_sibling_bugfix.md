# Overdub paint — Shorten must drop the longer same-start sibling

**Status:** Host in tree — device recapture pending  
**Date:** 2026-09-03  
**Kind:** bugfix  
**Parent:** [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](overdub_occupy_same_start_stop_hide_readmit_bugfix.md) (`201956` Hide sibling drop)  
**Evidence:** [`123236`](../../captures/session_20260903_123236.log)  
**Does not authorize:** `keepShortestEndAtPitchStart` on draw-source gather (`150650`); source-view sibling drop (`002145`); RC-D4 / reuse; `#41` Stage 2; off-clock wrap seal

---

## Debugging boundary

```text
RC-S retain+merge                         ← closed (122730)
ReuseCommittedDrawSource pending apply    ← closed (D3)
Hide same-start sibling drop              ← closed (201956 / 204043)
Shorten same-start sibling drop           ← this slice
keepShortest on gather                    ← still forbidden (150650)
source-view sibling drop                  ← still off
```

---

## Problem

[`123236`](../../captures/session_20260903_123236.log) is one OVERDUBBING session (L5598–L13866), 10-bar loop (`7680`), five wraps. Note edit / `src,effective` (172) has the occupy Shortens. The first STOPPED paint (`src,frame` 250, `DISP` 250 then 190) keeps the longer reconstruct sibling.

| `63@start` | reconstruct | effective | first STOPPED frame |
|---|---|---|---|
| 768 | 624 | **95** | **95 and 576** |
| 1536 | 624 | **95** | **95 and 576** |
| 2304 | 384 / 576 | **95** | **95 and 240** |
| 144 | 96 / 240 | *(removed)* | **240** |

Stop `DIAG,oh` `shorten=0 hide=4` is leftover pending after wrap seal, not “no Shortens this session.”

`dropLongerSamePitchStartSiblingsMarkedByHide` runs only for Hide (`201956`). Shorten updates one `noteId` and leaves the longer same-pitch+start remnant in the committed prefix. The piano roll reads as unshortened. Note edit reads effective.

## Invariant

```text
A Hide or Shorten that names a pitch+start may drop a longer
same-pitch+start sibling in the committed display prefix only.

Rows at liveCaptureBegin and after (sealed / preview / tails)
are not dropped. This is not keepShortestEndAtPitchStart.
Source view does not drop siblings.
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `Loop` still owns pending / sealed companions; display only consumes `applyPendingNoteChangesToDisplayNotes` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same committed-prefix scan as the Hide drop |
| Reuse | **YES** — extend `dropLongerSamePitchStartSiblingsMarkedByHide` to source transforms (Hide or Shorten) |

## Implementation

- `sourceTransformMarksPitchStart` is true for Hide **or** Shorten at that pitch+start.
- Display apply (`dropSameStartLongerAfterSourceTransform=true`) runs the drop after Hide/Shorten apply.
- Source-view / span-cache apply stays `false`.
- Suffix exclusion (`i >= liveCaptureBegin`) unchanged (`150650`).

## Host / device gates

- Native: sealed Shorten keeps `63@768→863` and drops `768→1344` / `768→1392`. Pending Shorten (unsealed) same. Suffix long hold at the same pitch+start survives (`liveCaptureBegin` exclusion). Existing Hide fixtures stay.
- Device: `123236`-shaped recapture. First STOPPED `src,frame` must not keep a longer same-pitch+start sibling beside the effective Shorten (no `63@768` **95 and 576**). Then pause — no gather `keepShortest`, no `#41` Stage 2.

## Pre-implementation review

### Ready

- `applyPendingHideAndShortenToNotes` / sibling drop in `LoopPendingNoteChange.cpp`
- Hide fixtures `test_sealed_hide_keeps_short_overdub_after_snapshot_current` / `test_sealed_hide_drops_same_start_longer_when_note_id_differs`

### Resolved

| Topic | Decision |
|-------|----------|
| Owner | Extend existing display sibling drop |
| Identity | Existing overlay pitch+start; Hide or Shorten names the key |
| 150650 | Suffix not scanned |
| Source view | No drop |

### Open before coding

None — implementer pin only.

### Proceed?

YES
