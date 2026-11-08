# Engine storage: fixed blocks on the card

How the fixed-memory engine keeps its data on the SD card, and why it uses preallocated fixed blocks rather than one
file per slot or per milestone. Authority: **DEC-253 — the A / B slot store** and **DEC-258 — the milestone library**
in [`DECISION_LOG.md`](../DECISION_LOG.md); this guide explains them and does not override them. For the legacy
runtime's storage see [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](RUNTIME_STORAGE_AND_PERSISTENCE.md); for the engine as a
whole, [`FIXED_MEMORY_ENGINE.md`](FIXED_MEMORY_ENGINE.md).

## Two files, both fixed blocks

| File | Size | Holds | Source |
|---|---|---|---|
| `/MidiLooper/engine/store.bin` | fixed (two copies per slot + 2 sectors) | the autosave: each slot's current file in two alternating copies, and the surface state (selected track and slots) | `Persistence.h`, `SetFile.h` |
| `/MidiLooper/library/library.bin` | 1 GiB | milestones (saved versions of the whole set) and the one recovery checkpoint | `Library.h`, `LibraryWriter.h` |

Each file is created once, **preallocated in one contiguous run**, and after that is only read and written by card
sector address (`SdCardIo`): no directory lookup, no allocation, no file-system flush. The file system is used only to
create and find the two files.

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
appends, so removing a milestone would hide it, not free its space, unless compaction is added later.

## Making a write safe: the publishing order

Anything that replaces something keeps two copies (A / B) and writes the copy that is **not** current; a small header
with a sequence number and a CRC is written **last**, and only once the data before it is programmed on the card. At
boot the newer valid copy wins. So a write cut off at any point leaves the previous version current.

- **Slot store:** a slot's body goes into its inactive copy, then that copy's header (DEC-253).
- **Milestone:** the set's data (the blob) is appended to the log, then its 64 B index record, then the index header
  (A / B) that counts it. Until the header is programmed the milestone does not exist; a failed write's space is
  simply reused by the next one (DEC-258).
- **Checkpoint:** written into the checkpoint region that is not current, its header last (DEC-258).

## The library file inside

| Area | Sectors | What it holds |
|---|---|---|
| Index header A / B | 0–1 | count, where the next milestone goes, the next sequence number |
| Index records | 2–513 | 4,096 records of 64 B: sequence, set, stage and stage number (`SET 02 T01`), parent, wall time, tempo, loop count, shortest / longest loop, place in the log, a 2-bit-per-slot mosaic |
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
(`Persistence::onCard`). The library file is created only with the transport stopped, saving approved and no take open
— at boot or right after `!ENGINE approve-saving` — because preallocating it is file-system work.

## Loading and returning (DEC-259)

A load replaces the whole working set with a milestone, undo histories included, and is not an undo step. It first
writes the working set as the recovery checkpoint (if that fails, nothing changes), then clears every slot's history
and reads the milestone. If reading fails, the checkpoint is loaded back. `return` loads the checkpoint again and writes
nothing. A slot the load left empty is written to the store as an empty file, so the next boot does not bring the old
content back. Slice 2a loads only while stopped; live switching is 2b.

## Serial commands (testing)

`!ENGINE library` (state), `library-open`, `milestone <S|B|E|T|A|F> [set]`, `milestones` (list), `checkpoint`,
`load <index>`, `return`. Refusals: `STOP FIRST`, `STOP THE TAKE FIRST`, `CLOSE THE EDIT FIRST`, `LIBRARY NOT READY`,
`LIBRARY FULL`, `BUSY`. The browser screen and live switching are later slices of the [sets browser proposal](../Plans/engine_sets_browser_proposal.md).
