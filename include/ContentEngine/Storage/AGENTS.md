# Storage — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Storage in the decision log. The on-card layout and
the undo store are described for people in `docs/Guides/ENGINE_STORAGE.md`.

## Owns
- `Persistence`: approval, autosave, the run-once store (`store.bin`), the surface state and settings sectors, boot
  load, migration, the cold tier and fetches; bodies in `PersistenceSaving.h` (saving, cold tier, fetch) and
  `PersistenceLoading.h` (loading). `RunStore`: the store's run area. `SlotFile`: the slot stream format
  (save / load jobs). `SetLibraryFormat`, `LibraryWriter`, `LibraryLoader`: the milestone library (`library.bin`),
  loads, get a loop. `V1Import`: reading V1 slot files.

## Invariants
- Nothing is written before the owner approves saving; the folder's existence is the remembered approval.
- No store write waits for the card: a grain writes only when the card is `ready` for that sector, and a save is
  complete only when the card is idle again. No file-system work after boot — sector I/O on preallocated files only
  (a file-system operation costs 4–8 ms).
- Each run is written once, as its own record; a record's header is written last, so a cut-short record fails its
  check and is never taken. A slot's state record has two copies; a save writes the one that is not current.
- Boot renames only (`nameStores`): never over an existing file, never a delete. `store.bak` (the two-copy store,
  DEC-253) is read once to migrate, never written.
- A new field in a state or settings sector carries its own check word, so zeros in an older sector read as "none".
- V1 files are read only, never written. Slot file versions: 3 fit run (DEC-257), 4 fit lookups by position
  (DEC-285), 5 groove pass (DEC-287); a version-3 fit lookup is forgiven, not loaded.
- A milestone's size is checked before anything is written; a failed write leaves the current header and checkpoint.
- A get-a-loop read takes preview runs from the preview partition only: it must not take what live takes rely on.

## Traps
- Device SD content is the owner's data: no delete, rename or quarantine on the card without explicit permission in
  the session (root AGENTS.md).
- Don't trust a guide's byte count for the V1 epoch header: it is 10 bytes, written field by field.

## Decisions
- DEC-253 (preallocated store) · DEC-254 (run-once store, per-slot undo) · DEC-258 (library file) ·
  DEC-259 (whole-set load) · DEC-260 / 261 (live load, gapless switch) · DEC-268 (trash) ·
  DEC-269 (named working set) · DEC-285 (fit run by position) · DEC-293 (mutes survive a reboot)
