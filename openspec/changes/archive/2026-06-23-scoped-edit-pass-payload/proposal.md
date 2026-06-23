## Why

`scoped-edit-pass-model` shipped scoped metadata on **editPass** rows, but note rows still use
legacy **EditChange** lists. Dual paths block deleting **EditChange**. Stored rows should use
**NoteRef** + **EditPropertyType** + MIDI note fields — not a generic **payload** blob.

## What Changes

- Replace **EditChangeList** with per-row **NoteRef** target + **propertyType** + only the fields
  that changed (ticks, pitch, velocity, or **note on/off** pair for **Create**).
- **Move** → **Update** + **NoteRange** (`startTick` + `endTick`); **length** → **Length**
  (`startTick` + `endTick`; end-only today, start-point length later).
- Add **Velocity** to **EditPropertyType**.
- Bump **STORAGE_VERSION** to **5**; reject v1–v4 (empty start). Canonical **editPass** row wire only
  on read/write — no **EditChange** on disk or in firmware. Tick **delta** encoding deferred
  (m8-edit §6.1).

## Sequencing

- After **`edit-session-state`** (**EditPassType** rename).
- Before CC edit.
