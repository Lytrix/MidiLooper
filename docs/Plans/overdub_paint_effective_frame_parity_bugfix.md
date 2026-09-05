# Overdub paint — frame must realize already-correct effective resolution

**Status:** RC1 / RC1b host shipped. RC4 / RC5 device PASS [`141142`](../../captures/session_20260905_141142.log). Occupy HITL pairs OD1/OD2 by start time and scores occupy-sized first-STOPPED effective/frame. Verify-only [`144036`](../../captures/session_20260905_144036.log): occupy holds PASS; RC1b `E != F` (no two-length). RC2 / RC3 stay gated.  
**Date:** 2026-09-05  
**Kind:** convergence bugfix (display realization) + occupy eligibility  
**Evidence:** fail leftovers on [`140729`](../../captures/session_20260903_140729.log) after Shorten sibling-drop PASS; RC1 first-STOPPED two-length regression on [`154310`](../../captures/session_20260903_154310.log); occupy stop `oh` shorten=0 on [`134436`](../../captures/session_20260905_134436.log)  
**Does not authorize:** redefining effective resolution; reconstruct pairing; gather `keepShortestEndAtPitchStart` (`150650`); `#41` Stage 2; off-clock wrap seal; LoopContentResolution redesign; source-view sibling drop (`002145`)

---

## Architectural status (transitional bridge)

This change repairs display realization of already-resolved effective content.

`sealedCompanionOverlayIds_` is a **bounded display-reconciliation list**, not an authoritative content representation. It must not become a second effective-event owner. `lastOverdubSealCompanionIds_` stays last-wrap-only for merged-stream admission. A future display-resolution migration may eliminate the overlay list once frame construction consumes the maintained effective representation directly.

```text
reconstruct → effective → frame
              ↑
        product truth

R != E   → often legitimate
E != F   → remaining product bug
```

```text
authoritative LoopPasses
        ↓
gatherCommittedEvents / LoopContentResolution
        ↓
E = correct
        ↓
display reconciliation (this slice — bridge only)
        ↓
F realizes E
```

Display-frame walk stays a bounded id list, not a new interval owner, and not a new content owner.

---

## Debugging boundary

```text
gatherCommittedEvents / src,effective     ← trust (product truth)
reconstruct dump                          ← diagnostic only
lastOverdubSealCompanionIds_ (admission)  ← last wrap only; do not union
sealedCompanionOverlayIds_ (display)      ← RC1 kept
stop-only sealed skip                     ← RC1b ([`154310`](../../captures/session_20260903_154310.log))
occupy On present-at-hold                 ← RC4 ([`134436`](../../captures/session_20260905_134436.log))
wrap seal oh (diagnostic only)            ← RC5
sealed upsert 77@336                      ← RC2 if recapture still 48
69@5328 admission miss                    ← RC3 separate
keepShortest on gather                    ← still forbidden
suffix sibling-drop                       ← still forbidden (`150650`)
```

---

## Inventory ([`140729`](../../captures/session_20260903_140729.log))

First STOPPED `src,frame` 159 then settle 135. Effective 133. Reconstruct 145. 24 first-stop sealed copies are not this RC.

| Key | reconstruct | effective | frame | Class |
|-----|-------------|-----------|-------|-------|
| `77@336` | 16 | 16 | **48** | RC2 if RC1 recapture still 48 |
| `63@1680` | 432 | **absent** | **288** | RC1 remnant |
| `69@3888` | 48 | **absent** | **48** | RC1 if EditPass Delete exists |
| `69@4656` | 48 | **absent** | **48** | RC1 if EditPass Delete exists |
| `69@5328` | 48 | 48 | **absent** | RC3 admission; parked `69@3024` class |

---

## RC1 — display retains sealed-companion overlay ids across wraps

**History fact (the bug):** `recordLastOverdubSeal` replaces `lastOverdubSealCompanionIds_`. Display overlay walked that list. After wrap 2, wrap-1 Hides left the overlay while those targets were still in snapshot membership (`201956`).

Admission (`tryAdmitLastOverdubSealIntoMergedEvents`) requires every id in `lastOverdubSealCompanionIds_` to have a NoteOn in the last wrap’s merged stream. Unioning that list would fail wrap-2 admit when wrap-1 targets are already gone. Last-wrap replace stays for admission.

**Intended invariant:**

```text
The display retains sealed-companion overlay ids until the painted
committed prefix represents that target's resolved hide/shorten
result, or the target is no longer part of the effective painted
domain.

Last wrap does not own companions. Display overlay owns outstanding
ids. Admission keeps a separate last-wrap list.
```

Lifecycle:

```text
seal N
  ↓
lastOverdubSealCompanionIds_ = this wrap (admission)
  ↓
append unique ids to sealedCompanionOverlayIds_
  ↓
drop overlay ids whose EditPass is no longer Active
  ↓
paint / apply overlay
```

**Obligation satisfied** (do not retire on window-miss / admission-miss):

- Hide: target pitch+start was in this apply’s committed prefix and is gone after apply, **or** the EditPass is no longer Active.
- Shorten: painted prefix has that pitch+start at the companion length, **or** the EditPass is no longer Active.
- Session close (`clearLastOverdubSeal`) clears the overlay list.

`69@5328` missing from the frame is admission (RC3). Do not retire a Hide because a different note is absent.

`69@3888` / `69@4656` are different starts than 3880/4648. Pin EditPass Delete vs overlap Hide before enabling `matchHideByOverlap` on the overlay apply.

### Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `Loop` still owns companions; display only consumes overlay |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — bounded overlay id list; not `passes.editPasses` per frame |
| Reuse | **YES** — extend `recordLastOverdubSeal` / `appendSealedCompanionOverlay` |

---

## RC1b — stop-only sealed skip ([`154310`](../../captures/session_20260903_154310.log))

RC1 Shortens the committed prefix (`63@0` **143** matches effective). `upsertDisplayNoteAtPitchStartFrom` still starts at `committedEnd`, so sealed **appends** record **576**. Sibling-drop does not scan that suffix (`150650`). First STOPPED `src,frame` **204** paints both lengths. `140729` passed because sealed copies were the same short length.

**Intended invariant:**

```text
After stop, a linear sealed row is not appended when the committed
prefix already has that pitch+start. During capture, suffix upsert
stays so a live long hold is not lost to a stale short prefix.
```

| Session | Prefix | Sealed | Capture | Required |
|---------|--------|--------|---------|----------|
| `154310` first STOPPED | 143 | 576 | **off** | one length: **143** |
| `150650` seam | stale 40 | live 624 | **on** | keep the **long** suffix |

Owner: `DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom`. Draw-source walk passes `captureActive`. Do not extend `dropLongerSamePitchStartSiblingsMarkedBySourceTransform` into the suffix. Do not add gather `keepShortest`.

### Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — draw-source sealed union still owns the walk |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same sealed row walk; prefix scan bounded by `committedEnd` |
| Reuse | **YES** — extend `upsertDisplayNoteAtPitchStartFrom` |

Host pins in `test_display_window_utils`: `test_stopped_sealed_upsert_skips_longer_same_pitch_start_154310`, `test_capture_sealed_upsert_keeps_longer_suffix_150650`.

### HITL evaluation

RC1b scores occupy-loop first-STOPPED `src,effective` vs `src,frame` only. Reconstruct is diagnostic. Leftover hops (`frame,count,352`, reconstruct/effective 1430 on [`142807`](../../captures/session_20260905_142807.log)) are not a gate and must not be waited on.

`Track::stopPlaying` re-arms stored-verify and `armStoppedFrameNoteDump` only when `trackManager.isSelectedTrack`. Occupy preset `--dnte-src-dump-drain-ms 8000` waits for occupy-sized (`<= 96`) effective + frame after OD2. `extract_phase_boundaries` assigns OD1/OD2 by start time and closes an overdub on `ODUB,stop,enter` when `ST,OVERDUBBING→PLAYING` is missing.

Verify-only [`144036`](../../captures/session_20260905_144036.log): OD1 source 81814170–90544026 (`shorten=0 hide=0`); OD2 occupy 91173391–105954997 (`shorten=10 hide=12`, `from=snapshot`, visual 22). RC1b scores 10 `E != F` mismatches and no two-length. That is not the 154310 sealed-append class.

| Issue | Meaning |
|-------|---------|
| `occupy_rc1b_dumps_missing` | occupy-sized effective/frame absent after OD2 |
| `occupy_rc1b_two_length:p@s:a,b` | first STOPPED frame has two lengths at one pitch+start |
| `occupy_rc1b_frame_effective_mismatch:p@s:f=…,e=…` | frame length ≠ effective at that pitch+start |

## RC4 — occupy On only transforms notes present at hold ([`134436`](../../captures/session_20260905_134436.log))

**Invariant:** `resolveOccupancyAtNoteOn` may transform a ledger-selected note only when that note is present at the hold phase.

Ledger supplies identity (DEC-050). Filter `appendNotesForIds` with `displayNotePresentAtHold` **before** geometric fallback. Do not change Hide-absorbs-Shorten. Do not retune snapshot fill.

```text
ledger IDs
  → appendNotesForIds
  → present-at-hold filter
  → compare surviving selection against eligible ID set
  → geometric fallback if required
  → Shorten / Hide
```

Host pin: `test_occupancy_ledger_id_not_present_at_hold_does_not_transform_134436`. Host `a04e2bd5`.

RC4 alone does not make stop `oh` shorten>=2. Lap-0 Shortens are wrap-cleared. RC5 next.

### Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |
| Reuse | **YES** — extend `resolveOccupancyAtNoteOn`; reuse `displayNotePresentAtHold` |

## RC5 — emit diagnostic `oh` at wrap seal, before pending clear

**Invariant:** every wrap that seals pending Shorten/Hide emits `oh` with the same occupancy classification semantics as stop-seal `oh`, from current `pendingNoteChanges_` before clear.

`oh` is observational only. No selection, paint, playback, or publication path may consume it. On, Off, and wrap-before-seal call `recountOverlapHoldTotalsFromPending`. Do not paste a third copy.

Host pin: `test_recount_overlap_hold_totals_follows_pending_after_seal`. Device occupy recapture next.

Do not move +1/4 onto lap 1. Do not count `draw_src hidden=`.

### Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |
| Reuse | **YES** — extract On/Off recount; `commitOverdubWrap` emits then seals |

## RC2 — gated on recapture

`77@336 == 16` after RC1 → stop. Else sealed upsert (`existing = sealedRow`) is a projection RC, not effective. No gather `keepShortest`.

## RC3 — separate

`ledger+effective exist`, `draw_src+frame absent`. Do not start until RC1 recapture.

---

## Pre-implementation review

### Ready

- `Loop::recordLastOverdubSeal` / `tryAdmitLastOverdubSealIntoMergedEvents` in `LoopCapture.cpp`
- `appendSealedCompanionOverlay` / `applyPendingNoteChangesToDisplayNotes` in `LoopPendingNoteChange.cpp`
- Host pins in `test_pending_note_change` (`sealRecordedHideCompanion`, `201956` Hide fixtures)

### Resolved

| Topic | Decision |
|-------|----------|
| Effective vs frame | E is truth; F must realize E |
| Reconstruct | Diagnostic; do not change pairing |
| Admission list | Last wrap only — do not union |
| Overlay list | Append unique sealed companion ids; existing overlay vocabulary |
| New domain noun | None — `sealedCompanionOverlayIds_` reuses sealed / companion / overlay |
| 3888/4656 | Pin Delete vs overlap before `matchHideByOverlap` |

### Open before coding

None — implementer pin only.

### Proceed?

YES

## Pre-implementation review (RC1b)

### Ready

- `DisplayWindowUtils::upsertDisplayNoteAtPitchStart` / new `upsertDisplayNoteAtPitchStartFrom`
- `applyDrawSourceOverlays` in `DisplayNoteWindowGather.cpp`
- Host clone in `test_loop_take_survival`

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Discriminator | `!captureActive` |
| Prefix already has linear pitch+start | skip sealed append |
| Capture seam | existing suffix upsert (`150650`) |
| Sibling-drop suffix | still excluded |
| RC1 | keep |

### Open before coding

None.

### Proceed?

YES
