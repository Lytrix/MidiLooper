# Overdub paint — frame must realize already-correct effective resolution

**Status:** RC1 / RC1b host shipped. RC1c host shipped; device recapture [`153848`](../../captures/session_20260905_153848.log): `E` is not reconstruct-shaped and frame has no `F == R` remnant. Occupy holds PASS. HITL still FAILs one 1-tick key (`52@2172` f=58 e=59). RC4 / RC5 device PASS [`141142`](../../captures/session_20260905_141142.log). RC2 / RC3 stay gated.  
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
gatherCommittedEvents / src,effective     ← trust (48@616 = 400, Off@1016)
materializeToEventVector (per layer)      ← trust (same 400)
copyEffectiveCommittedEventsInRange       ← RC1c host shipped; device [`153848`](../../captures/session_20260905_153848.log) (no F==R remnant)
reconstruct dump                          ← diagnostic only (2292)
resolveOccupancyAtNoteOn hold-1           ← writes 884 when applied to display
first STOPPED frame / visualCache         ← realizes the window-gather miss
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

Device [`144743`](../../captures/session_20260905_144743.log): OD1 source 509580888–518042419 (`shorten=0 hide=0`); OD2 occupy 518679360–533664815 (`shorten=11 hide=9`, `from=snapshot`, visual 22). Occupy-sized dumps reconstruct 39 / effective 30 / frame 28. HITL still labels those 11 `E != F` keys as `occupy_rc1b_frame_effective_mismatch`. They are **RC1c**, not the 154310 sealed-append class (no two-length). Verify-only [`144036`](../../captures/session_20260905_144036.log) is the same class after the OD1/OD2 time-order fix.

Recapture [`152012`](../../captures/session_20260905_152012.log) (same firmware, no RC1c code change): occupy holds PASS; 9 `E != F`; dumps 39 / 28 / 25; no two-length. `48@666` frame 739 / effective 399.

| Issue | Meaning |
|-------|---------|
| `occupy_rc1b_dumps_missing` | occupy-sized effective/frame absent after OD2 |
| `occupy_rc1b_two_length:p@s:a,b` | first STOPPED frame has two lengths at one pitch+start |
| `occupy_rc1b_frame_effective_mismatch:p@s:f=…,e=…` | frame length ≠ effective at that pitch+start |

## RC1c — window gather Delete on merged events ([`144743`](../../captures/session_20260905_144743.log) `48@616`)

Host fixture `test_occupy_144743_48_616_materialize_vs_window_vs_pending` (144743 SEVT 48 stream + Active Hide of `48@885` / `48@763`):

| Path | `48@616` length | Off@1016 |
|------|-----------------|----------|
| `mergeActiveCapturePasses` reconstruct | 2292 | present (Hide not applied) |
| `LoopPasses::materializeToEventVector` | **400** | present |
| `gatherCommittedEvents` (effective dump) | **400** | present |
| `gatherCommittedEventsInWindow` | **400** | present |
| pending after OD2 close | empty | — |
| `applyPendingNoteChangesToDisplayNotes` | does not write end 884 | — |

Per-layer materialize applies Hide of On@885 on the OD1 layer only, so LIFO Off is Off@1285. `copyEffectiveCommittedEventsInRange` merges chunks first, then `applyNoteEditPassSequence`. `applyDeleteNoteById` / `findNoteOffForOnIndex` pairs On@885 with occupy Off@1016. Reconstruct then leaves On@616 open until Off@2908 (2292). That is why first-STOPPED frame can keep reconstruct remnants (`F == R` on nine other `E != F` keys). `48@616` frame 268 is occupancy hold-1 against source 885 on top of that miss; leftover pending/overlay after session close does not write 884.

**Intended invariant:**

```text
copyEffectiveCommittedEventsInRange realizes the same 48@616 length as
per-layer materializeToEventVector (400, Off@1016 kept). Merged-stream
Delete must not pair a hidden source On with a later pass's Off.
```

Owner to extend: `Loop::copyEffectiveCommittedEventsInRange`. Do not add gather `keepShortest`. Do not start RC2 / RC3. Do not treat this as RC1 overlay leftover or RC1b sealed-append.

### Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — window gather still owns the merged apply; match per-layer pairing |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same windowed chunk walk + existing Delete apply |
| Reuse | **YES** — extend `copyEffectiveCommittedEventsInRange`; apply per committed list, then `mergeSortedLoopCaptureLayers` |

### Pre-implementation review (RC1c)

### Ready

- `Loop::copyEffectiveCommittedEventsInRange` in `LoopMaterialization.cpp`
- Per-layer apply in `LoopPasses::materializeToEventVector`
- Host pin `test_occupy_144743_48_616_materialize_vs_window_vs_pending`

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Effective dump | `gatherCommittedEvents` → per-layer materialize = 400 |
| Window gather | per-layer apply then merge → 400, Off@1016 kept |
| Pending / overlay after close | empty pending; apply does not write 884 |
| Owner | `copyEffectiveCommittedEventsInRange`, not RC1 overlay, not adopt_partial |

### Open before coding

None.

### Proceed?

YES — firmware this slice. Host fixture now expects window gather 400 / Off@1016 kept.

### Host shipped

`copyEffectiveCommittedEventsInRange` windows and applies Active Note rows on each `CommittedChunkIdList`, then merges. The merged stream is not applied again. `includeActiveEditRows == false` stays merge-only. Host `test_occupy_144743_48_616_materialize_vs_window_vs_pending` PASS. `teensy41-capture-serial` RAM1 locals **6848**. Native **1644/1646** (pre-existing playhead-tails fail).

Device [`153848`](../../captures/session_20260905_153848.log): occupy holds PASS; dumps 39 / 28 / 26; no two-length; 15 `E != R` occupy-short keys; **0** `F == R` remnants. `48@666` E=F=396 vs R=744 (was frame 739 / effective 399 on [`152012`](../../captures/session_20260905_152012.log)). HITL FAIL `52@2172` f=58 e=59 (E=R=59). First flash run [`153705`](../../captures/session_20260905_153705.log) dropped USB before dumps.

`advanceFallbackGatherSession` still applies Delete on the merged budgeted session. That remaining owner is not RC1c. `test_idle_slice_stale_overlay_rejects_and_uses_committed_fallback` pins window gather 448 and keeps overlay-reject; it does not require idle cache end == window gather.

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
