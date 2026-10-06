# Task 3.7a (I8) — the **differential lane parity** measured: the published-root lane and the retained-source lane serve one revision identically

**Slice:** `windowed-playback-generation` / task **3.7a**, increment **I8** · **Branch:**
`feature/windowed-playback-generation` · **Date:** 2026-10-06 · **Firmware:** *unchanged* (test-only increment:
`git diff --stat -- src include` is empty) · **Device:** NOT RUN (no device-visible behaviour is claimed; the lane has
no row family on the shipping env).

Packet written **before** the edit: [`EXECUTION.md`](EXECUTION.md). Authority: `tasks.md` item **3.7a** (its verify
clause names *differential fallback parity*), `G2-EXECUTION-PACKET.md` §"Slice 3.7a" (its transition oracle asks to
compare the selected output to effective-content truth; its red-proof list names *"allow fallback removal during the
slice (compatibility differential control fails)"*), the I1 slice packet's ladder row **I4** (*"the shipped-vs-fallback
comparison over the same revision"*), **DEC-108** §10/§13(c)/§14(c) and **DEC-114**.

## 1. What the increment is, and what it is not

The ladder's last unlanded row is the **differential fallback parity**. Three properties already had evidence, all of
them against the *effective-content owner* (`Loop::copyEffectiveCommittedEventsInRange`): the retained gather answers a
rootless interval (I1/I2), its per-layer resolution of the pinned revision's note edits equals that owner's (I7), and the
paged lane's emission equals it pass-exact (I3c-c/I5). What nothing had compared is the other enumeration of the same
revision: the **published snapshot root** — the primitive the shipped window build of `Track::requestPlaybackWindow` reads
*and* the one the merged-window fallback reads (`ensurePlaybackMergedMidiEventsBuilt` →
`Loop::tryGatherPublishedSnapshotEventsInWindow`) — against the retained lane's **pinned bodies**. Task 3.7b removes the
live mutable fallback and keeps a cold reference; if the two enumerations can differ for one revision, that removal
changes what a slot plays. This increment **measures** that comparison. **No production line changed**, and a difference
would have been a finding rather than a tolerance.

## 2. The oracle, and the shape it is measured on

* **Shapes** (all through the suite's own helpers, never a copy of a production path):
  * W-37a-27/28/29 — slot 0 holds a bar-spaced committed record pass over **24 bars** (`commitBarSpacedPassIntoTrackSlot`:
    On@bar×768 / Off@bar×768+48, channel 1, pitch 48, velocity 100/0) **and** a **current** published root for the same
    content and revision (`publishBarSpacedRootForSlot` — a deliberate pair with the same notes); the runtime is
    prewarmed and the same revision is **retained** (`PlaybackSourceRetainResult::Admitted`). 24 bars is over
    `DisplayWindowUtils::kMaxDetailedWindowBars` (16), so the windowed shape applies and the requester's own predicate
    agrees (`shouldAvoidFullVisualRebuild`).
  * W-37a-30 — I7's two-layer **crossing** revision (`prepareCrossingEditedRevision`: record pair 200/800; overdub pair
    100/300 plus a surviving pair 150/160; a pinned Active **Delete** of the overdub's note) with a current root
    published for the revision carrying the **post-edit** content.
* **The window** is the resolver's own two-bar interval (`Track::playbackWindowInterval`, start 0 at playhead 500, 1536
  ticks).
* **The comparison** is on the events' **wire identity** — `(tick, type, channel, note, velocity)` element-by-element,
  with the sizes asserted first. `noteId`/`eventId`/`canonicalOrder` are each lane's own bookkeeping (the publication
  assigns the root's, the store the bodies'), so comparing them would test the fixture's construction instead of the
  parity; and this construction makes the root's published `canonicalOrder` coincide with tick order, so **same-tick
  order stays task 4.3's gate** rather than being claimed here.

## 3. Measured

| Case | Result |
|---|---|
| **W-37a-27** one revision, two lanes (playhead 500) | root lane: answered, path **`Snapshot`** (a *current* root, not the stale-root overlay), 4 events, `inspectedEntries > 0`. retained lane: `Served`, `complete`, `indexAnsweredEmpty == false`, `pinnedLists == 1`, `inspectedChunks > 0`, driven over more than one call with each call inside `kRetainedGatherSliceEvents`. The two answers are **equal in size and element-wise identical**. Through production: `requestPlaybackWindow` → **`Admitted`**, its shuffle equals the **root's** answer, the published window carries the loop's published root identity, `origin == Committed`, revision equal, and the **served** window's content equals the **retained** lane's answer |
| **W-37a-28** every window of the loop | seven playheads (bar-aligned and not, from tick 1 to the last tick, whose window is clamped to the loop's tail): every window answers `Snapshot` and the two lanes agree element-wise; **5** of the windows carry content and the sweep reaches **13** of the loop's 24 bars, so the agreement is not vacuous |
| **W-37a-29** a revision only one lane holds | the loop's revision advances past the retained source and a current root is published for the new revision. The retained lane asked for the **current** revision refuses **`RevisionMismatch`**, inspecting **0** chunks and appending **0** events; a caller asking for exactly the revision the source holds is still served; the current root answers and the requester serves **it** (`Admitted`, identical to the root). And because the content did not change with the revision, the current root's answer equals the older revision's retained answer |
| **W-37a-30** an edited revision | both lanes answer **4** events; both **omit** the deleted note's On@100 and Off@300 and both keep the surviving pair (On@150) and the record pair (200/800); element-wise identical; and the owner's effective-content answer agrees with the retained lane as well |

**The parity holds.** That is the increment's claim and it is the compatibility statement 3.7b's fallback removal rests
on: for one revision, the lane that answers today and the lane that will answer afterwards serve the same events —
including on an edited revision, where the publication's flattened membership and 3.7a/I7's per-layer resolution could
legitimately have drifted.

## 4. Red-proofs

| ID | Mutation | Result |
|---|---|---|
| **RP-I8-window** | the retained gather answers the **whole loop** at both traversal sites (the window it was asked for is ignored) | **RED**, and exactly on this increment's fixtures: W-37a-27 *"Expected 4 Was 48. one revision, two lanes: the same number of events"* and W-37a-28 *"Expected 4 Was 48. every window of the loop agrees between the two lanes"*. 2 failed / 169 |
| **RP-I8-revision** | the gather's `expectedRevision` guard dropped **and** the index query passed the source's revision instead of the caller's | **RED** on W-37a-29 *"Expected 4 Was 0"* (`RevisionMismatch` vs `Served`) **and** on I1's own fixture *"Expected 4 Was 0"* (line 5112). 2 failed / 169 |
| **RP-I8-branch** | a retained source makes the requester **prefer** the retained branch even where the published root answers (the compatibility control's failure mode) | **RED** on W-37a-29 *"Expected 0 Was 5"* — the requester refuses (`SourceUnavailable`) a slot whose root could answer, because the preferred retained source **lags** it. 1 failed / 170 |

Each mutation was restored **byte-identically** (`src/Track/TrackPlaybackWindowBuild.cpp` sha256
`48c57b2fe9d7c78e451b24c170898103857a44fad62625e5d7b9486b7b84d4c7`, the pre-slice baseline, and
`git diff --stat -- src include` empty afterwards). The harness copies the file to a sha256-checked backup and restores
from it — **not** `git checkout --`, which is the trap I7's evidence records.

### Recorded, not smoothed over

* **RP-I8-revision's first form was a non-red-proof.** Dropping **only** the gather's own `source->revision !=
  expectedRevision` guard changed no outcome: the interval index refuses the foreign revision first
  (`queryRetainedIntervalForSlot` → `retainedRevisionIndex`'s own revision), so the named `RevisionMismatch` still
  arrives — the same layering I1 recorded for its **MR-3.7a-B**/**MR-3.7a-B'** pair. The mutation was re-expressed to
  drop **both** layers (which is the claim's whole mechanism) and is red as above.
* **RP-I8-branch's first form was far too broad, and its narrowed form does not fail W-37a-27.** The first form
  (`if (true || …)`: never consult the root) bypasses the root lane for *every* window request and fails **44** fixtures
  across the suite; it was narrowed to the shape the control is about (a retained source present). The narrowed form then
  fails **W-37a-29, not W-37a-27** — and that is a **correction to the packet's prediction**, with its mechanism named:
  the identity a window carries is the **loop's** published root (`requestPlaybackWindow` builds the source identity from
  `loop.publishedSnapshotIdentity()`), whether or not that root was the enumeration used, and the two lanes answer
  *identical content* by the parity measured above — so W-37a-27 cannot discriminate the branch choice at all. The branch
  choice is discriminated exactly where preferring the retained lane **changes an outcome**: when the source lags and the
  root could still answer. The fixture's own comment says so, and its "compatibility control" half is now presented as
  what it is (the root lane is still answerable and untouched) rather than as the branch discriminator.
* **A process incident, recorded rather than glossed:** two full-native runs reported **2,679** cases with the suite
  table *not* read past its tail. The cause was mine — a comment edit had swallowed two statements of W-37a-27's
  compatibility half (`use of undeclared identifier 'again'`), so `test_runtime_working_set_ownership` was **ERRORED**
  while the tail I read showed only `PASSED` rows and "0 failed". Fixed, re-run, and the figure recorded below comes from
  a run whose table carries **no** `ERRORED`/`FAILED` row.

## 5. Verification

| Check | Result |
|---|---|
| Focused `pio test -e native -f test_runtime_working_set_ownership` | **PASSED — 170/170** (baseline **166/166**; four new cases) |
| Full native `pio test -e native` | **2,848 cases / 7 skipped / 2,841 succeeded / 0 failed** (baseline measured with this slice stashed: **2,844 / 7 / 2,837**, matching I7's recorded figure; +4 = this increment) |
| Capture build `pio run -e teensy41-capture-serial` (RAM1 guard) | **SUCCESS**, line **byte-identical** to the pre-slice baseline: `variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`, floor 840, **128 B** of headroom |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat -- src include` | **empty** — a test-only increment |

## 6. Named, not claimed

* **No device leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b).** Nothing in this
  increment is observable on the shipping env (the lane has no row family), and no build was uploaded.
* **The comparison is one revision at a time, and only for a *current* root.** The stale-root `Overlay` path is 3.6b's
  one-pass-late allowance and is deliberately not compared here; W-37a-29's negative direction is the closest statement
  (each lane answers only for the revision it holds, and the current root serves the current revision).
* **Same-tick order stays 4.3's gate.** The root lane's order is the published `canonicalOrder` and the retained lane's
  is tick order; this construction makes them coincide, so no same-tick claim is made.
* **Nothing is claimed about note/event identity**, channel remapping, or the pane the events are displayed in; the
  comparison is the events' wire identity only.
* **The pager, the preview session, retained-source release and the demotion matrix are out of scope** (3.12/3.13/3.14's
  own tasks), as is the pin filter's production-unreachable state (I7's note) and the per-layer scratch's admission
  gating (DEC-115's follow-up list).
* **`retainPlaybackSourceForSlot` still has no production caller** (the 2026-10-06 pool-attribution finding stands): this
  increment compares the two lanes through the production requester and the gather, and it does not add a retainer. What
  it establishes is that the lane *is* content-equivalent where a retained source exists, so a later retainer or a
  demotion edge cannot change a slot's content.
