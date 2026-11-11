# Undo-depth sizing (E9.8b-3b)

Status: investigation (2026-11-11), part measured. Not authority; a change of the depth or of the pool rule needs a
`DEC-###` (DEC-254 owns the history). Owner: "99 is just arbitrarily chosen, not data analysed" (groove review,
2026-11-10). Tools: `!ENGINE history-cost` (`HistoryCost.h`, birth counters in `EngineMain.cpp`), `!ENGINE history`.

## 1. What the 99 is

- **One pool for all 64 slots** (`Config::kHistoryDepth`, `History.h`), not 99 a slot. A state is one manifest
  reference. Every slot's current state is in the pool too.
- **Full pool:** a commit evicts the oldest state in the set that is not a slot's current (its runs then reach the
  reclaim queue); a redo branch hit by eviction is dropped whole. Recording is never refused for history.
- **Overdub wraps share it:** every non-empty overdub wrap commits a state; Stop folds these into one step
  (`collapsedWraps`). NOTE_EDIT already keeps its E: steps in `EditWriter`'s own log and replaces one History state
  through `History::setSessionState`; it does not spend one shared state per edit action. A GROOVE visit also replaces
  in place (one state).
- **Hard ceilings:** entry indices are `uint8_t` (`Depth < 255`); the manifest table is 256 entries with 99 set aside
  for history (`EngineConfig.h` static_assert); the card's per-slot history record is `kHistoryDepth × (9 + 2 × 63)`
  bytes (`Library.h`, 13,365 B at 99).

## 2. What a state costs (device, the owner's set, image `958a4649…`, 2026-11-11)

`!ENGINE history-cost` at boot:

| | |
|---|---|
| States held | 99 of 99 (26 slot currents, 73 undo / redo steps) |
| Runs only the history keeps | 137 (of 227 runs in use; 285 of 512 free) |
| Rows only the history keeps | 7,722 (currents: 10,846; limit 131,072) |
| Manifests free | 157 of 256 |
| Pages free | 3,272 |
| Sizes | a manifest 148 B, a run descriptor 80 B, a page 1 KiB (64 rows) |
| Worst slot | 26 (T4 S3): 2,356 pinned rows |

Per non-current state, on average: **1.9 runs and 106 rows** (a run holds at least one partial page, so ≥ 1.9 KiB of
pages). Steps are very uneven: slots 32 / 56 / 10 / 26 hold 20 / 16 / 13 / 9 steps; 19 of 27 populated slots hold 0–1.

**The binding resource is run descriptors**, not memory: the history holds 60 % of the runs in use. Pages and rows have
large headroom (history + currents ≈ 18,500 rows of 131,072; 3,272 pages free).

## 3. What a bigger depth would cost (scaled from § 2)

| Depth | Extra states | + manifests (148 B) | + runs (est.) | Runs in use (of 512) | + pinned rows (est.) |
|------:|-----:|------:|-----:|-----:|------:|
| 99 (now) | — | — | — | 227 | — |
| 149 | 50 | 7.4 KB | ~95 | ~322 | ~5,300 |
| 199 | 100 | 14.8 KB | ~190 | ~417 | ~10,600 |
| 254 (max) | 155 | 22.9 KB | ~295 | ~522 — over the limit | ~16,400 |

These are measured-set projections, not guarantees: marginal states may share existing runs or retain much larger
replaced takes. The state-entry cap is full today; descriptors are the projected next ceiling as depth grows.
Above ~200 the run table (512 content runs) may need raising: 80 B a descriptor is cheap, but each run also holds at
least one 1 KiB page, so ~300 more runs means ~300 KB of pages from the content quota. The manifest table must grow by
the same number of entries (the static_assert); the card record by 135 B a state a slot.

## 4. How fast states are born — to be measured

The question that decides the depth: how many states a real session makes, and in what bursts. Expected from the rules
(§ 1): an overdub on a 1-bar loop at 120 BPM can commit a state every 2 s (30 a minute), provided each wrap adds rows.
It can evict other slots' older states before Stop folds its steps into one. It cannot generally reach 99 nonempty
wraps: each manifest has at most 63 runs, and `SlotTransport::wrap` ends the take when its run list or run descriptors
fill. NOTE_EDIT actions use a separate log rather than creating shared History states.

**Protocol** (owner): play a normal session for 15–30 minutes after a reboot — record, overdub, edit, groove, change
lengths — then `!ENGINE history-cost`. `history_births` gives the births since boot, the busiest minute, what made them
(`record`, `overdub` wraps, `edit`, `length`, `groove`, `fewer` = a clear), and `evicted_undo` / `undo_past_retained`
(steps lost to the pool, and undo presses that hit a forgotten floor).

## 5. Options (decided after § 4)

1. **Keep 99**, if the session shows few births and no `undo_past_retained`.
2. **Raise the depth** to ~150–200 (within the run table; ~15 KB of manifests) if steps are lost in normal play.
3. **Keep session states out of the shared pool** — an overdub / NOTE_EDIT session keeps at most its last K states in a
   small session reserve, so temporary wrap states cannot evict other slots' history (the fold makes them one step
   anyway). NOTE_EDIT already has a separate action log and one History state.
   Likely the most effective if `overdub` dominates the births; a rule change (DEC-254 amendment).
4. **A per-slot floor** — prefer preserving recent previous states on quiet slots; uneven use (§ 2) makes the
   oldest-first rule hit them. This does not preserve "back to empty" after older history has been evicted. Any hard
   minimum needs a proven resource budget and a policy for pressure beyond it.
5. The deferred card-held history (E4.6b–e) makes depth a card question; it stays deferred by the owner.

## 6. Recommendation to the owner (2026-11-11; proposed, not accepted)

For deep independent undo on every slot, extend the existing `History` / `Persistence` owners along
DEC-254 — per-slot card-backed history: recent states in PSRAM, older states on SD, immutable runs written once.
Up to 99 logical undos per slot is a product target, separate from the resident cache size. Retire a resident state
without losing history only after its content and state record are durable. Fetch older targets in grains while the
current state keeps playing, using one supersedable target per slot. Card absence, failure or a sustained write
backlog must have an explicit bounded retention-loss policy: fixed memory and uninterrupted recording cannot also
guarantee retaining every state indefinitely without a working card. This remains deferred, not implementation scope.

The useful first change is overdub session isolation, preserving wrap undo/redo, the session start, abandon and
Stop's single-step fold. It saves history entries; runs still referenced by the final overdub remain live. Combine
it with fair recent retention rather than global age alone, favoring eviction from slots above a proposed recent
minimum. Any minimum is best effort until its worst-case budget is proven; recording and accepted-note closure keep
priority. Preserving an original empty/take anchor is a separate policy, not the meaning of a recent-step floor.

A pool around 200 may be an interim improvement, after capacity and persistence-layout review. With 64 nonempty
currents it holds only 136 other states (~2.1 per slot); two previous nonempty states for each already require 192
history entries before separate session and reader resources. Increasing the pool alone cannot provide deep undo
on all slots. Growth affects manifest capacity, reader pins, descriptors, pages and store/checkpoint layout; beyond
254 states byte-sized history/storage indices must change, and beyond 1,024 total run descriptors the current
10-bit playback run identity must change. The session birth-rate measurement informs cache/reserve sizing, not the
basic need to separate per-slot logical depth from shared resident capacity.

Evidence: `EditWriter::publish`, `History::setSessionState`, `History::evictOldest`, `SlotTransport::commitStep`,
`SlotTransport::wrap`, `EngineConfig.h`, `SetFile.h`, `Library.h` and `TerminalScheduler::makeRunKey`; read-only
source review plus a focused independent critique. No firmware changes or new test/device results in this review.

### Card representation recommendation (2026-11-11; owner clarification)

Owner favors option 2, the run-once store of DEC-254 — per-slot card-backed history, and explicitly clarifies that
all work is development, rebuilding is acceptable, and no migration is required. Under those constraints the
recommendation is run-once directly. The earlier preference for extending whole-slot A/B copies over-weighted
preserving the existing store/recovery path. Development compatibility/migration is out of scope; this discussion
does not start firmware implementation or alter the card.

The run-once representation follows the immutable content model: store a run when new, then let states reference
its stable durable identity. Further edits write their new content and a small slot-state list without recopying
all retained historical content. Keep crash-safe A/B publication for the small state-list/catalog records. Extend
the existing `History` / `Persistence` owners, not a parallel storage owner. Count 99 undo targets separately from
the current state (up to 100 states in a straight history).

Remaining intrinsic contracts: checked durable identities independent of reused RAM RunIds; content programmed
before publication of the state list that names it; recovery of a previous complete list after interruption; bounded
space allocation/reclamation that respects all retained undo/redo states, saved snapshots and in-flight readers;
grain-stepped write/fetch/reclaim with recording and Stop independent of card completion; and explicit capacity,
backlog and failed-card outcomes. Repeated undo/redo during a fetch supersedes its target. Saved-milestone ownership
and reclaim reachability must be resolved before a layout is finalized; snapshots must not lose their referenced
content when working history is shortened. No claim of zero reclamation writes is implied by "run-once".

Extending whole-slot A/B remains a considered alternative: it reuses the existing recovery mechanism and avoids a
new allocator, but rewrites retained content and requires independent-run checks plus a cold-history capacity bound.
It is no longer the recommended intermediate step given the owner's no-migration constraint. Recommendation
reviewed independently; no new tests, device operations or firmware changes.
