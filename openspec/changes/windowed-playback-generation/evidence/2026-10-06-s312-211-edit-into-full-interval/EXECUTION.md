# Task 3.12 (I9) — fixture 2.11's remaining dimension: **an edit that moves events into a full interval**

Execution packet for the next increment of slice **3.12** (*"the bounded legacy path + the admission limit"*), written
**before** the edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md §G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), `tasks.md` fixture **2.11**
(*"an edit that moves events into a full interval … assert refusal by name at admission, complete ordered output for what
was accepted, and that stop/switch/undo are never refused"*), task **3.12**'s head clause (*"content is never truncated
to fit a window"*), `G2-EXECUTION-PACKET.md` §"Slice 3.12", **DEC-108** §14 and **DEC-114**.

## Why this increment, and what it is the next step of

Fixture 2.11 is the last open item of 3.12, and exactly **one** of its dimensions is still owed — the task list says so
in its own words: *"**Still open, not faked:** an edit that moves events into a full interval — reading it literally
against this owner, an edit's rows do not pass the capture-edge gate (`Track::recordMidiEvents`) that 3.12c wired, so the
clause needs the edit/interval-projection owner's terms (or a decision that it is already covered by
`…_the_capture_admission_counts_committed_windows_only`)"*. The 3.12e evidence recorded the same reading rather than
faking a fixture. This increment takes the **first** of those two options: implement it in the edit/interval owner's
terms, on the production path, rather than declare it covered.

**What "moves events into" means in this owner's terms.** The admission's accounting is
`acceptedEffectiveEvents (= the serving window's **committed** content + this interval's accepted rows) +
outstandingReservations + closureReserve`, with one predicate `fits()`. The serving window's content term is the
interval's **effective** content — the owner's own answer (`Loop::copyEffectiveCommittedEventsInRange`, per pass layer,
with the revision's pinned Active Note rows applied). A note edit that **relocates** a note *into* the served interval
(`EditActionType::Update` + `EditPropertyType::NoteRange`, `applyMoveNoteById`) therefore raises that term by exactly the
moved events, and the capture edge's headroom falls by the same amount — the boundary the clause is about. The same edit
also decides whether the interval still fits one window at all: past `L` the lane must not truncate, and 3.7a I3b's
route (the admission's own `WindowExceedsRetainedCapacity` refusal switching the demand to bounded pages) is what
answers it.

## Identity and scope

- **Slice:** 3.12 / increment **I9** (fixture 2.11's last dimension). **DEC-114's direction is unchanged**; the whole-loop
  `LoopSnapshot` stays the checkpoint and effective-content authority, the merged-window fallback stays intact, and no
  interval-root object, publisher, scheduler or second content authority is added.
- **Owner and production chain (unchanged):** the capture edge (`Track::recordMidiEvents` →
  `Track::captureIntervalAdmissionForSlot` → `WindowAdmissionAccounting::admitsNewEvent`) and the window lane
  (`Track::requestPlaybackWindow` → the deferred turn `Track::stepPlaybackViewMaintenance` → the published window), with
  the retained revision as the source (DEC-108 §10) and `applyNoteEditPassSequence` as the edit rule.
- **Primary files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (fixtures only).
  **A production line is expected to change only if a defect is measured**; a divergence between the served interval's
  content and the owner's answer is a **finding**, not a tolerance.
- **Preconditions already present:** 3.7a I1/I2's production requester and deferred lane for a rootless slot, I7's pinned
  per-layer edit resolution, I3b's paged route and its refusal vocabulary, 3.12a/b/c's accounting and edge wiring,
  3.12f's closure-capacity refusal.
- **External blockers:** none. No device leg (the lane has no row family on the shipping env) and no upload.
- **Non-goals (explicit):** no change to the declared caps (`L` = `kWindowAdmissionLimit`, the reserve, the ring's depth
  or `kWindowPageCapacity`) — they stay **declared, unmeasured** choices for task 6.3's sweeps; no `ExecutionBounds.md`
  row change; no timing/duration claim (S7.5a owns thresholds); no fallback removal (3.7b); no cross-interval closure
  dimension (already covered by the accounting fixture) and no device claim.
- **Commit boundary:** one commit on the working branch `feature/windowed-playback-generation` — fixtures, the task and
  evidence in the same commit; no PR without an explicit request.

## Oracle fixed before the edit

The oracle is **the effective-content owner's answer for the served interval** plus the **named** refusal — never "a
build happened" and never a hand-set count.

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-31** the move lands, and the served window is the owner's answer for the edited revision | a rootless (demoted) slot at 24 bars with one committed record pass holding **1,916** events inside the two-bar window and **two notes outside** it (On/Off at ticks 5000/5100 and 5200/5300, each with its own `NoteId`); runtime prewarmed; the revision retained | (a) `Loop::copyEffectiveCommittedEventsInRange` for the window — the **pre-edit** answer; (b) drive `Track::requestPlaybackWindow` + the deferred turn to a published window; (c) push the two `Update`/`NoteRange` rows moving both notes **into** the interval (to 200..260 and 300..360), advance `playbackRevision` as an edit does, re-retain the new revision, and re-serve | (a) 1,916 events, neither moved note present; (b) the served window's content equals (a) exactly; (c) the owner's post-edit answer is **1,920** — the four moved events present at their new ticks and **absent at the old ones** — the served window's content equals **that** exactly, `committedPlaybackWindowEventCountForSlot == 1,920`, and the **pre-edit** answer differs by exactly those four events | a served interval that ignores the pinned move (the pre-edit count), a truncated or partially applied revision, or a count that is not the owner's answer |
| **W-37a-32** the same edit **moves the admission boundary** | a take live on that slot with the interval's window served; the accepted set at the moment of the edit is one admitted note-on (1 accepted + 1 owed closure) | (a) with the **pre-edit** served window, read the accounting and assert a new note-on still fits; (b) apply the edit as W-37a-31, re-serve; (c) exercise the edge again | (a) `committed() == 1916 + 1 + 1 + 128 = 2046 ≤ L`, `admitsNewEvent(true) == true`; (b) the served content is 1,920; (c) the edge **refuses by name** (`CaptureAppendDenyReason::WindowAdmission` / `"window_admission"`), the store gains **no** row, the owed set is unchanged, a plain event is refused too, the **closure** of the already-accepted note still lands (its unit moves inside `L`, `committed()` unchanged), and a **stop is never refused**; and the boundary the edit moved is stated: `committed() - 4 ≤ L - 2`, i.e. those four moved events are exactly what crossed it | a refusal that is not the window's term (the accepted rows alone cannot reach `L` here), a refusal that leaves a row or a dangling reservation, a refused closure, or a refused stop |
| **W-37a-33** past `L` the lane **never truncates** | as W-37a-31, but the edit moves **200** more events *into* the interval, so its effective content exceeds `L` | drive the requester and then the deferred turn; then drive the paged route for the same revision and interval | the window demand is refused **by name** (`WindowExceedsRetainedCapacity` — the interval does not fit one window) and the **same revision and interval** are prepared as **pages** (`PagedPlaybackOutcome::Prepared` after its turns) from the same pinned bodies; the served window is never a truncated prefix of the interval | a window served with a truncated prefix (the clause this case exists for), or a refusal with no paged route behind it |

**Preserved invariants (asserted, not assumed).** The declared caps are untouched; the fallback is untouched; a refusal
mutates neither the retained source, the index, the served window nor the capture store beyond its own reservation
release; the interaction classes (stop/switch/undo) hold no representation in the accounting, so none can be refused by
it; and the pre-edit answer is measured **before** the edit, so the before/after in W-37a-32 is two measurements of the
same predicate rather than one number asserted twice.

**Differential oracle.** `Loop::copyEffectiveCommittedEventsInRange` over the same revision and interval — the
effective-content owner, untouched, which is also what the *served* window must equal (the same oracle I1/I7 used).

## Controls and coverage

- **Rule reuse, not rule re-invention:** the move is stated as the owner's own row shape
  (`EditPass{Update, NoteRange, targetNoteId, startTick, endTick}`) and applied by the shipped
  `applyNoteEditPassSequence` through the retained per-layer path (I7's); the serving route is 3.7a's production
  requester and deferred turn, not a copy; the paged route is I3b's `beginPagedPlaybackForSlot` /
  `stepPagedPlaybackForSlot`.
- **Production paths exercised:** the capture edge (`recordMidiEvents` through `noteOn`/`noteOff`/the raw entry), the
  accounting accessor, the requester, the deferred turn, the published-window read, and the pager's begin.
- **Boundedness:** the fixtures drive a bounded number of turns; the window is the resolver's own two-bar interval; the
  dense content is built through the store (no hand-set event counts), and the served count is read from the runtime.
- **Untestable before link:** none of the three cases needs a device; no row is added and no device claim is made.
- **Allocation:** the fixtures' own `SessionMidiEventVec` scratch and the store's chunk pool only; no production
  allocation changes.

## Red-proofs (planned, each restored byte-identically and sha256-checked)

| ID | Mutation | Must fail |
|---|---|---|
| **RP-I9-move** | the retained window path does not apply the pinned rows (the effective question answered without them) | W-37a-31 (the served window stays the pre-edit answer) |
| **RP-I9-window-term** | `Track::captureIntervalAdmissionForSlot` drops the serving window's committed content term | W-37a-32 (the refusal disappears) |
| **RP-I9-capacity** | the window admission's over-capacity refusal is not taken (the interval is served anyway) | W-37a-33 (a truncated prefix is served, or the named refusal is gone) |

A mutation that does not fail its fixture is recorded as a non-red-proof with its mechanism named, never reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (baseline: **170/170**). The change's neighbouring
  suites (`test_window_paged_legacy`, `test_windowed_preview_integration`) unchanged.
- Broad: `pio test -e native` (baseline: **2,848 cases / 7 skipped / 2,841 succeeded / 0 failed**).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` / `code 392,248 + padding 968` (floor 840). A test-only increment must leave it
  **byte-identical**; any production edit must be re-based only with a compiler-measured figure.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN** (no device-visible behaviour change is claimed; the lane has no row family on the shipping env).
- Evidence path: `evidence/2026-10-06-s312-211-edit-into-full-interval/`.

## Completion record

**Slice I9's record (2026-10-06).** Landed as designed in scope, with **two corrections the measurement forced** and one
**defect found and fixed** in the retained lane. Full detail and the raw numbers: [`EVIDENCE.md`](EVIDENCE.md).

- Oracle/contract unchanged in substance: **YES** -- the served interval must equal the effective-content owner's answer
  for the same revision and window, and the capture edge's refusal must be **by name**. Two readings were corrected by
  measurement rather than assumed: a `Move` row cannot grow a **windowed** interval (the owner skips a Delete/Update row
  whose target note is absent from the layer it is applied to, and a windowed layer holds the window's events only), so
  the clause is measured with the edit that **adds** events into the interval; and the window build's capacity bound is
  the view's **storage** (`sourceCount > kEventsPerView`), not `content + closureReserve` (that reserve is the capture
  edge's term).
- Fixtures implemented and discriminating: **YES** -- `test_2_11_an_edit_that_moves_events_into_the_served_interval_is_served_as_the_owners_answer`
  (W-37a-31), `..._moves_the_capture_admission_boundary` (W-37a-32), `..._past_capacity_is_refused_by_name_and_never_truncated`
  (W-37a-33).
- **Defect found and fixed:** with a retiring retained source beside the serving one (3.6a's roles), the lane's readers
  took "the first retained entry" -- the gather refused the **current** revision with `RevisionMismatch`, the index was
  built for the previous revision, and the two comparing readers could name the wrong holder. Fixed at the owner
  (`LoopPlaybackRuntime::servingRetainedSource()`, revision-first selection in the gather, serving-first in the index,
  and a **revision read** -- `Track::servingRetainedRevisionForSlot` -- for the readers). `retainedPlaybackSourceForSlot`
  keeps its first-retained meaning because **3.6a W-19** pins it (its header comment's "serving" line is recorded as a
  doc/behaviour mismatch rather than changed under that fixture).
- Red-proofs: **three, all RED with byte-identical restores** -- **RP-I9-source-selection** (the defect as it was:
  W-37a-31 + W-37a-33), **RP-I9-move** (the pinned rows collected empty: 3.7a/I7's own fixture, 3.7a/I8's, and all three
  of this increment's), **RP-I9-window-term** (the edge's window term dropped: W-37a-32 plus 3.12c's two edge
  fixtures).
- Native verification: **GREEN** -- focused **173/173** (baseline 170/170); full native **2,851 / 7 skipped / 2,844
  succeeded / 0 failed** (baseline 2,848/2,841), no `ERRORED`/`FAILED` line.
- Build/RAM1: **GREEN and byte-identical** -- the first working form cost **+256 B of ITCM** (`code 392,504 + padding
  712`, below the 840 floor) because the new accessor copied a whole `RetainedPlaybackSource` at its call sites;
  replacing it with a revision read restored `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` /
  `locals 34,880`, 128 B above the floor. **No re-base.**
- Hygiene: `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- Device: **NOT RUN** -- no device-visible behaviour is claimed.
- Task checkoff: **3.12 stays open** on nothing this increment names except the hardware envelope and 6.3's sweeps
  (both cited, not claimed), and **2.11's remaining dimension is now covered**; 3.13's save clause is the next item of
  this pair.
