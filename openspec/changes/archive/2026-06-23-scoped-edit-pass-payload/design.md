## Context

Note **editPass** rows carry **EditPassType** / **EditActionType** / **EditPropertyType** plus a
parallel **EditChangeList**. Materialize, session undo, and SD I/O maintain two representations.

Docs used **payload** as a generic blob term — avoid that word. Stored edits are **NoteRef**
targets plus the **MIDI note fields** that changed (tick, pitch, velocity) or a **note on/off**
pair for **Create**.

## Goals / Non-Goals

**Goals:**

- Single stored row shape on **EditPass**; remove **EditChange** from firmware.
- **Less is more:** each row carries **target** + **propertyType** + **only the fields that apply**.
- **Create** / **Delete:** **NoteRef** or embedded **note on** + **note off** **MidiEvent** pair.
- **Update** variants:
  - **move** → **NoteRange** (**target** + `startTick` + `endTick`)
  - **length** → **Length** (**target** + `startTick` + `endTick`; end-only change today)
  - **pitch** → **Pitch** (**target** + pitch)
  - **velocity** → **Velocity** (**target** + **note on** velocity)
- SD **v5** only: canonical **editPass** row wire on read and write; **no** legacy tail read, **no**
  **EditChange** load shim (see design D7).
- Session undo uses the same row shape (no **EditChange** adapters).

**Non-goals:**

- CC edit UI/apply.
- SD format **version bump** to **v5** (reject v1–v4 on load; firmware starts empty — existing
  `loadState` failure path).
- Tick **delta** encoding on SD (deferred — m8-edit §6.1 compaction); first migration uses
  absolute ticks relative to baseline **NoteRef** (same bytes as today, clearer apply).

## Decisions

### D1 — No generic payload; MIDI field vocabulary

**Choice:** Stored note edit rows use **NoteRef** + typed fields. Do not introduce a **payload**
struct or blob.

```cpp
// On EditPass (note rows) — fields used per propertyType
NoteRef target;
uint32_t startTick = 0;  // NoteRange, Length, Create
uint32_t endTick = 0;    // NoteRange, Length, Create
uint8_t pitch = 0;       // Pitch
uint8_t velocity = 0;    // Velocity
MidiEventVec addedEvents; // Create: note on + note off
```

**Rationale:** A note in this system is bounded by **note on**, **note off**, **pitch**, **velocity**
(channel on **NoteRef**). Edits name which of those changed — same domain as capture storage.

### D2 — Move vs length: same tick fields, different propertyType

| User edit | actionType | propertyType | Stored fields |
|-----------|------------|--------------|---------------|
| Add | Create | None | `addedEvents` (on+off) |
| Delete | Delete | None | `target` |
| Move | Update | **NoteRange** | `target`, `startTick`, `endTick` |
| Length | Update | **Length** | `target`, `startTick`, `endTick` |
| Pitch | Update | **Pitch** | `target`, `pitch` |
| Velocity | Update | **Velocity** | `target`, `velocity` |

**Rationale:** Move and length both bound the note by **note on** / **note off** ticks. Storing
`startTick` and `endTick` on **Length** rows prepares for start-point length edit (head trim /
lengthen from start) without a data-model change later. **propertyType** still routes apply:
**NoteRange** uses move semantics; **Length** uses length/overlap shorten-restore semantics. Today
length commits typically change `endTick` only; `startTick` is still written (current span, often
equal to baseline start).

**Wire size:** two `uint32_t` ticks on length rows — same as legacy **ChangeLength** rows that
already carry baseline identity plus new end; adding stored `startTick` is one extra field for
future UI, not a second framework.

### D3 — Absolute ticks first; delta compaction later

**Choice:** Store **absolute** tick values on the row (as **EditChange** does today for
`newStartTick` / `newEndTick`). **NoteRef** on the row remains the **baseline** identity before
apply.

**Deferred (m8-edit §6.1):** SD compaction may store tick **deltas** relative to `target` for smaller
tails on long loops. That is an encoding optimization, not a second logical model. Apply still
needs baseline **NoteRef** either way.

**Rationale:** Deltas do not remove the need for a target; they only shrink wire size. Absolute
ticks are simpler for overlap/length apply and match live **NoteEditFocus.commitBaseline**.

### D4 — Add **Velocity** to **EditPropertyType**

**Choice:** Extend **EditPropertyType** with **Velocity**. Velocity edit updates **note on**
velocity; **note off** velocity stays 0 per existing capture convention.

### D5 — Single apply path

One note-row apply per **EditPass**; remove **applyEditChangeList** and all **EditChange** RAM types.

### D6 — CC parked

After **long-loop-piano-roll-window**. CC rows use **ControlChangeRef** + **Tick** / **Value** —
same “target + changed field” pattern, not a blob.

### D7 — SD v5: canonical edit wire only; reject older state files

**Choice:** Bump **STORAGE_VERSION** to **5**. **loadState** accepts **only** version **5**. Versions
**1–4** are **unsupported** — load fails, firmware keeps default empty in-RAM state (existing
`Looper::setup` path). **No** dual-read, **no** edit-tail migration, **no** **EditChange** load shim.

**v5 editPasses tail** (within the loop-pool block) reads and writes **only** the canonical row wire:

- **EditPassType**, **EditActionType**, **EditPropertyType**, **EditPassState**, batch index
- **NoteRef** **target** + property fields (`startTick`, `endTick`, `pitch`, `velocity`) or
  **Create** note on/off pair
- **No** **EditChange** / **EditChangeType** on disk

Delete from firmware when this change ships:

- `readPersistedEditPassLegacyV4`
- transitional `EPT2` + **EditChange** scoped read/write
- `readPersistedEditChange` / `writePersistedEditChange` on the edit-pass path
- `deriveActionTypeFromLegacyChanges` / `derivePropertyTypeFromLegacyChanges` SD helpers

**Rationale:** User requirement — single on-disk model only; old SD state is discarded rather than
migrated. Re-record loops after upgrade.

**Out of scope:** chunk-ID-on-disk, chunked capture wire refactor (deferred runtime persistence
track).

## Risks

- **EditPropertyType::NoteRange** is a new enum value (user-approved scope — move is not Length).
- **v5** rejects all prior SD files — user must re-record; document in release notes.
- Velocity edit needs HITL + native coverage when UI ships.

## Open Questions

- None for length storage shape — **Length** rows use `startTick` + `endTick` (locked 2026-06-23).
