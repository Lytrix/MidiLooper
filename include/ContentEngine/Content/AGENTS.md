# Content — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Content in the decision log.

## Owns
- The committed source: 16-byte rows (`PassEvent`) on fixed pool pages (`PagePool`), runs (`RunTable`), state
  manifests (`ManifestTable`), row reservations (`RowReservations`), the take writer (`CaptureWriter`), reclaim
  (`ReclaimJob`), the channel-message classes (`MidiClass`).

## Invariants
- No allocation; every `init()` writes every field it reads: RAM2 and EXTMEM are not zeroed at reset, and PSRAM
  objects are not constructed.
- Committed source is lossless (DEC-245): `seq` and `origin` survive compaction; meaning is derived at composition.
- A run never spans more than one loop cycle; a held note's NoteOff stays in its originating run.
- An owed row is never refused (invariant R): a NoteOn reserves its NoteOff in the same run.
- Page conservation is by owner tag: each page is free or held by exactly one holder.
- Pages return through bounded `reclaim` calls only, never on the Clock path or inside a command.
- Preview runs (get-a-loop audition) take pages from their own partition only; content never takes its pages.
- Undefined MIDI statuses 0xF4–0xFE mark hide and fit-run rows; they are never sent.

## Traps
- Don't share a manifest identity between a state and its rebuild view: the edit-hide index cache is keyed by
  identity, and a shared one serves one view's hides to the other (a deleted note comes back). DEC-286.
- Don't limit essential (bank, program, CC64–69, CC120–127) or parameter (RPN / NRPN) messages: only continuous
  values are limited per tick (`MidiClass`).

## Decisions
- DEC-244 (equal-tick order) · DEC-245 (lossless committed source) · DEC-252 (bounded content engine) ·
  DEC-255 (muted note bit) · DEC-257 (loop length is a play window) · DEC-262 (late-Stop hide) ·
  DEC-286 (rebuild view identity)
