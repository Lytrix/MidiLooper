# 3.15d slice 3 — completion record (native)

Packet: [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md). Slice 2:
[`../2026-10-21-s315d-slice2-edge-adoption/EXECUTION.md`](../2026-10-21-s315d-slice2-edge-adoption/EXECUTION.md).
Branch `feature/unified-rolling-playback-path`, base `0f71c75d` (slice 2), commit `2b876c84`.

## Order executed

Packet first (scope + oracle), then the four fixtures against unmodified production, then the changes the REDs proved.
The packet's expected-RED paragraph was wrong in the useful direction: the parity, mutation and save shapes were green
immediately, and the RED was in the lifecycle — a **refused** preview demand.

## What the measurement said before any code

| Probe | Reading |
|---|---|
| Two-bar loop, session open at phase 500 | The open's preview request is refused `CannotCompleteBeforeHandoff` and **retained** (`reason=2`); `previewSourceActive=0`, nothing publishes |
| Same, after a requester visit | Unchanged: the retained promise is the lane's standing state |
| Session closed | The promise is **still retained** — `cancelPreviewWindowForSlot` early-returns on `!previewSourceActive` |
| Committed requester after the close (two-bar) | `Excluded` — for the loop's own `win_window_ineligible` reason, so the poisoning is invisible *there*; on a demoted loop it is measured by the mutation below (`after=2` = `Busy`) |

That is why the slice has two production edits and three pins: parity, mutation and the save already held; the promise
lifecycle did not.

## RED → GREEN, and which edit each one needs

`test_3_15d_slice3_a_refused_preview_demand_is_released_at_the_session_edge` is the discriminator. Its final form arms a
pending display refresh **before** the close, so the close's own flush runs the requester while the session is still
active — the harder path, and the one that makes the second edit necessary.

| Tree | Result |
|---|---|
| Base (`0f71c75d`), fixture as first written | RED at *"the exit releases the lane's own promise"* |
| + release in `cancelPreviewWindowForSlot` (edit 1) | first form GREEN; **RED** once the fixture arms the close-flush |
| edit 1 + a release in the requester's post-exit branch | still **RED** at the same assertion (the close re-retains after the cancel) |
| edit 1 + **flush-before-yield** in `closeNoteEditSession` (edit 2) | **GREEN**, 29/29 |
| edit 2 without edit 1 | RED (the refused demand is never released at all) |
| edit 1 removed, fixture's final form (mutation) | RED at *"the committed lane is no longer held out by the stale demand"* with `after=2` (`Busy`), `promiseSurvivedExit=1` — the poisoning is **measured**, not inferred from the guard's order |

The requester-side release (third row) was **removed**: with edit 2 in place no fixture can make it necessary, and the
slice-2 rule is not to keep unproven guards. Recorded rather than shipped.

The other three fixtures were green before any edit and stayed green:
`..._short_and_long_note_edit_emit_the_same_stream`,
`..._an_edit_while_the_preview_serves_is_carried_by_the_next_revision`,
`..._a_save_while_the_frame_serves_leaves_it_serving`.

## The changes (17 lines of production, comments included)

1. **`Track::cancelPreviewWindowForSlot`** (`TRACK_COLD_MEM`) releases the lane's **own retained demand** before its
   `previewSourceActive` early return, keyed on `pendingViewRequest_.source.origin == Preview`. Every session edge
   already calls the cancel unconditionally (exit, slot depart, selected-track depart), so no call site moved.
   DEC-108 §13c retention ("a refusal is a promise") is unchanged for every demand whose owner still exists; this ends a
   promise whose owner — the session projection — is being cancelled.
2. **`EditManager::closeNoteEditSession`** flushes the **last display advance before** the yield. The flush is the
   session's revision-advance seam and runs the preview requester; doing it after the cancel prepared a preview for a
   session that was already over (and, when refused, left the promise behind), while doing it first lets the cancel own
   the last word: an admitted source becomes the owed handoff, a refused one is released.

No constant, envelope term, route or pin moved; both edits are in flash-resident code (`.cold` / the EditManager TU), so
both images are byte-for-byte the same size as before.

## Fixture discoveries (harness, not production)

- A second `commitEmissionLoop` on the **same** slot shares the first pass, and the first half's wrap moves the track's
  `projectionCycleStartTick`; a mid-fixture long-loop half then computes its phase from the wrong origin and serves an
  *empty* window (measured: `cov=[46948,1536)`, `events=0`, `rev=2`, `sawPreview=1`). The parity fixture gives each loop
  its own slot and states its own cycle origin.
- The mutation fixture must count the emission of the turn that **establishes** the serving frame, not only the turns
  after the mutation — the crossed note's On belongs to the pre-mutation turn.
- A crossed note whose Off lies past the walk's end is a fixture error, not a seam error: walking past 1 900 is what makes
  "one On, one Off" measurable.

## Gates

| Check | Status | Evidence |
|---|---|---|
| Focused native | **PASS** | `test_windowed_preview_integration` **29/29** (25 existing + 4 new) |
| Full native | **PASS** | `/tmp/native_s3_final2.log`: **3 370 / 7 skipped / 3 363 succeeded / 0 failed** |
| `teensy41-capture-serial` | **PASS** | `variables 96 192 / code 392 216 / padding 1 000 / locals 34 880` — the approved pins, unchanged |
| `teensy41-capture-serial-window-qualification` | **PASS** | `variables 96 192 / code 392 376 / padding 840` — the approved floor, unchanged |
| `openspec validate --strict` | **PASS** | valid |
| `git diff --check` | **PASS** | clean |
| Device | NOT RUN | route decisions and their qualification are 3.15f's; the release is observable only where a demand was retained |

## Not claimed

- **Route parity for short loops is open, not conceded.** A whole-loop interval needs `768 + grains × 8 × 64` ticks of
  lead (2 304 at a three-grain price) and a two-bar loop's lead tops out at 1 535, so the window reader cannot serve it
  at any playhead. The slice pins **content** parity — the same emitted stream from the merged fallback and from the
  preview frame — and records the route limit as a property of the declared envelope, not of this slice.
- **The save fixture** measures the save against the **serving** frame, after the checkpoint is settled (the settle is
  fixture scaffolding and may legitimately move the lane); it does not claim the settle is side-effect free.

## Conclusions taken after the slice (owner, 2026-10-21)

**The recorded limit is a supported-route failure, not a fallback pass.** Slice 3 proved **content parity** and the
correct **lifecycle of a refused request**; it is explicitly not evidence that the two routes are identical. Read
against the authority, that means the merged fallback it measured is not a sanctioned terminal geometry: DEC-116 — one
rolling playback path per sounding slot requires the unified request → bounded owner turn → complete immutable ready
window → clock-edge adoption → ordered emission/ledger route "for every sounding slot, regardless of loop length", and
the spec's *All scheduled loop playback has one emission contract* scenario names a mutable merged branch as "a named
temporary migration fallback with a removal trigger, not the final playback authority". 3.7b is that trigger, so a
refusal on a two-bar NOTE_EDIT cannot be scored as a pass — which is why 3.15d stays unchecked.

**What is open is the coverage shape, not the route.** The whole-loop interval this slice could not admit
(`768 + grains × 8 × 64` ticks of lead needed; a two-bar loop tops out at 1 535) is the *current request's* shape, and
neither the spec nor DEC-116 requires it: the spec says a short loop "**may** have coverage spanning its whole loop", and
DEC-116 §3 keeps the span "a tunable, not a fixed bar count". So the obligation that survives this slice is **route
identity — the same request, owner turn, ready window, clock-edge adoption and emission/ledger contract — with correct
emitted and ledger behavior**. Whole-loop interval identity is not itself required. Three mechanisms can make the route
admissible for short loops, and choosing among them is 3.15f's:

1. **Sub-interval coverage** — request an interval the envelope admits, carrying the required wrap/closure and handoff
   cadence.
2. **Whole-loop coverage** — at a span/cost policy that is measured rather than assumed.
3. **Position-free preparation** — the DEC-136 shape for a demand the positioned envelope can never admit.

None of the three may weaken the DEC-136 — private committed-root preparation — envelope, and none was touched here.

**Consequences recorded in the docs:**

- `tasks.md` 3.15d keeps its first clause open as a literal route claim and states why the task stays unchecked.
- `tasks.md` 3.15f carries the delegated decision as **route parity, not whole-loop coverage parity**, with the three
  mechanisms named and the cutover gate defined as route identity plus correct emitted/ledger behavior.
- The parity fixture's own description calls itself content parity only, so a later reader cannot mistake it for a route
  equivalence proof.

No admission constant was touched and no owner moved (this is task scope, not an ownership transfer), so no DEC was
required.
