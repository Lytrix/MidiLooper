# Engine storage: fixed blocks on the card

How the fixed-memory engine keeps its data on the SD card, and why it uses preallocated fixed blocks rather than one
file per slot or per milestone. Authority: **DEC-254 — the run-once store and undo on the card** (which replaced
**DEC-253 — the A / B slot store**) and **DEC-258 — the milestone library** in [`DECISION_LOG.md`](../DECISION_LOG.md); this guide explains them and does not override them. For the legacy
runtime's storage see [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](RUNTIME_STORAGE_AND_PERSISTENCE.md); for the engine as a
whole, [`FIXED_MEMORY_ENGINE.md`](FIXED_MEMORY_ENGINE.md).

## The files, all fixed blocks

| File | Size | Holds | Source |
|---|---|---|---|
| `/MidiLooper/engine/store.bin` | about 1 GiB (2 sectors, two state records per slot, a 1 GiB run area) | the autosave, the run-once store (DEC-254): each run written once as its own record, per slot two alternating state records (its states, hot and cold), and the surface state | `Persistence.h` (with `PersistenceSaving.h`, `PersistenceLoading.h`), `RunStore.h`, `SlotFile.h` |
| `/MidiLooper/engine/store.bak` | the DEC-253 store | the store of before DEC-254 (whole slot copies), kept: read once at boot only when there is no `store.bin` (migration), never written | `Persistence.h` |
| `/MidiLooper/library/library.bin` | 1 GiB | milestones (saved versions of the whole set) and the one recovery checkpoint | `SetLibraryFormat.h`, `LibraryWriter.h`, `LibraryLoader.h` |

Each file is created once, **preallocated in one contiguous run**, and after that is only read and written by card
sector address (`SdCardIo`): no directory lookup, no allocation, no file-system flush. The file system is used only to
create and find the files (and, at boot, to rename the store files once: below).

## Why a fixed block and not separate files

1. **Speed.** Every file-system operation measured 4–8 ms on the device (E9.8g). Writing a new file means a folder
   lookup, a directory entry, cluster allocation (FAT or allocation bitmap), the data, the size update and a flush —
   several small writes scattered across the card, each waited for. In a fixed block the sector address of everything
   is known, and a milestone is one sequential multi-sector write, the fastest pattern for an SD card: a 509-sector set
   publishes in about 60 ms.
2. **No blocking while playing.** SdFat's file calls wait while the card programs, which stalls the main loop; the
   engine's background work runs in grains of about 800 µs at most, so a 4–8 ms stall would push MIDI timing. Sector
   writes are started and then polled (`CardIo::ready` / `libraryReady`: the card's busy line), never waited for. A
   milestone saved while playing kept the worst grain at 239 µs.
3. **Power loss.** Creating, growing or renaming a FAT / exFAT file is several separate metadata updates; a power cut
   between them can leave a half-written file, a damaged directory entry or lost clusters. In a fixed block the
   firmware orders every write itself and keeps two of anything that is replaced (below), each with a CRC: a torn write
   is detected and ignored, and the previous version stays valid. Nothing is ever half-published.
4. **A full card is predictable.** The space is reserved up front, so no write can run out of room halfway. The size
   of a save is computed exactly before it starts; one that does not fit is refused by name (`LIBRARY FULL`) before
   anything is written.
5. **No fragmentation or slowdown.** Thousands of small files fragment the card and slow directory lookups as folders
   grow; a fixed block's layout never changes.

The costs, accepted for a live instrument: the space is taken at once (1 GiB for the library, even when nearly empty);
on a computer the card shows one opaque file each, not browsable sets (an export would need a tool or a command); the
limits are fixed (4,096 milestones, a 2.1 MB set) and changing them is a new format version; and the library's log only
appends, so emptying the trash removes milestones from the library but does not free their space (below), unless
compaction is added later.

## Making a write safe: the publishing order

Anything that replaces something keeps two copies (A / B) and writes the copy that is **not** current; a small header
with a sequence number and a CRC is written **last**, and only once the data before it is programmed on the card. At
boot the newer valid copy wins. So a write cut off at any point leaves the previous version current.

- **Slot store:** a save writes the slot's new runs (each its own record, header last), then the slot's state record
  into its inactive copy, header last (DEC-254). Runs already on the card are not written again.
- **Milestone:** the set's data (the blob) is appended to the log, then its 64 B index record, then the index header
  (A / B) that counts it. Until the header is programmed the milestone does not exist; a failed write's space is
  simply reused by the next one (DEC-258).
- **Checkpoint:** written into the checkpoint region that is not current, its header last (DEC-258).
- **Working set:** named in the index header (DEC-269). A save names its set and the new milestone; a load or return
  writes only a new index header (A / B, no record). A cut-off write leaves the previous header, and the working set it
  names, current. The name travels with the card.

## The store file inside (DEC-254)

| Area | Sectors | What it holds |
|---|---|---|
| Surface state | 0–1 | the selected track, each track's selected slot, the muted tracks |
| State records | 2 per slot | per slot copies A / B: a header (`MLC1`, sequence, size, CRC), then the slot's states without rows — the slot stream's header, its run directory (each run's kind, rows and record sector), every undo / redo state; after it a cold section (`MLK1`) listing the slot's cold undo states |
| Run area | 2,097,152 (1 GiB) | run records in 4 KiB clusters: a header sector (`MLR1`: slot, kind, rows, the rows' CRC-32, a check word) written last, then the rows, 32 a sector |
| Cold records | reserved at the end of the run area | per slot and copy, the cold states' list (sequence and CRC tied to its copy) |

**Each run once.** A take, an overdub pass or an edit's result is a *run*. It is written to the run area once, the
first time a save finds it not on the card; every later state that lists it — and an overdub's chain lists the take
again and again — names its record sector only. A save of a slot therefore writes only what is new since the last one
plus the slot's small state record. The run area's space map is rebuilt at boot from both valid copies of every slot
(`ClusterMap`); a run no copy lists any more is free again from that boot on.

**Grains.** A save is cut into grains of one sector (each started only when the card is ready, never waited for); the
worst persistence grain measured while playing is under 0.4 ms (all engine timing: [ENGINE_TIMING.md](ENGINE_TIMING.md)). A load of an older undo step from the card (below)
reads at most four sectors a grain.

**File names (owner 2026-11-11).** The run-once store is `store.bin`; the DEC-253 store is kept as `store.bak`. The
boot renames them once (`Persistence::nameStores`): a card that has the interim `store2.bin` renames `store.bin` to
`store.bak` and then `store2.bin` to `store.bin`; a card with only the old store under `store.bin` renames it to
`store.bak` and migrates from it. Nothing is renamed over an existing file and nothing is deleted. The boot reports it
as `renamed=` in `!ENGINE take`'s `#ENGINE,runstore` line.

**Migration.** With no `store.bin` but a `store.bak`, the boot reads the slots from the old store (read only), then
writes every slot into a new run-once store. `store.bak` is never written.

## Undo on the card (DEC-254)

Each slot keeps up to **99 undo states of its own** (`kSlotDepth`); its 100th forgets that slot's own oldest step,
never another slot's. The states live in two tiers:

- **Hot** — in PSRAM, a shared table of 99 entries across all slots (the current states included). Undo and redo
  among hot states are instant.
- **Cold** — a descriptor in PSRAM only (revision, loop length, and per run its record sector), with the rows on the
  card. When the hot table is full, the oldest undo step anywhere is *demoted* to cold instead of forgotten — when every
  run it lists is already on the card; an unsaved one is forgotten as before. The same happens, one step every 50 ms at
  most, when the content pool runs low (below 1/8 of its pages). Redo steps, a slot's current state and a session's
  protected start are never demoted.

The UNDO level counts hot and cold steps together. An undo past a slot's oldest hot step *fetches* the cold step: the
slot keeps playing its current state while its runs are read (runs already in PSRAM are reused; the rows' CRC is
checked), then it switches to that step. More presses meanwhile move the target deeper; only the last target is
switched to. A damaged record refuses the fetch by name and the slot stays where it was; a clear or set load on the slot
cancels it; a commit meanwhile keeps the fetched steps without switching.

Cold states are saved in the slot's state record (its cold section) and come back at boot as descriptors; the boot
reads only hot states' rows. Milestones (the library) carry each slot's hot states only.

Diagnostics: `!ENGINE history` (`history_cold`: cold steps per slot), `!ENGINE take` (`#ENGINE,cold`: demoted,
pressure; `#ENGINE,fetch`; `#ENGINE,runstore`; `#ENGINE,grain_worst` per job class).

## The library file inside

| Area | Sectors | What it holds |
|---|---|---|
| Index header A / B | 0–1 | count, where the next milestone goes, the next sequence number |
| Index records | 2–513 | 4,096 records of 64 B: sequence, set, stage and stage number (`SET 2 T01` on screen), parent, wall time, tempo, loop count, shortest / longest loop, place in the log, a 2-bit-per-slot mosaic |
| Checkpoint regions A / B | 2 × 4,301 | the recovery checkpoint, each region room for the largest set |
| Milestone log | 2,088,036 | the milestones, appended |

A milestone is the whole set at the instant it was asked for (every slot's history pinned until the write ends; later
changes are not in it): a header sector (tempo, surface state, note-id high-water mark, each slot's byte size), then
each slot's file in the store's format — its full undo history and each run's rows once — each starting on a new
sector. The largest set is 4,300 sectors (a compile-time check); the log holds at least 485 of those, and about 4,100
of a typical 509-sector set, so the 4,096-record index is usually the limit reached first.

## One card stream

The store and the library share the card's single write stream. While a milestone or checkpoint writes, no slot
autosave starts (it follows when the milestone is done); a slot save already on the card finishes first
(`Persistence::onCard`). A fetch of a cold undo step runs after any save in progress, and no save starts during it. The library file is created only with the transport stopped, saving approved and no take open
— at boot or right after `!ENGINE approve-saving` — because preallocating it is file-system work.

## Loading and returning (DEC-259)

A load replaces the whole working set with a milestone, undo histories included, and is not an undo step. It first
writes the working set as the recovery checkpoint (if that fails, nothing changes), then clears every slot's history
and reads the milestone. If reading fails, the checkpoint is loaded back. `return` loads the checkpoint again and writes
nothing. A slot the load left empty is written to the store as an empty file, so the next boot does not bring the old
content back. While playing (DEC-260), the load writes the checkpoint as the set plays on, then waits for the global switch point —
the next bar, or the selected track's loop end (`!ENGINE switch-at bar|loop`). There the playing tracks stop, the set is
replaced, and they resume together at the next bar line on the loaded set's selected slots: a gap of about a bar.
To avoid the gap (DEC-261), each playing track's destination slot is first *staged*: its current state is loaded beside
the playing set and composed ahead, so at the switch point that track takes it at position 0 without stopping; the
full load then adopts that staged state as the slot's current one, so the track plays on untouched. Only when staging does not fit does a track take
the gap. `!ENGINE switch-stage 0|1` turns staging off and on.

## The trash (DEC-268)

Trashing, restoring and emptying rewrite nothing. Each is one more index record without a blob: a *trash* record
names the milestone it moves to the trash, or, with no target, every shown milestone of its set published before it
(one item); a *restore* record takes a trash record's item back out; an *empty-trash* record removes everything in the
trash for good. They are published as a milestone's record is (the record, then the index header, each programmed
first), so a cut-off one simply did not happen. At boot the records are replayed in order. A trashed milestone is still
on the card and loads again once restored. An emptied one's blob stays in the log and its space is not reused (that
would need compaction); its stage and set numbers are not reused either. Each of these uses one of the 4,096 index
records.

## The working set across a reboot (DEC-269)

The working set (the set the board plays and saves into, and the stage it descends from) is named in the index header,
in a block after the header's own CRC with a CRC of its own. A boot starts in the set it names, even when that set has
no shown stage left (all trashed or emptied); the base stage is kept only while it is shown in that set. A card whose
header names none (written by older firmware, or with a damaged block) falls back to the newest shown stage's set. A
load or return names it once the library is free (`#ENGINE,working,begin|done|failed`); the boot reports
`#ENGINE,working,boot,set=…,base=…,named=0|1`.

## Serial commands (testing)

`!ENGINE library` (state), `library-open`, `milestone <S|B|E|T|A|F> [set]`, `milestones` (list), `checkpoint`,
`load <index>`, `return`, `get <index> <T-S> <T-S>` (a loop from a stage, DEC-270), `trash <set> [index]`, `restore <index>`, `empty-trash`, `switch-at bar|loop`, `switch-stage 0|1`. Refusals: `STOP THE TAKE FIRST`, `CLOSE THE EDIT FIRST`, `LIBRARY NOT READY`,
`LIBRARY FULL`, `BUSY`. On the DROID the same actions run from the sets browser (HOLD TRACK; DEC-263, redesigned in DEC-267); `return` is serial-only.
