# Retained display source envelope — task 4.19, fixtures 1–7 and slices 1–2 (native + build)

**Date:** 2026-10-27
**Branch:** `feature/unified-rolling-playback-path` (dirty tree preserved; only the paths listed below changed)
**Contract:** [`docs/Plans/display_source_retention_handoff.md`](../../../../docs/Plans/display_source_retention_handoff.md)
(read with task 4.19; this file records the implementation and its measurements, not a second contract)
**No upload, no device stimulus, no SD action, no commit.**

## Outcome

The enforced RED regression **`test_display_source_progresses_with_pending_successor_stop_and_across_wrap` is
GREEN** for the first time (it failed at *"first wrapped frame must retain Record content while its replacement
source is not ready"*), and the retained envelope plus its provider branch are implemented as the settled
contract specifies: an **exact live-prefix predicate** over authoritative pass metadata at reuse time, a
`std::move` promotion at the existing publish edge, capacity-priced retention with a named over-cap refusal,
and the appended-row tier rule. Zero new `Loop` state; `sizeof(Loop)` untouched; the RAM1 line is byte-for-byte
the approved one.

## What changed

- `include/LoopContentResolution.h` — `BoundedDisplayRequest` carries `canonicalStorageGeneration`; new surface
  `kRetainedDisplaySourceNoteCapacity` (4096), `boundedDisplayRetainedRefusal/Ready/Notes/Capacity/PassCount`.
- `src/LoopContentResolution.cpp` — in `BoundedDisplaySession`: candidate prefix identity captured as the Order
  phase enumerates the pass set (id, state, exact committed-chunk ids), `promoteRetained()` at both publish
  edges, `retainedMatches()` (request identity + whole edit-row identity + exact retained-prefix walk),
  `resetCandidate()` split out of `reset()` so a re-key/`!passesMatch` keeps the envelope,
  `candidateNotes()` so the finished candidate's rows have one owner after promotion.
- `src/Loop/LoopVisualCache.cpp` — the provider branch: retained **after** `BoundedContinuation`, **before**
  `PreparedWindow`, reusing the same `noteIntersectsWindow` filter and reserve; the three request
  constructions stamp the generation.
- `include/Loop.h`, `src/DisplayManager/DisplayNoteWindowGather.cpp` — `QualifiedDisplaySource::RetainedEnvelope`
  and its `retained` token (if/else chain, no `switch`, no `.data`).
- `test/test_live_capture_display_boundary/test_live_capture_display_boundary.cpp` — fixtures 1–7 in the
  handoff's order plus the destructive pair; kept as permanent controls.

## Fixtures and their teeth (mutation evidence)

Seven mutations, each restored **byte-identically** (`sha256sum -c` OK on both source files):

| Mutation | Result |
|---|---|
| Prefix `CapturePassState` comparison dropped | **fixture 1 fails** — *"a Disabled pass inside the retained prefix must refuse the retained rows"* |
| Edit-row `EditPassState` comparison dropped | **fixture 3 fails** — *"a Disabled edit row inside the retained identity must refuse the retained rows"* |
| Declared-capacity refusal removed | **fixture 6 fails** — *"Expected 'retained_capacity' Was NULL"* |
| Prefix walk replaced by whole-set equality (`!=` on pass count) | **fixture 7 fails with its original message** *and* **fixture 4 fails** — the pre-fix shape reproduced |
| Appended-row **tier rule removed** | **fixture 4b fails** — *"with the tier released and an appended pass present, retention must stand down"* |
| Tier rule **inverted** (serve only once the tier is released) | **fixture 7 fails with its original message** *and* **fixture 4b fails** at its first half, receipt `covered=0,sealed=1,passes=3,retained=2` |
| Retained serve **ignores the request identity** (over-serve across slots) | **fixture 4c fails** — slot 1 was served slot 0's rows, receipt `served=1,rows=2,src=retained,slot1len=1536` |

Fixture numbers (this suite, `36` registered cases, all green): 1 capture undo refuses; 2 capture redo requalifies and the
served rows equal an **independent rebuild** note-for-note by `NoteId` + geometry; 3 edit undo refuses / redo
requalifies; 4 a wrap's appended pass preserves the prefix (production `maybeCommitOverdubWrap`); **4b the
appended-row tier rule** — while the sealed tier is still owed (receipt `covered=0,sealed=1,passes=3,retained=2`)
the envelope serves and does *not* claim the appended pass' rows, and once a production capture undo→redo inside
the prefix releases that tier, the envelope's own predicate still qualifies while the provider stands it down; 5
at the declared cap retention is accepted and priced `priced=4096,served=2048,cap=4096` — the reserved capacity,
not the served row count; 6 one row past the cap is a named refusal with the previous envelope intact; 7 the
frame-level immediate-wrap regression; **4c** a slot/focus change (another slot, even with its own committed take, is never served the envelope's rows, and returning focus to the owning slot still serves them); plus geometry-change and clear refusals.

## Measured verification

```
pio test -e native -f test_live_capture_display_boundary -f test_frozen_record_reader_survival   45/45
IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe              7/7
pio test -e native                          3509 cases / 7 skipped / 3502 succeeded / 0 failed
pio run -e teensy41-capture-serial          SUCCESS
```

RAM1, exactly the approved line — `variables 96,192`, `ITCM 393,216` (`code 389,992` + `padding 3,224`),
`locals 34,880`. No re-base, no new string-literal carve-out needed: the two refusal names and the `retained`
token landed without moving `variables` (the envelope's storage is `DMAMEM`/external, not DTCM).

## One clause read one way, recorded

The contract's role list puts *over-cap refusal* among the events that call `clearRetained()`, while its pricing
clause says the over-cap case keeps the previous envelope. Implemented as the pricing clause (and as fixture 6
asserts): a refusal **keeps** the previous envelope — priced capacity and rows unchanged — because destroying
qualified content for a refusal would reintroduce the very loss this task repairs. Every other listed event
(explicit `boundedDisplayCancel()`, an invalid request, replacement by a newer promotion) clears or replaces it.

## HITL readiness (not run)

Recipe filled: [`2026-10-27-RETAINED-DISPLAY-HITL-RECIPE.md`](2026-10-27-RETAINED-DISPLAY-HITL-RECIPE.md). It reuses the
authoritative wrapper `scripts/hitl_stop_interruption.py` in the S1.5 long-take shape (`--record-bars 24 --overdub-bars 24
--normal-stop`) because the registered 2-bar baseline never enters a moving-window provider turn; no new stimulus, no new
marker (`check_marker_contract.py 'DIAG,disp_turn,'` → `OK … tier-a-prefix='DIAG,disp_' (10/31 bytes)`), and the verdict is
row-level: **0** `st=ret,why=hold_gap` rows after the wrap (the defect's signature was 266 of them, with `req` moving while
`key` stayed `4088,12288`), **≥1** `why=retained` row with `key=req`, and no collapse in `n=`.

Flash-side facts measured at preparation: `teensy_loader_cli` **2.3** (the guide's minimum), capture image
`firmware.hex` sha256 `387725ea1ed3be2823668d7396ad6d26d79c88df665d16d7f27a18dc3ed8ffe2`, RAM1 line as above, and **no
board attached** (`/dev/cu.usbmodem*` absent). Two gates remain the owner's: the flash itself, and an authorized scratch
track/slot for the wrapper's destructive clear.

## Named, not claimed

- **No new fixture for load/hydrate.** It reaches the same `Loop` destruction the clear fixture already exercises
  (`resetPassTimeline` bumps the storage generation and drops the pass set), and `StorageManagerInternal::
  resetLoopSlotToEmpty` is not linkable in this include-based harness without dragging the storage owner in. The
  slot/focus half is now fixture 4c; the display-side slot authority (`invalidateForSlotChange`) stays attributed to
  the working-set suite's slot-change fixture.
- **Device behaviour is unmeasured.** No upload and no stimulus: the owner's acceptance target (task 4.4) needs
  the owner's approval for a flash and a registered content stimulus.
- **One pre-existing capture/commit observation, unattributed:** a two-bar Record take whose second pair sits at
  800..880 committed only one note in the fixture's own precondition (1328..1408 commits both). It is recorded
  here because a fixture precondition change was made for it; it is not this task's defect and was not chased.
- **No decision superseded, no ownership transfer, no new scheduler** (DEC-162's own reasoning line applies):
  the envelope lives in the existing `BoundedDisplaySession` owner and is reached through the existing deferred
  maintenance lane.
