# Undo-depth sizing (E9.8b-3b)

Status: investigation (2026-11-11), part measured. Not authority; a change of the depth or of the pool rule needs a
`DEC-###` (DEC-254 owns the history). Owner: "99 is just arbitrarily chosen, not data analysed" (groove review,
2026-11-10). Tools: `!ENGINE history-cost` (`HistoryCost.h`, birth counters in `EngineMain.cpp`), `!ENGINE history`.

## 1. What the 99 is

- **One pool for all 64 slots** (`Config::kHistoryDepth`, `History.h`), not 99 a slot. A state is one manifest
  reference. Every slot's current state is in the pool too.
- **Full pool:** a commit evicts the oldest state in the set that is not a slot's current (its runs then reach the
  reclaim queue); a redo branch hit by eviction is dropped whole. Recording is never refused for history.
- **Sessions share it:** every non-empty overdub wrap and every NOTE_EDIT action is a commit while its session is open;
  Stop / leaving folds them into one step (`collapsedWraps`). A GROOVE visit replaces in place (one state).
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

Above ~200 the run table (512 content runs) would need raising: 80 B a descriptor is cheap, but each run also holds at
least one 1 KiB page, so ~300 more runs means ~300 KB of pages from the content quota. The manifest table must grow by
the same number of entries (the static_assert); the card record by 135 B a state a slot.

## 4. How fast states are born — to be measured

The question that decides the depth: how many states a real session makes, and in what bursts. Expected from the rules
(§ 1): an overdub on a 1-bar loop at 120 BPM commits a state every 2 s while it runs (30 a minute), so a two-minute
overdub fills the whole pool with its own wraps and evicts every other slot's oldest step before Stop folds them into
one. NOTE_EDIT does the same per action.

**Protocol** (owner): play a normal session for 15–30 minutes after a reboot — record, overdub, edit, groove, change
lengths — then `!ENGINE history-cost`. `history_births` gives the births since boot, the busiest minute, what made them
(`record`, `overdub` wraps, `edit`, `length`, `groove`, `fewer` = a clear), and `evicted_undo` / `undo_past_retained`
(steps lost to the pool, and undo presses that hit a forgotten floor).

## 5. Options (decided after § 4)

1. **Keep 99**, if the session shows few births and no `undo_past_retained`.
2. **Raise the depth** to ~150–200 (within the run table; ~15 KB of manifests) if steps are lost in normal play.
3. **Keep session states out of the shared pool** — an overdub / NOTE_EDIT session keeps at most its last K states in a
   small session reserve, so a long overdub cannot evict other slots' history (the fold makes them one step anyway).
   Likely the most effective if `overdub` dominates the births; a rule change (DEC-254 amendment).
4. **A per-slot floor** — eviction never takes a slot's last undo step (its "back to empty"); uneven use (§ 2) makes
   the oldest-first rule hit the quiet slots.
5. The deferred card-held history (E4.6b–e) makes depth a card question; it stays deferred by the owner.
