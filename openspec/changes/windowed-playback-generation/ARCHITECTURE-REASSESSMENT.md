# Architecture reassessment — `windowed-playback-generation`

**Status:** original 2026-10-02 reassessment for **DEC-108 — windowed playback publication** is historical for the representation split. The 2026-10-07 addendum below records the user-approved single-path target under **DEC-116 — one rolling path per sounding slot**. Neither review authorizes firmware without its own G0–G2 packet or proves timing/capacity.

## Reason triggered

Three formal triggers fire together:

| Trigger | Why |
|---|---|
| **New owner introduced** | The window-build/publication owner becomes `Track::stepPlaybackViewMaintenance`, which **does not exist in code** today (`ExecutionBounds.md` rows 27–29 call it a declaration; the `CoverageServiceContract` struct and `TrackPlaybackRuntime::beginCoverageBuild` machinery do exist). |
| **Ownership transfer required** | Steady-state window publication moves off the incidental path it sits on today (`Track::ensurePlaybackMergedEventsForSlot`, reached from the `SlotProvision` turn, `src/MidiButtonActions.cpp:59`, `src/main.cpp:234`) to that owner. `SlotProvision` keeps requesting preparation and reading readiness. |
| **Timing model changes** | Readiness becomes a completed-publication predicate with a tempo-derived deadline, retained previous versions and a bounded service contract on the turn that produces them — replacing "build inline on the live turn when no pair/view is available". |

Supporting evidence, all read at HEAD this session: `docs/Authority/Architecture/ExecutionBounds.md` rows 2, 27–29 and
the `CoverageServiceContract` row; `src/Track/TrackPlaybackWindowBuild.cpp` (window build, readiness, fallback);
`src/Track/TrackPlaybackHotPath.cpp:287-314` (the three consumption paths); `src/Loop/LoopSnapshotLifetime.cpp:49-76`
(DEC-073 currency); `include/TrackPlaybackRuntime.h:317, 322` (the working window) and
`src/Track/TrackPlaybackRuntime.cpp:2246-2249` (the size pins).

## Current architecture

Active playback is served by **three representations**, chosen per slot per turn
(`src/Track/TrackPlaybackHotPath.cpp:287-314`):

1. **prepared pair** — admissible short loops (`fixedView == nullptr && !shouldAvoidFullVisualRebuild(...)`), consumed
   through `resolvePreparedPairConsume(...)` against `{playbackRevision, playbackGeneration, publishedSnapshotIdentity}`;
   the previous pair is already held while a replacement prepares (`PreparedPairConsumeDecision::held`).
2. **fixed view / bridge** — whole-take admissible slots, published only while every track is stopped
   (`publishPreparedPlaybackPairFromWorkingWindow`, and the fixed-view publish gated on `!anyTimingCriticalTrackActive()`).
3. **merged window** (`PlaybackMergedMidiEvents`, `LoopPlaybackRuntime::mergedMidiEvents`) — the fallback
   (`useMergedFallback`), i.e. the long-loop windowed path and the cold first audible turn. It is documented as the
   off-clock preparation's **working window**; the live turn may build it, and `ExecutionBounds.md` row 2 classes that
   build **Unbounded** (2 bars on long loops, the whole loop on short).

Owner of the state: `Loop` owns passes, the published root and `playbackRevision`; `Track`/`LoopPlaybackRuntime` own the
per-slot working window, the prepared pair and the immutable views; `TrackManager` owns transport orchestration and the
commit binding; `StorageManager`/`TrackUndo` consume the published root as the checkpoint authority. `activeLoopIndex`
stays the sole authority for which slot is active (DEC-105). `committedPassRevision()` **is** `playbackRevision` by
contract, and coverage — not the revision stamp — decides currency (DEC-073).

## Proposed evolution (historical DEC-108 representation split; superseded by 2026-10-07 addendum)

**Extend, do not replace.** Per DEC-108: keep prepared-pair and fixed-view playback for admissible loops; introduce
**immutable interval publication for demoted/windowed loops** inside the existing view set (window-sized, no restored
whole-take reservation); treat a whole-loop view as coverage that spans the whole loop; give the window the pair's
existing retention semantics as a **pinned source revision** rather than a copied buffer; make readiness a completed
publication with matching identity and sufficient coverage (empty interval = valid silence); and move the steady-state
build/publication onto `Track::stepPlaybackViewMaintenance` under the declared `CoverageServiceContract`.

Ladder position: **no new owner is invented** — the owner is the one `ExecutionBounds.md` already declares as a required
implementation contract, and its admissibility condition ("maximum phase advance, maximum consecutive denied turns,
required grain count, lead margin") is adopted rather than relaxed. What is new is the implementation and its proof.

## Migration impact

- **Files (at Apply, none now):** `src/Track/TrackPlaybackWindowBuild.cpp`, `src/Track/TrackPlaybackHotPath.cpp`,
  `src/Track/RuntimePlaybackViewBuild.cpp`, `src/Track/TrackPlaybackRuntime.cpp` (size pin), `include/RuntimePlaybackView*`,
  `include/TrackPlaybackRuntime.h`, `src/TrackManager/TrackManagerTransportTick.cpp` (the commit binding's readiness
  term), `src/TrackManager/TrackManagerSlotMatrix.cpp` + `src/main.cpp` (the owner's turn wiring).
- **Tests:** new fixtures first (identity/immutability, deadline, one-pass-late, edge allocation, empty interval, stale
  completion, repeated edits, boundary closures, root-demoted playback); `test_playback_prewarm` must stay green and is
  **cited, not restated**.
- **Device/HITL:** a new windowed stimulus profile through the existing content-verification pipeline, canonical
  stimuli preserved; no SD format change and no destructive operation.
- **RAM1/envelope:** the size pins are re-based deliberately with the reason recorded at the assert (currently
  `sizeof(LoopPlaybackRuntime) == 6368`, `sizeof(Loop) == 984`, `sizeof(TrackPlaybackRuntime) == 32`); source retention
  is charged to the envelope; the `padding_floor` is 840 B.
- **Compatibility:** no persistence or undo format change; the published root keeps its checkpoint authority, so S5's
  root demotion stays gated on this change's parity proof.

## Recommendation (historical DEC-108 representation split; superseded by 2026-10-07 addendum)

**One preferred path: implement DEC-108 as written, owner first.** Land `Track::stepPlaybackViewMaintenance` with the
declared `CoverageServiceContract` quantum and its proof fields before any behaviour change, since the admissibility
condition is what `ExecutionBounds.md` gates active windowed playback on; then interval publication, retention and the
readiness term, each behind its fixture. **Confidence: medium.** The owner, the readiness change and the retention
semantics are grounded in existing declarations, code and decisions; the **capacity, scheduling and retention bounds are
unproven**, and the change must not claim `N`, a supported density, or a cleared reservation until they are measured.

**Explicitly not decided here:** the product limit beyond the provisional 2,048-event window capacity; whether smaller
window spans are adopted; the supported `N`; and the removal of the live merged-window build (gated on its replacement
being verified).

## 2026-10-07 addendum — one path for all scheduled loop playback (DEC-116)

### Trigger and present evidence

The user selected a single rolling playback path for **short loops and NOTE_EDIT as well as long loops**. That supersedes this reassessment's § Proposed evolution / Recommendation where they keep prepared-pair and fixed-view playback as permanent alternatives. It changes the timing/serving model and the scope of compatibility retirement, so [the formal timing-model trigger](../../../docs/ARCHITECTURE_REASSESSMENT.md#formal-triggers) applies. No new owner, ownership transfer from `Loop` or `EditManager`, partial root or SD schema is proposed. The present hot-path source selection still branches among fixed view/bridge, prepared pair, paged delivery and mutable merged fallback (`Track::playCommittedLoopMidi`); `servicePreviewWindowForNoteEditSession` refuses short loops and requests whole-loop projection for long ones; Record stop has dedicated first-bar and frozen-source branches. The latest registered default-image Record leg (`captures/s75b-readyrole-20261008-03/`) still has a 2.121 s output gap. These are grounds for a scoped migration, not a shipped single-path claim.

### Alternatives and consequences

| Path | Benefit | Cost / ruling |
|---|---|---|
| One physical buffer for all tracks | Smallest headline memory | Rejected: simultaneously sounding tracks need independent immutable readers; saving their cursors and content elsewhere recreates per-role storage, and a busy builder can starve another track. A shared build scratch is a later measured option, not the current authority. |
| DEC-108 item 2 / D6 split retained indefinitely | Least immediate migration work | Superseded: multiple builders/readers keep duplicating handoff, NOTE_EDIT and wrap semantics. Valid as a **temporary** fallback only with parity gates and a removal trigger. |
| One `Track::stepPlaybackViewMaintenance` driver + one immutable window reader per sounding role, with leased current/ready/build (**preferred**) | Same boundary, source identity, ledger and deadline rules for every loop/mode; failures isolated by role and memory sized to active soundings | Requires source-specific freeze/pin adapters; real lease/transition accounting; a bounded dense-content page representation behind the same reader; controlled native interleavings plus device service/deadline and PSRAM/RAM1 measurements before cutover. |
| Two-bar materialized window by default | More nominal lookahead | Does not by itself explain a missed adoption or owner turn; higher resident cost and fill work. Keep span tunable under DEC-108; measure before choosing it. |

### Ownership and handoff

`Track::stepPlaybackViewMaintenance` remains the sole window request/build/publication owner, using the existing role-owned view set and deferred MIDI-service opportunity. **Per sounding slot** means a logical cursor and lease-qualified `current`/`ready`/private `build`; it never means one mutable current buffer for the whole device. `Loop` publishes only complete `LoopSnapshot` checkpoints and retains canonical content; the Track owner chooses a frozen complete Snapshot, a generation-qualified Record range, live Capture/overdub overlay or an `EditManager`-owned preview delta as its interval source. The preview has its own source/lifecycle revision and `origin == Preview`, not a second committed-content currency. `TrackManager` still owns slot selection and bind, `StorageManager` persistence and `TrackUndo` undo routing. The clock edge reads/adopts a complete source-qualified window and emits ordered MIDI/ledger effects only. Missed readiness preserves a valid prior version for its allowed traversal and is never converted into a passing timing verdict by fallback or silence.

### Migration and verification gates

The sibling `runtime-working-set-ownership` change owns lease/envelope and combined continuity acceptance; this change owns the single-path producer/reader migration. First inventory and fixture all production routes (short/long, empty, high-density restored, Record return, overdub, NOTE_EDIT preview/undo/exit, layered/jam, switch, transport and clock changes). Then move each source onto the **same** request/owner/edge contract under independently reviewable G2 packets; keep legacy routes only while an explicit differential parity/timing comparison is running. Remove pair/fixed/merged/paged **emission branches** after every supported route has proven exact ordered events, ledger closures, freshness and capacity refusal, then reclassify `ExecutionBounds.md` row 2 with evidence. Source pinning, one-pass-late, preservation of unaffected soundings, bounded pages for legacy dense content, fair grains under persistence/finalization contention and the RAM1 floor all remain gates. A new global window buffer, extra Manager, whole-loop root dependency or time-shifting events to mask lateness is rejected. No numeric envelope or code-completion claim is made here.

**Recommendation:** keep the existing `Track` owner and three-view role protocol, but converge to **one window reader/driver for every sounding slot**; do not solve the remaining Record gap by proliferating special consumers. This addendum supersedes only the old keep-pair/fixed recommendation; the original evidence and DEC-108 constraints not named by DEC-116 remain readable as history.

## 2026-10-08 addendum — end-state timing-model review (DEC-116 source/window obligation)

**Status: direction reviewed; production policy and device proof NOT IMPLEMENTED / NOT RUN.** This supplements the accepted DEC-116 — one rolling path per sounding slot addendum above; it neither supersedes DEC-108's source/lag/fairness rules nor asserts that today's rolling qualification lane is the final reader.

### Formal trigger and current architecture

The timing-model trigger applies to changing how `MidiServiceDrain::selectDeferredIdleOwner` grants existing source and playback turns for an affected tick. Today it rotates seven owner kinds, with an extra Snapshot opportunity on backlog. The first Record bar refresh runs in Track's VisualCache maintenance turn; a Record/overdub stop grain has precedence only after its track reaches a deferred maintenance turn. `Track::stepPlaybackViewMaintenance` is the existing window owner and serves one declared grain when selected; across tracks the playback holder choice rotates, and within a track the most urgent owed slot is offered. `TrackManager::updateAllTracks` retains clock-edge slot selection. No outer kind selection currently proves the *composed* source-owner → window-owner → reader handoff deadline. `Track::playCommittedLoopMidi` still selects fixed/bridge, prepared-pair, paged and mutable merged emitters; the rolling qualification path excludes short loops and non-active layered slots. These are VERIFIED code paths, not an attribution of the prior default-image output gap.

### Recommended end state and ownership

**Extend existing owners, not transfer them.** `Loop` alone publishes a complete `LoopSnapshot` checkpoint; it does not publish an interval root. Live Capture, a generation-qualified frozen Record range, selected committed/retained content, overdub Capture and `EditManager`'s NOTE_EDIT delta are source adapters for one Track-owned per-sounding-slot `current`/`ready`/private `build` protocol. `TrackManager` remains sole active-slot selector. A complete, source-qualified immutable `current` is immediately servable and can repeat an unchanged short loop without a new build. A changed interval instead owes a private complete successor by its actual earliest affected tick. A matching Record `current` keeps its physical role/cursor/ledger across stop; a stale or final-geometry-mismatched view cannot be relabeled and the frozen range remains pinned until its corrected successor or selected committed source is adopted. Partial Snapshot construction never gates playback when a qualified Capture/retained source already covers the interval.

The *logical affected-interval obligation* includes only source work that this reader actually needs, private window work, handoff/closure and eventual safe source retirement. Extend the **existing** deferred-owner arbitration with a bounded, deadline-aware opportunity for the required source and playback owners; do not introduce a new queue, Manager, scheduler class, or blanket RECORDING/OVERDUBBING precedence. Keep cross-track playback fairness and bounded turns for Snapshot, accepted stop/finalization, persistence, display and other sounding roles. An owner that cannot progress is named and skipped without losing its obligation. Today's `leadTicks=Config::TICKS_PER_BAR` is a transitional admission input, not a permanent extra margin for an already-served window; removing it alone does not certify a new one-bar successor. The proposed composed service formula and content/closure work remain qualification inputs, not measured supported bounds ([design § DEC-116](design.md#dec-116--single-rolling-playback-contract-design-target-no-firmware-checkoff)).

### Migration, alternatives and verification boundary

| Path | Ruling |
|---|---|
| Prioritize every Record/overdub turn over all others | Rejected: perpetual capture can starve source publication, another sounding role, stop/finalization or persistence; mode is not the affected-window deadline. |
| Wait for the whole `LoopSnapshot` before playback | Rejected: whole-root checkpoint publication is atomic, length-dependent and not required when a qualified frozen/retained source serves. |
| Keep first-Record, pair, bridge and merged readers permanently | Rejected by DEC-116: duplicates handoff/adoption rules and makes a gap's serving owner ambiguous. Keep only as temporary migration fallbacks until each route's replacement is verified. |
| Rebuild/order inline at the clock or stop edge | Rejected: violates the existing execution-bound and no-edge-build contract. |
| Reuse existing Track/Loop/TrackManager owners and one bounded reader (**preferred**) | Requires a fixture-first request → actual source grants → window grants → complete ready → edge adoption → MIDI/ledger oracle, plus RAM1/PSRAM and pinned hardware qualification before enablement. |

**Affected paths:** `src/main.cpp` / `MidiServiceDrain` owner selection; Track Record and overdub source preparation, `Track::stepPlaybackViewMaintenance`, `Track::playCommittedLoopMidi`, TrackManager's tick/slot entry, `Loop::stepPendingSnapshotPublish` as checkpoint owner, `EditManager` preview lifecycle and StorageManager's independent persistence opportunity. No SD schema or new content authority. Existing three-view roles and generation-qualified source holds stay window-sized per sounding role; any new state or scheduling policy must be re-budgeted and reviewed against `ExecutionBounds.md` before firmware edits. Rollback while migrating retains today's emitters and their registered tests, never a silent music/content exception.

**Review verdict:** G0 owner/call-path evidence is sufficient; G1 preferred architecture is the existing-owner extension with bounded non-starvation. G2 production oracle, the exact owner-kind selection contract, worst permitted opportunity gap and required RAM1/PSRAM/device evidence are **NOT ESTABLISHED**. The owner approved the one-reader outcome, not a particular unmeasured arbitration constant or device PASS. The trace contract is [end-state timing trace](evidence/2026-10-08-s315-end-state-timing/EXECUTION.md); a current default-image capture is not end-state evidence.

## 2026-10-08 addendum — C3 layered switch output boundary (DEC-116 existing-owner extension)

**Formal trigger — timing model / `updateAllTracks` contract.** In the two-enabled-slot C3 production fixture, a LoopEnd switch while the outgoing slot remains enabled emits 12 channel-wide CC123 rows (`TrackManager::updateAllTracks` → `Track::sendAllNotesOff`) and clears every slot's ledger. The outgoing slot is still independently sounding, so this is not a window-selector-only problem; it violates DEC-116 — one rolling path per sounding slot and the active change's additional-sounding-slot scenario. A separate gated fixture is RED on exactly those CC123 rows, while the two-window fixture is independently RED before playback on incomplete per-slot readiness.

**Current owners and extension:** `TrackManager` owns the pending switch, `activeLoopIndex`, enabled-set replacement and clock-edge commit; `Track`/`LoopPlaybackRuntime` own slot MIDI and ledgers. No ownership move, new queue, SD change or new note-closure emitter is needed for the first narrow correction. The existing `pendingEnabledSetReplacement` and `slotEnabled[i][previousPlaying]` are the production switch's own facts; they distinguish a still-enabled outgoing sounding from a single-slot replacement that actually retires it.

**Preferred bounded correction (existing-owner path; no hard approval gate):** At the switch edge, suppress the existing global CC123/clear-all-ledgers call **only** when the outgoing slot remains enabled *and* the switch is not replacing the enabled set. This removes twelve bounded sends from that one route rather than introducing content-sized closure work; no per-note traversal, new scheduling class, or `ExecutionBounds.md` class claim follows. When enabled-set replacement is pending or the outgoing slot was disabled, retain today's global stop behavior until a separate targeted-closure route is admitted. Verify both polarities through the production switch and ledger, not only a direct boolean. Preserve the old long-loop stop/slot-exit fixture and timing barrier elsewhere. Rollback is the old unconditional switch CC123; the qualification gate/fallback remain unchanged.

**Not qualified by this correction:** Other enabled slots can still be affected when outgoing is disabled; same-pitch overlapping voices, different loop lengths/projection origins, a switch mid-builder, jam/overdub/preview, and the composed source/window deadline need their own owner/reader/closure proof. The bounded correction does not establish C3 all-route cutover or device timing; do not remove live emitters. User already approved the one-reader per sounding-slot target and C3 is in `CURRENT_WORK`; no new owner/architecture decision or `DEC-###` is introduced.

### C3 correction — conditional CC123 suppression withdrawn (2026-10-08)

The narrow correction above was attempted and **reverted** after a focused read-only review identified two falsifiable gaps: both current button actions arm `pendingEnabledSetReplacement`, so `requestSlotSwitch` without replacement is an internal API shape rather than a reachable user gesture; and the first fixture's outgoing NoteOff@48 preceded the switch, so it did not exercise a held note. A valid two-bar outgoing committed source with On@700/Off@800, switching at the next grid tick 768, proved the second gap: suppressing CC123 kept the outgoing ledger open at the switch but its Off@800 never reached MIDI after `commitQueuedPlaybackStart` re-anchored the Track-level projection. This is a **stuck-note defect**, not a safe bounded optimization. The unconditional CC123/ledger clear remains production behavior; the partial C3 owner/reader edits were likewise reverted. Two gated fixtures remain RED: independent two-slot window readiness, and a held outgoing note's continuity at the real switch.

**Revised recommendation:** Before any switch-output change, pin a slot-qualified projection origin/cursor and closure-owner handoff for every still-enabled sounding slot, with exact post-switch Off and ledger discharge through the production action/TrackManager path. Decide the reachable gesture that retains the outgoing enabled set separately from today's replace-only short/long presses (or name an existing non-button caller); do not call an API-only native pass product qualification. Bound any proposed per-note closure work at the clock edge or defer it through the existing owner—no speculative replacement of fixed CC123 with a content-sized scan. The current one-reader per-sounding-slot direction remains DEC-116; this is a correction to the C3 implementation contract, not a new owner, schema or DEC. No additional formal reassessment is started in this session.

## 2026-10-22 addendum — the live overdub overlay moves onto the single window reader (3.15e, DEC-116 extension)

**Status: direction reviewed and mechanism chosen; production code NOT IMPLEMENTED, no DEC yet, device NOT RUN.** This is
the formal reassessment 3.15e's remaining clause requires before its firmware half starts.

### Reason triggered

Two triggers fire together. **Timing model:** the live overdub's captured rows are served by a second cursor walk at the
end of `Track::playCommittedLoopMidi`, and that same turn runs `loop.ensureCaptureEventsSorted()` — a full sort of the live
store — **on the MIDI service turn** (`src/Track/TrackPlaybackHotPath.cpp:580-590`). Removing that walk and that sort
changes which owner does the work and moves content-sized work off the clock edge, so `ExecutionBounds.md` row 2 must be
reclassified in the same commit that enforces the new class. **No reusable extension point at the emission boundary:** the
existing window machinery can build an append-stable window over the live store (`RuntimePlaybackViewSource::fromLiveCapture`,
`RuntimePlaybackViewBuild::extendLiveCapture`), but it cannot yet build one over **committed base + live overlay**, which is
the shape an overdub needs.

Not triggers here: no new owner (`Track` keeps the request/build/publication ownership), no ownership transfer from `Loop`
or `EditManager`, no persistent-state or SD schema change, and no undo change. The recorded ledger invariant
(`221334`/`224719`: the live echo must not write the occupy ledger) is **preserved, not repealed** — see *Ledger* below.

### Current architecture (G0, verified in code)

| Fact | Where |
|---|---|
| The live overlay is a **second walk**: `advancePlaybackCursor(… PlaybackEmitPolicy::ActiveCaptureOverdub, makeCapturePlaybackStream(loop))` over `loop.capture.store`, after the committed walk, on the same frame | `src/Track/TrackPlaybackHotPath.cpp:580-590` |
| That turn sorts the live store when a dirty flag says append order broke: `ensureCaptureEventsSorted()` + `reanchorCaptureIndex` | `src/Track/TrackPlaybackHotPath.cpp:582-584`, `src/Track/TrackPlaybackWindowBuild.cpp:841-853` |
| The echo is emit-only: `playbackCursorAdvanceSendCapture` calls `sendMidiEvent` and never `applyPlaybackLedgerEvent` | `src/Track/TrackPlaybackHotPath.cpp:86-91` |
| Every other route is committed-only; the merged store refuses live capture by contract; the prepared pair is published from it | `src/Track/TrackPlaybackWindowBuild.cpp:507-514`, `:1882-1928` |
| A `LiveCapture` window already exists — append-stable, incrementally extended, ordered off-clock, priced in grains — but is gated to **first-take Record** (`phase == Record && !hasCommittedPasses()`) | `src/Track/TrackPlaybackWindowBuild.cpp:159-163`, `:201-259`, `:6238-6295` |
| That build already fills from `RuntimeCompanionPasses` (committed pass inputs **with per-row provenance**) and from the live store, and has an explicit Fill → Order grain sequence with an incremental order bound | `include/RuntimePlaybackViewFill.h`, `src/Track/RuntimePlaybackViewBuild.cpp:176-321` |
| The view carries a per-row `passOrigins` table (`PlaybackPassEventOrigin{pass, event}`) with `passOriginsReady`, and a canonical `(phase, NoteOff < NoteOn < other)` order over retained rows | `include/RuntimePlaybackView.h:468-482`, `src/Track/RuntimePlaybackView.cpp:331-359` |
| `Loop::commitOverdubWrap` seals the pass, re-bases the live store and carries held NoteOns into the new pass; the frame's tail interval is drained by `advancePlaybackCursorAcrossOverdubWrap` | `src/Track/TrackOverdubWrap.cpp:16-177`, `src/Track/TrackPlaybackHotPath.cpp:148-176` |
| Three view roles only (`current`, `ready`, `build`); the committed lane and the NOTE_EDIT preview lane already compete for `ready` | `include/RuntimePlaybackView.h:28` |

### Alternatives and rulings

| Path | Benefit | Cost / ruling |
|---|---|---|
| Keep the echo walk and only move its sort off the edge | Smallest change; removes the content-sized sort from the MIDI turn | **Insufficient:** the clause is a *reader* clause, not only a bound clause; two walks keep two ordering/search rules and leave "one ledger owner" unanswerable. May still be taken as a **safety step** inside slice 1. |
| Make the overlay true input passthrough (send at the input edge, delete the walk) | Removes the replay outright and the sort with it | **Rejected:** it changes *when* a played overdub note sounds (input arrival instead of the clock grid) — a tick-quantization/timing-model change with musical consequences, and it cannot preserve the ledger split for a note whose Off lands after the wrap. |
| Fold live capture into `mergedMidiEvents` | One stream immediately | **Rejected, and already forbidden by contract:** that representation is committed-only precisely to keep occupy correct (`221334`); re-folding it recreates the defect that fixture pinned. |
| One window per overdub turn — committed rows from the lane's own committed source + appended live overlay rows, built by the existing owner, adopted at the edge (**preferred**) | Same request → bounded owner turn → complete immutable ready window → clock-edge adoption → ordered emission contract as every other route; ordering happens off-clock; the edge keeps one walk and no sort; overlay rows are provably exactly-once because they exist only in that window | Requires the build to continue from the committed source into the live store, a per-row origin marker for the ledger split, and role/capacity arbitration with the committed and preview lanes; needs RAM1 and device measurement before cutover |

### Preferred evolution (existing-owner extension)

1. **One builder, two source phases.** `RuntimePlaybackViewBuild` fills the committed base for the window's coverage from
   the lane's own committed source (the `RuntimeCompanionPasses` form, which already produces per-row provenance), then
   continues the *same* build from `fromLiveCapture(loop, /*appendStable=*/true)` and re-runs its incremental order as the
   overlay appends. Both phases write the same destination storage; nothing is re-sorted on the clock edge.
2. **Per-row origin, not a second store.** Overlay rows carry the overlay sentinel in `passOrigins`; committed rows keep
   their pass provenance. This is how the emission callback can keep `221334`: the ledger is applied only to
   committed-origin rows, so occupy still reads committed Entries only, while the overlay sounds exactly once.
3. **The edge keeps one walk.** `resolveQualificationPlaybackSourceForTurn` gains the overdub branch that binds the overlay
   window for the turn; when such a window is serving, the `ActiveCaptureOverdub` walk and its
   `ensureCaptureEventsSorted()` do not run. The echo stays as the **named compatibility fallback** whenever the window
   route is not selected (no committed base, first pass, lane refused, default image) — removed only with the emitter at
   3.7b.
4. **Hand-offs stay the owner's.** A pass commit (wrap or stop) advances the revision and invalidates the window exactly as
   it invalidates a committed one: the next build is `committed (now including the sealed pass) + the new live tail`, and
   the edge adopts it at its boundary. No new queue, no new Manager, no second content authority, no whole-loop root
   dependency.

### Ledger

The occupy ledger remains **committed-only**. The overlay's rows are emitted without ledger application (their own
`sentinel` origin), which preserves the `221334`/`224719` invariant *and* makes the clause's "exactly-once note/ledger
ownership" true in the strong sense: each committed note is ledgered once, each overlay row is sounded once, and the
committed pass that later contains it is ledgered when it is committed content — never twice. A DEC is owed when this
lands (a new per-row emission rule and the bound reclassification), not before.

### Migration impact

- **Files:** `RuntimePlaybackViewFill`/`RuntimePlaybackViewBuild` (continuation source + origin marking),
  `TrackPlaybackWindowBuild.cpp` (overdub overlay request/admission/build lane and the edge resolver branch),
  `TrackPlaybackHotPath.cpp` (skip the echo when the window serves; no sort on the edge), `LoopPlaybackRuntime` (request
  identity for the overlay lane; padding-only fields preferred), `ExecutionBounds.md` row 2, `NAMING.md` if a new stage
  name is introduced (a **source adapter**, per the existing vocabulary).
- **Tests:** new fixtures in `test_shared_capture_finalization` (the suite that links the real edge and the real stop) and
  the preview/RWO suites for role arbitration; the 3.15e oracle landed on 2026-10-22 is the parity reference and must stay
  green **on both routes** while the fallback exists.
- **RAM1/PSRAM:** no new event buffer is planned (the window's own role storage is the destination; the existing
  `mergeScratch`, `order`, `phases`, `retained` and `passOrigins` are reused). Origin marking costs no new array. Every
  slice must keep both images at the approved pins; a role-storage increase needs its own measurement.
- **Device:** the device leg is 3.15f's; slices here are native only, with the qualification image used for the eventual
  route comparison.

### Rollback

Delete the overdub branch of the resolver and the overlay request: the echo walk and its sort are still present and are
today's behavior. No data or schema is touched, so rollback is a source revert of the slice, and the native oracle keeps
guarding the fallback shape.

### Recommendation and approval

Preferred path: **one Track-owned overlay window (committed base + append-stable live overlay, provenance-marked), adopted
at the edge, echo retained as the compatibility fallback.** Confidence: high on the mechanism (every piece it needs
already exists and is cited above); medium on the role/capacity arbitration with the committed and preview lanes, which
is why it is slice 2 and not slice 1. The owner approved the one-reader direction for the overdub overlay in this session
(2026-10-22); the mechanism above is an **existing-owner extension**, not a new owner or ownership transfer, so it proceeds
under the decision ladder once slice 1's contract is written. G2 gates are NOT ESTABLISHED for any slice yet: each needs
its own packet, fixtures and RAM1 evidence, and no checkbox follows from this addendum.

### Slice queue (each its own G2 packet; no slice may weaken the DEC-136 envelope)

| # | Slice | Acceptance (native unless stated) | Fallback |
|---|---|---|---|
| 1 | **Build continuation**: the existing builder fills a committed source and continues into the live overlay, marking overlay rows in `passOrigins`, with the canonical order applied off-clock. Window built, **nothing consumes it yet** (the 3.15d slice-1 pattern). | A built overlay window contains base + overlay exactly once in canonical order; a later append extends it without re-emitting; a pass commit invalidates it; capacity refusal is named; **no** edge sort and no edge build; production image unchanged in behavior. | Echo unchanged |
| 2 | **Request/admission + role arbitration**: the overdub phase gets its own request identity and a bounded owner lane beside the committed and preview lanes (no starvation of either). | Request admitted with the declared envelope; role conflict with a committed successor and with a NOTE_EDIT preview is resolved by name, not by clobbering; RAM1 pins unchanged. | Echo unchanged |
| 3 | **Edge adoption** (`resolveQualificationPlaybackSourceForTurn` branch) + **echo skipped** when the window serves, with the ledger applied only to committed-origin rows. | The 2026-10-22 oracle's exact streams pass **through the window**; echo-only fixtures keep passing on the fallback path; a mutation forcing the window off restores the echo (named fallback), a mutation re-enabling the echo under the window **duplicates** and fails. | Echo |
| 4 | **Hand-offs**: wrap pass commit mid-overdub and the deferred stop handoff rebuild/adopt the overlay window; held NoteOn carried across the wrap is served once. | Exactly-once across the wrap and the stop, with the wrap-held continuation case (today UNKNOWN) covered; ledger closes; no duplicate at the seam. | Echo |
| 5 | **Bound reclassification + parity**: `ExecutionBounds.md` row 2 updated with the measured edge cost (no sort, one walk), and the device/route comparison handed to 3.15f. | Edge cost measured and reclassified; both images at pins; 3.15f consumes the evidence. | Echo until 3.7b |
