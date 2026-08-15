# Loop content resolution — event-sourced prototype

**Status:** Active — native Stages 0–8 PASS; Stage 9 complete (**5.18 FROZEN**, **5.1 PASS** [`173842`](../captures/session_20260815_173842.log), **5.2 PASS** [`180624`](../captures/session_20260815_180624.log) `begin_capture` 10050 µs); **6A PASS** [`185931`](../captures/session_20260815_185931.log) `match=1`; **6B PASS** [`192334`](../captures/session_20260815_192334.log); 6C consume-when-ready native landed; **6D.2 native PASS** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md) (split history+delta; 6D.1 one-vector FAIL; A/B rejected)  
**Date:** 2026-08-14  
**Decision:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Parent:** [DEC-036](../DECISION_LOG.md#dec-036-runtime-effective-event-source-for-overdub) Layer D 3b (overdub entry PASS); [DEC-035](../DECISION_LOG.md#dec-035-loop-persists-content-only) Layers C–D  
**OpenSpec:** `openspec/changes/loop-content-resolution/`  
**Next-chat handoff:** [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)  
**6D investigation:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**Does not authorize:** deleting `materializeToEventVector`; wiring resolution onto `handleMidiInput`; persisted checkpoint (D3) until Stage 7 shape is proven in RAM; overlay picker; Stage 3b GUS; interval reservation; RC-J

---

## Experimental boundary

Do not optimize `LoopPasses::materializeToEventVector` again. Prove whether an event-sourced, indexed, checkpointed `LoopContentResolution` can make materialization unnecessary on the **normal** path.

Keep the existing recording/materialization system alive until the prototype proves **correctness + sub-history scaling + device-level realtime latency**.

---

## Contrast

```text
CURRENT                              TARGET

Passes                               Passes
  ↓                                    ↓
materialize ALL                    indexed history
  ↓                                    ↓
gather ALL / window                resolve ONLY requested
  ↓                                state / window
reconstruct                            ↓
  ↓                                range cache if useful
invalidate ALL
```

```text
                         LoopPasses
                    authoritative history
                             │
              ┌──────────────┴──────────────┐
              │                             │
         RawMidiEvent                  EditAction
              │                             │
              └──────────────┬──────────────┘
                             │
                    indexes + checkpoints
                             │
                             ▼
                 ┌─────────────────────┐
                 │ LoopContentResolution│
                 │                     │
                 │ resolveState()      │  primary
                 │ resolveWindow()     │  primary
                 │ resolveNotes()      │  derived consumer
                 └──────────┬──────────┘
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
          Playback       Display        Editor
          window         window         window
```

**Physical PSRAM chunks stay a storage detail (`LoopEventStore`). They are not resolution boundaries.** Resolution operates on ticks, identities, and events. Chunks are how data happens to be packed. Do not design `chunk = resolution unit` — notes, edits, checkpoints, and display bars will not share those cuts.

---

## Core decision: G is the union, not different physics

A, B, C, and D are not competing architectures. They are capabilities one owner must eventually provide:

| Capability | Meaning |
|------------|---------|
| **A** | Do not invalidate unaffected display ranges |
| **B** | Incrementally maintain effective content |
| **C** | Find relevant events without walking every pass |
| **D** | Reach a useful state without replaying history from zero |
| **G** | One owner (`LoopContentResolution`) that can provide all four |

Stage **6D** is capability **B** for the **overdub query** after commit. **6D.1** one-vector mutation **FAIL**. **6D.2** split history+delta **PASS** (native; not live incremental LCR). Not capability letter C. Investigation: [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md). Do not implement as a second cache.

Shipping A then B then C as separate caches is how DEC-036 D1 died:

```text
materialize everything → invalidate everything → call it derived state
```

The D1 failure was not “incremental materialization does not work.” Do **not** repeat that with a new owner.

---

## What is already true

DEC-016 already states the same layering: Capture Storage → Derived Representations → Interval × Representation → Consumer.

| Claim | Current owner | Proof |
|-------|---------------|-------|
| Raw MIDI is authoritative | `Capture` append; `RecordPass` / `OverdubPass` chunk refs | [`LoopPasses.h`](../../include/LoopPasses.h), `Loop::appendCaptureEventWithResult` |
| Notes are derived | `NoteUtils::reconstructDisplayNotes` | `Loop::rebuildVisualCacheIdleSlice` |
| Committed pass **content** is immutable; Active/Disabled is mutable history state | undo toggles state, does not rewrite chunks | [`TrackUndo.cpp`](../../src/TrackUndo.cpp); DEC-035 |
| Edits are operations | `EditPass` Create / Update / Delete + property | [`EditPass.h`](../../include/EditPass.h); `applyNoteEditPassSequence` |
| Long-loop playback is already a window query | 2-bar `gatherCommittedEventsInWindow` | `ensurePlaybackMergedMidiEventsBuilt` |
| Display idle is already bar-sliced | `visualCache.dirtyBars` | `rebuildVisualCacheIdleSlice` |
| Overdub **entry** is cheap when the visual cache is clean | copy `visualCache.notes` | `establishOverdubSourceView`; [`045556`](../../captures/session_20260814_045556.log) `begin_capture` **2214 µs**; undo [`112909`](../../captures/session_20260814_112909.log) **3 ms** |

Resolution consumes the **active pass set + edit history**, not “every stored pass is active.”

---

## Remaining rebuild

Overdub open is no longer the 6.8 s path. Post-commit still dirties and rematerializes:

```text
overdub stop
    → commitCapturePass → notifyCommittedContentChanged → markPassDerivedStale
    → finalizeCommitSideEffects → markDisplayCachesStale   // ALL bars dirty
    → idle slices: collectActiveCommittedChunkLists (every active pass)
                   CommittedEventRange::inWindow (still O(pass lists))
                   applyNoteEditPassSequence (all active edit rows)
                   reconstructDisplayNotes (that window)
```

`EffectiveEventStore` is **not** a class — it is `passesMaterializedStore_` plus `ensureEffectiveEventStoreCurrent`. Full flatten still happens on first `gatherCommittedEvents` after stale.

| Path | Long loop (>16 bars) | Short loop (≤16 bars) |
|------|----------------------|------------------------|
| Overdub entry | Copy clean `visualCache.notes`, else 16-bar window | Same |
| Playback merge | 2-bar window | Full `gatherCommittedEventsWithCapture` |
| Overdub stop display | `adoptComposedDisplayNotesFromViewport` | `rebuildVisualCacheFromPasses` → `VCACHE,full` |
| Idle while OVERDUB | 4-bar slices | Same slices; stopped idle then full `ensureVisualCacheBuilt` |

Other O(all) sites: `MidiLedManager::prepareLedNoteLookup` when `visualCacheDirty`; `shouldRestoreCommittedOverlapOnOverdubStop` if source view missing; NOTE_EDIT `rematerializeEditView`; `Loop::midiEvents()` / `legacyMidiEventsFromCommitted`.

Windowed gather is **not** O(window events). `CommittedEventRange::inWindow` iterates every active pass’s chunk list. A loop `for (auto& pass : passes) if (pass.intersects(window))` can claim a small window while still walking 80 pass lists.

[`DerivedViews.md`](../Authority/Architecture/DerivedViews.md) invalidation rule 1 is the opposite of the cache invariant: any storage mutation discards the whole derived event representation. Stop-path `markDisplayCachesStale` is that rule in code.

---

## Event vocabulary

Three different things. Do not collapse them into `Event`.

```text
RawMidiEvent     NOTE_ON / NOTE_OFF / CC / …
EditAction       Create / Delete / ChangeLength / ChangeTick / ChangePitch / …
ResolvedEvent    effective NOTE_ON / NOTE_OFF / … after layer semantics
```

Ownership:

```text
LoopPasses              owns RawMidiEvent + EditAction (EditPass rows)
LoopContentResolution   consumes them; produces ResolvedEvent and state
Playback                consumes ResolvedEvent
Display / Editor        may consume resolveNotes() as a projection of that result
```

Today’s `MidiEvent` is the raw/resolved MIDI **shape**. `EditPass` is the edit-action **row**. Do not introduce a fourth synonym. Type aliases or comments may pin the three roles until a rename is justified.

---

## Primary queries

`CREATE_NOTE` / `resolveNotes` is a **derived consumer**, not the architecture center. Playback wants MIDI state/events, not `DisplayNote`.

```text
raw events + edit actions
        ↓
LoopContentResolution
        ├── resolveState()     primary  — sounding MIDI at a tick
        ├── resolveWindow()    primary  — ResolvedEvent in a range
        └── resolveNotes()     derived  — Note projection for display/edit
```

Do not recreate: raw events → build all Notes → cache Notes → play Notes.

---

## Hard invariants

1. **Committed pass content is immutable. Active/Disabled is mutable history state.** Resolution uses the active pass set plus edit history.
2. **After indexing/checkpointing, resolution cost is proportional to the relevant candidate events and affected state, not the number of historical passes.** Two costs, both bounded:
   - **Find:** history → index → candidate events (must not walk every pass list)
   - **Resolve:** candidate events → layer semantics → ResolvedEvent / state
3. **`resolveState(tick)` must have a bounded historical replay distance through checkpoints. It may not require replaying the loop from tick 0.** Checkpoint spacing is a measured **performance** parameter (`checkpointIntervalTicks`), not a semantic property of the loop. A checkpoint is a jump point: it MUST reduce replay work without becoming a proportional copy of the resolved loop. Per-bar full `soundingAt` fails this ([`225351`](../captures/session_20260814_225351.log)). Fast live loop switching is a fundamental query, not an optional later optimization.
4. **For a fixed active pass set and fixed edit history, resolution is deterministic and independent of cache state, chunk boundaries, or previous resolution order.** `resolveWindow(A)` and `resolveWindow(B)` cannot disagree because A populated a cache first.
5. **Valid derived state is never discarded merely because unrelated content changed.**
6. **No realtime MIDI or display-critical path may perform work proportional to total loop history.**
7. **Physical PSRAM chunks are not resolution boundaries.**
8. **Derived-index storage.** Derived indexes on the target device use contiguous/bulk PSRAM storage. Per-entry dynamic allocation into PSRAM associative containers (`std::map`, `std::multimap`, `std::unordered_map`) is prohibited on realtime-adjacent index construction paths. Where the query contract permits, indexes are flat PSRAM arrays built by append/bulk construction and ordered or uniqued in a bounded operation. Flat storage is not an automatic replacement for every associative structure. Representation B is not justified unless a measured flat query is too expensive. Measured: `spanBoundaries` (5.15), `tickEvents` (5.17; `byTick` removed), `channelByNoteId` (5.7c **FROZEN**), `byNoteId` (5.18 **FROZEN** last-wins flat). `openOnByPitch` stays a LIFO stack. Flatten from the query, not the container type.

### Derived-structure contracts

Do not accumulate unnamed caches. Every derived structure has a query contract before a representation.

| Index | Query | Multiplicity | Ordering | Mutation | Candidate |
|-------|-------|--------------|----------|----------|-----------|
| `spanBoundaries` | tick range → start/end apply | many | tick + C-order at equal tick | build, then read | flat A **frozen** |
| `tickEvents` | tick window → Active `(passId, eventIndex)` | many | tick + C-order at equal tick | build, then read | flat A **frozen** |
| channel lookup | stored-byte copy onto `SoundingNote.channel`; not loop identity (DEC-033; output is `Track::midiChannel`) | unique, first-wins | `noteId` after sort | build, then read | flat A **frozen**; not a pairing key |
| `byNoteId` | `NoteId` → `{passId, on, off}` for `appendNoteEvents` | unique key, last assignment wins | none | pair walk, then read | last-wins flat A **frozen** (5.18) |
| `openOnByPitch` | pairing walk only: pitch → open ON indexes | many per pitch | LIFO | every on/off in the pass | retain stack (5.18 `op=2.3 ms`, `pk=1`) |

Detail: [`loop_content_resolution_pair_index_refinement.md`](loop_content_resolution_pair_index_refinement.md).

---

## Naming

**Accepted:** `LoopContentResolution` (domain-owner noun).  
**Rejected:** `Resolver`, `LoopContentResolver` — [`NoteGeometryResolver`](../../include/NoteGeometryResolver.h) already owns edit-overlap Resolution.

API: `resolveState()`, `resolveWindow()`, `resolveNotes()`.

Not owner of: raw passes (`LoopPasses`), persistence (`StorageManager`), live NOTE_EDIT geometry (`NoteGeometryResolver`).

---

## Parallel prototype

Production MIDI, display, overdub entry, and NOTE_EDIT stay on today’s owners until all three gates pass.

### Canonical stress fixture (before Stage 1 gets far)

Do not prove the architecture on `P0 + P1`. Create one fixture early and grow it:

```text
64 or 128 bars
45+ passes
multiple channels
same-pitch overlaps
shorten / extend / delete / move
wrap-around
```

Every stage runs against that fixture. Counters on every run:

```text
events in history
passes in history
events in query window
candidate events
resolution operations
elapsed µs
```

### Stages

Native-only first. Replay overdub overlap from archived `openspec/specs/overdub-pass-overlap-resolution/` — do not invent new layer rules. Stages 1–5 prove **resolveWindow / resolveState**; `resolveNotes` is checked as a projection of the same result, not as the primary artifact.

| Stage | Prototype does | Reuse |
|-------|----------------|-------|
| 0 | Canonical fixture + counters | — |
| 1 | One pass: `resolveWindow` / `resolveState`; `resolveNotes` as projection | `MidiEvent`; pairing already in `reconstructNotes` |
| 2 | Two overlapping passes, same pitch → boundary resolution | DEC-031/032 overlap |
| 3 | DELETE via `EditAction` | `EditActionType::Delete` + `targetNoteId` |
| 4 | SHORTEN / EXTEND | `EditPropertyType::Length` |
| 5 | MOVE | `EditPropertyType::Tick` / `Pitch` |
| 6 | Window query on the full fixture; cost vs `materializeToEventVector` + reconstruct | tick index; `CommittedEventRange` is not sufficient if it still walks pass lists |
| 7 | **PASS** In-RAM checkpoints at `checkpointIntervalTicks`; `resolveState` from checkpoint + tail | DEC-035 D3 *shape*; not persisted yet |
| 8 | **PASS** Loop switch at a high tick — warm destination `resolveState`; bounded replay, never from 0, no checkpoint rebuild | `resolveState` is required here |
| 9 | Device three-part gate — **5.18 FROZEN**; **5.1 PASS** [`173842`](../captures/session_20260815_173842.log); **5.2 PASS** [`180624`](../captures/session_20260815_180624.log); **6A PASS** [`185931`](../captures/session_20260815_185931.log) `match=1`; **6B PASS** [`192334`](../captures/session_20260815_192334.log). 6C not started | keep 3b copy path |

**Layer semantics:** the cut-at-boundary example is existing overdub overlap. The prototype consumes that spec. It does not replace `NoteGeometryResolver` for live NOTE_EDIT.

---

## Three gates (all required)

“Beats the current owners” is not the bar. Record **worst-case** measured latency, not average.

### Correctness

Identical fixtures: old `materialize` + `reconstruct` == new resolve (byte-for-byte, or documented semantic equivalence). Determinism invariant holds with cache cold vs warm.

### Complexity

Prove `commit P(N)` does **not** traverse `P0…P(N-1)` except through **indexed affected regions**. Finding candidates must not be `for each pass if intersects(window)`.

### Device latency (`035414` class)

- no multi-second MIDI stall — **5.1 PASS** [`173842`](../captures/session_20260815_173842.log) idle complete path: `midi_gap` 39.1 ms
- no multi-second OLED stall — **5.1 PASS** same capture: consecutive `DFRAME` 1.034 s vs healthy 0.968 s
- overdub entry remains cheap (`begin_capture` stays under the existing < 50 ms bar) — **5.2 PASS** [`180624`](../captures/session_20260815_180624.log) **10050 µs**
- no `VCACHE,full` on the normal path — **5.2 PASS** (none in [`180624`](../captures/session_20260815_180624.log))
- no full materialization after commit — **5.2 PASS** (3b visual-cache copy)
- bounded resolution slices — PASS (8-event slices; `nsort` 10.0 ms / `isort` 28.1 ms under 50 ms)
- record worst-case µs, not only totals — [`173842`](../captures/session_20260815_173842.log) complete line

The LCR device gate runs only while transport is idle. It does not run during PLAYING. 5.1 is that idle complete path. 5.2 is overdub entry on the production 3b path.

### Failure meaning

If the prototype cannot demonstrate a **materially better scaling model** without introducing another O(history) derived owner, **stop**. Then implement A+C on existing owners.

A poor first tick-index implementation does **not** by itself disprove the architecture. Another O(history) derived store that `invalidateCaches` will discard **does** disprove this attempt.

---

## Production migration (only after all three gates)

**Stage 6 question:** Can prepared LCR state replace existing materialized derived views **without introducing synchronous work at the transport transition?**

**Overdub invariant (DEC-037 amendment 2026-08-15, strengthened):** Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state. They may only consume already-prepared derived state. `ensureLcrIndexCurrent()` (or any ensure/rebuild helper) on that path is a violation.

`LoopContentResolution` is the **producer of prepared derived state**, not a replacement for `overdubSourceView`:

```text
LoopPasses
    │
    ▼
LoopContentResolution     ← idle / background
    ├── resolveWindow()   → display-range derived data
    ├── resolveState()    → playback / loop-switch state
    └── resolveNotes()    → display/editor projection
              │
              ▼  already prepared
OVERDUB START/STOP consume only
```

Wrong: `OVERDUB → LCR construction → consume`. Right: `IDLE → LCR construction → READY`; overdub only consumes.

**Entry vs stop:**

- Entry is largely explained. 3b [`045556`](../../captures/session_20260814_045556.log) **2214 µs** shows **< 3 ms is achievable when the committed visual/source representation is already prepared**. [`180624`](../../captures/session_20260815_180624.log) **10050 µs** still passes the 50 ms gate. LCR has not shown it can beat that copy. Do not claim switching everything to LCR makes start < 3 ms.
- Stop is the remaining expensive path: commit → `notifyCommittedContentChanged` → `markDisplayCachesStale` → idle rebuild. LCR helps **indirectly**: commit raw content, dirty affected ranges, return immediately; idle prepares derived state.

Keep 3b `visualCache.notes` copy as fallback. Do not remove it in 6A–6C. LCR can fail to be ready without making overdub entry expensive.

**Experiment order:**

1. **6A — prepared display range (idle).** **PASS** [`185931`](../../captures/session_20260815_185931.log) `DIAG,lcr,6a,win=784,proj=5539,oracle=9192,tot=6323,ev=78,notes=40,match=1`. `midi_gap` 43.6 ms during LCR / 14.3 ms after. DFRAME 0.977 s at the sample (idx peak 1.027 s). No `VCACHE,full`. Production idle consume waits for 6B dirty ranges (stamp still matches only until the next commit).
2. **6B — commit invalidation.** **PASS** [`192334`](../../captures/session_20260815_192334.log). Three 139-bar overdub stops: `stale_range` notes 2375/2437/2450 `dcnt` 15/5/5; no `adopt_partial`; no `VCACHE,full`; `ODUB,stop,display` 35/30/26 ms; idle `slice_clean` notes 2437/2450/2464. Contrast [`185931`](../../captures/session_20260815_185931.log) `adopt_partial` 2403→496. Score which work stays synchronous with the transport transition:

| Phase | Desired property |
|-------|------------------|
| `stopOverdubbing()` entry | bounded |
| `commitCapturePass` | bounded |
| commit bookkeeping | bounded |
| return to MIDI loop | bounded |
| first idle derived-state preparation | sliced |
| display repaint | sliced |
| eventual full consistency | asynchronous |

3. **6C — overdub source.** Native landed. [`194643`](../../captures/session_20260815_194643.log): `mat=` complete, `6a` `match=1`; `6c`/`begin_capture` lost to `RING,overflow`. INFO open 42 ms then 9 ms. Recapture: overdub+stop within ~1 s. Score against 3b **2214 µs**.
4. **After 6C — MIDI Input Gap > 50 ms.** [`192334`](../../captures/session_20260815_192334.log) `DIAG,midi_gap` **135 / 119 / 138 ms** at 54.7 / 64.7 / 69.8 s; `clockrate` stayed **47**. Not in 6C scope. Not RC-J (post-STOPPED clock drop).
5. **Later:** long-loop playback gather; short-loop / NOTE_EDIT hydrate.
6. **D3 persist checkpoint** — same checkpoint type as stage 7; `StorageManager` remains persist owner (DEC-008).

`< 3 ms` is a **regression target**, not an architectural promise. `< 50 ms` stays the hard gate. LCR’s job is to eliminate post-commit / full-rebuild machinery.

**Forbidden until gate:** delete `materializeToEventVector`; put resolution on `handleMidiInput`; cascade `invalidateCaches` onto the prototype store; call `resolveWindow` / construct / sort / checkpoint LCR from `startOverdubbing` or `stopOverdubbing`.

---

## Architecture gate

| Question | Answer |
|----------|--------|
| Owner | New: `LoopContentResolution`. Content stays `LoopPasses`. Persist stays `StorageManager`. |
| Primary invariant | Active pass set + edit history → deterministic resolve of requested state/window; cost after index/checkpoint tracks candidates, not pass count; `resolveState` has bounded replay distance. |
| Ownership change | **YES** — new derivation owner (DEC-037). |
| Transition change | **NO** for record/overdub FSM. |
| Reuse | YES for data (`LoopPasses`, `EditPass`, overlap spec). NO for “keep materialize as the resolver.” |
| Out of scope | Overlay picker; Stage 3b GUS; interval reservation; RC-J; deleting 3b visual-cache overdub copy. |

---

## Decision review (full preflight — new owner)

**Searched:** `DECISION_LOG` DEC-016 / DEC-035 / DEC-036; OpenSpec `loop-effective-event-source`, `overdub-pass-overlap-resolution`, `loop-content-history`; plans Layer D / Layer history persistence / DerivedViews.

**Findings:** DEC-016 already requires representation × interval. DEC-035 records D3 checkpoint + D4 range load as later layers. DEC-036 D1 eager flatten was withdrawn; 3b visual-cache overdub source is device PASS. `NoteGeometryResolver` owns live NOTE_EDIT overlap, not loop history resolution. `CommittedEventRange::inWindow` still walks all pass lists.

**Existing reusable pattern:** `LoopPasses` content, `EditPass` rows, overlap spec, `visualCache.dirtyBars`, long-loop playback window. **No** reusable “keep entire loop flattened” extension point survives `invalidateCaches`.

**Reuse decision:** **NO** for using `materializeToEventVector` as the resolution owner. **YES** for data and layer semantics.

**New constraints:** `LoopContentResolution` is the sole owner of effective musical state queries. Caches are range-based and optional. Chunks are not semantic boundaries. Prototype stays off the MIDI hot path until all three gates pass.

**Migration required:** NO for SD. Production consumer swap is gated; materialize remains until then.
