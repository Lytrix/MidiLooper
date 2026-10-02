# Can 64 slots each hold 64 bars at 3 notes per 16th? (2026-10-02, read-only)

**The owner's requirement:** every one of the 64 slots (8 tracks x 8 loops, `include/Globals.h:65-66`) minimally able
to hold **64 bars of content at 3 notes per 16th note**.

**The answer, in one line: no — not by 6-7x, and the first wall is not bytes but the chunk-id space. The current
change does not solve it, and its own documents already say so.**

## The target, in events

64 bars x 16 sixteenths = 1,024 sixteenth positions; x 3 notes = 3,072 note **onsets**; a note is an On and an Off,
so **6,144 events per slot**; x 64 slots = **393,216 events**.

## The arithmetic (VERIFIED unless labelled)

| Term | KiB | source |
|---|---|---|
| boot fixed (`live` empty / settled) | 1,801 / 1,949 | device rows (`[Memory] ext` at boot) |
| **raw committed payload** 1,536 chunks x 5,132 B | **7,698** | `CHUNK_CAPACITY = 256`, `sizeof(EventChunk) = 5,132` (measured on the target ABI) |
| snapshot representation (floor .. measured-root anchor) | **23,040 .. 73,920** | DERIVED from the structure: 1 `LoopSnapshotNote` + **2** `LoopSnapshotEvent` + 3 membership refs per note |
| per-slot shells (6,888 B x 64) | 430 | `LoopPlaybackRuntime` 6,352 B + `Loop` 536 B per slot |
| view-set objects (33,024 B x 64) | 2,064 | `sizeof(RuntimePlaybackViewSet)` measured |
| view arrays x 64, today's windowed P | 21,184 | `S2C23-CLASS-BUDGET.md` |
| reserves | 120 | `CHUNK_RESERVE = 16` |
| **total** | **56,338** | vs an 8,192 KiB pool -> **over by 48,146 KiB** |

## Three independent blockers (each one alone is fatal)

1. **The chunk-id ceiling, not bytes.** 393,216 events needs **1,536 chunks** against
   `POOL_CHUNK_COUNT = 512` (`include/LoopEventStore.h:18`) = **3.0x**. The id space is a structural limit; the
   ceiling is already quoted as binding in `tasks.md:554`.
2. **The raw committed payload alone is 94% of the pool** (7,698 of 8,192 KiB) and 1,455 KiB over the 6,243 KiB free
   after boot. No sharing of views, staging or snapshots can recover that.
3. **The snapshot representation duplicates the committed events.** `LoopSnapshotEvent` carries a **full
   `MidiEvent`** (`include/LoopSnapshot.h:117-122`) and the publisher creates **two** event nodes per note
   (`src/Loop/LoopSnapshotPublish.cpp:390-434`), so the term is 2.5-9x the pool on its own.

Dropping *every* view and snapshot term still leaves ~11,900 KiB. **The requirement cannot be reached by budget
discipline; it needs the data to be smaller.**

## What the current change does and does not reduce

**Reduces:** the 8-chunk-per-committed-slot canonical-copy reservation and boot staging
(`src/Track/TrackPlaybackWindowBuild.cpp:620-640`), lazy chunk backing, and it **derives** the windowed P
(331 -> 28.6 KiB, `S2C1-PLAYBACK-WINDOW-LEASE.md`).

**Does not reduce:** the committed event bodies — `S2C23-CLASS-BUDGET.md:43,113` puts "all resident event bodies"
under `canonical` until S6, which *is* the 7,698 KiB term; the 512-chunk / 131,072-event ceiling; whole-loop
playback/checkpoint roots (~325 KB per loop until phase F, `S2C23:114`); and C 383 KiB / D 308 KiB
(`S2C23:48-49`).

**And the mechanism that would matter most is not here.** Windowed publication is the term that makes per-slot cost
length-independent, and it is **extracted to a sibling change that does not yet exist**:
`design.md:812` — *"S2.9 is extracted to a sibling change, `windowed-playback-generation`"* — and the S4c gate was
closed with **no claim of shipped windowed publication**. `kEventsPerView = 2048` is still a hard `constexpr`
(`include/RuntimePlaybackView.h:20`).

The change's own budget document already admits the outcome: *"sum 12,626 KiB vs 8,192 -> **OVER by 4,434 KiB**"* and
*"cannot fit **at any N**"* (`S2C23-CLASS-BUDGET.md:72,77-78`).

## What the repository already claims about capacity

- `proposal.md:8`: *"The target is **~60 recorded slots at up to 64 bars** (envelope: 8 tracks x 8 slots = 64)"* —
  the requirement is the proposal's own.
- `proposal.md:11-13` (DERIVED reading): the pool supports **four** resident slot bundles.
- `design.md:267`, `tasks.md:4391`, `CURRENT_WORK.md:6013`: the resident event store holds *"roughly 28
  sixty-four-bar loops"* — at the **observed** density of **~4,600 events** per loop, i.e. **~21 slots at the
  owner's 6,144-event density**, not 64.
- The only affirmative "64 slots at 64 bars" statement in the repo is a **1 B/bar visual mask** (~8-16 KB,
  `design.md:492-494`) — metadata, not content.

So the repo's own numbers say the requirement is ~3x beyond today's residency, before any snapshot term is added.

## Levers, with numbers (each as a delta from 56,338 KiB)

| lever | delta | alone? |
|---|---|---|
| (i) windowed publication (S2.9, **not landed**) | **-19,397** | no — still 28,749 over |
| (ii) drop the `MidiEvent` SysEx pointer (20 -> 16 B committed, 35 -> 31 B/event views) | **-5,632** (11.7% of the gap) | no |
| (iii) **stop the snapshot duplicating committed events** (`LoopSnapshotEvent.value` = full `MidiEvent`; 2 event nodes per note) | **-23,040 .. -73,920** | **the only single lever of the right order** |
| (iv) a compact committed format + derived note-offs | **-6,153** | no |
| (i)+(ii)+(iii)+(iv) | **7,543 total** (649 KiB margin) | reachable **only with all four** |

(ii) is capability-neutral in practice (ingress has no SysEx case — `MidiHandler.cpp:402-432`; persistence requires a
null pointer — `StorageLoopIoBodyFormatV1.cpp:176-181`; `MidiEvent::SysEx` has no `src/` caller), so it costs a
latent capability, not a used one. (iii) is named in the repo's own falsification record
(`FALSIFICATION.md:100-125`) as belonging to "S2's scope or in its own gated phase".

## What is owed (a decision, not a patch)

1. **The chunk-id ceiling must be re-opened as a design question** — `POOL_CHUNK_COUNT = 512` (131,072 events) cannot
   hold 393,216 events at any byte budget.
2. **Snapshot de-duplication** (lever iii) is the only single lever of the right order and is the one the repo already
   flagged for "S2's scope or its own gated phase".
3. **Windowed publication must actually land** (S2.9 -> `windowed-playback-generation`) for the per-slot term to stop
   scaling with content.
4. **A supported-density claim needs to be stated and defended** — e.g. "N slots at D events/slot" — because today the
   only numbers in the repo are ~28 loops at the observed density and ~21 at the target density, against a proposal
   target of ~60 slots at up to 64 bars.

**Provenance.** The sweep that produced these numbers was read-only. The load-bearing citations were re-verified
directly: `CHUNK_CAPACITY`/`POOL_CHUNK_COUNT` (`LoopEventStore.h:17-18`), `kSnapshotSlabBytes = 32 * 1024`
(`LoopSnapshot.h:50`), `LoopSnapshotEvent` carrying `MidiEvent value` (`LoopSnapshot.h:117-122`),
`kEventsPerView = 2048` (`RuntimePlaybackView.h:20`), `proposal.md:8`, `design.md:267`, `design.md:812`, and the
`S2C23` figures quoted. Labels: VERIFIED = a cited line or a device row; DERIVED = arithmetic over cited numbers;
UNKNOWN = the snapshot term's exact bytes (its structure is exact, its size is bounded from the repo's own
14k-node/450 KB anchor).
