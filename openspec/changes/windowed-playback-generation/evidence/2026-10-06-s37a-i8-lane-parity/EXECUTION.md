# Task 3.7a (I8) — the **differential lane parity**: the published-root lane and the retained-source lane answer one revision identically

Execution packet for the next increment of slice **3.7a** (*"Wire the retained-source interval index into the production
window consumer while retaining the legacy fallback"*), written **before** the edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md §G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), `tasks.md` item **3.7a** (its verify
clause names *"differential fallback parity"*), [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) §"Slice 3.7a"
(its transition oracle asks to *"compare selected output to effective-content truth"* and its red-proof list names
*"allow fallback removal during the slice (compatibility differential control fails)"*), the slice packet of I1
([`../2026-10-06-s37a-production-consumer/EXECUTION.md`](../2026-10-06-s37a-production-consumer/EXECUTION.md), whose
increment ladder row **I4** is *"Differential fallback parity and the timing admission … the shipped-vs-fallback
comparison over the same revision"*), **DEC-108** (§10, §13(c), §14(c)) and **DEC-114**.

## Why this increment, and what it is the next step of

The ladder's last row is the one that has not landed: **I4's "differential fallback parity"** — *the shipped-vs-fallback
comparison over the same revision*. What the landed increments proved, and what they did **not**:

| Property | Proven by | Oracle used |
|---|---|---|
| the retained gather answers a rootless interval | I1, I2 | `Loop::copyEffectiveCommittedEventsInRange` (the effective-content owner) |
| the per-layer resolution of the pinned revision's note edits equals the owner's | I7 | the same owner function |
| the paged lane's emission equals the effective content, pass-exact | I3c-c, I5 | the same owner function |
| **the retained lane's answer equals what the published-root lane serves for one revision** | **not proven** | — |

That last line is the compatibility statement `3.7b` rests on. Task `3.7b` removes the live mutable **merged-window
fallback** and keeps a cold reference; the fallback's content comes from the loop's **published snapshot root**
(`ensurePlaybackMergedMidiEventsBuilt` → `Loop::tryGatherPublishedSnapshotEventsInWindow`, the same primitive the shipped
window build of `requestPlaybackWindow` reads), while `3.7a`'s lane reads the retained revision's **pinned bodies**. Those
are two independent enumerations of one revision — the root's published membership (flattened, ordered by the published
`canonicalOrder`, with the publication's own unpublished-overlay rule) and the pinned bodies (per pass layer, tick-sorted,
edited per layer). Nothing so far compared them. If they can differ, then removing the fallback changes what a slot plays
— which is exactly what `3.7b` may not do silently.

Two further facts make the comparison *possible* today and worth pinning now:

* the shape it applies to is real: a slot may carry **both** a current published root (a restored slot's SD load publishes
  one) and a retained source for the same revision (DEC-108 §10's demotion path);
* the two lanes are already distinguished in the shipped requester: `Track::requestPlaybackWindow` serves from the
  **root** whenever the root enumeration answers, and takes the retained gather **only** when it cannot
  (`!Loop::tryGatherPublishedSnapshotEventsInWindow`). So the parity claim is not hypothetical: it says the branch that
  answers today and the branch that will answer after `3.7b` serve the same events.

**Identity and scope.** Slice **3.7a / increment I8**. No production line is expected to change: this increment is the
**measurement** the ladder owes, and a divergence it finds is a **finding to report**, not a tolerance to grant and not a
silent repair (the slice's own rule: *a difference is a finding, never a tolerance*). **DEC-114's direction is
unchanged** — no interval-root object, no second publisher, no second scheduler, no second content authority; the
whole-loop `LoopSnapshot` stays the checkpoint and effective-content authority.

**Owner and production chain (unchanged).** `Track::requestPlaybackWindow` (the 3.7a-named requester) and the deferred
lane's `Track::stepPlaybackViewMaintenance`, both reaching the same two enumerations; the merged-window fallback stays
exactly where it is.

**Non-goals (explicit).** No fallback removal (that is `3.7b`); no change to `ensurePlaybackMergedMidiEventsBuilt`, to
`Loop::tryGatherPublishedSnapshotEventsInWindow`, to `Loop::copyEffectiveCommittedEventsInRange`, or to the gather itself
unless a **defect** is measured and in scope; no new row family, no `ExecutionBounds.md` change, no device leg, no timing
claim; no claim about **same-tick order** (that stays 4.3's gate) or about the note/event **identity** each lane assigns to
the same content.

**Commit boundary:** one commit on the working branch `feature/windowed-playback-generation` — the fixtures, the task and
runtime-doc updates and the evidence in the same commit; no PR without an explicit request.

## Oracle fixed before the edit

The oracle is **lane agreement on the wire identity of the events** — for each window: the same count, and the same
`(tick, type, pitch, velocity, channel)` element-by-element — plus the **named refusals** each lane owes. Never "a build
happened", never "the sizes match".

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-27** the two lanes serve one revision identically | slot 0 carries a bar-spaced committed record pass over 24 bars (`commitBarSpacedPassIntoTrackSlot`) **and** a current published root for the **same** content and revision (`publishBarSpacedRootForSlot`); runtime prewarmed | (a) `Loop::tryGatherPublishedSnapshotEventsInWindow` for the two-bar window at playhead 500; (b) drive `Track::gatherRetainedSourceWindowForSlot` for that window and revision to completion; (c) `Track::requestPlaybackWindow` + the maintenance turns; (d) read the served view | (a) `true` with path **`Snapshot`** (a current root, not an overlay) and a non-empty answer; (b) `Served`, `complete`, `pinnedLists == 1`, `inspectedChunks > 0`; (a) and (b) equal in size and **element-wise on the wire identity**; (c) `Admitted`, the shuffled answer equals the **root's**, the published identity carries the **root's** snapshot identity and `origin == Committed`; (d) the served content equals the **retained** answer | a retained answer that omits or adds an event of the same revision (window, layer or ordering divergence), or a served window that does not carry the root it read |
| **W-37a-28** the two lanes agree at **every** window | as W-37a-27 | sweep playheads across the loop (each bar's own interior, plus non-bar-aligned playheads); per window compare the two lanes | every window agrees element-wise; and across the sweep **each bar's pair appears in at least one window** with both lanes agreeing — so the sweep is not vacuous and the boundary cases are exercised | an agreement that holds only at playhead 500 (a window-slicing divergence at another start), or a sweep whose windows carry nothing |
| **W-37a-29** a revision the two lanes **disagree** about is refused **by name** | as W-37a-27, then the loop's revision advances past the retained one (an edit's effect on the revision a window is dated by) and a **current** root is published for the new revision | (a) `gatherRetainedSourceWindowForSlot(expectedRevision = new)`; (b) the retained lane for `expectedRevision = retained`; (c) `tryGatherPublishedSnapshotEventsInWindow` | (a) `RevisionMismatch`, **nothing inspected, nothing appended**; (b) `Served` with the older revision's content (a caller that asks for exactly it is still answered); (c) the **current** root answers non-empty | a gather that answers the current revision from a lagging retained source (the divergence this case exists for), or a refusal that spends work |
| **W-37a-30** an **edited** revision is resolved the same way by both lanes | the two-layer crossing revision of I7 (record pair 200/800, overdub pair 100/300 and a surviving pair 150/160, a pinned Active **Delete** of the overdub's note), with a **current** root published for the same revision whose events are the **post-edit** content | compare the two lanes for the two-bar window at playhead 500 | equal element-wise, and both **omit** the deleted note's On and Off while both keep the record pair and the surviving pair | a root lane that carries the deleted note (or the retained lane that drops a surviving event) — the post-edit agreement is where the two rules could legitimately have drifted |

**Preserved invariants (asserted, not assumed).** The requester still serves a root-backed slot from the **root** (its
published window carries that root's identity) — the fallback/root lane is not displaced by the retained lane's presence;
the merged-window fallback itself is untouched; a refusal never mutates the retained source, the index or the served
window; `activeLoopIndex` is still the activation authority; no new owner, buffer lifetime or content authority exists.

**Differential oracle.** The **published-root enumeration** (`Loop::tryGatherPublishedSnapshotEventsInWindow`) for the
same revision and window — the primitive both the shipped window build and the merged fallback read — compared against
3.7a's retained gather. The effective-content owner (`Loop::copyEffectiveCommittedEventsInRange`) keeps its own role
(I1/I7's oracle) and is **not** substituted for it: the two questions are different, and this increment answers the one
the ladder left open. A difference is a finding, never a tolerance.

## Controls and coverage

- **Rule reuse, not rule re-invention:** the fixtures build their shapes with the suite's existing helpers
  (`commitBarSpacedPassIntoTrackSlot` / `publishBarSpacedRootForSlot` — a deliberate pair with the *same* bar-spaced
  notes — and I7's `prepareCrossingEditedRevision`), and both lanes are driven through their own entry points, never
  through a copy of either.
- **Production paths exercised:** `Track::requestPlaybackWindow`, the maintenance turn
  (`Track::stepPlaybackViewMaintenance`), the published-window read, and the two enumerations themselves.
- **Named, not implied, in the comparison:** the walk compares the events' **wire identity** (tick, type, pitch, velocity,
  channel). `noteId`/`eventId`/`canonicalOrder` are each lane's own bookkeeping — the root's are assigned by the
  publication, the bodies' by the store — so comparing them would test the fixtures' construction rather than the parity.
  The root lane's order is the published `canonicalOrder` and the retained lane's is tick order: this construction makes
  the two coincide, and **same-tick order stays 4.3's gate**.
- **Boundedness:** the root lane is read for a two-bar window; the retained lane is driven call by call inside a stated
  bound and asserted to stay inside the slice budget. No fixture drives the whole loop through the production turn.
- **Untestable before link:** none of the four cases needs a device; no device claim is made and no row is added.
- **Allocation:** no lane is asked to grow a buffer beyond the fixtures' own `SessionMidiEventVec` scratch; the
  per-layer scratch stays the caller's, as I7 left it.

## Red-proofs (planned, each restored byte-identically and sha256-checked)

| ID | Mutation | Must fail |
|---|---|---|
| **RP-I8-window** | the retained gather answers from the interval's **start** rather than the window's (`windowStart` ignored) | W-37a-27/W-37a-28 (the lanes stop agreeing) |
| **RP-I8-revision** | the retained gather's `expectedRevision` guard dropped | W-37a-29 (the lagging source answers the current revision) |
| **RP-I8-branch** | `requestPlaybackWindow` takes the retained branch even when the root enumeration answers (the compatibility control the G2 packet names: the fallback bypassed during the slice) | W-37a-27 (the published window no longer carries the root it read) |

A mutation that does not fail its fixture is recorded as a non-red-proof with its mechanism named, never reworded.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (baseline: **166/166**). The change's neighbouring
  suites (`test_window_paged_legacy`, `test_windowed_preview_integration`) unchanged.
- Broad: `pio test -e native` (baseline: **2,844 cases / 7 skipped / 2,837 succeeded / 0 failed**).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` / `code 392,248 + padding 968` (floor 840). A test-only increment must leave that line
  **byte-identical**; a `.data` byte that snaps an alignment edge, if one appears, is a finding to fix rather than to
  re-base.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN** (no device-visible behaviour change is claimed; the lane has no row family on the shipping env).
- Evidence path: `evidence/2026-10-06-s37a-i8-lane-parity/`.

## Completion record

**Slice I8's record (2026-10-06).** Landed as designed, **test-only**: `git diff --stat -- src include` is empty, and one
red-proof had to be re-expressed while another turned out not to discriminate the case the packet predicted. Full detail
and the raw numbers: [`EVIDENCE.md`](EVIDENCE.md).

- Oracle/contract unchanged: **YES** — lane agreement on the events' wire identity for one revision, plus the named
  refusals; the effective-content owner keeps its own role (I1/I7's oracle) and was not substituted for the root lane's.
- Fixtures implemented and discriminating: **YES** — `test_3_7a_i8_the_two_lanes_serve_one_revision_identically`
  (W-37a-27), `..._the_two_lanes_agree_at_every_window_across_the_loop` (W-37a-28),
  `..._a_revision_only_one_lane_holds_is_refused_by_name` (W-37a-29),
  `..._an_edited_revision_is_resolved_the_same_way_by_both_lanes` (W-37a-30). **The parity holds**, including on the
  edited revision — the compatibility statement 3.7b's removal rests on.
- Red-proofs: **three, all RED with byte-identical restores** (`src/Track/TrackPlaybackWindowBuild.cpp` sha256
  `48c57b2fe9d7c78e…`, the pre-slice baseline; the harness restores from a sha256-checked copy, never `git checkout --`):
  **RP-I8-window** (W-37a-27/W-37a-28, *Expected 4 Was 48*), **RP-I8-revision** (W-37a-29 and I1's fixture,
  *Expected 4 Was 0*), **RP-I8-branch** (W-37a-29, *Expected 0 Was 5*). **Two deviations recorded rather than reworded:**
  RP-I8-revision's first form — the gather's own source guard alone — was a **non-red-proof** (the interval index refuses
  the foreign revision first; the same layering I1's MR-3.7a-B/B' records) and was re-expressed to drop both layers; and
  RP-I8-branch was first far too broad (`if (true || …)`, 44 fixtures) and, narrowed to the shape the control is about,
  it fails **W-37a-29, not W-37a-27** — a **correction to this packet's prediction**, because the identity a window
  carries is the **loop's** published root whether or not that root was the enumeration used, and the two lanes answer
  identical content by the parity itself. W-37a-27's compatibility half is presented as what it is (the root lane is
  still answerable and untouched), and the fixture says so.
- Native verification: **GREEN** — focused **170/170** (baseline 166/166); full native **2,848 / 7 skipped / 2,841
  succeeded / 0 failed** (baseline measured with this slice stashed: 2,844/2,837).
- Build/RAM1: **GREEN and byte-identical** — `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` /
  `locals 34,880`, floor 840, 128 B of headroom. No re-base; the increment changes no production line.
- Hygiene: `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- Device: **NOT RUN** — no device-visible behaviour is claimed (the lane has no row family on the shipping env).
- Task checkoff: **3.7a stays open** (3.7b's conditions unchanged), as do 3.12/3.13/3.14/4.5. What this increment closes
  is the ladder row I4 named as *"differential fallback parity"* — the last unlanded item of the I1 packet's four-row
  ladder.
