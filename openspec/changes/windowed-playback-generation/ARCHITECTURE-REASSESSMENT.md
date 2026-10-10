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
