# Coverage-qualified compositor — failed attempt (archived 2026-10-26)

## Why archived
The implementation delegate terminated on a provider **balance error** after 54 turns, not on a design
blocker. It left the display tree **not compiling**:

```
DisplayNoteWindowGather.cpp:1012 'this' argument to collectQualifiedCommittedDisplayNotes has type
  'const Loop', but function is not marked const
DisplayNoteWindowGather.cpp:1218 no matching function call to emitDisplayTurnReceipt
```

## Repair applied (compilation seams only, no behaviour)
- `const_cast<Loop&>(loop)` at the provider call (the provider records which source served, so it is not
  const; the display owner holds a const reference). Labelled at the call site.
- Dropped the two extra arguments the call passed to `emitDisplayTurnReceipt` (the retained covered
  interval) so the call matches its declaration. Signature untouched.

## State after the seams — split verified, compositor INERT
```
test_immediate_visual_cache_probe (controls)   4/4 exit 0
IMMEDIATE_VISUAL_CASE=bound                    6/6 exit 0
HOLD_SPLIT turns=3 view_starts=0,2000,4000 req_start=4000
           retained_key=6128,12288 drawn_rows=1,2,3 content_notes=4
```
The display gates are exactly where the display-hold split left them. No compositor case exists or passed:
bars 16-24 and fresh-overdub visibility are **unproved**, and the partial composition code present in the
tree is inert because its evidence inputs were dropped rather than wired.

## Decision
Archive this attempt and return to the verified split before the next slice:
1. Revert the inert compositor remnants so the tree is the verified split plus nothing else.
2. Write the exact RED first: requested 12288..18432 (bars 16-24) with a retained frame 6128..12288, plus a
   fresh overdub inside the requested interval, asserting the owner's invariant — a retained frame is
   authoritative only over its recorded coverage; outside it another source may contribute only if
   independently qualified for the requested interval/current generation; retained 6128..12288 must
   contribute **zero** rows to a wholly 12288..18432 request; a partially prepared continuation contributes
   only its published portion; the fresh overdub appears through the live overlay exactly once; holes stay
   honest; and a moving viewport does not invalidate content identities unnecessarily.
3. Implement the compositor locally as one bounded slice against that RED, generation coherence included.

No upload, no device, no commit for this archive.
