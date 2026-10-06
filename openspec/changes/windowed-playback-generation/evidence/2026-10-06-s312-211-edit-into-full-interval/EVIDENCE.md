# Task 3.12 (I9) — fixture 2.11's last dimension measured, and the defect its fixtures found in the retained lane

**Slice:** `windowed-playback-generation` / task **3.12**, increment **I9** (fixture **2.11**'s remaining dimension:
*"an edit that moves events into a full interval"*) · **Branch:** `feature/windowed-playback-generation` · **Date:**
2026-10-06 · **Firmware:** changed (`include/Track.h`, `include/TrackPlaybackRuntime.h`,
`src/Track/TrackPlaybackWindowBuild.cpp`) · **Device:** NOT RUN (no device-visible behaviour is claimed).

Packet written **before** the edits: [`EXECUTION.md`](EXECUTION.md). Authority: `tasks.md` fixture **2.11** and task
**3.12**'s head clause (*"content is never truncated to fit a window"*), `G2-EXECUTION-PACKET.md` §"Slice 3.12",
**DEC-108** §14, **DEC-114**.

## 1. A defect the fixtures found, fixed here

The clause's own shape — a revision change on a slot that already holds a **retiring** retained source beside its
**serving** one (3.6a's roles, bounded at two) — made the lane refuse the revision it was serving:

* `Track::gatherRetainedSourceWindowForSlot` picked "the **first retained** entry". With the previous revision in the
  first role slot, the gather answered **`RevisionMismatch`** for the current revision (`request=5`, `gather=4`,
  `lists=0`, `inspected=0` in the first fixture run) — so a slot whose window is rebuilt after an edit could not be
  served at all, and 3.7b's "the lane serves what the fallback served" would have failed on the first revision change.
* The same "first retained" pattern dated `buildRetainedIntervalIndexForSlot` (the index would describe the *previous*
  revision) and the two readers that compare a source's revision with the loop's (`playbackWindowReadinessForSlot`'s
  `OwesBuild`/`OwesSource` choice, `playbackWindowSourceIsCurrentForSlot`).

**The fix, at the owner of the question:** `LoopPlaybackRuntime::servingRetainedSource()` (serving role first, first
retained entry as the fallback) and
* the gather prefers the entry whose `revision == expectedRevision` **first**, then the serving one — so a *lagging*
  caller is still served the revision it asked for (the one-pass-late allowance stays the caller's rule, task 3.6b:
  I1's fixture still passes), while the current revision is never refused;
* the index builder indexes the **serving** revision (the one the retention just admitted);
* `Track::servingRetainedRevisionForSlot` is a **revision read** (no source copy) for the two comparing readers.

**Recorded, not changed under another fixture:** `Track::retainedPlaybackSourceForSlot` keeps its "first retained
entry" behaviour, because task **3.6a**'s fixture **W-19** reads the *older* source through it and asserts its role
became `Retiring`. The header comment above it says *"Read the **serving** retained source"*, which is a
**doc/behaviour mismatch** this increment found and records rather than silently fixing (3.6a's evidence would then
have to change).

### The ITCM budget was this increment's second real work

The first working form cost **+256 B of ITCM** and failed the RAM1 guard (`code 392,504 + padding 712 < 840`). The
cause was the new accessor **copying a whole `RetainedPlaybackSource`** (two vectors: pins + owned bodies) at its call
sites; replacing it with the **revision read** (`servingRetainedRevisionForSlot`) restored the line **byte-identically
to the pre-slice baseline** — `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`,
128 B above the floor. **No re-base**, and the same finding I7/I1 recorded one level down: in this TU the cost is
what an un-inlined call site drags in, not the loop.

## 2. What the clause turned out to mean (a reading corrected by measurement)

The packet's first form of the edit was a **`Move` row** (`Update` + `NoteRange`) relocating a note *from outside* the
window into it. **Measured (`Expected 1920 Was 1916`, from the owner *and* the lane):** it cannot grow the interval —
`applyNoteEditPassSequence` applies a Delete/Update row only when its target note is present in the layer it is applied
to, and a **windowed** layer holds the window's events only, so the row is skipped there. The whole-loop resolution
*did* move such a note, but both consumers of this clause read the **windowed** resolution, so the clause is measured
in the shape it can be measured in: an edit that **adds events into the interval** (`EditActionType::Create`, whose
`addedEvents` are the events the row contributes — the row shape I7's own note already relied on for `Create` rows).

**And the bound is not the one the packet assumed.** The window build's capacity bound is the view's **storage**
(`sourceCount > kEventsPerView` = 2048), *not* `content + closureReserve` — the reserve is the **capture edge's**
term, and `WindowAdmissionAccounting` is the edge's arithmetic only. Measured: an edited interval of 1,922 events was
**admitted** (`Expected 10 Was 0`), and the named refusal appears at 2,050 events. Both corrections are recorded in the
fixtures' own comments.

## 3. Measured

| Case | Result |
|---|---|
| **W-37a-31** the move lands, and the served window is the owner's answer | pre-edit: 1,916 events, neither edit note present. The edit's **two `Create` rows** (notes at 200/260 and 300/360, pitches 60/61 — the dense content deliberately occupies only pitches 48..59) put **four events** into the interval → the owner's post-edit answer is **1,920**, the four present at their own ticks. Through production (retain + requester + the deferred turn + task 3.4's adoption): the served window's content equals the owner's post-edit answer **in the same tick order and, per tick, as a multiset** (same-tick order stays 4.3's gate), and `committedPlaybackWindowEventCountForSlot` is that same answer. The two answers differ by exactly the four events |
| **W-37a-32** the same edit moves the capture-edge boundary | two slots holding the **same** content, one of them with the edit: accountings (read with no live interval: the owed term is the track's own pending-note set, measured and named) `2044` vs `2048` = a **four-event** difference, `admitsNewEvent(true)` **true** for the control and **false** for the edited content. With a live interval on each slot (through the production gesture): the control slot's note-on is **accepted** (1,916 + 1 accepted + 1 owed + reserve), and the edited slot's first note-on is **refused by name** (`window_admission`), leaving **no row** and **no dangling reservation**; a plain event is refused too |
| **W-37a-33** past the capacity the lane never truncates | the edit puts **67 notes (134 events)** in: 2,050 events, **over** the window's storage capacity. The gather still **serves** the revision (17 slices, `complete`, 2,050 events — the refusal is the admission's, not the source's), the **requester refuses by name** (`WindowExceedsRetainedCapacity`), the **served window stays the old complete answer** (byte-for-byte the pre-edit content, verified tick-order and per-tick multiset — never a truncated prefix), and the deferred demand **stays owed** (`AwaitingSourceQualification` in this drive, which runs no transport — the *named* over-capacity refusal is the requester's, asserted above) |

## 4. Red-proofs

| ID | Mutation | Result |
|---|---|---|
| **RP-I9-source-selection** | the gather's revision-first selection reverted to "the first retained entry" (the defect as it was) | **RED**: W-37a-31 *"the owner publishes a window for this revision (request=5 … gather=4 …)"* and W-37a-33 *"Expected 0 Was 4"*. 2 failed / 171 |
| **RP-I9-move** | the pinned Active Note rows are collected empty (the per-layer path applies none) | **RED on five fixtures**: 3.7a/I7's own *"Expected 264 Was 266. the retained answer is exactly the owner's per-layer content"*, 3.7a/I8's *"the per-layer resolution omits the deleted On"*, and all three of this increment's (the served window stays 1,916; the edited interval 1,916; 2,050 → 1,916). 5 failed / 168 |
| **RP-I9-window-term** | `captureIntervalAdmissionForSlot` drops the serving window's committed content term | **RED**: W-37a-32 *"Expected 2044 Was 128"* plus 3.12c's two edge fixtures (*"Expected 2047 Was 130"*, *"Expected 2047 Was 128"*). 3 failed / 170 |

Each mutation was restored **byte-identically** (harness restores from a sha256-checked copy; `TrackPlaybackWindowBuild.cpp`
and `TrackCaptureInput.cpp` verified, `git diff --stat -- src include` thereafter showing only this increment's own
edits).

## 5. Verification

| Check | Result |
|---|---|
| Focused `pio test -e native -f test_runtime_working_set_ownership` | **PASSED — 173/173** (baseline 170/170; three new cases) |
| Full native `pio test -e native` | **2,851 cases / 7 skipped / 2,844 succeeded / 0 failed** (baseline 2,848/2,841), **no** `ERRORED`/`FAILED` line |
| Capture build `pio run -e teensy41-capture-serial` (RAM1 guard) | **SUCCESS**, line **byte-identical** to the pre-slice baseline: `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`, floor 840, **128 B** of headroom. **No re-base** (see §1's ITCM finding) |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |

## 6. Named, not claimed

* **No device leg, no timing/duration claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b).** Nothing
  in this increment is observable on the shipping env.
* **The closure-at-capacity clause is cited, not re-proved.** `recordMidiEvents` exempts `NoteOff` **by name**, so the
  gate is not consulted for a closure; exercising it at the boundary needs a note the *edited* interval had already
  accepted, which its own capacity forbids. Task **3.12c**'s fixture asserts that arithmetic (a unit moved from owed to
  accepted with `committed()` unchanged) and is cited rather than duplicated. **Recorded, not attributed:** the edited
  slot's own stop admission returned `false` in this fixture's hand-started transport state (no accepted notes), so the
  stop clause is asserted on the control slot and cited from 3.12c; the edited slot's stop route is **not** claimed.
* **The capture accounting's owed term is the track's**, not the interval's: `captureIntervalAdmissionForSlot` reads
  `pendingNotes.size()`, so a note owed on one slot appears in another slot's accounting. Measured here (it is why the
  boundary block reads both accountings before any live interval), **not** fixed — it is the shipped unit's shape and
  changing it belongs to the accounting's own owner.
* **`retainPlaybackSourceForSlot` still has no production caller** (the 2026-10-06 pool-attribution finding stands);
  this increment fixes the *readers* so that the lane is correct the moment a retainer exists (RWO S5's demotion edge or
  3.6a's own wiring), and it does not add one.
* **Not claimed:** that a `Move` row ever grows a windowed interval (it does not — §2); the whole-loop resolution's
  behaviour beyond the reading above; same-tick order (4.3's gate, and the comparison here is per-tick as a multiset);
  the pager's per-layer resolution (I7's named non-goal); the declared caps' values (`L`, the reserve, the ring's depth,
  `kWindowPageCapacity` — task 6.3's sweeps); and the preview session (3.13's save clause is the next item of this
  pair).
