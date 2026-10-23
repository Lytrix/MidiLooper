# Bounded immutable event-window source cursor

Task 3.10 — requester/retry integration; baseline 25542299 plus uncommitted
completion-clock and requester-safety repairs. Status: specified before code.

## Decision review / contract
Reuse DEC-108 — windowed publication's selected LoopSnapshot effective-content
owner and generation-qualified candidate requirement. Extend its query boundary,
not raw-pass reconstruction. Reuse LoopSnapshot's final-reference retirement
helper (releaseSnapshotReference); no second scheduler or whole-loop root lease.
Architecture rules explicitly permit methods/state on an existing owner.
No ownership transfer, persistence/undo change, clock change, or capacity change.

Owner: LoopSnapshot::EventWindowCursor, supplied by a future requesting candidate
role. At begin it pins at most three bar-membership heads intersecting one linear
window of at most two bars (an unaligned two-bar interval intersects three bars).
Pins cover immutable existing event pages, not event copies or the root; unchanged
membership is shared. Busy/invalid admission mutates nothing. Empty coverage is
valid. Reading never consults the root after admission. Each step inspects one
reference OR moves one page/bar; nonmatching entries count too. Output retains
canonicalOrder and eventId: page traversal is NOT global canonical playback order.

The fixed metadata is independent of loop length. Referenced-body cost is at most
three memberships, each under the existing 4096-reference/bar envelope. This is
not a claim that all referenced events fit the 2048-event output limit; later
admission must count and refuse overflow without truncation. No runtime member is
added yet, so runtime size pins/RAM1 must not grow.

Cancellation clears raw traversal pointers, not owning pins. Retirement uses the
existing one-child/emptied-node quantum; shared descendants are untouched. Retiring
cursors cannot read, rewind or rebind. Active-transport owners MUST drain retirement
before destruction (same precondition as LoopSnapshot::ReleaseCursor); no weak
reference resurrection during retirement. No allocation or recursive tail teardown
inside begin/read/retirement steps. Caller revision/generation qualification remains
the future Track binding's job; this cursor introduces no second currency counter.

## Transition oracles
C1: build >one page per bar with deliberately nonmonotone canonical order. Pin an
unaligned two-bar window; retire the root; root and unrelated note/event weak refs
expire, selected rows survive. Each read costs exactly one unit (including rejected
rows/page/bar transitions); output values/id/order match original effective rows
exactly once. Rewind repeats the same immutable content. Begin while busy refuses.
C2: cancel halfway; reads/rewind/rebind refuse; zero retirement quantum changes
nothing; each retirement call advances <=quantum; final selected weak refs expire;
cursor can then be reused. Shared sibling memberships remain readable every grain.
C3: empty window terminates in bounded bar steps; zero length/out-of-range/wrap/
overwide/overflow inputs refuse without disturbing state. Repeat same window on
short/long roots: equal inspected work. No whole-root retention or allocation.
Mutations: remove tick filtering; drop a window pin; collapse release into recursive
reset; each intended assertion must fail and source restore byte-identically.

## Checks
`pio test -e native -f test_loop_event_store`; full `pio test -e native`;
`pio run -e teensy41-capture-serial`; strict OpenSpec and diff check.
Hardware NOT RUN: source primitive has no live caller yet. This slice does not
claim requester scheduling, canonical playback ordering, boundary consumption, or
a supported timing envelope. Integration follows after those bindings are complete.

## Completion
Verified source-query prerequisite; no runtime binding or live activation.

- Focused `pio test -e native -f test_loop_event_store`: **47/47**.
- Final full `pio test -e native`: **2,750 total / 2,743 passed / 7 skipped /
  zero failed**, 92.008 seconds. Earlier pre-review run also passed (269.636 s).
- Capture build: **SUCCESS**, 47.384 seconds; RAM1 variables **96,192**, code
  **390,536**, padding **2,680**, locals **34,880**; floor **840** unchanged.
  No existing runtime/root layout changed (nested type, no runtime member).
- One bounded review found missing exact endpoint tests, not a code defect.
  Added start-inclusive/end-exclusive cases; both endpoint mutations fail.
- Five mutations fail intended assertions: filtering, middle-bar pin, recursive
  release, exclusive start, inclusive end. Every restore byte-identical.
- Strict OpenSpec validation and `git diff --check`: PASS.
- No upload, HITL or commit. Firmware image/hash and mutation hash in
  `source-cursor-verification.json`. Logs in session scratch, prefixed
  `window-cursor-`; final native log is `window-cursor-native-final.log`.

## Measured and structural bounds
The 120-reference fixture emits exactly 80 in-window events and costs exactly
**132 read units** (120 references + 9 page exits + 3 bar exits) at 16/64/512 bars.
The empty unaligned two-bar window costs three bar exits, not a hidden scan.
Root/unrelated note/unrelated event weak references expire while selected window
rows remain alive. Two cursors share heads safely; one retires in three units while
the other keeps reading. Unique retirement releases at most one event leaf/unit.
Snapshot allocator counter stays unchanged across cursor admission/read/release.

Construction enforces `kSnapshotMembershipMaxPages` (256), including pages left
partly empty by edits. Therefore the cursor declares read and release upper bounds
of **3 × (256 × (16 + 1) + 1) = 13,059 units**. These are source-work bounds,
not 13,059 events, not a device-time guarantee, and not a claim that all input fits
the 2,048-event destination. Fixed cursor metadata and window-only page/body pins
must be charged when a candidate runtime role adopts this primitive.

## Next binding / unchanged exclusions
The existing `RuntimePlaybackViewSource` requires indexed, count-qualified rows.
This cursor deliberately exposes inspected-work steps and full ordering metadata,
so it must not be connected by hiding a scan inside `readEventAt`. Implement a
bounded count/fill/order binding in the existing owner, retaining the original
request deadline and charging source traversal as well as output sorting. Canonical
order/eventId tie handling is still needed: membership-page order is not a global
playback ordering authority. Wire candidate lifecycle/cancellation/retirement and
pending-only service before enabling the requester. Then qualify paired slot phase,
shared-builder exclusion, boundary consumption, and actual device timing.

This is an implementation of DEC-108's bounded selected-snapshot query and the
existing snapshot retirement contract, not a new publication/retention policy.
Window pins do not replace the serving/candidate/retiring retained revision roles
or claim that future windows can be reconstructed from this cursor alone. No
new product exclusion or decision supersession; no deliverable shipped.
