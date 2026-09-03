# Overdub paint — frame must realize already-correct effective resolution

**Status:** RC1 host shipped. Device recapture next.  
**Date:** 2026-09-03  
**Kind:** convergence bugfix (display realization)  
**Evidence:** fail leftovers on [`140729`](../../captures/session_20260903_140729.log) after Shorten sibling-drop PASS  
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
sealedCompanionOverlayIds_ (display)      ← RC1
sealed upsert 77@336                      ← RC2 if recapture still 48
69@5328 admission miss                    ← RC3 separate
keepShortest on gather                    ← still forbidden
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
